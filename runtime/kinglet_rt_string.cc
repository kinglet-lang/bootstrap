#include "runtime/kinglet_rt_value.h"

#include <string>

namespace {

enum class KlKind : uint8_t { String = 0, Array = 1, Struct = 2 };

struct KlHeader {
  KlKind kind;
};

struct KlString {
  KlHeader hdr{KlKind::String};
  std::string bytes;
};

} // namespace

extern "C" {

kl_h kl_string_new(const char *data, int32_t len) {
  auto *obj = new KlString();
  if (data != nullptr && len > 0) {
    obj->bytes.assign(data, static_cast<std::size_t>(len));
  }
  return kl_box_ptr(obj);
}

int32_t kl_string_len(kl_h value) {
  if (!kl_is_heap(value)) {
    return 0;
  }
  return static_cast<int32_t>(
      static_cast<KlString *>(kl_unbox_ptr(value))->bytes.size());
}

} // extern "C"
