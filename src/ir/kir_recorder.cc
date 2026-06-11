#include "ir/kir_recorder.h"

#include "ir/kir_numeric.h"

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
  return {static_cast<int32_t>(value),
          static_cast<int32_t>(static_cast<uint64_t>(value) >> 32)};
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
                                 const std::string &source_path) {
  fn_ = KirFunction{};
  fn_.name = name;
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

std::size_t KirRecorder::record_jump(OpCode op, ast::SourceLocation location) {
  if (!active_) {
    return 0;
  }
  KirOpcode kir_op = KirOpcode::Br;
  if (op == OpCode::JmpFalse) {
    kir_op = KirOpcode::CondBr;
  } else if (op == OpCode::JmpIfErr) {
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

void KirRecorder::on_constant(const Value &value, uint32_t pool_index,
                              ast::SourceLocation location, KirType numeric_type) {
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
      bb_.instrs.push_back(
          rec(KirOpcode::ConstU8, {static_cast<int32_t>(v & 0xff)}, location));
      break;
    case KirType::Int32:
    case KirType::UInt32:
      bb_.instrs.push_back(
          rec(KirOpcode::ConstI32, {static_cast<int32_t>(v)}, location));
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
      bb_.instrs.push_back(rec(
          KirOpcode::ConstF32,
          encode_f32_operands(static_cast<float>(value.as_double_storage)), location));
    }
  } else if (value.type == ValueType::Null) {
    bb_.instrs.push_back(rec(KirOpcode::ConstNull, {}, location));
  } else if (value.type == ValueType::String) {
    bb_.instrs.push_back(
        rec(KirOpcode::ConstString, {static_cast<int32_t>(pool_index)}, location));
  } else if (value.type == ValueType::Function) {
    bb_.instrs.push_back(
        rec(KirOpcode::ConstFn, {static_cast<int32_t>(value.function_idx)}, location));
  }
}

void KirRecorder::on_emit(OpCode op, uint32_t operand, ast::SourceLocation location) {
  if (!active_) {
    return;
  }
  switch (op) {
  case OpCode::True:
    bb_.instrs.push_back(rec(KirOpcode::ConstBool, {1}, location));
    break;
  case OpCode::False:
    bb_.instrs.push_back(rec(KirOpcode::ConstBool, {0}, location));
    break;
  case OpCode::Add:
    bb_.instrs.push_back(rec(KirOpcode::IAdd, {}, location));
    break;
  case OpCode::AddI32:
    bb_.instrs.push_back(rec(KirOpcode::IAdd32, {}, location));
    break;
  case OpCode::Subtract:
    bb_.instrs.push_back(rec(KirOpcode::ISub, {}, location));
    break;
  case OpCode::SubtractI32:
    bb_.instrs.push_back(rec(KirOpcode::ISub32, {}, location));
    break;
  case OpCode::Multiply:
    bb_.instrs.push_back(rec(KirOpcode::IMul, {}, location));
    break;
  case OpCode::MultiplyI32:
    bb_.instrs.push_back(rec(KirOpcode::IMul32, {}, location));
    break;
  case OpCode::Divide:
    bb_.instrs.push_back(rec(KirOpcode::IDiv, {}, location));
    break;
  case OpCode::DivideI32:
    bb_.instrs.push_back(rec(KirOpcode::IDiv32, {}, location));
    break;
  case OpCode::Modulo:
    bb_.instrs.push_back(rec(KirOpcode::IMod, {}, location));
    break;
  case OpCode::ModuloI32:
    bb_.instrs.push_back(rec(KirOpcode::IMod32, {}, location));
    break;
  case OpCode::Not:
    bb_.instrs.push_back(rec(KirOpcode::Not, {}, location));
    break;
  case OpCode::BitNot:
    bb_.instrs.push_back(rec(KirOpcode::BitNot, {}, location));
    break;
  case OpCode::BitAnd:
    bb_.instrs.push_back(rec(KirOpcode::BitAnd, {}, location));
    break;
  case OpCode::BitOr:
    bb_.instrs.push_back(rec(KirOpcode::BitOr, {}, location));
    break;
  case OpCode::BitXor:
    bb_.instrs.push_back(rec(KirOpcode::BitXor, {}, location));
    break;
  case OpCode::Shl:
    bb_.instrs.push_back(rec(KirOpcode::Shl, {}, location));
    break;
  case OpCode::Shr:
    bb_.instrs.push_back(rec(KirOpcode::Shr, {}, location));
    break;
  case OpCode::Eq:
    bb_.instrs.push_back(rec(KirOpcode::ICmpEq, {}, location));
    break;
  case OpCode::Neq:
    bb_.instrs.push_back(rec(KirOpcode::ICmpNeq, {}, location));
    break;
  case OpCode::Lt:
    bb_.instrs.push_back(rec(KirOpcode::ICmpLt, {}, location));
    break;
  case OpCode::Gt:
    bb_.instrs.push_back(rec(KirOpcode::ICmpGt, {}, location));
    break;
  case OpCode::Le:
    bb_.instrs.push_back(rec(KirOpcode::ICmpLe, {}, location));
    break;
  case OpCode::Ge:
    bb_.instrs.push_back(rec(KirOpcode::ICmpGe, {}, location));
    break;
  case OpCode::LoadLocal:
    bb_.instrs.push_back(rec(KirOpcode::LoadLocal, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::StoreLocal:
    bb_.instrs.push_back(rec(KirOpcode::StoreLocal, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::Null:
    bb_.instrs.push_back(rec(KirOpcode::ConstNull, {}, location));
    break;
  case OpCode::Pop:
    bb_.instrs.push_back(rec(KirOpcode::Pop, {}, location));
    break;
  case OpCode::Call:
    bb_.instrs.push_back(rec(KirOpcode::Call, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::Return:
    bb_.instrs.push_back(rec(KirOpcode::Ret, {}, location));
    break;
  case OpCode::PushHandler:
    bb_.instrs.push_back(rec(KirOpcode::PushHandler, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::PopHandler:
    bb_.instrs.push_back(rec(KirOpcode::PopHandler, {}, location));
    break;
  case OpCode::PropagateErr:
    bb_.instrs.push_back(rec(KirOpcode::PropagateErr, {}, location));
    break;
  case OpCode::StructNew:
    bb_.instrs.push_back(
        rec(KirOpcode::StructNew, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::FieldGet:
    bb_.instrs.push_back(
        rec(KirOpcode::FieldGet, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::FieldSet:
    bb_.instrs.push_back(
        rec(KirOpcode::FieldSet, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::ArrayNew:
    bb_.instrs.push_back(
        rec(KirOpcode::ArrayNew, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::IndexGet:
    bb_.instrs.push_back(rec(KirOpcode::IndexGet, {}, location));
    break;
  case OpCode::IndexSet:
    bb_.instrs.push_back(rec(KirOpcode::IndexSet, {}, location));
    break;
  case OpCode::ArrayLen:
    bb_.instrs.push_back(rec(KirOpcode::ArrayLen, {}, location));
    break;
  case OpCode::ArraySlice:
    bb_.instrs.push_back(rec(KirOpcode::ArraySlice, {}, location));
    break;
  case OpCode::ArrayPush:
    bb_.instrs.push_back(rec(KirOpcode::ArrayPush, {}, location));
    break;
  case OpCode::ArrayResize:
    bb_.instrs.push_back(rec(KirOpcode::ArrayResize, {}, location));
    break;
  case OpCode::ArrayPop:
    bb_.instrs.push_back(rec(KirOpcode::ArrayPop, {}, location));
    break;
  case OpCode::ArrayRemove:
    bb_.instrs.push_back(rec(KirOpcode::ArrayRemove, {}, location));
    break;
  case OpCode::ArrayContains:
    bb_.instrs.push_back(rec(KirOpcode::ArrayContains, {}, location));
    break;
  case OpCode::ArrayClear:
    bb_.instrs.push_back(rec(KirOpcode::ArrayClear, {}, location));
    break;
  case OpCode::ArrayInsert:
    bb_.instrs.push_back(rec(KirOpcode::ArrayInsert, {}, location));
    break;
  case OpCode::ArrayIndexOf:
    bb_.instrs.push_back(rec(KirOpcode::ArrayIndexOf, {}, location));
    break;
  case OpCode::ArrayReverse:
    bb_.instrs.push_back(rec(KirOpcode::ArrayReverse, {}, location));
    break;
  case OpCode::StringStartsWith:
    bb_.instrs.push_back(rec(KirOpcode::StrStartsWith, {}, location));
    break;
  case OpCode::StringEndsWith:
    bb_.instrs.push_back(rec(KirOpcode::StrEndsWith, {}, location));
    break;
  case OpCode::StringReplace:
    bb_.instrs.push_back(rec(KirOpcode::StrReplace, {}, location));
    break;
  case OpCode::StringSplit:
    bb_.instrs.push_back(rec(KirOpcode::StrSplit, {}, location));
    break;
  case OpCode::StringTrim:
    bb_.instrs.push_back(rec(KirOpcode::StrTrim, {}, location));
    break;
  case OpCode::StringToUpper:
    bb_.instrs.push_back(rec(KirOpcode::StrToUpper, {}, location));
    break;
  case OpCode::StringToLower:
    bb_.instrs.push_back(rec(KirOpcode::StrToLower, {}, location));
    break;
  case OpCode::MapNew:
    bb_.instrs.push_back(rec(KirOpcode::MapNew, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::MapHas:
    bb_.instrs.push_back(rec(KirOpcode::MapHas, {}, location));
    break;
  case OpCode::MapKeys:
    bb_.instrs.push_back(rec(KirOpcode::MapKeys, {}, location));
    break;
  case OpCode::EnumVariant:
    bb_.instrs.push_back(
        rec(KirOpcode::EnumVariant, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::EnumVariantPayload:
    bb_.instrs.push_back(
        rec(KirOpcode::EnumVariantPayload, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::EnumPayloadGet:
    bb_.instrs.push_back(
        rec(KirOpcode::EnumPayloadGet, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::CastTo:
    bb_.instrs.push_back(rec(KirOpcode::CastTo, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::FloatToBits:
    bb_.instrs.push_back(rec(KirOpcode::FloatToBits, {}, location));
    break;
  case OpCode::NativeOut:
    bb_.instrs.push_back(rec(KirOpcode::NativeOut, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::NativeOutLn:
    bb_.instrs.push_back(rec(KirOpcode::NativeOutLn, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::NativeErr:
    bb_.instrs.push_back(rec(KirOpcode::NativeErr, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::NativeErrLn:
    bb_.instrs.push_back(rec(KirOpcode::NativeErrLn, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::NativeIn:
    bb_.instrs.push_back(rec(KirOpcode::NativeIn, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::NativeInSecret:
    bb_.instrs.push_back(rec(KirOpcode::NativeInSecret, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::NativeFsRead:
    bb_.instrs.push_back(rec(KirOpcode::NativeFsRead, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::NativeFsWrite:
    bb_.instrs.push_back(rec(KirOpcode::NativeFsWrite, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::NativeSysArgs:
    bb_.instrs.push_back(rec(KirOpcode::NativeSysArgs, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::Negate:
    bb_.instrs.push_back(rec(KirOpcode::INeg, {}, location));
    break;
  default:
    bb_.instrs.push_back(rec(KirOpcode::Nop, {}, location));
    break;
  }
}

} // namespace kinglet
