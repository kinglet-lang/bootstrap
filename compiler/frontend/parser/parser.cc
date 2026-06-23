// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "frontend/parser/parser.h"
#include "frontend/parser/parser_internal.h"

#include <cctype>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace kinglet {

bool is_assignment_operator(TokenType type) {
  return type == TokenType::EQUAL || type == TokenType::PLUS_EQUAL ||
         type == TokenType::MINUS_EQUAL || type == TokenType::STAR_EQUAL ||
         type == TokenType::SLASH_EQUAL;
}

void skip_array_and_nullable_suffix(const std::vector<Token> &tokens, size_t &pos) {
  while (pos + 1 < tokens.size() && tokens[pos].type == TokenType::LEFT_BRACKET &&
         tokens[pos + 1].type == TokenType::RIGHT_BRACKET) {
    pos += 2;
  }
  if (pos < tokens.size() && tokens[pos].type == TokenType::QUESTION) {
    ++pos;
  }
}

ast::AssignOp token_to_assign_op(TokenType type) {
  switch (type) {
  case TokenType::EQUAL:
    return ast::AssignOp::Assign;
  case TokenType::PLUS_EQUAL:
    return ast::AssignOp::AddAssign;
  case TokenType::MINUS_EQUAL:
    return ast::AssignOp::SubAssign;
  case TokenType::STAR_EQUAL:
    return ast::AssignOp::MulAssign;
  case TokenType::SLASH_EQUAL:
    return ast::AssignOp::DivAssign;
  default:
    return ast::AssignOp::Assign;
  }
}

ast::BinaryOp token_to_binary_op(TokenType type) {
  switch (type) {
  case TokenType::PLUS:
    return ast::BinaryOp::Add;
  case TokenType::MINUS:
    return ast::BinaryOp::Sub;
  case TokenType::STAR:
    return ast::BinaryOp::Mul;
  case TokenType::SLASH:
    return ast::BinaryOp::Div;
  case TokenType::PERCENT:
    return ast::BinaryOp::Mod;
  case TokenType::EQUAL_EQUAL:
    return ast::BinaryOp::Eq;
  case TokenType::BANG_EQUAL:
    return ast::BinaryOp::Neq;
  case TokenType::LESS:
    return ast::BinaryOp::Lt;
  case TokenType::GREATER:
    return ast::BinaryOp::Gt;
  case TokenType::LESS_EQUAL:
    return ast::BinaryOp::Le;
  case TokenType::GREATER_EQUAL:
    return ast::BinaryOp::Ge;
  case TokenType::AMP_AMP:
    return ast::BinaryOp::And;
  case TokenType::PIPE_PIPE:
    return ast::BinaryOp::Or;
  case TokenType::AMP:
    return ast::BinaryOp::BitAnd;
  case TokenType::PIPE:
    return ast::BinaryOp::BitOr;
  case TokenType::CARET:
    return ast::BinaryOp::BitXor;
  case TokenType::LESS_LESS:
    return ast::BinaryOp::Shl;
  case TokenType::GREATER_GREATER:
    return ast::BinaryOp::Shr;
  default:
    return ast::BinaryOp::Add;
  }
}

ast::UnaryOp token_to_unary_op(TokenType type) {
  switch (type) {
  case TokenType::MINUS:
    return ast::UnaryOp::Neg;
  case TokenType::BANG:
    return ast::UnaryOp::Not;
  case TokenType::TILDE:
    return ast::UnaryOp::BitNot;
  default:
    return ast::UnaryOp::Neg;
  }
}

Parser::Parser(const std::vector<Token> &tokens) : tokens_(tokens) {}

Parser::Parser(const std::vector<Token> &tokens, std::size_t completion_index)
    : tokens_(tokens), completion_mode_(true), completion_index_(completion_index) {}

bool Parser::at_completion() const {
  return completion_mode_ && current_ == completion_index_;
}

bool Parser::completion_after_dangling_access() const {
  if (current_ == 0) return false;
  switch (previous().type) {
  case TokenType::DOT:
  case TokenType::COLON_COLON:
  case TokenType::COLON:
    return true;
  default:
    return false;
  }
}

void Parser::set_completion(lsp::CompletionInfo info) {
  completion_result_ = std::move(info);
}

