#include "ir/kir_recorder.h"

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
                              ast::SourceLocation location) {
  if (!active_) {
    return;
  }
  if (value.type == ValueType::Int || value.type == ValueType::Char) {
    const int64_t v = value.as_int;
    bb_.instrs.push_back(rec(KirOpcode::ConstInt,
                             {static_cast<int32_t>(v),
                              static_cast<int32_t>(static_cast<uint64_t>(v) >> 32)},
                             location));
  } else if (value.type == ValueType::Bool) {
    bb_.instrs.push_back(rec(KirOpcode::ConstBool, {value.as_bool ? 1 : 0}, location));
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
  case OpCode::Subtract:
    bb_.instrs.push_back(rec(KirOpcode::ISub, {}, location));
    break;
  case OpCode::Multiply:
    bb_.instrs.push_back(rec(KirOpcode::IMul, {}, location));
    break;
  case OpCode::Divide:
    bb_.instrs.push_back(rec(KirOpcode::IDiv, {}, location));
    break;
  case OpCode::Modulo:
    bb_.instrs.push_back(rec(KirOpcode::IMod, {}, location));
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
  case OpCode::ArrayNew:
    bb_.instrs.push_back(
        rec(KirOpcode::ArrayNew, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::IndexGet:
    bb_.instrs.push_back(rec(KirOpcode::IndexGet, {}, location));
    break;
  case OpCode::ArrayLen:
    bb_.instrs.push_back(rec(KirOpcode::ArrayLen, {}, location));
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
  case OpCode::Negate:
    bb_.instrs.push_back(rec(KirOpcode::INeg, {}, location));
    break;
  default:
    bb_.instrs.push_back(rec(KirOpcode::Nop, {}, location));
    break;
  }
}

} // namespace kinglet
