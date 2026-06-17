// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "ast/ast.h"
#include "vm/chunk.h"

namespace kinglet {

struct DenseArrayLit2D {
  int rows = 0;
  int cols = 0;
};

// True when `lit` is [[scalar...], ...] with every row the same length (v1: 2D only).
bool analyze_dense_array_literal_2d(const ast::ArrayLiteralExpr &lit, DenseArrayLit2D *out);

} // namespace kinglet
