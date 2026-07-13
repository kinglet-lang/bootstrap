// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "frontend/types/type_id.h"

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace kinglet {

enum class TypeKind : std::uint8_t {
  Int,
  Float,
  Bool,
  Char,
  String,
  Void,
  Null,
  Function,
  Struct,
  Enum,
  Array,
  Map,
  Ref,
  MutRef,
  Concept,
  Optional,
};

struct Type;

struct FieldInfo {
  std::string name;
  TypeKind type_kind;
  std::string type_name;
  // Full resolved type of the field, retaining information that type_kind alone
  // loses — notably an array field's element_type. May be null for fields
  // registered before this was populated; callers fall back to type_kind/name.
  std::shared_ptr<Type> type;
  // True when this Optional<Self> field needs pointer indirection to avoid
  // infinite struct size (e.g. `node? next` in `struct node`).
  bool is_indirect = false;
};

struct Type {
  explicit Type(TypeKind kind);
  Type(const Type &other);
  Type &operator=(const Type &other);

  bool is_numeric() const;
  bool is_compatible_with(const Type &other) const;
  static Type promote(const Type &a, const Type &b);

  // Returns the canonical TypeId for this type, combining kind and name.
  TypeId type_id() const;

  TypeKind kind;
  std::string name;
  std::vector<Type> param_types;
  std::shared_ptr<Type> return_type;
  std::shared_ptr<Type> element_type;
  // For Map: key_type holds K, element_type holds V.
  std::shared_ptr<Type> key_type;
  std::vector<FieldInfo> fields;
  std::vector<std::string> variants;
  std::vector<std::vector<Type>> variant_param_types;
  bool nullable = false;
  bool is_resource = false;
  // Fixed-size array: > 0 means a T[fixed_size] array stored inline.
  int fixed_size = -1;
};

const Type &int_type();
const Type &float_type();
const Type &double_type();
const Type &bool_type();
const Type &char_type();
const Type &byte_type();
const Type &string_type();
const Type &void_type();
const Type &null_type();
Type array_type(Type element_type);
Type map_type(Type key, Type value);
Type optional_type(Type inner_type);

std::ostream &operator<<(std::ostream &out, const Type &type);
std::ostream &operator<<(std::ostream &out, TypeKind kind);

} // namespace kinglet
