#pragma once

// Shared heap object layout for the native runtime. Every boxed value starts
// with a KlHeader; the kind discriminates the concrete layout. This header is
// private to runtime/*.cc — the public ABI is kinglet_rt_value.h.

#include "runtime/kinglet_rt_value.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

enum class KlKind : uint8_t {
  String = 0,
  Array = 1,
  Struct = 2,
  Enum = 3,
  Float = 4,
  Map = 5,
  FieldMutRef = 6,
};

struct KlHeader {
  KlKind kind;
  // Reference count: starts at 1 (owning) on allocation; kl_retain increments,
  // kl_release decrements and frees (cascading into container elements) at zero.
  uint32_t refcount = 1;
};

struct KlString {
  KlHeader hdr{KlKind::String};
  std::string bytes;
};

struct KlArray {
  KlHeader hdr{KlKind::Array};
  std::vector<kl_h> elements;
  // Non-empty => row-major flat storage; rank = dense_dims.size().
  std::vector<int32_t> dense_dims;
};

struct KlStruct {
  KlHeader hdr{KlKind::Struct};
  int32_t type_index = 0;
  std::vector<kl_h> fields;
};

struct KlEnum {
  KlHeader hdr{KlKind::Enum};
  int32_t type_index = 0;
  int32_t variant_index = 0;
  std::vector<kl_h> payload;
};

struct KlFloat {
  KlHeader hdr{KlKind::Float};
  double value = 0.0;
};

struct KlMapEntry {
  kl_h key = 0;
  kl_h value = 0;
};

// Insertion-ordered map with separate storage for int and string keys
// so lookups never allocate (no "s:"/"i:" prefix encoding).
struct KlMap {
  KlHeader hdr{KlKind::Map};
  // Original keys in insertion order (kl_h values). Walked by kl_map_keys()
  // and kl_release(). Lookups dispatch to the correct sub-map below.
  std::vector<kl_h> order;
  std::unordered_map<int64_t, KlMapEntry> int_entries;
  std::unordered_map<std::string, KlMapEntry> str_entries;
};

struct KlFieldMutRef {
  KlHeader hdr{KlKind::FieldMutRef};
  kl_h struct_obj = 0;
  int32_t field_index = -1;
};

inline KlKind kl_heap_kind(kl_h value) {
  return static_cast<KlHeader *>(kl_unbox_ptr(value))->kind;
}

inline bool kl_is_kind(kl_h value, KlKind kind) {
  return kl_is_heap(value) && kl_heap_kind(value) == kind;
}

// Convert dense storage to nested row arrays when a mutating API is used.
void kl_array_ensure_jagged(KlArray *arr);

// Unbox a numeric value to double: boxed float as-is, plain integer widened.
inline double kl_as_double(kl_h value) {
  if (kl_is_kind(value, KlKind::Float)) {
    return static_cast<KlFloat *>(kl_unbox_ptr(value))->value;
  }
  return static_cast<double>(kl_to_int(value));
}

// Display text shared by io printing, string concat, and string casts.
// Defined in kinglet_rt_num.cc.
std::string kl_value_text(kl_h value);
