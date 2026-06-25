// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kinglet::lsp {

enum class CompletionPosition : std::uint8_t {
  None,
  TopLevelDecl,
  TypeExpr,
  ExpressionStart,
  Statement,
  StructFieldDecl,
  EnumVariant,
  ConceptMethodDecl,
  FieldAccess,
  NamespaceAccess,
  ImportPath,
  ImportSymbol,
  MatchArm,
  StructLiteral,
  ParameterType,
  UsingNamespace,
};

struct CompletionInfo {
  CompletionPosition position;
  std::string enclosing_type;
  std::string receiver_type;
  std::string ns_name;
  std::string import_path;
  std::string struct_name;
  // Generic type parameters in scope at the cursor (e.g. concept/struct
  // <T, U>), so type-position completion can offer them.
  std::vector<std::string> type_params;
};

} // namespace kinglet::lsp