std::string Parser::infer_receiver_type(const ast::Expr *expr) const {
  if (const auto *id = dynamic_cast<const ast::IdentifierExpr *>(expr)) {
    return id->name;
  }
  // io::out / io::err / io::in are built-in stream objects; expose them under a
  // synthetic type name the resolver recognises for member completion.
  if (const auto *ns = dynamic_cast<const ast::NamespaceAccessExpr *>(expr)) {
    if (ns->namespace_name == "io" &&
        (ns->member_name == "out" || ns->member_name == "err"))
      return "$io_ostream";
    if (ns->namespace_name == "io" && ns->member_name == "in")
      return "$io_istream";
  }
  // Encode an access chain so the resolver (which owns the symbol table) can
  // walk it to a concrete type. Segments are separated by '\x1f'; a method-call
  // segment is suffixed with "()". E.g. `r.scale(2).` -> "r\x1fscale()".
  if (const auto *field = dynamic_cast<const ast::FieldAccessExpr *>(expr)) {
    std::string base = infer_receiver_type(field->object.get());
    if (base.empty()) return {};
    return base + "\x1f" + field->field_name;
  }
  if (const auto *call = dynamic_cast<const ast::CallExpr *>(expr)) {
    if (const auto *callee =
            dynamic_cast<const ast::FieldAccessExpr *>(call->callee.get())) {
      std::string base = infer_receiver_type(callee->object.get());
      if (base.empty()) return {};
      return base + "\x1f" + callee->field_name + "()";
    }
  }
  if (const auto *pipe = dynamic_cast<const ast::PipeExpr *>(expr)) {
    return infer_receiver_type(pipe->right.get());
  }
  return {};
}

ParseResult Parser::parse() {
  std::vector<ast::DeclPtr> declarations;
  while (!is_at_end() && !has_completion()) {
    ast::DeclPtr decl = declaration();
    if (decl) {
      declarations.push_back(std::move(decl));
    }
  }

  return ParseResult{
      .program = std::make_unique<ast::Program>(std::move(declarations)),
      .errors = std::move(errors_),
  };
}

bool Parser::is_at_end() const {
  return peek().type == TokenType::END_OF_FILE;
}

bool Parser::check(TokenType type) const {
  if (type == TokenType::GREATER && pending_greater_) return true;
  return !is_at_end() && peek().type == type;
}

bool Parser::check_next(TokenType type) const {
  if (pending_greater_) {
    if (type == TokenType::GREATER) return true;
    return current_ < tokens_.size() && tokens_[current_].type == type;
  }
  return current_ + 1 < tokens_.size() && tokens_[current_ + 1].type == type;
}

bool Parser::check_after_next(TokenType type) const {
  return current_ + 2 < tokens_.size() && tokens_[current_ + 2].type == type;
}

bool Parser::match(TokenType type) {
  if (type == TokenType::GREATER && pending_greater_) {
    pending_greater_ = false;
    return true;
  }
  if (!check(type)) {
    return false;
  }
  advance();
  return true;
}

bool Parser::match_any(std::initializer_list<TokenType> types) {
  for (TokenType type : types) {
    if (match(type)) {
      return true;
    }
  }
  return false;
}

const Token &Parser::advance() {
  if (!is_at_end()) {
    ++current_;
  }
  return previous();
}

const Token &Parser::peek() const {
  return tokens_[current_];
}

const Token &Parser::previous() const {
  return tokens_[current_ - 1];
}

const Token &Parser::consume(TokenType type, std::string_view message) {
  if (check(type)) {
    return advance();
  }
  error_at(peek(), message);
  return peek();
}

bool Parser::is_type_start(TokenType type) const {
  switch (type) {
  case TokenType::AUTO:
  case TokenType::INT:
  case TokenType::INT8:
  case TokenType::INT16:
  case TokenType::INT32:
  case TokenType::INT64:
  case TokenType::UINT8:
  case TokenType::UINT16:
  case TokenType::UINT32:
  case TokenType::UINT64:
  case TokenType::FLOAT:
  case TokenType::FLOAT32:
  case TokenType::FLOAT64:
  case TokenType::DOUBLE:
  case TokenType::BOOL:
  case TokenType::STRING:
  case TokenType::VOID:
  case TokenType::BYTE:
  case TokenType::CHAR:
  case TokenType::IDENTIFIER:
  case TokenType::LEFT_BRACE: // map type {K: V}
  case TokenType::AMP:
    return true;
  default:
    return false;
  }
}

bool Parser::is_decl_keyword(TokenType type) const {
  switch (type) {
  case TokenType::STRUCT:
  case TokenType::ENUM:
  case TokenType::USING:
  case TokenType::IMPORT:
  case TokenType::PUB:
    return true;
  default:
    return false;
  }
}

