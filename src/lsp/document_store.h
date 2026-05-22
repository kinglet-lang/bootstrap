#pragma once

#include "ast/ast.h"
#include "lsp/analysis.h"

#include <string>
#include <unordered_map>

namespace kinglet::lsp {

struct Document {
  std::string uri;
  int version = 0;
  std::string text;
  bool dirty = true;
  AnalysisResult analysis;
};

class DocumentStore {
public:
  void open(const std::string &uri, const std::string &text, int version = 0);
  void change(const std::string &uri, const std::string &text, int version = 0);
  void close(const std::string &uri);
  Document *get(const std::string &uri);
  const Document *get(const std::string &uri) const;

private:
  std::unordered_map<std::string, Document> docs_;
};

} // namespace kinglet::lsp
