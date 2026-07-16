// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "ir/kir_recorder.h"

#include "ir/kir_numeric.h"
#include "ir/lowering_op.h"

#include <cstring>

namespace kinglet {

namespace {

KirInstr rec(KirOpcode op, std::vector<int32_t> operands, ast::SourceLocation loc) {
  KirInstr instr;
  instr.op = op;
  instr.operands = std::move(operands);
  instr.line = loc.line;
  instr.col = loc.column;
  return instr;
}

std::vector<int32_t> encode_i64_operands(int64_t value) {
  return {static_cast<int32_t>(value), static_cast<int32_t>(static_cast<uint64_t>(value) >> 32)};
}

std::vector<int32_t> encode_f32_operands(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return {static_cast<int32_t>(bits)};
}

std::vector<int32_t> encode_f64_operands(double value) {
  int64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return encode_i64_operands(bits);
}

} // namespace

void KirRecorder::begin_function(const std::string &name, int param_count,
                                 const std::string &source_path, const std::string &mangled_name) {
  fn_ = KirFunction{};
  fn_.name = name;
  fn_.mangled_name = mangled_name;
  fn_.source_path = source_path;
  fn_.param_count = param_count;
  bb_ = KirBasicBlock{};
  bb_.label = "bb0";
  active_ = true;
}

void KirRecorder::end_function(KirModule *module) {
  if (!active_ || !module) {
    return;
  }
  fn_.blocks.push_back(std::move(bb_));
  module->functions.push_back(std::move(fn_));
  active_ = false;
}

std::size_t KirRecorder::instr_count() const {
  return bb_.instrs.size();
}

std::size_t KirRecorder::record_jump(LoweringOp op, ast::SourceLocation location) {
  if (!active_) {
    return 0;
  }
  KirOpcode kir_op = KirOpcode::Br;
  if (op == LoweringOp::JmpFalse) {
    kir_op = KirOpcode::CondBr;
  } else if (op == LoweringOp::JmpIfErr) {
    kir_op = KirOpcode::JmpIfErr;
  }
  bb_.instrs.push_back(rec(kir_op, {0}, location));
  return bb_.instrs.size() - 1;
}

void KirRecorder::patch_jump(std::size_t jump_instr_index, int32_t relative_offset) {
  if (!active_ || jump_instr_index >= bb_.instrs.size()) {
    return;
  }
  bb_.instrs[jump_instr_index].operands[0] = relative_offset;
}

void KirRecorder::patch_operand(std::size_t instr_index, int32_t operand) {
  if (!active_ || instr_index >= bb_.instrs.size()) {
    return;
  }
  if (bb_.instrs[instr_index].operands.empty()) {
    bb_.instrs[instr_index].operands.push_back(operand);
  } else {
    bb_.instrs[instr_index].operands[0] = operand;
  }
}

void KirRecorder::on_constant(const Value &value, uint32_t pool_index, ast::SourceLocation location,
                              KirType numeric_type) {
  if (!active_) {
    return;
  }
  if (value.type == ValueType::Int || value.type == ValueType::Char) {
    const int64_t v = value.as_int;
    KirType width = numeric_type;
    if (width == KirType::Any) {
      width = value.type == ValueType::Char ? KirType::Int8 : KirType::Int64;
    }
    switch (kir_type_normalize(width)) {
    case KirType::Int8:
    case KirType::UInt8:
      bb_.instrs.push_back(rec(KirOpcode::ConstU8, {static_cast<int32_t>(v & 0xff)}, location));
      break;
    case KirType::Int32:
    case KirType::UInt32:
      bb_.instrs.push_back(rec(KirOpcode::ConstI32, {static_cast<int32_t>(v)}, location));
      break;
    case KirType::Int64:
    case KirType::UInt64:
      bb_.instrs.push_back(rec(KirOpcode::ConstI64, encode_i64_operands(v), location));
      break;
    default:
      bb_.instrs.push_back(rec(KirOpcode::ConstInt, encode_i64_operands(v), location));
      break;
    }
  } else if (value.type == ValueType::Bool) {
    bb_.instrs.push_back(rec(KirOpcode::ConstBool, {value.as_bool ? 1 : 0}, location));
  } else if (value.type == ValueType::Double) {
    KirType width = numeric_type;
    if (width == KirType::Any) {
      width = KirType::Float32;
    }
    if (kir_type_normalize(width) == KirType::Float64) {
      bb_.instrs.push_back(
          rec(KirOpcode::ConstF64, encode_f64_operands(value.as_double_storage), location));
    } else {
      bb_.instrs.push_back(rec(KirOpcode::ConstF32,
                               encode_f32_operands(static_cast<float>(value.as_double_storage)),
                               location));
    }
  } else if (value.type == ValueType::Null) {
    bb_.instrs.push_back(rec(KirOpcode::ConstNull, {}, location));
  } else if (value.type == ValueType::String) {
    bb_.instrs.push_back(rec(KirOpcode::ConstString, {static_cast<int32_t>(pool_index)}, location));
  } else if (value.type == ValueType::Function) {
    bb_.instrs.push_back(
        rec(KirOpcode::ConstFn, {static_cast<int32_t>(value.function_idx)}, location));
  } else if (value.type == ValueType::NativeFunction) {
    bb_.instrs.push_back(
        rec(KirOpcode::ConstNativeFn, {static_cast<int32_t>(value.native_fn)}, location));
  }
}

void KirRecorder::on_emit(LoweringOp op, uint32_t operand, ast::SourceLocation location) {
  if (!active_) {
    return;
  }
  switch (op) {
  case LoweringOp::True:
    bb_.instrs.push_back(rec(KirOpcode::ConstBool, {1}, location));
    break;
  case LoweringOp::False:
    bb_.instrs.push_back(rec(KirOpcode::ConstBool, {0}, location));
    break;
  case LoweringOp::Add:
    bb_.instrs.push_back(rec(KirOpcode::IAdd, {}, location));
    break;
  case LoweringOp::AddI32:
    bb_.instrs.push_back(rec(KirOpcode::IAdd32, {}, location));
    break;
  case LoweringOp::Subtract:
    bb_.instrs.push_back(rec(KirOpcode::ISub, {}, location));
    break;
  case LoweringOp::SubtractI32:
    bb_.instrs.push_back(rec(KirOpcode::ISub32, {}, location));
    break;
  case LoweringOp::Multiply:
    bb_.instrs.push_back(rec(KirOpcode::IMul, {}, location));
    break;
  case LoweringOp::MultiplyI32:
    bb_.instrs.push_back(rec(KirOpcode::IMul32, {}, location));
    break;
  case LoweringOp::Divide:
    bb_.instrs.push_back(rec(KirOpcode::IDiv, {}, location));
    break;
  case LoweringOp::DivideI32:
    bb_.instrs.push_back(rec(KirOpcode::IDiv32, {}, location));
    break;
  case LoweringOp::Modulo:
    bb_.instrs.push_back(rec(KirOpcode::IMod, {}, location));
    break;
  case LoweringOp::ModuloI32:
    bb_.instrs.push_back(rec(KirOpcode::IMod32, {}, location));
    break;
  case LoweringOp::Not:
    bb_.instrs.push_back(rec(KirOpcode::Not, {}, location));
    break;
  case LoweringOp::BitNot:
    bb_.instrs.push_back(rec(KirOpcode::BitNot, {}, location));
    break;
  case LoweringOp::BitAnd:
    bb_.instrs.push_back(rec(KirOpcode::BitAnd, {}, location));
    break;
  case LoweringOp::BitOr:
    bb_.instrs.push_back(rec(KirOpcode::BitOr, {}, location));
    break;
  case LoweringOp::BitXor:
    bb_.instrs.push_back(rec(KirOpcode::BitXor, {}, location));
    break;
  case LoweringOp::Shl:
    bb_.instrs.push_back(rec(KirOpcode::Shl, {}, location));
    break;
  case LoweringOp::Shr:
    bb_.instrs.push_back(rec(KirOpcode::Shr, {}, location));
    break;
  case LoweringOp::Eq:
    bb_.instrs.push_back(rec(KirOpcode::ICmpEq, {}, location));
    break;
  case LoweringOp::Neq:
    bb_.instrs.push_back(rec(KirOpcode::ICmpNeq, {}, location));
    break;
  case LoweringOp::Lt:
    bb_.instrs.push_back(rec(KirOpcode::ICmpLt, {}, location));
    break;
  case LoweringOp::Gt:
    bb_.instrs.push_back(rec(KirOpcode::ICmpGt, {}, location));
    break;
  case LoweringOp::Le:
    bb_.instrs.push_back(rec(KirOpcode::ICmpLe, {}, location));
    break;
  case LoweringOp::Ge:
    bb_.instrs.push_back(rec(KirOpcode::ICmpGe, {}, location));
    break;
  case LoweringOp::LoadLocal:
    bb_.instrs.push_back(rec(KirOpcode::LoadLocal, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::LoadLocalAddr:
    bb_.instrs.push_back(rec(KirOpcode::LoadLocalAddr, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::DerefLoad:
    bb_.instrs.push_back(rec(KirOpcode::DerefLoad, {}, location));
    break;
  case LoweringOp::DerefStore:
    bb_.instrs.push_back(rec(KirOpcode::DerefStore, {}, location));
    break;
  case LoweringOp::StoreLocal:
    bb_.instrs.push_back(rec(KirOpcode::StoreLocal, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::Null:
    bb_.instrs.push_back(rec(KirOpcode::ConstNull, {}, location));
    break;
  case LoweringOp::Pop:
    bb_.instrs.push_back(rec(KirOpcode::Pop, {}, location));
    break;
  case LoweringOp::Call:
    bb_.instrs.push_back(rec(KirOpcode::Call, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::Return:
    bb_.instrs.push_back(rec(KirOpcode::Ret, {}, location));
    break;
  case LoweringOp::PushHandler:
    bb_.instrs.push_back(rec(KirOpcode::PushHandler, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::PopHandler:
    bb_.instrs.push_back(rec(KirOpcode::PopHandler, {}, location));
    break;
  case LoweringOp::PropagateErr:
    bb_.instrs.push_back(rec(KirOpcode::PropagateErr, {}, location));
    break;
  case LoweringOp::StructNew:
    bb_.instrs.push_back(rec(KirOpcode::StructNew, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::BorrowFieldMut:
    bb_.instrs.push_back(rec(KirOpcode::BorrowFieldMut, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::FieldGet:
    bb_.instrs.push_back(rec(KirOpcode::FieldGet, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::FieldSet:
    bb_.instrs.push_back(rec(KirOpcode::FieldSet, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::ArrayNew:
    bb_.instrs.push_back(rec(KirOpcode::ArrayNew, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::DenseArrayNew: {
    int rows = 0;
    int cols = 0;
    unpack_dense2d_shape(static_cast<uint32_t>(operand), &rows, &cols);
    bb_.instrs.push_back(rec(KirOpcode::DenseArrayNew, {rows, cols}, location));
    break;
  }
  case LoweringOp::IndexGet:
    bb_.instrs.push_back(rec(KirOpcode::IndexGet, {}, location));
    break;
  case LoweringOp::IndexSet:
    bb_.instrs.push_back(rec(KirOpcode::IndexSet, {}, location));
    break;
  case LoweringOp::BorrowIndexMut:
    bb_.instrs.push_back(rec(KirOpcode::BorrowIndexMut, {}, location));
    break;
  case LoweringOp::ArrayLen:
    bb_.instrs.push_back(rec(KirOpcode::ArrayLen, {}, location));
    break;
  case LoweringOp::ArraySlice:
    bb_.instrs.push_back(rec(KirOpcode::ArraySlice, {}, location));
    break;
  case LoweringOp::ArrayPush:
    bb_.instrs.push_back(rec(KirOpcode::ArrayPush, {}, location));
    break;
  case LoweringOp::ArrayResize:
    bb_.instrs.push_back(rec(KirOpcode::ArrayResize, {}, location));
    break;
  case LoweringOp::ArrayPop:
    bb_.instrs.push_back(rec(KirOpcode::ArrayPop, {}, location));
    break;
  case LoweringOp::ArrayRemove:
    bb_.instrs.push_back(rec(KirOpcode::ArrayRemove, {}, location));
    break;
  case LoweringOp::ArrayContains:
    bb_.instrs.push_back(rec(KirOpcode::ArrayContains, {}, location));
    break;
  case LoweringOp::ArrayClear:
    bb_.instrs.push_back(rec(KirOpcode::ArrayClear, {}, location));
    break;
  case LoweringOp::ArrayInsert:
    bb_.instrs.push_back(rec(KirOpcode::ArrayInsert, {}, location));
    break;
  case LoweringOp::ArrayIndexOf:
    bb_.instrs.push_back(rec(KirOpcode::ArrayIndexOf, {}, location));
    break;
  case LoweringOp::ArrayReverse:
    bb_.instrs.push_back(rec(KirOpcode::ArrayReverse, {}, location));
    break;
  case LoweringOp::StringStartsWith:
    bb_.instrs.push_back(rec(KirOpcode::StrStartsWith, {}, location));
    break;
  case LoweringOp::StringEndsWith:
    bb_.instrs.push_back(rec(KirOpcode::StrEndsWith, {}, location));
    break;
  case LoweringOp::StringReplace:
    bb_.instrs.push_back(rec(KirOpcode::StrReplace, {}, location));
    break;
  case LoweringOp::StringSplit:
    bb_.instrs.push_back(rec(KirOpcode::StrSplit, {}, location));
    break;
  case LoweringOp::StringTrim:
    bb_.instrs.push_back(rec(KirOpcode::StrTrim, {}, location));
    break;
  case LoweringOp::StringToUpper:
    bb_.instrs.push_back(rec(KirOpcode::StrToUpper, {}, location));
    break;
  case LoweringOp::StringToLower:
    bb_.instrs.push_back(rec(KirOpcode::StrToLower, {}, location));
    break;
  case LoweringOp::MapNew:
    bb_.instrs.push_back(rec(KirOpcode::MapNew, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::MapHas:
    bb_.instrs.push_back(rec(KirOpcode::MapHas, {}, location));
    break;
  case LoweringOp::MapKeys:
    bb_.instrs.push_back(rec(KirOpcode::MapKeys, {}, location));
    break;
  case LoweringOp::EnumVariant:
    bb_.instrs.push_back(rec(KirOpcode::EnumVariant, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::EnumVariantPayload:
    bb_.instrs.push_back(
        rec(KirOpcode::EnumVariantPayload, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::EnumPayloadGet:
    bb_.instrs.push_back(rec(KirOpcode::EnumPayloadGet, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::CastTo:
    bb_.instrs.push_back(rec(KirOpcode::CastTo, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::FloatToBits:
    bb_.instrs.push_back(rec(KirOpcode::FloatToBits, {}, location));
    break;
  case LoweringOp::BitsToFloat:
    bb_.instrs.push_back(rec(KirOpcode::BitsToFloat, {}, location));
    break;
  case LoweringOp::NativeOut:
    bb_.instrs.push_back(rec(KirOpcode::NativeOut, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::NativeOutLn:
    bb_.instrs.push_back(rec(KirOpcode::NativeOutLn, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::NativeErr:
    bb_.instrs.push_back(rec(KirOpcode::NativeErr, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::NativeErrLn:
    bb_.instrs.push_back(rec(KirOpcode::NativeErrLn, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::NativeIn:
    bb_.instrs.push_back(rec(KirOpcode::NativeIn, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::NativeInSecret:
    bb_.instrs.push_back(rec(KirOpcode::NativeInSecret, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::NativeFsRead:
    bb_.instrs.push_back(rec(KirOpcode::NativeFsRead, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::NativeFsWrite:
    bb_.instrs.push_back(rec(KirOpcode::NativeFsWrite, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::NativeFsListdir:
    bb_.instrs.push_back(rec(KirOpcode::NativeFsListdir, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::NativeSysArgs:
    bb_.instrs.push_back(rec(KirOpcode::NativeSysArgs, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::Negate:
    bb_.instrs.push_back(rec(KirOpcode::INeg, {}, location));
    break;
  case LoweringOp::Drop:
    bb_.instrs.push_back(rec(KirOpcode::Drop, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::NativeFsExists:
    bb_.instrs.push_back(rec(KirOpcode::NativeFsExists, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::NativeFsReadBytes:
    bb_.instrs.push_back(
        rec(KirOpcode::NativeFsReadBytes, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::NativeFsWriteBytes:
    bb_.instrs.push_back(
        rec(KirOpcode::NativeFsWriteBytes, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::NativeFsOpen:
    bb_.instrs.push_back(rec(KirOpcode::NativeFsOpen, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::NativeFsCreate:
    bb_.instrs.push_back(rec(KirOpcode::NativeFsCreate, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::NativeFileRead:
    bb_.instrs.push_back(rec(KirOpcode::NativeFileRead, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::NativeFileWrite:
    bb_.instrs.push_back(rec(KirOpcode::NativeFileWrite, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::NativeFileSize:
    bb_.instrs.push_back(rec(KirOpcode::NativeFileSize, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::NativeFileSync:
    bb_.instrs.push_back(rec(KirOpcode::NativeFileSync, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::NativeFileClose:
    bb_.instrs.push_back(rec(KirOpcode::NativeFileClose, {static_cast<int32_t>(operand)}, location));
    break;
  case LoweringOp::NativeFileIsOpen:
    bb_.instrs.push_back(
        rec(KirOpcode::NativeFileIsOpen, {static_cast<int32_t>(operand)}, location));
    break;
  default:
    bb_.instrs.push_back(rec(KirOpcode::Nop, {}, location));
    break;
  }
}

} // namespace kinglet
