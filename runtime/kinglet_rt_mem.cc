// Reference-counting core for the native runtime.
//
// Every heap object carries a refcount in KlHeader (starts at 1 = owning).
// kl_retain/kl_release are no-ops on non-heap wire values (plain integers and
// inline enums). At zero refcount kl_release frees the object and recurses
// into container contents so Array/Map/Struct/Enum release everything they own.
//
// Note: cyclic references (e.g. arr.push(arr)) are not reclaimed — refcount
// tracing cannot break cycles. Most programs never form self-referential
// cycles; a periodic trial-deletion pass would be needed to collect the rest.

#include "runtime/kinglet_rt_internal.h"

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
  auto *hdr = static_cast<KlHeader *>(kl_unbox_ptr(value));
  if (hdr->refcount == 0) {
    // Defensive: already freed. Prevents a double release from corrupting
    // an unrelated allocation.
    return;
  }
  if (--hdr->refcount != 0) {
    return;
  }
  switch (hdr->kind) {
  case KlKind::String:
    delete static_cast<KlString *>(kl_unbox_ptr(value));
    break;
  case KlKind::Float:
    delete static_cast<KlFloat *>(kl_unbox_ptr(value));
    break;
  case KlKind::Array: {
    auto *arr = static_cast<KlArray *>(kl_unbox_ptr(value));
    for (kl_h elem : arr->elements) {
      kl_release(elem);
    }
    delete arr;
    break;
  }
  case KlKind::Map: {
    auto *map = static_cast<KlMap *>(kl_unbox_ptr(value));
    for (const auto &kv : map->entries) {
      kl_release(kv.second.key);
      kl_release(kv.second.value);
    }
    delete map;
    break;
  }
  case KlKind::Struct: {
    auto *s = static_cast<KlStruct *>(kl_unbox_ptr(value));
    for (kl_h field : s->fields) {
      kl_release(field);
    }
    delete s;
    break;
  }
  case KlKind::Enum: {
    auto *e = static_cast<KlEnum *>(kl_unbox_ptr(value));
    for (kl_h payload : e->payload) {
      kl_release(payload);
    }
    delete e;
    break;
  }
  case KlKind::FieldMutRef:
    // Borrows the struct; does not own it — do not cascade into struct_obj.
    delete static_cast<KlFieldMutRef *>(kl_unbox_ptr(value));
    break;
  }
}

} // extern "C"
