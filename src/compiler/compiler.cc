// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "compiler/compiler.h"
#include "compiler/dense_array_lit.h"

#include "compiler/expr_width.h"
#include "ir/ir_builder.h"
#include "ir/kir_numeric.h"
#include "module/module_id.h"
#include "module/native_symbol.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>

namespace kinglet {

namespace {

const ast::Expr *single_return_expr(const ast::FunctionDecl &function) {
  const auto *body = dynamic_cast<const ast::BlockStmt *>(function.body.get());
  if (!body || body->statements.size() != 1) {
    return nullptr;
  }
  const auto *ret = dynamic_cast<const ast::ReturnStmt *>(body->statements[0].get());
  if (!ret || !ret->value) {
    return nullptr;
  }
  return ret->value.get();
}

} // namespace

CompileResult Compiler::compile(const ast::Program &program) {
  ast::Program &mutable_program = const_cast<ast::Program &>(program);
  ast::desugar_pipes(mutable_program);

  chunk_ = Chunk();
  emitter_.reset(&chunk_);
  kir_module_ = KirModule();
  locals_.clear();
  errors_.clear();
  warnings_.clear();
  used_.clear();
  opened_.clear();
  function_indices_.clear();
  struct_indices_.clear();
  enum_indices_.clear();
  processed_modules_.clear();
  namespace_source_paths_.clear();
  function_source_paths_.clear();
  concept_registry_.clear();
  func_first_param_.clear();
  global_const_inits_.clear();

  // Pre-register the built-in CastError enum at chunk index 0 so the VM
  // can build CastError variants on Cast failure without an enum lookup.
  {
    EnumMeta meta;
    meta.name = "CastError";
    meta.variants = {"Empty", "NotANumber", "Overflow"};
    meta.variant_param_counts = {0, 1, 1};
    int idx = chunk_.add_enum_meta(std::move(meta));
    enum_indices_["CastError"] = idx;
  }

  for (const ast::DeclPtr &declaration : program.declarations) {
    if (const auto *import_decl = dynamic_cast<const ast::ImportDecl *>(declaration.get())) {
      process_import(*import_decl);
    }
    if (const auto *logical_import = dynamic_cast<const ast::LogicalImportDecl *>(declaration.get())) {
      process_logical_import(*logical_import);
    }
    if (const auto *import_block = dynamic_cast<const ast::ImportBlockDecl *>(declaration.get())) {
      for (const auto &imp : import_block->imports) {
        if (const auto *id = dynamic_cast<const ast::ImportDecl *>(imp.get())) {
          process_import(*id);
        }
      }
    }
  }

  for (const ast::DeclPtr &declaration : program.declarations) {
    if (const auto *using_decl = dynamic_cast<const ast::UsingDecl *>(declaration.get())) {
      used_.insert(using_decl->namespace_name);
      if (using_decl->is_namespace) {
        opened_.insert(using_decl->namespace_name);
        if (imported_namespaces_.count(using_decl->namespace_name)) {
          open_imported_namespace(using_decl->namespace_name);
        }
      }
    }
    if (const auto *using_alias = dynamic_cast<const ast::UsingAliasDecl *>(declaration.get())) {
      module_aliases_[using_alias->alias] = module_id_to_qualifier(using_alias->module_id);
    }
  }

  // Pass 1: register all structs, enums, and functions
  for (const ast::DeclPtr &declaration : program.declarations) {
    if (const auto *struct_decl = dynamic_cast<const ast::StructDecl *>(declaration.get())) {
      if (!struct_decl->type_params.empty()) {
        generic_struct_decls_[struct_decl->name] = struct_decl;
        continue;
      }
      StructMeta meta;
      meta.name = struct_decl->name;
      for (const auto &field : struct_decl->fields) {
        meta.field_names.push_back(field.name);
      }
      int idx = chunk_.add_struct_meta(std::move(meta));
      struct_indices_[struct_decl->name] = idx;
    }
    if (const auto *enum_decl = dynamic_cast<const ast::EnumDecl *>(declaration.get())) {
      EnumMeta meta;
      meta.name = enum_decl->name;
      for (const auto &v : enum_decl->variants) {
        meta.variants.push_back(v.name);
        meta.variant_param_counts.push_back(static_cast<int>(v.param_types.size()));
      }
      int idx = chunk_.add_enum_meta(std::move(meta));
      enum_indices_[enum_decl->name] = idx;
    }
  }

  for (const ast::DeclPtr &declaration : program.declarations) {
    if (const auto *concept_decl = dynamic_cast<const ast::ConceptDecl *>(declaration.get())) {
      concept_registry_[concept_decl->name] = concept_decl;
    }
    if (const auto *top = dynamic_cast<const ast::TopLevelStmtDecl *>(declaration.get())) {
      if (const auto *var = dynamic_cast<const ast::VarDeclStmt *>(top->stmt.get())) {
        if (var->storage == "const" && var->init) {
          global_const_inits_[var->name] = var->init.get();
        }
      }
    }
  }

  std::vector<const ast::FunctionDecl *> functions;
  int main_index = -1;
  for (const ast::DeclPtr &declaration : program.declarations) {
    if (const auto *function = dynamic_cast<const ast::FunctionDecl *>(declaration.get())) {
      if (!function->type_params.empty()) {
        generic_func_decls_[function->name] = function;
        continue;
      }
      int idx = chunk_.add_function(FunctionInfo{
          .name = function->name,
          .entry = 0,
          .param_count = static_cast<int>(function->params.size()),
      });
      record_function_source(idx, entry_source_path_);
      uint32_t const_idx = chunk_.add_constant(Value::function_value(idx));
      function_indices_[function->name] = static_cast<int>(const_idx);
      method_return_types_[function->name] = function->return_type.name;
      if (!function->params.empty()) {
        const std::string &receiver = function->params[0].type.name;
        func_first_param_[function->name] = receiver;
        if (struct_indices_.count(receiver)) {
          function_indices_[receiver + "::" + function->name] = static_cast<int>(const_idx);
        }
      }
      functions.push_back(function);
      if (function->name == "main") {
        main_index = idx;
      }
    }
  }

  if (main_index < 0) {
    error_at(program.location, "Expected a main function.");
    return CompileResult{.chunk = std::move(chunk_),
                         .kir = std::move(kir_module_),
                         .errors = std::move(errors_),
                         .warnings = std::move(warnings_)};
  }

  // Emit preamble: call main, then return its result
  ast::SourceLocation preamble_loc{1, 0};
  emit_constant(Value::function_value(main_index), preamble_loc);
  emit_operand(OpCode::Call, 0, preamble_loc);
  emit(OpCode::Return, preamble_loc);

  // Pass 2: compile each function body
  for (const auto *function : functions) {
    compile_function(*function);
    if (!errors_.empty()) break;
  }

  // Pass 2b: compile imported function bodies (deterministic namespace order).
  std::vector<std::string> imported_ns_order;
  imported_ns_order.reserve(imported_function_decls_.size());
  for (const auto &[ns, _] : imported_function_decls_) {
    imported_ns_order.push_back(ns);
  }
  std::sort(imported_ns_order.begin(), imported_ns_order.end());
  for (const std::string &ns : imported_ns_order) {
    const auto &func_list = imported_function_decls_.at(ns);
    for (const auto *function : func_list) {
      std::string qualified = module_id_to_qualifier(ns) + "::" + function->name;
      compile_function(*function, qualified);
      if (!errors_.empty()) break;
    }
    if (!errors_.empty()) break;
  }

  // Pass 3: compile deferred generic function instantiations
  while (!pending_generic_funcs_.empty() && errors_.empty()) {
    auto pending = std::move(pending_generic_funcs_);
    pending_generic_funcs_.clear();
    for (const auto &[name, decl] : pending) {
      compile_function(*decl, name);
      if (!errors_.empty()) break;
    }
  }

  attach_kir_metadata();
  return CompileResult{.chunk = std::move(chunk_),
                       .kir = std::move(kir_module_),
                       .errors = std::move(errors_),
                       .warnings = std::move(warnings_)};
}

CompileResult Compiler::compile_module(const ast::Program &program) {
  chunk_ = Chunk();
  emitter_.reset(&chunk_);
  kir_module_ = KirModule();
  locals_.clear();
  errors_.clear();
  warnings_.clear();
  used_.clear();
  opened_.clear();
  function_indices_.clear();
  struct_indices_.clear();
  enum_indices_.clear();
  processed_modules_.clear();

  // Pre-register the built-in CastError enum at chunk index 0; see compile().
  {
    EnumMeta meta;
    meta.name = "CastError";
    meta.variants = {"Empty", "NotANumber", "Overflow"};
    meta.variant_param_counts = {0, 1, 1};
    int idx = chunk_.add_enum_meta(std::move(meta));
    enum_indices_["CastError"] = idx;
  }

  for (const ast::DeclPtr &declaration : program.declarations) {
    if (const auto *using_decl = dynamic_cast<const ast::UsingDecl *>(declaration.get())) {
      used_.insert(using_decl->namespace_name);
      if (using_decl->is_namespace) {
        opened_.insert(using_decl->namespace_name);
        if (imported_namespaces_.count(using_decl->namespace_name)) {
          open_imported_namespace(using_decl->namespace_name);
        }
      }
    }
    if (const auto *using_alias = dynamic_cast<const ast::UsingAliasDecl *>(declaration.get())) {
      module_aliases_[using_alias->alias] = module_id_to_qualifier(using_alias->module_id);
    }
  }

  // Register structs and enums
  for (const ast::DeclPtr &declaration : program.declarations) {
    if (const auto *struct_decl = dynamic_cast<const ast::StructDecl *>(declaration.get())) {
      if (!struct_decl->type_params.empty()) {
        generic_struct_decls_[struct_decl->name] = struct_decl;
        continue;
      }
      StructMeta meta;
      meta.name = struct_decl->name;
      for (const auto &field : struct_decl->fields) {
        meta.field_names.push_back(field.name);
      }
      int idx = chunk_.add_struct_meta(std::move(meta));
      struct_indices_[struct_decl->name] = idx;
    }
    if (const auto *enum_decl = dynamic_cast<const ast::EnumDecl *>(declaration.get())) {
      EnumMeta meta;
      meta.name = enum_decl->name;
      for (const auto &v : enum_decl->variants) {
        meta.variants.push_back(v.name);
        meta.variant_param_counts.push_back(static_cast<int>(v.param_types.size()));
      }
      int idx = chunk_.add_enum_meta(std::move(meta));
      enum_indices_[enum_decl->name] = idx;
    }
  }

  // Register and compile all functions (no main required)
  std::vector<const ast::FunctionDecl *> functions;
  for (const ast::DeclPtr &declaration : program.declarations) {
    if (const auto *function = dynamic_cast<const ast::FunctionDecl *>(declaration.get())) {
      if (!function->type_params.empty()) {
        generic_func_decls_[function->name] = function;
        continue;
      }
      int idx = chunk_.add_function(FunctionInfo{
          .name = function->name,
          .entry = 0,
          .param_count = static_cast<int>(function->params.size()),
      });
      record_function_source(idx, entry_source_path_);
      uint32_t const_idx = chunk_.add_constant(Value::function_value(idx));
      function_indices_[function->name] = static_cast<int>(const_idx);
      functions.push_back(function);
    }
  }

  for (const auto *function : functions) {
    compile_function(*function);
    if (!errors_.empty()) break;
  }

  attach_kir_metadata();
  return CompileResult{.chunk = std::move(chunk_),
                       .kir = std::move(kir_module_),
                       .errors = std::move(errors_),
                       .warnings = std::move(warnings_)};
}

void Compiler::record_function_source(int function_idx, const std::string &source_path) {
  if (function_idx < 0) {
    return;
  }
  const auto idx = static_cast<std::size_t>(function_idx);
  if (function_source_paths_.size() <= idx) {
    function_source_paths_.resize(idx + 1);
  }
  function_source_paths_[idx] = source_path;
}

void Compiler::attach_kir_metadata() {
  kir_module_.struct_metas.clear();
  for (const StructMeta &meta : chunk_.struct_metas()) {
    KirStructMeta km;
    km.name = meta.name;
    km.field_names = meta.field_names;
    kir_module_.struct_metas.push_back(std::move(km));
  }
  kir_module_.enum_metas.clear();
  kir_module_.function_names.clear();
  kir_module_.function_symbols.clear();
  kir_module_.function_param_counts.clear();
  for (std::size_t i = 0; i < chunk_.functions().size(); ++i) {
    const FunctionInfo &fn = chunk_.functions()[i];
    kir_module_.function_names.push_back(fn.name);
    kir_module_.function_param_counts.push_back(static_cast<int32_t>(fn.param_count));
    std::string src;
    if (i < function_source_paths_.size()) {
      src = function_source_paths_[i];
    }
    kir_module_.function_symbols.push_back(mangled_native_symbol(fn.name, src));
  }
  for (const EnumMeta &meta : chunk_.enum_metas()) {
    KirEnumMeta km;
    km.name = meta.name;
    km.variants = meta.variants;
    km.variant_param_counts.reserve(meta.variant_param_counts.size());
    for (int count : meta.variant_param_counts) {
      km.variant_param_counts.push_back(static_cast<int32_t>(count));
    }
    kir_module_.enum_metas.push_back(std::move(km));
  }
  const std::vector<Value> &constants = chunk_.constants();
  kir_module_.constant_strings.resize(constants.size());
  for (std::size_t i = 0; i < constants.size(); ++i) {
    if (constants[i].type == ValueType::String) {
      kir_module_.constant_strings[i] = constants[i].string_val();
    }
  }
}

void Compiler::push_scope() {
  scope_stack_.push_back(locals_.size());
}

void Compiler::pop_scope() {
  if (scope_stack_.empty()) return;
  const std::size_t target = scope_stack_.back();
  scope_stack_.pop_back();
  while (locals_.size() > target) {
    locals_.pop_back();
  }
}

std::string Compiler::infer_struct_type(const ast::Expr &expr) const {
  if (const auto *id = dynamic_cast<const ast::IdentifierExpr *>(&expr)) {
    auto it = local_types_.find(id->name);
    if (it != local_types_.end()) return it->second;
    return "";
  }
  if (const auto *field = dynamic_cast<const ast::FieldAccessExpr *>(&expr)) {
    std::string parent_type = infer_struct_type(*field->object);
    if (parent_type.empty()) return "";
    auto si = struct_indices_.find(parent_type);
    if (si == struct_indices_.end()) return "";
    const auto &meta = chunk_.struct_metas()[static_cast<std::size_t>(si->second)];
    for (std::size_t i = 0; i < meta.field_names.size(); ++i) {
      if (meta.field_names[i] == field->field_name) {
        return "";
      }
    }
    std::string method_key = parent_type + "::" + field->field_name;
    auto fi = function_indices_.find(method_key);
    if (fi != function_indices_.end()) return parent_type;
    return "";
  }
  if (const auto *call = dynamic_cast<const ast::CallExpr *>(&expr)) {
    if (const auto *callee_id = dynamic_cast<const ast::IdentifierExpr *>(call->callee.get())) {
      if (struct_indices_.count(callee_id->name)) return callee_id->name;
      auto ret_it = method_return_types_.find(callee_id->name);
      if (ret_it != method_return_types_.end() && struct_indices_.count(ret_it->second))
        return ret_it->second;
    }
    if (const auto *field_callee = dynamic_cast<const ast::FieldAccessExpr *>(call->callee.get())) {
      std::string obj_type = infer_struct_type(*field_callee->object);
      if (!obj_type.empty()) {
        std::string method_key = obj_type + "::" + field_callee->field_name;
        auto ret_it = method_return_types_.find(method_key);
        if (ret_it != method_return_types_.end() && struct_indices_.count(ret_it->second))
          return ret_it->second;
      }
    }
    return "";
  }
  if (const auto *struct_lit = dynamic_cast<const ast::StructLiteralExpr *>(&expr)) {
    return struct_lit->struct_type.name;
  }
  return "";
}

int Compiler::resolve_free_function_for_type(const std::string &name,
                                             const std::string &arg_type) const {
  auto fit = func_first_param_.find(name);
  if (fit == func_first_param_.end() || fit->second != arg_type) {
    return -1;
  }
  auto it = function_indices_.find(name);
  if (it == function_indices_.end()) {
    return -1;
  }
  return it->second;
}

std::string Compiler::infer_arg_type_name(const ast::Expr &expr) const {
  if (dynamic_cast<const ast::IntLiteralExpr *>(&expr)) return "int";
  if (dynamic_cast<const ast::FloatLiteralExpr *>(&expr)) return "float";
  if (dynamic_cast<const ast::BoolLiteralExpr *>(&expr)) return "bool";
  if (dynamic_cast<const ast::CharLiteralExpr *>(&expr)) return "char";
  if (dynamic_cast<const ast::StringLiteralExpr *>(&expr)) return "string";
  if (const auto *id = dynamic_cast<const ast::IdentifierExpr *>(&expr)) {
    auto it = local_types_.find(id->name);
    if (it != local_types_.end()) return it->second;
    return "";
  }
  // Struct value or method return — reuse the struct-type inferencer.
  return infer_struct_type(expr);
}

void Compiler::compile_function(const ast::FunctionDecl &function, const std::string &lookup_name) {
  locals_.clear();
  scope_stack_.clear();
  local_types_.clear();

  const std::string prev_compiling_ns = compiling_namespace_;
  if (!lookup_name.empty()) {
    const auto sep = lookup_name.find("::");
    compiling_namespace_ = sep == std::string::npos ? "" : lookup_name.substr(0, sep);
  } else {
    compiling_namespace_.clear();
  }

  // Look up this function's index and patch its entry point
  const std::string &name = lookup_name.empty() ? function.name : lookup_name;
  auto it = function_indices_.find(name);
  int const_idx = it->second;
  int func_idx = chunk_.constants()[static_cast<std::size_t>(const_idx)].function_idx;
  const_cast<FunctionInfo &>(chunk_.functions()[static_cast<std::size_t>(func_idx)]).entry =
      chunk_.instructions().size();

  // Parameters become locals at slots 0..N-1
  for (const auto &param : function.params) {
    locals_.push_back(Local{.name = param.name, .is_mutable = true});
    if (param.name == "self" && !lookup_name.empty()) {
      auto sep = lookup_name.find("::");
      if (sep != std::string::npos) {
        local_types_["self"] = lookup_name.substr(0, sep);
      }
    } else if (!param.type.name.empty()) {
      local_types_[param.name] = param.type.name;
    }
  }

  std::string fn_source = entry_source_path_;
  if (!lookup_name.empty()) {
    const auto sep = lookup_name.find("::");
    if (sep != std::string::npos) {
      const std::string ns = lookup_name.substr(0, sep);
      const auto src_it = namespace_source_paths_.find(ns);
      if (src_it != namespace_source_paths_.end()) {
        fn_source = src_it->second;
      }
    }
  }

  // KIR fast path: single `return <expr>` with IrBuilder-supported expression.
  if (const ast::Expr *ret_expr = single_return_expr(function)) {
    IrBuilder builder;
    if (auto kir = builder.build_expr_function(name, *ret_expr)) {
      kir->source_path = fn_source;
      kir->param_count = static_cast<int>(function.params.size());
      kir_module_.functions.push_back(*kir);
      emitter_.lower(*kir);
      implicit_return_stmt_ = nullptr;
      compiling_namespace_ = prev_compiling_ns;
      return;
    }
  }

  // Detect implicit return: if last statement in body is an ExprStmt,
  // compile it as a return instead of discarding the value.
  const auto *body = dynamic_cast<const ast::BlockStmt *>(function.body.get());
  if (body && !body->statements.empty()) {
    const auto *last = dynamic_cast<const ast::ExprStmt *>(body->statements.back().get());
    if (last && function.return_type.name != "void") {
      implicit_return_stmt_ = last;
    }
  }

  kir_recorder_.begin_function(name, static_cast<int>(function.params.size()), fn_source);
  kir_instr_at_bc_.clear();
  bool body_returned = false;
  if (body && !body->statements.empty()) {
    body_returned =
        dynamic_cast<const ast::ReturnStmt *>(body->statements.back().get()) != nullptr;
  }
  compile_stmt(*function.body);
  implicit_return_stmt_ = nullptr;

  // Fallthrough safety: implicit null return
  if (errors_.empty() && !body_returned) {
    emit(OpCode::Null, function.location);
    emit(OpCode::Return, function.location);
  }
  kir_recorder_.end_function(&kir_module_);
  compiling_namespace_ = prev_compiling_ns;
}

void Compiler::compile_stmt(const ast::Stmt &stmt) {
  if (const auto *block = dynamic_cast<const ast::BlockStmt *>(&stmt)) {
    push_scope();
    bool returned = false;
    for (const ast::StmtPtr &statement : block->statements) {
      if (returned) break;
      compile_stmt(*statement);
      if (!errors_.empty()) {
        return;
      }
      if (dynamic_cast<const ast::ReturnStmt *>(statement.get())) {
        returned = true;
      }
    }
    pop_scope();
    return;
  }

  if (const auto *return_stmt = dynamic_cast<const ast::ReturnStmt *>(&stmt)) {
    if (return_stmt->value) {
      compile_expr(*return_stmt->value);
    } else {
      emit(OpCode::Null, return_stmt->location);
    }
    emit(OpCode::Return, return_stmt->location);
    return;
  }

  if (const auto *var_decl = dynamic_cast<const ast::VarDeclStmt *>(&stmt)) {
    uint32_t slot = 0;
    if (!declare_local(*var_decl, &slot)) {
      return;
    }
    if (!var_decl->type.name.empty()) {
      // Record the monomorphized name for a generic type (Box<int> -> Box__int)
      // so member access on this local resolves to the instantiated struct.
      std::string ty = var_decl->type.name;
      for (const auto &a : var_decl->type.type_args) ty += "__" + a.to_string();
      local_types_[var_decl->name] = ty;
    } else if (var_decl->init) {
      if (const auto *struct_lit = dynamic_cast<const ast::StructLiteralExpr *>(var_decl->init.get())) {
        local_types_[var_decl->name] = struct_lit->struct_type.name;
      }
    }
    if (var_decl->init) {
      compile_expr(*var_decl->init);
    } else {
      emit(OpCode::Null, var_decl->location);
    }
    emit_operand(OpCode::StoreLocal, slot, var_decl->location);
    emit(OpCode::Pop, var_decl->location);
    return;
  }

  if (const auto *unpack = dynamic_cast<const ast::UnpackDeclStmt *>(&stmt)) {
    compile_expr(*unpack->init);
    uint32_t arr_slot = static_cast<uint32_t>(locals_.size());
    locals_.push_back(Local{.name = "$unpack_tmp", .is_mutable = false});
    emit_operand(OpCode::StoreLocal, arr_slot, unpack->location);
    emit(OpCode::Pop, unpack->location);

    for (std::size_t i = 0; i < unpack->names.size(); ++i) {
      uint32_t slot = static_cast<uint32_t>(locals_.size());
      locals_.push_back(Local{.name = unpack->names[i], .is_mutable = true});
      emit_operand(OpCode::LoadLocal, arr_slot, unpack->location);
      emit_constant(Value::int_value(static_cast<int64_t>(i)), unpack->location);
      emit(OpCode::IndexGet, unpack->location);
      emit_operand(OpCode::StoreLocal, slot, unpack->location);
      emit(OpCode::Pop, unpack->location);
    }

    if (!unpack->rest_name.empty()) {
      uint32_t slot = static_cast<uint32_t>(locals_.size());
      locals_.push_back(Local{.name = unpack->rest_name, .is_mutable = true});
      emit_operand(OpCode::LoadLocal, arr_slot, unpack->location);
      emit_constant(Value::int_value(static_cast<int64_t>(unpack->names.size())), unpack->location);
      emit_operand(OpCode::LoadLocal, arr_slot, unpack->location);
      emit(OpCode::ArrayLen, unpack->location);
      emit(OpCode::ArraySlice, unpack->location);
      emit_operand(OpCode::StoreLocal, slot, unpack->location);
      emit(OpCode::Pop, unpack->location);
    }
    return;
  }

  if (const auto *expr_stmt = dynamic_cast<const ast::ExprStmt *>(&stmt)) {
    compile_expr(*expr_stmt->expr);
    if (expr_stmt == implicit_return_stmt_) {
      emit(OpCode::Return, expr_stmt->location);
    } else {
      emit(OpCode::Pop, expr_stmt->location);
    }
    return;
  }

  if (const auto *if_stmt = dynamic_cast<const ast::IfStmt *>(&stmt)) {
    compile_expr(*if_stmt->condition);
    const std::size_t then_jump = emit_jump(OpCode::JmpFalse, if_stmt->location);
    compile_stmt(*if_stmt->then_branch);
    if (if_stmt->else_branch) {
      const std::size_t else_jump = emit_jump(OpCode::Jmp, if_stmt->location);
      patch_jump(then_jump);
      compile_stmt(*if_stmt->else_branch);
      patch_jump(else_jump);
    } else {
      patch_jump(then_jump);
    }
    return;
  }

  if (const auto *guard_stmt = dynamic_cast<const ast::GuardStmt *>(&stmt)) {
    compile_expr(*guard_stmt->condition);
    const std::size_t else_jump = emit_jump(OpCode::JmpFalse, guard_stmt->location);
    const std::size_t skip_jump = emit_jump(OpCode::Jmp, guard_stmt->location);
    patch_jump(else_jump);
    compile_stmt(*guard_stmt->else_body);
    patch_jump(skip_jump);
    return;
  }

  if (const auto *while_stmt = dynamic_cast<const ast::WhileStmt *>(&stmt)) {
    loop_stack_.emplace_back();

    const std::size_t loop_start = chunk_.instructions().size();
    compile_expr(*while_stmt->condition);
    loop_stack_.back().break_jumps.push_back(emit_jump(OpCode::JmpFalse, while_stmt->location));
    compile_stmt(*while_stmt->body);
    const std::size_t loop_jump = emit_jump(OpCode::Jmp, while_stmt->location);
    patch_jump_to(loop_jump, loop_start);

    for (std::size_t jump : loop_stack_.back().break_jumps) {
      patch_jump(jump);
    }
    for (std::size_t jump : loop_stack_.back().continue_jumps) {
      patch_jump_to(jump, loop_start);
    }
    loop_stack_.pop_back();
    return;
  }

  if (const auto *for_stmt = dynamic_cast<const ast::ForStmt *>(&stmt)) {
    push_scope();
    loop_stack_.emplace_back();

    if (for_stmt->init) {
      compile_stmt(*for_stmt->init);
    }

    const std::size_t loop_start = chunk_.instructions().size();
    if (for_stmt->condition) {
      compile_expr(*for_stmt->condition);
      loop_stack_.back().break_jumps.push_back(emit_jump(OpCode::JmpFalse, for_stmt->location));
    }
    compile_stmt(*for_stmt->body);

    const std::size_t step_start = chunk_.instructions().size();
    for (std::size_t jump : loop_stack_.back().continue_jumps) {
      patch_jump_to(jump, step_start);
    }

    if (for_stmt->step) {
      compile_stmt(*for_stmt->step);
    }

    const std::size_t loop_jump = emit_jump(OpCode::Jmp, for_stmt->location);
    patch_jump_to(loop_jump, loop_start);

    for (std::size_t jump : loop_stack_.back().break_jumps) {
      patch_jump(jump);
    }
    loop_stack_.pop_back();
    pop_scope();
    return;
  }

  if (dynamic_cast<const ast::BreakStmt *>(&stmt)) {
    if (loop_stack_.empty()) {
      error_at(stmt.location, "break must be inside a loop.");
      return;
    }
    const std::size_t jump = emit_jump(OpCode::Jmp, stmt.location);
    loop_stack_.back().break_jumps.push_back(jump);
    return;
  }

  if (dynamic_cast<const ast::ContinueStmt *>(&stmt)) {
    if (loop_stack_.empty()) {
      error_at(stmt.location, "continue must be inside a loop.");
      return;
    }
    const std::size_t jump = emit_jump(OpCode::Jmp, stmt.location);
    loop_stack_.back().continue_jumps.push_back(jump);
    return;
  }

  if (const auto *try_catch = dynamic_cast<const ast::TryCatchStmt *>(&stmt)) {
    // Emit: PushHandler <catch_pc>
    //       <try body>
    //       PopHandler
    //       Jmp END
    // catch_pc:
    //       Pop (error value from stack)
    //       <for each catch arm>
    //         Dup + EnumVariantTag comparison → match arm
    //       END:
    //
    // Simplified single-catch model: one catch landing pad that stores the
    // error value into the binding slot and runs the catch body.

    // Placeholder PushHandler — patch operand after we know catch_pc.
    const std::size_t handler_idx = chunk_.instructions().size();
    emit_operand(OpCode::PushHandler, 0, stmt.location);

    // Compile try body — `?` inside will generate JmpIfErr whose target
    // is the catch stub (see PropagateExpr).
    const bool prev_in_try = in_try_;
    const std::size_t prev_catch_pc = try_catch_pc_;
    in_try_ = true;
    // try_catch_pc_ will be set below once we know catch_pc.
    compile_stmt(*try_catch->body);
    in_try_ = prev_in_try;
    try_catch_pc_ = prev_catch_pc;

    emit(OpCode::PopHandler, stmt.location);
    const std::size_t end_jump = emit_jump(OpCode::Jmp, stmt.location);

    // --- catch landing pad ---
    const std::size_t catch_pc = chunk_.instructions().size();
    // Patch the PushHandler operand to be the relative offset.
    const int32_t handler_offset = static_cast<int32_t>(catch_pc - (handler_idx + 1));
    chunk_.patch_operand(handler_idx, handler_offset);
    const auto handler_kir = kir_instr_at_bc_.find(handler_idx);
    if (handler_kir != kir_instr_at_bc_.end()) {
      kir_recorder_.patch_operand(handler_kir->second, handler_offset);
    }

    // The error value is on the stack (left by `?` stub: Pop + Null + Return
    // in function-level mode; inside try the `?` stub will instead Jmp here
    // with the error value still on stack after Pop of original operand).
    // For the try-body success path, no error value lands here.
    // For the `?` error path, the stub pops the error value already before
    // jumping — so the landing pad only needs to handle the binding.

    // Single catch arm: bind error value, execute body.
    if (!try_catch->catches.empty()) {
      const ast::CatchArm &arm = try_catch->catches[0];
      const uint32_t err_slot = static_cast<uint32_t>(locals_.size());
      locals_.push_back(Local{.name = arm.binding_name, .is_mutable = false});
      emit_operand(OpCode::StoreLocal, err_slot, stmt.location);
      compile_stmt(*arm.body);
      locals_.pop_back();
    }

    // If multiple catch arms, they'd chain here with enum-variant checks.
    // For now only the first arm is compiled (design doc: single-catch simplification).

    patch_jump(end_jump);
    return;
  }

  error_at(stmt.location, "Unsupported statement in VM compiler.");
}

void Compiler::compile_expr(const ast::Expr &expr) {
  if (const auto *int_lit = dynamic_cast<const ast::IntLiteralExpr *>(&expr)) {
    const KirType width =
        kir_type_from_int_literal_suffix(int_lit->width_suffix, int_lit->value);
    emit_constant(Value::int_value(int_lit->value), int_lit->location, width);
    return;
  }

  if (const auto *char_lit = dynamic_cast<const ast::CharLiteralExpr *>(&expr)) {
    emit_constant(Value::char_value(char_lit->value), char_lit->location, KirType::Int8);
    return;
  }

  if (const auto *float_lit = dynamic_cast<const ast::FloatLiteralExpr *>(&expr)) {
    const KirType width = kir_type_from_float_literal_suffix(float_lit->width_suffix);
    emit_constant(Value::double_value(float_lit->value), float_lit->location, width);
    return;
  }

  if (const auto *string_lit = dynamic_cast<const ast::StringLiteralExpr *>(&expr)) {
    // Unescape: \n -> newline, \t -> tab, etc.
    std::string unescaped;
    unescaped.reserve(string_lit->value.size());
    for (std::size_t i = 0; i < string_lit->value.size(); ++i) {
      if (string_lit->value[i] == '\\' && i + 1 < string_lit->value.size()) {
        switch (string_lit->value[i + 1]) {
        case 'n':
          unescaped += '\n';
          ++i;
          break;
        case 't':
          unescaped += '\t';
          ++i;
          break;
        case 'r':
          unescaped += '\r';
          ++i;
          break;
        case '\\':
          unescaped += '\\';
          ++i;
          break;
        case '"':
          unescaped += '"';
          ++i;
          break;
        default:
          unescaped += string_lit->value[i];
          break;
        }
      } else {
        unescaped += string_lit->value[i];
      }
    }
    emit_constant(Value::string_value(unescaped), string_lit->location);
    return;
  }

  if (const auto *bool_lit = dynamic_cast<const ast::BoolLiteralExpr *>(&expr)) {
    emit(bool_lit->value ? OpCode::True : OpCode::False, bool_lit->location);
    return;
  }

  if (dynamic_cast<const ast::NullLiteralExpr *>(&expr)) {
    emit(OpCode::Null, expr.location);
    return;
  }

  if (const auto *unary = dynamic_cast<const ast::UnaryExpr *>(&expr)) {
    compile_expr(*unary->right);
    switch (unary->op) {
    case ast::UnaryOp::Neg:
      emit(OpCode::Negate, unary->location);
      break;
    case ast::UnaryOp::Not:
      emit(OpCode::Not, unary->location);
      break;
    case ast::UnaryOp::BitNot:
      emit(OpCode::BitNot, unary->location);
      break;
    default:
      error_at(unary->location, "Unsupported unary operator.");
      break;
    }
    return;
  }

  if (const auto *identifier = dynamic_cast<const ast::IdentifierExpr *>(&expr)) {
    auto git = global_const_inits_.find(identifier->name);
    if (git != global_const_inits_.end()) {
      compile_expr(*git->second);
      return;
    }
    const int slot = resolve_local(identifier->name);
    if (slot < 0) {
      error_at(identifier->location, "Use of undeclared variable '" + identifier->name + "'.");
      return;
    }
    emit_operand(OpCode::LoadLocal, static_cast<uint32_t>(slot), identifier->location);
    return;
  }

  if (const auto *assign = dynamic_cast<const ast::AssignExpr *>(&expr)) {
    compile_assignment(*assign);
    return;
  }

  if (const auto *binary = dynamic_cast<const ast::BinaryExpr *>(&expr)) {
    if (binary->op == ast::BinaryOp::And) {
      compile_expr(*binary->left);
      std::size_t false_jump = emit_jump(OpCode::JmpFalse, binary->location);
      compile_expr(*binary->right);
      std::size_t end_jump = emit_jump(OpCode::Jmp, binary->location);
      patch_jump(false_jump);
      emit(OpCode::False, binary->location);
      patch_jump(end_jump);
      return;
    }
    if (binary->op == ast::BinaryOp::Or) {
      compile_expr(*binary->left);
      std::size_t false_jump = emit_jump(OpCode::JmpFalse, binary->location);
      emit(OpCode::True, binary->location);
      std::size_t end_jump = emit_jump(OpCode::Jmp, binary->location);
      patch_jump(false_jump);
      compile_expr(*binary->right);
      patch_jump(end_jump);
      return;
    }
    compile_expr(*binary->left);
    compile_expr(*binary->right);
    const std::string width = infer_expr_type_name(*binary, local_types_);
    switch (binary->op) {
    case ast::BinaryOp::Add:
    case ast::BinaryOp::Sub:
    case ast::BinaryOp::Mul:
    case ast::BinaryOp::Div:
    case ast::BinaryOp::Mod:
      emit(width_arithmetic_opcode(binary->op, width), binary->location);
      break;
    case ast::BinaryOp::Eq:
      emit(OpCode::Eq, binary->location);
      break;
    case ast::BinaryOp::Neq:
      emit(OpCode::Neq, binary->location);
      break;
    case ast::BinaryOp::Lt:
      emit(OpCode::Lt, binary->location);
      break;
    case ast::BinaryOp::Gt:
      emit(OpCode::Gt, binary->location);
      break;
    case ast::BinaryOp::Le:
      emit(OpCode::Le, binary->location);
      break;
    case ast::BinaryOp::Ge:
      emit(OpCode::Ge, binary->location);
      break;
    case ast::BinaryOp::BitAnd:
      emit(OpCode::BitAnd, binary->location);
      break;
    case ast::BinaryOp::BitOr:
      emit(OpCode::BitOr, binary->location);
      break;
    case ast::BinaryOp::BitXor:
      emit(OpCode::BitXor, binary->location);
      break;
    case ast::BinaryOp::Shl:
      emit(OpCode::Shl, binary->location);
      break;
    case ast::BinaryOp::Shr:
      emit(OpCode::Shr, binary->location);
      break;
    default:
      error_at(binary->location, "Unsupported binary operator.");
      break;
    }
    return;
  }

  if (const auto *call_expr = dynamic_cast<const ast::CallExpr *>(&expr)) {
    const auto *callee_id = dynamic_cast<const ast::IdentifierExpr *>(call_expr->callee.get());
    // Handle bare io:: members when 'using namespace io;' is in effect
    if (callee_id && opened_.count("io") != 0) {
      if (callee_id->name == "out") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeOut, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }
      if (callee_id->name == "err") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeErr, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }
      if (callee_id->name == "in") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeIn, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }
    }

    const auto *ns_callee =
        dynamic_cast<const ast::NamespaceAccessExpr *>(call_expr->callee.get());
    // Type-qualified methods: int::bits(float)
    if (ns_callee && ns_callee->namespace_name == "int" && ns_callee->member_name == "bits") {
      compile_expr(*call_expr->args[0]);
      emit(OpCode::FloatToBits, call_expr->location);
      return;
    }
    if (ns_callee && used_.count(ns_callee->namespace_name) != 0 &&
        ns_callee->namespace_name == "io") {
      if (ns_callee->member_name == "out") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeOut, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }

      if (ns_callee->member_name == "err") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeErr, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }

      if (ns_callee->member_name == "in") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeIn, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }
    }

    // Handle fs::__read(...) / fs::__write(...) direct calls.
    if (ns_callee && used_.count("fs") != 0 && ns_callee->namespace_name == "fs") {
      if (ns_callee->member_name == "__read") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeFsRead, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }
      if (ns_callee->member_name == "__write") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeFsWrite, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }
    }

    // Handle sys::args() direct call.
    if (ns_callee && used_.count("sys") != 0 && ns_callee->namespace_name == "sys") {
      if (ns_callee->member_name == "args") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeSysArgs, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }
    }

    // Handle enum variant construction with payload: Shape::Circle(1.0)
    if (ns_callee) {
      auto enum_it = enum_indices_.find(ns_callee->namespace_name);
      if (enum_it != enum_indices_.end()) {
        int type_idx = enum_it->second;
        const auto &meta = chunk_.enum_metas()[static_cast<std::size_t>(type_idx)];
        int variant_idx = -1;
        for (int i = 0; i < static_cast<int>(meta.variants.size()); ++i) {
          if (meta.variants[static_cast<std::size_t>(i)] == ns_callee->member_name) {
            variant_idx = i;
            break;
          }
        }
        if (variant_idx < 0) {
          error_at(ns_callee->location, "Unknown enum variant '" + ns_callee->member_name + "'.");
          return;
        }
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        uint32_t operand = (static_cast<uint32_t>(type_idx) << 16) |
                           static_cast<uint32_t>(variant_idx);
        emit_operand(OpCode::EnumVariantPayload, operand, call_expr->location);
        return;
      }
    }

    if (ns_callee && concept_registry_.count(ns_callee->namespace_name)) {
      if (call_expr->args.empty()) {
        error_at(call_expr->location, "Concept method '" + ns_callee->namespace_name + "::" +
                                        ns_callee->member_name + "' expects at least one argument.");
        return;
      }
      const std::string arg_ty = infer_arg_type_name(*call_expr->args[0]);
      const int const_idx =
          resolve_free_function_for_type(ns_callee->member_name, arg_ty);
      if (const_idx < 0) {
        error_at(call_expr->location, "No implementation of '" + ns_callee->namespace_name + "::" +
                                        ns_callee->member_name + "' for type '" + arg_ty + "'.");
        return;
      }
      for (const ast::ExprPtr &arg : call_expr->args) {
        compile_expr(*arg);
      }
      emit_constant(Value::function_value(
                        chunk_.constants()[static_cast<std::size_t>(const_idx)].function_idx),
                    call_expr->location);
      emit_operand(OpCode::Call, static_cast<uint32_t>(call_expr->args.size()), call_expr->location);
      return;
    }

    // Handle io::out.line(...), io::err.line(...), io::in.secret(...)
    const auto *field_callee =
        dynamic_cast<const ast::FieldAccessExpr *>(call_expr->callee.get());
    if (field_callee) {
      const auto *ns_obj =
          dynamic_cast<const ast::NamespaceAccessExpr *>(field_callee->object.get());
      if (ns_obj && ns_obj->namespace_name == "io" && used_.count("io") != 0) {
        if (ns_obj->member_name == "out" && field_callee->field_name == "line") {
          for (const ast::ExprPtr &arg : call_expr->args) {
            compile_expr(*arg);
          }
          emit_operand(OpCode::NativeOutLn, static_cast<uint32_t>(call_expr->args.size()),
                       call_expr->location);
          return;
        }
        if (ns_obj->member_name == "err" && field_callee->field_name == "line") {
          for (const ast::ExprPtr &arg : call_expr->args) {
            compile_expr(*arg);
          }
          emit_operand(OpCode::NativeErrLn, static_cast<uint32_t>(call_expr->args.size()),
                       call_expr->location);
          return;
        }
        if (ns_obj->member_name == "in" && field_callee->field_name == "secret") {
          for (const ast::ExprPtr &arg : call_expr->args) {
            compile_expr(*arg);
          }
          emit_operand(OpCode::NativeInSecret, static_cast<uint32_t>(call_expr->args.size()),
                       call_expr->location);
          return;
        }
      }
    }

    // Handle aliased bare names from `using io { out }`: out.line(...)
    // emits the same native opcode as io::out.line(...).
    if (field_callee) {
      const auto *id_obj =
          dynamic_cast<const ast::IdentifierExpr *>(field_callee->object.get());
      if (id_obj && opened_.count("io") != 0) {
        if (id_obj->name == "out" && field_callee->field_name == "line") {
          for (const ast::ExprPtr &arg : call_expr->args) {
            compile_expr(*arg);
          }
          emit_operand(OpCode::NativeOutLn, static_cast<uint32_t>(call_expr->args.size()),
                       call_expr->location);
          return;
        }
        if (id_obj->name == "err" && field_callee->field_name == "line") {
          for (const ast::ExprPtr &arg : call_expr->args) {
            compile_expr(*arg);
          }
          emit_operand(OpCode::NativeErrLn, static_cast<uint32_t>(call_expr->args.size()),
                       call_expr->location);
          return;
        }
        if (id_obj->name == "in" && field_callee->field_name == "secret") {
          for (const ast::ExprPtr &arg : call_expr->args) {
            compile_expr(*arg);
          }
          emit_operand(OpCode::NativeInSecret, static_cast<uint32_t>(call_expr->args.size()),
                       call_expr->location);
          return;
        }
      }
      if (id_obj) {
        auto alias_it = using_aliases_.find(id_obj->name);
        if (alias_it != using_aliases_.end()) {
          const auto &[ns, member] = alias_it->second;
          if (ns == "io") {
            if (member == "out" && field_callee->field_name == "line") {
              for (const ast::ExprPtr &arg : call_expr->args) compile_expr(*arg);
              emit_operand(OpCode::NativeOutLn, static_cast<uint32_t>(call_expr->args.size()),
                           call_expr->location);
              return;
            }
            if (member == "err" && field_callee->field_name == "line") {
              for (const ast::ExprPtr &arg : call_expr->args) compile_expr(*arg);
              emit_operand(OpCode::NativeErrLn, static_cast<uint32_t>(call_expr->args.size()),
                           call_expr->location);
              return;
            }
            if (member == "in" && field_callee->field_name == "secret") {
              for (const ast::ExprPtr &arg : call_expr->args) compile_expr(*arg);
              emit_operand(OpCode::NativeInSecret, static_cast<uint32_t>(call_expr->args.size()),
                           call_expr->location);
              return;
            }
          }
        }
      }
    }

    // Handle array/string method calls
    if (field_callee) {
      const std::string &method = field_callee->field_name;
      if (method == "len" || method == "push" || method == "pop" ||
          method == "remove" || method == "contains" || method == "clear" ||
          method == "insert" || method == "index_of" || method == "slice" ||
          method == "reverse" || method == "resize" || method == "has" ||
          method == "keys" || method == "starts_with" ||
          method == "ends_with" || method == "replace" || method == "split" ||
          method == "trim" || method == "to_upper" || method == "to_lower") {
        compile_expr(*field_callee->object);
        if (method == "len") {
          emit(OpCode::ArrayLen, call_expr->location);
          return;
        }
        if (method == "has") {
          compile_expr(*call_expr->args[0]);
          emit(OpCode::MapHas, call_expr->location);
          return;
        }
        if (method == "keys") {
          emit(OpCode::MapKeys, call_expr->location);
          return;
        }
        if (method == "push") {
          compile_expr(*call_expr->args[0]);
          emit(OpCode::ArrayPush, call_expr->location);
          return;
        }
        if (method == "resize") {
          compile_expr(*call_expr->args[0]);
          compile_expr(*call_expr->args[1]);
          emit(OpCode::ArrayResize, call_expr->location);
          return;
        }
        if (method == "pop") {
          emit(OpCode::ArrayPop, call_expr->location);
          return;
        }
        if (method == "remove") {
          compile_expr(*call_expr->args[0]);
          emit(OpCode::ArrayRemove, call_expr->location);
          return;
        }
        if (method == "contains") {
          compile_expr(*call_expr->args[0]);
          emit(OpCode::ArrayContains, call_expr->location);
          return;
        }
        if (method == "clear") {
          emit(OpCode::ArrayClear, call_expr->location);
          return;
        }
        if (method == "insert") {
          compile_expr(*call_expr->args[0]);
          compile_expr(*call_expr->args[1]);
          emit(OpCode::ArrayInsert, call_expr->location);
          return;
        }
        if (method == "index_of") {
          compile_expr(*call_expr->args[0]);
          emit(OpCode::ArrayIndexOf, call_expr->location);
          return;
        }
        if (method == "slice") {
          compile_expr(*call_expr->args[0]);
          compile_expr(*call_expr->args[1]);
          emit(OpCode::ArraySlice, call_expr->location);
          return;
        }
        if (method == "reverse") {
          emit(OpCode::ArrayReverse, call_expr->location);
          return;
        }
        if (method == "starts_with") {
          compile_expr(*call_expr->args[0]);
          emit(OpCode::StringStartsWith, call_expr->location);
          return;
        }
        if (method == "ends_with") {
          compile_expr(*call_expr->args[0]);
          emit(OpCode::StringEndsWith, call_expr->location);
          return;
        }
        if (method == "replace") {
          compile_expr(*call_expr->args[0]);
          compile_expr(*call_expr->args[1]);
          emit(OpCode::StringReplace, call_expr->location);
          return;
        }
        if (method == "split") {
          compile_expr(*call_expr->args[0]);
          emit(OpCode::StringSplit, call_expr->location);
          return;
        }
        if (method == "trim") {
          emit(OpCode::StringTrim, call_expr->location);
          return;
        }
        if (method == "to_upper") {
          emit(OpCode::StringToUpper, call_expr->location);
          return;
        }
        if (method == "to_lower") {
          emit(OpCode::StringToLower, call_expr->location);
          return;
        }
      }
    }

    // Handle impl method calls: obj.method(args...)
    if (field_callee) {
      std::string obj_type = infer_struct_type(*field_callee->object);
      if (!obj_type.empty()) {
        std::string method_key = obj_type + "::" + field_callee->field_name;
        auto func_it = function_indices_.find(method_key);
        if (func_it != function_indices_.end()) {
          compile_expr(*field_callee->object);
          for (const ast::ExprPtr &arg : call_expr->args) {
            compile_expr(*arg);
          }
          emit_constant(Value::function_value(
              chunk_.constants()[static_cast<std::size_t>(func_it->second)].function_idx),
              call_expr->location);
          emit_operand(OpCode::Call, static_cast<uint32_t>(call_expr->args.size() + 1),
                       call_expr->location);
          return;
        }
      }
    }

    // Generic function call — type arguments are explicit, or inferred from the
    // argument expressions (matching parameters written as bare type params).
    if (callee_id && generic_func_decls_.count(callee_id->name)) {
      const ast::FunctionDecl *decl = generic_func_decls_.at(callee_id->name);
      std::vector<std::string> type_arg_names;
      if (!call_expr->type_args.empty()) {
        for (const auto &arg : call_expr->type_args) {
          type_arg_names.push_back(arg.to_string());
        }
      } else {
        std::unordered_map<std::string, std::string> inferred;
        for (size_t i = 0; i < decl->params.size() && i < call_expr->args.size(); ++i) {
          const ast::TypeExpr &pt = decl->params[i].type;
          if (!pt.type_args.empty() || inferred.count(pt.name)) continue;
          bool is_type_param = false;
          for (const std::string &tp : decl->type_params) {
            if (tp == pt.name) { is_type_param = true; break; }
          }
          if (is_type_param) {
            inferred[pt.name] = infer_arg_type_name(*call_expr->args[i]);
          }
        }
        for (const std::string &tp : decl->type_params) {
          auto it = inferred.find(tp);
          if (it != inferred.end() && !it->second.empty()) {
            type_arg_names.push_back(it->second);
          }
        }
      }
      if (type_arg_names.size() == decl->type_params.size()) {
        std::string mangled = callee_id->name;
        for (const std::string &n : type_arg_names) {
          mangled += "__" + n;
        }
        auto func_it = function_indices_.find(mangled);
        if (func_it == function_indices_.end()) {
          int idx = chunk_.add_function(FunctionInfo{
              .name = mangled,
              .entry = 0,
              .param_count = static_cast<int>(decl->params.size()),
          });
          record_function_source(idx, entry_source_path_);
          uint32_t const_idx = chunk_.add_constant(Value::function_value(idx));
          function_indices_[mangled] = static_cast<int>(const_idx);
          pending_generic_funcs_.push_back({mangled, decl});
        }
        func_it = function_indices_.find(mangled);
        if (func_it != function_indices_.end()) {
          for (const ast::ExprPtr &arg : call_expr->args) {
            compile_expr(*arg);
          }
          emit_constant(Value::function_value(
              chunk_.constants()[static_cast<std::size_t>(func_it->second)].function_idx),
              call_expr->location);
          emit_operand(OpCode::Call, static_cast<uint32_t>(call_expr->args.size()), call_expr->location);
          return;
        }
      }
    }

    // User-defined function call
    if (callee_id) {
      std::unordered_map<std::string, int>::const_iterator func_it =
          function_indices_.end();
      if (!compiling_namespace_.empty()) {
        func_it = function_indices_.find(compiling_namespace_ + "::" + callee_id->name);
      }
      if (func_it == function_indices_.end()) {
        func_it = function_indices_.find(callee_id->name);
      }
      if (func_it == function_indices_.end()) {
        std::vector<std::string> ns_sorted(imported_namespaces_.begin(),
                                           imported_namespaces_.end());
        std::sort(ns_sorted.begin(), ns_sorted.end());
        for (const auto &ns : ns_sorted) {
          auto qit = function_indices_.find(module_id_to_qualifier(ns) + "::" + callee_id->name);
          if (qit != function_indices_.end()) {
            func_it = qit;
            break;
          }
        }
      }
      if (func_it != function_indices_.end()) {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_constant(Value::function_value(
            chunk_.constants()[static_cast<std::size_t>(func_it->second)].function_idx),
            call_expr->location);
        emit_operand(OpCode::Call, static_cast<uint32_t>(call_expr->args.size()), call_expr->location);
        return;
      }
    }

    for (const ast::ExprPtr &arg : call_expr->args) {
      compile_expr(*arg);
    }
    compile_expr(*call_expr->callee);
    emit_operand(OpCode::Call, static_cast<uint32_t>(call_expr->args.size()), call_expr->location);
    return;
  }

  if (const auto *match_expr = dynamic_cast<const ast::MatchExpr *>(&expr)) {
    compile_expr(*match_expr->value);
    const uint32_t temp_slot = static_cast<uint32_t>(locals_.size());
    locals_.push_back(Local{.name = "<match_value>", .is_mutable = false});
    emit_operand(OpCode::StoreLocal, temp_slot, match_expr->location);

    std::vector<std::size_t> end_jumps;
    for (const ast::MatchArm &arm : match_expr->arms) {
      const auto *identifier = dynamic_cast<const ast::IdentifierExpr *>(arm.pattern.get());
      const auto *binding = dynamic_cast<const ast::BindingPattern *>(arm.pattern.get());

      if (identifier && identifier->name == "_") {
        if (arm.guard) {
          compile_expr(*arm.guard);
          const std::size_t next_arm = emit_jump(OpCode::JmpFalse, match_expr->location);
          emit(OpCode::Pop, match_expr->location);
          compile_expr(*arm.body);
          end_jumps.push_back(emit_jump(OpCode::Jmp, match_expr->location));
          patch_jump(next_arm);
        } else {
          emit(OpCode::Pop, match_expr->location);
          compile_expr(*arm.body);
        }
      } else if (binding) {
        const uint32_t bind_slot = static_cast<uint32_t>(locals_.size());
        locals_.push_back(Local{.name = binding->name, .is_mutable = false});
        emit_operand(OpCode::LoadLocal, temp_slot, match_expr->location);
        emit_operand(OpCode::StoreLocal, bind_slot, match_expr->location);
        emit(OpCode::Pop, match_expr->location);
        if (arm.guard) {
          compile_expr(*arm.guard);
          const std::size_t next_arm = emit_jump(OpCode::JmpFalse, match_expr->location);
          emit(OpCode::Pop, match_expr->location);
          compile_expr(*arm.body);
          end_jumps.push_back(emit_jump(OpCode::Jmp, match_expr->location));
          patch_jump(next_arm);
        } else {
          compile_expr(*arm.body);
        }
        locals_.pop_back();
      } else if (const auto *arr_pat = dynamic_cast<const ast::ArrayPattern *>(arm.pattern.get())) {
        std::vector<uint32_t> bind_slots;
        std::vector<std::size_t> fail_jumps;
        for (std::size_t i = 0; i < arr_pat->elements.size(); ++i) {
          const auto &elem = arr_pat->elements[i];
          const auto *elem_binding = dynamic_cast<const ast::BindingPattern *>(elem.get());
          const auto *elem_wildcard = dynamic_cast<const ast::IdentifierExpr *>(elem.get());
          if (elem_binding) {
            const uint32_t slot = static_cast<uint32_t>(locals_.size());
            locals_.push_back(Local{.name = elem_binding->name, .is_mutable = false});
            emit_operand(OpCode::LoadLocal, temp_slot, match_expr->location);
            emit_constant(Value::int_value(static_cast<int>(i)), match_expr->location);
            emit(OpCode::IndexGet, match_expr->location);
            emit_operand(OpCode::StoreLocal, slot, match_expr->location);
            emit(OpCode::Pop, match_expr->location);
            bind_slots.push_back(slot);
          } else if (elem_wildcard && elem_wildcard->name == "_") {
            // wildcard element, skip
          } else {
            emit_operand(OpCode::LoadLocal, temp_slot, match_expr->location);
            emit_constant(Value::int_value(static_cast<int>(i)), match_expr->location);
            emit(OpCode::IndexGet, match_expr->location);
            compile_expr(*elem);
            emit(OpCode::Eq, match_expr->location);
            fail_jumps.push_back(emit_jump(OpCode::JmpFalse, match_expr->location));
          }
        }
        if (arm.guard) {
          compile_expr(*arm.guard);
          fail_jumps.push_back(emit_jump(OpCode::JmpFalse, match_expr->location));
          emit(OpCode::Pop, match_expr->location);
        }
        compile_expr(*arm.body);
        end_jumps.push_back(emit_jump(OpCode::Jmp, match_expr->location));
        for (std::size_t fj : fail_jumps) {
          patch_jump(fj);
        }
        for (std::size_t j = 0; j < bind_slots.size(); ++j) {
          locals_.pop_back();
        }
      } else if (const auto *enum_pat = dynamic_cast<const ast::EnumPattern *>(arm.pattern.get())) {
        // Enum pattern: match variant and optionally extract payload
        auto enum_it = enum_indices_.find(enum_pat->enum_name);
        if (enum_it == enum_indices_.end()) {
          error_at(enum_pat->location, "Unknown enum type '" + enum_pat->enum_name + "'.");
          return;
        }
        int type_idx = enum_it->second;
        const auto &meta = chunk_.enum_metas()[static_cast<std::size_t>(type_idx)];
        int variant_idx = -1;
        for (int i = 0; i < static_cast<int>(meta.variants.size()); ++i) {
          if (meta.variants[static_cast<std::size_t>(i)] == enum_pat->variant_name) {
            variant_idx = i;
            break;
          }
        }
        if (variant_idx < 0) {
          error_at(enum_pat->location, "Unknown enum variant '" + enum_pat->variant_name + "'.");
          return;
        }

        std::vector<std::size_t> fail_jumps;
        std::vector<uint32_t> bind_slots;

        // Check if value matches this enum type and variant
        // Stack: [initial_val]
        emit_operand(OpCode::LoadLocal, temp_slot, match_expr->location);
        uint32_t operand = (static_cast<uint32_t>(type_idx) << 16) | static_cast<uint32_t>(variant_idx);
        emit_operand(OpCode::EnumVariant, operand, enum_pat->location);
        emit(OpCode::Eq, enum_pat->location);
        fail_jumps.push_back(emit_jump(OpCode::JmpFalse, enum_pat->location));
        // JmpFalse popped the Eq result; initial_val still on stack.

        // Extract payload bindings — these push/pop around initial_val.
        for (std::size_t i = 0; i < enum_pat->fields.size(); ++i) {
          const auto *field_binding = dynamic_cast<const ast::BindingPattern *>(enum_pat->fields[i].get());
          if (field_binding) {
            const uint32_t slot = static_cast<uint32_t>(locals_.size());
            locals_.push_back(Local{.name = field_binding->name, .is_mutable = false});
            emit_operand(OpCode::LoadLocal, temp_slot, enum_pat->location);
            emit_operand(OpCode::EnumPayloadGet, static_cast<uint32_t>(i), enum_pat->location);
            emit_operand(OpCode::StoreLocal, slot, enum_pat->location);
            emit(OpCode::Pop, enum_pat->location);
            bind_slots.push_back(slot);
          }
        }

        if (arm.guard) {
          compile_expr(*arm.guard);
          fail_jumps.push_back(emit_jump(OpCode::JmpFalse, match_expr->location));
          // Pop initial_val so body result is clean on stack.
          emit(OpCode::Pop, match_expr->location);
        } else {
          // No guard: pop initial_val before body.
          emit(OpCode::Pop, match_expr->location);
        }

        compile_expr(*arm.body);
        end_jumps.push_back(emit_jump(OpCode::Jmp, match_expr->location));

        for (std::size_t fj : fail_jumps) {
          patch_jump(fj);
        }
        for (std::size_t j = 0; j < bind_slots.size(); ++j) {
          locals_.pop_back();
        }
      } else if (const auto *struct_pat = dynamic_cast<const ast::StructPattern *>(arm.pattern.get())) {
        auto struct_it = struct_indices_.find(struct_pat->struct_name);
        if (struct_it == struct_indices_.end()) {
          error_at(struct_pat->location, "Unknown struct type '" + struct_pat->struct_name + "'.");
          return;
        }
        const auto &meta = chunk_.struct_metas()[static_cast<std::size_t>(struct_it->second)];
        std::vector<std::size_t> fail_jumps;
        std::vector<uint32_t> bind_slots;
        for (std::size_t i = 0; i < struct_pat->fields.size(); ++i) {
          const auto &pf = struct_pat->fields[i];
          std::string field_name;
          if (!pf.name.empty()) {
            field_name = pf.name;
          } else if (i < meta.field_names.size()) {
            field_name = meta.field_names[i];
          } else {
            error_at(struct_pat->location, "Struct pattern has too many fields.");
            return;
          }
          const auto *field_binding = dynamic_cast<const ast::BindingPattern *>(pf.pattern.get());
          const auto *field_wildcard = dynamic_cast<const ast::IdentifierExpr *>(pf.pattern.get());
          if (field_binding) {
            const uint32_t slot = static_cast<uint32_t>(locals_.size());
            locals_.push_back(Local{.name = field_binding->name, .is_mutable = false});
            emit_operand(OpCode::LoadLocal, temp_slot, struct_pat->location);
            uint32_t field_const = chunk_.add_constant(Value::string_value(field_name));
            emit_operand(OpCode::FieldGet, field_const, struct_pat->location);
            emit_operand(OpCode::StoreLocal, slot, struct_pat->location);
            emit(OpCode::Pop, struct_pat->location);
            bind_slots.push_back(slot);
          } else if (field_wildcard && field_wildcard->name == "_") {
            continue;
          } else {
            emit_operand(OpCode::LoadLocal, temp_slot, struct_pat->location);
            uint32_t field_const = chunk_.add_constant(Value::string_value(field_name));
            emit_operand(OpCode::FieldGet, field_const, struct_pat->location);
            compile_expr(*pf.pattern);
            emit(OpCode::Eq, struct_pat->location);
            fail_jumps.push_back(emit_jump(OpCode::JmpFalse, struct_pat->location));
          }
        }
        if (arm.guard) {
          compile_expr(*arm.guard);
          fail_jumps.push_back(emit_jump(OpCode::JmpFalse, match_expr->location));
          emit(OpCode::Pop, match_expr->location);
        } else {
          emit(OpCode::Pop, match_expr->location);
        }
        compile_expr(*arm.body);
        end_jumps.push_back(emit_jump(OpCode::Jmp, match_expr->location));
        for (std::size_t fj : fail_jumps) {
          patch_jump(fj);
        }
        for (std::size_t j = 0; j < bind_slots.size(); ++j) {
          locals_.pop_back();
        }
      } else {
        emit_operand(OpCode::LoadLocal, temp_slot, match_expr->location);
        compile_expr(*arm.pattern);
        emit(OpCode::Eq, match_expr->location);
        if (arm.guard) {
          const std::size_t skip_guard = emit_jump(OpCode::JmpFalse, match_expr->location);
          emit(OpCode::Pop, match_expr->location);
          compile_expr(*arm.guard);
          const std::size_t next_arm = emit_jump(OpCode::JmpFalse, match_expr->location);
          emit(OpCode::Pop, match_expr->location);
          compile_expr(*arm.body);
          end_jumps.push_back(emit_jump(OpCode::Jmp, match_expr->location));
          patch_jump(next_arm);
          patch_jump(skip_guard);
        } else {
          const std::size_t next_arm = emit_jump(OpCode::JmpFalse, match_expr->location);
          emit(OpCode::Pop, match_expr->location);
          compile_expr(*arm.body);
          end_jumps.push_back(emit_jump(OpCode::Jmp, match_expr->location));
          patch_jump(next_arm);
        }
      }
    }

    for (std::size_t jump : end_jumps) {
      patch_jump(jump);
    }

    locals_.pop_back();
    return;
  }

  if (const auto *ns_access = dynamic_cast<const ast::NamespaceAccessExpr *>(&expr)) {
    auto enum_it = enum_indices_.find(ns_access->namespace_name);
    if (enum_it != enum_indices_.end()) {
      int type_idx = enum_it->second;
      const auto &meta = chunk_.enum_metas()[static_cast<std::size_t>(type_idx)];
      int variant_idx = -1;
      for (int i = 0; i < static_cast<int>(meta.variants.size()); ++i) {
        if (meta.variants[static_cast<std::size_t>(i)] == ns_access->member_name) {
          variant_idx = i;
          break;
        }
      }
      if (variant_idx < 0) {
        error_at(ns_access->location, "Unknown enum variant '" + ns_access->member_name + "'.");
        return;
      }
      int param_count = meta.variant_param_counts[static_cast<std::size_t>(variant_idx)];
      if (param_count > 0) {
        error_at(ns_access->location, "Enum variant '" + ns_access->member_name + "' requires " +
                 std::to_string(param_count) + " argument(s).");
        return;
      }
      uint32_t operand = (static_cast<uint32_t>(type_idx) << 16) |
                         static_cast<uint32_t>(variant_idx);
      emit_operand(OpCode::EnumVariant, operand, ns_access->location);
      return;
    }
    if (ns_access->namespace_name == "io" && used_.count("io") != 0) {
      NativeFn fn;
      if (ns_access->member_name == "out") {
        fn = NativeFn::IoOut;
      } else if (ns_access->member_name == "err") {
        fn = NativeFn::IoErr;
      } else if (ns_access->member_name == "in") {
        fn = NativeFn::IoIn;
      } else {
        error_at(ns_access->location, "Unknown io member '" + ns_access->member_name + "'.");
        return;
      }
      emit_constant(Value::native_function_value(fn), ns_access->location);
      return;
    }
    if (ns_access->namespace_name == "fs" && used_.count("fs") != 0) {
      NativeFn fn;
      if (ns_access->member_name == "__read") {
        fn = NativeFn::FsRead;
      } else if (ns_access->member_name == "__write") {
        fn = NativeFn::FsWrite;
      } else {
        error_at(ns_access->location, "Unknown fs member '" + ns_access->member_name + "'.");
        return;
      }
      emit_constant(Value::native_function_value(fn), ns_access->location);
      return;
    }
    if (ns_access->namespace_name == "sys" && used_.count("sys") != 0) {
      NativeFn fn;
      if (ns_access->member_name == "args") {
        fn = NativeFn::SysArgs;
      } else {
        error_at(ns_access->location, "Unknown sys member '" + ns_access->member_name + "'.");
        return;
      }
      emit_constant(Value::native_function_value(fn), ns_access->location);
      return;
    }
    // Check concept namespaces for fully-qualified calls (Printable::to_string)
    if (concept_registry_.count(ns_access->namespace_name)) {
      // Defer to calling context: the callee will resolve the trait method
      // based on the argument type. For now, emit the member as a name lookup
      // so the call site can mangle it as Type::method.
      return;
    }
    // Check imported module namespaces
    {
      const std::string qualified =
          resolve_module_qualified(ns_access->namespace_name, ns_access->member_name);
      const std::string prefix = qualified.substr(0, qualified.rfind("::"));
      if (imported_qualifiers_.count(prefix)) {
        auto it = function_indices_.find(qualified);
        if (it != function_indices_.end()) {
          emit_constant(Value::function_value(
                            chunk_.constants()[static_cast<std::size_t>(it->second)].function_idx),
                        ns_access->location);
          return;
        }
        error_at(ns_access->location, "'" + ns_access->member_name + "' is not exported from '" +
                                        prefix + "'.");
        return;
      }
    }
    if (used_.count(ns_access->namespace_name) != 0) {
      return;
    }
    if (ns_access->namespace_name == "io") {
      error_at(ns_access->location,
               "Module 'io' is not imported. Add 'using io;' at the top of the file.");
    } else {
      error_at(ns_access->location, "Unknown namespace '" + ns_access->namespace_name + "'.");
    }
    return;
  }

  if (const auto *struct_lit = dynamic_cast<const ast::StructLiteralExpr *>(&expr)) {
    // Infer omitted type arguments for a generic struct literal from its field
    // values (e.g. `Box { 7 }` -> `Box<int>`), mirroring the checker.
    ast::TypeExpr lit_type = struct_lit->struct_type;
    if (lit_type.type_args.empty() && generic_struct_decls_.count(lit_type.name)) {
      const ast::StructDecl *decl = generic_struct_decls_.at(lit_type.name);
      std::unordered_map<std::string, std::string> inferred;
      for (size_t i = 0; i < decl->fields.size() && i < struct_lit->fields.size(); ++i) {
        const ast::TypeExpr &ft = decl->fields[i].type;
        if (!ft.type_args.empty() || inferred.count(ft.name)) continue;
        bool is_type_param = false;
        for (const std::string &tp : decl->type_params) {
          if (tp == ft.name) { is_type_param = true; break; }
        }
        if (is_type_param) {
          inferred[ft.name] = infer_arg_type_name(*struct_lit->fields[i].value);
        }
      }
      std::vector<std::string> targs;
      for (const std::string &tp : decl->type_params) {
        auto it = inferred.find(tp);
        if (it != inferred.end() && !it->second.empty()) targs.push_back(it->second);
      }
      if (targs.size() == decl->type_params.size()) {
        for (const std::string &n : targs) {
          ast::TypeExpr a;
          a.name = n;
          lit_type.type_args.push_back(a);
        }
      }
    }
    int struct_idx = resolve_struct(lit_type);
    if (struct_idx < 0) {
      std::string type_name = struct_lit->struct_type.to_string();
      error_at(struct_lit->location, "Unknown struct type '" + type_name + "'.");
      return;
    }
    int type_idx = struct_idx;
    const auto &meta = chunk_.struct_metas()[static_cast<std::size_t>(type_idx)];
    for (std::size_t i = 0; i < meta.field_names.size(); ++i) {
      if (i < struct_lit->fields.size()) {
        compile_expr(*struct_lit->fields[i].value);
      } else {
        emit(OpCode::Null, struct_lit->location);
      }
    }
    emit_operand(OpCode::StructNew, static_cast<uint32_t>((type_idx << 16) |
                 static_cast<int>(meta.field_names.size())), struct_lit->location);
    return;
  }

  if (const auto *field_access = dynamic_cast<const ast::FieldAccessExpr *>(&expr)) {
    if (const auto *ns_obj = dynamic_cast<const ast::NamespaceAccessExpr *>(field_access->object.get());
        ns_obj && ns_obj->namespace_name == "io" && used_.count("io") != 0) {
      NativeFn fn;
      if (ns_obj->member_name == "out" && field_access->field_name == "line") {
        fn = NativeFn::IoOutLine;
      } else if (ns_obj->member_name == "err" && field_access->field_name == "line") {
        fn = NativeFn::IoErrLine;
      } else if (ns_obj->member_name == "in" && field_access->field_name == "secret") {
        fn = NativeFn::IoInSecret;
      } else {
        error_at(field_access->location, "Unknown io method '" + ns_obj->member_name + "." + field_access->field_name + "'.");
        return;
      }
      emit_constant(Value::native_function_value(fn), field_access->location);
      return;
    }
    compile_expr(*field_access->object);
    uint32_t field_const = chunk_.add_constant(Value::string_value(field_access->field_name));
    emit_operand(OpCode::FieldGet, field_const, field_access->location);
    return;
  }

  if (const auto *field_assign = dynamic_cast<const ast::FieldAssignExpr *>(&expr)) {
    compile_expr(*field_assign->object);
    compile_expr(*field_assign->value);
    uint32_t field_const = chunk_.add_constant(Value::string_value(field_assign->field_name));
    emit_operand(OpCode::FieldSet, field_const, field_assign->location);
    return;
  }

  if (const auto *array_lit = dynamic_cast<const ast::ArrayLiteralExpr *>(&expr)) {
    DenseArrayLit2D dense_shape;
    if (analyze_dense_array_literal_2d(*array_lit, &dense_shape)) {
      for (const ast::ExprPtr &row_expr : array_lit->elements) {
        const auto *row_lit = dynamic_cast<const ast::ArrayLiteralExpr *>(row_expr.get());
        for (const ast::ExprPtr &cell : row_lit->elements) {
          compile_expr(*cell);
        }
      }
      emit_operand(OpCode::DenseArrayNew,
                   pack_dense2d_shape(dense_shape.rows, dense_shape.cols),
                   array_lit->location);
      return;
    }
    for (const ast::ExprPtr &element : array_lit->elements) {
      compile_expr(*element);
    }
    emit_operand(OpCode::ArrayNew, static_cast<uint32_t>(array_lit->elements.size()),
                 array_lit->location);
    return;
  }

  if (const auto *map_lit = dynamic_cast<const ast::MapLiteralExpr *>(&expr)) {
    for (std::size_t i = 0; i < map_lit->keys.size(); ++i) {
      compile_expr(*map_lit->keys[i]);
      compile_expr(*map_lit->values[i]);
    }
    emit_operand(OpCode::MapNew, static_cast<uint32_t>(map_lit->keys.size()),
                 map_lit->location);
    return;
  }

  if (const auto *index_expr = dynamic_cast<const ast::IndexExpr *>(&expr)) {
    compile_expr(*index_expr->object);
    compile_expr(*index_expr->index);
    emit(OpCode::IndexGet, index_expr->location);
    return;
  }

  if (const auto *index_assign = dynamic_cast<const ast::IndexAssignExpr *>(&expr)) {
    compile_expr(*index_assign->object);
    compile_expr(*index_assign->index);
    compile_expr(*index_assign->value);
    emit(OpCode::IndexSet, index_assign->location);
    return;
  }

  if (const auto *cast = dynamic_cast<const ast::CastExpr *>(&expr)) {
    compile_expr(*cast->value);
    int target_kind = -1;
    const std::string &t = cast->target_type.name;
    if (t == "int") target_kind = 0;
    else if (t == "float") target_kind = 1;
    else if (t == "string") target_kind = 2;
    else if (t == "char" || t == "byte") target_kind = 3;
    if (target_kind < 0) {
      error_at(cast->location, "Cast target '" + t + "' is not supported in VM compiler.");
      return;
    }
    emit_operand(OpCode::CastTo, static_cast<uint32_t>(target_kind), cast->location);
    return;
  }

  if (const auto *ternary = dynamic_cast<const ast::TernaryExpr *>(&expr)) {
    compile_expr(*ternary->condition);
    const std::size_t else_jump = emit_jump(OpCode::JmpFalse, ternary->location);
    compile_expr(*ternary->then_expr);
    const std::size_t end_jump = emit_jump(OpCode::Jmp, ternary->location);
    patch_jump(else_jump);
    compile_expr(*ternary->else_expr);
    patch_jump(end_jump);
    return;
  }

  if (const auto *null_coalesce = dynamic_cast<const ast::NullCoalesceExpr *>(&expr)) {
    compile_expr(*null_coalesce->left);
    // Peek-and-branch on null / CastError. Success arm keeps the original LHS;
    // fallback arm sees the error value (Pop for bare ?:, or bind with let err =>).
    const std::size_t fallback_jump = emit_jump(OpCode::JmpIfErr, null_coalesce->location);
    const std::size_t end_jump = emit_jump(OpCode::Jmp, null_coalesce->location);
    patch_jump(fallback_jump);
    if (null_coalesce->err_binding.empty()) {
      emit(OpCode::Pop, null_coalesce->location);
      compile_expr(*null_coalesce->right);
    } else {
      const uint32_t err_slot = static_cast<uint32_t>(locals_.size());
      locals_.push_back(Local{.name = null_coalesce->err_binding, .is_mutable = false});
      emit_operand(OpCode::StoreLocal, err_slot, null_coalesce->location);
      compile_expr(*null_coalesce->right);
      locals_.pop_back();
    }
    patch_jump(end_jump);
    return;
  }

  if (const auto *prop = dynamic_cast<const ast::PropagateExpr *>(&expr)) {
    compile_expr(*prop->value);
    if (in_try_) {
      // Inside a try block: use PropagateErr which peeks the stack and
      // either no-ops (success) or pops + jumps to catch via handler_stack_.
      emit(OpCode::PropagateErr, prop->location);
    } else {
      // Function-level: JmpIfErr → Pop + Null + Return.
      const std::size_t err_jump = emit_jump(OpCode::JmpIfErr, prop->location);
      const std::size_t end_jump = emit_jump(OpCode::Jmp, prop->location);
      patch_jump(err_jump);
      emit(OpCode::Pop, prop->location);
      emit(OpCode::Null, prop->location);
      emit(OpCode::Return, prop->location);
      patch_jump(end_jump);
    }
    return;
  }

  error_at(expr.location, "Unsupported expression in VM compiler.");
}

