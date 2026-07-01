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

ast::DeclPtr Parser::declaration() {
  if (at_completion()) {
    // A member-access or type-separator operator (`.`, `::`, or a lone `:`)
    // cannot begin a top-level declaration. If one immediately precedes the
    // cursor here, the position is syntactically invalid, so offer no
    // completion rather than flooding the list with every declaration keyword.
    if (completion_after_dangling_access()) {
      set_completion({lsp::CompletionPosition::None, {}, {}, {}, {}, {}});
      return nullptr;
    }
    set_completion({lsp::CompletionPosition::TopLevelDecl, {}, {}, {}, {}, {}});
    return nullptr;
  }

  if (match(TokenType::USING)) {
    return using_declaration();
  }

  if (match(TokenType::EXPORT)) {
    return export_module_declaration();
  }

  if (match(TokenType::IMPORT)) {
    return import_declaration();
  }

  bool is_public = match(TokenType::PUB);

  if (match(TokenType::STRUCT)) {
    auto decl = struct_declaration();
    if (decl) {
      auto *sd = static_cast<ast::StructDecl *>(decl.get());
      sd->is_public = is_public;
    }
    return decl;
  }

  if (match(TokenType::ENUM)) {
    auto decl = enum_declaration();
    if (decl) {
      auto *ed = static_cast<ast::EnumDecl *>(decl.get());
      ed->is_public = is_public;
    }
    return decl;
  }

  if (match(TokenType::CONCEPT)) {
    auto decl = concept_declaration();
    if (decl) {
      auto *cd = static_cast<ast::ConceptDecl *>(decl.get());
      cd->is_public = is_public;
    }
    return decl;
  }

  if (is_function_declaration_start()) {
    auto decl = function_declaration();
    if (decl) {
      auto *fd = static_cast<ast::FunctionDecl *>(decl.get());
      fd->is_public = is_public;
    }
    return decl;
  }

  if (is_public) {
    error_at(peek(),
             "'pub' can only be used before function, struct, enum, or concept declarations.");
  }

  ast::StmtPtr stmt = statement();
  if (!stmt) {
    synchronize();
    return nullptr;
  }
  const ast::SourceLocation location = stmt->location;
  return std::make_unique<ast::TopLevelStmtDecl>(location, std::move(stmt));
}

ast::DeclPtr Parser::using_declaration() {
  const Token &using_token = previous();
  bool is_namespace = false;
  if (match(TokenType::NAMESPACE)) {
    is_namespace = true;
  }
  if (at_completion()) {
    set_completion({lsp::CompletionPosition::UsingNamespace, {}, {}, {}, {}, {}});
    return nullptr;
  }
  const Token &name = consume(TokenType::IDENTIFIER, "Expected name after 'using'.");
  if (!is_namespace && match(TokenType::EQUAL)) {
    const std::string module_id = parse_module_id("module alias");
    consume(TokenType::SEMICOLON, "Expected ';' after using alias.");
    return std::make_unique<ast::UsingAliasDecl>(location_of(using_token),
                                                 std::string(token_text(name)), module_id);
  }
  if (match(TokenType::COLON_COLON)) {
    error_at(previous(),
             "Wildcard `using module::*` is not supported; use `using namespace module` instead.");
    return nullptr;
  }
  std::string module_id = std::string(token_text(name));
  while (match(TokenType::DOT)) {
    const Token &part =
        consume(TokenType::IDENTIFIER, "Expected identifier after '.' in module name.");
    module_id.push_back('.');
    module_id += token_text(part);
  }
  if (match(TokenType::LEFT_BRACE)) {
    error_at(previous(), "Selective `using module { sym }` is not supported; use qualified access, "
                         "`using alias = module`, or `using namespace module`.");
    return nullptr;
  }
  consume(TokenType::SEMICOLON, "Expected ';' after using declaration.");
  return std::make_unique<ast::UsingDecl>(location_of(using_token), std::move(module_id),
                                          is_namespace);
}

