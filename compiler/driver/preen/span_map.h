// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "frontend/ast/ast.h"
#include "frontend/lexer/token.h"

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace kinglet::preen {

struct TokenSpan {
  std::size_t start = 0;
  std::size_t end = 0; // exclusive
};

class SpanMap {
public:
  SpanMap(const ast::Program &program, const std::vector<Token> &tokens);

  bool has(const ast::Node &node) const;
  TokenSpan span(const ast::Node &node) const;

private:
  std::size_t locate_token(int line, int column) const;
  void build_expr(const ast::Expr &expr);
  void build_stmt(const ast::Stmt &stmt);
  void build_decl(const ast::Decl &decl);

  const std::vector<Token> &tokens_;
  std::unordered_map<const ast::Node *, TokenSpan> spans_;
};

} // namespace kinglet::preen
