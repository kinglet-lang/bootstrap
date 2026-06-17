// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "compiler/dense_array_lit.h"

namespace kinglet {

namespace {

bool is_scalar_literal(const ast::Expr &expr) {
  return dynamic_cast<const ast::IntLiteralExpr *>(&expr) != nullptr ||
         dynamic_cast<const ast::FloatLiteralExpr *>(&expr) != nullptr ||
         dynamic_cast<const ast::BoolLiteralExpr *>(&expr) != nullptr ||
         dynamic_cast<const ast::CharLiteralExpr *>(&expr) != nullptr ||
         dynamic_cast<const ast::NullLiteralExpr *>(&expr) != nullptr ||
         dynamic_cast<const ast::StringLiteralExpr *>(&expr) != nullptr;
}

} // namespace

bool analyze_dense_array_literal_2d(const ast::ArrayLiteralExpr &lit, DenseArrayLit2D *out) {
  if (out == nullptr || lit.elements.empty()) {
    return false;
  }
  int cols = -1;
  for (const ast::ExprPtr &row_expr : lit.elements) {
    const auto *row_lit = dynamic_cast<const ast::ArrayLiteralExpr *>(row_expr.get());
    if (row_lit == nullptr || row_lit->elements.empty()) {
      return false;
    }
    const int row_len = static_cast<int>(row_lit->elements.size());
    if (cols < 0) {
      cols = row_len;
    } else if (cols != row_len) {
      return false;
    }
    for (const ast::ExprPtr &cell : row_lit->elements) {
      if (!is_scalar_literal(*cell)) {
        return false;
      }
    }
  }
  out->rows = static_cast<int>(lit.elements.size());
  out->cols = cols;
  return out->rows > 0 && out->cols > 0;
}

} // namespace kinglet
