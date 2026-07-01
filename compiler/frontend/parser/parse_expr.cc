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

ast::ExprPtr Parser::expression() {
  return assignment();
}

ast::ExprPtr Parser::ternary() {
  ast::ExprPtr expr = coalesce();
  if (match(TokenType::QUESTION)) {
    auto loc = location_of(previous());
    ast::ExprPtr then_expr = expression();
    consume(TokenType::COLON, "Expected ':' in ternary expression.");
    ast::ExprPtr else_expr = ternary(); // right-associative
    return std::make_unique<ast::TernaryExpr>(loc, std::move(expr), std::move(then_expr),
                                              std::move(else_expr));
  }
  return expr;
}

ast::ExprPtr Parser::assignment() {
  ast::ExprPtr expr = ternary();
  if (has_completion())
    return expr;
  if (is_assignment_operator(peek().type)) {
    const Token &op = advance();
    ast::ExprPtr value = assignment();
    auto *identifier = dynamic_cast<ast::IdentifierExpr *>(expr.get());
    if (identifier != nullptr) {
      const ast::SourceLocation location = expr->location;
      return std::make_unique<ast::AssignExpr>(location, identifier->name,
                                               token_to_assign_op(op.type), std::move(value));
    }
    auto *index_expr = dynamic_cast<ast::IndexExpr *>(expr.get());
    if (index_expr != nullptr) {
      if (op.type != TokenType::EQUAL) {
        error_at(op, "Compound assignment to indexed values is not supported.");
        return value;
      }
      const ast::SourceLocation location = expr->location;
      return std::make_unique<ast::IndexAssignExpr>(location, std::move(index_expr->object),
                                                    std::move(index_expr->index), std::move(value));
    }
    auto *field_access = dynamic_cast<ast::FieldAccessExpr *>(expr.get());
    if (field_access != nullptr) {
      const ast::SourceLocation location = expr->location;
      return std::make_unique<ast::FieldAssignExpr>(location, std::move(field_access->object),
                                                    field_access->field_name, std::move(value));
    }
    error_at(op, "Invalid assignment target.");
    return value;
  }
  return expr;
}

ast::ExprPtr Parser::pipeline() {
  ast::ExprPtr expr = logical_or();
  while (match(TokenType::PIPE_GREATER)) {
    ast::ExprPtr func = logical_or();
    expr =
        std::make_unique<ast::PipeExpr>(location_of(previous()), std::move(expr), std::move(func));
  }
  return expr;
}

ast::ExprPtr Parser::coalesce() {
  ast::ExprPtr expr = pipeline();
  while (match(TokenType::QUESTION_COLON)) {
    auto loc = location_of(previous());
    std::string err_binding;
    if (check(TokenType::LET) && current_ + 1 < tokens_.size() &&
        tokens_[current_ + 1].type == TokenType::IDENTIFIER && current_ + 2 < tokens_.size() &&
        tokens_[current_ + 2].type == TokenType::FAT_ARROW) {
      advance(); // consume `let`
      const Token &name_tok = consume(TokenType::IDENTIFIER, "Expected error binding name.");
      err_binding = token_text(name_tok);
      consume(TokenType::FAT_ARROW, "Expected '=>' after error binding name.");
    }
    ast::ExprPtr rhs = coalesce();
    expr = std::make_unique<ast::NullCoalesceExpr>(loc, std::move(expr), std::move(err_binding),
                                                   std::move(rhs));
  }
  return expr;
}

ast::ExprPtr Parser::logical_or() {
  ast::ExprPtr expr = logical_and();
  while (match(TokenType::PIPE_PIPE)) {
    const Token &op = previous();
    ast::ExprPtr right = logical_and();
    expr = std::make_unique<ast::BinaryExpr>(location_of(op), std::move(expr),
                                             token_to_binary_op(op.type), std::move(right));
  }
  return expr;
}

ast::ExprPtr Parser::logical_and() {
  ast::ExprPtr expr = bit_or();
  while (match(TokenType::AMP_AMP)) {
    const Token &op = previous();
    ast::ExprPtr right = bit_or();
    expr = std::make_unique<ast::BinaryExpr>(location_of(op), std::move(expr),
                                             token_to_binary_op(op.type), std::move(right));
  }
  return expr;
}