void Compiler::compile_assignment(const ast::AssignExpr &assign) {
  const int slot = resolve_local(assign.name);
  if (slot < 0) {
    error_at(assign.location, "Assignment to undeclared variable '" + assign.name + "'.");
    return;
  }
  if (!locals_[static_cast<std::size_t>(slot)].is_mutable) {
    error_at(assign.location, "Cannot assign to const variable '" + assign.name + "'.");
    return;
  }

  if (assign.op == ast::AssignOp::Assign) {
    compile_expr(*assign.value);
  } else {
    emit_operand(OpCode::LoadLocal, static_cast<uint32_t>(slot), assign.location);
    compile_expr(*assign.value);
    switch (assign.op) {
    case ast::AssignOp::AddAssign:
      emit(OpCode::Add, assign.location);
      break;
    case ast::AssignOp::SubAssign:
      emit(OpCode::Subtract, assign.location);
      break;
    case ast::AssignOp::MulAssign:
      emit(OpCode::Multiply, assign.location);
      break;
    case ast::AssignOp::DivAssign:
      emit(OpCode::Divide, assign.location);
      break;
    default:
      error_at(assign.location, "Unsupported assignment operator.");
      return;
    }
  }

  emit_operand(OpCode::StoreLocal, static_cast<uint32_t>(slot), assign.location);
}

