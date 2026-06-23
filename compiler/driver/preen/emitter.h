// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "frontend/ast/ast.h"
#include "frontend/lexer/token.h"
#include "driver/preen/config.h"
#include "driver/preen/span_map.h"
#include "driver/preen/token_trivia.h"
#include "driver/preen/trivia.h"

#include <string>
#include <string_view>
#include <vector>

namespace kinglet::preen {

class Emitter {
public:
  Emitter(const FmtConfig &config, const TriviaMap *decl_trivia,
          const TokenTriviaIndex *token_trivia, const SpanMap *spans,
          const std::vector<Token> *tokens);

  std::string emit_program(const ast::Program &program);

private:
  const FmtConfig &config_;
  const TriviaMap *decl_trivia_;
  const TokenTriviaIndex *token_trivia_;
  const SpanMap *spans_;
  const std::vector<Token> *tokens_;
  std::string out_;
  int indent_level_ = 0;

  std::string indent_str(int extra = 0) const;
  void append(const std::string &text);
  void append_line(const std::string &text);
  void append_leading_trivia(int line);
  void newline();

  std::string emit_type(const ast::TypeExpr &type);
  std::string emit_expr(const ast::Expr &expr);
  std::string emit_stmt(const ast::Stmt &stmt, bool block_child = false);
  std::string emit_decl(const ast::Decl &decl, bool top_level = false);
  std::string emit_block_body(const ast::BlockStmt &block);
  std::string emit_parameters(const std::vector<ast::Parameter> &params);
  std::string emit_type_params(const std::vector<std::string> &params);
  std::string emit_string_literal(const std::string &value);
  std::string emit_char_literal(int8_t value);
  std::string stmt_trailing(const ast::Stmt &stmt) const;
  bool is_single_line_block(const ast::BlockStmt &block, int line) const;

  void emit_top_level_decl(const ast::Decl &decl);

  std::string leading_token(std::size_t index) const;
  std::string trailing_token(std::size_t index) const;
  std::string gap_between(std::size_t from_after, std::size_t before,
                          std::string_view canonical_if_blank) const;
  std::size_t find_token_between(std::size_t start, std::size_t end, TokenType type) const;
  bool is_blank(const std::string &text) const;
};

} // namespace kinglet::preen
