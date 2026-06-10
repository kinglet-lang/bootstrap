#include "runtime/kinglet_rt_internal.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

namespace {

[[noreturn]] void runtime_abort(const char *message) {
  std::fprintf(stderr, "runtime error: %s\n", message);
  std::exit(70);
}

bool is_float(kl_h value) { return kl_is_kind(value, KlKind::Float); }

bool is_string(kl_h value) { return kl_is_kind(value, KlKind::String); }

std::string float_text(double value) {
  // Match the VM's default ostream formatting for doubles.
  std::ostringstream oss;
  oss << value;
  return oss.str();
}

kl_h concat(kl_h left, kl_h right) {
  const std::string text = kl_value_text(left) + kl_value_text(right);
  return kl_string_new(text.data(), static_cast<int32_t>(text.size()));
}

} // namespace

std::string kl_value_text(kl_h value) {
  if (value == 0) {
    return "null";
  }
  if (kl_is_inline_enum(value)) {
    const int variant = static_cast<int>(static_cast<uint64_t>(value) & 0xFFFF);
    return std::to_string(variant);
  }
  if (!kl_is_heap(value)) {
    return std::to_string(kl_to_int(value));
  }
  void *ptr = kl_unbox_ptr(value);
  switch (static_cast<KlHeader *>(ptr)->kind) {
  case KlKind::String:
    return static_cast<KlString *>(ptr)->bytes;
  case KlKind::Float:
    return float_text(static_cast<KlFloat *>(ptr)->value);
  case KlKind::Enum:
    return "<enum>";
  case KlKind::Array:
    return "[array]";
  case KlKind::Struct:
    return "<struct>";
  }
  return "?";
}

extern "C" {

kl_h kl_float_new(double value) {
  auto *obj = new KlFloat();
  obj->value = value;
  return kl_box_ptr(obj);
}

kl_h kl_float_from_bits(int64_t bits) {
  double value = 0.0;
  std::memcpy(&value, &bits, sizeof(value));
  return kl_float_new(value);
}

int64_t kl_float_to_bits(kl_h value) {
  const double v = is_float(value) ? static_cast<KlFloat *>(kl_unbox_ptr(value))->value
                                   : static_cast<double>(kl_to_int(value));
  int64_t bits = 0;
  std::memcpy(&bits, &v, sizeof(bits));
  return bits;
}

kl_h kl_value_add(kl_h left, kl_h right) {
  if (is_string(left) || is_string(right)) {
    return concat(left, right);
  }
  if (is_float(left) || is_float(right)) {
    return kl_float_new(kl_as_double(left) + kl_as_double(right));
  }
  return kl_from_int(kl_to_int(left) + kl_to_int(right));
}

kl_h kl_value_sub(kl_h left, kl_h right) {
  if (is_float(left) || is_float(right)) {
    return kl_float_new(kl_as_double(left) - kl_as_double(right));
  }
  return kl_from_int(kl_to_int(left) - kl_to_int(right));
}

kl_h kl_value_mul(kl_h left, kl_h right) {
  if (is_float(left) || is_float(right)) {
    return kl_float_new(kl_as_double(left) * kl_as_double(right));
  }
  return kl_from_int(kl_to_int(left) * kl_to_int(right));
}

kl_h kl_value_div(kl_h left, kl_h right) {
  if (is_float(left) || is_float(right)) {
    return kl_float_new(kl_as_double(left) / kl_as_double(right));
  }
  const int64_t divisor = kl_to_int(right);
  if (divisor == 0) {
    runtime_abort("Division by zero.");
  }
  return kl_from_int(kl_to_int(left) / divisor);
}

kl_h kl_value_mod(kl_h left, kl_h right) {
  if (is_float(left) || is_float(right)) {
    return kl_float_new(std::fmod(kl_as_double(left), kl_as_double(right)));
  }
  const int64_t divisor = kl_to_int(right);
  if (divisor == 0) {
    runtime_abort("Modulo by zero.");
  }
  return kl_from_int(kl_to_int(left) % divisor);
}

int32_t kl_value_cmp(kl_h left, kl_h right) {
  if (is_string(left) && is_string(right)) {
    const std::string &l = static_cast<KlString *>(kl_unbox_ptr(left))->bytes;
    const std::string &r = static_cast<KlString *>(kl_unbox_ptr(right))->bytes;
    return l < r ? -1 : (l == r ? 0 : 1);
  }
  if (is_float(left) || is_float(right)) {
    const double l = kl_as_double(left);
    const double r = kl_as_double(right);
    return l < r ? -1 : (l == r ? 0 : 1);
  }
  const int64_t l = kl_to_int(left);
  const int64_t r = kl_to_int(right);
  return l < r ? -1 : (l == r ? 0 : 1);
}

} // extern "C"
