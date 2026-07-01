// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "ir/kir_numeric.h"

#include "frontend/types/numeric.h"

namespace kinglet {

namespace {

KirType kir_from_canonical_int(std::string_view name) {
  if (name == "int8")
    return KirType::Int8;
  if (name == "int16")
    return KirType::Int16;
  if (name == "int32")
    return KirType::Int32;
  if (name == "int64")
    return KirType::Int64;
  if (name == "uint8")
    return KirType::UInt8;
  if (name == "uint16")
    return KirType::UInt16;
  if (name == "uint32")
    return KirType::UInt32;
  if (name == "uint64")
    return KirType::UInt64;
  return KirType::Int64;
}

KirType kir_from_canonical_float(std::string_view name) {
  if (name == "float32")
    return KirType::Float32;
  if (name == "float64")
    return KirType::Float64;
  return KirType::Float32;
}

} // namespace

KirType kir_type_from_surface_type(const Type &type) {
  switch (type.kind) {
  case TypeKind::Int: {
    if (type.name.empty()) {
      return KirType::Int64;
    }
    return kir_from_canonical_int(type.name);
  }
  case TypeKind::Float:
    if (type.name.empty()) {
      return KirType::Float32;
    }
    return kir_from_canonical_float(type.name);
  case TypeKind::Bool:
    return KirType::Bool;
  case TypeKind::Null:
    return KirType::Null;
  case TypeKind::Char:
    return KirType::Int8;
  case TypeKind::String:
    return KirType::String;
  case TypeKind::Void:
    return KirType::Void;
  case TypeKind::Array:
    return KirType::Array;
  case TypeKind::Map:
    return KirType::Map;
  case TypeKind::Struct:
    return KirType::Struct;
  case TypeKind::Enum:
    return KirType::Enum;
  case TypeKind::Function:
    return KirType::Fn;
  }
  return KirType::Any;
}

KirType kir_type_from_int_literal_suffix(std::string_view suffix, int64_t value) {
  return kir_type_from_surface_type(int_literal_type_from_suffix(suffix, value));
}

KirType kir_type_from_float_literal_suffix(std::string_view suffix) {
  return kir_type_from_surface_type(float_literal_type_from_suffix(suffix));
}

KirType kir_type_normalize(KirType type) {
  if (type == KirType::Int) {
    return KirType::Int64;
  }
  if (type == KirType::Float) {
    return KirType::Float32;
  }
  return type;
}

KirType kir_const_opcode_result_type(KirOpcode op) {
  switch (op) {
  case KirOpcode::ConstInt:
  case KirOpcode::ConstI64:
    return KirType::Int64;
  case KirOpcode::ConstI32:
    return KirType::Int32;
  case KirOpcode::ConstU8:
    return KirType::UInt8;
  case KirOpcode::ConstF32:
    return KirType::Float32;
  case KirOpcode::ConstF64:
    return KirType::Float64;
  case KirOpcode::ConstFloat:
    return KirType::Float;
  default:
    return KirType::Any;
  }
}

KirBinopSpec kir_binop_spec(KirOpcode op) {
  KirBinopSpec spec;
  spec.specialized = true;
  switch (op) {
  case KirOpcode::IAdd32:
    spec.generic = KirOpcode::IAdd;
    spec.width = KirType::Int32;
    return spec;
  case KirOpcode::IAdd64:
    spec.generic = KirOpcode::IAdd;
    spec.width = KirType::Int64;
    return spec;
  case KirOpcode::ISub32:
    spec.generic = KirOpcode::ISub;
    spec.width = KirType::Int32;
    return spec;
  case KirOpcode::ISub64:
    spec.generic = KirOpcode::ISub;
    spec.width = KirType::Int64;
    return spec;
  case KirOpcode::IMul32:
    spec.generic = KirOpcode::IMul;
    spec.width = KirType::Int32;
    return spec;
  case KirOpcode::IMul64:
    spec.generic = KirOpcode::IMul;
    spec.width = KirType::Int64;
    return spec;
  case KirOpcode::IDiv32:
    spec.generic = KirOpcode::IDiv;
    spec.width = KirType::Int32;
    return spec;
  case KirOpcode::IDiv64:
    spec.generic = KirOpcode::IDiv;
    spec.width = KirType::Int64;
    return spec;
  case KirOpcode::IMod32:
    spec.generic = KirOpcode::IMod;
    spec.width = KirType::Int32;
    return spec;
  case KirOpcode::IMod64:
    spec.generic = KirOpcode::IMod;
    spec.width = KirType::Int64;
    return spec;
  case KirOpcode::FAdd32:
    spec.generic = KirOpcode::IAdd;
    spec.width = KirType::Float32;
    return spec;
  case KirOpcode::FAdd64:
    spec.generic = KirOpcode::IAdd;
    spec.width = KirType::Float64;
    return spec;
  case KirOpcode::FSub32:
    spec.generic = KirOpcode::ISub;
    spec.width = KirType::Float32;
    return spec;
  case KirOpcode::FSub64:
    spec.generic = KirOpcode::ISub;
    spec.width = KirType::Float64;
    return spec;
  case KirOpcode::FMul32:
    spec.generic = KirOpcode::IMul;
    spec.width = KirType::Float32;
    return spec;
  case KirOpcode::FMul64:
    spec.generic = KirOpcode::IMul;
    spec.width = KirType::Float64;
    return spec;
  case KirOpcode::FDiv32:
    spec.generic = KirOpcode::IDiv;
    spec.width = KirType::Float32;
    return spec;
  case KirOpcode::FDiv64:
    spec.generic = KirOpcode::IDiv;
    spec.width = KirType::Float64;
    return spec;
  default:
    spec.specialized = false;
    spec.generic = op;
    return spec;
  }
}

bool kir_opcode_is_arithmetic(KirOpcode op) {
  switch (op) {
  case KirOpcode::IAdd:
  case KirOpcode::ISub:
  case KirOpcode::IMul:
  case KirOpcode::IDiv:
  case KirOpcode::IMod:
  case KirOpcode::IAdd32:
  case KirOpcode::IAdd64:
  case KirOpcode::ISub32:
  case KirOpcode::ISub64:
  case KirOpcode::IMul32:
  case KirOpcode::IMul64:
  case KirOpcode::IDiv32:
  case KirOpcode::IDiv64:
  case KirOpcode::IMod32:
  case KirOpcode::IMod64:
  case KirOpcode::FAdd32:
  case KirOpcode::FAdd64:
  case KirOpcode::FSub32:
  case KirOpcode::FSub64:
  case KirOpcode::FMul32:
  case KirOpcode::FMul64:
  case KirOpcode::FDiv32:
  case KirOpcode::FDiv64:
    return true;
  default:
    return false;
  }
}

bool kir_type_is_integer(KirType type) {
  switch (type) {
  case KirType::Int:
  case KirType::Int8:
  case KirType::Int16:
  case KirType::Int32:
  case KirType::Int64:
  case KirType::UInt8:
  case KirType::UInt16:
  case KirType::UInt32:
  case KirType::UInt64:
  case KirType::Char:
    return true;
  default:
    return false;
  }
}

bool kir_type_is_float(KirType type) {
  return type == KirType::Float || type == KirType::Float32 || type == KirType::Float64;
}

KirType kir_type_join_numeric(KirType a, KirType b) {
  if (a == b) {
    return a;
  }
  if (a == KirType::Int) {
    a = KirType::Int64;
  }
  if (b == KirType::Int) {
    b = KirType::Int64;
  }
  if (a == KirType::Float) {
    a = KirType::Float32;
  }
  if (b == KirType::Float) {
    b = KirType::Float32;
  }
  if (kir_type_is_integer(a) && kir_type_is_integer(b)) {
    if (a == KirType::Int32 && b == KirType::Int64) {
      return KirType::Int64;
    }
    if (a == KirType::Int64 && b == KirType::Int32) {
      return KirType::Int64;
    }
    return KirType::Any;
  }
  if (kir_type_is_float(a) && kir_type_is_float(b)) {
    return KirType::Float64;
  }
  return KirType::Any;
}

} // namespace kinglet