void Compiler::emit(OpCode op, ast::SourceLocation location) {
  kir_instr_at_bc_[chunk_.instructions().size()] = kir_recorder_.instr_count();
  kir_recorder_.on_emit(op, 0, location);
  emitter_.emit(op, location);
}

void Compiler::emit_operand(OpCode op, uint32_t operand, ast::SourceLocation location) {
  kir_instr_at_bc_[chunk_.instructions().size()] = kir_recorder_.instr_count();
  kir_recorder_.on_emit(op, operand, location);
  emitter_.emit_operand(op, operand, location);
}

void Compiler::emit_constant(Value value, ast::SourceLocation location, KirType numeric_type) {
  kir_instr_at_bc_[chunk_.instructions().size()] = kir_recorder_.instr_count();
  const uint32_t pool_index = chunk_.add_constant(value);
  emitter_.emit_operand(OpCode::Constant, pool_index, location);
  kir_recorder_.on_constant(value, pool_index, location, numeric_type);
}

std::size_t Compiler::emit_jump(OpCode op, ast::SourceLocation location) {
  kir_instr_at_bc_[chunk_.instructions().size()] = kir_recorder_.instr_count();
  kir_recorder_.record_jump(op, location);
  return emitter_.emit_jump(op, location);
}

void Compiler::patch_jump(std::size_t offset) {
  emitter_.patch_jump(offset);
  if (offset >= chunk_.instructions().size()) {
    return;
  }
  const auto jump_it = kir_instr_at_bc_.find(offset);
  if (jump_it == kir_instr_at_bc_.end()) {
    return;
  }
  const int32_t rel = chunk_.instructions()[offset].operand;
  const std::size_t bc_target = offset + 1 + static_cast<std::size_t>(rel);
  std::size_t kir_target = 0;
  if (bc_target == chunk_.instructions().size()) {
    kir_target = kir_recorder_.instr_count();
  } else {
    const auto target_it = kir_instr_at_bc_.find(bc_target);
    if (target_it == kir_instr_at_bc_.end()) {
      return;
    }
    kir_target = target_it->second;
  }
  const int32_t kir_rel =
      static_cast<int32_t>(kir_target) - static_cast<int32_t>(jump_it->second) - 1;
  kir_recorder_.patch_jump(jump_it->second, kir_rel);
}

