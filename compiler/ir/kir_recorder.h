// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "frontend/ast/ast.h"
#include "ir/kir.h"
#include "ir/lowering_op.h"
#include "ir/lowering_value.h"

namespace kinglet {

// Translates LoweringOp emission events into structured KIR during compilation.
// Compiler calls on_emit() / on_constant() / record_jump() as it walks the AST;
// KirRecorder accumulates KirInstr nodes and finalises a KirFunction on end_function().
class KirRecorder {
public:
  void begin_function(const std::string &name, int param_count, const std::string &source_path = "",
                      const std::string &mangled_name = "");
  void end_function(KirModule *module);

  void on_emit(LoweringOp op, uint32_t operand, ast::SourceLocation location);
  void on_constant(const Value &value, uint32_t pool_index, ast::SourceLocation location,
                   KirType numeric_type = KirType::Any);

  std::size_t record_jump(LoweringOp op, ast::SourceLocation location);
  void patch_jump(std::size_t jump_instr_index, int32_t relative_offset);
  void patch_operand(std::size_t instr_index, int32_t operand);
  std::size_t instr_count() const;

  bool active() const { return active_; }

private:
  KirFunction fn_;
  KirBasicBlock bb_;
  bool active_ = false;
};

} // namespace kinglet
