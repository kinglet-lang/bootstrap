#include "runtime/kinglet_rt_internal.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

[[noreturn]] void runtime_abort(const char *message) {
  std::fprintf(stderr, "runtime error: %s\n", message);
  std::exit(70);
}

bool kl_array_is_dense(const KlArray *arr) {
  return arr != nullptr && !arr->dense_dims.empty();
}

} // namespace

void kl_array_ensure_jagged(KlArray *arr) {
  if (arr == nullptr || arr->dense_dims.empty()) {
    return;
  }
  if (arr->dense_dims.size() == 2) {
    const int32_t rows = arr->dense_dims[0];
    const int32_t cols = arr->dense_dims[1];
    std::vector<kl_h> row_handles;
    row_handles.reserve(static_cast<std::size_t>(rows));
    for (int32_t row = 0; row < rows; ++row) {
      const std::size_t base = static_cast<std::size_t>(row) * static_cast<std::size_t>(cols);
      row_handles.push_back(
          kl_array_new(cols, arr->elements.data() + static_cast<std::ptrdiff_t>(base)));
    }
    arr->dense_dims.clear();
    arr->elements = std::move(row_handles);
    return;
  }
  arr->dense_dims.clear();
  arr->elements.clear();
}

extern "C" {

kl_h kl_array_new(int32_t count, const kl_h *elements) {
  auto *obj = new KlArray();
  if (count > 0 && elements != nullptr) {
    obj->elements.assign(elements, elements + count);
    for (kl_h e : obj->elements) {
      kl_retain(e);
    }
  }
  return kl_box_ptr(obj);
}

kl_h kl_dense_array_new(int32_t rows, int32_t cols, const kl_h *elements) {
  auto *obj = new KlArray();
  if (rows > 0 && cols > 0 && elements != nullptr) {
    obj->dense_dims = {rows, cols};
    obj->elements.assign(elements,
                         elements + static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols));
    for (kl_h e : obj->elements) {
      kl_retain(e);
    }
  }
  return kl_box_ptr(obj);
}

kl_h kl_dense2d_get(kl_h grid, int32_t row, int32_t col) {
  if (!kl_is_kind(grid, KlKind::Array)) {
    return kl_from_int(0);
  }
  auto *obj = static_cast<KlArray *>(kl_unbox_ptr(grid));
  if (obj->dense_dims.size() != 2 || row < 0 || col < 0) {
    return kl_from_int(0);
  }
  const int32_t rows = obj->dense_dims[0];
  const int32_t cols = obj->dense_dims[1];
  if (row >= rows || col >= cols) {
    return kl_from_int(0);
  }
  const std::size_t idx =
      static_cast<std::size_t>(row) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(col);
  kl_h elem = obj->elements[idx];
  kl_retain(elem);
  return elem;
}

kl_h kl_array_get(kl_h array, int32_t index) {
  if (!kl_is_heap(array) || index < 0) {
    return kl_from_int(0);
  }
  void *ptr = kl_unbox_ptr(array);
  auto *hdr = static_cast<KlHeader *>(ptr);
  if (hdr->kind == KlKind::String) {
    // Mirror the VM: indexing a string yields the byte as a char (int8).
    const std::string &bytes = static_cast<KlString *>(ptr)->bytes;
    if (static_cast<std::size_t>(index) >= bytes.size()) {
      return kl_from_int(0);
    }
    return kl_from_int(static_cast<int8_t>(
        static_cast<unsigned char>(bytes[static_cast<std::size_t>(index)])));
  }
  if (hdr->kind != KlKind::Array) {
    return kl_from_int(0);
  }
  auto *obj = static_cast<KlArray *>(ptr);
  if (kl_array_is_dense(obj)) {
    if (obj->dense_dims.size() == 2) {
      const int32_t rows = obj->dense_dims[0];
      const int32_t cols = obj->dense_dims[1];
      if (index >= rows) {
        runtime_abort("Array index out of bounds.");
      }
      const std::size_t base =
          static_cast<std::size_t>(index) * static_cast<std::size_t>(cols);
      return kl_array_new(cols, obj->elements.data() + static_cast<std::ptrdiff_t>(base));
    }
    return kl_from_int(0);
  }
  if (static_cast<std::size_t>(index) >= obj->elements.size()) {
    runtime_abort("Array index out of bounds.");
  }
  kl_h elem = obj->elements[static_cast<std::size_t>(index)];
  kl_retain(elem);
  return elem;
}

int32_t kl_array_len(kl_h array) {
  if (!kl_is_heap(array)) {
    return 0;
  }
  auto *obj = static_cast<KlArray *>(kl_unbox_ptr(array));
  if (kl_array_is_dense(obj)) {
    return obj->dense_dims[0];
  }
  return static_cast<int32_t>(obj->elements.size());
}

