#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Native value wire format: signed integers are stored directly; heap refs are
// tagged pointers (bit 63 set). Matches the i64 stack in KirToLlvm.
using kl_h = int64_t;

static constexpr kl_h KL_TAG_HEAP = static_cast<kl_h>(1ULL << 63);

static inline kl_h kl_from_int(int32_t value) {
  return static_cast<kl_h>(value);
}

static inline int32_t kl_to_int(kl_h value) {
  return static_cast<int32_t>(value);
}

static inline int kl_is_heap(kl_h value) {
  return (value & KL_TAG_HEAP) != 0;
}

static inline void *kl_unbox_ptr(kl_h value) {
  return reinterpret_cast<void *>(static_cast<intptr_t>(value & ~KL_TAG_HEAP));
}

static inline kl_h kl_box_ptr(void *ptr) {
  return static_cast<kl_h>(reinterpret_cast<intptr_t>(ptr)) | KL_TAG_HEAP;
}

kl_h kl_string_new(const char *data, int32_t len);
int32_t kl_string_len(kl_h value);
int32_t kl_value_len(kl_h value);

kl_h kl_array_new(int32_t count, const kl_h *elements);
kl_h kl_array_get(kl_h array, int32_t index);
int32_t kl_array_len(kl_h array);

kl_h kl_struct_new(int32_t type_index, int32_t field_count, const kl_h *fields);
int32_t kl_struct_type_index(kl_h object);
kl_h kl_struct_field_at(kl_h object, int32_t field_index);

#ifdef __cplusplus
}
#endif
