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
} // namespace kinglet

#include <functional>
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
  SemanticContext &sema() { return sema_; }
  const SemanticContext &sema() const { return sema_; }

  // Tracks whether a local has a definite value yet. Unassigned means the
  // declaration had no initializer and the type has no language-level
  // default (see is_defaultable_type()); reading it before an assignment
  // covers every path is a compile error. PartiallyInitialized applies only
  // to struct locals initialized field-by-field: some fields have been
  // assigned but not all of them, so a field read is only valid for a field
  // already in initialized_fields, and a whole-value read is not tracked in
  // this first implementation (see type_checker.cc's VarDeclStmt/field
  // access handling for the exact scope of what is and isn't covered).
  enum class InitState { Unassigned, Initialized, PartiallyInitialized };

  struct VarInfo {
    Type type;
    bool is_mutable;
    bool used = false;
    bool transferred = false;
    InitState init_state = InitState::Initialized;
    std::unordered_set<std::string> initialized_fields;
    ast::SourceLocation location;
  };

  // ── Function overloading support ───────────────────────────────────

  struct OverloadEntry {
    Type func_type;
    std::string mangled_name;
    int arity; // cached for fast filtering
  };

  using OverloadSet = std::vector<OverloadEntry>;

  struct MethodInfo {
    const ast::FunctionDecl *decl;
    std::string target_type;
  };

  // Completion callback for ADR 0024 phase C2. When set, the TypeChecker
  // invokes this callback every time it evaluates a CompletionMarkerExpr,
  // passing the resolved receiver type, the live scope stack, the method
  // registry, and the type registry — the real data perch currently
  // approximates with its own hand-rolled walk_access_chain()/member_type().
  // The callback is optional; when unset, CompletionMarkerExpr silently
  // resolves to Void (LSP-mode parses don't depend on its result).
  struct CompletionContext {
    Type receiver_type = Type(TypeKind::Void);
    // Innermost-first copy of the scope stack at the marker. Copied because
    // the TypeChecker may pop scopes immediately after the visit() call.
    std::vector<std::unordered_map<std::string, VarInfo>> scopes;
    const std::unordered_map<std::string, MethodInfo> *method_registry = nullptr;
    const std::unordered_map<std::string, Type> *type_registry = nullptr;
  };
  using CompletionCallback = std::function<void(const CompletionContext &)>;
  void set_completion_callback(CompletionCallback cb) { completion_callback_ = std::move(cb); }

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
  void visit(const ast::BlockExpr &x) override;
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
  void visit(const ast::CompletionMarkerExpr &x) override;

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
  Type check_block_expr(const ast::BlockExpr &block);
  Type check_null_coalesce(const ast::NullCoalesceExpr &nc);
  Type check_propagate(const ast::PropagateExpr &prop);
  Type check_index_assign(const ast::IndexAssignExpr &idx);
  Type check_expr(const ast::Expr &expr);

  void push_scope();
  void pop_scope();
  void declare_var(const std::string &name, const Type &type, bool is_mutable,
                   ast::SourceLocation loc = {}, InitState init_state = InitState::Initialized);
  // True when a bare declaration with no initializer is immediately valid
  // for this type: `T?`, `string`, arrays, and maps all have a language-
  // level default value (null / "" / empty array / empty map). Everything
  // else (scalars, enums, structs with no applicable zero-argument `@init`
  // path) starts Unassigned and must be assigned on every path before it is
  // read -- see visit(VarDeclStmt) for where this decides the declared
  // InitState, and check_int_literal()'s caller in the .cc file for why the
  // checker must not fabricate a default int/float/bool/char value the way
  // lowering's old emit_default_value() did.
  static bool is_defaultable_type(const Type &type);
  // Field names of a struct type, or an empty set for any other type. Used
  // to decide when every field of a struct local has been assigned (at
  // which point the whole variable becomes Initialized) and, during
  // if/else merging, to treat an Initialized struct branch as having
  // touched every field for intersection purposes.
  static std::unordered_set<std::string> struct_field_names(const Type &type);
  // Marks `name` as definitely assigned as a whole value. Called from
  // check_assign() for an ordinary `name = expr` write.
  void mark_initialized(const std::string &name);
  // Marks a single struct field as definitely assigned. Called from
  // check_field_assign() for `name.field = expr`. Promotes `name` to fully
  // Initialized once every field of its struct type has been touched this
  // way, so later whole-value reads of `name` succeed without requiring a
  // separate literal/constructor initialization.
  void mark_field_initialized(const std::string &name, const std::string &field);
  // Errors if reading `name` as a whole value is not yet definite. Called
  // from check_identifier() for every bare-identifier read.
  void check_definite_assignment_read(const std::string &name, ast::SourceLocation loc);
  // Errors if reading a specific struct field is not yet definite. Called
  // from check_field_access() when the object being accessed is a bare
  // local struct variable that is not yet fully Initialized.
  void check_field_definite_assignment_read(const std::string &name, const std::string &field,
                                            ast::SourceLocation loc);
  // Nonzero while resolving the immediate object of `obj.field` when the
  // field access itself is the operation that decides what part of `obj` is
  // touched. For `obj.field = value`, an Unassigned struct local is allowed
  // because the write may initialize that field. For `obj.field` reads, the
  // object lookup is allowed and the checker then validates the specific
  // field path with check_field_definite_assignment_read(). Only ever
  // incremented/decremented directly around that one check_expr() call, so
  // it cannot leak into unrelated nested reads.
  int suppress_definite_assignment_for_field_write_ = 0;
  // Definite-assignment state snapshot, keyed by variable name: (whole-value
  // state, the set of struct fields touched so far). Used by if/else
  // branch merging and by loops to save/restore/merge just the
  // definite-assignment bookkeeping without disturbing borrow/transfer
  // state or re-running any checks.
  using DefiniteAssignmentSnapshot =
      std::unordered_map<std::string, std::pair<InitState, std::unordered_set<std::string>>>;
  DefiniteAssignmentSnapshot snapshot_definite_assignment_state();
  void restore_definite_assignment_state(const DefiniteAssignmentSnapshot &snapshot);
  // Writes the merge of `then_snapshot` and `else_snapshot` into the live
  // VarInfo objects: a variable/field is Initialized after the branch only
  // if it is initialized on both arms (an absent `else` passes the pre-if
  // snapshot here as `else_snapshot`, so "no assignment happened" merges
  // exactly like a branch that assigned nothing).
  void merge_definite_assignment_snapshots(const DefiniteAssignmentSnapshot &then_snapshot,
                                           const DefiniteAssignmentSnapshot &else_snapshot);
  std::optional<Type> lookup_var(const std::string &name);
  VarInfo *find_var_info(const std::string &name);
  std::optional<Type> lookup_type(const std::string &name) const;
  Type resolve_type_name(const std::string &name) const;
  Type resolve_type_expr(const ast::TypeExpr &expr, ast::SourceLocation loc = {});
  bool function_uses_concept_params(const ast::FunctionDecl &function) const;
  // Infer the return type of an `auto`-returning function by walking its body's
  // return statements and unifying their expression types. Returns Void when the
  // function has no value-returning `return`. Runs in a scratch scope with the
  // function's parameters declared, so it must be called with no active scope
  // state that it should not see. Diagnostics for conflicting return types are
  // emitted here.
  Type infer_auto_return_type(const ast::FunctionDecl &function);
  // Recursively collect the types of every value-bearing `return` in a stmt.
  void collect_return_types(const ast::Stmt &stmt, std::vector<Type> &out);
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
  void instantiate_generic_struct(const ast::StructDecl *decl,
                                  const std::vector<ast::TypeExpr> &args);
  void error_at(ast::SourceLocation location, std::string message);
  void warn_at(ast::SourceLocation location, std::string message);
  void check_fmt_args(const std::vector<ast::ExprPtr> &args, ast::SourceLocation location);
  void forward_declare_imported_types(const ParsedModule &mod, const std::string &qualifier = "");
  std::string resolve_module_qualified(const std::string &ns, const std::string &member) const;
  std::string resolve_qualified_type_name(const std::string &name) const;
  void open_imported_namespace(const std::string &module_id);

  struct ActiveBorrow {
    std::string referent;
    bool mut = false;
    std::size_t scope_depth = 0;
  };

  void register_borrow(const std::string &referent, bool mut, ast::SourceLocation loc);
  void release_mut_borrow(const std::string &referent);
  void check_referent_access(const std::string &name, ast::SourceLocation loc, bool mutating);
  // Computes a call/init argument's type for parameter/candidate matching
  // without prematurely classifying an explicit `&expr` marker as a borrow.
  // See the .cc definition for why this must run before a target parameter
  // type is chosen (overload resolution, generic instantiation).
  Type check_call_arg_type(const ast::Expr &arg_expr);
  // Strips an explicit `&expr` marker (ADR 0028 D3) down to the wrapped
  // expression, so borrow classification runs against the actual referent
  // whether or not the caller wrote the marker.
  static const ast::Expr &strip_borrow_marker(const ast::Expr &expr);
  // Classifies and (when appropriate) registers a borrow for a single call
  // argument against its resolved parameter type. Returns true if the
  // argument was accepted as a reference-typed target (shared or exclusive);
  // false if the target was not reference-typed at all (a bare-T parameter),
  // in which case no borrow classification applies (D4's transfer/copy path
  // handles that argument instead).
  bool check_borrow_argument(const ast::Expr &arg_expr, const Type &param_type,
                             ast::SourceLocation loc);
  // Runs check_borrow_argument() over every argument against its resolved
  // parameter type, then releases any exclusive borrows taken for the
  // duration of the call (ADR 0028 D10's reborrow-for-the-callee rule).
  // Takes raw pointers (not ast::ExprPtr) so callers can pass a slice of a
  // larger argument list (e.g. skipping a UFCS receiver at index 0) without
  // needing to copy non-copyable unique_ptrs.
  void check_call_argument_borrows(const std::vector<const ast::Expr *> &args,
                                   const std::vector<Type> &param_types);
  void check_call_argument_borrows(const std::vector<ast::ExprPtr> &args,
                                   const std::vector<Type> &param_types);
  static std::optional<std::string> referent_name_from_lvalue(const ast::Expr &expr);
  // Builds a place-based borrow path (e.g. "p.left", "arr[]") instead of
  // stripping down to the root variable name, so disjoint field borrows
  // within the same variable do not conflict (ADR 0028 D12-2).
  static std::optional<std::string> borrow_path(const ast::Expr &expr);
  // Returns true when two borrow paths conflict: equal paths always
  // conflict; otherwise one path being a prefix of the other at a
  // non-identifier boundary (dot or bracket) means the wider path
  // covers the narrower one and they cannot coexist.
  static bool paths_conflict(std::string_view a, std::string_view b);
  bool is_mutable_lvalue(const ast::Expr &expr) const;
  static bool is_reference_type(const Type &type);
  void check_reference_escape(const Type &value_type, ast::SourceLocation loc);

  std::vector<ActiveBorrow> active_borrows_;
  std::vector<std::unordered_map<std::string, VarInfo>> scopes_;
  std::unordered_map<std::string, Type> type_registry_;
  SemanticContext sema_;
  // Function overload sets keyed by plain name, registered during pass 1
  // for overload resolution in pass 2.
  std::unordered_map<std::string, OverloadSet> function_overloads_;
  std::vector<const ast::FunctionDecl *> free_functions_;
  std::unordered_set<std::string> instantiated_;

  std::unordered_map<std::string, MethodInfo> method_registry_;

  std::vector<TypeError> errors_;
  std::unordered_map<std::string, KirFunctionSig> kir_function_sigs_;
  std::unordered_set<std::string> imported_bare_names_; // for selective imports
  // Owns the built-in io::reader / io::writer concept declarations so the
  // raw pointers in concept_registry_ remain valid for the checker's lifetime.
  std::vector<std::unique_ptr<const ast::ConceptDecl>> builtin_concepts_;
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
  int allow_fallible_cast_depth_ = 0;
  ModuleLoader *module_loader_ = nullptr;
  CompletionCallback completion_callback_;
};

} // namespace kinglet
