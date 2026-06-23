// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "frontend/ast/ast.h"
#include "backend/compiler/bytecode_emitter.h"
#include "ir/kir.h"
#include "ir/kir_recorder.h"
#include "frontend/module/module_loader.h"
#include "backend/vm/chunk.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kinglet {

struct CompileError {
  ast::SourceLocation location;
  std::string message;
};

struct CompileWarning {
  ast::SourceLocation location;
  std::string message;
};

struct CompileResult {
  Chunk chunk;
  KirModule kir;
  std::vector<CompileError> errors;
  std::vector<CompileWarning> warnings;
};

class Compiler : public ast::ExprVisitor {
public:
  CompileResult compile(const ast::Program &program);
  CompileResult compile_module(const ast::Program &program);
  void set_module_loader(ModuleLoader *loader) { module_loader_ = loader; }
  void set_entry_source_path(std::string path) { entry_source_path_ = std::move(path); }

private:
  struct Local {
    std::string name;
    bool is_mutable = true;
    enum class SlotKind { Value, Ref, MutRef } slot_kind = SlotKind::Value;
  };

  struct LoopInfo {
    std::vector<std::size_t> break_jumps;
    std::vector<std::size_t> continue_jumps;
  };

  void push_scope();
  void pop_scope();

  void compile_function(const ast::FunctionDecl &function, const std::string &lookup_name = "");
  void compile_stmt(const ast::Stmt &stmt);
  void compile_expr(const ast::Expr &expr);
  void compile_assignment(const ast::AssignExpr &assign);

  // Per-node compile helpers, extracted from the former monolithic compile_expr.
  void compile_int_literal(const ast::IntLiteralExpr &lit);
  void compile_char_literal(const ast::CharLiteralExpr &lit);
  void compile_float_literal(const ast::FloatLiteralExpr &lit);
  void compile_string_literal(const ast::StringLiteralExpr &lit);
  void compile_bool_literal(const ast::BoolLiteralExpr &lit);
  void compile_null_literal(const ast::NullLiteralExpr &lit);
  void compile_unary(const ast::UnaryExpr &unary);
  void compile_identifier(const ast::IdentifierExpr &id);
  void compile_assign_expr(const ast::AssignExpr &assign);
  void compile_binary(const ast::BinaryExpr &binary);
  void compile_call(const ast::CallExpr &call);
  void compile_match(const ast::MatchExpr &match);
  void compile_namespace_access(const ast::NamespaceAccessExpr &ns);
  void compile_struct_literal(const ast::StructLiteralExpr &lit);
  void compile_field_access(const ast::FieldAccessExpr &fa);
  void compile_field_assign(const ast::FieldAssignExpr &fa);
  void compile_array_literal(const ast::ArrayLiteralExpr &lit);
  void compile_map_literal(const ast::MapLiteralExpr &lit);
  void compile_index(const ast::IndexExpr &idx);
  void compile_index_assign(const ast::IndexAssignExpr &idx);
  void compile_cast(const ast::CastExpr &cast);
  void compile_ternary(const ast::TernaryExpr &ternary);
  void compile_null_coalesce(const ast::NullCoalesceExpr &nc);
  void compile_propagate(const ast::PropagateExpr &prop);

  // ExprVisitor overrides — each forwards to the corresponding compile_X().
  void visit(const ast::IntLiteralExpr &x) override;
  void visit(const ast::CharLiteralExpr &x) override;
  void visit(const ast::FloatLiteralExpr &x) override;
  void visit(const ast::StringLiteralExpr &x) override;
  void visit(const ast::BoolLiteralExpr &x) override;
  void visit(const ast::UnaryExpr &x) override;
  void visit(const ast::IdentifierExpr &x) override;
  void visit(const ast::AssignExpr &x) override;
  void visit(const ast::BinaryExpr &x) override;
  void visit(const ast::CallExpr &x) override;
  void visit(const ast::MatchExpr &x) override;
  void visit(const ast::NamespaceAccessExpr &x) override;
  void visit(const ast::StructLiteralExpr &x) override;
  void visit(const ast::FieldAccessExpr &x) override;
  void visit(const ast::FieldAssignExpr &x) override;
  void visit(const ast::ArrayLiteralExpr &x) override;
  void visit(const ast::MapLiteralExpr &x) override;
  void visit(const ast::IndexExpr &x) override;
  void visit(const ast::IndexAssignExpr &x) override;
  void visit(const ast::CastExpr &x) override;
  void visit(const ast::TernaryExpr &x) override;
  void visit(const ast::NullCoalesceExpr &x) override;
  void visit(const ast::PropagateExpr &x) override;
  // Fallbacks for types compile_expr never sees at the top level.
  void visit(const ast::NullLiteralExpr &x) override;
  void visit(const ast::PipeExpr &) override {}
  void visit(const ast::BindingPattern &) override {}
  void visit(const ast::ArrayPattern &) override {}
  void visit(const ast::EnumPattern &) override {}
  void visit(const ast::StructPattern &) override {}