ast::ExprPtr Parser::bit_or() {
  ast::ExprPtr expr = bit_xor();
  while (match(TokenType::PIPE)) {
    const Token &op = previous();
    ast::ExprPtr right = bit_xor();
    expr = std::make_unique<ast::BinaryExpr>(location_of(op), std::move(expr),
                                             token_to_binary_op(op.type), std::move(right));
  }
  return expr;
}

ast::ExprPtr Parser::bit_xor() {
  ast::ExprPtr expr = bit_and();
  while (match(TokenType::CARET)) {
    const Token &op = previous();
    ast::ExprPtr right = bit_and();
    expr = std::make_unique<ast::BinaryExpr>(location_of(op), std::move(expr),
                                             token_to_binary_op(op.type), std::move(right));
  }
  return expr;
}

ast::ExprPtr Parser::bit_and() {
  ast::ExprPtr expr = equality();
  while (match(TokenType::AMP)) {
    const Token &op = previous();
    ast::ExprPtr right = equality();
    expr = std::make_unique<ast::BinaryExpr>(location_of(op), std::move(expr),
                                             token_to_binary_op(op.type), std::move(right));
  }
  return expr;
}

ast::ExprPtr Parser::equality() {
  ast::ExprPtr expr = comparison();
  while (match_any({TokenType::EQUAL_EQUAL, TokenType::BANG_EQUAL})) {
    const Token &op = previous();
    ast::ExprPtr right = comparison();
    expr = std::make_unique<ast::BinaryExpr>(location_of(op), std::move(expr),
                                             token_to_binary_op(op.type), std::move(right));
  }
  return expr;
}

ast::ExprPtr Parser::comparison() {
  ast::ExprPtr expr = shift();

  auto is_comparison_op = [this]() {
    return check(TokenType::LESS) || check(TokenType::LESS_EQUAL) || check(TokenType::GREATER) ||
           check(TokenType::GREATER_EQUAL);
  };

  if (!is_comparison_op()) {
    return expr;
  }

  advance();
  auto op1_loc = location_of(previous());
  ast::BinaryOp bin_op1 = token_to_binary_op(previous().type);

  std::size_t mid_start = current_;
  ast::ExprPtr mid = shift();

  if (!is_comparison_op()) {
    return std::make_unique<ast::BinaryExpr>(op1_loc, std::move(expr), bin_op1, std::move(mid));
  }

  std::size_t saved = current_;
  current_ = mid_start;
  ast::ExprPtr mid_copy = shift();
  current_ = saved;

  ast::ExprPtr left_cmp =
      std::make_unique<ast::BinaryExpr>(op1_loc, std::move(expr), bin_op1, std::move(mid));

  advance();
  auto op2_loc = location_of(previous());
  ast::BinaryOp bin_op2 = token_to_binary_op(previous().type);
  ast::ExprPtr right_term = shift();

  ast::ExprPtr right_cmp = std::make_unique<ast::BinaryExpr>(op2_loc, std::move(mid_copy), bin_op2,
                                                             std::move(right_term));

  return std::make_unique<ast::BinaryExpr>(op1_loc, std::move(left_cmp), ast::BinaryOp::And,
                                           std::move(right_cmp));
}

ast::ExprPtr Parser::shift() {
  ast::ExprPtr expr = term();
  while (match_any({TokenType::LESS_LESS, TokenType::GREATER_GREATER})) {
    const Token &op = previous();
    ast::ExprPtr right = term();
    expr = std::make_unique<ast::BinaryExpr>(location_of(op), std::move(expr),
                                             token_to_binary_op(op.type), std::move(right));
  }
  return expr;
}

ast::ExprPtr Parser::term() {
  ast::ExprPtr expr = factor();
  while (match_any({TokenType::PLUS, TokenType::MINUS})) {
    const Token &op = previous();
    ast::ExprPtr right = factor();
    expr = std::make_unique<ast::BinaryExpr>(location_of(op), std::move(expr),
                                             token_to_binary_op(op.type), std::move(right));
  }
  return expr;
}