void Compiler::patch_jump_to(std::size_t offset, std::size_t target) {
  emitter_.patch_jump_to(offset, target);
  const auto jump_it = kir_instr_at_bc_.find(offset);
  if (jump_it == kir_instr_at_bc_.end()) {
    return;
  }
  std::size_t kir_target = 0;
  if (target == chunk_.instructions().size()) {
    kir_target = kir_recorder_.instr_count();
  } else {
    const auto target_it = kir_instr_at_bc_.find(target);
    if (target_it == kir_instr_at_bc_.end()) {
      return;
    }
    kir_target = target_it->second;
  }
  const int32_t kir_rel =
      static_cast<int32_t>(kir_target) - static_cast<int32_t>(jump_it->second) - 1;
  kir_recorder_.patch_jump(jump_it->second, kir_rel);
}

int Compiler::resolve_local(const std::string &name) const {
  for (std::size_t i = locals_.size(); i > 0; --i) {
    if (locals_[i - 1].name == name) {
      return static_cast<int>(i - 1);
    }
  }
  return -1;
}

bool Compiler::declare_local(const ast::VarDeclStmt &var_decl, uint32_t *slot) {
  if (resolve_local(var_decl.name) >= 0) {
    error_at(var_decl.location, "Duplicate variable declaration '" + var_decl.name + "'.");
    return false;
  }
  const bool is_mutable = var_decl.storage != "const";
  locals_.push_back(Local{
      .name = var_decl.name,
      .is_mutable = is_mutable,
  });
  *slot = static_cast<uint32_t>(locals_.size() - 1);
  return true;
}

