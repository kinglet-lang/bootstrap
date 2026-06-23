// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "driver/preen/span_map.h"

#include <algorithm>

namespace kinglet::preen {

namespace {

TokenSpan merge_spans(const TokenSpan &a, const TokenSpan &b) {
  if (a.end == 0) {
    return b;
  }
  if (b.end == 0) {
    return a;
  }
  return TokenSpan{std::min(a.start, b.start), std::max(a.end, b.end)};
}

} // namespace

SpanMap::SpanMap(const ast::Program &program, const std::vector<Token> &tokens)
    : tokens_(tokens) {
  for (const ast::DeclPtr &decl : program.declarations) {
    build_decl(*decl);
  }
}

bool SpanMap::has(const ast::Node &node) const {
  return spans_.count(&node) > 0;
}

TokenSpan SpanMap::span(const ast::Node &node) const {
  const auto it = spans_.find(&node);
  if (it == spans_.end()) {
    return {};
  }
  return it->second;
}

std::size_t SpanMap::locate_token(int line, int column) const {
  for (std::size_t i = 0; i < tokens_.size(); ++i) {
    if (tokens_[i].line == line && tokens_[i].column == column) {
      return i;
    }
  }
  for (std::size_t i = 0; i < tokens_.size(); ++i) {
    if (tokens_[i].line == line) {
      return i;
    }
  }
  return 0;
}

void SpanMap::build_expr(const ast::Expr &expr) {
  TokenSpan current{locate_token(expr.location.line, expr.location.column), 0};

  if (const auto *pipe = dynamic_cast<const ast::PipeExpr *>(&expr)) {
    build_expr(*pipe->left);
    build_expr(*pipe->right);
    current = merge_spans(span(*pipe->left), span(*pipe->right));
    if (current.end == 0) {
      current.end = current.start + 1;
    }
    spans_[&expr] = current;
    return;
  }

  if (const auto *binary = dynamic_cast<const ast::BinaryExpr *>(&expr)) {
    build_expr(*binary->left);
    build_expr(*binary->right);
    current = merge_spans(span(*binary->left), span(*binary->right));
    current.end = std::max(current.end, current.start + 1);
    spans_[&expr] = current;
    return;
  }

  if (const auto *unary = dynamic_cast<const ast::UnaryExpr *>(&expr)) {
    build_expr(*unary->right);
    current.end = std::max(span(*unary->right).end, current.start + 1);
    spans_[&expr] = current;
    return;
  }

  if (const auto *call = dynamic_cast<const ast::CallExpr *>(&expr)) {
    build_expr(*call->callee);
    TokenSpan merged = span(*call->callee);
    for (const ast::ExprPtr &arg : call->args) {
      build_expr(*arg);
      merged = merge_spans(merged, span(*arg));
    }
    current = merged;
    if (current.end == 0) {
      current.end = current.start + 1;
    }
    spans_[&expr] = current;
    return;
  }

  if (const auto *assign = dynamic_cast<const ast::AssignExpr *>(&expr)) {
    build_expr(*assign->value);
    current.end = std::max(span(*assign->value).end, current.start + 1);
    spans_[&expr] = current;
    return;
  }

  if (const auto *ternary = dynamic_cast<const ast::TernaryExpr *>(&expr)) {
    build_expr(*ternary->condition);
    build_expr(*ternary->then_expr);
    build_expr(*ternary->else_expr);
    current = merge_spans(span(*ternary->condition), span(*ternary->else_expr));
    spans_[&expr] = current;
    return;
  }

  if (const auto *coalesce = dynamic_cast<const ast::NullCoalesceExpr *>(&expr)) {
    build_expr(*coalesce->left);
    build_expr(*coalesce->right);
    current = merge_spans(span(*coalesce->left), span(*coalesce->right));
    spans_[&expr] = current;
    return;
  }

  if (const auto *propagate = dynamic_cast<const ast::PropagateExpr *>(&expr)) {
    build_expr(*propagate->value);
    current.end = std::max(span(*propagate->value).end, current.start + 1);
    spans_[&expr] = current;
    return;
  }

  if (const auto *field = dynamic_cast<const ast::FieldAccessExpr *>(&expr)) {
    build_expr(*field->object);
    current = merge_spans(current, span(*field->object));
    current.end = std::max(current.end, current.start + 1);
    spans_[&expr] = current;
    return;
  }

  if (const auto *field_assign = dynamic_cast<const ast::FieldAssignExpr *>(&expr)) {
    build_expr(*field_assign->object);
    build_expr(*field_assign->value);
    current = merge_spans(span(*field_assign->object), span(*field_assign->value));
    spans_[&expr] = current;
    return;
  }

  if (const auto *index = dynamic_cast<const ast::IndexExpr *>(&expr)) {
    build_expr(*index->object);
    build_expr(*index->index);
    current = merge_spans(span(*index->object), span(*index->index));
    spans_[&expr] = current;
    return;
  }

  if (const auto *index_assign = dynamic_cast<const ast::IndexAssignExpr *>(&expr)) {
    build_expr(*index_assign->object);
    build_expr(*index_assign->index);
    build_expr(*index_assign->value);
    current = merge_spans(span(*index_assign->object), span(*index_assign->value));
    spans_[&expr] = current;
    return;
  }

  if (const auto *array = dynamic_cast<const ast::ArrayLiteralExpr *>(&expr)) {
    for (const ast::ExprPtr &element : array->elements) {
      build_expr(*element);
      current = merge_spans(current, span(*element));
    }
    current.end = std::max(current.end, current.start + 1);
    spans_[&expr] = current;
    return;
  }

  if (const auto *map = dynamic_cast<const ast::MapLiteralExpr *>(&expr)) {
    for (std::size_t i = 0; i < map->keys.size(); ++i) {
      build_expr(*map->keys[i]);
      build_expr(*map->values[i]);
      current = merge_spans(current, span(*map->values[i]));
    }
    current.end = std::max(current.end, current.start + 1);
    spans_[&expr] = current;
    return;
  }

  if (const auto *lit = dynamic_cast<const ast::StructLiteralExpr *>(&expr)) {
    for (const auto &field : lit->fields) {
      build_expr(*field.value);
      current = merge_spans(current, span(*field.value));
    }
    current.end = std::max(current.end, current.start + 1);
    spans_[&expr] = current;
    return;
  }

  if (const auto *match = dynamic_cast<const ast::MatchExpr *>(&expr)) {
    build_expr(*match->value);
    current = span(*match->value);
    for (const ast::MatchArm &arm : match->arms) {
      build_expr(*arm.pattern);
      build_expr(*arm.body);
      if (arm.guard) {
        build_expr(*arm.guard);
      }
      current = merge_spans(current, span(*arm.body));
    }
    spans_[&expr] = current;
    return;
  }

  if (const auto *cast = dynamic_cast<const ast::CastExpr *>(&expr)) {
    build_expr(*cast->value);
    current.end = std::max(span(*cast->value).end, current.start + 1);
    spans_[&expr] = current;
    return;
  }

  current.end = current.start + 1;
  spans_[&expr] = current;
}

void SpanMap::build_stmt(const ast::Stmt &stmt) {
  if (const auto *block = dynamic_cast<const ast::BlockStmt *>(&stmt)) {
    for (const ast::StmtPtr &child : block->statements) {
      build_stmt(*child);
    }
    return;
  }
  if (const auto *expr_stmt = dynamic_cast<const ast::ExprStmt *>(&stmt)) {
    build_expr(*expr_stmt->expr);
    TokenSpan expr_span =
        spans_.count(expr_stmt->expr.get()) > 0 ? span(*expr_stmt->expr) : TokenSpan{};
    if (expr_span.end > 0) {
      for (std::size_t i = expr_span.end; i < tokens_.size(); ++i) {
        if (tokens_[i].type == TokenType::SEMICOLON) {
          expr_span.end = i + 1;
          break;
        }
      }
    }
    spans_[&stmt] = expr_span;
    return;
  }
  if (const auto *ret = dynamic_cast<const ast::ReturnStmt *>(&stmt)) {
    if (ret->value) {
      build_expr(*ret->value);
      spans_[&stmt] = span(*ret->value);
    }
    return;
  }
  if (const auto *var = dynamic_cast<const ast::VarDeclStmt *>(&stmt)) {
    if (var->init) {
      build_expr(*var->init);
    }
    const std::size_t start = locate_token(stmt.location.line, stmt.location.column);
    std::size_t end = start + 1;
    for (std::size_t i = start; i < tokens_.size(); ++i) {
      if (tokens_[i].type == TokenType::SEMICOLON) {
        end = i + 1;
        break;
      }
    }
    spans_[&stmt] = TokenSpan{start, end};
    return;
  }
  if (const auto *unpack = dynamic_cast<const ast::UnpackDeclStmt *>(&stmt)) {
    build_expr(*unpack->init);
    spans_[&stmt] = span(*unpack->init);
    return;
  }
  if (const auto *if_stmt = dynamic_cast<const ast::IfStmt *>(&stmt)) {
    build_expr(*if_stmt->condition);
    build_stmt(*if_stmt->then_branch);
    if (if_stmt->else_branch) {
      build_stmt(*if_stmt->else_branch);
    }
    return;
  }
  if (const auto *guard = dynamic_cast<const ast::GuardStmt *>(&stmt)) {
    build_expr(*guard->condition);
    build_stmt(*guard->else_body);
    return;
  }
  if (const auto *while_stmt = dynamic_cast<const ast::WhileStmt *>(&stmt)) {
    build_expr(*while_stmt->condition);
    build_stmt(*while_stmt->body);
    return;
  }
  if (const auto *for_stmt = dynamic_cast<const ast::ForStmt *>(&stmt)) {
    if (for_stmt->init) {
      build_stmt(*for_stmt->init);
    }
    if (for_stmt->condition) {
      build_expr(*for_stmt->condition);
    }
    if (for_stmt->step) {
      build_stmt(*for_stmt->step);
    }
    build_stmt(*for_stmt->body);
    return;
  }
  if (const auto *try_stmt = dynamic_cast<const ast::TryCatchStmt *>(&stmt)) {
    build_stmt(*try_stmt->body);
    for (const ast::CatchArm &arm : try_stmt->catches) {
      build_stmt(*arm.body);
    }
  }
}

void SpanMap::build_decl(const ast::Decl &decl) {
  if (const auto *fn = dynamic_cast<const ast::FunctionDecl *>(&decl)) {
    build_stmt(*fn->body);
    return;
  }
  if (const auto *top = dynamic_cast<const ast::TopLevelStmtDecl *>(&decl)) {
    build_stmt(*top->stmt);
  }
}

} // namespace kinglet::preen
