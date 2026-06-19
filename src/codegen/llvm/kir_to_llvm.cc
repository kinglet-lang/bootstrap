// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "codegen/llvm/kir_to_llvm.h"

#include "ir/kir.h"
#include "ir/kir_numeric.h"
#include "ir/kir_specialize.h"
#include "ir/kir_typing.h"

#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/SHA256.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#if LLVM_VERSION_MAJOR >= 16
#include <llvm/TargetParser/Host.h>
#else
#include <llvm/Support/Host.h>
#endif

#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#include "module/native_symbol.h"

namespace kinglet {

namespace {

std::vector<const KirInstr *> linear_instrs(const KirFunction &fn) {
  std::vector<const KirInstr *> out;
  for (const KirBasicBlock &bb : fn.blocks) {
    for (const KirInstr &instr : bb.instrs) {
      out.push_back(&instr);
    }
  }
  return out;
}

int max_local_slot(const KirFunction &fn) {
  int max_slot = fn.param_count - 1;
  for (const KirBasicBlock &bb : fn.blocks) {
    for (const KirInstr &instr : bb.instrs) {
      if (instr.op == KirOpcode::LoadLocal || instr.op == KirOpcode::StoreLocal) {
        if (!instr.operands.empty()) {
          max_slot = std::max(max_slot, instr.operands[0]);
        }
      }
    }
  }
  return std::max(max_slot, 0);
}

std::string g_lower_context;

llvm::Value *pop_value(std::vector<llvm::Value *> *stack, std::string *error,
                       std::vector<KirType> *type_stack = nullptr) {
  if (stack->empty()) {
    if (g_lower_context.empty()) {
      *error = "native lowering stack underflow";
    } else {
      *error = "native lowering stack underflow in " + g_lower_context;
    }
    return nullptr;
  }
  llvm::Value *value = stack->back();
  stack->pop_back();
  if (type_stack != nullptr && !type_stack->empty()) {
    type_stack->pop_back();
  }
  return value;
}

llvm::Type *stack_llvm_type(llvm::LLVMContext &ctx, KirType type) {
  return type == KirType::Float ? llvm::Type::getDoubleTy(ctx) : llvm::Type::getInt64Ty(ctx);
}

llvm::Value *const_double(llvm::IRBuilder<> &builder, const KirInstr *instr) {
  int64_t bits = 0;
  const int32_t low = instr->operands[0];
  const int32_t high = instr->operands.size() > 1 ? instr->operands[1] : (low < 0 ? -1 : 0);
  bits = (static_cast<uint64_t>(static_cast<uint32_t>(high)) << 32) |
         static_cast<uint32_t>(low);
  double value = 0.0;
  std::memcpy(&value, &bits, sizeof(value));
  return llvm::ConstantFP::get(builder.getDoubleTy(), value);
}

struct RtFns;
llvm::Value *to_wire_i64(llvm::IRBuilder<> &builder, const RtFns &rt, llvm::Value *value,
                         KirType type);

struct RtFns {
  llvm::Function *string_new = nullptr;
  llvm::Function *array_new = nullptr;
  llvm::Function *dense_array_new = nullptr;
  llvm::Function *dense2d_get = nullptr;
  llvm::Function *array_get = nullptr;
  llvm::Function *value_len = nullptr;
  llvm::Function *struct_new = nullptr;
  llvm::Function *struct_type_index = nullptr;
  llvm::Function *struct_field_at = nullptr;
  llvm::Function *struct_field_set = nullptr;
  llvm::Function *slice = nullptr;
  llvm::Function *enum_new = nullptr;
  llvm::Function *enum_new_payload = nullptr;
  llvm::Function *enum_payload_at = nullptr;
  llvm::Function *cast_to_int = nullptr;
  llvm::Function *cast_to_float = nullptr;
  llvm::Function *cast_to_string = nullptr;
  llvm::Function *cast_to_char = nullptr;
  llvm::Function *native_out = nullptr;
  llvm::Function *native_out_ln = nullptr;
  llvm::Function *native_err = nullptr;
  llvm::Function *native_err_ln = nullptr;
  llvm::Function *native_in = nullptr;
  llvm::Function *native_fs_read = nullptr;
  llvm::Function *native_fs_write = nullptr;
  llvm::Function *native_sys_args = nullptr;
  llvm::Function *value_eq = nullptr;
  llvm::Function *value_is_err = nullptr;
  llvm::Function *exit_code = nullptr;
  llvm::Function *float_from_bits = nullptr;
  llvm::Function *float_new = nullptr;
  llvm::Function *float_get = nullptr;
  llvm::Function *float_to_bits = nullptr;
  llvm::Function *bool_to_string = nullptr;
  llvm::Function *null_to_string = nullptr;
  llvm::Function *value_add = nullptr;
  llvm::Function *value_sub = nullptr;
  llvm::Function *value_mul = nullptr;
  llvm::Function *value_div = nullptr;
  llvm::Function *value_mod = nullptr;
  llvm::Function *value_cmp = nullptr;
  llvm::Function *array_push = nullptr;
  llvm::Function *array_resize = nullptr;
  llvm::Function *array_pop = nullptr;
  llvm::Function *array_clear = nullptr;
  llvm::Function *array_insert = nullptr;
  llvm::Function *array_reverse = nullptr;
  llvm::Function *contains = nullptr;
  llvm::Function *index_of = nullptr;
  llvm::Function *map_new = nullptr;
  llvm::Function *map_has = nullptr;
  llvm::Function *map_keys = nullptr;
  llvm::Function *index_get = nullptr;
  llvm::Function *index_set = nullptr;
  llvm::Function *remove = nullptr;
  llvm::Function *str_starts_with = nullptr;
  llvm::Function *str_ends_with = nullptr;
  llvm::Function *str_replace = nullptr;
  llvm::Function *str_split = nullptr;
  llvm::Function *str_trim = nullptr;
  llvm::Function *str_to_upper = nullptr;
  llvm::Function *str_to_lower = nullptr;
};

RtFns declare_runtime(llvm::Module *module) {
  llvm::LLVMContext &ctx = module->getContext();
  llvm::Type *i32 = llvm::Type::getInt32Ty(ctx);
  llvm::Type *i64 = llvm::Type::getInt64Ty(ctx);
  llvm::Type *i8p = llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(ctx));
  llvm::Type *i64p = llvm::PointerType::getUnqual(i64);

  RtFns rt;
  rt.string_new = llvm::Function::Create(
      llvm::FunctionType::get(i64, {i8p, i32}, false), llvm::Function::ExternalLinkage,
      "kl_string_new", module);
  rt.array_new = llvm::Function::Create(
      llvm::FunctionType::get(i64, {i32, i64p}, false), llvm::Function::ExternalLinkage,
      "kl_array_new", module);
  rt.dense_array_new = llvm::Function::Create(
      llvm::FunctionType::get(i64, {i32, i32, i64p}, false), llvm::Function::ExternalLinkage,
      "kl_dense_array_new", module);
  rt.dense2d_get = llvm::Function::Create(llvm::FunctionType::get(i64, {i64, i32, i32}, false),
                                          llvm::Function::ExternalLinkage, "kl_dense2d_get", module);
  rt.array_get = llvm::Function::Create(llvm::FunctionType::get(i64, {i64, i32}, false),
                                        llvm::Function::ExternalLinkage, "kl_array_get", module);
  rt.value_len = llvm::Function::Create(llvm::FunctionType::get(i32, {i64}, false),
                                        llvm::Function::ExternalLinkage, "kl_value_len", module);
  rt.struct_new = llvm::Function::Create(
      llvm::FunctionType::get(i64, {i32, i32, i64p}, false), llvm::Function::ExternalLinkage,
      "kl_struct_new", module);
  rt.struct_type_index = llvm::Function::Create(llvm::FunctionType::get(i32, {i64}, false),
                                               llvm::Function::ExternalLinkage,
                                               "kl_struct_type_index", module);
  rt.struct_field_at = llvm::Function::Create(llvm::FunctionType::get(i64, {i64, i32}, false),
                                              llvm::Function::ExternalLinkage,
                                              "kl_struct_field_at", module);
  rt.struct_field_set = llvm::Function::Create(llvm::FunctionType::get(i64, {i64, i32, i64}, false),
                                               llvm::Function::ExternalLinkage,
                                               "kl_struct_field_set", module);
  rt.slice = llvm::Function::Create(llvm::FunctionType::get(i64, {i64, i64, i64}, false),
                                      llvm::Function::ExternalLinkage, "kl_slice", module);
  rt.enum_new = llvm::Function::Create(llvm::FunctionType::get(i64, {i32, i32}, false),
                                       llvm::Function::ExternalLinkage, "kl_enum_new", module);
  rt.enum_new_payload = llvm::Function::Create(
      llvm::FunctionType::get(i64, {i32, i32, i32, i64p}, false), llvm::Function::ExternalLinkage,
      "kl_enum_new_payload", module);
  rt.enum_payload_at = llvm::Function::Create(llvm::FunctionType::get(i64, {i64, i32}, false),
                                              llvm::Function::ExternalLinkage, "kl_enum_payload_at",
                                              module);
  rt.cast_to_int = llvm::Function::Create(llvm::FunctionType::get(i64, {i64}, false),
                                          llvm::Function::ExternalLinkage, "kl_cast_to_int", module);
  rt.cast_to_float = llvm::Function::Create(llvm::FunctionType::get(i64, {i64}, false),
                                            llvm::Function::ExternalLinkage, "kl_cast_to_float", module);
  rt.cast_to_string = llvm::Function::Create(llvm::FunctionType::get(i64, {i64}, false),
                                              llvm::Function::ExternalLinkage, "kl_cast_to_string",
                                              module);
  rt.cast_to_char = llvm::Function::Create(llvm::FunctionType::get(i64, {i64}, false),
                                           llvm::Function::ExternalLinkage, "kl_cast_to_char", module);
  rt.native_out = llvm::Function::Create(llvm::FunctionType::get(i64, {i32, i64p}, false),
                                         llvm::Function::ExternalLinkage, "kl_native_out", module);
  rt.native_out_ln = llvm::Function::Create(llvm::FunctionType::get(i64, {i32, i64p}, false),
                                            llvm::Function::ExternalLinkage, "kl_native_out_ln",
                                            module);
  rt.native_err = llvm::Function::Create(llvm::FunctionType::get(i64, {i32, i64p}, false),
                                         llvm::Function::ExternalLinkage, "kl_native_err", module);
  rt.native_err_ln = llvm::Function::Create(llvm::FunctionType::get(i64, {i32, i64p}, false),
                                           llvm::Function::ExternalLinkage, "kl_native_err_ln",
                                           module);
  rt.native_in = llvm::Function::Create(llvm::FunctionType::get(i64, {i32, i64p, i32}, false),
                                        llvm::Function::ExternalLinkage, "kl_native_in", module);
  rt.native_fs_read = llvm::Function::Create(llvm::FunctionType::get(i64, {i64}, false),
                                             llvm::Function::ExternalLinkage, "kl_native_fs_read",
                                             module);
  rt.native_fs_write = llvm::Function::Create(llvm::FunctionType::get(i64, {i64, i64}, false),
                                              llvm::Function::ExternalLinkage, "kl_native_fs_write",
                                              module);
  rt.native_sys_args = llvm::Function::Create(llvm::FunctionType::get(i64, false),
                                               llvm::Function::ExternalLinkage, "kl_native_sys_args",
                                               module);
  rt.value_eq = llvm::Function::Create(llvm::FunctionType::get(i32, {i64, i64}, false),
                                       llvm::Function::ExternalLinkage, "kl_value_eq", module);
  rt.value_is_err = llvm::Function::Create(llvm::FunctionType::get(i32, {i64}, false),
                                           llvm::Function::ExternalLinkage, "kl_value_is_err", module);
  rt.exit_code = llvm::Function::Create(llvm::FunctionType::get(i32, {i64}, false),
                                        llvm::Function::ExternalLinkage, "kl_exit_code", module);
  rt.float_from_bits = llvm::Function::Create(llvm::FunctionType::get(i64, {i64}, false),
                                              llvm::Function::ExternalLinkage,
                                              "kl_float_from_bits", module);
  rt.float_to_bits = llvm::Function::Create(llvm::FunctionType::get(i64, {i64}, false),
                                            llvm::Function::ExternalLinkage,
                                            "kl_float_to_bits", module);
  llvm::Type *f64 = llvm::Type::getDoubleTy(ctx);
  rt.float_new = llvm::Function::Create(llvm::FunctionType::get(i64, {f64}, false),
                                        llvm::Function::ExternalLinkage, "kl_float_new", module);
  rt.float_get = llvm::Function::Create(llvm::FunctionType::get(f64, {i64}, false),
                                         llvm::Function::ExternalLinkage, "kl_float_get", module);
  rt.bool_to_string = llvm::Function::Create(llvm::FunctionType::get(i64, {i64}, false),
                                              llvm::Function::ExternalLinkage, "kl_bool_to_string",
                                              module);
  rt.null_to_string = llvm::Function::Create(llvm::FunctionType::get(i64, {}, false),
                                              llvm::Function::ExternalLinkage, "kl_null_to_string",
                                              module);
  rt.value_add = llvm::Function::Create(llvm::FunctionType::get(i64, {i64, i64}, false),
                                        llvm::Function::ExternalLinkage, "kl_value_add", module);
  rt.value_sub = llvm::Function::Create(llvm::FunctionType::get(i64, {i64, i64}, false),
                                        llvm::Function::ExternalLinkage, "kl_value_sub", module);
  rt.value_mul = llvm::Function::Create(llvm::FunctionType::get(i64, {i64, i64}, false),
                                        llvm::Function::ExternalLinkage, "kl_value_mul", module);
  rt.value_div = llvm::Function::Create(llvm::FunctionType::get(i64, {i64, i64}, false),
                                        llvm::Function::ExternalLinkage, "kl_value_div", module);
  rt.value_mod = llvm::Function::Create(llvm::FunctionType::get(i64, {i64, i64}, false),
                                        llvm::Function::ExternalLinkage, "kl_value_mod", module);
  rt.value_cmp = llvm::Function::Create(llvm::FunctionType::get(i32, {i64, i64}, false),
                                        llvm::Function::ExternalLinkage, "kl_value_cmp", module);

