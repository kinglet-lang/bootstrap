// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "frontend/ast/ast.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace kinglet {

class ModuleLoader;

// Shared semantic state consumed by both TypeChecker and Compiler.
// Holds the program-scope name tables that both passes must agree on:
// using/namespace tracking, module alias mapping, generic and concept
// declaration registries, and imported-namespace sets.
class SemanticContext {
public:
  bool function_uses_concept_params(const ast::FunctionDecl &function) const;
  void clear();

  std::unordered_set<std::string> used_;
  std::unordered_set<std::string> opened_;
  std::unordered_set<std::string> imported_namespaces_;
  std::unordered_set<std::string> imported_qualifiers_;
  std::unordered_map<std::string, std::string> module_aliases_;
  std::unordered_map<std::string, const ast::StructDecl *> generic_structs_;
  std::unordered_map<std::string, const ast::FunctionDecl *> generic_functions_;
  std::unordered_map<std::string, const ast::FunctionDecl *> concept_generic_functions_;
  std::unordered_map<std::string, const ast::ConceptDecl *> concept_registry_;
};

} // namespace kinglet
