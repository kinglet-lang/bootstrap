#pragma once

#include "ir/lowering_opcode.h"
#include "ir/lowering_value.h"
#include "ir/lowering_metadata.h"

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace kinglet {

struct Instruction {
  OpCode op;
  int32_t operand = 0;
  int line = 0;
  int column = 0;
};

class Chunk {
public:
  uint32_t add_constant(Value value);
  void write(OpCode op, int line, int column);
  void write_operand(OpCode op, uint32_t operand, int line, int column);
  void write_constant(Value value, int line, int column);

  int add_function(FunctionInfo info);
  int add_struct_meta(StructMeta meta);
  int add_enum_meta(EnumMeta meta);
  const std::vector<FunctionInfo> &functions() const;
  const std::vector<StructMeta> &struct_metas() const;
  const std::vector<EnumMeta> &enum_metas() const;

  const std::vector<Value> &constants() const;
  const std::vector<Instruction> &instructions() const;
  void disassemble(std::ostream &out) const;
  void patch_operand(std::size_t index, int32_t operand);

private:
  std::vector<Value> constants_;
  std::vector<Instruction> instructions_;
  std::vector<FunctionInfo> functions_;
  std::vector<StructMeta> struct_metas_;
  std::vector<EnumMeta> enum_metas_;
};

} // namespace kinglet
