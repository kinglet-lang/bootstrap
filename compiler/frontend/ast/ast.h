// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace kinglet::ast {

enum class BinaryOp : std::uint8_t {
  Add,
  Sub,
  Mul,
  Div,
  Mod,
  Eq,
  Neq,
  Lt,
  Gt,
  Le,
  Ge,
  And,
  Or,
  BitAnd,
  BitOr,
  BitXor,
  Shl,
  Shr,
};

enum class UnaryOp : std::uint8_t {
  Neg,
  Not,
  BitNot,
  Ref,
  MutRef,
};

enum class AssignOp : std::uint8_t {
  Assign,
  AddAssign,
  SubAssign,
  MulAssign,
  DivAssign,
};

const char *binary_op_name(BinaryOp op);
const char *unary_op_name(UnaryOp op);
const char *assign_op_name(AssignOp op);

struct SourceLocation {
  int line = 0;
  int column = 0;
  int length = 1;
};

struct Node {
  explicit Node(SourceLocation location);
  virtual ~Node() = default;
  virtual void print(std::ostream &out, int indent = 0) const = 0;

  SourceLocation location;
};

// ---------------------------------------------------------------------------
// Visitor infrastructure
//
// Const reading visitors: each concrete node implements accept() to dispatch
// to the matching visit() overload. These cover the read-only passes (type
// checking, compilation) that previously used long dynamic_cast chains.
// Mutating passes that rewrite owning pointers (e.g. pipe desugaring) keep
// their own recursive walk and do not use these bases.
//
// Adding a new AST node: declare it below, add a pure-virtual visit() to the
// matching visitor, implement accept() on the node — the compiler then forces
// every visitor subclass to handle it.
// ---------------------------------------------------------------------------

struct IntLiteralExpr;
struct CharLiteralExpr;
struct FloatLiteralExpr;
struct StringLiteralExpr;
struct BoolLiteralExpr;
struct NullLiteralExpr;
struct ArrayLiteralExpr;
struct MapLiteralExpr;
struct IdentifierExpr;
struct UnaryExpr;
struct BinaryExpr;
struct AssignExpr;
struct CallExpr;
struct PipeExpr;
struct CastExpr;
struct TernaryExpr;
struct NullCoalesceExpr;
struct PropagateExpr;
struct BindingPattern;
struct ArrayPattern;
struct EnumPattern;
struct StructPattern;
struct MatchExpr;
struct NamespaceAccessExpr;
struct FieldAccessExpr;
struct FieldAssignExpr;
struct IndexExpr;
struct IndexAssignExpr;
struct StructLiteralExpr;

struct ExprStmt;
struct TryCatchStmt;
struct ReturnStmt;
struct VarDeclStmt;
struct UnpackDeclStmt;
struct BlockStmt;
struct IfStmt;
struct GuardStmt;
struct WhileStmt;
struct ForStmt;
struct BreakStmt;
struct ContinueStmt;

struct FunctionDecl;
struct ImportDecl;
struct LogicalImportDecl;
struct ExportModuleDecl;
struct ImportBlockDecl;
struct UsingDecl;
struct UsingAliasDecl;
struct StructDecl;
struct EnumDecl;
struct ConceptDecl;
struct TopLevelStmtDecl;

