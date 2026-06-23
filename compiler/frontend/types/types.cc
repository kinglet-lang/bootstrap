// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "frontend/types/types.h"

#include "frontend/types/numeric.h"

#include <utility>

namespace kinglet {

Type::Type(TypeKind kind) : kind(kind) {}

TypeId Type::type_id() const {
  switch (kind) {
  case TypeKind::Int:
    if (name == "int8") return TypeId::Int8;
    if (name == "int16") return TypeId::Int16;
    if (name == "int32") return TypeId::Int32;
    if (name == "uint8") return TypeId::UInt8;
    if (name == "uint16") return TypeId::UInt16;
    if (name == "uint32") return TypeId::UInt32;
    if (name == "uint64") return TypeId::UInt64;
    return TypeId::Int64; // default (int / int64)
  case TypeKind::Float:
    if (name == "float32") return TypeId::Float32;
    return TypeId::Float64; // default (float64)
  case TypeKind::Bool:    return TypeId::Bool;
  case TypeKind::Char:    return TypeId::Char;
  case TypeKind::String:  return TypeId::String;
  case TypeKind::Void:    return TypeId::Void;
  case TypeKind::Null:    return TypeId::Null;
  case TypeKind::Array:   return TypeId::Array;
  case TypeKind::Map:     return TypeId::Map;
  case TypeKind::Struct:  return TypeId::Struct;
  case TypeKind::Enum:    return TypeId::Enum;
  case TypeKind::Function:return TypeId::Function;
  case TypeKind::Ref:     return TypeId::Ref;
  case TypeKind::MutRef:  return TypeId::MutRef;
  case TypeKind::Concept: return TypeId::Concept;
  }
  return TypeId::Unknown;
}

Type::Type(const Type &other)
    : kind(other.kind), name(other.name), param_types(other.param_types),
      return_type(other.return_type ? std::make_shared<Type>(*other.return_type) : nullptr),
      element_type(other.element_type ? std::make_shared<Type>(*other.element_type) : nullptr),
      key_type(other.key_type ? std::make_shared<Type>(*other.key_type) : nullptr),
      fields(other.fields), variants(other.variants),
      variant_param_types(other.variant_param_types), nullable(other.nullable) {}

Type &Type::operator=(const Type &other) {
  if (this != &other) {
    kind = other.kind;
    name = other.name;
    param_types = other.param_types;
    return_type = other.return_type ? std::make_shared<Type>(*other.return_type) : nullptr;
    element_type = other.element_type ? std::make_shared<Type>(*other.element_type) : nullptr;
    key_type = other.key_type ? std::make_shared<Type>(*other.key_type) : nullptr;
    fields = other.fields;
    variants = other.variants;
    variant_param_types = other.variant_param_types;
    nullable = other.nullable;
  }
  return *this;
}

bool Type::is_numeric() const {
  return is_integer_type(*this) || is_float_type(*this);
}

bool Type::is_compatible_with(const Type &other) const {
  if (kind == other.kind) {
    if (kind == TypeKind::Int) {
      return name == other.name;
    }
    if (kind == TypeKind::Float) {
      return type_id() == other.type_id() || type_id() == TypeId::Float32 ||
             other.type_id() == TypeId::Float32;
    }
    if (kind == TypeKind::Char) {
      return other.kind == TypeKind::Char || other.type_id() == TypeId::Int8;
    }
    if (kind == TypeKind::Struct || kind == TypeKind::Enum) {
      return name == other.name;
    }
    if (kind == TypeKind::Array) {
      if (!element_type || !other.element_type) {
        return true;
      }
      if (element_type->kind == TypeKind::Null || other.element_type->kind == TypeKind::Null) {
        return true;
      }
      return element_type->is_compatible_with(*other.element_type);
    }
    if (kind == TypeKind::Map) {
      // An empty map literal `{}` has unknown key/value types; treat missing
      // sides as compatible so `{string: int} m = {};` type-checks.
      if (!key_type || !other.key_type || !element_type || !other.element_type) {
        return true;
      }
      bool key_ok = key_type->kind == TypeKind::Null ||
                    other.key_type->kind == TypeKind::Null ||
                    key_type->is_compatible_with(*other.key_type);
      bool val_ok = element_type->kind == TypeKind::Null ||
                    other.element_type->kind == TypeKind::Null ||
                    element_type->is_compatible_with(*other.element_type);
      return key_ok && val_ok;
    }
    if (kind == TypeKind::Ref || kind == TypeKind::MutRef) {
      if (!element_type || !other.element_type) {
        return true;
      }
      return element_type->is_compatible_with(*other.element_type);
    }
    return true;
  }
  if (kind == TypeKind::MutRef && other.kind == TypeKind::Ref) {
    if (!element_type || !other.element_type) {
      return true;
    }
    return element_type->is_compatible_with(*other.element_type);
  }
  if (kind == TypeKind::Int && other.kind == TypeKind::Char) {
    return type_id() == TypeId::Int8;
  }
  if (kind == TypeKind::Char && other.kind == TypeKind::Int) {
    return other.type_id() == TypeId::Int8;
  }
  if (kind == TypeKind::Null || other.kind == TypeKind::Null) {
    return true;
  }
  return false;
}

Type Type::promote(const Type &a, const Type &b) {
  if (a.kind == TypeKind::Float || b.kind == TypeKind::Float) {
    return promote_float_binary(a, b);
  }
  if (a.kind == TypeKind::Int && b.kind == TypeKind::Int) {
    if (a.name == b.name) return a;
    return make_int_type("int64");
  }
  return a;
}

static const Type INT_INSTANCE = make_int_type("int64");
static const Type FLOAT_INSTANCE = make_float_type("float32");
static const Type DOUBLE_INSTANCE = make_float_type("float64");
static const Type BOOL_INSTANCE{TypeKind::Bool};
static const Type CHAR_INSTANCE = [] {
  Type t(TypeKind::Char);
  t.name = "int8";
  return t;
}();
static const Type BYTE_INSTANCE = make_int_type("uint8");
static const Type STRING_INSTANCE{TypeKind::String};
static const Type VOID_INSTANCE{TypeKind::Void};
static const Type NULL_INSTANCE{TypeKind::Null};

const Type &int_type() {
  return INT_INSTANCE;
}

const Type &float_type() {
  return FLOAT_INSTANCE;
}

const Type &double_type() {
  return DOUBLE_INSTANCE;
}

const Type &bool_type() {
  return BOOL_INSTANCE;
}

const Type &char_type() {
  return CHAR_INSTANCE;
}

const Type &byte_type() {
  return BYTE_INSTANCE;
}

const Type &string_type() {
  return STRING_INSTANCE;
}

const Type &void_type() {
  return VOID_INSTANCE;
}

const Type &null_type() {
  return NULL_INSTANCE;
}

Type array_type(Type element_type) {
  Type result(TypeKind::Array);
  result.element_type = std::make_shared<Type>(std::move(element_type));
  return result;
}

Type map_type(Type key, Type value) {
  Type result(TypeKind::Map);
  result.key_type = std::make_shared<Type>(std::move(key));
  result.element_type = std::make_shared<Type>(std::move(value));
  return result;
}

std::ostream &operator<<(std::ostream &out, const Type &type) {
  if (type.kind == TypeKind::Array && type.element_type) {
    return out << *type.element_type << "[]";
  }
  if (type.kind == TypeKind::Map && type.key_type && type.element_type) {
    return out << "{" << *type.key_type << ": " << *type.element_type << "}";
  }
  return out << type.kind;
}

std::ostream &operator<<(std::ostream &out, TypeKind kind) {
  switch (kind) {
  case TypeKind::Int:
    return out << "Int";
  case TypeKind::Float:
    return out << "Float";
  case TypeKind::Bool:
    return out << "Bool";
  case TypeKind::Char:
    return out << "Char";
  case TypeKind::String:
    return out << "String";
  case TypeKind::Void:
    return out << "Void";
  case TypeKind::Null:
    return out << "Null";
  case TypeKind::Function:
    return out << "Function";
  case TypeKind::Struct:
    return out << "Struct";
  case TypeKind::Enum:
    return out << "Enum";
  case TypeKind::Array:
    return out << "Array";
  case TypeKind::Map:
    return out << "Map";
  case TypeKind::Ref:
    return out << "Ref";
  case TypeKind::MutRef:
    return out << "MutRef";
  case TypeKind::Concept:
    return out << "Concept";
  }
  return out << "Unknown";
}

} // namespace kinglet
