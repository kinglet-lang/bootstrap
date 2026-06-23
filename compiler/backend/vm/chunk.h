#pragma once

#include "backend/vm/value.h"

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
  LoadLocalAddr,
  DerefLoad,
  DerefStore,
  StoreLocal,
  Pop,
  Dup,
  CastTo, FloatToBits, BitsToFloat,
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
  NativeFsListdir,
  NativeSysArgs,
  StructNew,
  BorrowFieldMut,
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
  // New opcodes must be appended here, never inserted mid-enum: KirRecorder's
  // on_emit() switch maps OpCode ordinals to KirOpcode, so a mid-enum insert
  // would silently mismap every opcode that follows.
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

private:
  std::vector<Value> constants_;
  std::vector<Instruction> instructions_;
  std::vector<FunctionInfo> functions_;
  std::vector<StructMeta> struct_metas_;
  std::vector<EnumMeta> enum_metas_;
};

const char *opcode_name(OpCode op);

} // namespace kinglet