  auto fn_1 = [&](const char *name) {
    return llvm::Function::Create(llvm::FunctionType::get(i64, {i64}, false),
                                  llvm::Function::ExternalLinkage, name, module);
  };
  auto fn_2 = [&](const char *name) {
    return llvm::Function::Create(llvm::FunctionType::get(i64, {i64, i64}, false),
                                  llvm::Function::ExternalLinkage, name, module);
  };
  auto fn_3 = [&](const char *name) {
    return llvm::Function::Create(llvm::FunctionType::get(i64, {i64, i64, i64}, false),
                                  llvm::Function::ExternalLinkage, name, module);
  };
  auto fn_2_i32 = [&](const char *name) {
    return llvm::Function::Create(llvm::FunctionType::get(i32, {i64, i64}, false),
                                  llvm::Function::ExternalLinkage, name, module);
  };
  rt.array_push = fn_2("kl_array_push");
  rt.array_resize = fn_3("kl_array_resize");
  rt.array_pop = fn_1("kl_array_pop");
  rt.array_clear = fn_1("kl_array_clear");
  rt.array_insert = fn_3("kl_array_insert");
  rt.array_reverse = fn_1("kl_array_reverse");
  rt.contains = fn_2_i32("kl_contains");
  rt.index_of = fn_2("kl_index_of");
  rt.map_new = llvm::Function::Create(llvm::FunctionType::get(i64, {i32, i64p}, false),
                                      llvm::Function::ExternalLinkage, "kl_map_new", module);
  rt.map_has = fn_2_i32("kl_map_has");
  rt.map_keys = fn_1("kl_map_keys");
  rt.index_get = fn_2("kl_index_get");
  rt.index_set = fn_3("kl_index_set");
  rt.remove = fn_2("kl_remove");
  rt.str_starts_with = fn_2_i32("kl_str_starts_with");
  rt.str_ends_with = fn_2_i32("kl_str_ends_with");
  rt.str_replace = fn_3("kl_str_replace");
  rt.str_split = fn_2("kl_str_split");
  rt.str_trim = fn_1("kl_str_trim");
  rt.str_to_upper = fn_1("kl_str_to_upper");
  rt.str_to_lower = fn_1("kl_str_to_lower");
  return rt;
}

llvm::Value *const_i64(llvm::IRBuilder<> &builder, const KirInstr *instr) {
  llvm::Type *i32 = builder.getInt32Ty();
  llvm::Type *i64 = builder.getInt64Ty();
  const int32_t low = instr->operands[0];
  const int32_t high = instr->operands.size() > 1 ? instr->operands[1] : (low < 0 ? -1 : 0);
  llvm::Value *lo = builder.CreateZExt(llvm::ConstantInt::get(i32, static_cast<uint32_t>(low)), i64);
  llvm::Value *hi =
      builder.CreateShl(builder.CreateZExt(llvm::ConstantInt::get(i32, static_cast<uint32_t>(high)), i64),
                        llvm::ConstantInt::get(i64, 32));
  return builder.CreateOr(lo, hi);
}

llvm::Value *bool_to_i64(llvm::IRBuilder<> &builder, llvm::Value *cond) {
  return builder.CreateZExt(cond, builder.getInt64Ty());
}

int field_index_for_name(const KirStructMeta &meta, const std::string &field_name) {
  for (std::size_t i = 0; i < meta.field_names.size(); ++i) {
    if (meta.field_names[i] == field_name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

llvm::Value *resolve_field_index(llvm::IRBuilder<> &builder, llvm::Value *type_idx,
                                const KirModule &kir_module, const std::string &field_name) {
  llvm::Type *i32 = builder.getInt32Ty();
  llvm::Value *result = llvm::ConstantInt::get(i32, -1);
  for (std::size_t t = 0; t < kir_module.struct_metas.size(); ++t) {
    const int fi = field_index_for_name(kir_module.struct_metas[t], field_name);
    if (fi < 0) {
      continue;
    }
    llvm::Value *is_match =
        builder.CreateICmpEQ(type_idx, llvm::ConstantInt::get(i32, static_cast<int>(t)));
    result = builder.CreateSelect(is_match, llvm::ConstantInt::get(i32, fi), result);
  }
  return result;
}

// Arithmetic dispatches through the runtime: string concat for `+`, IEEE
// double math when either operand is a boxed float, plain int64 otherwise.
llvm::Value *binop(llvm::IRBuilder<> &builder, const RtFns &rt, KirOpcode op,
                   llvm::Value *left, llvm::Value *right) {
  switch (op) {
  case KirOpcode::IAdd:
    return builder.CreateCall(rt.value_add, {left, right});
  case KirOpcode::ISub:
    return builder.CreateCall(rt.value_sub, {left, right});
  case KirOpcode::IMul:
    return builder.CreateCall(rt.value_mul, {left, right});
  case KirOpcode::IDiv:
    return builder.CreateCall(rt.value_div, {left, right});
  case KirOpcode::IMod:
    return builder.CreateCall(rt.value_mod, {left, right});
  default:
    return nullptr;
  }
}

llvm::Value *to_wire_i64(llvm::IRBuilder<> &builder, const RtFns &rt, llvm::Value *value,
                         KirType type) {
  llvm::Type *i64 = builder.getInt64Ty();
  if (type == KirType::Float && value->getType()->isDoubleTy()) {
    return builder.CreateCall(rt.float_new, {value});
  }
  if (value->getType()->isIntegerTy(64)) {
    return value;
  }
  if (value->getType()->isDoubleTy()) {
    return builder.CreateCall(rt.float_new, {value});
  }
  return llvm::ConstantInt::get(i64, 0);
}

KirType kir_field_type_for_name(const KirModule &module, const std::string &field_name) {
  for (const KirStructMeta &meta : module.struct_metas) {
    for (std::size_t fi = 0; fi < meta.field_names.size(); ++fi) {
      if (meta.field_names[fi] == field_name && fi < meta.field_types.size()) {
        return meta.field_types[fi];
      }
    }
  }
  return KirType::Any;
}

KirType kir_local_type(const KirFunction &fn, int slot) {
  if (slot >= 0 && static_cast<std::size_t>(slot) < fn.local_types.size() &&
      fn.local_types[static_cast<std::size_t>(slot)] != KirType::Any) {
    return fn.local_types[static_cast<std::size_t>(slot)];
  }
  if (slot >= 0 && static_cast<std::size_t>(slot) < fn.param_types.size()) {
    return fn.param_types[static_cast<std::size_t>(slot)];
  }
  return KirType::Any;
}

struct UnboxedScalar {
  llvm::Value *value = nullptr;
  KirType type = KirType::Any;
  bool is_double = false;
};

llvm::Value *wire_for_string_display(llvm::IRBuilder<> &builder, const RtFns &rt,
                                     llvm::Value *wire, KirType type) {
  switch (type) {
  case KirType::Bool:
    return builder.CreateCall(rt.bool_to_string, {wire});
  case KirType::Null:
    return builder.CreateCall(rt.null_to_string, {});
  default:
    return wire;
  }
}

UnboxedScalar unbox_wire_scalar(llvm::IRBuilder<> &builder, const RtFns &rt, llvm::Value *wire,
                                KirType type, llvm::Type *i64, llvm::Type *i32) {
  UnboxedScalar out;
  out.type = type;
  const KirType width = kir_type_normalize(type);
  if (width == KirType::Int32 || width == KirType::UInt32) {
    out.value = builder.CreateSExt(builder.CreateTrunc(wire, i32), i64);
    out.type = width;
    return out;
  }
  if (kir_type_is_integer(width)) {
    out.value = wire;
    out.type = width;
    return out;
  }
  if (kir_type_is_float(width)) {
    out.value = builder.CreateCall(rt.float_get, {wire});
    out.type = width;
    out.is_double = true;
    return out;
  }
  out.value = wire;
  return out;
}

llvm::Value *typed_binop(llvm::IRBuilder<> &builder, const RtFns &rt, KirOpcode op,
                         llvm::Value *lhs, llvm::Value *rhs, KirType lhs_ty, KirType rhs_ty) {
  const KirBinopSpec spec = kir_binop_spec(op);
  if (spec.specialized) {
    op = spec.generic;
    lhs_ty = spec.width;
    rhs_ty = spec.width;
  }
  if ((lhs_ty == KirType::Int32 && rhs_ty == KirType::Int32) ||
      (lhs_ty == KirType::UInt32 && rhs_ty == KirType::UInt32)) {
    llvm::Type *i32 = builder.getInt32Ty();
    llvm::Type *i64 = builder.getInt64Ty();
    llvm::Value *l = builder.CreateTrunc(lhs, i32);
    llvm::Value *r = builder.CreateTrunc(rhs, i32);
    llvm::Value *result = nullptr;
    switch (op) {
    case KirOpcode::IAdd:
      result = builder.CreateAdd(l, r);
      break;
    case KirOpcode::ISub:
      result = builder.CreateSub(l, r);
      break;
    case KirOpcode::IMul:
      result = builder.CreateMul(l, r);
      break;
    case KirOpcode::IDiv:
      result = builder.CreateSDiv(l, r);
      break;
    case KirOpcode::IMod:
      result = builder.CreateSRem(l, r);
      break;
    default:
      break;
    }
    if (result != nullptr) {
      return builder.CreateSExt(result, i64);
    }
  }
  if ((lhs_ty == KirType::Int || lhs_ty == KirType::Int64) &&
      (rhs_ty == KirType::Int || rhs_ty == KirType::Int64)) {
    switch (op) {
    case KirOpcode::IAdd:
      return builder.CreateAdd(lhs, rhs);
    case KirOpcode::ISub:
      return builder.CreateSub(lhs, rhs);
    case KirOpcode::IMul:
      return builder.CreateMul(lhs, rhs);
    case KirOpcode::IDiv:
      return builder.CreateSDiv(lhs, rhs);
    case KirOpcode::IMod:
      return builder.CreateSRem(lhs, rhs);
    default:
      break;
    }
  }
  if (kir_type_is_float(lhs_ty) || kir_type_is_float(rhs_ty)) {
    llvm::Type *f64 = builder.getDoubleTy();
    llvm::Value *l = lhs->getType()->isDoubleTy() ? lhs : builder.CreateCall(rt.float_get, {lhs});
    llvm::Value *r = rhs->getType()->isDoubleTy() ? rhs : builder.CreateCall(rt.float_get, {rhs});
    if (lhs_ty == KirType::Int) {
      l = builder.CreateSIToFP(lhs, f64);
    }
    if (rhs_ty == KirType::Int) {
      r = builder.CreateSIToFP(rhs, f64);
    }
    switch (op) {
    case KirOpcode::IAdd:
      return builder.CreateFAdd(l, r);
    case KirOpcode::ISub:
      return builder.CreateFSub(l, r);
    case KirOpcode::IMul:
      return builder.CreateFMul(l, r);
    case KirOpcode::IDiv:
      return builder.CreateFDiv(l, r);
    default:
      break;
    }
  }
  if (op == KirOpcode::IAdd &&
      (lhs_ty == KirType::String || rhs_ty == KirType::String)) {
    llvm::Value *wl = wire_for_string_display(builder, rt, lhs, lhs_ty);
    llvm::Value *wr = wire_for_string_display(builder, rt, rhs, rhs_ty);
    return builder.CreateCall(rt.value_add, {wl, wr});
  }
  llvm::Value *wl = lhs->getType()->isDoubleTy() ? builder.CreateCall(rt.float_new, {lhs}) : lhs;
  llvm::Value *wr = rhs->getType()->isDoubleTy() ? builder.CreateCall(rt.float_new, {rhs}) : rhs;
  return binop(builder, rt, op, wl, wr);
}

// Relational compares go through kl_value_cmp so strings and boxed floats
// order correctly; the three-way result is then compared against zero.
llvm::Value *icmp(llvm::IRBuilder<> &builder, const RtFns &rt, KirOpcode op,
                  llvm::Value *left, llvm::Value *right) {
  llvm::Value *zero32 = llvm::ConstantInt::get(builder.getInt32Ty(), 0);
  llvm::Value *cmp = nullptr;
  switch (op) {
  case KirOpcode::ICmpLt:
    cmp = builder.CreateCall(rt.value_cmp, {left, right});
    return builder.CreateICmpSLT(cmp, zero32);
  case KirOpcode::ICmpGt:
    cmp = builder.CreateCall(rt.value_cmp, {left, right});
    return builder.CreateICmpSGT(cmp, zero32);
  case KirOpcode::ICmpLe:
    cmp = builder.CreateCall(rt.value_cmp, {left, right});
    return builder.CreateICmpSLE(cmp, zero32);
  case KirOpcode::ICmpGe:
    cmp = builder.CreateCall(rt.value_cmp, {left, right});
    return builder.CreateICmpSGE(cmp, zero32);
  default:
    return nullptr;
  }
}

void rebuild_phi_predecessors(llvm::Function *fn) {
  for (llvm::BasicBlock &bb : *fn) {
    std::vector<llvm::PHINode *> phis;
    for (llvm::Instruction &inst : bb) {
      if (auto *phi = llvm::dyn_cast<llvm::PHINode>(&inst)) {
        phis.push_back(phi);
      }
    }
    if (phis.empty()) {
      continue;
    }
    const llvm::SmallVector<llvm::BasicBlock *, 8> preds(llvm::pred_begin(&bb),
                                                        llvm::pred_end(&bb));
    for (llvm::PHINode *phi : phis) {
      llvm::SmallVector<llvm::Value *, 8> values;
      values.reserve(preds.size());
      for (llvm::BasicBlock *pred : preds) {
        llvm::Value *incoming = llvm::UndefValue::get(phi->getType());
        for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
          if (phi->getIncomingBlock(i) == pred) {
            incoming = phi->getIncomingValue(i);
            break;
          }
        }
        values.push_back(incoming);
      }
      llvm::IRBuilder<> builder(&bb, bb.begin());
      llvm::PHINode *fixed =
          builder.CreatePHI(phi->getType(), static_cast<unsigned>(preds.size()), phi->getName());
      for (unsigned i = 0; i < preds.size(); ++i) {
        fixed->addIncoming(values[i], preds[i]);
      }
      phi->replaceAllUsesWith(fixed);
      phi->eraseFromParent();
    }
  }
}

bool emit_object(llvm::Module &module, const std::string &obj_path, std::string *error) {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  std::string triple_err;
  const llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
  const std::string triple_str = triple.str();
  // LLVM 21 switched setTargetTriple/createTargetMachine from std::string to Triple.
#if LLVM_VERSION_MAJOR >= 21
  module.setTargetTriple(triple);
#else
  module.setTargetTriple(triple_str);
#endif

  const llvm::Target *target = llvm::TargetRegistry::lookupTarget(triple_str, triple_err);
  if (target == nullptr) {
    *error = "LLVM target lookup failed: " + triple_err;
    return false;
  }

  llvm::TargetOptions options;
  std::string cpu = llvm::sys::getHostCPUName().str();
  llvm::TargetMachine *machine =
#if LLVM_VERSION_MAJOR >= 21
      target->createTargetMachine(triple, cpu, "", options, llvm::Reloc::Model::PIC_);
#else
      target->createTargetMachine(triple_str, cpu, "", options, llvm::Reloc::Model::PIC_);
#endif
  if (machine == nullptr) {
    *error = "LLVM target machine creation failed";
    return false;
  }

  module.setDataLayout(machine->createDataLayout());

  std::error_code ec;
  llvm::raw_fd_ostream dest(obj_path, ec, llvm::sys::fs::OF_None);
  if (ec) {
    *error = "failed to open object file: " + ec.message();
    return false;
  }

  llvm::legacy::PassManager pass;
#if LLVM_VERSION_MAJOR >= 18
  const auto obj_file_type = llvm::CodeGenFileType::ObjectFile;
#else
  const auto obj_file_type = llvm::CGFT_ObjectFile;
#endif
  if (machine->addPassesToEmitFile(pass, dest, nullptr, obj_file_type)) {
    *error = "LLVM cannot emit an object file for this target";
    return false;
  }
  pass.run(module);
  dest.flush();
  return true;
}

bool link_objects(const std::vector<std::string> &obj_paths, const std::string &rt_lib_path,
                  const std::string &out_path, std::string *error) {
  if (obj_paths.empty()) {
    *error = "link_objects requires at least one object file";
    return false;
  }
  std::ostringstream cmd;
  cmd << "clang++ -o ";
  cmd << '"' << out_path << "\"";
  for (const std::string &obj_path : obj_paths) {
    cmd << " \"" << obj_path << '"';
  }
  cmd << " \"" << rt_lib_path << '"';
  const int rc = std::system(cmd.str().c_str());
  if (rc != 0) {
    *error = "link failed (exit " + std::to_string(rc) + "): " + cmd.str();
    return false;
  }
  return true;
}

class FunctionLowerer {
public:
  FunctionLowerer(llvm::LLVMContext *context, const KirModule &kir_module, llvm::Function *llvm_fn,
                  const RtFns &rt)
      : context_(context), kir_module_(kir_module), llvm_fn_(llvm_fn), rt_(rt) {}

  void set_debug_scope(llvm::DISubprogram *subprogram) { di_subprogram_ = subprogram; }

  bool lower(const KirFunction &fn, std::string *error) {
    llvm::Type *i32 = llvm::Type::getInt32Ty(*context_);
    llvm::Type *i64 = llvm::Type::getInt64Ty(*context_);
    linear_ = linear_instrs(fn);
    if (linear_.empty()) {
      llvm::BasicBlock *entry = llvm::BasicBlock::Create(*context_, "entry", llvm_fn_);
      llvm::IRBuilder<> ret_builder(entry);
      ret_builder.CreateRet(llvm::ConstantInt::get(i64, 0));
      return true;
    }

    std::set<std::size_t> leaders;
    leaders.insert(0);
    for (std::size_t i = 0; i < linear_.size(); ++i) {
      const KirInstr *instr = linear_[i];
      if (instr->op == KirOpcode::PushHandler) {
        if (instr->operands.empty()) {
          *error = "push_handler missing operand";
          return false;
        }
        leaders.insert(i + 1 + static_cast<std::size_t>(instr->operands[0]));
      } else if (instr->op == KirOpcode::Br || instr->op == KirOpcode::CondBr ||
                 instr->op == KirOpcode::JmpIfErr) {
        if (instr->operands.empty()) {
          *error = "jump instruction missing operand";
          return false;
        }
        const int rel = instr->operands[0];
        const int target = static_cast<int>(i) + 1 + rel;
        if (target < 0 || static_cast<std::size_t>(target) > linear_.size()) {
          *error = "jump target out of range";
          return false;
        }
        leaders.insert(static_cast<std::size_t>(target));
        if (instr->op == KirOpcode::CondBr || instr->op == KirOpcode::JmpIfErr) {
          leaders.insert(i + 1);
        }
      } else if (instr->op == KirOpcode::PropagateErr) {
        leaders.insert(i + 1);
      }
    }

    llvm::BasicBlock *entry_bb = llvm::BasicBlock::Create(*context_, "entry", llvm_fn_);

    std::vector<llvm::BasicBlock *> blocks;
    blocks.reserve(leaders.size());
    int block_id = 0;
    for (std::size_t start : leaders) {
      (void)start;
      blocks.push_back(
          llvm::BasicBlock::Create(*context_, "bb" + std::to_string(block_id++), llvm_fn_));
    }

    std::map<std::size_t, llvm::BasicBlock *> block_for_leader;
    {
      int idx = 0;
      for (std::size_t start : leaders) {
        block_for_leader[start] = blocks[static_cast<std::size_t>(idx++)];
      }
    }

    auto block_index = [&](std::size_t pc) -> llvm::BasicBlock * {
      auto it = block_for_leader.upper_bound(pc);
      if (it == block_for_leader.begin()) {
        return blocks.front();
      }
      --it;
      return it->second;
    };

    llvm::BasicBlock *code_bb = block_for_leader[*leaders.begin()];
    llvm::IRBuilder<> alloca_builder(entry_bb);
    const int locals = max_local_slot(fn) + 1;
    local_slots_.resize(static_cast<std::size_t>(locals));
    for (int i = 0; i < locals; ++i) {
      local_slots_[static_cast<std::size_t>(i)] =
          alloca_builder.CreateAlloca(i64, nullptr, "local" + std::to_string(i));
    }
    for (int i = 0; i < fn.param_count; ++i) {
      alloca_builder.CreateStore(llvm_fn_->getArg(static_cast<unsigned>(i)),
                                 local_slots_[static_cast<std::size_t>(i)]);
    }
    alloca_builder.CreateBr(code_bb);

    std::vector<llvm::Value *> temps(linear_.size(), nullptr);
    std::vector<KirType> temp_types(linear_.size(), KirType::Void);
    std::vector<llvm::Value *> stack;
    std::vector<KirType> type_stack;
    std::map<llvm::BasicBlock *, std::vector<llvm::Value *>> exit_stacks;
    std::map<llvm::BasicBlock *, std::vector<KirType>> exit_type_stacks;
    std::vector<std::size_t> handler_pcs;
    std::set<std::size_t> handler_landings;
    for (std::size_t i = 0; i < linear_.size(); ++i) {
      const KirInstr *instr = linear_[i];
      if (instr->op == KirOpcode::PushHandler && !instr->operands.empty()) {
        handler_landings.insert(i + 1 + static_cast<std::size_t>(instr->operands[0]));
      }
    }

    auto merge_stack_at_leader = [&](std::size_t leader_pc) -> bool {
      if (leader_pc == 0) {
        stack.clear();
        return true;
      }
      llvm::BasicBlock *bb = block_for_leader[leader_pc];
      std::vector<llvm::BasicBlock *> preds;
      auto add_pred = [&](llvm::BasicBlock *pred) {
        if (std::find(preds.begin(), preds.end(), pred) == preds.end()) {
          preds.push_back(pred);
        }
      };
      std::vector<std::size_t> sim_handlers;
      for (std::size_t j = 0; j < linear_.size(); ++j) {
        const KirInstr *jump = linear_[j];
        if (jump->op == KirOpcode::PushHandler) {
          sim_handlers.push_back(j + 1 + static_cast<std::size_t>(jump->operands[0]));
        } else if (jump->op == KirOpcode::PopHandler) {
          if (!sim_handlers.empty()) {
            sim_handlers.pop_back();
          }
        }
        if (jump->op == KirOpcode::Br) {
          if (j + 1 + static_cast<std::size_t>(jump->operands[0]) == leader_pc) {
            add_pred(block_index(j));
          }
        } else if (jump->op == KirOpcode::CondBr || jump->op == KirOpcode::JmpIfErr) {
          if (j + 1 == leader_pc) {
            add_pred(block_index(j));
          }
          if (j + 1 + static_cast<std::size_t>(jump->operands[0]) == leader_pc) {
            add_pred(block_index(j));
          }
        } else if (jump->op == KirOpcode::PropagateErr) {
          if (j + 1 == leader_pc) {
            add_pred(block_index(j));
          }
          if (!sim_handlers.empty() && sim_handlers.back() == leader_pc) {
            add_pred(block_index(j));
          }
        }
      }
      auto prev_it = leaders.lower_bound(leader_pc);
      if (prev_it != leaders.begin()) {
        --prev_it;
        const std::size_t prev_leader = *prev_it;
        if (prev_leader < leader_pc) {
          bool can_fallthrough = true;
          for (std::size_t j = prev_leader; j < leader_pc; ++j) {
            const KirInstr *instr = linear_[j];
            if (instr->op == KirOpcode::Br || instr->op == KirOpcode::Ret ||
                instr->op == KirOpcode::CondBr || instr->op == KirOpcode::JmpIfErr ||
                instr->op == KirOpcode::PropagateErr) {
              can_fallthrough = false;
              break;
            }
          }
          if (can_fallthrough) {
            add_pred(block_for_leader[prev_leader]);
          }
        }
      }
      if (preds.empty()) {
        stack.clear();
        type_stack.clear();
        if (handler_landings.count(leader_pc) > 0) {
          // Unreachable in success-only paths; keep stack depth for catch binding.
          stack.push_back(llvm::UndefValue::get(i64));
          type_stack.push_back(KirType::Any);
        }
        return true;
      }
      if (preds.size() == 1) {
        llvm::BasicBlock *pred = preds.front();
        if (pred == entry_bb) {
          stack.clear();
          type_stack.clear();
          return true;
        }
        const auto it = exit_stacks.find(pred);
        if (it == exit_stacks.end()) {
          *error = "native lowering missing stack state for predecessor block";
          return false;
        }
        stack = it->second;
        const auto tit = exit_type_stacks.find(pred);
        type_stack = tit != exit_type_stacks.end() ? tit->second : std::vector<KirType>{};
        return true;
      }
      std::size_t max_depth = 0;
      for (llvm::BasicBlock *pred : preds) {
        const auto it = exit_stacks.find(pred);
        if (it != exit_stacks.end()) {
          max_depth = std::max(max_depth, it->second.size());
        }
      }
      stack.assign(max_depth, nullptr);
      type_stack.assign(max_depth, KirType::Any);
      llvm::IRBuilder<> phi_builder(bb);
      phi_builder.SetInsertPoint(bb, bb->getFirstInsertionPt());
      for (std::size_t d = 0; d < max_depth; ++d) {
        llvm::PHINode *phi = phi_builder.CreatePHI(i64, static_cast<unsigned>(preds.size()),
                                                     "stk" + std::to_string(d));
        for (llvm::BasicBlock *pred : preds) {
          llvm::Value *incoming = llvm::UndefValue::get(i64);
          const auto it = exit_stacks.find(pred);
          if (it != exit_stacks.end() && d < it->second.size()) {
            incoming = it->second[d];
          }
          phi->addIncoming(incoming, pred);
        }
        stack[d] = phi;
      }
      return true;
    };

    for (std::size_t i = 0; i < linear_.size(); ++i) {
      const KirInstr *instr = linear_[i];
      g_lower_context =
          "function '" + fn.name + "' at instruction '" + kir_opcode_name(instr->op) + "'";
      llvm::BasicBlock *bb = block_index(i);
      if (leaders.count(i) > 0) {
        if (i > 0) {
          llvm::BasicBlock *prev_bb = block_index(i - 1);
          if (prev_bb != bb && prev_bb->getTerminator() == nullptr) {
            exit_stacks[prev_bb] = stack;
            exit_type_stacks[prev_bb] = type_stack;
          }
        }
        if (!merge_stack_at_leader(i)) {
          return false;
        }
      }
      llvm::IRBuilder<> builder(bb);
      if (di_subprogram_ != nullptr) {
        builder.SetCurrentDebugLocation(llvm::DILocation::get(
            *context_, instr->line > 0 ? static_cast<unsigned>(instr->line) : 1,
            instr->col > 0 ? static_cast<unsigned>(instr->col) : 0, di_subprogram_));
      }
      auto push = [&](llvm::Value *v) {
        stack.push_back(v);
        KirType ty = KirType::Any;
        if (static_cast<std::size_t>(i) < fn.instr_types.size()) {
          ty = fn.instr_types[i];
        }
        type_stack.push_back(ty);
      };

      auto pop_args_array = [&](int argc) -> llvm::Value * {
        if (argc <= 0) {
          return llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(i64));
        }
        llvm::AllocaInst *elements =
            builder.CreateAlloca(i64, llvm::ConstantInt::get(i32, argc), "native_args");
        for (int ai = argc - 1; ai >= 0; --ai) {
          if (stack.empty() || type_stack.empty()) {
            *error = "native lowering stack underflow in " + g_lower_context;
            return nullptr;
          }
          llvm::Value *elem = stack.back();
          const KirType elem_ty = type_stack.back();
          stack.pop_back();
          type_stack.pop_back();
          elem = wire_for_string_display(builder, rt_, elem, elem_ty);
          llvm::Value *slot =
              builder.CreateGEP(i64, elements, llvm::ConstantInt::get(i32, ai));
          builder.CreateStore(elem, slot);
        }
        return builder.CreateBitCast(elements, llvm::PointerType::getUnqual(i64));
      };

      auto pop_binop = [&](KirOpcode op) -> bool {
        if (stack.size() < 2 || type_stack.size() < 2) {
          *error = "native lowering stack underflow in " + g_lower_context;
          return false;
        }
        llvm::Value *rhs = stack.back();
        KirType rhs_ty = type_stack.back();
        stack.pop_back();
        type_stack.pop_back();
        llvm::Value *lhs = stack.back();
        KirType lhs_ty = type_stack.back();
        stack.pop_back();
        type_stack.pop_back();
        llvm::Value *result = typed_binop(builder, rt_, op, lhs, rhs, lhs_ty, rhs_ty);
        if (result == nullptr) {
          *error = "unsupported binop";
          return false;
        }
        const KirType result_ty =
            static_cast<std::size_t>(i) < fn.instr_types.size() ? fn.instr_types[i] : KirType::Any;
        temps[i] = result;
        temp_types[i] = result_ty;
        if (result->getType()->isDoubleTy()) {
          push(builder.CreateCall(rt_.float_new, {result}));
        } else {
          push(result);
        }
        return true;
      };

      auto pop_icmp = [&](KirOpcode op) -> bool {
        llvm::Value *rhs = pop_value(&stack, error, &type_stack);
        llvm::Value *lhs = pop_value(&stack, error, &type_stack);
        if (lhs == nullptr || rhs == nullptr) {
          return false;
        }
        llvm::Value *result = nullptr;
        if (op == KirOpcode::ICmpEq || op == KirOpcode::ICmpNeq) {
          llvm::Value *eq =
              builder.CreateCall(rt_.value_eq, {lhs, rhs});
          if (op == KirOpcode::ICmpNeq) {
            eq = builder.CreateXor(eq, llvm::ConstantInt::get(builder.getInt32Ty(), 1));
          }
          result = bool_to_i64(builder, builder.CreateICmpNE(eq, llvm::ConstantInt::get(i32, 0)));
        } else {
          result = bool_to_i64(builder, icmp(builder, rt_, op, lhs, rhs));
        }
        push(result);
        temps[i] = result;
        return true;
      };

      switch (instr->op) {
      case KirOpcode::ConstInt:
      case KirOpcode::ConstI64: {
        llvm::Value *value = const_i64(builder, instr);
        push(value);
        temps[i] = value;
        if (static_cast<std::size_t>(i) < fn.instr_types.size()) {
          temp_types[i] = fn.instr_types[i];
        }
        break;
      }
      case KirOpcode::ConstI32: {
        llvm::Value *value = builder.CreateSExt(
            llvm::ConstantInt::get(i32, static_cast<uint32_t>(instr->operands[0])), i64);
        push(value);
        temps[i] = value;
        temp_types[i] = KirType::Int32;
        break;
      }
      case KirOpcode::ConstU8: {
        llvm::Value *value = builder.CreateZExt(
            llvm::ConstantInt::get(builder.getInt8Ty(),
                                   static_cast<uint8_t>(instr->operands[0] & 0xff)),
            i64);
        push(value);
        temps[i] = value;
        temp_types[i] = KirType::UInt8;
        break;
      }
      case KirOpcode::ConstF32: {
        uint32_t bits = static_cast<uint32_t>(instr->operands[0]);
        float f = 0.0f;
        std::memcpy(&f, &bits, sizeof(f));
        llvm::Value *dbl = llvm::ConstantFP::get(builder.getDoubleTy(), static_cast<double>(f));
        push(builder.CreateCall(rt_.float_new, {dbl}));
        temps[i] = stack.back();
        temp_types[i] = KirType::Float32;
        break;
      }
      case KirOpcode::ConstF64: {
        llvm::Value *dbl = const_double(builder, instr);
        push(builder.CreateCall(rt_.float_new, {dbl}));
        temps[i] = stack.back();
        temp_types[i] = KirType::Float64;
        break;
      }
      case KirOpcode::ConstFloat: {
        if (static_cast<std::size_t>(i) < fn.instr_types.size() &&
            fn.instr_types[i] == KirType::Float) {
          llvm::Value *dbl = const_double(builder, instr);
          temps[i] = dbl;
          temp_types[i] = KirType::Float;
          push(builder.CreateCall(rt_.float_new, {dbl}));
        } else {
          llvm::Value *value =
              builder.CreateCall(rt_.float_from_bits, {const_i64(builder, instr)});
          push(value);
          temps[i] = value;
          temp_types[i] = KirType::Float;
        }
        break;
      }
      case KirOpcode::ConstBool:
        push(llvm::ConstantInt::get(i64, instr->operands.empty() ? 0 : instr->operands[0]));
        temps[i] = stack.back();
        break;
      case KirOpcode::ConstNull:
        push(llvm::ConstantInt::get(i64, 0));
        temps[i] = stack.back();
        break;
      case KirOpcode::ConstString: {
        const int pool_idx = instr->operands[0];
        if (pool_idx < 0 ||
            static_cast<std::size_t>(pool_idx) >= kir_module_.constant_strings.size()) {
          *error = "const_string pool index " + std::to_string(pool_idx) +
                   " out of range (pool size " +
                   std::to_string(kir_module_.constant_strings.size()) + ") in " +
                   g_lower_context;
          return false;
        }
        const std::string &text = kir_module_.constant_strings[static_cast<std::size_t>(pool_idx)];
        llvm::Value *data = builder.CreateGlobalStringPtr(text);
        llvm::Value *len =
            llvm::ConstantInt::get(i32, static_cast<int>(text.size()));
        llvm::Value *handle = builder.CreateCall(rt_.string_new, {data, len});
        push(handle);
        temps[i] = handle;
        break;
      }
      case KirOpcode::ConstFn: {
        const int fn_index = instr->operands[0];
        if (fn_index < 0 || static_cast<std::size_t>(fn_index) >= kir_module_.function_names.size()) {
          *error = "const_fn index out of range";
          return false;
        }
        push(llvm::ConstantInt::get(i64, fn_index));
        temps[i] = stack.back();
        break;
      }
      case KirOpcode::LoadLocal: {
        const int slot = instr->operands[0];
        if (slot < 0 || static_cast<std::size_t>(slot) >= local_slots_.size()) {
          *error = "load_local slot out of range";
          return false;
        }
        llvm::Value *loaded = builder.CreateLoad(i64, local_slots_[static_cast<std::size_t>(slot)]);
        const KirType local_ty = kir_local_type(fn, slot);
        const UnboxedScalar scalar =
            unbox_wire_scalar(builder, rt_, loaded, local_ty, i64, i32);
        if (scalar.is_double) {
          push(builder.CreateCall(rt_.float_new, {scalar.value}));
          temps[i] = stack.back();
          temp_types[i] = scalar.type;
        } else {
          push(scalar.value);
          temps[i] = scalar.value;
          temp_types[i] = scalar.type == KirType::Any ? KirType::Any : scalar.type;
        }
        break;
      }
      case KirOpcode::StoreLocal: {
        const int slot = instr->operands[0];
        if (stack.empty()) {
          *error = "store_local stack underflow";
          return false;
        }
        llvm::Value *value = stack.back();
        if (slot < 0 || static_cast<std::size_t>(slot) >= local_slots_.size()) {
          *error = "store_local slot out of range";
          return false;
        }
        builder.CreateStore(value, local_slots_[static_cast<std::size_t>(slot)]);
        break;
      }
      case KirOpcode::Pop:
        if (pop_value(&stack, error, &type_stack) == nullptr) {
          return false;
        }
        break;
      case KirOpcode::IAdd:
      case KirOpcode::ISub:
      case KirOpcode::IMul:
      case KirOpcode::IDiv:
      case KirOpcode::IMod:
      case KirOpcode::IAdd32:
      case KirOpcode::IAdd64:
      case KirOpcode::ISub32:
      case KirOpcode::ISub64:
      case KirOpcode::IMul32:
      case KirOpcode::IMul64:
      case KirOpcode::IDiv32:
      case KirOpcode::IDiv64:
      case KirOpcode::IMod32:
      case KirOpcode::IMod64:
      case KirOpcode::FAdd32:
      case KirOpcode::FAdd64:
      case KirOpcode::FSub32:
      case KirOpcode::FSub64:
      case KirOpcode::FMul32:
      case KirOpcode::FMul64:
      case KirOpcode::FDiv32:
      case KirOpcode::FDiv64:
        if (instr->operands.size() >= 2) {
          const int lhs_i = instr->operands[0];
          const int rhs_i = instr->operands[1];
          if (lhs_i < 0 || static_cast<std::size_t>(lhs_i) >= i || rhs_i < 0 ||
              static_cast<std::size_t>(rhs_i) >= i) {
            *error = "indexed binop operand out of range";
            return false;
          }
          llvm::Value *lhs = temps[static_cast<std::size_t>(lhs_i)];
          llvm::Value *rhs = temps[static_cast<std::size_t>(rhs_i)];
          const KirType lhs_ty = temp_types[static_cast<std::size_t>(lhs_i)];
          const KirType rhs_ty = temp_types[static_cast<std::size_t>(rhs_i)];
          llvm::Value *result =
              typed_binop(builder, rt_, instr->op, lhs, rhs, lhs_ty, rhs_ty);
          if (result == nullptr) {
            *error = "unsupported indexed binop";
            return false;
          }
          const KirType result_ty =
              static_cast<std::size_t>(i) < fn.instr_types.size() ? fn.instr_types[i] : KirType::Any;
          temps[i] = result;
          temp_types[i] = result_ty;
          if (result->getType()->isDoubleTy()) {
            push(builder.CreateCall(rt_.float_new, {result}));
          } else {
            push(result);
          }
        } else if (!pop_binop(instr->op)) {
          return false;
        }
        break;
      case KirOpcode::ICmpEq:
      case KirOpcode::ICmpNeq:
      case KirOpcode::ICmpLt:
      case KirOpcode::ICmpGt:
      case KirOpcode::ICmpLe:
      case KirOpcode::ICmpGe:
        if (!pop_icmp(instr->op)) {
          return false;
        }
        break;
      case KirOpcode::Not: {
        llvm::Value *v = pop_value(&stack, error, &type_stack);
        if (v == nullptr) {
          return false;
        }
        llvm::Value *result = bool_to_i64(
            builder, builder.CreateICmpEQ(v, llvm::ConstantInt::get(i64, 0)));
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::BitNot: {
        llvm::Value *v = pop_value(&stack, error, &type_stack);
        if (v == nullptr) {
          return false;
        }
        llvm::Value *result = builder.CreateXor(v, llvm::ConstantInt::get(i64, -1));
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::BitAnd:
      case KirOpcode::BitOr:
      case KirOpcode::BitXor: {
        llvm::Value *rhs = pop_value(&stack, error, &type_stack);
        llvm::Value *lhs = pop_value(&stack, error, &type_stack);
        if (lhs == nullptr || rhs == nullptr) {
          return false;
        }
        llvm::Value *result = nullptr;
        if (instr->op == KirOpcode::BitAnd) {
          result = builder.CreateAnd(lhs, rhs);
        } else if (instr->op == KirOpcode::BitOr) {
          result = builder.CreateOr(lhs, rhs);
        } else {
          result = builder.CreateXor(lhs, rhs);
        }
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::Shl:
      case KirOpcode::Shr: {
        llvm::Value *rhs = pop_value(&stack, error, &type_stack);
        llvm::Value *lhs = pop_value(&stack, error, &type_stack);
        if (lhs == nullptr || rhs == nullptr) {
          return false;
        }
        // The VM clamps out-of-range shift amounts to a zero result.
        llvm::Value *in_range =
            builder.CreateICmpULT(rhs, llvm::ConstantInt::get(i64, 64));
        llvm::Value *amount =
            builder.CreateSelect(in_range, rhs, llvm::ConstantInt::get(i64, 0));
        llvm::Value *shifted = instr->op == KirOpcode::Shl
                                   ? builder.CreateShl(lhs, amount)
                                   : builder.CreateLShr(lhs, amount);
        llvm::Value *result =
            builder.CreateSelect(in_range, shifted, llvm::ConstantInt::get(i64, 0));
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::Call: {
        const int argc = instr->operands[0];
        if (argc < 0) {
          *error = "call arg count invalid";
          return false;
        }
        llvm::Value *callee_tag = pop_value(&stack, error, &type_stack);
        if (callee_tag == nullptr) {
          return false;
        }
        std::vector<llvm::Value *> args(static_cast<std::size_t>(argc));
        for (int arg = argc - 1; arg >= 0; --arg) {
          args[static_cast<std::size_t>(arg)] = pop_value(&stack, error, &type_stack);
          if (args[static_cast<std::size_t>(arg)] == nullptr) {
            return false;
          }
        }
        llvm::ConstantInt *callee_const = llvm::dyn_cast<llvm::ConstantInt>(callee_tag);
        if (callee_const == nullptr) {
          *error = "call callee is not a known function constant";
          return false;
        }
        const int fn_index = static_cast<int>(callee_const->getSExtValue());
        if (fn_index < 0 ||
            static_cast<std::size_t>(fn_index) >= kir_module_.function_symbols.size()) {
          *error = "call function index out of range";
          return false;
        }
        const std::string &target_symbol =
            kir_module_.function_symbols[static_cast<std::size_t>(fn_index)];
        llvm::Module *llvm_module = llvm_fn_->getParent();
        llvm::Function *target = llvm_module->getFunction(target_symbol);
        if (target == nullptr) {
          const int param_count =
              kir_module_.function_param_counts[static_cast<std::size_t>(fn_index)];
          std::vector<llvm::Type *> param_types(static_cast<std::size_t>(param_count), i64);
          auto *fn_type = llvm::FunctionType::get(i64, param_types, false);
          target = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                          target_symbol, llvm_module);
        }
        llvm::Value *result = builder.CreateCall(target, args);
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::StructNew: {
        const int packed = instr->operands[0];
        const int type_idx = packed >> 16;
        const int field_count = packed & 0xFFFF;
        if (field_count < 0) {
          *error = "struct_new field count invalid";
          return false;
        }
        llvm::Type *i64p = llvm::PointerType::getUnqual(i64);
        llvm::AllocaInst *fields =
            builder.CreateAlloca(i64, llvm::ConstantInt::get(i32, field_count), "struct_fields");
        for (int fi = field_count - 1; fi >= 0; --fi) {
          llvm::Value *field = pop_value(&stack, error, &type_stack);
          if (field == nullptr) {
            return false;
          }
          llvm::Value *slot =
              builder.CreateGEP(i64, fields, llvm::ConstantInt::get(i32, fi));
          builder.CreateStore(field, slot);
        }
        llvm::Value *obj = builder.CreateCall(
            rt_.struct_new,
            {llvm::ConstantInt::get(i32, type_idx), llvm::ConstantInt::get(i32, field_count),
             builder.CreateBitCast(fields, i64p)});
        push(obj);
        temps[i] = obj;
        break;
      }
      case KirOpcode::FieldGet: {
        const int pool_idx = instr->operands[0];
        if (pool_idx < 0 ||
            static_cast<std::size_t>(pool_idx) >= kir_module_.constant_strings.size()) {
          *error = "field_get pool index out of range";
          return false;
        }
        llvm::Value *obj = pop_value(&stack, error, &type_stack);
        if (obj == nullptr) {
          return false;
        }
        const std::string &field_name =
            kir_module_.constant_strings[static_cast<std::size_t>(pool_idx)];
        llvm::Value *type_idx = builder.CreateCall(rt_.struct_type_index, {obj});
        llvm::Value *field_idx =
            resolve_field_index(builder, type_idx, kir_module_, field_name);
        const KirType field_ty =
            kir_field_type_for_name(kir_module_, field_name);
        llvm::Value *wire = builder.CreateCall(rt_.struct_field_at, {obj, field_idx});
        const UnboxedScalar scalar =
            unbox_wire_scalar(builder, rt_, wire, field_ty, i64, i32);
        if (scalar.is_double) {
          push(builder.CreateCall(rt_.float_new, {scalar.value}));
          temps[i] = stack.back();
          temp_types[i] = scalar.type;
        } else {
          push(scalar.value);
          temps[i] = scalar.value;
          temp_types[i] = scalar.type == KirType::Any ? KirType::Any : scalar.type;
        }
        break;
      }
      case KirOpcode::FieldSet: {
        const int pool_idx = instr->operands[0];
        if (pool_idx < 0 ||
            static_cast<std::size_t>(pool_idx) >= kir_module_.constant_strings.size()) {
          *error = "field_set pool index out of range";
          return false;
        }
        llvm::Value *value = pop_value(&stack, error, &type_stack);
        if (value == nullptr) {
          return false;
        }
        llvm::Value *obj = pop_value(&stack, error, &type_stack);
        if (obj == nullptr) {
          return false;
        }
        const std::string &field_name =
            kir_module_.constant_strings[static_cast<std::size_t>(pool_idx)];
        llvm::Value *type_idx = builder.CreateCall(rt_.struct_type_index, {obj});
        llvm::Value *field_idx =
            resolve_field_index(builder, type_idx, kir_module_, field_name);
        llvm::Value *updated =
            builder.CreateCall(rt_.struct_field_set, {obj, field_idx, value});
        push(updated);
        temps[i] = updated;
        break;
      }
      case KirOpcode::ArrayNew: {
        const int element_count = instr->operands[0];
        if (element_count < 0) {
          *error = "array_new element count invalid";
          return false;
        }
        llvm::Type *i64p = llvm::PointerType::getUnqual(i64);
        llvm::AllocaInst *elements = builder.CreateAlloca(
            i64, llvm::ConstantInt::get(i32, element_count), "array_elems");
        for (int ei = element_count - 1; ei >= 0; --ei) {
          llvm::Value *elem = pop_value(&stack, error, &type_stack);
          if (elem == nullptr) {
            return false;
          }
          llvm::Value *slot =
              builder.CreateGEP(i64, elements, llvm::ConstantInt::get(i32, ei));
          builder.CreateStore(elem, slot);
        }
        llvm::Value *arr = builder.CreateCall(
            rt_.array_new,
            {llvm::ConstantInt::get(i32, element_count),
             builder.CreateBitCast(elements, i64p)});
        push(arr);
        temps[i] = arr;
        break;
      }
      case KirOpcode::DenseArrayNew: {
        if (instr->operands.size() < 2) {
          *error = "dense_array_new missing shape operands";
          return false;
        }
        const int rows = instr->operands[0];
        const int cols = instr->operands[1];
        if (rows <= 0 || cols <= 0) {
          *error = "dense_array_new shape invalid";
          return false;
        }
        const int element_count = rows * cols;
        llvm::Type *i64p = llvm::PointerType::getUnqual(i64);
        llvm::AllocaInst *elements = builder.CreateAlloca(
            i64, llvm::ConstantInt::get(i32, element_count), "dense_elems");
        for (int ei = element_count - 1; ei >= 0; --ei) {
          llvm::Value *elem = pop_value(&stack, error, &type_stack);
          if (elem == nullptr) {
            return false;
          }
          llvm::Value *slot =
              builder.CreateGEP(i64, elements, llvm::ConstantInt::get(i32, ei));
          builder.CreateStore(elem, slot);
        }
        llvm::Value *arr = builder.CreateCall(
            rt_.dense_array_new,
            {llvm::ConstantInt::get(i32, rows), llvm::ConstantInt::get(i32, cols),
             builder.CreateBitCast(elements, i64p)});
        push(arr);
        temps[i] = arr;
        break;
      }
      case KirOpcode::IndexGet: {
        llvm::Value *index = pop_value(&stack, error, &type_stack);
        llvm::Value *array = pop_value(&stack, error, &type_stack);
        if (index == nullptr || array == nullptr) {
          return false;
        }
        llvm::Value *wire = builder.CreateCall(rt_.index_get, {array, index});
        const KirType element_ty =
            static_cast<std::size_t>(i) < fn.instr_types.size() ? fn.instr_types[i]
                                                                 : KirType::Any;
        if (kir_type_is_scalar(element_ty)) {
          const UnboxedScalar scalar =
              unbox_wire_scalar(builder, rt_, wire, element_ty, i64, i32);
          if (scalar.is_double) {
            push(builder.CreateCall(rt_.float_new, {scalar.value}));
            temps[i] = stack.back();
            temp_types[i] = scalar.type;
          } else {
            push(scalar.value);
            temps[i] = scalar.value;
            temp_types[i] = scalar.type == KirType::Any ? KirType::Any : scalar.type;
          }
        } else {
          push(wire);
          temps[i] = wire;
          temp_types[i] = element_ty;
        }
        break;
      }
      case KirOpcode::IndexSet: {
        KirType value_ty = KirType::Any;
        if (!type_stack.empty()) {
          value_ty = type_stack.back();
        }
        llvm::Value *value = pop_value(&stack, error, &type_stack);
        llvm::Value *index = pop_value(&stack, error, &type_stack);
        llvm::Value *object = pop_value(&stack, error, &type_stack);
        if (value == nullptr || index == nullptr || object == nullptr) {
          return false;
        }
        llvm::Value *wire_value = to_wire_i64(builder, rt_, value, value_ty);
        llvm::Value *result =
            builder.CreateCall(rt_.index_set, {object, index, wire_value});
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::ArrayPush: {
        KirType value_ty = KirType::Any;
        if (!type_stack.empty()) {
          value_ty = type_stack.back();
        }
        llvm::Value *value = pop_value(&stack, error, &type_stack);
        llvm::Value *array = pop_value(&stack, error, &type_stack);
        if (value == nullptr || array == nullptr) {
          return false;
        }
        llvm::Value *wire_value = to_wire_i64(builder, rt_, value, value_ty);
        llvm::Value *result = builder.CreateCall(rt_.array_push, {array, wire_value});
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::ArrayResize: {
        llvm::Value *default_value = pop_value(&stack, error, &type_stack);
        llvm::Value *count = pop_value(&stack, error, &type_stack);
        llvm::Value *array = pop_value(&stack, error, &type_stack);
        if (default_value == nullptr || count == nullptr || array == nullptr) {
          return false;
        }
        llvm::Value *result =
            builder.CreateCall(rt_.array_resize, {array, count, default_value});
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::ArrayPop: {
        llvm::Value *array = pop_value(&stack, error, &type_stack);
        if (array == nullptr) {
          return false;
        }
        llvm::Value *result = builder.CreateCall(rt_.array_pop, {array});
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::ArrayRemove: {
        llvm::Value *key = pop_value(&stack, error, &type_stack);
        llvm::Value *object = pop_value(&stack, error, &type_stack);
        if (key == nullptr || object == nullptr) {
          return false;
        }
        llvm::Value *result = builder.CreateCall(rt_.remove, {object, key});
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::ArrayContains: {
        llvm::Value *needle = pop_value(&stack, error, &type_stack);
        llvm::Value *object = pop_value(&stack, error, &type_stack);
        if (needle == nullptr || object == nullptr) {
          return false;
        }
        llvm::Value *found32 = builder.CreateCall(rt_.contains, {object, needle});
        llvm::Value *result = builder.CreateZExt(found32, i64);
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::ArrayClear: {
        llvm::Value *array = pop_value(&stack, error, &type_stack);
        if (array == nullptr) {
          return false;
        }
        llvm::Value *result = builder.CreateCall(rt_.array_clear, {array});
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::ArrayInsert: {
        llvm::Value *value = pop_value(&stack, error, &type_stack);
        llvm::Value *index = pop_value(&stack, error, &type_stack);
        llvm::Value *array = pop_value(&stack, error, &type_stack);
        if (value == nullptr || index == nullptr || array == nullptr) {
          return false;
        }
        llvm::Value *result =
            builder.CreateCall(rt_.array_insert, {array, index, value});
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::ArrayIndexOf: {
        llvm::Value *needle = pop_value(&stack, error, &type_stack);
        llvm::Value *object = pop_value(&stack, error, &type_stack);
        if (needle == nullptr || object == nullptr) {
          return false;
        }
        llvm::Value *result = builder.CreateCall(rt_.index_of, {object, needle});
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::ArrayReverse: {
        llvm::Value *array = pop_value(&stack, error, &type_stack);
        if (array == nullptr) {
          return false;
        }
        llvm::Value *result = builder.CreateCall(rt_.array_reverse, {array});
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::StrStartsWith:
      case KirOpcode::StrEndsWith: {
        llvm::Value *needle = pop_value(&stack, error, &type_stack);
        llvm::Value *str = pop_value(&stack, error, &type_stack);
        if (needle == nullptr || str == nullptr) {
          return false;
        }
        llvm::Function *fn = instr->op == KirOpcode::StrStartsWith
                                 ? rt_.str_starts_with
                                 : rt_.str_ends_with;
        llvm::Value *found32 = builder.CreateCall(fn, {str, needle});
        llvm::Value *result = builder.CreateZExt(found32, i64);
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::StrReplace: {
        llvm::Value *new_str = pop_value(&stack, error, &type_stack);
        llvm::Value *old_str = pop_value(&stack, error, &type_stack);
        llvm::Value *str = pop_value(&stack, error, &type_stack);
        if (new_str == nullptr || old_str == nullptr || str == nullptr) {
          return false;
        }
        llvm::Value *result =
            builder.CreateCall(rt_.str_replace, {str, old_str, new_str});
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::StrSplit: {
        llvm::Value *delim = pop_value(&stack, error, &type_stack);
        llvm::Value *str = pop_value(&stack, error, &type_stack);
        if (delim == nullptr || str == nullptr) {
          return false;
        }
        llvm::Value *result = builder.CreateCall(rt_.str_split, {str, delim});
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::StrTrim:
      case KirOpcode::StrToUpper:
      case KirOpcode::StrToLower: {
        llvm::Value *str = pop_value(&stack, error, &type_stack);
        if (str == nullptr) {
          return false;
        }
        llvm::Function *fn = rt_.str_trim;
        if (instr->op == KirOpcode::StrToUpper) {
          fn = rt_.str_to_upper;
        } else if (instr->op == KirOpcode::StrToLower) {
          fn = rt_.str_to_lower;
        }
        llvm::Value *result = builder.CreateCall(fn, {str});
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::MapNew: {
        const int pair_count = instr->operands.empty() ? 0 : instr->operands[0];
        if (pair_count < 0) {
          *error = "map_new pair count invalid";
          return false;
        }
        llvm::Type *i64p = llvm::PointerType::getUnqual(i64);
        llvm::AllocaInst *pairs = builder.CreateAlloca(
            i64, llvm::ConstantInt::get(i32, pair_count * 2), "map_pairs");
        for (int pi = pair_count - 1; pi >= 0; --pi) {
          llvm::Value *value = pop_value(&stack, error, &type_stack);
          llvm::Value *key = pop_value(&stack, error, &type_stack);
          if (value == nullptr || key == nullptr) {
            return false;
          }
          llvm::Value *key_slot =
              builder.CreateGEP(i64, pairs, llvm::ConstantInt::get(i32, 2 * pi));
          llvm::Value *value_slot =
              builder.CreateGEP(i64, pairs, llvm::ConstantInt::get(i32, 2 * pi + 1));
          builder.CreateStore(key, key_slot);
          builder.CreateStore(value, value_slot);
        }
        llvm::Value *map = builder.CreateCall(
            rt_.map_new, {llvm::ConstantInt::get(i32, pair_count),
                          builder.CreateBitCast(pairs, i64p)});
        push(map);
        temps[i] = map;
        break;
      }
      case KirOpcode::MapHas: {
        llvm::Value *key = pop_value(&stack, error, &type_stack);
        llvm::Value *map = pop_value(&stack, error, &type_stack);
        if (key == nullptr || map == nullptr) {
          return false;
        }
        llvm::Value *found32 = builder.CreateCall(rt_.map_has, {map, key});
        llvm::Value *result = builder.CreateZExt(found32, i64);
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::MapKeys: {
        llvm::Value *map = pop_value(&stack, error, &type_stack);
        if (map == nullptr) {
          return false;
        }
        llvm::Value *result = builder.CreateCall(rt_.map_keys, {map});
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::ArrayLen: {
        llvm::Value *obj = pop_value(&stack, error, &type_stack);
        if (obj == nullptr) {
          return false;
        }
        llvm::Value *len32 = builder.CreateCall(rt_.value_len, {obj});
        llvm::Value *len64 = builder.CreateSExt(len32, i64);
        push(len64);
        temps[i] = len64;
        break;
      }
      case KirOpcode::ArraySlice: {
        llvm::Value *end = pop_value(&stack, error, &type_stack);
        if (end == nullptr) {
          return false;
        }
        llvm::Value *start = pop_value(&stack, error, &type_stack);
        if (start == nullptr) {
          return false;
        }
        llvm::Value *obj = pop_value(&stack, error, &type_stack);
        if (obj == nullptr) {
          return false;
        }
        llvm::Value *result = builder.CreateCall(rt_.slice, {obj, start, end});
        push(result);
        temps[i] = result;
        break;
      }
      case KirOpcode::INeg: {
        llvm::Value *v = pop_value(&stack, error, &type_stack);
        if (v == nullptr) {
          return false;
        }
        llvm::Value *neg =
            builder.CreateCall(rt_.value_sub, {llvm::ConstantInt::get(i64, 0), v});
        push(neg);
        temps[i] = neg;
        break;
      }
      case KirOpcode::FloatToBits: {
        llvm::Value *v = pop_value(&stack, error, &type_stack);
        if (v == nullptr) {
          return false;
        }
        llvm::Value *bits = builder.CreateCall(rt_.float_to_bits, {v});
        push(bits);
        temps[i] = bits;
        break;
      }
      case KirOpcode::Nop:
        break;
      case KirOpcode::EnumVariant: {
        const int packed = instr->operands[0];
        const int type_idx = packed >> 16;
        const int variant_idx = packed & 0xFFFF;
        const uint64_t wire = (0xFFFDULL << 48) |
                            (static_cast<uint64_t>(type_idx & 0xFFFF) << 16) |
                            static_cast<uint64_t>(variant_idx & 0xFFFF);
        llvm::Value *value = llvm::ConstantInt::get(i64, wire);
        push(value);
        temps[i] = value;
        break;
      }
      case KirOpcode::EnumVariantPayload: {
        const int packed = instr->operands[0];
        const int type_idx = packed >> 16;
        const int variant_idx = packed & 0xFFFF;
        int param_count = 0;
        if (type_idx >= 0 &&
            static_cast<std::size_t>(type_idx) < kir_module_.enum_metas.size()) {
          const KirEnumMeta &meta =
              kir_module_.enum_metas[static_cast<std::size_t>(type_idx)];
          if (variant_idx >= 0 &&
              static_cast<std::size_t>(variant_idx) < meta.variant_param_counts.size()) {
            param_count =
                meta.variant_param_counts[static_cast<std::size_t>(variant_idx)];
          }
        }
        if (param_count < 0) {
          *error = "enum_variant_payload param count invalid";
          return false;
        }
        llvm::Type *i64p = llvm::PointerType::getUnqual(i64);
        llvm::AllocaInst *elements = nullptr;
        if (param_count > 0) {
          elements = builder.CreateAlloca(i64, llvm::ConstantInt::get(i32, param_count),
                                          "enum_payload");
          for (int pi = param_count - 1; pi >= 0; --pi) {
            llvm::Value *elem = pop_value(&stack, error, &type_stack);
            if (elem == nullptr) {
              return false;
            }
            llvm::Value *slot =
                builder.CreateGEP(i64, elements, llvm::ConstantInt::get(i32, pi));
            builder.CreateStore(elem, slot);
          }
        }
        llvm::Value *payload_ptr =
            param_count > 0
                ? builder.CreateBitCast(elements, i64p)
                : llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(i64));
        llvm::Value *value = builder.CreateCall(
            rt_.enum_new_payload,
            {llvm::ConstantInt::get(i32, type_idx), llvm::ConstantInt::get(i32, variant_idx),
             llvm::ConstantInt::get(i32, param_count), payload_ptr});
        push(value);
        temps[i] = value;
        break;
      }
      case KirOpcode::EnumPayloadGet: {
        const int payload_idx = instr->operands[0];
        llvm::Value *enum_val = pop_value(&stack, error, &type_stack);
        if (enum_val == nullptr) {
          return false;
        }
        llvm::Value *payload = builder.CreateCall(
            rt_.enum_payload_at,
            {enum_val, llvm::ConstantInt::get(i32, payload_idx)});
        push(payload);
        temps[i] = payload;
        break;
      }
      case KirOpcode::CastTo: {
        const int kind = instr->operands[0];
        if (stack.empty()) {
          *error = "cast stack underflow";
          return false;
        }
        llvm::Value *src = stack.back();
        KirType src_ty = type_stack.empty() ? KirType::Any : type_stack.back();
        stack.pop_back();
        if (!type_stack.empty()) {
          type_stack.pop_back();
        }
        llvm::Value *result = nullptr;
        if (kind == 0) {
          result = builder.CreateCall(rt_.cast_to_int, {to_wire_i64(builder, rt_, src, src_ty)});
        } else if (kind == 1) {
          if (src_ty == KirType::Float && src->getType()->isDoubleTy()) {
            result = builder.CreateCall(rt_.float_new, {src});
          } else {
            result = builder.CreateCall(rt_.cast_to_float,
                                        {to_wire_i64(builder, rt_, src, src_ty)});
          }
        } else if (kind == 2) {
          if (src_ty == KirType::Bool) {
            result = builder.CreateCall(rt_.bool_to_string,
                                        {to_wire_i64(builder, rt_, src, src_ty)});
          } else if (src_ty == KirType::Null) {
            result = builder.CreateCall(rt_.null_to_string, {});
          } else {
            result = builder.CreateCall(rt_.cast_to_string,
                                        {to_wire_i64(builder, rt_, src, src_ty)});
          }
        } else if (kind == 3) {
          result = builder.CreateCall(rt_.cast_to_char,
                                      {to_wire_i64(builder, rt_, src, src_ty)});
        } else {
          *error = "unsupported CastTo target kind in native lowering";
          return false;
        }
        push(result);
        temps[i] = result;
        temp_types[i] = kir_cast_target_type(kind);
        break;
      }
      case KirOpcode::NativeOut:
      case KirOpcode::NativeOutLn:
      case KirOpcode::NativeErr:
      case KirOpcode::NativeErrLn: {
        const int argc = instr->operands[0];
        llvm::Value *args = pop_args_array(argc);
        if (args == nullptr) {
          return false;
        }
        llvm::Function *target = rt_.native_out;
        if (instr->op == KirOpcode::NativeOutLn) {
          target = rt_.native_out_ln;
        } else if (instr->op == KirOpcode::NativeErr) {
          target = rt_.native_err;
        } else if (instr->op == KirOpcode::NativeErrLn) {
          target = rt_.native_err_ln;
        }
        builder.CreateCall(target, {llvm::ConstantInt::get(i32, argc), args});
        push(llvm::ConstantInt::get(i64, 0));
        temps[i] = stack.back();
        break;
      }
      case KirOpcode::NativeIn:
      case KirOpcode::NativeInSecret: {
        const int argc = instr->operands[0];
        llvm::Value *args = pop_args_array(argc);
        if (args == nullptr) {
          return false;
        }
        const int secret = instr->op == KirOpcode::NativeInSecret ? 1 : 0;
        llvm::Value *line =
            builder.CreateCall(rt_.native_in,
                               {llvm::ConstantInt::get(i32, argc), args,
                                llvm::ConstantInt::get(i32, secret)});
        push(line);
        temps[i] = line;
        break;
      }
      case KirOpcode::NativeFsRead: {
        const int argc = instr->operands[0];
        if (argc != 1) {
          *error = "native_fs_read expects exactly one argument";
          return false;
        }
        llvm::Value *path = pop_value(&stack, error, &type_stack);
        if (path == nullptr) {
          return false;
        }
        llvm::Value *contents = builder.CreateCall(rt_.native_fs_read, {path});
        push(contents);
        temps[i] = contents;
        break;
      }
      case KirOpcode::NativeFsWrite: {
        const int argc = instr->operands[0];
        if (argc != 2) {
          *error = "native_fs_write expects exactly two arguments";
          return false;
        }
        llvm::Value *content = pop_value(&stack, error, &type_stack);
        llvm::Value *path = pop_value(&stack, error, &type_stack);
        if (path == nullptr || content == nullptr) {
          return false;
        }
        builder.CreateCall(rt_.native_fs_write, {path, content});
        push(llvm::ConstantInt::get(i64, 0));
        temps[i] = stack.back();
        break;
      }
      case KirOpcode::NativeSysArgs: {
        llvm::Value *argv = builder.CreateCall(rt_.native_sys_args, {});
        push(argv);
        temps[i] = argv;
        break;
      }
      case KirOpcode::Ret: {
        if (bb->getTerminator() != nullptr) {
          break;
        }
        llvm::Value *retv = llvm::ConstantInt::get(i64, 0);
        KirType ret_ty = fn.return_type;
        if (!instr->operands.empty()) {
          const int idx = instr->operands[0];
          if (idx >= 0 && static_cast<std::size_t>(idx) < i) {
            retv = temps[static_cast<std::size_t>(idx)];
            ret_ty = temp_types[static_cast<std::size_t>(idx)];
            if (ret_ty == KirType::Void &&
                static_cast<std::size_t>(idx) < fn.instr_types.size()) {
              ret_ty = fn.instr_types[static_cast<std::size_t>(idx)];
            }
          }
        } else if (!stack.empty()) {
          retv = stack.back();
          ret_ty = type_stack.back();
        }
        builder.CreateRet(to_wire_i64(builder, rt_, retv, ret_ty));
        exit_stacks[bb] = stack;
        exit_type_stacks[bb] = type_stack;
        break;
      }
      case KirOpcode::Br: {
        if (bb->getTerminator() != nullptr) {
          break;
        }
        const int rel = instr->operands[0];
        const std::size_t target = i + 1 + static_cast<std::size_t>(rel);
        builder.CreateBr(block_index(target));
        exit_stacks[bb] = stack;
        exit_type_stacks[bb] = type_stack;
        break;
      }
      case KirOpcode::CondBr: {
        if (bb->getTerminator() != nullptr) {
          break;
        }
        llvm::Value *cond_i64 = pop_value(&stack, error, &type_stack);
        if (cond_i64 == nullptr) {
          return false;
        }
        llvm::Value *cond = builder.CreateICmpNE(cond_i64, llvm::ConstantInt::get(i64, 0));
        const int rel = instr->operands[0];
        const std::size_t false_target = i + 1 + static_cast<std::size_t>(rel);
        const std::size_t true_target = i + 1;
        builder.CreateCondBr(cond, block_index(true_target), block_index(false_target));
        exit_stacks[bb] = stack;
        exit_type_stacks[bb] = type_stack;
        break;
      }
      case KirOpcode::JmpIfErr: {
        if (bb->getTerminator() != nullptr) {
          break;
        }
        if (stack.empty()) {
          *error = "jmp_if_err stack underflow";
          return false;
        }
        llvm::Value *top = stack.back();
        llvm::Value *is_err =
            builder.CreateCall(rt_.value_is_err, {top});
        llvm::Value *cond = builder.CreateICmpNE(is_err, llvm::ConstantInt::get(i32, 0));
        const int rel = instr->operands[0];
        const std::size_t err_target = i + 1 + static_cast<std::size_t>(rel);
        const std::size_t ok_target = i + 1;
        builder.CreateCondBr(cond, block_index(err_target), block_index(ok_target));
        exit_stacks[bb] = stack;
        exit_type_stacks[bb] = type_stack;
        break;
      }
      case KirOpcode::PushHandler: {
        const int rel = instr->operands[0];
        handler_pcs.push_back(i + 1 + static_cast<std::size_t>(rel));
        break;
      }
      case KirOpcode::PopHandler: {
        if (handler_pcs.empty()) {
          *error = "pop_handler with empty handler stack";
          return false;
        }
        handler_pcs.pop_back();
        break;
      }
      case KirOpcode::PropagateErr: {
        if (bb->getTerminator() != nullptr) {
          break;
        }
        if (stack.empty()) {
          *error = "propagate_err stack underflow";
          return false;
        }
        if (handler_pcs.empty()) {
          *error = "propagate_err without active handler";
          return false;
        }
        llvm::Value *top = stack.back();
        llvm::Value *is_err =
            builder.CreateCall(rt_.value_is_err, {top});
        llvm::Value *cond = builder.CreateICmpNE(is_err, llvm::ConstantInt::get(i32, 0));
        const std::size_t catch_target = handler_pcs.back();
        const std::size_t ok_target = i + 1;
        builder.CreateCondBr(cond, block_index(catch_target), block_index(ok_target));
        exit_stacks[bb] = stack;
        exit_type_stacks[bb] = type_stack;
        break;
      }
      default:
        *error = "unsupported KIR opcode in native lowering (" + g_lower_context + ")";
        return false;
      }
    }

    std::vector<std::size_t> leader_list(leaders.begin(), leaders.end());
    for (std::size_t li = 0; li + 1 < leader_list.size(); ++li) {
      llvm::BasicBlock *bb = block_for_leader[leader_list[li]];
      if (bb->getTerminator() == nullptr) {
        llvm::IRBuilder<> builder(bb);
        builder.CreateBr(block_for_leader[leader_list[li + 1]]);
      }
    }
    for (llvm::BasicBlock *bb : blocks) {
      if (bb->getTerminator() == nullptr) {
        llvm::IRBuilder<> builder(bb);
        builder.CreateRet(llvm::ConstantInt::get(i64, 0));
      }
    }
    rebuild_phi_predecessors(llvm_fn_);
    return true;
  }

private:
  llvm::LLVMContext *context_;
  const KirModule &kir_module_;
  llvm::Function *llvm_fn_;
  const RtFns &rt_;
  llvm::DISubprogram *di_subprogram_ = nullptr;
  std::vector<llvm::AllocaInst *> local_slots_;
  std::vector<const KirInstr *> linear_;
};

void copy_kir_metadata(KirModule *dst, const KirModule &src) {
  dst->constant_strings = src.constant_strings;
  dst->struct_metas = src.struct_metas;
  dst->enum_metas = src.enum_metas;
  dst->function_names = src.function_names;
  dst->function_symbols = src.function_symbols;
  dst->function_param_counts = src.function_param_counts;
  dst->function_signatures = src.function_signatures;
}

int first_instr_line(const KirFunction &fn) {
  for (const KirBasicBlock &bb : fn.blocks) {
    for (const KirInstr &instr : bb.instrs) {
      if (instr.line > 0) {
        return instr.line;
      }
    }
  }
  return 1;
}

bool lower_user_functions(llvm::Module *module, const KirModule &functions,
                          const KirModule &metadata, bool debug_info, std::string *error) {
  llvm::LLVMContext &context = module->getContext();
  llvm::Type *i64 = llvm::Type::getInt64Ty(context);
  const RtFns rt = declare_runtime(module);

  std::unique_ptr<llvm::DIBuilder> di;
  if (debug_info) {
    module->addModuleFlag(llvm::Module::Warning, "Debug Info Version",
                          llvm::DEBUG_METADATA_VERSION);
    module->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 4);
    di = std::make_unique<llvm::DIBuilder>(*module);
    const std::string &cu_src = functions.functions.front().source_path;
    const std::filesystem::path cu_path(cu_src.empty() ? "<entry>" : cu_src);
    llvm::DIFile *cu_file =
        di->createFile(cu_path.filename().string(), cu_path.parent_path().string());
    di->createCompileUnit(llvm::dwarf::DW_LANG_C, cu_file, "kinglet", /*isOptimized=*/false,
                          /*Flags=*/"", /*RuntimeVersion=*/0);
  }

  for (const KirFunction &fn : functions.functions) {
    std::vector<llvm::Type *> param_types(static_cast<std::size_t>(fn.param_count), i64);
    auto *fn_type = llvm::FunctionType::get(i64, param_types, false);
    llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                           mangled_native_symbol(fn.name, fn.source_path), module);
  }

  for (const KirFunction &fn : functions.functions) {
    llvm::Function *llvm_fn = module->getFunction(mangled_native_symbol(fn.name, fn.source_path));
    FunctionLowerer lowerer(&context, metadata, llvm_fn, rt);
    if (di) {
      const std::filesystem::path src_path(fn.source_path.empty() ? "<entry>"
                                                                  : fn.source_path);
      llvm::DIFile *file =
          di->createFile(src_path.filename().string(), src_path.parent_path().string());
      llvm::DISubroutineType *sp_type =
          di->createSubroutineType(di->getOrCreateTypeArray({}));
      const int line = first_instr_line(fn);
      llvm::DISubprogram *sp = di->createFunction(
          file, fn.name, llvm_fn->getName(), file, static_cast<unsigned>(line), sp_type,
          static_cast<unsigned>(line), llvm::DINode::FlagZero,
          llvm::DISubprogram::SPFlagDefinition);
      llvm_fn->setSubprogram(sp);
      lowerer.set_debug_scope(sp);
    }
    if (!lowerer.lower(fn, error)) {
      return false;
    }
  }
  if (di) {
    di->finalize();
  }
  return true;
}

bool lower_entry_shim(llvm::Module *module, std::string *error) {
  llvm::LLVMContext &context = module->getContext();
  const RtFns rt = declare_runtime(module);
  auto *entry_type = llvm::FunctionType::get(llvm::Type::getInt32Ty(context), false);
  auto *entry = llvm::Function::Create(entry_type, llvm::Function::ExternalLinkage, "kinglet_main",
                                       module);
  llvm::BasicBlock *entry_bb = llvm::BasicBlock::Create(context, "entry", entry);
  llvm::IRBuilder<> builder(entry_bb);
  llvm::Function *user_main = module->getFunction("kinglet_user_main");
  if (user_main == nullptr) {
    user_main = llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getInt64Ty(context), false),
                                       llvm::Function::ExternalLinkage, "kinglet_user_main", module);
  }
  llvm::Value *raw_result = builder.CreateCall(user_main, {});
  llvm::Value *exit_code = builder.CreateCall(rt.exit_code, {raw_result});
  builder.CreateRet(exit_code);
  return true;
}

std::string temp_object_path(const std::string &tag, const std::string &out_path) {
  const unsigned long tag_hash =
      static_cast<unsigned long>(std::hash<std::string>{}(out_path + tag));
  return (std::filesystem::temp_directory_path() /
          ("kinglet-" + std::to_string(tag_hash) + ".o"))
      .string();
}

// Canonical per-shard fingerprint for the object cache. Pool indices are
// resolved to their values (strings, function symbols) so that edits in one
// module which shift the shared constant pool do not invalidate other
// modules' cached objects. Struct and enum metadata stay global because
// lowering embeds module-wide type and field indices.
std::string shard_fingerprint_text(const KirModule &shard, const KirModule &full,
                                   const NativeCompileOptions &options) {
  std::ostringstream out;
  out << "salt:" << options.cache_salt << '\n';
  out << "triple:" << llvm::sys::getDefaultTargetTriple() << '\n';
  out << "debug:" << (options.debug_info ? 1 : 0) << '\n';
  for (const KirStructMeta &meta : full.struct_metas) {
    out << "struct:" << meta.name;
    for (const std::string &field : meta.field_names) {
      out << ',' << field;
    }
    out << '\n';
  }
  for (const KirEnumMeta &meta : full.enum_metas) {
    out << "enum:" << meta.name;
    for (std::size_t v = 0; v < meta.variants.size(); ++v) {
      out << ',' << meta.variants[v] << '/'
          << (v < meta.variant_param_counts.size() ? meta.variant_param_counts[v] : 0);
    }
    out << '\n';
  }
  for (const KirFunction &fn : shard.functions) {
    out << "fn:" << mangled_native_symbol(fn.name, fn.source_path) << ':' << fn.param_count << '\n';
    for (const KirBasicBlock &bb : fn.blocks) {
      for (const KirInstr &instr : bb.instrs) {
        out << kir_opcode_name(instr.op);
        const bool string_pool_op = instr.op == KirOpcode::ConstString ||
                                    instr.op == KirOpcode::FieldGet ||
                                    instr.op == KirOpcode::FieldSet;
        if (string_pool_op && !instr.operands.empty()) {
          const int idx = instr.operands[0];
          if (idx >= 0 && static_cast<std::size_t>(idx) < full.constant_strings.size()) {
            out << " s\"" << full.constant_strings[static_cast<std::size_t>(idx)] << '"';
          } else {
            out << " s?" << idx;
          }
        } else if (instr.op == KirOpcode::ConstFn && !instr.operands.empty()) {
          const int idx = instr.operands[0];
          if (idx >= 0 && static_cast<std::size_t>(idx) < full.function_symbols.size()) {
            out << " f" << full.function_symbols[static_cast<std::size_t>(idx)] << '/'
                << (static_cast<std::size_t>(idx) < full.function_param_counts.size()
                        ? full.function_param_counts[static_cast<std::size_t>(idx)]
                        : -1);
          } else {
            out << " f?" << idx;
          }
        } else {
          for (int32_t operand : instr.operands) {
            out << ' ' << operand;
          }
        }
        out << '@' << instr.line << ':' << instr.col << '\n';
      }
    }
  }
  return out.str();
}

std::string shard_stamp(const KirModule &shard, const KirModule &full,
                        const NativeCompileOptions &options) {
  llvm::SHA256 hasher;
  const std::string text = shard_fingerprint_text(shard, full, options);
  hasher.update(text);
#if LLVM_VERSION_MAJOR >= 16
  const auto digest = hasher.final();
  return llvm::toHex(llvm::ArrayRef<uint8_t>(digest.data(), digest.size()),
                     /*LowerCase=*/true);
#else
  const llvm::StringRef digest = hasher.final();
  return llvm::toHex(llvm::ArrayRef<uint8_t>(
                         reinterpret_cast<const uint8_t *>(digest.data()),
                         digest.size()),
                     /*LowerCase=*/true);
#endif
}

} // namespace

NativeCompileResult KirToLlvm::compile_executable(const KirModule &module,
                                                  const std::string &out_path,
                                                  const std::string &rt_lib_path,
                                                  const NativeCompileOptions &options) {
  std::string error;
  KirModule typed_module = module;
  infer_kir_types(&typed_module);
  specialize_kir_arithmetic(&typed_module);
  const KirModule &module_ref = typed_module;
  std::map<std::string, KirModule> shards;
  for (const KirFunction &fn : module_ref.functions) {
    const std::string key = fn.source_path.empty() ? "__entry__" : fn.source_path;
    shards[key].functions.push_back(fn);
  }
  if (shards.empty()) {
    return {.ok = false, .error = "KIR module has no functions"};
  }

  const bool use_cache = !options.object_cache_dir.empty();
  if (use_cache) {
    std::error_code cache_ec;
    std::filesystem::create_directories(options.object_cache_dir, cache_ec);
    if (cache_ec) {
      return {.ok = false,
              .error = "cannot create object cache dir " + options.object_cache_dir +
                       ": " + cache_ec.message()};
    }
  }

  std::vector<std::string> obj_paths;
  // Only freshly emitted (non-cached) objects are removed after linking.
  std::vector<std::string> temp_objs;
  std::size_t shard_idx = 0;
  for (auto &[key, shard] : shards) {
    copy_kir_metadata(&shard, module_ref);

    std::string obj_path;
    if (use_cache) {
      const std::string stamp = shard_stamp(shard, module_ref, options);
      obj_path = (std::filesystem::path(options.object_cache_dir) / (stamp + ".o")).string();
      if (std::filesystem::exists(obj_path)) {
        obj_paths.push_back(obj_path);
        ++shard_idx;
        continue;
      }
    } else {
      obj_path = temp_object_path(key, out_path);
      temp_objs.push_back(obj_path);
    }

    llvm::LLVMContext context;
    llvm::Module llvm_module("kinglet_mod_" + std::to_string(shard_idx++), context);
    if (!lower_user_functions(&llvm_module, shard, module_ref, options.debug_info, &error)) {
      return {.ok = false, .error = error};
    }
    if (llvm::verifyModule(llvm_module, &llvm::errs())) {
      return {.ok = false, .error = "invalid LLVM module after lowering"};
    }
    if (use_cache) {
      // Emit to a private temp and rename so concurrent builds see either a
      // complete object or none.
      const std::string tmp_path = obj_path + ".tmp";
      if (!emit_object(llvm_module, tmp_path, &error)) {
        return {.ok = false, .error = error};
      }
      std::error_code rename_ec;
      std::filesystem::rename(tmp_path, obj_path, rename_ec);
      if (rename_ec) {
        return {.ok = false,
                .error = "cannot move object into cache: " + rename_ec.message()};
      }
    } else if (!emit_object(llvm_module, obj_path, &error)) {
      return {.ok = false, .error = error};
    }
    obj_paths.push_back(obj_path);
  }

  llvm::LLVMContext entry_context;
  llvm::Module entry_module("kinglet_entry", entry_context);
  if (!lower_entry_shim(&entry_module, &error)) {
    return {.ok = false, .error = error};
  }
  if (llvm::verifyModule(entry_module, &llvm::errs())) {
    return {.ok = false, .error = "invalid LLVM entry module"};
  }
  const std::string entry_obj = temp_object_path("entry", out_path);
  if (!emit_object(entry_module, entry_obj, &error)) {
    return {.ok = false, .error = error};
  }
  obj_paths.push_back(entry_obj);
  temp_objs.push_back(entry_obj);

  if (!link_objects(obj_paths, rt_lib_path, out_path, &error)) {
    std::error_code ec;
    for (const std::string &obj_path : temp_objs) {
      std::filesystem::remove(obj_path, ec);
    }
    return {.ok = false, .error = error};
  }

#if defined(__APPLE__)
  // Mach-O keeps DWARF in the object files (debug map); bake a dSYM before the
  // temporary objects are removed.
  if (options.debug_info) {
    const std::string dsym_cmd = "dsymutil \"" + out_path + "\"";
    if (std::system(dsym_cmd.c_str()) != 0) {
      std::error_code ec;
      for (const std::string &obj_path : temp_objs) {
        std::filesystem::remove(obj_path, ec);
      }
      return {.ok = false, .error = "dsymutil failed for " + out_path};
    }
  }
#endif

  std::error_code ec;
  for (const std::string &obj_path : temp_objs) {
    std::filesystem::remove(obj_path, ec);
  }
  return {.ok = true};
}

} // namespace kinglet