int Compiler::resolve_struct(const ast::TypeExpr &type) {
  if (type.type_args.empty()) {
    auto it = struct_indices_.find(type.name);
    if (it != struct_indices_.end()) return it->second;
    return -1;
  }
  std::string mangled = type.name;
  for (const auto &arg : type.type_args) {
    mangled += "__" + arg.to_string();
  }
  auto it = struct_indices_.find(mangled);
  if (it != struct_indices_.end()) return it->second;
  auto gen_it = generic_struct_decls_.find(type.name);
  if (gen_it == generic_struct_decls_.end()) return -1;
  const ast::StructDecl *decl = gen_it->second;
  StructMeta meta;
  meta.name = mangled;
  for (const auto &field : decl->fields) {
    meta.field_names.push_back(field.name);
  }
  int idx = chunk_.add_struct_meta(std::move(meta));
  struct_indices_[mangled] = idx;
  return idx;
}

void Compiler::error_at(ast::SourceLocation location, std::string message) {
  errors_.push_back(CompileError{
      .location = location,
      .message = std::move(message),
  });
}

void Compiler::warning_at(ast::SourceLocation location, std::string message) {
  warnings_.push_back(CompileWarning{
      .location = location,
      .message = std::move(message),
  });
}

void Compiler::process_import(const ast::ImportDecl &import_decl) {
  if (!module_loader_) {
    error_at(import_decl.location, "Import not supported (no module loader configured).");
    return;
  }

  process_import_from(import_decl, /*importing_file_dir=*/"");
}