struct ExprVisitor {
  virtual ~ExprVisitor() = default;
  virtual void visit(const IntLiteralExpr &) = 0;
  virtual void visit(const CharLiteralExpr &) = 0;
  virtual void visit(const FloatLiteralExpr &) = 0;
  virtual void visit(const StringLiteralExpr &) = 0;
  virtual void visit(const BoolLiteralExpr &) = 0;
  virtual void visit(const NullLiteralExpr &) = 0;
  virtual void visit(const ArrayLiteralExpr &) = 0;
  virtual void visit(const MapLiteralExpr &) = 0;
  virtual void visit(const IdentifierExpr &) = 0;
  virtual void visit(const UnaryExpr &) = 0;
  virtual void visit(const BinaryExpr &) = 0;
  virtual void visit(const AssignExpr &) = 0;
  virtual void visit(const CallExpr &) = 0;
  virtual void visit(const PipeExpr &) = 0;
  virtual void visit(const CastExpr &) = 0;
  virtual void visit(const TernaryExpr &) = 0;
  virtual void visit(const NullCoalesceExpr &) = 0;
  virtual void visit(const PropagateExpr &) = 0;
  virtual void visit(const BindingPattern &) = 0;
  virtual void visit(const ArrayPattern &) = 0;
  virtual void visit(const EnumPattern &) = 0;
  virtual void visit(const StructPattern &) = 0;
  virtual void visit(const MatchExpr &) = 0;
  virtual void visit(const NamespaceAccessExpr &) = 0;
  virtual void visit(const FieldAccessExpr &) = 0;
  virtual void visit(const FieldAssignExpr &) = 0;
  virtual void visit(const IndexExpr &) = 0;
  virtual void visit(const IndexAssignExpr &) = 0;
  virtual void visit(const StructLiteralExpr &) = 0;
};

struct StmtVisitor {
  virtual ~StmtVisitor() = default;
  virtual void visit(const ExprStmt &) = 0;
  virtual void visit(const TryCatchStmt &) = 0;
  virtual void visit(const ReturnStmt &) = 0;
  virtual void visit(const VarDeclStmt &) = 0;
  virtual void visit(const UnpackDeclStmt &) = 0;
  virtual void visit(const BlockStmt &) = 0;
  virtual void visit(const IfStmt &) = 0;
  virtual void visit(const GuardStmt &) = 0;
  virtual void visit(const WhileStmt &) = 0;
  virtual void visit(const ForStmt &) = 0;
  virtual void visit(const BreakStmt &) = 0;
  virtual void visit(const ContinueStmt &) = 0;
};

struct DeclVisitor {
  virtual ~DeclVisitor() = default;
  virtual void visit(const FunctionDecl &) = 0;
  virtual void visit(const ImportDecl &) = 0;
  virtual void visit(const LogicalImportDecl &) = 0;
  virtual void visit(const ExportModuleDecl &) = 0;
  virtual void visit(const ImportBlockDecl &) = 0;
  virtual void visit(const UsingDecl &) = 0;
  virtual void visit(const UsingAliasDecl &) = 0;
  virtual void visit(const StructDecl &) = 0;
  virtual void visit(const EnumDecl &) = 0;
  virtual void visit(const ConceptDecl &) = 0;
  virtual void visit(const TopLevelStmtDecl &) = 0;
};

struct Expr : Node {
  using Node::Node;
  virtual void accept(ExprVisitor &v) const = 0;
};
struct Stmt : Node {
  using Node::Node;
  virtual void accept(StmtVisitor &v) const = 0;
};
struct Decl : Node {
  using Node::Node;
  virtual void accept(DeclVisitor &v) const = 0;
};

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using DeclPtr = std::unique_ptr<Decl>;

struct TypeExpr {
  std::string name;
  std::vector<TypeExpr> type_args;
  std::string to_string() const;
};

struct IntLiteralExpr final : Expr {
  IntLiteralExpr(SourceLocation location, int64_t value, std::string width_suffix = {});
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  int64_t value;
  std::string width_suffix;
};

struct CharLiteralExpr final : Expr {
  CharLiteralExpr(SourceLocation location, int8_t value);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  int8_t value;
};

struct FloatLiteralExpr final : Expr {
  FloatLiteralExpr(SourceLocation location, double value, std::string width_suffix = {});
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  double value;
  std::string width_suffix;
};

struct StringLiteralExpr final : Expr {
  StringLiteralExpr(SourceLocation location, std::string value);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  std::string value;
};

struct BoolLiteralExpr final : Expr {
  BoolLiteralExpr(SourceLocation location, bool value);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  bool value;
};

struct NullLiteralExpr final : Expr {
  explicit NullLiteralExpr(SourceLocation location);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }
};

struct ArrayLiteralExpr final : Expr {
  ArrayLiteralExpr(SourceLocation location, std::vector<ExprPtr> elements);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  std::vector<ExprPtr> elements;
};

