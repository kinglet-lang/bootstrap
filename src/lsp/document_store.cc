#include "lsp/document_store.h"

namespace kinglet::lsp {

void DocumentStore::open(const std::string &uri, const std::string &text, int version) {
  docs_[uri] = Document{uri, version, text, true, {}};
}

void DocumentStore::change(const std::string &uri, const std::string &text, int version) {
  auto it = docs_.find(uri);
  if (it != docs_.end()) {
    it->second.text = text;
    it->second.version = version;
    it->second.dirty = true;
  } else {
    open(uri, text, version);
  }
}

void DocumentStore::close(const std::string &uri) {
  docs_.erase(uri);
}

Document *DocumentStore::get(const std::string &uri) {
  auto it = docs_.find(uri);
  return it != docs_.end() ? &it->second : nullptr;
}

const Document *DocumentStore::get(const std::string &uri) const {
  auto it = docs_.find(uri);
  return it != docs_.end() ? &it->second : nullptr;
}

} // namespace kinglet::lsp