void Compiler::process_import_from(const ast::ImportDecl &import_decl, const std::string &importing_file_dir) {
  if (!module_loader_) {
    error_at(import_decl.location, "Import not supported (no module loader configured).");
    return;
  }

  auto result = importing_file_dir.empty()
      ? module_loader_->load(import_decl.path)
      : module_loader_->load_from(import_decl.path, importing_file_dir);
  if (!result.module) {
    error_at(import_decl.location, result.error);
    return;
  }

  const ParsedModule &mod = *result.module;

  // Process each module once. Diamond dependencies (token/ast/bytecode reached
  // via several import paths) would otherwise re-register functions and
  // re-compile imported bodies repeatedly — exponential on a deep DAG.
  if (!processed_modules_.insert(mod.resolved_path).second) {
    return;
  }

  // Use the resolved canonical path from load() to determine the module
  // directory for transitive imports — works for both // and relative paths.
  std::string mod_dir = std::filesystem::path(mod.resolved_path).parent_path().string();

  // Recursively process imports declared inside the loaded module so that
  // types and functions it depends on are registered before we compile calls
  // into it (e.g. scanner.kl imports token.kl for the Token struct).
  if (mod.program) {
    for (const auto &inner_decl : mod.program->declarations) {
      if (const auto *inner_import = dynamic_cast<const ast::ImportDecl *>(inner_decl.get())) {
        process_import_from(*inner_import, mod_dir);
      } else if (const auto *inner_logical =
                     dynamic_cast<const ast::LogicalImportDecl *>(inner_decl.get())) {
        process_logical_import(*inner_logical);
      } else if (const auto *inner_block =
                     dynamic_cast<const ast::ImportBlockDecl *>(inner_decl.get())) {
        for (const auto &imp : inner_block->imports) {
          if (const auto *id = dynamic_cast<const ast::ImportDecl *>(imp.get())) {
            process_import_from(*id, mod_dir);
          }
        }
      }
    }
  }

  std::string ns = import_decl.alias.empty() ? mod.namespace_name : import_decl.alias;

  imported_namespaces_.insert(ns);
  namespace_source_paths_[ns] = mod.resolved_path;

  for (const auto *func : mod.public_functions) {
    if (!import_decl.selected_symbols.empty()) {
      bool found = false;
      for (const auto &s : import_decl.selected_symbols) {
        if (s == func->name) { found = true; break; }
      }
      if (!found) continue;
    }

    int idx = chunk_.add_function(FunctionInfo{
        .name = func->name,
        .entry = 0,
        .param_count = static_cast<int>(func->params.size()),
    });
    record_function_source(idx, mod.resolved_path);
    uint32_t const_idx = chunk_.add_constant(Value::function_value(idx));

    std::string qualified = ns + "::" + func->name;
    function_indices_[qualified] = static_cast<int>(const_idx);

    if (!import_decl.selected_symbols.empty()) {
      function_indices_[func->name] = static_cast<int>(const_idx);
    }

    imported_function_decls_[ns].push_back(func);
  }

  // Also register private functions so pub functions can call them.
  for (const auto *func : mod.private_functions) {
    if (function_indices_.count(ns + "::" + func->name)) continue;
    int idx = chunk_.add_function(FunctionInfo{
        .name = func->name,
        .entry = 0,
        .param_count = static_cast<int>(func->params.size()),
    });
    record_function_source(idx, mod.resolved_path);
    uint32_t const_idx = chunk_.add_constant(Value::function_value(idx));
    function_indices_[ns + "::" + func->name] = static_cast<int>(const_idx);
    imported_function_decls_[ns].push_back(func);
  }

  // Register imported pub structs so struct literals and field access compile.
  for (const auto *sd : mod.public_structs) {
    if (!import_decl.selected_symbols.empty()) {
      bool found = false;
      for (const auto &s : import_decl.selected_symbols) {
        if (s == sd->name) { found = true; break; }
      }
      if (!found) continue;
    }
    if (sd->type_params.empty()) {
      StructMeta meta;
      meta.name = sd->name;
      for (const auto &field : sd->fields) {
        meta.field_names.push_back(field.name);
      }
      int idx = chunk_.add_struct_meta(std::move(meta));
      struct_indices_[sd->name] = idx;
    }
  }

  // Register imported pub enums so enum variants compile.
  for (const auto *ed : mod.public_enums) {
    if (!import_decl.selected_symbols.empty()) {
      bool found = false;
      for (const auto &s : import_decl.selected_symbols) {
        if (s == ed->name) { found = true; break; }
      }
      if (!found) continue;
    }
    EnumMeta meta;
    meta.name = ed->name;
    for (const auto &v : ed->variants) {
      meta.variants.push_back(v.name);
      meta.variant_param_counts.push_back(static_cast<int>(v.param_types.size()));
    }
    int idx = chunk_.add_enum_meta(std::move(meta));
    enum_indices_[ed->name] = idx;
  }

  // Register private structs/enums (needed by private helper functions).
  for (const auto *sd : mod.private_structs) {
    if (struct_indices_.count(sd->name)) continue;
    if (sd->type_params.empty()) {
      StructMeta meta;
      meta.name = sd->name;
      for (const auto &field : sd->fields) meta.field_names.push_back(field.name);
      int idx = chunk_.add_struct_meta(std::move(meta));
      struct_indices_[sd->name] = idx;
    }
  }
  for (const auto *ed : mod.private_enums) {
    if (enum_indices_.count(ed->name)) continue;
    EnumMeta meta;
    meta.name = ed->name;
    for (const auto &v : ed->variants) {
      meta.variants.push_back(v.name);
      meta.variant_param_counts.push_back(static_cast<int>(v.param_types.size()));
    }
    int idx = chunk_.add_enum_meta(std::move(meta));
    enum_indices_[ed->name] = idx;
  }

}

