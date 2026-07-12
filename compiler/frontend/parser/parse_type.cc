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

std::string Parser::parse_module_id(const char *context) {
  const Token &first = consume(TokenType::IDENTIFIER, std::string("Expected ") + context + ".");
  std::string module_id(token_text(first));
  while (match(TokenType::DOT)) {
    const Token &part =
        consume(TokenType::IDENTIFIER, "Expected identifier after '.' in module name.");
    module_id.push_back('.');
    module_id += token_text(part);
  }
  return module_id;
}

ast::ExprPtr Parser::parse_namespace_access(const Token &first, std::vector<std::string> segments) {
  if (segments.size() < 2) {
    return std::make_unique<ast::IdentifierExpr>(location_of(first), segments.front());
  }
  std::string qualifier;
  for (size_t i = 0; i + 1 < segments.size(); ++i) {
    if (i > 0) {
      qualifier += "::";
    }
    qualifier += segments[i];
  }
  return std::make_unique<ast::NamespaceAccessExpr>(location_of(first), qualifier, segments.back());
}

std::vector<ast::Parameter> Parser::parameters() {
  std::vector<ast::Parameter> params;
  if (check(TokenType::RIGHT_PAREN)) {
    return params;
  }

  do {
    if (at_completion()) {
      set_completion(
          {lsp::CompletionPosition::ParameterType, {}, {}, {}, {}, {}, active_type_params_});
      return params;
    }
    ast::TypeExpr type = parse_type_expr();
    if (has_completion())
      return params;
    if (at_completion()) {
      // Cursor sits at the parameter *name* position (e.g. `auto █`). The
      // name is a fresh identifier the user is about to type, not a
      // reference to anything already in scope, so there is nothing
      // meaningful to suggest here. Without this guard the completion
      // token is never claimed, parsing falls through to the failing
      // consume() below, and error recovery discards the whole function
      // declaration — surfacing unrelated top-level noise (keywords, the
      // function's own not-yet-parsed name, etc.) instead of an empty list.
      set_completion({lsp::CompletionPosition::None, {}, {}, {}, {}, {}});
      return params;
    }
    const Token &name = consume(TokenType::IDENTIFIER, "Expected parameter name.");
    params.push_back(ast::Parameter{std::move(type), token_text(name)});
  } while (match(TokenType::COMMA));

  return params;
}

ast::StmtPtr Parser::function_body() {
  if (match(TokenType::LEFT_BRACE)) {
    return block_statement();
  }
  if (match(TokenType::FAT_ARROW)) {
    if (match(TokenType::LEFT_BRACE)) {
      return block_statement();
    }
    ast::ExprPtr value = expression();
    if (has_completion())
      return nullptr;
    consume(TokenType::SEMICOLON, "Expected ';' after expression body.");
    if (!value) {
      return std::make_unique<ast::ReturnStmt>(location_of(previous()), nullptr);
    }
    const ast::SourceLocation location = value->location;
    return std::make_unique<ast::ReturnStmt>(location, std::move(value));
  }

  error_at(peek(), "Expected function body.");
  return std::make_unique<ast::BlockStmt>(location_of(peek()), std::vector<ast::StmtPtr>{});
}

ast::TypeExpr Parser::parse_type_expr() {
  // Depth guard: type syntax recurses through generic arguments
  // (List<List<...>>) and map types ({K: V}). Deep nesting would otherwise
  // overflow the native stack.
  RecursionGuard guard(*this);
  if (!guard.ok()) {
    note_recursion_limit();
    return ast::TypeExpr{"<error>", {}};
  }
  if (at_completion()) {
    set_completion({lsp::CompletionPosition::TypeExpr, {}, {}, {}, {}, {}, active_type_params_});
    return ast::TypeExpr{"<error>", {}};
  }
  // `const` here belongs to the type itself, not variable storage: it marks
  // a shared borrow (`const T&`) as opposed to an exclusive one (`T&`).
  // Callers that need to disambiguate `const` at the *start* of a variable
  // declaration (where it could instead mean "non-reassignable binding")
  // use const_prefix_is_reference_type() before reaching here; by the time
  // parse_type_expr() itself sees CONST, it is always the type-level shared
  // borrow marker (ADR 0028 D3).
  const bool shared = match(TokenType::CONST);
  if (!is_type_start(peek().type)) {
    error_at(peek(), "Expected type name.");
    return ast::TypeExpr{"<error>", {}};
  }
  ast::TypeExpr result;
  // Map type: {K: V} — encoded as Map<K, V> (name="Map", type_args=[K, V]).
  if (check(TokenType::LEFT_BRACE)) {
    advance(); // consume '{'
    ast::TypeExpr key = parse_type_expr();
    consume(TokenType::COLON, "Expected ':' between map key and value types.");
    ast::TypeExpr value = parse_type_expr();
    consume(TokenType::RIGHT_BRACE, "Expected '}' after map value type.");
    std::vector<ast::TypeExpr> map_args;
    map_args.push_back(std::move(key));
    map_args.push_back(std::move(value));
    std::string name = "Map";
    std::vector<ast::TypeExpr> type_args = std::move(map_args);
    while (match(TokenType::LEFT_BRACKET)) {
      consume(TokenType::RIGHT_BRACKET, "Expected ']' after array type suffix.");
      std::vector<ast::TypeExpr> array_arg;
      array_arg.push_back(ast::TypeExpr{std::move(name), std::move(type_args)});
      name = "Array";
      type_args = std::move(array_arg);
    }
    result = ast::TypeExpr{std::move(name), std::move(type_args)};
  } else {
    std::string name = token_text(advance());
    while (match(TokenType::COLON_COLON)) {
      const Token &part =
          consume(TokenType::IDENTIFIER, "Expected identifier after '::' in type name.");
      name += "::";
      name += token_text(part);
    }
    std::vector<ast::TypeExpr> type_args;
    if (check(TokenType::LESS) && !pending_greater_) {
      advance(); // consume '<'
      do {
        type_args.push_back(parse_type_expr());
      } while (match(TokenType::COMMA));
      if (peek().type == TokenType::GREATER_GREATER && !pending_greater_) {
        advance(); // consume '>>'
        pending_greater_ = true;
      } else {
        if (!match(TokenType::GREATER)) {
          error_at(peek(), "Expected '>' after type arguments.");
        }
      }
    }
    while (match(TokenType::LEFT_BRACKET)) {
      consume(TokenType::RIGHT_BRACKET, "Expected ']' after array type suffix.");
      std::vector<ast::TypeExpr> array_arg;
      array_arg.push_back(ast::TypeExpr{std::move(name), std::move(type_args)});
      name = "Array";
      type_args = std::move(array_arg);
    }
    result = ast::TypeExpr{std::move(name), std::move(type_args)};
  }
  if (check(TokenType::QUESTION)) {
    advance();
    std::vector<ast::TypeExpr> inner;
    inner.push_back(std::move(result));
    result = ast::TypeExpr{"Nullable", std::move(inner)};
  }
  // Postfix reference marker: `T&` (exclusive) / `const T&` (shared,
  // `shared` captured above). ADR 0028 D3 replaces 0022's prefix `&T` /
  // `&mut T` with this postfix form; `mut` is gone entirely.
  if (match(TokenType::AMP)) {
    std::vector<ast::TypeExpr> inner;
    inner.push_back(std::move(result));
    return ast::TypeExpr{shared ? "&" : "&mut", std::move(inner)};
  }
  if (shared) {
    error_at(peek(), "Expected '&' after 'const' in type position.");
  }
  return result;
}

} // namespace kinglet
