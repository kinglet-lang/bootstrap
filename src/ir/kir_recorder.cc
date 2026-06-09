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

void KirRecorder::begin_function(const std::string &name, int param_count) {
  fn_ = KirFunction{};
  fn_.name = name;
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

void KirRecorder::on_constant(const Value &value, ast::SourceLocation location) {
  if (!active_) {
    return;
  }
  if (value.type == ValueType::Int || value.type == ValueType::Char) {
    bb_.instrs.push_back(
        rec(KirOpcode::ConstInt, {static_cast<int32_t>(value.as_int)}, location));
  } else if (value.type == ValueType::Bool) {
    bb_.instrs.push_back(rec(KirOpcode::ConstBool, {value.as_bool ? 1 : 0}, location));
  } else if (value.type == ValueType::Null) {
    bb_.instrs.push_back(rec(KirOpcode::ConstNull, {}, location));
  }
}

void KirRecorder::on_emit(OpCode op, uint32_t operand, ast::SourceLocation location) {
  if (!active_) {
    return;
  }
  switch (op) {
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
  case OpCode::Jmp:
    bb_.instrs.push_back(rec(KirOpcode::Br, {static_cast<int32_t>(operand)}, location));
    break;
  case OpCode::JmpFalse:
    bb_.instrs.push_back(rec(KirOpcode::CondBr, {static_cast<int32_t>(operand)}, location));
    break;
  default:
    break;
  }
}

} // namespace kinglet
