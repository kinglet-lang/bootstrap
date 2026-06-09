#include "compiler/bytecode_emitter.h"

#include <cstdint>

namespace kinglet {

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

} // namespace kinglet
