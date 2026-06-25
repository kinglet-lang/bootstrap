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
    const Token &part = consume(TokenType::IDENTIFIER, "Expected identifier after '.' in module name.");
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
      set_completion({lsp::CompletionPosition::ParameterType, {}, {}, {}, {}, {},
                      active_type_params_});
      return params;
    }
    ast::TypeExpr type = parse_type_expr();
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
    if (has_completion()) return nullptr;
    consume(TokenType::SEMICOLON, "Expected ';' after expression body.");
    const ast::SourceLocation location = value->location;
    return std::make_unique<ast::ReturnStmt>(location, std::move(value));
  }

  error_at(peek(), "Expected function body.");
  return std::make_unique<ast::BlockStmt>(location_of(peek()), std::vector<ast::StmtPtr>{});
}


ast::TypeExpr Parser::parse_type_expr() {
  if (at_completion()) {
    set_completion({lsp::CompletionPosition::TypeExpr, {}, {}, {}, {}, {},
                    active_type_params_});
    return ast::TypeExpr{"<error>", {}};
  }
  if (match(TokenType::AMP)) {
    const bool mut = check(TokenType::IDENTIFIER) && token_text(peek()) == "mut";
    if (mut) {
      advance();
    }
    ast::TypeExpr inner = parse_type_expr();
    if (mut) {
      return ast::TypeExpr{"&mut", {std::move(inner)}};
    }
    return ast::TypeExpr{"&", {std::move(inner)}};
  }
  if (!is_type_start(peek().type)) {
    error_at(peek(), "Expected type name.");
    return ast::TypeExpr{"<error>", {}};
  }
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
    ast::TypeExpr result{std::move(name), std::move(type_args)};
    if (check(TokenType::QUESTION)) {
      advance();
      std::vector<ast::TypeExpr> inner;
      inner.push_back(std::move(result));
      return ast::TypeExpr{"Nullable", std::move(inner)};
    }
    return result;
  }
  std::string name = token_text(advance());
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
  ast::TypeExpr result{std::move(name), std::move(type_args)};
  if (check(TokenType::QUESTION)) {
    advance();
    std::vector<ast::TypeExpr> inner;
    inner.push_back(std::move(result));
    return ast::TypeExpr{"Nullable", std::move(inner)};
  }
  return result;
}


} // namespace kinglet
