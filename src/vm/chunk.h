#pragma once

#include "vm/value.h"

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace kinglet {

inline uint32_t pack_dense2d_shape(int rows, int cols) {
  return (static_cast<uint32_t>(rows) << 16) |
         (static_cast<uint32_t>(cols) & 0xFFFFu);
}

inline void unpack_dense2d_shape(uint32_t packed, int *rows, int *cols) {
  *rows = static_cast<int>(packed >> 16);
  *cols = static_cast<int>(packed & 0xFFFFu);
}

enum class OpCode : uint8_t {
  Constant,
  Null,
  True,
  False,
  Add,
  Subtract,
  Multiply,
  Divide,
  Modulo,
  Negate,
  Not,
  BitNot,
  BitAnd,
  BitOr,
  BitXor,
  Shl,
  Shr,
  LoadLocal,
  StoreLocal,
  Pop,
  Dup,
  CastTo, FloatToBits,
  Call,
  Return,
  Jmp,
  JmpFalse,
  JmpIfErr,
  Eq,
  Neq,
  Lt,
  Gt,
  Le,
  Ge,
  NativeOut,
  NativeOutLn,
  NativeErr,
  NativeErrLn,
  NativeIn,
  NativeInSecret,
  NativeFsRead,
  NativeFsWrite,
  NativeSysArgs,
  StructNew,
  FieldGet,
  FieldSet,
  EnumVariant,
  ArrayNew,
  IndexGet,
  IndexSet,
  ArrayLen,
  ArrayPush,
  ArrayResize,
  ArrayPop,
  ArrayRemove,
  ArrayContains,
  ArrayClear,
  ArrayInsert,
  ArrayIndexOf,
  ArraySlice,
  ArrayReverse,
  StringStartsWith,
  StringEndsWith,
  StringReplace,
  StringSplit,
  StringTrim,
  StringToUpper,
  StringToLower,
  EnumVariantPayload,
  EnumPayloadGet,
  MapNew,
  MapGet,
  MapSet,
  MapHas,
  MapRemove,
  MapKeys,
  MapLen,
  PushHandler,
  PopHandler,
  PropagateErr,
  IsNull,
  // Appended after the original VM opcode order (kept in lockstep with the
  // self-host backend/vm/chunk.h and compiler/bytecode.kl). New opcodes must be
  // appended here, never inserted mid-enum: kbc serializes opcodes as raw enum
  // ordinals, so a mid-enum insert renumbers every later opcode.
  StringToInt,
  StringToFloat,
  StringCode,
  StringCodeAt,
  AddI32,
  SubtractI32,
  MultiplyI32,
  DivideI32,
  ModuloI32,
  DenseArrayNew,
};

struct Instruction {
  OpCode op;
  int32_t operand = 0;
  int line = 0;
  int column = 0;
};

struct FunctionInfo {
  std::string name;
  std::size_t entry = 0;
  int param_count = 0;
};

struct StructMeta {
  std::string name;
  std::vector<std::string> field_names;
};

struct EnumMeta {
  std::string name;
  std::vector<std::string> variants;
  std::vector<int> variant_param_counts;
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

  // Bytecode serialization (.kbc format)
  // When strip_debug is true, line/column info is omitted from the output,
  // producing smaller .kbc files suitable for deployment.
  bool serialize(const std::string &path, bool strip_debug = false) const;
  static Chunk deserialize(const std::string &path, std::string *error);

private:
  std::vector<Value> constants_;
  std::vector<Instruction> instructions_;
  std::vector<FunctionInfo> functions_;
  std::vector<StructMeta> struct_metas_;
  std::vector<EnumMeta> enum_metas_;
};

const char *opcode_name(OpCode op);

} // namespace kinglet
