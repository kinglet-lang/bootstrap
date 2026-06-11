#include "compiler/bytecode_emitter.h"

#include <cstdint>
#include <cstring>

namespace kinglet {

namespace {

int64_t decode_i64_operands(const KirInstr &instr) {
  const int32_t low = instr.operands[0];
  const int32_t high = instr.operands.size() > 1 ? instr.operands[1] : (low < 0 ? -1 : 0);
  return (static_cast<uint64_t>(static_cast<uint32_t>(high)) << 32) |
         static_cast<uint32_t>(low);
}

double decode_f64_operands(const KirInstr &instr) {
  int64_t bits = decode_i64_operands(instr);
  double value = 0.0;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

} // namespace

void BytecodeEmitter::emit(OpCode op, ast::SourceLocation location) {
  chunk_->write(op, location.line, location.column);
}

void BytecodeEmitter::emit_operand(OpCode op, uint32_t operand, ast::SourceLocation location) {
  chunk_->write_operand(op, operand, location.line, location.column);
}

void BytecodeEmitter::emit_constant(Value value, ast::SourceLocation location) {
  chunk_->write_constant(value, location.line, location.column);
}

std::size_t BytecodeEmitter::emit_jump(OpCode op, ast::SourceLocation location) {
  emit_operand(op, 0, location);
  return chunk_->instructions().size() - 1;
}

void BytecodeEmitter::patch_jump(std::size_t offset) {
  patch_jump_to(offset, chunk_->instructions().size());
}

void BytecodeEmitter::patch_jump_to(std::size_t offset, std::size_t target) {
  const int32_t jump_offset = static_cast<int32_t>(target - offset - 1);
  Instruction &instruction = const_cast<Instruction &>(chunk_->instructions()[offset]);
  instruction.operand = jump_offset;
}

void BytecodeEmitter::lower(const KirFunction &function) {
  for (const KirBasicBlock &bb : function.blocks) {
    for (const KirInstr &instr : bb.instrs) {
      ast::SourceLocation loc{instr.line, instr.col};
      switch (instr.op) {
      case KirOpcode::ConstInt:
      case KirOpcode::ConstI64:
        emit_constant(Value::int_value(decode_i64_operands(instr)), loc);
        break;
      case KirOpcode::ConstI32:
        emit_constant(Value::int_value(instr.operands[0]), loc);
        break;
      case KirOpcode::ConstU8:
        emit_constant(Value::int_value(instr.operands[0] & 0xff), loc);
        break;
      case KirOpcode::ConstF32:
      case KirOpcode::ConstF64:
      case KirOpcode::ConstFloat:
        emit_constant(Value::double_value(decode_f64_operands(instr)), loc);
        break;
      case KirOpcode::IAdd:
      case KirOpcode::IAdd64:
      case KirOpcode::FAdd32:
      case KirOpcode::FAdd64:
        emit(OpCode::Add, loc);
        break;
      case KirOpcode::IAdd32:
        emit(OpCode::AddI32, loc);
        break;
      case KirOpcode::ISub:
      case KirOpcode::ISub64:
      case KirOpcode::FSub32:
      case KirOpcode::FSub64:
        emit(OpCode::Subtract, loc);
        break;
      case KirOpcode::ISub32:
        emit(OpCode::SubtractI32, loc);
        break;
      case KirOpcode::IMul:
      case KirOpcode::IMul64:
      case KirOpcode::FMul32:
      case KirOpcode::FMul64:
        emit(OpCode::Multiply, loc);
        break;
      case KirOpcode::IMul32:
        emit(OpCode::MultiplyI32, loc);
        break;
      case KirOpcode::IDiv:
      case KirOpcode::IDiv64:
      case KirOpcode::FDiv32:
      case KirOpcode::FDiv64:
        emit(OpCode::Divide, loc);
        break;
      case KirOpcode::IDiv32:
        emit(OpCode::DivideI32, loc);
        break;
      case KirOpcode::IMod:
      case KirOpcode::IMod64:
        emit(OpCode::Modulo, loc);
        break;
      case KirOpcode::IMod32:
        emit(OpCode::ModuloI32, loc);
        break;
      case KirOpcode::LoadLocal:
        emit_operand(OpCode::LoadLocal, static_cast<uint32_t>(instr.operands[0]), loc);
        break;
      case KirOpcode::StoreLocal:
        emit_operand(OpCode::StoreLocal, static_cast<uint32_t>(instr.operands[0]), loc);
        break;
      case KirOpcode::Pop:
        emit(OpCode::Pop, loc);
        break;
      case KirOpcode::Call:
        emit_operand(OpCode::Call, static_cast<uint32_t>(instr.operands[0]), loc);
        break;
      case KirOpcode::Ret:
        emit(OpCode::Return, loc);
        break;
      default:
        break;
      }
    }
  }
}

} // namespace kinglet
