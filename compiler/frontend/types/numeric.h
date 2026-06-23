// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "frontend/types/types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kinglet {

// Canonical width names stored in Type::name for Int / Float kinds.
struct IntWidthInfo {
  bool is_signed;
  int bits;
};

std::optional<IntWidthInfo> parse_int_width_name(std::string_view name);
std::optional<std::string_view> parse_float_width_name(std::string_view name);

// Map surface spellings (including aliases int/float/double/char/byte) to canonical names.
std::optional<std::string> canonical_int_type_name(std::string_view surface);
std::optional<std::string> canonical_float_type_name(std::string_view surface);

Type make_int_type(std::string_view canonical_name);
Type make_float_type(std::string_view canonical_name);

bool is_integer_type(const Type &type);
bool is_float_type(const Type &type);

std::string integer_type_display_name(const Type &type);
std::string float_type_display_name(const Type &type);

IntWidthInfo int_width_info(const Type &type);
bool float_is_64(const Type &type);

bool integer_fits_width(int64_t value, const IntWidthInfo &width);
Type default_int_literal_type(int64_t value);
Type int_literal_type_from_suffix(std::string_view suffix, int64_t value);
Type float_literal_type_from_suffix(std::string_view suffix);

// Assignment / init: allow same width or widening only.
bool integer_assignable(const Type &from, const Type &to);
bool float_assignable(const Type &from, const Type &to);

// Binary arithmetic: variables must match width; integer literals adapt to the other operand.
std::optional<Type> try_promote_integer_binary(const Type &left, const Type &right,
                                               bool left_is_literal, bool right_is_literal);
Type promote_float_binary(const Type &left, const Type &right);

} // namespace kinglet
