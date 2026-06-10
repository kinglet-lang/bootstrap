#include "runtime/kinglet_rt_internal.h"

#include <string>
#include <vector>

extern "C" {

kl_h kl_string_new(const char *data, int32_t len) {
  auto *obj = new KlString();
  if (data != nullptr && len > 0) {
    obj->bytes.assign(data, static_cast<std::size_t>(len));
  }
  return kl_box_ptr(obj);
}

int32_t kl_string_len(kl_h value) {
  if (!kl_is_heap(value)) {
    return 0;
  }
  void *ptr = kl_unbox_ptr(value);
  auto *hdr = static_cast<KlHeader *>(ptr);
  if (hdr->kind != KlKind::String) {
    return 0;
  }
  return static_cast<int32_t>(static_cast<KlString *>(ptr)->bytes.size());
}

int32_t kl_string_view(kl_h value, const char **data, int32_t *len) {
  if (data != nullptr) {
    *data = nullptr;
  }
  if (len != nullptr) {
    *len = 0;
  }
  if (!kl_is_heap(value)) {
    return 0;
  }
  void *ptr = kl_unbox_ptr(value);
  auto *hdr = static_cast<KlHeader *>(ptr);
  if (hdr->kind != KlKind::String) {
    return 0;
  }
  const KlString *str = static_cast<const KlString *>(ptr);
  if (data != nullptr) {
    *data = str->bytes.data();
  }
  if (len != nullptr) {
    *len = static_cast<int32_t>(str->bytes.size());
  }
  return 1;
}

} // extern "C"

namespace {

const std::string *string_bytes(kl_h value) {
  if (!kl_is_kind(value, KlKind::String)) {
    return nullptr;
  }
  return &static_cast<KlString *>(kl_unbox_ptr(value))->bytes;
}

kl_h make_string(const std::string &text) {
  return kl_string_new(text.data(), static_cast<int32_t>(text.size()));
}

} // namespace

extern "C" {

int32_t kl_str_starts_with(kl_h str, kl_h prefix) {
  const std::string *s = string_bytes(str);
  const std::string *p = string_bytes(prefix);
  if (s == nullptr || p == nullptr) {
    return 0;
  }
  return s->size() >= p->size() && s->compare(0, p->size(), *p) == 0 ? 1 : 0;
}

int32_t kl_str_ends_with(kl_h str, kl_h suffix) {
  const std::string *s = string_bytes(str);
  const std::string *p = string_bytes(suffix);
  if (s == nullptr || p == nullptr) {
    return 0;
  }
  return s->size() >= p->size() &&
                 s->compare(s->size() - p->size(), p->size(), *p) == 0
             ? 1
             : 0;
}

kl_h kl_str_replace(kl_h str, kl_h old_str, kl_h new_str) {
  const std::string *s = string_bytes(str);
  const std::string *o = string_bytes(old_str);
  const std::string *n = string_bytes(new_str);
  if (s == nullptr || o == nullptr || n == nullptr) {
    return make_string("");
  }
  std::string result = *s;
  if (!o->empty()) {
    std::size_t pos = 0;
    while ((pos = result.find(*o, pos)) != std::string::npos) {
      result.replace(pos, o->size(), *n);
      pos += n->size();
    }
  }
  return make_string(result);
}

kl_h kl_str_split(kl_h str, kl_h delim) {
  const std::string *s = string_bytes(str);
  const std::string *d = string_bytes(delim);
  if (s == nullptr || d == nullptr) {
    return kl_array_new(0, nullptr);
  }
  std::vector<kl_h> parts;
  if (d->empty()) {
    parts.reserve(s->size());
    for (char c : *s) {
      parts.push_back(kl_string_new(&c, 1));
    }
  } else {
    std::size_t start = 0;
    std::size_t pos = 0;
    while ((pos = s->find(*d, start)) != std::string::npos) {
      parts.push_back(make_string(s->substr(start, pos - start)));
      start = pos + d->size();
    }
    parts.push_back(make_string(s->substr(start)));
  }
  return kl_array_new(static_cast<int32_t>(parts.size()), parts.data());
}

kl_h kl_str_trim(kl_h str) {
  const std::string *s = string_bytes(str);
  if (s == nullptr) {
    return make_string("");
  }
  const std::size_t start = s->find_first_not_of(" \t\n\r");
  if (start == std::string::npos) {
    return make_string("");
  }
  const std::size_t end = s->find_last_not_of(" \t\n\r");
  return make_string(s->substr(start, end - start + 1));
}

kl_h kl_str_to_upper(kl_h str) {
  const std::string *s = string_bytes(str);
  if (s == nullptr) {
    return make_string("");
  }
  std::string result = *s;
  for (char &c : result) {
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - 'a' + 'A');
    }
  }
  return make_string(result);
}

kl_h kl_str_to_lower(kl_h str) {
  const std::string *s = string_bytes(str);
  if (s == nullptr) {
    return make_string("");
  }
  std::string result = *s;
  for (char &c : result) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return make_string(result);
}

} // extern "C"