ast::DeclPtr Parser::export_module_declaration() {
  const Token &export_token = previous();
  const Token &module_kw = consume(TokenType::IDENTIFIER, "Expected 'module' after 'export'.");
  if (token_text(module_kw) != "module") {
    error_at(module_kw, "Expected 'module' after 'export'.");
    return nullptr;
  }
  const std::string module_id = parse_module_id("module name after 'export module'");
  consume(TokenType::SEMICOLON, "Expected ';' after export module.");
  return std::make_unique<ast::ExportModuleDecl>(location_of(export_token), module_id);
}

ast::DeclPtr Parser::import_declaration() {
  const Token &import_token = previous();

  if (at_completion()) {
    // `import █` — offer module names from kinglet.nest. We leave
    // import_path empty so the resolver knows it's the path slot itself.
    set_completion({lsp::CompletionPosition::ImportPath, {}, {}, {}, {}, {}});
    return nullptr;
  }

  if (match(TokenType::LEFT_BRACE)) {
    error_at(previous(), "Import block syntax `import { ... }` is removed; use `import module-id;` "
                         "with kinglet.nest.");
    return nullptr;
  }
  if (check(TokenType::IDENTIFIER)) {
    const std::string module_id = parse_module_id("module name after 'import'");
    consume(TokenType::SEMICOLON, "Expected ';' after import.");
    return std::make_unique<ast::LogicalImportDecl>(location_of(import_token), module_id);
  }

  error_at(peek(), "Expected module name after 'import'.");
  return nullptr;
}

ast::DeclPtr Parser::struct_declaration() {
  const Token &struct_token = previous();
  const Token &name = consume(TokenType::IDENTIFIER, "Expected struct name.");

  std::vector<std::string> type_params;
  if (match(TokenType::LESS)) {
    do {
      const Token &param = consume(TokenType::IDENTIFIER, "Expected type parameter name.");
      type_params.push_back(token_text(param));
    } while (match(TokenType::COMMA));
    consume(TokenType::GREATER, "Expected '>' after type parameters.");
  }

  consume(TokenType::LEFT_BRACE, "Expected '{' after struct name.");

  std::vector<ast::FieldDef> fields;
  while (!check(TokenType::RIGHT_BRACE) && !is_at_end()) {
    if (at_completion()) {
      set_completion({lsp::CompletionPosition::StructFieldDecl, {}, {}, {}, {}, {}});
      return nullptr;
    }
    size_t start_pos = current_;
    ast::TypeExpr type = parse_type_expr();
    if (has_completion())
      return nullptr;
    const Token &field_name = consume(TokenType::IDENTIFIER, "Expected field name.");
    consume(TokenType::SEMICOLON, "Expected ';' after field declaration.");
    fields.push_back(ast::FieldDef{std::move(type), token_text(field_name)});
    if (current_ == start_pos) {
      advance();
    }
  }
  consume(TokenType::RIGHT_BRACE, "Expected '}' after struct body.");

  return std::make_unique<ast::StructDecl>(location_of(struct_token), token_text(name),
                                           std::move(type_params), std::move(fields));
}

ast::DeclPtr Parser::enum_declaration() {
  const Token &enum_token = previous();
  const Token &name = consume(TokenType::IDENTIFIER, "Expected enum name.");
  consume(TokenType::LEFT_BRACE, "Expected '{' after enum name.");

  std::vector<ast::EnumVariantDecl> variants;
  while (!check(TokenType::RIGHT_BRACE) && !is_at_end()) {
    if (at_completion()) {
      set_completion({lsp::CompletionPosition::EnumVariant, {}, {}, {}, {}, {}});
      return nullptr;
    }
    size_t start_pos = current_;
    const Token &variant = consume(TokenType::IDENTIFIER, "Expected variant name.");
    ast::EnumVariantDecl decl;
    decl.name = token_text(variant);
    if (match(TokenType::LEFT_PAREN)) {
      while (!check(TokenType::RIGHT_PAREN) && !is_at_end()) {
        size_t inner_pos = current_;
        decl.param_types.push_back(parse_type_expr());
        if (!check(TokenType::RIGHT_PAREN)) {
          consume(TokenType::COMMA, "Expected ',' between variant parameter types.");
        }
        if (current_ == inner_pos) {
          advance();
        }
      }
      consume(TokenType::RIGHT_PAREN, "Expected ')' after variant parameters.");
    }
    variants.push_back(std::move(decl));
    if (!check(TokenType::RIGHT_BRACE)) {
      consume(TokenType::COMMA, "Expected ',' between enum variants.");
    }
    if (current_ == start_pos) {
      advance();
    }
  }
  consume(TokenType::RIGHT_BRACE, "Expected '}' after enum body.");

  return std::make_unique<ast::EnumDecl>(location_of(enum_token), token_text(name),
                                         std::move(variants));
}

