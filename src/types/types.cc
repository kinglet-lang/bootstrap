#include "types/types.h"

#include <utility>

namespace kinglet {

Type::Type(TypeKind kind) : kind(kind) {}

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
  return kind == TypeKind::Int || kind == TypeKind::Float;
}

bool Type::is_compatible_with(const Type &other) const {
  if (kind == other.kind) {
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
    return true;
  }
  if (is_numeric() && other.is_numeric()) {
    return true;
  }
  if (kind == TypeKind::Null || other.kind == TypeKind::Null) {
    return true;
  }
  return false;
}

Type Type::promote(const Type &a, const Type &b) {
  if (a.kind == TypeKind::Float || b.kind == TypeKind::Float) {
    return float_type();
  }
  return a;
}

static const Type INT_INSTANCE{TypeKind::Int};
static const Type FLOAT_INSTANCE{TypeKind::Float};
static const Type BOOL_INSTANCE{TypeKind::Bool};
static const Type CHAR_INSTANCE{TypeKind::Char};
static const Type STRING_INSTANCE{TypeKind::String};
static const Type VOID_INSTANCE{TypeKind::Void};
static const Type NULL_INSTANCE{TypeKind::Null};

const Type &int_type() {
  return INT_INSTANCE;
}

const Type &float_type() {
  return FLOAT_INSTANCE;
}

const Type &bool_type() {
  return BOOL_INSTANCE;
}

const Type &char_type() {
  return CHAR_INSTANCE;
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
  }
  return out << "Unknown";
}

} // namespace kinglet