bool Parser::is_declaration_start() const {
  if (check(TokenType::CONST)) {
    return true;
  }
  if (!is_type_start(peek().type)) return false;
  size_t pos = current_ + 1;
  if (pos < tokens_.size() && tokens_[pos].type == TokenType::LEFT_BRACKET &&
      peek().type == TokenType::AUTO) {
    return true;
  }
  // Skip balanced <...> to find the identifier after the type
  if (pos < tokens_.size() && tokens_[pos].type == TokenType::LESS) {
    int depth = 1;
    ++pos;
    while (pos < tokens_.size() && depth > 0) {
      if (tokens_[pos].type == TokenType::LESS) ++depth;
      else if (tokens_[pos].type == TokenType::GREATER) --depth;
      else if (tokens_[pos].type == TokenType::GREATER_GREATER) depth -= 2;
      ++pos;
    }
  }
  skip_array_and_nullable_suffix(tokens_, pos);
  return pos < tokens_.size() && tokens_[pos].type == TokenType::IDENTIFIER;
}

bool Parser::looks_like_map_var_decl() const {
  // Expect current token to be '{'. Scan to the matching '}', requiring a
  // depth-1 ':' (the K:V separator), then an identifier (the variable name)
  // immediately after. A block body never has that shape.
  if (!check(TokenType::LEFT_BRACE)) return false;
  size_t pos = current_ + 1;
  int depth = 1;
  bool saw_top_level_colon = false;
  while (pos < tokens_.size() && depth > 0) {
    TokenType t = tokens_[pos].type;
    if (t == TokenType::LEFT_BRACE) {
      ++depth;
    } else if (t == TokenType::RIGHT_BRACE) {
      --depth;
    } else if (t == TokenType::COLON && depth == 1) {
      saw_top_level_colon = true;
    } else if (t == TokenType::SEMICOLON && depth == 1) {
      // A ';' at brace depth 1 means this is a block of statements, not a type.
      return false;
    } else if (t == TokenType::END_OF_FILE) {
      return false;
    }
    ++pos;
  }
  if (!saw_top_level_colon) return false;
  skip_array_and_nullable_suffix(tokens_, pos);
  return pos < tokens_.size() && tokens_[pos].type == TokenType::IDENTIFIER;
}

bool Parser::is_function_declaration_start() const {
  if (!is_type_start(peek().type)) return false;
  // Skip balanced <...> after the type name to find IDENTIFIER then '(' or '<'
  size_t pos = current_ + 1;
  if (pos < tokens_.size() && tokens_[pos].type == TokenType::LESS) {
    int depth = 1;
    ++pos;
    while (pos < tokens_.size() && depth > 0) {
      if (tokens_[pos].type == TokenType::LESS) ++depth;
      else if (tokens_[pos].type == TokenType::GREATER) --depth;
      else if (tokens_[pos].type == TokenType::GREATER_GREATER) depth -= 2;
      ++pos;
    }
  }
  skip_array_and_nullable_suffix(tokens_, pos);
  if (pos >= tokens_.size() || tokens_[pos].type != TokenType::IDENTIFIER) return false;
  ++pos; // skip function name
  // Function may have type params: name<T>(...)
  if (pos < tokens_.size() && tokens_[pos].type == TokenType::LESS) {
    int depth = 1;
    ++pos;
    while (pos < tokens_.size() && depth > 0) {
      if (tokens_[pos].type == TokenType::LESS) ++depth;
      else if (tokens_[pos].type == TokenType::GREATER) --depth;
      else if (tokens_[pos].type == TokenType::GREATER_GREATER) depth -= 2;
      ++pos;
    }
  }
  return pos < tokens_.size() && tokens_[pos].type == TokenType::LEFT_PAREN;
}

ast::SourceLocation Parser::location_of(const Token &token) const {
  return ast::SourceLocation{
      .line = token.line,
      .column = token.column,
      .length = static_cast<int>(token.lexeme.size()),
  };
}

std::string Parser::token_text(const Token &token) const {
  return std::string(token.lexeme);
}

void Parser::synchronize() {
  if (at_completion()) {
    set_completion({lsp::CompletionPosition::Statement, {}, {}, {}, {}, {}});
    return;
  }
  advance();
  while (!is_at_end() && !has_completion()) {
    if (previous().type == TokenType::SEMICOLON) {
      return;
    }
    if (at_completion()) {
      set_completion({lsp::CompletionPosition::Statement, {}, {}, {}, {}, {}});
      return;
    }
    switch (peek().type) {
    case TokenType::CONST:
    case TokenType::RETURN:
    case TokenType::IF:
    case TokenType::FOR:
    case TokenType::BREAK:
    case TokenType::CONTINUE:
    case TokenType::WHILE:
      return;
    default:
      break;
    }
    advance();
  }
}

void Parser::error_at(const Token &token, std::string_view message) {
  errors_.push_back(ParseError{
      .line = token.line,
      .column = token.column,
      .message = std::string(message),
  });
}

} // namespace kinglet