ast::DeclPtr Parser::concept_declaration() {
  const Token &concept_token = previous();
  const Token &name = consume(TokenType::IDENTIFIER, "Expected concept name.");
  std::vector<std::string> type_params;
  if (match(TokenType::LESS)) {
    do {
      const Token &param = consume(TokenType::IDENTIFIER, "Expected type parameter name.");
      type_params.push_back(token_text(param));
    } while (match(TokenType::COMMA));
    consume(TokenType::GREATER, "Expected '>' after type parameters.");
  }
  if (type_params.empty()) {
    error_at(previous(), "Concept must declare at least one type parameter.");
  }
  // Expose the concept's type params to nested type-position completion
  // (method return types and parameter types reference them, e.g. 'T value').
  active_type_params_ = type_params;
  consume(TokenType::LEFT_BRACE, "Expected '{' after concept name.");
  // Guard before the body loop so an empty body ('{ █ }') still sets a
  // completion context.  ParameterType filters out 'auto' — concept
  // method signatures require explicit return types, not type deduction.
  if (at_completion()) {
    set_completion(
        {lsp::CompletionPosition::ParameterType, {}, {}, {}, {}, {}, active_type_params_});
    active_type_params_.clear();
    return nullptr;
  }
  std::vector<ast::ConceptMethodDecl> methods;
  while (!check(TokenType::RIGHT_BRACE) && !is_at_end() && !has_completion()) {
    ast::TypeExpr ret_type = parse_type_expr();
    const Token &method_name = consume(TokenType::IDENTIFIER, "Expected method name in concept.");
    consume(TokenType::LEFT_PAREN, "Expected '(' after method name.");
    auto params = parameters();
    consume(TokenType::RIGHT_PAREN, "Expected ')' after parameter list.");
    consume(TokenType::SEMICOLON, "Expected ';' after concept method signature.");
    methods.push_back(ast::ConceptMethodDecl{ret_type, token_text(method_name), std::move(params)});
  }
  active_type_params_.clear();
  if (has_completion())
    return nullptr;
  consume(TokenType::RIGHT_BRACE, "Expected '}' after concept body.");
  return std::make_unique<ast::ConceptDecl>(location_of(concept_token), token_text(name),
                                            std::move(type_params), std::move(methods));
}

ast::DeclPtr Parser::function_declaration() {
  const Token &return_type_token = peek();
  ast::TypeExpr return_type = parse_type_expr();
  const Token &name = consume(TokenType::IDENTIFIER, "Expected function name.");

  std::vector<std::string> type_params;
  if (match(TokenType::LESS)) {
    do {
      const Token &param = consume(TokenType::IDENTIFIER, "Expected type parameter name.");
      type_params.push_back(token_text(param));
    } while (match(TokenType::COMMA));
    consume(TokenType::GREATER, "Expected '>' after type parameters.");
  }

  consume(TokenType::LEFT_PAREN, "Expected '(' after function name.");
  std::vector<ast::Parameter> params = parameters();
  consume(TokenType::RIGHT_PAREN, "Expected ')' after parameter list.");
  ast::StmtPtr body = function_body();
  return std::make_unique<ast::FunctionDecl>(location_of(return_type_token), std::move(return_type),
                                             token_text(name), std::move(type_params),
                                             std::move(params), std::move(body));
}

} // namespace kinglet
