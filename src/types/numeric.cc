#include "types/numeric.h"

#include <algorithm>

namespace kinglet {

namespace {

constexpr int64_t kInt32Max = 2147483647LL;
constexpr int64_t kInt32Min = -2147483648LL;

IntWidthInfo width_signed(int bits) { return IntWidthInfo{true, bits}; }
IntWidthInfo width_unsigned(int bits) { return IntWidthInfo{false, bits}; }

std::optional<IntWidthInfo> parse_canonical_int(std::string_view name) {
  if (name == "int8") return width_signed(8);
  if (name == "int16") return width_signed(16);
  if (name == "int32") return width_signed(32);
  if (name == "int64") return width_signed(64);
  if (name == "uint8") return width_unsigned(8);
  if (name == "uint16") return width_unsigned(16);
  if (name == "uint32") return width_unsigned(32);
  if (name == "uint64") return width_unsigned(64);
  return std::nullopt;
}

} // namespace

std::optional<IntWidthInfo> parse_int_width_name(std::string_view name) {
  return parse_canonical_int(name);
}

std::optional<std::string_view> parse_float_width_name(std::string_view name) {
  if (name == "float32" || name == "float") return "float32";
  if (name == "float64" || name == "double") return "float64";
  return std::nullopt;
}

std::optional<std::string> canonical_int_type_name(std::string_view surface) {
  if (surface == "int") return "int64";
  if (surface == "char") return "int8";
  if (surface == "byte") return "uint8";
  if (auto w = parse_canonical_int(surface)) {
    (void)w;
    return std::string(surface);
  }
  return std::nullopt;
}

std::optional<std::string> canonical_float_type_name(std::string_view surface) {
  if (auto w = parse_float_width_name(surface)) {
    return std::string(*w);
  }
  return std::nullopt;
}

Type make_int_type(std::string_view canonical_name) {
  Type t(TypeKind::Int);
  t.name = std::string(canonical_name);
  return t;
}

Type make_float_type(std::string_view canonical_name) {
  Type t(TypeKind::Float);
  t.name = std::string(canonical_name);
  return t;
}

bool is_integer_type(const Type &type) {
  return type.kind == TypeKind::Int ||
         (type.kind == TypeKind::Char && parse_canonical_int("int8").has_value());
}

bool is_float_type(const Type &type) { return type.kind == TypeKind::Float; }

std::string integer_type_display_name(const Type &type) {
  if (type.kind == TypeKind::Char) return "char";
  if (!type.name.empty()) {
    if (type.name == "int64") return "int";
    if (type.name == "int8") return "char";
    if (type.name == "uint8") return "byte";
    return type.name;
  }
  return "int";
}

std::string float_type_display_name(const Type &type) {
  if (!type.name.empty()) {
    if (type.name == "float32") return "float";
    if (type.name == "float64") return "double";
    return type.name;
  }
  return "float";
}

IntWidthInfo int_width_info(const Type &type) {
  if (type.kind == TypeKind::Char) return width_signed(8);
  if (auto info = parse_canonical_int(type.name)) return *info;
  return width_signed(64);
}

bool float_is_64(const Type &type) { return type.name == "float64"; }

bool integer_fits_width(int64_t value, const IntWidthInfo &width) {
  if (width.is_signed) {
    switch (width.bits) {
    case 8:
      return value >= -128 && value <= 127;
    case 16:
      return value >= -32768 && value <= 32767;
    case 32:
      return value >= kInt32Min && value <= kInt32Max;
    case 64:
      return true;
    default:
      return false;
    }
  }
  switch (width.bits) {
  case 8:
    return value >= 0 && value <= 255;
  case 16:
    return value >= 0 && value <= 65535;
  case 32:
    return value >= 0 && value <= 4294967295LL;
  case 64:
    return value >= 0;
  default:
    return false;
  }
}

Type default_int_literal_type(int64_t value) {
  if (value >= kInt32Min && value <= kInt32Max) {
    return make_int_type("int32");
  }
  return make_int_type("int64");
}

Type int_literal_type_from_suffix(std::string_view suffix, int64_t value) {
  if (suffix.empty()) return default_int_literal_type(value);
  std::string canonical;
  if (suffix == "i8") canonical = "int8";
  else if (suffix == "i16") canonical = "int16";
  else if (suffix == "i32") canonical = "int32";
  else if (suffix == "i64") canonical = "int64";
  else if (suffix == "u8") canonical = "uint8";
  else if (suffix == "u16") canonical = "uint16";
  else if (suffix == "u32") canonical = "uint32";
  else if (suffix == "u64") canonical = "uint64";
  else return default_int_literal_type(value);
  auto info = parse_canonical_int(canonical);
  if (!info || !integer_fits_width(value, *info)) {
    return make_int_type(canonical);
  }
  return make_int_type(canonical);
}

Type float_literal_type_from_suffix(std::string_view suffix) {
  if (suffix == "f32") {
    return make_float_type("float32");
  }
  if (suffix == "f64") {
    return make_float_type("float64");
  }
  return make_float_type("float32");
}

bool integer_assignable(const Type &from, const Type &to) {
  if (!is_integer_type(from) || !is_integer_type(to)) return false;
  const IntWidthInfo fw = int_width_info(from);
  const IntWidthInfo tw = int_width_info(to);
  if (fw.is_signed != tw.is_signed) return false;
  return fw.bits <= tw.bits;
}

bool float_assignable(const Type &from, const Type &to) {
  if (!is_float_type(from) || !is_float_type(to)) return false;
  if (from.name == to.name) return true;
  return from.name == "float32" && to.name == "float64";
}

std::optional<Type> try_promote_integer_binary(const Type &left, const Type &right,
                                               bool left_is_literal, bool right_is_literal) {
  if (left_is_literal && !right_is_literal && is_integer_type(right)) return right;
  if (right_is_literal && !left_is_literal && is_integer_type(left)) return left;
  if (left_is_literal && right_is_literal && left.name == right.name) return left;
  if (left_is_literal && right_is_literal) return std::nullopt;
  if (left.name == right.name && is_integer_type(left) && is_integer_type(right)) return left;
  return std::nullopt;
}

Type promote_float_binary(const Type &left, const Type &right) {
  if (float_is_64(left) || float_is_64(right)) return make_float_type("float64");
  if (left.name == right.name) return left;
  return make_float_type("float64");
}

} // namespace kinglet