ast::ExprPtr Parser::factor() {
  ast::ExprPtr expr = unary();
  while (match_any({TokenType::STAR, TokenType::SLASH, TokenType::PERCENT})) {
    const Token &op = previous();
    ast::ExprPtr right = unary();
    expr = std::make_unique<ast::BinaryExpr>(location_of(op), std::move(expr),
                                             token_to_binary_op(op.type), std::move(right));
  }
  return expr;
}

ast::ExprPtr Parser::unary() {
  if (match(TokenType::AMP)) {
    const Token &amp = previous();
    const bool mut = check(TokenType::IDENTIFIER) && token_text(peek()) == "mut";
    if (mut) {
      advance();
    }
    ast::ExprPtr inner = unary();
    return std::make_unique<ast::UnaryExpr>(
        location_of(amp), mut ? ast::UnaryOp::MutRef : ast::UnaryOp::Ref, std::move(inner));
  }
  if (match_any({TokenType::BANG, TokenType::MINUS, TokenType::TILDE})) {
    const Token &op = previous();
    ast::ExprPtr right = unary();
    return std::make_unique<ast::UnaryExpr>(location_of(op), token_to_unary_op(op.type),
                                            std::move(right));
  }
  return call();
}

ast::ExprPtr Parser::call() {
  ast::ExprPtr expr = primary();
  if (has_completion())
    return expr;
  while (true) {
    if (check(TokenType::LESS) && dynamic_cast<const ast::IdentifierExpr *>(expr.get())) {
      size_t saved = current_;
      size_t pos = current_ + 1;
      int depth = 1;
      bool valid = false;
      while (pos < tokens_.size() && depth > 0) {
        if (tokens_[pos].type == TokenType::LESS)
          ++depth;
        else if (tokens_[pos].type == TokenType::GREATER)
          --depth;
        else if (tokens_[pos].type == TokenType::GREATER_GREATER)
          depth -= 2;
        else if (tokens_[pos].type == TokenType::SEMICOLON ||
                 tokens_[pos].type == TokenType::END_OF_FILE)
          break;
        ++pos;
      }
      if (depth == 0 && pos < tokens_.size() && tokens_[pos].type == TokenType::LEFT_PAREN) {
        advance();
        std::vector<ast::TypeExpr> type_args;
        do {
          type_args.push_back(parse_type_expr());
        } while (match(TokenType::COMMA));
        if (pending_greater_) {
          pending_greater_ = false;
        } else {
          consume(TokenType::GREATER, "Expected '>' after type arguments.");
        }
        consume(TokenType::LEFT_PAREN, "Expected '(' after type arguments.");
        std::vector<ast::ExprPtr> args;
        if (!check(TokenType::RIGHT_PAREN)) {
          do {
            args.push_back(expression());
          } while (match(TokenType::COMMA));
        }
        consume(TokenType::RIGHT_PAREN, "Expected ')' after arguments.");
        const ast::SourceLocation location = expr->location;
        expr = std::make_unique<ast::CallExpr>(location, std::move(expr), std::move(type_args),
                                               std::move(args));
        valid = true;
      }
      if (!valid) {
        current_ = saved;
      }
      if (valid)
        continue;
    }
    if (match(TokenType::LEFT_PAREN)) {
      std::vector<ast::ExprPtr> args;
      if (!check(TokenType::RIGHT_PAREN)) {
        do {
          args.push_back(expression());
        } while (match(TokenType::COMMA));
      }
      consume(TokenType::RIGHT_PAREN, "Expected ')' after arguments.");
      const ast::SourceLocation location = expr->location;
      expr = std::make_unique<ast::CallExpr>(location, std::move(expr),
                                             std::vector<ast::TypeExpr>{}, std::move(args));
    } else if (match(TokenType::DOT)) {
      if (at_completion()) {
        std::string receiver = infer_receiver_type(expr.get());
        set_completion({lsp::CompletionPosition::FieldAccess, {}, receiver, {}, {}, {}});
        return expr;
      }
      const Token &field = consume(TokenType::IDENTIFIER, "Expected field name after '.'.");
      const ast::SourceLocation location = expr->location;
      expr = std::make_unique<ast::FieldAccessExpr>(location, std::move(expr), token_text(field));
    } else if (match(TokenType::LEFT_BRACKET)) {
      ast::ExprPtr index = expression();
      consume(TokenType::RIGHT_BRACKET, "Expected ']' after index.");
      const ast::SourceLocation location = expr->location;
      expr = std::make_unique<ast::IndexExpr>(location, std::move(expr), std::move(index));
    } else if (match(TokenType::MATCH)) {
      expr = match_expression(std::move(expr));
    } else if (check(TokenType::QUESTION)) {
      // Disambiguate postfix `?` (propagate) from ternary `? :`.
      // If the token after `?` can start an expression, leave `?` for the
      // ternary parser at a higher precedence level; otherwise consume as
      // postfix propagate.
      if (current_ + 1 < tokens_.size()) {
        switch (tokens_[current_ + 1].type) {
        case TokenType::IDENTIFIER:
        case TokenType::INTEGER:
        case TokenType::FLOAT_LIT:
        case TokenType::STRING_LIT:
        case TokenType::CHAR_LIT:
        case TokenType::TRUE:
        case TokenType::FALSE:
        case TokenType::NULL_:
        case TokenType::LEFT_PAREN:
        case TokenType::LEFT_BRACKET:
        case TokenType::LEFT_BRACE:
        case TokenType::MINUS:
        case TokenType::BANG:
        case TokenType::TILDE:
        case TokenType::INT:
        case TokenType::FLOAT:
        case TokenType::DOUBLE:
        case TokenType::BOOL:
        case TokenType::STRING:
        case TokenType::VOID:
        case TokenType::CHAR:
        case TokenType::BYTE:
          break; // ternary — don't consume `?`
        default:
          advance(); // consume `?` as propagate
          const ast::SourceLocation location = location_of(previous());
          expr = std::make_unique<ast::PropagateExpr>(location, std::move(expr));
          continue;
        }
      }
      break;
    } else {
      break;
    }
  }
  return expr;
}

