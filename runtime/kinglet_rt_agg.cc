#include "runtime/kinglet_rt_value.h"

#include <vector>

namespace {

enum class KlKind : uint8_t { String = 0, Array = 1, Struct = 2 };

struct KlHeader {
  KlKind kind;
};

struct KlArray {
  KlHeader hdr{KlKind::Array};
  std::vector<kl_h> elements;
};

struct KlStruct {
  KlHeader hdr{KlKind::Struct};
  int32_t type_index = 0;
  std::vector<kl_h> fields;
};

} // namespace

extern "C" int32_t kl_string_len(kl_h value);

extern "C" {

kl_h kl_array_new(int32_t count, const kl_h *elements) {
  auto *obj = new KlArray();
  if (count > 0 && elements != nullptr) {
    obj->elements.assign(elements, elements + count);
  }
  return kl_box_ptr(obj);
}

kl_h kl_array_get(kl_h array, int32_t index) {
  if (!kl_is_heap(array) || index < 0) {
    return kl_from_int(0);
  }
  auto *obj = static_cast<KlArray *>(kl_unbox_ptr(array));
  if (static_cast<std::size_t>(index) >= obj->elements.size()) {
    return kl_from_int(0);
  }
  return obj->elements[static_cast<std::size_t>(index)];
}

int32_t kl_array_len(kl_h array) {
  if (!kl_is_heap(array)) {
    return 0;
  }
  return static_cast<int32_t>(
      static_cast<KlArray *>(kl_unbox_ptr(array))->elements.size());
}

kl_h kl_struct_new(int32_t type_index, int32_t field_count, const kl_h *fields) {
  auto *obj = new KlStruct();
  obj->type_index = type_index;
  if (field_count > 0 && fields != nullptr) {
    obj->fields.assign(fields, fields + field_count);
  }
  return kl_box_ptr(obj);
}

int32_t kl_value_len(kl_h value) {
  if (!kl_is_heap(value)) {
    return 0;
  }
  void *ptr = kl_unbox_ptr(value);
  auto *hdr = static_cast<KlHeader *>(ptr);
  if (hdr->kind == KlKind::String) {
    return kl_string_len(value);
  }
  if (hdr->kind == KlKind::Array) {
    return static_cast<int32_t>(static_cast<KlArray *>(ptr)->elements.size());
  }
  return 0;
}

int32_t kl_struct_type_index(kl_h object) {
  if (!kl_is_heap(object)) {
    return -1;
  }
  void *ptr = kl_unbox_ptr(object);
  auto *hdr = static_cast<KlHeader *>(ptr);
  if (hdr->kind != KlKind::Struct) {
    return -1;
  }
  return static_cast<KlStruct *>(ptr)->type_index;
}

kl_h kl_struct_field_at(kl_h object, int32_t field_index) {
  if (!kl_is_heap(object) || field_index < 0) {
    return kl_from_int(0);
  }
  auto *obj = static_cast<KlStruct *>(kl_unbox_ptr(object));
  if (static_cast<std::size_t>(field_index) >= obj->fields.size()) {
    return kl_from_int(0);
  }
  return obj->fields[static_cast<std::size_t>(field_index)];
}

} // extern "C"
