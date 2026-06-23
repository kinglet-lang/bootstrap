// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "frontend/ast/ast.h"

namespace kinglet::ast {

namespace {

void desugar_pipes_in_expr(ExprPtr &expr);

void desugar_pipes_in_stmt(StmtPtr &stmt) {
  if (!stmt) {
    return;
  }
  if (auto *block = dynamic_cast<BlockStmt *>(stmt.get())) {
    for (StmtPtr &child : block->statements) {
      desugar_pipes_in_stmt(child);
    }
    return;
  }
  if (auto *expr_stmt = dynamic_cast<ExprStmt *>(stmt.get())) {
    desugar_pipes_in_expr(expr_stmt->expr);
    return;
  }
  if (auto *ret = dynamic_cast<ReturnStmt *>(stmt.get())) {
    desugar_pipes_in_expr(ret->value);
    return;
  }
  if (auto *var = dynamic_cast<VarDeclStmt *>(stmt.get())) {
    desugar_pipes_in_expr(var->init);
    return;
  }
  if (auto *unpack = dynamic_cast<UnpackDeclStmt *>(stmt.get())) {
    desugar_pipes_in_expr(unpack->init);
    return;
  }
  if (auto *if_stmt = dynamic_cast<IfStmt *>(stmt.get())) {
    desugar_pipes_in_expr(if_stmt->condition);
    desugar_pipes_in_stmt(if_stmt->then_branch);
    desugar_pipes_in_stmt(if_stmt->else_branch);
    return;
  }
  if (auto *guard = dynamic_cast<GuardStmt *>(stmt.get())) {
    desugar_pipes_in_expr(guard->condition);
    desugar_pipes_in_stmt(guard->else_body);
    return;
  }
  if (auto *while_stmt = dynamic_cast<WhileStmt *>(stmt.get())) {
    desugar_pipes_in_expr(while_stmt->condition);
    desugar_pipes_in_stmt(while_stmt->body);
    return;
  }
  if (auto *for_stmt = dynamic_cast<ForStmt *>(stmt.get())) {
    desugar_pipes_in_stmt(for_stmt->init);
    desugar_pipes_in_expr(for_stmt->condition);
    desugar_pipes_in_stmt(for_stmt->step);
    desugar_pipes_in_stmt(for_stmt->body);
    return;
  }
  if (auto *try_stmt = dynamic_cast<TryCatchStmt *>(stmt.get())) {
    desugar_pipes_in_stmt(try_stmt->body);
    for (CatchArm &arm : try_stmt->catches) {
      desugar_pipes_in_stmt(arm.body);
    }
  }
}

void desugar_pipes_in_expr(ExprPtr &expr) {
  if (!expr) {
    return;
  }

  if (auto *pipe = dynamic_cast<PipeExpr *>(expr.get())) {
    desugar_pipes_in_expr(pipe->left);
    desugar_pipes_in_expr(pipe->right);
    auto left = std::move(pipe->left);
    auto right = std::move(pipe->right);
    const SourceLocation loc = pipe->location;
    auto call = std::make_unique<CallExpr>(loc, std::move(right), std::vector<TypeExpr>{},
                                           std::vector<ExprPtr>{});
    call->args.push_back(std::move(left));
    expr = std::move(call);
    return;
  }

  if (auto *unary = dynamic_cast<UnaryExpr *>(expr.get())) {
    desugar_pipes_in_expr(unary->right);
    return;
  }
  if (auto *binary = dynamic_cast<BinaryExpr *>(expr.get())) {
    desugar_pipes_in_expr(binary->left);
    desugar_pipes_in_expr(binary->right);
    return;
  }
  if (auto *assign = dynamic_cast<AssignExpr *>(expr.get())) {
    desugar_pipes_in_expr(assign->value);
    return;
  }
  if (auto *call = dynamic_cast<CallExpr *>(expr.get())) {
    desugar_pipes_in_expr(call->callee);
    for (ExprPtr &arg : call->args) {
      desugar_pipes_in_expr(arg);
    }
    return;
  }
  if (auto *cast = dynamic_cast<CastExpr *>(expr.get())) {
    desugar_pipes_in_expr(cast->value);
    return;
  }
  if (auto *ternary = dynamic_cast<TernaryExpr *>(expr.get())) {
    desugar_pipes_in_expr(ternary->condition);
    desugar_pipes_in_expr(ternary->then_expr);
    desugar_pipes_in_expr(ternary->else_expr);
    return;
  }
  if (auto *coalesce = dynamic_cast<NullCoalesceExpr *>(expr.get())) {
    desugar_pipes_in_expr(coalesce->left);
    desugar_pipes_in_expr(coalesce->right);
    return;
  }
  if (auto *propagate = dynamic_cast<PropagateExpr *>(expr.get())) {
    desugar_pipes_in_expr(propagate->value);
    return;
  }
  if (auto *field = dynamic_cast<FieldAccessExpr *>(expr.get())) {
    desugar_pipes_in_expr(field->object);
    return;
  }
  if (auto *field_assign = dynamic_cast<FieldAssignExpr *>(expr.get())) {
    desugar_pipes_in_expr(field_assign->object);
    desugar_pipes_in_expr(field_assign->value);
    return;
  }
  if (auto *index = dynamic_cast<IndexExpr *>(expr.get())) {
    desugar_pipes_in_expr(index->object);
    desugar_pipes_in_expr(index->index);
    return;
  }
  if (auto *index_assign = dynamic_cast<IndexAssignExpr *>(expr.get())) {
    desugar_pipes_in_expr(index_assign->object);
    desugar_pipes_in_expr(index_assign->index);
    desugar_pipes_in_expr(index_assign->value);
    return;
  }
  if (auto *array = dynamic_cast<ArrayLiteralExpr *>(expr.get())) {
    for (ExprPtr &element : array->elements) {
      desugar_pipes_in_expr(element);
    }
    return;
  }
  if (auto *map = dynamic_cast<MapLiteralExpr *>(expr.get())) {
    for (ExprPtr &key : map->keys) {
      desugar_pipes_in_expr(key);
    }
    for (ExprPtr &value : map->values) {
      desugar_pipes_in_expr(value);
    }
    return;
  }
  if (auto *lit = dynamic_cast<StructLiteralExpr *>(expr.get())) {
    for (auto &field : lit->fields) {
      desugar_pipes_in_expr(field.value);
    }
    return;
  }
  if (auto *match = dynamic_cast<MatchExpr *>(expr.get())) {
    desugar_pipes_in_expr(match->value);
    for (MatchArm &arm : match->arms) {
      desugar_pipes_in_expr(arm.pattern);
      desugar_pipes_in_expr(arm.guard);
      desugar_pipes_in_expr(arm.body);
    }
    return;
  }
  if (auto *array_pat = dynamic_cast<ArrayPattern *>(expr.get())) {
    for (ExprPtr &element : array_pat->elements) {
      desugar_pipes_in_expr(element);
    }
    return;
  }
  if (auto *enum_pat = dynamic_cast<EnumPattern *>(expr.get())) {
    for (ExprPtr &field : enum_pat->fields) {
      desugar_pipes_in_expr(field);
    }
    return;
  }
  if (auto *struct_pat = dynamic_cast<StructPattern *>(expr.get())) {
    for (auto &field : struct_pat->fields) {
      desugar_pipes_in_expr(field.pattern);
    }
  }
}

void desugar_pipes_in_decl(DeclPtr &decl) {
  if (!decl) {
    return;
  }
  if (auto *fn = dynamic_cast<FunctionDecl *>(decl.get())) {
    desugar_pipes_in_stmt(fn->body);
    return;
  }
  if (auto *top = dynamic_cast<TopLevelStmtDecl *>(decl.get())) {
    desugar_pipes_in_stmt(top->stmt);
  }
}

} // namespace

void desugar_pipes(Program &program) {
  for (DeclPtr &decl : program.declarations) {
    desugar_pipes_in_decl(decl);
  }
}

} // namespace kinglet::ast
