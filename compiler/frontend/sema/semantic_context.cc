// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "frontend/sema/semantic_context.h"

#include <algorithm>

namespace kinglet {

bool SemanticContext::function_uses_concept_params(const ast::FunctionDecl &function) const {
  return std::ranges::any_of(function.params, [this](const ast::Parameter &param) {
    if (concept_registry_.contains(param.type.name)) {
      return true;
    }
    // Handle io::-qualified builtin concept names (ADR 0026 D7).
    if (param.type.name == "io::reader" || param.type.name == "io::writer") {
      return concept_registry_.contains(param.type.name.substr(4));
    }
    return false;
  });
}

void SemanticContext::clear() {
  used_.clear();
  opened_.clear();
  imported_namespaces_.clear();
  imported_qualifiers_.clear();
  module_aliases_.clear();
  generic_structs_.clear();
  generic_functions_.clear();
  concept_generic_functions_.clear();
  concept_registry_.clear();
}

} // namespace kinglet
