#include "runtime/kinglet_rt_internal.h"

#include <string>
#include <vector>

namespace {

// Mirror the VM's encode_map_key: strings and ints are valid keys; anything
// else collapses to the empty key (same degenerate behaviour as the VM).
std::string encode_key(kl_h key) {
  if (kl_is_kind(key, KlKind::String)) {
    return "s:" + static_cast<KlString *>(kl_unbox_ptr(key))->bytes;
  }
  if (!kl_is_heap(key) && !kl_is_inline_enum(key)) {
    return "i:" + std::to_string(kl_to_int(key));
  }
  return std::string();
}

KlMap *as_map(kl_h value) {
  if (!kl_is_kind(value, KlKind::Map)) {
    return nullptr;
  }
  return static_cast<KlMap *>(kl_unbox_ptr(value));
}

void map_set(KlMap *map, kl_h key, kl_h value) {
  std::string ek = encode_key(key);
  auto it = map->entries.find(ek);
  if (it == map->entries.end()) {
    map->order.push_back(ek);
    map->entries.emplace(ek, KlMapEntry{key, value});
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
  return obj->entries.count(encode_key(key)) != 0 ? 1 : 0;
}

kl_h kl_map_keys(kl_h map) {
  KlMap *obj = as_map(map);
  if (obj == nullptr) {
    return kl_array_new(0, nullptr);
  }
  std::vector<kl_h> keys;
  keys.reserve(obj->order.size());
  for (const std::string &ek : obj->order) {
    keys.push_back(obj->entries.at(ek).key);
  }
  return kl_array_new(static_cast<int32_t>(keys.size()), keys.data());
}

// Generic indexed read: maps look up by key (missing -> null), strings yield
// the byte as a char, arrays index by integer.
kl_h kl_index_get(kl_h object, kl_h key) {
  if (KlMap *obj = as_map(object)) {
    auto it = obj->entries.find(encode_key(key));
    if (it == obj->entries.end()) {
      return 0;
    }
    kl_h v = it->second.value;
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
    std::string ek = encode_key(key);
    auto it = obj->entries.find(ek);
    if (it != obj->entries.end()) {
      kl_h old_key = it->second.key;
      kl_h old_value = it->second.value;
      obj->entries.erase(it);
      for (auto order_it = obj->order.begin(); order_it != obj->order.end();) {
        if (*order_it == ek) {
          order_it = obj->order.erase(order_it);
        } else {
          ++order_it;
        }
      }
      kl_release(old_key);
      kl_release(old_value);
    }
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