  void emit(OpCode op, ast::SourceLocation location);
  void emit_operand(OpCode op, uint32_t operand, ast::SourceLocation location);
  void emit_constant(Value value, ast::SourceLocation location,
                     KirType numeric_type = KirType::Any);
  std::size_t emit_jump(OpCode op, ast::SourceLocation location);
  void patch_jump(std::size_t offset);
  void patch_jump_to(std::size_t offset, std::size_t target);
  int resolve_local(const std::string &name) const;
  bool local_is_ref(int slot) const;
  bool local_is_mut_ref(int slot) const;
  void compile_lvalue_addr(const ast::Expr &expr);
  bool declare_local(const ast::VarDeclStmt &var_decl, uint32_t *slot);
  int resolve_struct(const ast::TypeExpr &type);
  void error_at(ast::SourceLocation location, std::string message);
  void warning_at(ast::SourceLocation location, std::string message);

  void process_import(const ast::ImportDecl &import_decl);
  void process_import_from(const ast::ImportDecl &import_decl, const std::string &importing_file_dir);
  void process_logical_import(const ast::LogicalImportDecl &import_decl);
  // Registers a single imported module's namespace, transitive imports,
  // functions, structs, and enums into the compiler's symbol tables. Shared by
  // the manifest path and the directory-as-module path.
  void register_imported_module(const ParsedModule &mod);
  std::string infer_struct_type(const ast::Expr &expr) const;
  // Best-effort source-level type name of an expression, used to infer generic
  // type arguments at a call site (literals, locals, struct/method returns).
  std::string infer_arg_type_name(const ast::Expr &expr) const;
  int resolve_free_function_for_type(const std::string &name, const std::string &arg_type) const;
  bool function_uses_concept_params(const ast::FunctionDecl &function) const;
  void attach_kir_metadata();
  void record_function_source(int function_idx, const std::string &source_path);
  std::string resolve_module_qualified(const std::string &ns, const std::string &member) const;
  void open_imported_namespace(const std::string &module_id);

  Chunk chunk_;
  BytecodeEmitter emitter_;
  KirModule kir_module_;
  KirRecorder kir_recorder_;
  std::vector<Local> locals_;
  std::vector<std::size_t> scope_stack_;
  std::vector<CompileError> errors_;
  std::vector<CompileWarning> warnings_;
  std::vector<LoopInfo> loop_stack_;
  std::unordered_set<std::string> used_;
  std::unordered_set<std::string> opened_;
  std::unordered_map<std::string, int> function_indices_;
  std::unordered_map<std::string, int> struct_indices_;
  std::unordered_map<std::string, int> enum_indices_;
  std::unordered_map<std::string, const ast::StructDecl *> generic_struct_decls_;
  std::unordered_map<std::string, const ast::FunctionDecl *> generic_func_decls_;
  std::unordered_map<std::string, const ast::FunctionDecl *> concept_generic_func_decls_;
  std::vector<std::pair<std::string, const ast::FunctionDecl *>> pending_generic_funcs_;
  const ast::ExprStmt *implicit_return_stmt_ = nullptr;
  ModuleLoader *module_loader_ = nullptr;
  std::unordered_map<std::string, std::string> namespace_aliases_;
  std::unordered_map<std::string, std::string> module_aliases_;
  std::unordered_set<std::string> imported_namespaces_;
  std::unordered_set<std::string> imported_qualifiers_;
  std::unordered_map<std::string, std::vector<const ast::FunctionDecl *>> imported_function_decls_;
  // Resolved paths of modules already processed by process_import_from, so a
  // module reached through several import paths (diamond deps) is registered
  // and compiled exactly once.
  std::unordered_set<std::string> processed_modules_;
  std::unordered_map<std::string, std::string> namespace_source_paths_;
  std::string entry_source_path_;
  std::string compiling_namespace_;
  std::vector<std::string> function_source_paths_;
  std::unordered_map<std::string, std::string> local_types_;
  std::unordered_map<std::string, const ast::ConceptDecl *> concept_registry_;
  std::unordered_map<std::string, std::string> method_return_types_;
  std::unordered_map<std::string, std::string> func_first_param_;
  std::unordered_map<std::string, const ast::Expr *> global_const_inits_;
  bool in_try_ = false;
  std::size_t try_catch_pc_ = 0; // catch landing pad PC for current try block
  std::unordered_map<std::size_t, std::size_t> kir_instr_at_bc_;
};

} // namespace kinglet
