// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace kinglet {

// Canonical numeric type identifier, encoding both kind and width in one value.
// Non-numeric types (Bool, Char, String, Void, Null, ...) and compound types
// (Struct, Enum, Array, Map, ...) are also represented here so callers can use
// a single dispatch value instead of combining TypeKind with Type::name.
enum class TypeId : uint8_t {
  // Scalar: signed integers
  Int8,
  Int16,
  Int32,
  Int64,
  // Scalar: unsigned integers
  UInt8,
  UInt16,
  UInt32,
  UInt64,
  // Scalar: floats
  Float32,
  Float64,
  // Primitives
  Bool,
  Char, // alias for Int8 at the surface; distinct kind for type-checking
  String,
  Void,
  Null,
  // Compound
  Array,
  Map,
  Struct,
  Enum,
  Function,
  // Reference types
  Ref,
  MutRef,
  // Meta
  Concept,
  // Unknown / not yet resolved
  Unknown,
};

// Returns true if id is any integer type (signed or unsigned).
inline bool type_id_is_integer(TypeId id) {
  switch (id) {
  case TypeId::Int8:
  case TypeId::Int16:
  case TypeId::Int32:
  case TypeId::Int64:
  case TypeId::UInt8:
  case TypeId::UInt16:
  case TypeId::UInt32:
  case TypeId::UInt64:
    return true;
  default:
    return false;
  }
}

inline bool type_id_is_signed_integer(TypeId id) {
  switch (id) {
  case TypeId::Int8:
  case TypeId::Int16:
  case TypeId::Int32:
  case TypeId::Int64:
    return true;
  default:
    return false;
  }
}

inline bool type_id_is_float(TypeId id) {
  return id == TypeId::Float32 || id == TypeId::Float64;
}

inline bool type_id_is_numeric(TypeId id) {
  return type_id_is_integer(id) || type_id_is_float(id);
}

// Returns bit width (8/16/32/64) for integer and float types; 0 for others.
inline int type_id_bit_width(TypeId id) {
  switch (id) {
  case TypeId::Int8:
  case TypeId::UInt8:
    return 8;
  case TypeId::Int16:
  case TypeId::UInt16:
    return 16;
  case TypeId::Int32:
  case TypeId::UInt32:
  case TypeId::Float32:
    return 32;
  case TypeId::Int64:
  case TypeId::UInt64:
  case TypeId::Float64:
    return 64;
  default:
    return 0;
  }
}

} // namespace kinglet