ast::ExprPtr Parser::primary() {
  if (at_completion()) {
    set_completion({lsp::CompletionPosition::ExpressionStart, {}, {}, {}, {}, {}});
    return nullptr;
  }
  if (match(TokenType::INTEGER)) {
    const Token &literal = previous();
    return std::make_unique<ast::IntLiteralExpr>(location_of(literal), literal.int_value,
                                                 literal.suffix);
  }
  if (match(TokenType::FLOAT_LIT)) {
    const Token &literal = previous();
    return std::make_unique<ast::FloatLiteralExpr>(location_of(literal), literal.float_value,
                                                   literal.suffix);
  }
  if (match(TokenType::STRING_LIT)) {
    const Token &literal = previous();
    std::string text = token_text(literal);
    // Strip surrounding quotes
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
      text = text.substr(1, text.size() - 2);
    }
    return std::make_unique<ast::StringLiteralExpr>(location_of(literal), std::move(text));
  }
  if (match(TokenType::CHAR_LIT)) {
    const Token &literal = previous();
    return std::make_unique<ast::CharLiteralExpr>(location_of(literal),
                                                  static_cast<int8_t>(literal.int_value));
  }
  if (match(TokenType::TRUE)) {
    const Token &literal = previous();
    return std::make_unique<ast::BoolLiteralExpr>(location_of(literal), true);
  }
  if (match(TokenType::FALSE)) {
    const Token &literal = previous();
    return std::make_unique<ast::BoolLiteralExpr>(location_of(literal), false);
  }
  if (match(TokenType::NULL_)) {
    const Token &literal = previous();
    return std::make_unique<ast::NullLiteralExpr>(location_of(literal));
  }
  if (match(TokenType::LEFT_BRACKET)) {
    const Token &left_bracket = previous();
    std::vector<ast::ExprPtr> elements;
    if (!check(TokenType::RIGHT_BRACKET)) {
      do {
        elements.push_back(expression());
      } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_BRACKET, "Expected ']' after array literal.");
    return std::make_unique<ast::ArrayLiteralExpr>(location_of(left_bracket), std::move(elements));
  }
  // Map literal in expression position: `{}` or `{k: v, ...}`. Blocks never
  // appear in expression position, so a '{' here is unambiguously a map.
  if (match(TokenType::LEFT_BRACE)) {
    const Token &left_brace = previous();
    std::vector<ast::ExprPtr> keys;
    std::vector<ast::ExprPtr> values;
    if (!check(TokenType::RIGHT_BRACE)) {
      do {
        if (check(TokenType::RIGHT_BRACE))
          break; // trailing comma
        keys.push_back(expression());
        consume(TokenType::COLON, "Expected ':' between map key and value.");
        values.push_back(expression());
      } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_BRACE, "Expected '}' after map literal.");
    return std::make_unique<ast::MapLiteralExpr>(location_of(left_brace), std::move(keys),
                                                 std::move(values));
  }
  if (check(TokenType::INT) || check(TokenType::FLOAT) || check(TokenType::STRING) ||
      check(TokenType::BOOL) || check(TokenType::BYTE) || check(TokenType::DOUBLE) ||
      check(TokenType::CHAR)) {
    // Type-qualified method call: int::bits(expr)
    if (current_ + 2 < tokens_.size() && tokens_[current_ + 1].type == TokenType::COLON_COLON &&
        tokens_[current_ + 2].type == TokenType::IDENTIFIER) {
      const Token &type_tok = advance(); // consume type keyword
      advance();                         // consume ::
      const Token &method = advance();   // consume method name
      if (check(TokenType::LEFT_PAREN)) {
        consume(TokenType::LEFT_PAREN, "Expected '(' after type-qualified method.");
        std::vector<ast::ExprPtr> args;
        if (!check(TokenType::RIGHT_PAREN)) {
          do {
            args.push_back(expression());
          } while (match(TokenType::COMMA));
        }
        consume(TokenType::RIGHT_PAREN, "Expected ')' after type-qualified method arguments.");
        auto callee = std::make_unique<ast::NamespaceAccessExpr>(
            location_of(type_tok), token_text(type_tok), token_text(method));
        return std::make_unique<ast::CallExpr>(location_of(type_tok), std::move(callee),
                                               std::vector<ast::TypeExpr>{}, std::move(args));
      }
      // Not a call — treat as namespace access (unlikely but safe fallback)
      return std::make_unique<ast::NamespaceAccessExpr>(location_of(type_tok), token_text(type_tok),
                                                        token_text(method));
    }
    if (current_ + 1 < tokens_.size() && tokens_[current_ + 1].type == TokenType::LEFT_PAREN) {
      const Token &type_tok = advance();
      ast::TypeExpr target{std::string(token_text(type_tok)), {}};
      consume(TokenType::LEFT_PAREN, "Expected '(' after type name.");
      ast::ExprPtr value = expression();
      consume(TokenType::RIGHT_PAREN, "Expected ')' after cast operand.");
      return std::make_unique<ast::CastExpr>(location_of(type_tok), std::move(target),
                                             std::move(value));
    }
  }
  if (match(TokenType::IDENTIFIER)) {
    const Token &identifier = previous();
    if (match(TokenType::COLON_COLON)) {
      if (at_completion()) {
        set_completion(
            {lsp::CompletionPosition::NamespaceAccess, {}, {}, token_text(identifier), {}, {}});
        return std::make_unique<ast::IdentifierExpr>(location_of(identifier),
                                                     token_text(identifier));
      }
      std::vector<std::string> segments;
      segments.push_back(std::string(token_text(identifier)));
      const Token &first_part = consume(TokenType::IDENTIFIER, "Expected name after '::'.");
      segments.push_back(std::string(token_text(first_part)));
      while (match(TokenType::COLON_COLON)) {
        const Token &part = consume(TokenType::IDENTIFIER, "Expected name after '::'.");
        segments.push_back(std::string(token_text(part)));
      }
      return parse_namespace_access(identifier, std::move(segments));
    }
    if (check(TokenType::LEFT_BRACE) && current_ + 1 < tokens_.size() &&
        tokens_[current_ + 1].type != TokenType::RIGHT_BRACE && !token_text(identifier).empty() &&
        std::isupper(token_text(identifier)[0])) {
      advance(); // consume '{'
      std::vector<ast::StructLiteralExpr::FieldInit> fields;
      while (!check(TokenType::RIGHT_BRACE) && !is_at_end()) {
        if (at_completion()) {
          set_completion({lsp::CompletionPosition::StructLiteral,
                          {},
                          {},
                          {},
                          {},
                          std::string(token_text(identifier))});
          return std::make_unique<ast::IdentifierExpr>(location_of(identifier),
                                                       token_text(identifier));
        }
        ast::ExprPtr value = expression();
        if (has_completion())
          return std::make_unique<ast::IdentifierExpr>(location_of(identifier),
                                                       token_text(identifier));
        fields.push_back(ast::StructLiteralExpr::FieldInit{"", std::move(value)});
        if (!check(TokenType::RIGHT_BRACE)) {
          consume(TokenType::COMMA, "Expected ',' between struct fields.");
        }
      }
      consume(TokenType::RIGHT_BRACE, "Expected '}' after struct literal.");
      return std::make_unique<ast::StructLiteralExpr>(
          location_of(identifier), ast::TypeExpr{token_text(identifier), {}}, std::move(fields));
    }
    return std::make_unique<ast::IdentifierExpr>(location_of(identifier), token_text(identifier));
  }
  if (match(TokenType::LEFT_PAREN)) {
    ast::ExprPtr expr = expression();
    consume(TokenType::RIGHT_PAREN, "Expected ')' after expression.");
    return expr;
  }

  error_at(peek(), "Expected expression.");
  const Token &error_token = advance();
  return std::make_unique<ast::NullLiteralExpr>(location_of(error_token));
}

