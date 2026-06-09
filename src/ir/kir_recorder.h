#pragma once

#include "ast/ast.h"
#include "ir/kir.h"
#include "vm/chunk.h"
#include "vm/value.h"

namespace kinglet {

// Records bytecode emission as structured KIR during legacy compile paths.
class KirRecorder {
public:
  void begin_function(const std::string &name, int param_count);
  void end_function(KirModule *module);

  void on_emit(OpCode op, uint32_t operand, ast::SourceLocation location);
  void on_constant(const Value &value, ast::SourceLocation location);

  std::size_t record_jump(OpCode op, ast::SourceLocation location);
  void patch_jump(std::size_t jump_instr_index, int32_t relative_offset);
  std::size_t instr_count() const;

  bool active() const { return active_; }

private:
  KirFunction fn_;
  KirBasicBlock bb_;
  bool active_ = false;
};

} // namespace kinglet
