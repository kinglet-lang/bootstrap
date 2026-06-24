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

ast::StmtPtr Parser::statement() {
  if (at_completion()) {
    // As in declaration(): a dangling `.`, `::`, or `:` cannot begin a
    // statement, so suppress completion rather than offering every keyword.
    if (completion_after_dangling_access()) {
      set_completion({lsp::CompletionPosition::None, {}, {}, {}, {}, {}});
      return nullptr;
    }
    set_completion({lsp::CompletionPosition::Statement, {}, {}, {}, {}, {}});
    return nullptr;
  }
  // A leading '{' is normally a block, but `{K: V} name = ...` is a map
  // variable declaration. Disambiguate by lookahead before treating it as a
  // block (Kinglet has no labelled blocks, so the var-decl shape is unambiguous).
  if (check(TokenType::LEFT_BRACE) && looks_like_map_var_decl()) {
    return var_declaration();
  }
  if (match(TokenType::LEFT_BRACE)) {
    return block_statement();
  }
  if (match(TokenType::RETURN)) {
    return return_statement();
  }
  if (match(TokenType::IF)) {
    return if_statement();
  }
  if (match(TokenType::GUARD)) {
    return guard_statement();
  }
  if (match(TokenType::WHILE)) {
    return while_statement();
  }
  if (match(TokenType::FOR)) {
    return for_statement();
  }
  if (match(TokenType::TRY)) {
    return try_catch_statement();
  }
  if (match(TokenType::BREAK)) {
    return break_statement();
  }
  if (match(TokenType::CONTINUE)) {
    return continue_statement();
  }
  if (is_declaration_start()) {
    return var_declaration();
  }
  if (match(TokenType::LET)) {
    if (at_completion()) {
      set_completion({lsp::CompletionPosition::ExpressionStart, {}, {}, {}, {}, {}});
      return nullptr;
    }
    return expression_statement();
  }
  return expression_statement();
}


ast::StmtPtr Parser::block_statement() {
  const Token &left_brace = previous();
  std::vector<ast::StmtPtr> statements;
  while (!check(TokenType::RIGHT_BRACE) && !is_at_end() && !has_completion()) {
    statements.push_back(statement());
  }
  if (at_completion()) {
    // Cursor landed inside the block (e.g. '{ █ }') but before any
    // statement parser ran, so set the context here.
    set_completion({lsp::CompletionPosition::Statement, {}, {}, {}, {}, {}});
    return nullptr;
  }
  if (has_completion()) return nullptr;
  consume(TokenType::RIGHT_BRACE, "Expected '}' after block.");
  return std::make_unique<ast::BlockStmt>(location_of(left_brace), std::move(statements));
}


ast::StmtPtr Parser::return_statement() {
  const Token &return_token = previous();
  ast::ExprPtr value;
  if (!check(TokenType::SEMICOLON)) {
    value = expression();
  }
  consume(TokenType::SEMICOLON, "Expected ';' after return value.");
  return std::make_unique<ast::ReturnStmt>(location_of(return_token), std::move(value));
}


ast::StmtPtr Parser::if_statement() {
  const Token &if_token = previous();
  ast::ExprPtr condition = condition_expression();
  ast::StmtPtr then_branch = statement();
  ast::StmtPtr else_branch;
  if (match(TokenType::ELSE)) {
    else_branch = statement();
  }
  return std::make_unique<ast::IfStmt>(location_of(if_token), std::move(condition), std::move(then_branch),
                                       std::move(else_branch));
}


ast::StmtPtr Parser::while_statement() {
  const Token &while_token = previous();
  ast::ExprPtr condition = condition_expression();
  ast::StmtPtr body = statement();
  return std::make_unique<ast::WhileStmt>(location_of(while_token), std::move(condition),
                                          std::move(body));
}


ast::StmtPtr Parser::guard_statement() {
  const Token &guard_token = previous();
  ast::ExprPtr condition = expression();
  consume(TokenType::ELSE, "Expected 'else' after guard condition.");
  consume(TokenType::LEFT_BRACE, "Expected '{' after 'else' in guard statement.");
  ast::StmtPtr else_body = block_statement();
  return std::make_unique<ast::GuardStmt>(location_of(guard_token), std::move(condition),
                                          std::move(else_body));
}


