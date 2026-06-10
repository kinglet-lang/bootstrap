#include "codegen/llvm/kir_to_llvm.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace kinglet {

namespace {

std::string fn_symbol(const std::string &name) {
  if (name == "main") {
    return "kinglet_user_main";
  }
  return "kinglet_fn_" + name;
}

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

llvm::Value *pop_value(std::vector<llvm::Value *> *stack, std::string *error) {
  if (stack->empty()) {
    *error = "native lowering stack underflow";
    return nullptr;
  }
  llvm::Value *value = stack->back();
  stack->pop_back();
  return value;
}

struct RtFns {
  llvm::Function *string_new = nullptr;
  llvm::Function *array_new = nullptr;
  llvm::Function *array_get = nullptr;
  llvm::Function *value_len = nullptr;
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
  rt.array_get = llvm::Function::Create(llvm::FunctionType::get(i64, {i64, i32}, false),
                                        llvm::Function::ExternalLinkage, "kl_array_get", module);
  rt.value_len = llvm::Function::Create(llvm::FunctionType::get(i32, {i64}, false),
                                        llvm::Function::ExternalLinkage, "kl_value_len", module);
  return rt;
}

llvm::Value *bool_to_i64(llvm::IRBuilder<> &builder, llvm::Value *cond) {
  return builder.CreateZExt(cond, builder.getInt64Ty());
}

llvm::Value *binop(llvm::IRBuilder<> &builder, KirOpcode op, llvm::Value *left,
                   llvm::Value *right) {
  switch (op) {
  case KirOpcode::IAdd:
    return builder.CreateAdd(left, right);
  case KirOpcode::ISub:
    return builder.CreateSub(left, right);
  case KirOpcode::IMul:
    return builder.CreateMul(left, right);
  case KirOpcode::IDiv:
    return builder.CreateSDiv(left, right);
  case KirOpcode::IMod:
    return builder.CreateSRem(left, right);
  default:
    return nullptr;
  }
}

llvm::Value *icmp(llvm::IRBuilder<> &builder, KirOpcode op, llvm::Value *left,
                  llvm::Value *right) {
  switch (op) {
  case KirOpcode::ICmpEq:
    return builder.CreateICmpEQ(left, right);
  case KirOpcode::ICmpNeq:
    return builder.CreateICmpNE(left, right);
  case KirOpcode::ICmpLt:
    return builder.CreateICmpSLT(left, right);
  case KirOpcode::ICmpGt:
    return builder.CreateICmpSGT(left, right);
  case KirOpcode::ICmpLe:
    return builder.CreateICmpSLE(left, right);
  case KirOpcode::ICmpGe:
    return builder.CreateICmpSGE(left, right);
  default:
    return nullptr;
  }
}

bool emit_object(llvm::Module &module, const std::string &obj_path, std::string *error) {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  std::string triple_err;
  const llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
  module.setTargetTriple(triple);

  const llvm::Target *target = llvm::TargetRegistry::lookupTarget(triple.str(), triple_err);
  if (target == nullptr) {
    *error = "LLVM target lookup failed: " + triple_err;
    return false;
  }

  llvm::TargetOptions options;
  std::string cpu = llvm::sys::getHostCPUName().str();
  llvm::TargetMachine *machine =
      target->createTargetMachine(triple, cpu, "", options, llvm::Reloc::Model::PIC_);
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
  if (machine->addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
    *error = "LLVM cannot emit an object file for this target";
    return false;
  }
  pass.run(module);
  dest.flush();
  return true;
}

bool link_executable(const std::string &obj_path, const std::string &rt_lib_path,
                     const std::string &out_path, std::string *error) {
  std::ostringstream cmd;
  cmd << "clang++ -o ";
  cmd << '"' << out_path << "\" ";
  cmd << '"' << obj_path << "\" ";
  cmd << '"' << rt_lib_path << '"';
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
      if (instr->op == KirOpcode::Br || instr->op == KirOpcode::CondBr) {
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
        if (instr->op == KirOpcode::CondBr) {
          leaders.insert(i + 1);
        }
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
    std::vector<llvm::Value *> stack;

    for (std::size_t i = 0; i < linear_.size(); ++i) {
      const KirInstr *instr = linear_[i];
      llvm::BasicBlock *bb = block_index(i);
      llvm::IRBuilder<> builder(bb);
      auto push = [&](llvm::Value *v) { stack.push_back(v); };

      auto pop_binop = [&](KirOpcode op) -> bool {
        llvm::Value *rhs = pop_value(&stack, error);
        llvm::Value *lhs = pop_value(&stack, error);
        if (lhs == nullptr || rhs == nullptr) {
          return false;
        }
        llvm::Value *result = binop(builder, op, lhs, rhs);
        if (result == nullptr) {
          *error = "unsupported integer binop";
          return false;
        }
        push(result);
        temps[i] = result;
        return true;
      };

      auto pop_icmp = [&](KirOpcode op) -> bool {
        llvm::Value *rhs = pop_value(&stack, error);
        llvm::Value *lhs = pop_value(&stack, error);
        if (lhs == nullptr || rhs == nullptr) {
          return false;
        }
        llvm::Value *result = bool_to_i64(builder, icmp(builder, op, lhs, rhs));
        push(result);
        temps[i] = result;
        return true;
      };

      switch (instr->op) {
      case KirOpcode::ConstInt:
        push(llvm::ConstantInt::get(i64, instr->operands[0]));
        temps[i] = stack.back();
        break;
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
          *error = "const_string pool index out of range";
          return false;
        }
        const std::string &text = kir_module_.constant_strings[static_cast<std::size_t>(pool_idx)];
        llvm::Value *data = builder.CreateGlobalString(text);
        llvm::Value *len =
            llvm::ConstantInt::get(i32, static_cast<int>(text.size()));
        llvm::Value *handle = builder.CreateCall(rt_.string_new, {data, len});
        push(handle);
        temps[i] = handle;
        break;
      }
      case KirOpcode::ConstFn: {
        const int fn_index = instr->operands[0];
        if (fn_index < 0 || static_cast<std::size_t>(fn_index) >= kir_module_.functions.size()) {
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
        push(loaded);
        temps[i] = loaded;
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
        if (pop_value(&stack, error) == nullptr) {
          return false;
        }
        break;
      case KirOpcode::IAdd:
      case KirOpcode::ISub:
      case KirOpcode::IMul:
      case KirOpcode::IDiv:
      case KirOpcode::IMod:
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
          llvm::Value *result = binop(builder, instr->op, lhs, rhs);
          if (result == nullptr) {
            *error = "unsupported indexed binop";
            return false;
          }
          push(result);
          temps[i] = result;
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
      case KirOpcode::Call: {
        const int argc = instr->operands[0];
        if (argc < 0) {
          *error = "call arg count invalid";
          return false;
        }
        llvm::Value *callee_tag = pop_value(&stack, error);
        if (callee_tag == nullptr) {
          return false;
        }
        std::vector<llvm::Value *> args(static_cast<std::size_t>(argc));
        for (int arg = argc - 1; arg >= 0; --arg) {
          args[static_cast<std::size_t>(arg)] = pop_value(&stack, error);
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
        if (fn_index < 0 || static_cast<std::size_t>(fn_index) >= kir_module_.functions.size()) {
          *error = "call function index out of range";
          return false;
        }
        const std::string &target_name = kir_module_.functions[static_cast<std::size_t>(fn_index)].name;
        llvm::Function *target = llvm_fn_->getParent()->getFunction(fn_symbol(target_name));
        if (target == nullptr) {
          *error = "call target not lowered";
          return false;
        }
        llvm::Value *result = builder.CreateCall(target, args);
        push(result);
        temps[i] = result;
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
          llvm::Value *elem = pop_value(&stack, error);
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
      case KirOpcode::IndexGet: {
        llvm::Value *index = pop_value(&stack, error);
        llvm::Value *array = pop_value(&stack, error);
        if (index == nullptr || array == nullptr) {
          return false;
        }
        llvm::Value *idx32 = builder.CreateTrunc(index, i32);
        llvm::Value *value = builder.CreateCall(rt_.array_get, {array, idx32});
        push(value);
        temps[i] = value;
        break;
      }
      case KirOpcode::ArrayLen: {
        llvm::Value *obj = pop_value(&stack, error);
        if (obj == nullptr) {
          return false;
        }
        llvm::Value *len32 = builder.CreateCall(rt_.value_len, {obj});
        llvm::Value *len64 = builder.CreateSExt(len32, i64);
        push(len64);
        temps[i] = len64;
        break;
      }
      case KirOpcode::Ret: {
        if (bb->getTerminator() != nullptr) {
          break;
        }
        llvm::Value *retv = llvm::ConstantInt::get(i64, 0);
        if (!instr->operands.empty()) {
          const int idx = instr->operands[0];
          if (idx >= 0 && static_cast<std::size_t>(idx) < i) {
            retv = temps[static_cast<std::size_t>(idx)];
          }
        } else if (!stack.empty()) {
          retv = stack.back();
        }
        builder.CreateRet(retv);
        break;
      }
      case KirOpcode::Br: {
        if (bb->getTerminator() != nullptr) {
          break;
        }
        const int rel = instr->operands[0];
        const std::size_t target = i + 1 + static_cast<std::size_t>(rel);
        builder.CreateBr(block_index(target));
        break;
      }
      case KirOpcode::CondBr: {
        if (bb->getTerminator() != nullptr) {
          break;
        }
        llvm::Value *cond_i64 = pop_value(&stack, error);
        if (cond_i64 == nullptr) {
          return false;
        }
        llvm::Value *cond = builder.CreateICmpNE(cond_i64, llvm::ConstantInt::get(i64, 0));
        const int rel = instr->operands[0];
        const std::size_t false_target = i + 1 + static_cast<std::size_t>(rel);
        const std::size_t true_target = i + 1;
        builder.CreateCondBr(cond, block_index(true_target), block_index(false_target));
        break;
      }
      default:
        *error = "unsupported KIR opcode in native lowering";
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
    return true;
  }

private:
  llvm::LLVMContext *context_;
  const KirModule &kir_module_;
  llvm::Function *llvm_fn_;
  const RtFns &rt_;
  std::vector<llvm::AllocaInst *> local_slots_;
  std::vector<const KirInstr *> linear_;
};

bool lower_module(llvm::Module *module, const KirModule &kir_module, std::string *error) {
  llvm::LLVMContext &context = module->getContext();
  llvm::Type *i32 = llvm::Type::getInt32Ty(context);
  llvm::Type *i64 = llvm::Type::getInt64Ty(context);
  const RtFns rt = declare_runtime(module);

  for (const KirFunction &fn : kir_module.functions) {
    std::vector<llvm::Type *> param_types(static_cast<std::size_t>(fn.param_count), i64);
    auto *fn_type = llvm::FunctionType::get(i64, param_types, false);
    llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, fn_symbol(fn.name), module);
  }

  for (const KirFunction &fn : kir_module.functions) {
    llvm::Function *llvm_fn = module->getFunction(fn_symbol(fn.name));
    FunctionLowerer lowerer(&context, kir_module, llvm_fn, rt);
    if (!lowerer.lower(fn, error)) {
      return false;
    }
  }

  auto *entry_type = llvm::FunctionType::get(i32, false);
  auto *entry = llvm::Function::Create(entry_type, llvm::Function::ExternalLinkage, "kinglet_main",
                                       module);
  llvm::BasicBlock *entry_bb = llvm::BasicBlock::Create(context, "entry", entry);
  llvm::IRBuilder<> builder(entry_bb);
  llvm::Function *user_main = module->getFunction("kinglet_user_main");
  if (user_main == nullptr) {
    *error = "KIR module missing main";
    return false;
  }
  llvm::Value *exit_code = builder.CreateTrunc(builder.CreateCall(user_main, {}), i32);
  builder.CreateRet(exit_code);
  return true;
}

} // namespace

NativeCompileResult KirToLlvm::compile_executable(const KirModule &module,
                                                  const std::string &out_path,
                                                  const std::string &rt_lib_path) {
  llvm::LLVMContext context;
  llvm::Module llvm_module("kinglet", context);

  std::string error;
  if (!lower_module(&llvm_module, module, &error)) {
    return {.ok = false, .error = error};
  }

  if (llvm::verifyModule(llvm_module, &llvm::errs())) {
    return {.ok = false, .error = "invalid LLVM module after lowering"};
  }

  const std::filesystem::path obj_path =
      std::filesystem::temp_directory_path() /
      ("kinglet-" + std::to_string(static_cast<unsigned long>(std::hash<std::string>{}(out_path))) +
       ".o");

  if (!emit_object(llvm_module, obj_path.string(), &error)) {
    return {.ok = false, .error = error};
  }

  if (!link_executable(obj_path.string(), rt_lib_path, out_path, &error)) {
    std::error_code ec;
    std::filesystem::remove(obj_path, ec);
    return {.ok = false, .error = error};
  }

  std::error_code ec;
  std::filesystem::remove(obj_path, ec);
  return {.ok = true};
}

} // namespace kinglet
