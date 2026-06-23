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

class Compiler {
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
