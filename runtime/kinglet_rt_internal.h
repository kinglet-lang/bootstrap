#pragma once

// Shared heap object layout for the native runtime. Every boxed value starts
// with a KlHeader; the kind discriminates the concrete layout. This header is
// private to runtime/*.cc — the public ABI is kinglet_rt_value.h.

#include "runtime/kinglet_rt_value.h"

#include <cstring>
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
  IndexMutRef = 7,
  File = 8,
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

// Flat heap storage: header + type_index + field_count, followed by an inline
// array of field_count kl_h values. Use KlStruct::create(n) to allocate, and
// fields() / field_count to iterate. No std::vector indirection.
struct KlStruct {
  static KlStruct *create(int32_t field_count) {
    const std::size_t sz = sizeof(KlStruct) + sizeof(kl_h) * static_cast<std::size_t>(field_count);
    void *buf = ::operator new(sz);
    auto *s = static_cast<KlStruct *>(buf);
    s->hdr.kind = KlKind::Struct;
    s->hdr.refcount = 1;
    s->type_index = 0;
    s->field_count = field_count;
    s->is_resource = false;
    return s;
  }
  static void destroy(KlStruct *s) { ::operator delete(s); }

  kl_h *fields() { return reinterpret_cast<kl_h *>(this + 1); }
  const kl_h *fields() const { return reinterpret_cast<const kl_h *>(this + 1); }

  KlHeader hdr{}; // kind = Struct; refcount set by create()
  int32_t type_index = 0;
  int32_t field_count = 0;
  // Mirrors the checker's Type::is_resource for this struct's declared type
  // (true iff the struct has an @destroy body). Resource-typed values are
  // move-only by language semantics, so kl_ensure_unique() must never clone
  // them -- cloning would fabricate a second independent owner of a value
  // the type system treats as uniquely owned. Set by kl_struct_new() from
  // the type metadata the compiler already tracks (KirStructMeta::has_destroy).
  bool is_resource = false;
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

struct KlIndexMutRef {
  KlHeader hdr{KlKind::IndexMutRef};
  kl_h array_obj = 0;
  int32_t index = -1;
};

struct KlFile {
  KlHeader hdr{KlKind::File};
  // Native file handle. -1 means closed/invalid.
  // On POSIX this stores a POSIX fd cast to int64_t; on Windows a HANDLE.
  int64_t fd = -1;
  // 0 = read mode, 1 = write mode (set by fs::open / fs::create).
  int32_t write_mode = 0;
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
  if (kl_is_inline_float(value)) {
    const uint32_t bits = static_cast<uint32_t>(static_cast<uint64_t>(value) & 0xFFFFFFFFULL);
    float f = 0.0f;
    std::memcpy(&f, &bits, sizeof(f));
    return static_cast<double>(f);
  }
  if (kl_is_kind(value, KlKind::Float)) {
    return static_cast<KlFloat *>(kl_unbox_ptr(value))->value;
  }
  return static_cast<double>(kl_to_int(value));
}

// Display text shared by io printing, string concat, and string casts.
// Defined in kinglet_rt_num.cc.
std::string kl_value_text(kl_h value);
