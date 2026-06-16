#pragma once

#include "ast/ast.h"
#include "preen/config.h"
#include "preen/trivia.h"

#include <string>

namespace kinglet::preen {

class Emitter {
public:
  Emitter(const FmtConfig &config, const TriviaMap *trivia);

  std::string emit_program(const ast::Program &program);

private:
  const FmtConfig &config_;
  const TriviaMap *trivia_;
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

  void emit_top_level_decl(const ast::Decl &decl);
};

} // namespace kinglet::preen