// A map literal: `{}` (empty) or `{k1: v1, k2: v2}`. Parallel key/value lists.
struct MapLiteralExpr final : Expr {
  MapLiteralExpr(SourceLocation location, std::vector<ExprPtr> keys,
                 std::vector<ExprPtr> values);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  std::vector<ExprPtr> keys;
  std::vector<ExprPtr> values;
};

struct IdentifierExpr final : Expr {
  IdentifierExpr(SourceLocation location, std::string name);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  std::string name;
};

struct UnaryExpr final : Expr {
  UnaryExpr(SourceLocation location, UnaryOp op, ExprPtr right);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  UnaryOp op;
  ExprPtr right;
};

struct BinaryExpr final : Expr {
  BinaryExpr(SourceLocation location, ExprPtr left, BinaryOp op, ExprPtr right);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  ExprPtr left;
  BinaryOp op;
  ExprPtr right;
};

struct AssignExpr final : Expr {
  AssignExpr(SourceLocation location, std::string name, AssignOp op, ExprPtr value);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  std::string name;
  AssignOp op;
  ExprPtr value;
};

struct CallExpr final : Expr {
  CallExpr(SourceLocation location, ExprPtr callee, std::vector<TypeExpr> type_args,
           std::vector<ExprPtr> args);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  ExprPtr callee;
  std::vector<TypeExpr> type_args;
  std::vector<ExprPtr> args;
};

// Pipeline operator: `left |> right` (right is invoked with left as the first argument).
struct PipeExpr final : Expr {
  PipeExpr(SourceLocation location, ExprPtr left, ExprPtr right);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  ExprPtr left;
  ExprPtr right;
};

struct CastExpr final : Expr {
  CastExpr(SourceLocation location, TypeExpr target_type, ExprPtr value);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  TypeExpr target_type;
  ExprPtr value;
};

// Ternary conditional: `cond ? then : else`.
struct TernaryExpr final : Expr {
  TernaryExpr(SourceLocation location, ExprPtr condition, ExprPtr then_expr, ExprPtr else_expr);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  ExprPtr condition;
  ExprPtr then_expr;
  ExprPtr else_expr;
};

// `e ?: f` (bare) or `e ?: let err => f` — null / CastError Elvis (selfhost syntax).
struct NullCoalesceExpr final : Expr {
  NullCoalesceExpr(SourceLocation location, ExprPtr left, std::string err_binding, ExprPtr right);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  ExprPtr left;
  std::string err_binding;
  ExprPtr right;
};

// Postfix `?` propagation: in a `T?`-returning function, returns null on
// failure of the operand; inside a `try` block, transfers control to the
// matching `catch`.
struct PropagateExpr final : Expr {
  PropagateExpr(SourceLocation location, ExprPtr value);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  ExprPtr value;
};

struct BindingPattern final : Expr {
  BindingPattern(SourceLocation location, std::string name);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  std::string name;
};

struct ArrayPattern final : Expr {
  ArrayPattern(SourceLocation location, std::vector<ExprPtr> elements);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  std::vector<ExprPtr> elements;
};

struct EnumPattern final : Expr {
  EnumPattern(SourceLocation location, std::string enum_name, std::string variant_name,
              std::vector<ExprPtr> fields);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  std::string enum_name;
  std::string variant_name;
  std::vector<ExprPtr> fields;
};

struct StructPatternField {
  std::string name;
  ExprPtr pattern;
};

struct StructPattern final : Expr {
  StructPattern(SourceLocation location, std::string struct_name,
                std::vector<StructPatternField> fields);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  std::string struct_name;
  std::vector<StructPatternField> fields;
};

struct MatchArm {
  ExprPtr pattern;
  ExprPtr guard;
  ExprPtr body;
};

struct CatchArm {
  TypeExpr error_type;
  std::string binding_name;
  StmtPtr body;
};

struct MatchExpr final : Expr {
  MatchExpr(SourceLocation location, ExprPtr value, std::vector<MatchArm> arms);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  ExprPtr value;
  std::vector<MatchArm> arms;
};

