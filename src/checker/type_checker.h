#pragma once

#include "ast/ast.h"
#include "ir/kir.h"
#include "types/types.h"

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

enum class DiagnosticSeverity { Error = 1, Warning = 2, Info = 3, Hint = 4 };

struct TypeError {
  ast::SourceLocation location;
  std::string message;
  DiagnosticSeverity severity = DiagnosticSeverity::Error;
};

struct TypeCheckResult {
  std::vector<TypeError> errors;
};

class TypeChecker {
public:
  TypeCheckResult check(const ast::Program &program);
  void set_module_loader(ModuleLoader *loader) { module_loader_ = loader; }
  void populate_kir_types(KirModule *module) const;

private:
  void check_function(const ast::FunctionDecl &function);
  void check_stmt(const ast::Stmt &stmt, const Type &expected_return);
  Type check_expr(const ast::Expr &expr);

  void push_scope();
  void pop_scope();
  void declare_var(const std::string &name, const Type &type, bool is_mutable, ast::SourceLocation loc = {});
  std::optional<Type> lookup_var(const std::string &name);
  std::optional<Type> lookup_type(const std::string &name) const;
  Type resolve_type_name(const std::string &name) const;
  Type resolve_type_expr(const ast::TypeExpr &expr, ast::SourceLocation loc = {});
  std::string mangle_name(const std::string &base, const std::vector<ast::TypeExpr> &args) const;
  void instantiate_generic_struct(const ast::StructDecl *decl, const std::vector<ast::TypeExpr> &args);
  void error_at(ast::SourceLocation location, std::string message);
  void warn_at(ast::SourceLocation location, std::string message);
  void check_fmt_args(const std::vector<ast::ExprPtr> &args, ast::SourceLocation location);
  void forward_declare_imported_types(const ParsedModule &mod);

  struct VarInfo {
    Type type;
    bool is_mutable;
    bool used = false;
    ast::SourceLocation location;
  };

  std::vector<std::unordered_map<std::string, VarInfo>> scopes_;
  std::unordered_map<std::string, Type> type_registry_;
  std::unordered_map<std::string, const ast::StructDecl *> generic_structs_;
  std::unordered_map<std::string, const ast::FunctionDecl *> generic_functions_;
  std::unordered_set<std::string> instantiated_;

  struct MethodInfo {
    const ast::FunctionDecl *decl;
    std::string target_type;
  };
  std::unordered_map<std::string, MethodInfo> method_registry_;

  std::unordered_map<std::string, const ast::ConceptDecl *> concept_registry_;

  std::vector<TypeError> errors_;
  std::unordered_map<std::string, KirFunctionSig> kir_function_sigs_;
  std::unordered_set<std::string> used_;    // using io;
  std::unordered_set<std::string> opened_;  // using namespace io;
  std::unordered_set<std::string> imported_namespaces_;
  std::unordered_set<std::string> imported_bare_names_;  // for selective imports
  // Bare names brought in by `using io { out }` for a system namespace
  // (io/fs/sys), mapped to the (namespace, member) they alias. These name
  // native members, so they resolve structurally rather than via the symbol
  // table.
  std::unordered_map<std::string, std::pair<std::string, std::string>> using_aliases_;
  // Per-namespace exported / private symbol names, populated when an import is
  // processed. Used to give a precise diagnostic for `using mod { sym };` when
  // a symbol is missing from the module or exists but is not pub.
  std::unordered_map<std::string, std::unordered_set<std::string>> module_public_symbols_;
  std::unordered_map<std::string, std::unordered_set<std::string>> module_private_symbols_;
  int loop_depth_ = 0;
  const ast::ExprStmt *implicit_return_stmt_ = nullptr;
  ModuleLoader *module_loader_ = nullptr;
};

} // namespace kinglet
