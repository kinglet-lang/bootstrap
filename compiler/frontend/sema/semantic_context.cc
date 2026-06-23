// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "frontend/sema/semantic_context.h"

namespace kinglet {

bool SemanticContext::function_uses_concept_params(const ast::FunctionDecl &function) const {
  for (const auto &param : function.params) {
    if (param.type.type_args.empty() && concept_registry_.count(param.type.name)) {
      return true;
    }
  }
  return false;
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
