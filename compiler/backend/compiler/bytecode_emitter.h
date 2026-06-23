// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "frontend/ast/ast.h"
#include "ir/kir.h"
#include "backend/vm/chunk.h"
#include "backend/vm/value.h"

namespace kinglet {

class BytecodeEmitter {
public:
  BytecodeEmitter() = default;
  explicit BytecodeEmitter(Chunk *chunk) : chunk_(chunk) {}

  void reset(Chunk *chunk) { chunk_ = chunk; }

  void emit(OpCode op, ast::SourceLocation location);
  void emit_operand(OpCode op, uint32_t operand, ast::SourceLocation location);
  void emit_constant(Value value, ast::SourceLocation location);
  std::size_t emit_jump(OpCode op, ast::SourceLocation location);
  void patch_jump(std::size_t offset);
  void patch_jump_to(std::size_t offset, std::size_t target);

  // Lower structured KIR into bytecode (stack machine).
  void lower(const KirFunction &function);

  Chunk *chunk() const { return chunk_; }

private:
  Chunk *chunk_ = nullptr;
};

} // namespace kinglet
