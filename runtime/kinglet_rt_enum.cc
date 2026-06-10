#include "runtime/kinglet_rt_value.h"

#include <string>

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
};

} // namespace

extern "C" {

kl_h kl_enum_new(int32_t type_index, int32_t variant_index) {
  auto *obj = new KlEnum();
  obj->type_index = type_index;
  obj->variant_index = variant_index;
  return kl_box_ptr(obj);
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
  if (kl_is_inline_enum(left) && kl_is_inline_enum(right)) {
    return left == right ? 1 : 0;
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
  if (lh->kind == KlKind::Enum) {
    auto *le = static_cast<KlEnum *>(lp);
    auto *re = static_cast<KlEnum *>(rp);
    return (le->type_index == re->type_index && le->variant_index == re->variant_index) ? 1
                                                                                        : 0;
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