kl_h kl_struct_new(int32_t type_index, int32_t field_count, const kl_h *fields) {
  auto *obj = new KlStruct();
  obj->type_index = type_index;
  if (field_count > 0 && fields != nullptr) {
    obj->fields.assign(fields, fields + field_count);
    for (kl_h f : obj->fields) {
      kl_retain(f);
    }
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
    return kl_array_len(value);
  }
  if (hdr->kind == KlKind::Map) {
    return static_cast<int32_t>(static_cast<KlMap *>(ptr)->order.size());
  }
  return 0;
}

kl_h kl_array_push(kl_h array, kl_h value) {
  if (kl_is_kind(array, KlKind::Array)) {
    auto *obj = static_cast<KlArray *>(kl_unbox_ptr(array));
    if (!obj->dense_dims.empty()) {
      kl_array_ensure_jagged(obj);
    }
    obj->elements.push_back(value);
    kl_retain(value);
  }
  return 0;
}

kl_h kl_array_resize(kl_h array, kl_h count, kl_h default_value) {
  if (kl_is_kind(array, KlKind::Array)) {
    const int64_t n = kl_to_int(count);
    if (n >= 0) {
      auto *obj = static_cast<KlArray *>(kl_unbox_ptr(array));
      const std::size_t old_size = obj->elements.size();
      const std::size_t new_size = static_cast<std::size_t>(n);
      if (new_size > old_size) {
        obj->elements.resize(new_size, default_value);
        for (std::size_t i = old_size; i < new_size; ++i) {
          kl_retain(obj->elements[i]);
        }
      } else if (new_size < old_size) {
        for (std::size_t i = new_size; i < old_size; ++i) {
          kl_release(obj->elements[i]);
        }
        obj->elements.resize(new_size);
      }
    }
  }
  return 0;
}

kl_h kl_array_pop(kl_h array) {
  if (!kl_is_kind(array, KlKind::Array)) {
    return 0;
  }
  auto *arr = static_cast<KlArray *>(kl_unbox_ptr(array));
  if (arr->elements.empty()) {
    return 0;
  }
  kl_h last = arr->elements.back();
  arr->elements.pop_back();
  return last;
}

kl_h kl_array_clear(kl_h array) {
  if (kl_is_kind(array, KlKind::Array)) {
    auto *obj = static_cast<KlArray *>(kl_unbox_ptr(array));
    for (kl_h e : obj->elements) {
      kl_release(e);
    }
    obj->elements.clear();
  }
  return 0;
}

kl_h kl_array_insert(kl_h array, kl_h index, kl_h value) {
  if (!kl_is_kind(array, KlKind::Array)) {
    return 0;
  }
  auto *arr = static_cast<KlArray *>(kl_unbox_ptr(array));
  const int64_t idx = kl_to_int(index);
  if (idx < 0 || static_cast<std::size_t>(idx) > arr->elements.size()) {
    return 0;
  }
  // Inserting an array splices its elements, mirroring the VM.
  if (kl_is_kind(value, KlKind::Array)) {
    auto *src = static_cast<KlArray *>(kl_unbox_ptr(value));
    arr->elements.insert(arr->elements.begin() + idx, src->elements.begin(),
                         src->elements.end());
    for (kl_h e : src->elements) {
      kl_retain(e);
    }
  } else {
    arr->elements.insert(arr->elements.begin() + idx, value);
    kl_retain(value);
  }
  return 0;
}

kl_h kl_array_reverse(kl_h array) {
  if (kl_is_kind(array, KlKind::Array)) {
    auto *arr = static_cast<KlArray *>(kl_unbox_ptr(array));
    for (std::size_t i = 0, j = arr->elements.size(); i + 1 < j; ++i, --j) {
      kl_h tmp = arr->elements[i];
      arr->elements[i] = arr->elements[j - 1];
      arr->elements[j - 1] = tmp;
    }
  }
  return 0;
}

// contains() dispatches: substring search on strings, element search on
// arrays (via kl_value_eq).
int32_t kl_contains(kl_h object, kl_h needle) {
  if (kl_is_kind(object, KlKind::String)) {
    if (!kl_is_kind(needle, KlKind::String)) {
      return 0;
    }
    const std::string &hay = static_cast<KlString *>(kl_unbox_ptr(object))->bytes;
    const std::string &sub = static_cast<KlString *>(kl_unbox_ptr(needle))->bytes;
    return hay.find(sub) != std::string::npos ? 1 : 0;
  }
  if (!kl_is_kind(object, KlKind::Array)) {
    return 0;
  }
  auto *arr = static_cast<KlArray *>(kl_unbox_ptr(object));
  for (kl_h elem : arr->elements) {
    if (kl_value_eq(elem, needle)) {
      return 1;
    }
  }
  return 0;
}