ast::StmtPtr Parser::for_statement() {
  const Token &for_token = previous();
  consume(TokenType::LEFT_PAREN, "Expected '(' after 'for'.");

  // Parse init: either a variable declaration, an expression statement, or empty
  ast::StmtPtr init;
  if (is_declaration_start()) {
    init = var_declaration();
    // var_declaration already consumes its own trailing semicolon
  } else if (!check(TokenType::SEMICOLON)) {
    ast::ExprPtr expr = expression();
    init = std::make_unique<ast::ExprStmt>(expr->location, std::move(expr));
    consume(TokenType::SEMICOLON, "Expected ';' after for init.");
  } else {
    consume(TokenType::SEMICOLON, "Expected ';' after for init.");
  }

  // Parse condition
  ast::ExprPtr condition;
  if (!check(TokenType::SEMICOLON)) {
    condition = expression();
  }
  consume(TokenType::SEMICOLON, "Expected ';' after for condition.");

  // Parse step: expression without consuming trailing semicolon (the ')' closes the header)
  ast::StmtPtr step;
  if (!check(TokenType::RIGHT_PAREN)) {
    ast::ExprPtr step_expr = expression();
    step = std::make_unique<ast::ExprStmt>(step_expr->location, std::move(step_expr));
  }
  consume(TokenType::RIGHT_PAREN, "Expected ')' after for clauses.");

  ast::StmtPtr body = statement();
  return std::make_unique<ast::ForStmt>(location_of(for_token), std::move(init),
                                        std::move(condition), std::move(step), std::move(body));
}


ast::StmtPtr Parser::try_catch_statement() {
  const Token &try_token = previous();
  consume(TokenType::LEFT_BRACE, "Expected '{' after 'try'.");
  ast::StmtPtr body = block_statement();

  std::vector<ast::CatchArm> catches;
  while (match(TokenType::CATCH)) {
    consume(TokenType::LEFT_PAREN, "Expected '(' after 'catch'.");
    consume(TokenType::LET, "Expected 'let' in catch pattern.");
    const Token &name_tok = consume(TokenType::IDENTIFIER, "Expected binding name.");
    consume(TokenType::COLON, "Expected ':' after catch binding name.");
    ast::TypeExpr err_type = parse_type_expr();
    consume(TokenType::RIGHT_PAREN, "Expected ')' after catch type.");
    consume(TokenType::LEFT_BRACE, "Expected '{' after catch pattern.");
    ast::StmtPtr catch_body = block_statement();
    catches.push_back(ast::CatchArm{
        .error_type = std::move(err_type),
        .binding_name = token_text(name_tok),
        .body = std::move(catch_body),
    });
  }
  if (catches.empty()) {
    error_at(try_token, "Expected at least one 'catch' clause after 'try'.");
  }
  return std::make_unique<ast::TryCatchStmt>(location_of(try_token), std::move(body),
                                             std::move(catches));
}


ast::StmtPtr Parser::break_statement() {
  const Token &break_token = previous();
  consume(TokenType::SEMICOLON, "Expected ';' after 'break'.");
  return std::make_unique<ast::BreakStmt>(location_of(break_token));
}


ast::StmtPtr Parser::continue_statement() {
  const Token &continue_token = previous();
  consume(TokenType::SEMICOLON, "Expected ';' after 'continue'.");
  return std::make_unique<ast::ContinueStmt>(location_of(continue_token));
}


