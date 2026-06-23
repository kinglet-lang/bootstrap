// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "frontend/ast/ast.h"
#include "ir/kir.h"

#include <optional>
#include <string>

namespace kinglet {

// Builds structured KIR from a narrow AST subset (literals and int arithmetic).
// Used as a fast path for simple single-expression functions, bypassing the
// full OpCode → KirRecorder → KirModule pipeline.
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
