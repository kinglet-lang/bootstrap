#pragma once

#include <string>

namespace kinglet::lsp {

enum class CompletionPosition {
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
};

} // namespace kinglet::lsp