ast::StmtPtr Parser::var_declaration() {
  const Token &start_token = peek();
  std::string storage;
  if (match(TokenType::CONST)) {
    storage = token_text(previous());
  }

  ast::TypeExpr type;
  Token name = peek();
  bool has_type = false;

  if (is_type_start(peek().type)) {
    size_t pos = current_ + 1;
    if (pos < tokens_.size() && tokens_[pos].type == TokenType::LEFT_BRACKET &&
        peek().type == TokenType::AUTO) {
      advance(); // consume type token (auto/int/etc)
      advance(); // consume '['
      std::vector<std::string> names;
      std::string rest_name;
      while (!check(TokenType::RIGHT_BRACKET) && !is_at_end()) {
        if (match(TokenType::DOT_DOT_DOT)) {
          Token rest = consume(TokenType::IDENTIFIER, "Expected name after '...'.");
          rest_name = token_text(rest);
        } else {
          Token n = consume(TokenType::IDENTIFIER, "Expected variable name in destructuring.");
          names.push_back(token_text(n));
        }
        if (!check(TokenType::RIGHT_BRACKET)) {
          consume(TokenType::COMMA, "Expected ',' between names.");
        }
      }
      consume(TokenType::RIGHT_BRACKET, "Expected ']' after destructuring names.");
      consume(TokenType::EQUAL, "Expected '=' after destructuring pattern.");
      ast::ExprPtr init = expression();
      consume(TokenType::SEMICOLON, "Expected ';' after variable declaration.");
      return std::make_unique<ast::UnpackDeclStmt>(location_of(start_token), std::move(names),
                                                   std::move(rest_name), std::move(init));
    }
  }
  if (is_type_start(peek().type)) {
    size_t pos = current_ + 1;
    if (peek().type == TokenType::LEFT_BRACE) {
      // Map type {K: V}: scan to the matching '}', then expect an identifier.
      int depth = 1;
      while (pos < tokens_.size() && depth > 0) {
        if (tokens_[pos].type == TokenType::LEFT_BRACE) ++depth;
        else if (tokens_[pos].type == TokenType::RIGHT_BRACE) --depth;
        ++pos;
      }
      skip_array_and_nullable_suffix(tokens_, pos);
      has_type = pos < tokens_.size() && tokens_[pos].type == TokenType::IDENTIFIER;
    } else if (pos < tokens_.size() && tokens_[pos].type == TokenType::LESS) {
      int depth = 1;
      ++pos;
      while (pos < tokens_.size() && depth > 0) {
        if (tokens_[pos].type == TokenType::LESS) ++depth;
        else if (tokens_[pos].type == TokenType::GREATER) --depth;
        else if (tokens_[pos].type == TokenType::GREATER_GREATER) depth -= 2;
        ++pos;
      }
      skip_array_and_nullable_suffix(tokens_, pos);
      has_type = pos < tokens_.size() && tokens_[pos].type == TokenType::IDENTIFIER;
    } else {
      skip_array_and_nullable_suffix(tokens_, pos);
      has_type = pos < tokens_.size() && tokens_[pos].type == TokenType::IDENTIFIER;
    }
  }
  if (has_type) {
    type = parse_type_expr();
    if (pending_greater_) pending_greater_ = false;
    name = consume(TokenType::IDENTIFIER, "Expected variable name.");
  } else {
    name = consume(TokenType::IDENTIFIER, "Expected variable name.");
  }

  ast::ExprPtr init;
  if (match(TokenType::EQUAL)) {
    init = expression();
  } else if (!type.name.empty() && type.name != "Map" && type.name != "Array" &&
             check(TokenType::LEFT_BRACE)) {
    advance(); // consume '{'
    std::vector<ast::StructLiteralExpr::FieldInit> fields;
    while (!check(TokenType::RIGHT_BRACE) && !is_at_end()) {
      if (at_completion()) {
        set_completion({lsp::CompletionPosition::StructLiteral, {}, {}, {}, {}, type.name});
        return nullptr;
      }
      ast::ExprPtr value = expression();
      if (has_completion()) return nullptr;
      fields.push_back(ast::StructLiteralExpr::FieldInit{"", std::move(value)});
      if (!check(TokenType::RIGHT_BRACE)) {
        consume(TokenType::COMMA, "Expected ',' between struct fields.");
      }
    }
    consume(TokenType::RIGHT_BRACE, "Expected '}' after struct literal.");
    init = std::make_unique<ast::StructLiteralExpr>(
        location_of(start_token), type, std::move(fields));
  }
  consume(TokenType::SEMICOLON, "Expected ';' after variable declaration.");
  return std::make_unique<ast::VarDeclStmt>(location_of(start_token), std::move(storage), std::move(type),
                                            token_text(name), std::move(init));
}


ast::StmtPtr Parser::expression_statement() {
  ast::ExprPtr expr = expression();
  if (at_completion()) {
    set_completion({lsp::CompletionPosition::ExpressionStart, {}, {}, {}, {}, {}});
    return nullptr;
  }
  if (has_completion()) return nullptr;
  const ast::SourceLocation location = expr->location;
  consume(TokenType::SEMICOLON, "Expected ';' after expression.");
  return std::make_unique<ast::ExprStmt>(location, std::move(expr));
}


} // namespace kinglet