struct ExprStmt final : Stmt {
  ExprStmt(SourceLocation location, ExprPtr expr);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(StmtVisitor &v) const override { v.visit(*this); }

  ExprPtr expr;
};

struct TryCatchStmt final : Stmt {
  TryCatchStmt(SourceLocation location, StmtPtr body, std::vector<CatchArm> catches);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(StmtVisitor &v) const override { v.visit(*this); }

  StmtPtr body;
  std::vector<CatchArm> catches;
};

struct ReturnStmt final : Stmt {
  ReturnStmt(SourceLocation location, ExprPtr value);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(StmtVisitor &v) const override { v.visit(*this); }

  ExprPtr value;
};

struct VarDeclStmt final : Stmt {
  VarDeclStmt(SourceLocation location, std::string storage, TypeExpr type, std::string name,
              ExprPtr init);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(StmtVisitor &v) const override { v.visit(*this); }

  std::string storage;
  TypeExpr type;
  std::string name;
  ExprPtr init;
};

struct UnpackDeclStmt final : Stmt {
  UnpackDeclStmt(SourceLocation location, std::vector<std::string> names,
                 std::string rest_name, ExprPtr init);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(StmtVisitor &v) const override { v.visit(*this); }

  std::vector<std::string> names;
  std::string rest_name;
  ExprPtr init;
};

struct BlockStmt final : Stmt {
  BlockStmt(SourceLocation location, std::vector<StmtPtr> statements);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(StmtVisitor &v) const override { v.visit(*this); }

  std::vector<StmtPtr> statements;
};

struct IfStmt final : Stmt {
  IfStmt(SourceLocation location, ExprPtr condition, StmtPtr then_branch, StmtPtr else_branch);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(StmtVisitor &v) const override { v.visit(*this); }

  ExprPtr condition;
  StmtPtr then_branch;
  StmtPtr else_branch;
};

struct GuardStmt final : Stmt {
  GuardStmt(SourceLocation location, ExprPtr condition, StmtPtr else_body);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(StmtVisitor &v) const override { v.visit(*this); }

  ExprPtr condition;
  StmtPtr else_body;
};

struct WhileStmt final : Stmt {
  WhileStmt(SourceLocation location, ExprPtr condition, StmtPtr body);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(StmtVisitor &v) const override { v.visit(*this); }

  ExprPtr condition;
  StmtPtr body;
};

struct ForStmt final : Stmt {
  ForStmt(SourceLocation location, StmtPtr init, ExprPtr condition, StmtPtr step, StmtPtr body);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(StmtVisitor &v) const override { v.visit(*this); }

  StmtPtr init;
  ExprPtr condition;
  StmtPtr step;
  StmtPtr body;
};

struct BreakStmt final : Stmt {
  explicit BreakStmt(SourceLocation location);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(StmtVisitor &v) const override { v.visit(*this); }
};

struct ContinueStmt final : Stmt {
  explicit ContinueStmt(SourceLocation location);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(StmtVisitor &v) const override { v.visit(*this); }
};

struct Parameter {
  TypeExpr type;
  std::string name;
};

struct FunctionDecl final : Decl {
  FunctionDecl(SourceLocation location, TypeExpr return_type, std::string name,
               std::vector<std::string> type_params, std::vector<Parameter> params, StmtPtr body);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(DeclVisitor &v) const override { v.visit(*this); }

  TypeExpr return_type;
  std::string name;
  std::vector<std::string> type_params;
  std::vector<Parameter> params;
  StmtPtr body;
  bool is_public = false;
};

struct ImportDecl final : Decl {
  ImportDecl(SourceLocation location, std::string path, std::string alias,
             std::vector<std::string> selected_symbols);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(DeclVisitor &v) const override { v.visit(*this); }

  std::string path;
  std::string alias;
  std::vector<std::string> selected_symbols;
};

struct LogicalImportDecl final : Decl {
  LogicalImportDecl(SourceLocation location, std::string module_id);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(DeclVisitor &v) const override { v.visit(*this); }

  std::string module_id;
};

struct ExportModuleDecl final : Decl {
  ExportModuleDecl(SourceLocation location, std::string name);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(DeclVisitor &v) const override { v.visit(*this); }