void Compiler::process_logical_import(const ast::LogicalImportDecl &import_decl) {
  if (!module_loader_) {
    error_at(import_decl.location, "Import not supported (no module loader configured).");
    return;
  }
  // Manifest entry first, then directory-as-module (auto-import every
  // <module_id>/*.kl), removing the need for a _dir.kl manifest.
  auto result = module_loader_->resolve_logical(import_decl.module_id);
  for (const ParsedModule *mod : result.modules) {
    register_imported_module(*mod);
  }
  if (result.modules.empty()) {
    error_at(import_decl.location,
             result.error.empty() ? ("Unknown module '" + import_decl.module_id + "'")
                                  : result.error);
  } else if (!result.error.empty()) {
    error_at(import_decl.location, result.error);
  }
}

void Compiler::register_imported_module(const ParsedModule &mod) {
  if (!processed_modules_.insert(mod.resolved_path).second) {
    return;
  }

  if (mod.program) {
    for (const auto &inner_decl : mod.program->declarations) {
      if (const auto *inner_import = dynamic_cast<const ast::ImportDecl *>(inner_decl.get())) {
        std::string mod_dir = std::filesystem::path(mod.resolved_path).parent_path().string();
        process_import_from(*inner_import, mod_dir);
      } else if (const auto *inner_logical =
                     dynamic_cast<const ast::LogicalImportDecl *>(inner_decl.get())) {
        process_logical_import(*inner_logical);
      } else if (const auto *inner_block =
                     dynamic_cast<const ast::ImportBlockDecl *>(inner_decl.get())) {
        std::string mod_dir = std::filesystem::path(mod.resolved_path).parent_path().string();
        for (const auto &imp : inner_block->imports) {
          if (const auto *id = dynamic_cast<const ast::ImportDecl *>(imp.get())) {
            process_import_from(*id, mod_dir);
          }
        }
      }
    }
  }

  const std::string ns = mod.namespace_name;
  const std::string qual = module_id_to_qualifier(ns);
  imported_namespaces_.insert(ns);
  imported_qualifiers_.insert(qual);
  namespace_source_paths_[ns] = mod.resolved_path;

  for (const auto *func : mod.public_functions) {
    int idx = chunk_.add_function(FunctionInfo{
        .name = func->name,
        .entry = 0,
        .param_count = static_cast<int>(func->params.size()),
    });
    record_function_source(idx, mod.resolved_path);
    uint32_t const_idx = chunk_.add_constant(Value::function_value(idx));
    function_indices_[qual + "::" + func->name] = static_cast<int>(const_idx);
    imported_function_decls_[ns].push_back(func);
  }

  for (const auto *func : mod.private_functions) {
    if (function_indices_.count(qual + "::" + func->name)) continue;
    int idx = chunk_.add_function(FunctionInfo{
        .name = func->name,
        .entry = 0,
        .param_count = static_cast<int>(func->params.size()),
    });
    record_function_source(idx, mod.resolved_path);
    uint32_t const_idx = chunk_.add_constant(Value::function_value(idx));
    function_indices_[qual + "::" + func->name] = static_cast<int>(const_idx);
    imported_function_decls_[ns].push_back(func);
  }

  for (const auto *sd : mod.public_structs) {
    if (sd->type_params.empty()) {
      StructMeta meta;
      meta.name = sd->name;
      for (const auto &field : sd->fields) meta.field_names.push_back(field.name);
      int idx = chunk_.add_struct_meta(std::move(meta));
      struct_indices_[sd->name] = idx;
    }
  }

  for (const auto *ed : mod.public_enums) {
    EnumMeta meta;
    meta.name = ed->name;
    for (const auto &v : ed->variants) {
      meta.variants.push_back(v.name);
      meta.variant_param_counts.push_back(static_cast<int>(v.param_types.size()));
    }
    int idx = chunk_.add_enum_meta(std::move(meta));
    enum_indices_[ed->name] = idx;
  }

  for (const auto *sd : mod.private_structs) {
    if (struct_indices_.count(sd->name)) continue;
    if (sd->type_params.empty()) {
      StructMeta meta;
      meta.name = sd->name;
      for (const auto &field : sd->fields) meta.field_names.push_back(field.name);
      int idx = chunk_.add_struct_meta(std::move(meta));
      struct_indices_[sd->name] = idx;
    }
  }
  for (const auto *ed : mod.private_enums) {
    if (enum_indices_.count(ed->name)) continue;
    EnumMeta meta;
    meta.name = ed->name;
    for (const auto &v : ed->variants) {
      meta.variants.push_back(v.name);
      meta.variant_param_counts.push_back(static_cast<int>(v.param_types.size()));
    }
    int idx = chunk_.add_enum_meta(std::move(meta));
    enum_indices_[ed->name] = idx;
  }
}

std::string Compiler::resolve_module_qualified(const std::string &ns,
                                               const std::string &member) const {
  auto it = module_aliases_.find(ns);
  const std::string prefix = it != module_aliases_.end() ? it->second : ns;
  return prefix + "::" + member;
}

void Compiler::open_imported_namespace(const std::string &module_id) {
  if (!imported_namespaces_.count(module_id)) {
    return;
  }
  const std::string prefix = module_id_to_qualifier(module_id) + "::";
  std::vector<std::string> qualified_keys;
  qualified_keys.reserve(function_indices_.size());
  for (const auto &[qualified, _] : function_indices_) {
    qualified_keys.push_back(qualified);
  }
  std::sort(qualified_keys.begin(), qualified_keys.end());
  for (const auto &qualified : qualified_keys) {
    if (qualified.rfind(prefix, 0) != 0) {
      continue;
    }
    const std::string bare = qualified.substr(prefix.size());
    if (!bare.empty() && bare.find("::") == std::string::npos) {
      function_indices_[bare] = function_indices_.at(qualified);
    }
  }
}

} // namespace kinglet
