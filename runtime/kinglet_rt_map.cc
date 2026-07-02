#include "runtime/kinglet_rt_internal.h"

#include <string>
#include <vector>

namespace {

KlMap *as_map(kl_h value) {
  if (!kl_is_kind(value, KlKind::Map)) {
    return nullptr;
  }
  return static_cast<KlMap *>(kl_unbox_ptr(value));
}

// Returns true if two order-list keys match. Int keys compared by value;
// string keys compared by content.
bool order_key_match(kl_h a, kl_h b) {
  if (kl_is_kind(a, KlKind::String) && kl_is_kind(b, KlKind::String)) {
    return static_cast<KlString *>(kl_unbox_ptr(a))->bytes ==
           static_cast<KlString *>(kl_unbox_ptr(b))->bytes;
  }
  if (!kl_is_heap(a) && !kl_is_heap(b) && !kl_is_inline_enum(a) &&
      !kl_is_inline_enum(b)) {
    return a == b;
  }
  return a == b;
}

bool is_string_key(kl_h key) {
  return kl_is_kind(key, KlKind::String);
}

bool is_int_key(kl_h key) {
  return !kl_is_heap(key) && !kl_is_inline_enum(key);
}

void map_set(KlMap *map, kl_h key, kl_h value) {
  if (is_string_key(key)) {
    const std::string &bytes = static_cast<KlString *>(kl_unbox_ptr(key))->bytes;
    auto it = map->str_entries.find(bytes);
    if (it == map->str_entries.end()) {
      map->order.push_back(key);
      map->str_entries.emplace(bytes, KlMapEntry{key, value});
      kl_retain(key);
      kl_retain(value);
    } else {
      kl_h old_key = it->second.key;
      kl_h old_value = it->second.value;
      it->second = KlMapEntry{key, value};
      kl_retain(key);
      kl_retain(value);
      kl_release(old_key);
      kl_release(old_value);
    }
    return;
  }
  if (is_int_key(key)) {
    const int64_t ik = kl_to_int(key);
    auto it = map->int_entries.find(ik);
    if (it == map->int_entries.end()) {
      map->order.push_back(key);
      map->int_entries.emplace(ik, KlMapEntry{key, value});
      kl_retain(key);
      kl_retain(value);
    } else {
      kl_h old_key = it->second.key;
      kl_h old_value = it->second.value;
      it->second = KlMapEntry{key, value};
      kl_retain(key);
      kl_retain(value);
      kl_release(old_key);
      kl_release(old_value);
    }
    return;
  }
  // Invalid key type — silently accept but store nothing (degenerate path).
}

// Look up a key's KlMapEntry pointer, or nullptr if not found.
const KlMapEntry *map_find(KlMap *map, kl_h key) {
  if (is_string_key(key)) {
    const auto &bytes = static_cast<KlString *>(kl_unbox_ptr(key))->bytes;
    auto it = map->str_entries.find(bytes);
    return it != map->str_entries.end() ? &it->second : nullptr;
  }
  if (is_int_key(key)) {
    auto it = map->int_entries.find(kl_to_int(key));
    return it != map->int_entries.end() ? &it->second : nullptr;
  }
  return nullptr;
}

// Erase a key from the map sub-stores (retains/releases handled by callers).
bool map_erase(KlMap *map, kl_h key) {
  if (is_string_key(key)) {
    return map->str_entries.erase(
               static_cast<KlString *>(kl_unbox_ptr(key))->bytes) > 0;
  }
  if (is_int_key(key)) {
    return map->int_entries.erase(kl_to_int(key)) > 0;
  }
  return false;
}

} // namespace

extern "C" {

kl_h kl_map_new(int32_t pair_count, const kl_h *pairs) {
  auto *obj = new KlMap();
  for (int32_t i = 0; i < pair_count; ++i) {
    map_set(obj, pairs[2 * i], pairs[2 * i + 1]);
  }
  return kl_box_ptr(obj);
}

int32_t kl_map_has(kl_h map, kl_h key) {
  KlMap *obj = as_map(map);
  if (obj == nullptr) {
    return 0;
  }
  return map_find(obj, key) != nullptr ? 1 : 0;
}

kl_h kl_map_keys(kl_h map) {
  KlMap *obj = as_map(map);
  if (obj == nullptr) {
    return kl_array_new(0, nullptr);
  }
  std::vector<kl_h> keys;
  keys.reserve(obj->order.size());
  for (kl_h k : obj->order) {
    const KlMapEntry *entry = map_find(obj, k);
    if (entry != nullptr) {
      keys.push_back(entry->key);
    }
  }
  return kl_array_new(static_cast<int32_t>(keys.size()), keys.data());
}

// Generic indexed read: maps look up by key (missing -> null), strings yield
// the byte as a char, arrays index by integer.
kl_h kl_index_get(kl_h object, kl_h key) {
  if (KlMap *obj = as_map(object)) {
    const KlMapEntry *entry = map_find(obj, key);
    if (entry == nullptr) {
      return 0;
    }
    kl_h v = entry->value;
    kl_retain(v);
    return v;
  }
  return kl_array_get(object, static_cast<int32_t>(kl_to_int(key)));
}

// Generic indexed write; returns the stored value (the VM pushes it back).
kl_h kl_index_set(kl_h object, kl_h key, kl_h value) {
  if (KlMap *obj = as_map(object)) {
    map_set(obj, key, value);
    return value;
  }
  if (kl_is_kind(object, KlKind::Array)) {
    auto *arr = static_cast<KlArray *>(kl_unbox_ptr(object));
    if (!arr->dense_dims.empty()) {
      kl_array_ensure_jagged(arr);
    }
    const int64_t idx = kl_to_int(key);
    if (idx >= 0 && static_cast<std::size_t>(idx) < arr->elements.size()) {
      kl_h old = arr->elements[static_cast<std::size_t>(idx)];
      kl_retain(value);
      arr->elements[static_cast<std::size_t>(idx)] = value;
      kl_release(old);
    }
    return value;
  }
  return value;
}

// remove() on maps erases by key; on arrays erases by index and returns the
// removed element.
kl_h kl_remove(kl_h object, kl_h key) {
  if (KlMap *obj = as_map(object)) {
    const KlMapEntry *entry = map_find(obj, key);
    if (entry == nullptr) {
      return 0;
    }
    kl_h old_key = entry->key;
    kl_h old_value = entry->value;
    (void)map_erase(obj, key);
    // Remove from insertion-order list.
    for (auto it = obj->order.begin(); it != obj->order.end(); ++it) {
      if (order_key_match(*it, old_key)) {
        obj->order.erase(it);
        break;
      }
    }
    kl_release(old_key);
    kl_release(old_value);
    return 0;
  }
  if (kl_is_kind(object, KlKind::Array)) {
    auto *arr = static_cast<KlArray *>(kl_unbox_ptr(object));
    const int64_t idx = kl_to_int(key);
    if (idx < 0 || static_cast<std::size_t>(idx) >= arr->elements.size()) {
      return 0;
    }
    kl_h removed = arr->elements[static_cast<std::size_t>(idx)];
    arr->elements.erase(arr->elements.begin() + idx);
    return removed;
  }
  return 0;
}

} // extern "C"