  std::string name;
};

struct ImportBlockDecl final : Decl {
  ImportBlockDecl(SourceLocation location, std::vector<DeclPtr> imports);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(DeclVisitor &v) const override { v.visit(*this); }

  std::vector<DeclPtr> imports;
};

struct UsingDecl final : Decl {
  UsingDecl(SourceLocation location, std::string namespace_name, bool is_namespace);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(DeclVisitor &v) const override { v.visit(*this); }

  std::string namespace_name;
  bool is_namespace;
};

struct UsingAliasDecl final : Decl {
  UsingAliasDecl(SourceLocation location, std::string alias, std::string module_id);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(DeclVisitor &v) const override { v.visit(*this); }

  std::string alias;
  std::string module_id;
};

struct NamespaceAccessExpr final : Expr {
  NamespaceAccessExpr(SourceLocation location, std::string namespace_name,
                      std::string member_name);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  std::string namespace_name;
  std::string member_name;
};

struct FieldAccessExpr final : Expr {
  FieldAccessExpr(SourceLocation location, ExprPtr object, std::string field_name);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  ExprPtr object;
  std::string field_name;
};

struct FieldAssignExpr final : Expr {
  FieldAssignExpr(SourceLocation location, ExprPtr object, std::string field_name, ExprPtr value);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  ExprPtr object;
  std::string field_name;
  ExprPtr value;
};

struct IndexExpr final : Expr {
  IndexExpr(SourceLocation location, ExprPtr object, ExprPtr index);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  ExprPtr object;
  ExprPtr index;
};

struct IndexAssignExpr final : Expr {
  IndexAssignExpr(SourceLocation location, ExprPtr object, ExprPtr index, ExprPtr value);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  ExprPtr object;
  ExprPtr index;
  ExprPtr value;
};

struct FieldDef {
  TypeExpr type;
  std::string name;
};

struct StructLiteralExpr final : Expr {
  struct FieldInit {
    std::string name;
    ExprPtr value;
  };
  StructLiteralExpr(SourceLocation location, TypeExpr struct_type,
                    std::vector<FieldInit> fields);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(ExprVisitor &v) const override { v.visit(*this); }

  TypeExpr struct_type;
  std::vector<FieldInit> fields;
};

struct StructDecl final : Decl {
  StructDecl(SourceLocation location, std::string name, std::vector<std::string> type_params,
             std::vector<FieldDef> fields);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(DeclVisitor &v) const override { v.visit(*this); }

  std::string name;
  std::vector<std::string> type_params;
  std::vector<FieldDef> fields;
  bool is_public = false;
};

struct EnumVariantDecl {
  std::string name;
  std::vector<TypeExpr> param_types;
};

struct EnumDecl final : Decl {
  EnumDecl(SourceLocation location, std::string name, std::vector<EnumVariantDecl> variants);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(DeclVisitor &v) const override { v.visit(*this); }

  std::string name;
  std::vector<EnumVariantDecl> variants;
  bool is_public = false;
};

struct ConceptMethodDecl {
  TypeExpr return_type;
  std::string name;
  std::vector<Parameter> params;
};

struct ConceptDecl final : Decl {
  ConceptDecl(SourceLocation location, std::string name,
              std::vector<std::string> type_params,
              std::vector<ConceptMethodDecl> methods);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(DeclVisitor &v) const override { v.visit(*this); }

  std::string name;
  std::vector<std::string> type_params;
  std::vector<ConceptMethodDecl> methods;
  bool is_public = false;
};

struct TopLevelStmtDecl final : Decl {
  TopLevelStmtDecl(SourceLocation location, StmtPtr stmt);
  void print(std::ostream &out, int indent = 0) const override;
  void accept(DeclVisitor &v) const override { v.visit(*this); }

  StmtPtr stmt;
};

struct Program final : Node {
  explicit Program(std::vector<DeclPtr> declarations);
  void print(std::ostream &out, int indent = 0) const override;

  std::vector<DeclPtr> declarations;
};

void desugar_pipes(Program &program);

} // namespace kinglet::ast
