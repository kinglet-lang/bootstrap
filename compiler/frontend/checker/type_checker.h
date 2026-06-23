// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "frontend/ast/ast.h"
#include "ir/kir.h"
#include "frontend/types/types.h"
#include "frontend/sema/semantic_context.h"

#include <cstdint>

namespace kinglet {
class ModuleLoader;
struct ParsedModule;
}

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kinglet {

enum class DiagnosticSeverity : std::uint8_t { Error = 1, Warning = 2, Info = 3, Hint = 4 };

struct TypeError {
  ast::SourceLocation location;
  std::string message;
  DiagnosticSeverity severity = DiagnosticSeverity::Error;
};

struct TypeCheckResult {
  std::vector<TypeError> errors;
};

class TypeChecker : public ast::StmtVisitor, public ast::ExprVisitor {
public:
  TypeCheckResult check(const ast::Program &program);
  void set_module_loader(ModuleLoader *loader) { module_loader_ = loader; }
  void populate_kir_types(KirModule *module) const;

private:
  // StmtVisitor overrides. check_stmt() stashes the current expected return
  // type in stmt_expected_return_ and dispatches via accept(); each override
  // reads that member where it needs the enclosing function's return type.
  void visit(const ast::ExprStmt &stmt) override;
  void visit(const ast::TryCatchStmt &stmt) override;
  void visit(const ast::ReturnStmt &stmt) override;
  void visit(const ast::VarDeclStmt &stmt) override;
  void visit(const ast::UnpackDeclStmt &stmt) override;
  void visit(const ast::BlockStmt &stmt) override;
  void visit(const ast::IfStmt &stmt) override;
  void visit(const ast::GuardStmt &stmt) override;
  void visit(const ast::WhileStmt &stmt) override;
  void visit(const ast::ForStmt &stmt) override;
  void visit(const ast::BreakStmt &stmt) override;
  void visit(const ast::ContinueStmt &stmt) override;

  // ExprVisitor overrides — each forwards to the corresponding check_X()
  // helper and stores the result in expr_result_.
  void visit(const ast::IntLiteralExpr &x) override;
  void visit(const ast::CharLiteralExpr &x) override;
  void visit(const ast::FloatLiteralExpr &x) override;
  void visit(const ast::StringLiteralExpr &x) override;
  void visit(const ast::BoolLiteralExpr &x) override;
  void visit(const ast::NullLiteralExpr &x) override;
  void visit(const ast::ArrayLiteralExpr &x) override;
  void visit(const ast::MapLiteralExpr &x) override;
  void visit(const ast::IdentifierExpr &x) override;
  void visit(const ast::UnaryExpr &x) override;
  void visit(const ast::BinaryExpr &x) override;
  void visit(const ast::AssignExpr &x) override;
  void visit(const ast::CallExpr &x) override;
  void visit(const ast::PipeExpr &x) override;
  void visit(const ast::CastExpr &x) override;
  void visit(const ast::TernaryExpr &x) override;
  void visit(const ast::NullCoalesceExpr &x) override;
  void visit(const ast::PropagateExpr &x) override;
  void visit(const ast::BindingPattern &x) override;
  void visit(const ast::ArrayPattern &x) override;
  void visit(const ast::EnumPattern &x) override;
  void visit(const ast::StructPattern &x) override;
  void visit(const ast::MatchExpr &x) override;
  void visit(const ast::NamespaceAccessExpr &x) override;
  void visit(const ast::FieldAccessExpr &x) override;
  void visit(const ast::FieldAssignExpr &x) override;
  void visit(const ast::IndexExpr &x) override;
  void visit(const ast::IndexAssignExpr &x) override;
  void visit(const ast::StructLiteralExpr &x) override;

  void check_function(const ast::FunctionDecl &function);
  void check_stmt(const ast::Stmt &stmt, const Type &expected_return);
  // Per-expression-type check helpers. check_expr() dispatches to these; each
  // returns the inferred type of its node. Extracted from the former monolithic
  // check_expr dynamic_cast chain, one node type per method.
  Type check_int_literal(const ast::IntLiteralExpr &lit);
  Type check_char_literal(const ast::CharLiteralExpr &lit);
  Type check_float_literal(const ast::FloatLiteralExpr &lit);
  Type check_string_literal(const ast::StringLiteralExpr &lit);
  Type check_bool_literal(const ast::BoolLiteralExpr &lit);
  Type check_null_literal(const ast::NullLiteralExpr &lit);
  Type check_namespace_access(const ast::NamespaceAccessExpr &ns);
  Type check_identifier(const ast::IdentifierExpr &id);
  Type check_unary(const ast::UnaryExpr &unary);
  Type check_binary(const ast::BinaryExpr &binary);
  Type check_assign(const ast::AssignExpr &assign);
  Type check_binding_pattern(const ast::BindingPattern &binding);
  Type check_array_pattern(const ast::ArrayPattern &pat);
  Type check_struct_pattern(const ast::StructPattern &pat);
  Type check_match(const ast::MatchExpr &match);
  Type check_call(const ast::CallExpr &call);
  Type check_struct_literal(const ast::StructLiteralExpr &lit);
  Type check_field_access(const ast::FieldAccessExpr &fa);
  Type check_field_assign(const ast::FieldAssignExpr &fa);
  Type check_array_literal(const ast::ArrayLiteralExpr &lit);
  Type check_map_literal(const ast::MapLiteralExpr &lit);
  Type check_index(const ast::IndexExpr &idx);
  Type check_cast(const ast::CastExpr &cast);
  Type check_ternary(const ast::TernaryExpr &ternary);
  Type check_null_coalesce(const ast::NullCoalesceExpr &nc);
  Type check_propagate(const ast::PropagateExpr &prop);
  Type check_index_assign(const ast::IndexAssignExpr &idx);
  Type check_expr(const ast::Expr &expr);

  void push_scope();
  void pop_scope();
  void declare_var(const std::string &name, const Type &type, bool is_mutable, ast::SourceLocation loc = {});
  std::optional<Type> lookup_var(const std::string &name);
  std::optional<Type> lookup_type(const std::string &name) const;
  Type resolve_type_name(const std::string &name) const;
  Type resolve_type_expr(const ast::TypeExpr &expr, ast::SourceLocation loc = {});
  bool function_uses_concept_params(const ast::FunctionDecl &function) const;
  std::string type_match_key(const Type &type) const;
  bool type_satisfies_concept(const ast::ConceptDecl *concept_decl, const Type &concrete,
                              ast::SourceLocation loc);
  const ast::FunctionDecl *find_free_function_for_type(const std::string &name,
                                                       const std::string &key) const;
  std::optional<Type> lookup_concept_method(const std::string &concept_name,
                                            const std::string &method_name, const Type &arg_ty,
                                            ast::SourceLocation loc);
  std::optional<Type> lookup_ufcs_free_method(const std::string &method_name, const Type &receiver,
                                              const std::vector<ast::ExprPtr> &args,
                                              ast::SourceLocation loc);
  std::string mangle_name(const std::string &base, const std::vector<ast::TypeExpr> &args) const;
  void instantiate_generic_struct(const ast::StructDecl *decl, const std::vector<ast::TypeExpr> &args);
  void error_at(ast::SourceLocation location, std::string message);
  void warn_at(ast::SourceLocation location, std::string message);
  void check_fmt_args(const std::vector<ast::ExprPtr> &args, ast::SourceLocation location);
  void forward_declare_imported_types(const ParsedModule &mod);
  std::string resolve_module_qualified(const std::string &ns, const std::string &member) const;
  void open_imported_namespace(const std::string &module_id);

  struct VarInfo {
    Type type;
    bool is_mutable;
    bool used = false;
    ast::SourceLocation location;
  };

  struct ActiveBorrow {
    std::string referent;
    bool mut = false;
    std::size_t scope_depth = 0;
  };

  void register_borrow(const std::string &referent, bool mut, ast::SourceLocation loc);
  void release_mut_borrow(const std::string &referent);
  void check_referent_access(const std::string &name, ast::SourceLocation loc, bool mutating);
  void release_call_argument_borrows(const std::vector<ast::ExprPtr> &args);
  static std::optional<std::string> referent_name_from_lvalue(const ast::Expr &expr);
  bool is_mutable_lvalue(const ast::Expr &expr) const;
  static bool is_reference_type(const Type &type);
  void check_reference_escape(const Type &value_type, ast::SourceLocation loc);

  std::vector<ActiveBorrow> active_borrows_;
  std::vector<std::unordered_map<std::string, VarInfo>> scopes_;
  std::unordered_map<std::string, Type> type_registry_;
  SemanticContext sema_;
  std::vector<const ast::FunctionDecl *> free_functions_;
  std::unordered_set<std::string> instantiated_;

  struct MethodInfo {
    const ast::FunctionDecl *decl;
    std::string target_type;
  };
  std::unordered_map<std::string, MethodInfo> method_registry_;

  std::vector<TypeError> errors_;
  std::unordered_map<std::string, KirFunctionSig> kir_function_sigs_;
  std::unordered_set<std::string> imported_bare_names_;  // for selective imports
  // Per-namespace exported / private symbol names, populated when an import is
  // processed. Used to give a precise diagnostic for `using mod { sym };` when
  // a symbol is missing from the module or exists but is not pub.
  std::unordered_map<std::string, std::unordered_set<std::string>> module_public_symbols_;
  std::unordered_map<std::string, std::unordered_set<std::string>> module_private_symbols_;
  int loop_depth_ = 0;
  const ast::ExprStmt *implicit_return_stmt_ = nullptr;
  Type implicit_return_value_type_{TypeKind::Void};
  Type stmt_expected_return_{TypeKind::Void};
  Type expr_result_{TypeKind::Void};
  ModuleLoader *module_loader_ = nullptr;
};

} // namespace kinglet
