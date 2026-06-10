#include "runtime/kinglet_rt_value.h"

#include <cerrno>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

enum class KlKind : uint8_t { String = 0, Array = 1, Struct = 2, Enum = 3 };

struct KlHeader {
  KlKind kind;
};

struct KlString {
  KlHeader hdr{KlKind::String};
  std::string bytes;
};

struct KlEnum {
  KlHeader hdr{KlKind::Enum};
  int32_t type_index = 0;
  int32_t variant_index = 0;
  std::vector<kl_h> payload;
};

bool decode_enum_tag(kl_h value, int32_t *type_index, int32_t *variant_index) {
  if (kl_is_inline_enum(value)) {
    const uint64_t wire = static_cast<uint64_t>(value);
    *type_index = static_cast<int32_t>((wire >> 16) & 0xFFFF);
    *variant_index = static_cast<int32_t>(wire & 0xFFFF);
    return true;
  }
  if (kl_is_heap(value)) {
    auto *obj = static_cast<KlEnum *>(kl_unbox_ptr(value));
    if (obj->hdr.kind != KlKind::Enum) {
      return false;
    }
    *type_index = obj->type_index;
    *variant_index = obj->variant_index;
    return true;
  }
  return false;
}

kl_h cast_error(int32_t variant, kl_h payload) {
  kl_h elements[1] = {payload};
  const int32_t count = payload == 0 ? 0 : 1;
  return kl_enum_new_payload(0, variant, count, count > 0 ? elements : nullptr);
}

} // namespace

extern "C" {

kl_h kl_enum_new(int32_t type_index, int32_t variant_index) {
  auto *obj = new KlEnum();
  obj->type_index = type_index;
  obj->variant_index = variant_index;
  return kl_box_ptr(obj);
}

kl_h kl_enum_new_payload(int32_t type_index, int32_t variant_index, int32_t count,
                         const kl_h *elements) {
  auto *obj = new KlEnum();
  obj->type_index = type_index;
  obj->variant_index = variant_index;
  if (count > 0 && elements != nullptr) {
    obj->payload.assign(elements, elements + count);
  }
  return kl_box_ptr(obj);
}

kl_h kl_enum_payload_at(kl_h value, int32_t index) {
  if (!kl_is_heap(value) || index < 0) {
    return kl_from_int(0);
  }
  auto *obj = static_cast<KlEnum *>(kl_unbox_ptr(value));
  if (obj->hdr.kind != KlKind::Enum) {
    return kl_from_int(0);
  }
  if (static_cast<std::size_t>(index) >= obj->payload.size()) {
    return kl_from_int(0);
  }
  return obj->payload[static_cast<std::size_t>(index)];
}

kl_h kl_cast_to_int(kl_h value) {
  if (!kl_is_heap(value) && !kl_is_inline_enum(value)) {
    return value;
  }
  if (kl_is_inline_enum(value)) {
    int32_t type_idx = 0;
    int32_t variant_idx = 0;
    if (!decode_enum_tag(value, &type_idx, &variant_idx)) {
      return kl_from_int(0);
    }
    return kl_from_int(variant_idx);
  }
  if (!kl_is_heap(value)) {
    return value;
  }
  void *ptr = kl_unbox_ptr(value);
  auto *hdr = static_cast<KlHeader *>(ptr);
  if (hdr->kind == KlKind::Enum) {
    return kl_from_int(static_cast<KlEnum *>(ptr)->variant_index);
  }
  if (hdr->kind != KlKind::String) {
    return kl_from_int(0);
  }
  const std::string &s = static_cast<KlString *>(ptr)->bytes;
  if (s.empty()) {
    return cast_error(0, 0);
  }
  char *end = nullptr;
  errno = 0;
  const long long v = std::strtoll(s.c_str(), &end, 10);
  if (end == s.c_str() || *end != '\0') {
    return cast_error(1, value);
  }
  if (errno == ERANGE) {
    return cast_error(2, value);
  }
  return kl_from_int(static_cast<int64_t>(v));
}

int32_t kl_value_is_err(kl_h value) {
  if (value == 0) {
    return 1;
  }
  if (kl_is_inline_enum(value)) {
    const int type_idx = static_cast<int>((static_cast<uint64_t>(value) >> 16) & 0xFFFF);
    return type_idx == 0 ? 1 : 0;
  }
  if (kl_is_heap(value)) {
    auto *obj = static_cast<KlEnum *>(kl_unbox_ptr(value));
    if (obj->hdr.kind == KlKind::Enum) {
      return obj->type_index == 0 ? 1 : 0;
    }
  }
  return 0;
}

int32_t kl_value_eq(kl_h left, kl_h right) {
  if (kl_is_inline_enum(left) || kl_is_inline_enum(right) ||
      (kl_is_heap(left) && static_cast<KlHeader *>(kl_unbox_ptr(left))->kind == KlKind::Enum) ||
      (kl_is_heap(right) && static_cast<KlHeader *>(kl_unbox_ptr(right))->kind == KlKind::Enum)) {
    int32_t lt = 0;
    int32_t lv = 0;
    int32_t rt = 0;
    int32_t rv = 0;
    if (!decode_enum_tag(left, &lt, &lv) || !decode_enum_tag(right, &rt, &rv)) {
      return 0;
    }
    return (lt == rt && lv == rv) ? 1 : 0;
  }
  if (!kl_is_heap(left) && !kl_is_heap(right)) {
    return left == right ? 1 : 0;
  }
  if (!kl_is_heap(left) || !kl_is_heap(right)) {
    return 0;
  }
  void *lp = kl_unbox_ptr(left);
  void *rp = kl_unbox_ptr(right);
  auto *lh = static_cast<KlHeader *>(lp);
  auto *rh = static_cast<KlHeader *>(rp);
  if (lh->kind != rh->kind) {
    return 0;
  }
  if (lh->kind == KlKind::String) {
    return static_cast<KlString *>(lp)->bytes == static_cast<KlString *>(rp)->bytes ? 1 : 0;
  }
  return lp == rp ? 1 : 0;
}

int32_t kl_exit_code(kl_h value) {
  if (kl_is_heap(value)) {
    return 0;
  }
  const int64_t n = kl_to_int(value);
  if (n < 0) {
    return 255;
  }
  if (n > 255) {
    return 255;
  }
  return static_cast<int32_t>(n);
}

} // extern "C"
