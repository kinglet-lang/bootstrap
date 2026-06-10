#include "runtime/kinglet_rt_internal.h"

#include <string>

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
  void *ptr = kl_unbox_ptr(value);
  auto *hdr = static_cast<KlHeader *>(ptr);
  if (hdr->kind != KlKind::String) {
    return 0;
  }
  return static_cast<int32_t>(static_cast<KlString *>(ptr)->bytes.size());
}

int32_t kl_string_view(kl_h value, const char **data, int32_t *len) {
  if (data != nullptr) {
    *data = nullptr;
  }
  if (len != nullptr) {
    *len = 0;
  }
  if (!kl_is_heap(value)) {
    return 0;
  }
  void *ptr = kl_unbox_ptr(value);
  auto *hdr = static_cast<KlHeader *>(ptr);
  if (hdr->kind != KlKind::String) {
    return 0;
  }
  const KlString *str = static_cast<const KlString *>(ptr);
  if (data != nullptr) {
    *data = str->bytes.data();
  }
  if (len != nullptr) {
    *len = static_cast<int32_t>(str->bytes.size());
  }
  return 1;
}

} // extern "C"