ast::ExprPtr Parser::condition_expression() {
  if (match(TokenType::LEFT_PAREN)) {
    ast::ExprPtr condition = expression();
    consume(TokenType::RIGHT_PAREN, "Expected ')' after condition.");
    return condition;
  }
  return expression();
}

ast::ExprPtr Parser::parse_match_pattern() {
  if (match(TokenType::LET)) {
    const Token &name_token = consume(TokenType::IDENTIFIER, "Expected variable name after 'let'.");
    return std::make_unique<ast::BindingPattern>(location_of(name_token),
                                                 std::string(name_token.lexeme));
  }
  if (match(TokenType::LEFT_BRACKET)) {
    return parse_array_pattern();
  }
  if (check(TokenType::IDENTIFIER) && current_ + 1 < tokens_.size() &&
      tokens_[current_ + 1].type == TokenType::COLON_COLON) {
    const Token &enum_token = advance();
    std::string enum_name(token_text(enum_token));
    advance(); // consume '::'
    const Token &variant_token =
        consume(TokenType::IDENTIFIER, "Expected variant name after '::'.");
    std::string variant_name(token_text(variant_token));
    std::vector<ast::ExprPtr> fields;
    if (match(TokenType::LEFT_PAREN)) {
      while (!check(TokenType::RIGHT_PAREN) && !is_at_end() && !has_completion()) {
        if (match(TokenType::LET)) {
          const Token &name_tok =
              consume(TokenType::IDENTIFIER, "Expected variable name after 'let'.");
          fields.push_back(std::make_unique<ast::BindingPattern>(location_of(name_tok),
                                                                 std::string(name_tok.lexeme)));
        } else {
          fields.push_back(parse_match_pattern());
        }
        if (has_completion())
          break;
        if (!check(TokenType::RIGHT_PAREN)) {
          consume(TokenType::COMMA, "Expected ',' between enum pattern fields.");
        }
      }
      if (!has_completion()) {
        consume(TokenType::RIGHT_PAREN, "Expected ')' after enum pattern fields.");
      }
    }
    return std::make_unique<ast::EnumPattern>(location_of(enum_token), std::move(enum_name),
                                              std::move(variant_name), std::move(fields));
  }
  if (check(TokenType::IDENTIFIER) && current_ + 1 < tokens_.size() &&
      tokens_[current_ + 1].type == TokenType::LEFT_BRACE && !token_text(peek()).empty() &&
      std::isupper(static_cast<unsigned char>(token_text(peek())[0]))) {
    const Token &struct_token = advance();
    return parse_struct_pattern(struct_token);
  }
  return expression();
}

