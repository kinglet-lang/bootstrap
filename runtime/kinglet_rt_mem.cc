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
      for (kl_h field : s->fields) {
        work.push_back(field);
      }
      delete s;
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
    }
  }
}

} // extern "C"
