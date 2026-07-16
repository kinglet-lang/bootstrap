// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace kinglet {

inline uint32_t pack_dense2d_shape(int rows, int cols) {
  return (static_cast<uint32_t>(rows) << 16) | (static_cast<uint32_t>(cols) & 0xFFFFu);
}

inline void unpack_dense2d_shape(uint32_t packed, int *rows, int *cols) {
  *rows = static_cast<int>(packed >> 16);
  *cols = static_cast<int>(packed & 0xFFFFu);
}

enum class LoweringOp : uint8_t {
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
  CastTo,
  FloatToBits,
  BitsToFloat,
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
  // New lowering ops must be appended here, never inserted mid-enum: KirRecorder's
  // on_emit() switch maps LoweringOp ordinals to KirOpcode, so a mid-enum insert
  // would silently mismap every lowering op that follows.
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
  BorrowIndexMut,
  Drop,
  NativeFsExists,
  NativeFsReadText,
  NativeFsWriteText,
  NativeFsReadBytes,
  NativeFsWriteBytes,
  NativeFsOpen,
  NativeFsCreate,
  NativeFileRead,
  NativeFileWrite,
  NativeFileSize,
  NativeFileSync,
  NativeFileClose,
  NativeFileIsOpen,
};

const char *lowering_op_name(LoweringOp op);

} // namespace kinglet