ast::ExprPtr Parser::parse_array_pattern() {
  const Token &bracket = previous();
  std::vector<ast::ExprPtr> elements;
  while (!check(TokenType::RIGHT_BRACKET) && !is_at_end() && !has_completion()) {
    if (match(TokenType::LET)) {
      const Token &name_token =
          consume(TokenType::IDENTIFIER, "Expected variable name after 'let'.");
      elements.push_back(std::make_unique<ast::BindingPattern>(location_of(name_token),
                                                               std::string(name_token.lexeme)));
    } else {
      elements.push_back(parse_match_pattern());
    }
    if (has_completion())
      break;
    if (!check(TokenType::RIGHT_BRACKET)) {
      consume(TokenType::COMMA, "Expected ',' between array pattern elements.");
    }
  }
  if (has_completion())
    return std::make_unique<ast::ArrayPattern>(location_of(bracket), std::move(elements));
  consume(TokenType::RIGHT_BRACKET, "Expected ']' after array pattern.");
  return std::make_unique<ast::ArrayPattern>(location_of(bracket), std::move(elements));
}

ast::ExprPtr Parser::parse_struct_pattern(const Token &struct_token) {
  std::string struct_name(token_text(struct_token));
  consume(TokenType::LEFT_BRACE, "Expected '{' after struct name in pattern.");
  std::vector<ast::StructPatternField> fields;
  while (!check(TokenType::RIGHT_BRACE) && !is_at_end() && !has_completion()) {
    std::string field_name;
    ast::ExprPtr pattern;
    if (match(TokenType::LET)) {
      const Token &name_token =
          consume(TokenType::IDENTIFIER, "Expected variable name after 'let'.");
      pattern = std::make_unique<ast::BindingPattern>(location_of(name_token),
                                                      std::string(name_token.lexeme));
    } else if (check(TokenType::IDENTIFIER) && current_ + 1 < tokens_.size() &&
               tokens_[current_ + 1].type == TokenType::COLON) {
      const Token &name_token = advance();
      field_name = std::string(name_token.lexeme);
      advance(); // consume ':'
      pattern = parse_match_pattern();
    } else {
      pattern = parse_match_pattern();
    }
    fields.push_back(ast::StructPatternField{std::move(field_name), std::move(pattern)});
    if (has_completion())
      break;
    if (!check(TokenType::RIGHT_BRACE)) {
      consume(TokenType::COMMA, "Expected ',' between struct pattern fields.");
    }
  }
  if (!has_completion()) {
    consume(TokenType::RIGHT_BRACE, "Expected '}' after struct pattern.");
  }
  return std::make_unique<ast::StructPattern>(location_of(struct_token), std::move(struct_name),
                                              std::move(fields));
}