// index_of() dispatches: substring position on strings, element position on
// arrays; -1 when not found.
int64_t kl_index_of(kl_h object, kl_h needle) {
  if (kl_is_kind(object, KlKind::String)) {
    if (!kl_is_kind(needle, KlKind::String)) {
      return -1;
    }
    const std::string &hay = static_cast<KlString *>(kl_unbox_ptr(object))->bytes;
    const std::string &sub = static_cast<KlString *>(kl_unbox_ptr(needle))->bytes;
    const std::size_t pos = hay.find(sub);
    return pos == std::string::npos ? -1 : static_cast<int64_t>(pos);
  }
  if (!kl_is_kind(object, KlKind::Array)) {
    return -1;
  }
  auto *arr = static_cast<KlArray *>(kl_unbox_ptr(object));
  for (std::size_t i = 0; i < arr->elements.size(); ++i) {
    if (kl_value_eq(arr->elements[i], needle)) {
      return static_cast<int64_t>(i);
    }
  }
  return -1;
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
  kl_h field = obj->fields[static_cast<std::size_t>(field_index)];
  kl_retain(field);
  return field;
}

kl_h kl_struct_field_set(kl_h object, int32_t field_index, kl_h value) {
  if (!kl_is_heap(object) || field_index < 0) {
    return object;
  }
  auto *obj = static_cast<KlStruct *>(kl_unbox_ptr(object));
  if (static_cast<std::size_t>(field_index) >= obj->fields.size()) {
    return object;
  }
  kl_h old = obj->fields[static_cast<std::size_t>(field_index)];
  kl_retain(value);
  obj->fields[static_cast<std::size_t>(field_index)] = value;
  kl_release(old);
  return object;
}

int32_t kl_field_mut_ref_is(kl_h value) {
  return kl_is_kind(value, KlKind::FieldMutRef) ? 1 : 0;
}

kl_h kl_field_mut_ref_new(kl_h object, int32_t field_index) {
  if (!kl_is_kind(object, KlKind::Struct) || field_index < 0) {
    return kl_from_int(0);
  }
  auto *obj = static_cast<KlStruct *>(kl_unbox_ptr(object));
  if (static_cast<std::size_t>(field_index) >= obj->fields.size()) {
    return kl_from_int(0);
  }
  auto *ref = new KlFieldMutRef();
  ref->struct_obj = object;
  ref->field_index = field_index;
  return kl_box_ptr(ref);
}

kl_h kl_field_mut_ref_load(kl_h reference) {
  if (!kl_is_kind(reference, KlKind::FieldMutRef)) {
    return kl_from_int(0);
  }
  auto *ref = static_cast<KlFieldMutRef *>(kl_unbox_ptr(reference));
  return kl_struct_field_at(ref->struct_obj, ref->field_index);
}

kl_h kl_field_mut_ref_store(kl_h reference, kl_h value) {
  if (!kl_is_kind(reference, KlKind::FieldMutRef)) {
    return value;
  }
  auto *ref = static_cast<KlFieldMutRef *>(kl_unbox_ptr(reference));
  (void)kl_struct_field_set(ref->struct_obj, ref->field_index, value);
  return value;
}

kl_h kl_ref_load(kl_h reference) {
  if (kl_field_mut_ref_is(reference)) {
    return kl_field_mut_ref_load(reference);
  }
  auto *slot = reinterpret_cast<kl_h *>(static_cast<intptr_t>(reference));
  return *slot;
}

void kl_ref_store(kl_h reference, kl_h value) {
  if (kl_field_mut_ref_is(reference)) {
    (void)kl_field_mut_ref_store(reference, value);
    return;
  }
  auto *slot = reinterpret_cast<kl_h *>(static_cast<intptr_t>(reference));
  *slot = value;
}

kl_h kl_slice(kl_h value, int64_t start, int64_t end) {
  if (!kl_is_heap(value)) {
    return kl_string_new("", 0);
  }
  void *ptr = kl_unbox_ptr(value);
  auto *hdr = static_cast<KlHeader *>(ptr);
  if (hdr->kind == KlKind::String) {
    const char *data = nullptr;
    int32_t len = 0;
    if (!kl_string_view(value, &data, &len)) {
      return kl_string_new("", 0);
    }
    int64_t slen = len;
    if (start < 0) {
      start = 0;
    }
    if (end > slen) {
      end = slen;
    }
    if (start >= end) {
      return kl_string_new("", 0);
    }
    return kl_string_new(data + start, static_cast<int32_t>(end - start));
  }
  if (hdr->kind == KlKind::Array) {
    auto *arr = static_cast<KlArray *>(ptr);
    int64_t alen = static_cast<int64_t>(arr->elements.size());
    if (start < 0) {
      start = 0;
    }
    if (end > alen) {
      end = alen;
    }
    if (start >= end) {
      return kl_array_new(0, nullptr);
    }
    const int32_t count = static_cast<int32_t>(end - start);
    return kl_array_new(count, arr->elements.data() + start);
  }
  return kl_string_new("", 0);
}

} // extern "C"
