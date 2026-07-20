// Reference-counting core for the native runtime.
//
// Every heap object carries a refcount in KlHeader (starts at 1 = owning).
// kl_retain/kl_release are no-ops on non-heap wire values (plain integers and
// inline enums). At zero refcount kl_release frees the object and cascades
// into container contents via an iterative worklist so deeply nested structures
// don't overflow the C stack.
//
// Note: cyclic references (e.g. arr.push(arr)) are not reclaimed — refcount
// tracing cannot break cycles. Most programs never form self-referential
// cycles; a periodic trial-deletion pass would be needed to collect the rest.

#include "runtime/kinglet_rt_internal.h"

#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#elif defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#endif

extern "C" {

void kl_retain(kl_h value) {
  if (kl_is_heap(value)) {
    ++static_cast<KlHeader *>(kl_unbox_ptr(value))->refcount;
  }
}

void kl_release(kl_h value) {
  if (!kl_is_heap(value)) {
    return;
  }

  // Fast path: single non-container object.
  {
    auto *hdr = static_cast<KlHeader *>(kl_unbox_ptr(value));
    if (hdr->refcount == 0)
      return; // already freed
    if (--hdr->refcount != 0)
      return;
  }

  // Worklist for deferred decrement-and-delete.
  std::vector<kl_h> work;
  work.reserve(64);
  work.push_back(value);

  while (!work.empty()) {
    kl_h v = work.back();
    work.pop_back();

    if (!kl_is_heap(v))
      continue;

    auto *hdr = static_cast<KlHeader *>(kl_unbox_ptr(v));
    if (hdr->refcount == 0)
      continue; // already freed by a previous iteration
    if (--hdr->refcount != 0)
      continue;

    // Collect children before deleting the parent. Children are kl_h values
    // (int64_t) stored inside std containers; the containers' destructors
    // won't touch the raw int64_t elements, so pushing them onto the worklist
    // before `delete` is safe.
    switch (hdr->kind) {
    case KlKind::String:
      delete static_cast<KlString *>(kl_unbox_ptr(v));
      break;
    case KlKind::Float:
      delete static_cast<KlFloat *>(kl_unbox_ptr(v));
      break;
    case KlKind::Array: {
      auto *arr = static_cast<KlArray *>(kl_unbox_ptr(v));
      for (kl_h elem : arr->elements) {
        work.push_back(elem);
      }
      delete arr;
      break;
    }
    case KlKind::Map: {
      auto *map = static_cast<KlMap *>(kl_unbox_ptr(v));
      for (const auto &kv : map->int_entries) {
        work.push_back(kv.second.key);
        work.push_back(kv.second.value);
      }
      for (const auto &kv : map->str_entries) {
        work.push_back(kv.second.key);
        work.push_back(kv.second.value);
      }
      delete map;
      break;
    }
    case KlKind::Struct: {
      auto *s = static_cast<KlStruct *>(kl_unbox_ptr(v));
      for (int32_t i = 0; i < s->field_count; ++i) {
        work.push_back(s->fields()[i]);
      }
      KlStruct::destroy(s);
      break;
    }
    case KlKind::Enum: {
      auto *e = static_cast<KlEnum *>(kl_unbox_ptr(v));
      for (kl_h payload : e->payload) {
        work.push_back(payload);
      }
      delete e;
      break;
    }
    case KlKind::FieldMutRef:
      // Borrows the struct; does not own it — do not cascade into struct_obj.
      delete static_cast<KlFieldMutRef *>(kl_unbox_ptr(v));
      break;
    case KlKind::IndexMutRef:
      // Borrows the array; does not own it - do not cascade into array_obj.
      delete static_cast<KlIndexMutRef *>(kl_unbox_ptr(v));
      break;
    case KlKind::File: {
      // Close the native handle before freeing, so file handles are not
      // leaked even if the user forgot to call .close().
      auto *f = static_cast<KlFile *>(kl_unbox_ptr(v));
      if (f->fd >= 0) {
#if defined(__unix__) || defined(__APPLE__)
        ::close(static_cast<int>(f->fd));
#else
        ::_close(static_cast<int>(f->fd));
#endif
      }
      delete f;
      break;
    }
    }
  }
}

// Copy-on-write: guarantees the value at *slot is uniquely owned before a
// caller writes through it in place. Value-type containers (struct/array/map
// without @destroy) are cloned one level deep when shared (refcount > 1);
// resource-typed structs are left untouched (move-only, never cloned) and
// non-container/non-heap values pass through unchanged. The clone is stored
// back into *slot so subsequent reads of that local/field/element see the
// now-independent copy, and the original's shared reference is released.
kl_h kl_ensure_unique(kl_h *slot) {
  const kl_h value = *slot;
  if (!kl_is_heap(value)) {
    return value;
  }
  auto *hdr = static_cast<KlHeader *>(kl_unbox_ptr(value));
  if (hdr->refcount <= 1) {
    return value;
  }
  if (hdr->kind == KlKind::Struct && static_cast<KlStruct *>(kl_unbox_ptr(value))->is_resource) {
    return value;
  }
  kl_h clone = value;
  switch (hdr->kind) {
  case KlKind::Struct:
    clone = kl_struct_shallow_clone(value);
    break;
  case KlKind::Array:
    clone = kl_array_shallow_clone(value);
    break;
  case KlKind::Map:
    clone = kl_map_shallow_clone(value);
    break;
  default:
    // Strings, enums, and the borrow/file kinds are not COW targets here:
    // strings already have their own in-place-append fast path (kinglet_rt_num.cc),
    // and enums/borrows/files are either immutable-by-value or not written
    // through this path.
    return value;
  }
  *slot = clone;
  kl_release(value);
  return clone;
}

} // extern "C"