ast::ExprPtr Parser::match_expression(ast::ExprPtr value) {
  const Token &match_token = previous();
  consume(TokenType::LEFT_BRACE, "Expected '{' after 'match'.");

  std::vector<ast::MatchArm> arms;
  while (!check(TokenType::RIGHT_BRACE) && !is_at_end()) {
    if (at_completion()) {
      set_completion(
          {lsp::CompletionPosition::MatchArm, {}, infer_receiver_type(value.get()), {}, {}, {}});
      return value;
    }
    ast::ExprPtr pattern = parse_match_pattern();
    ast::ExprPtr guard;
    if (match(TokenType::IF)) {
      consume(TokenType::LEFT_PAREN, "Expected '(' after 'if' in guard.");
      guard = expression();
      consume(TokenType::RIGHT_PAREN, "Expected ')' after guard condition.");
    }
    consume(TokenType::FAT_ARROW, "Expected '=>' after match pattern.");
    ast::ExprPtr body = expression();
    arms.push_back(ast::MatchArm{std::move(pattern), std::move(guard), std::move(body)});
    if (!check(TokenType::RIGHT_BRACE)) {
      consume(TokenType::COMMA, "Expected ',' after match arm.");
    }
  }
  consume(TokenType::RIGHT_BRACE, "Expected '}' after match arms.");
  return std::make_unique<ast::MatchExpr>(location_of(match_token), std::move(value),
                                          std::move(arms));
}

} // namespace kinglet
