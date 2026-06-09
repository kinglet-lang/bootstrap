#pragma once

#include "ast/ast.h"
#include "ir/kir.h"

#include <optional>
#include <string>

namespace kinglet {

// Builds structured KIR from a narrow AST subset (literals and int arithmetic).
// The full compiler still emits bytecode directly; this module is the KIR
// reference builder for --ir and golden tests (ADR 0005 M1).
class IrBuilder {
public:
  std::optional<KirFunction> build_expr_function(const std::string &name,
                                                 const ast::Expr &expr) const;

private:
  struct BuildResult {
    bool ok = false;
    KirFunction function;
    int value_id = -1;
  };

  bool build_expr_into(KirFunction *fn, KirBasicBlock *bb, const ast::Expr &expr,
                       int *out_value) const;
};

} // namespace kinglet
