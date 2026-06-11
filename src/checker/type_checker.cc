#include "checker/type_checker.h"

#include "module/module_loader.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace kinglet {

namespace {

bool match_arm_is_catchall(const ast::MatchArm &arm) {
  if (arm.guard) {
    return false;
  }
  if (dynamic_cast<const ast::BindingPattern *>(arm.pattern.get())) {
    return true;
  }
  const auto *id = dynamic_cast<const ast::IdentifierExpr *>(arm.pattern.get());
  return id && id->name == "_";
}

bool pattern_is_null_literal(const ast::Expr *pattern) {
  return pattern && dynamic_cast<const ast::NullLiteralExpr *>(pattern);
}

bool pattern_is_binding_or_wildcard(const ast::Expr *pattern) {
  if (!pattern) {
    return false;
  }
  if (dynamic_cast<const ast::BindingPattern *>(pattern)) {
    return true;
  }
  const auto *id = dynamic_cast<const ast::IdentifierExpr *>(pattern);
  return id && id->name == "_";
}

bool payload_pattern_is_exhaustive(const ast::Expr *pattern, const Type &payload_type) {
  if (!pattern) {
    return false;
  }
  if (pattern_is_binding_or_wildcard(pattern)) {
    return true;
  }
  if (payload_type.kind == TypeKind::Bool || payload_type.kind == TypeKind::Int ||
      payload_type.kind == TypeKind::Float || payload_type.kind == TypeKind::Char ||
      payload_type.kind == TypeKind::String) {
    return false;
  }
  if (payload_type.kind == TypeKind::Enum) {
    const auto *ep = dynamic_cast<const ast::EnumPattern *>(pattern);
    if (!ep || ep->enum_name != payload_type.name) {
      return false;
    }
    int variant_idx = -1;
    for (std::size_t i = 0; i < payload_type.variants.size(); ++i) {
      if (payload_type.variants[i] == ep->variant_name) {
        variant_idx = static_cast<int>(i);
        break;
      }
    }
    if (variant_idx < 0) {
      return false;
    }
    const auto &params = payload_type.variant_param_types[static_cast<std::size_t>(variant_idx)];
    if (params.empty()) {
      return true;
    }
    if (ep->fields.size() != params.size()) {
      return false;
    }
    for (std::size_t i = 0; i < params.size(); ++i) {
      if (!payload_pattern_is_exhaustive(ep->fields[i].get(), params[i])) {
        return false;
      }
    }
    return true;
  }
  return false;
}

std::string join_csv(const std::vector<std::string> &items) {
  std::string out;
  for (const auto &item : items) {
    if (!out.empty()) {
      out += ", ";
    }
    out += item;
  }
  return out;
}

bool arms_cover_bool(const std::vector<ast::MatchArm> &arms, bool &missing_true, bool &missing_false,
                     bool skip_null_patterns = false) {
  missing_true = missing_false = false;
  bool has_true = false;
  bool has_false = false;
  for (const auto &arm : arms) {
    if (skip_null_patterns && pattern_is_null_literal(arm.pattern.get())) {
      continue;
    }
    if (match_arm_is_catchall(arm)) {
      return true;
    }
    if (const auto *bl = dynamic_cast<const ast::BoolLiteralExpr *>(arm.pattern.get())) {
      if (bl->value) {
        has_true = true;
      } else {
        has_false = true;
      }
    }
  }
  missing_true = !has_true;
  missing_false = !has_false;
  return has_true && has_false;
}

std::optional<std::string> check_enum_arms_exhaustive(const std::vector<ast::MatchArm> &arms,
                                                      const Type &enum_type,
                                                      bool skip_null_patterns = false) {
  if (enum_type.variants.empty()) {
    return std::nullopt;
  }
  std::unordered_set<std::string> uncovered(enum_type.variants.begin(), enum_type.variants.end());
  std::vector<std::string> partial_payload;
  for (const auto &arm : arms) {
    if (skip_null_patterns && pattern_is_null_literal(arm.pattern.get())) {
      continue;
    }
    if (match_arm_is_catchall(arm)) {
      uncovered.clear();
      break;
    }
    const auto *ep = dynamic_cast<const ast::EnumPattern *>(arm.pattern.get());
    if (!ep) {
      continue;
    }
    int variant_idx = -1;
    for (std::size_t i = 0; i < enum_type.variants.size(); ++i) {
      if (enum_type.variants[i] == ep->variant_name) {
        variant_idx = static_cast<int>(i);
        break;
      }
    }
    if (variant_idx < 0) {
      continue;
    }
    const auto &params = enum_type.variant_param_types[static_cast<std::size_t>(variant_idx)];
    if (params.empty()) {
      uncovered.erase(ep->variant_name);
      continue;
    }
    if (ep->fields.size() != params.size()) {
      partial_payload.push_back(ep->variant_name);
      continue;
    }
    bool all_exhaustive = true;
    for (std::size_t i = 0; i < params.size(); ++i) {
      if (!payload_pattern_is_exhaustive(ep->fields[i].get(), params[i])) {
        all_exhaustive = false;
        break;
      }
    }
    if (all_exhaustive) {
      uncovered.erase(ep->variant_name);
    } else {
      partial_payload.push_back(ep->variant_name);
    }
  }
  if (!partial_payload.empty()) {
    return "Non-exhaustive payload pattern for variant(s): " + join_csv(partial_payload) + ".";
  }
  if (!uncovered.empty()) {
    std::vector<std::string> missing;
    for (const auto &v : enum_type.variants) {
      if (uncovered.count(v)) {
        missing.push_back(v);
      }
    }
    return "Non-exhaustive match. Missing variant(s): " + join_csv(missing) + ".";
  }
  return std::nullopt;
}

std::optional<std::string> check_nullable_arms_exhaustive(const std::vector<ast::MatchArm> &arms,
                                                          const Type &nullable_type) {
  for (const auto &arm : arms) {
    if (match_arm_is_catchall(arm)) {
      return std::nullopt;
    }
  }
  bool has_null = false;
  bool has_non_null_binding = false;
  bool has_non_null_arm = false;
  for (const auto &arm : arms) {
    if (pattern_is_null_literal(arm.pattern.get())) {
      has_null = true;
      continue;
    }
    has_non_null_arm = true;
    if (pattern_is_binding_or_wildcard(arm.pattern.get()) && !arm.guard) {
      has_non_null_binding = true;
    }
  }
  if (!has_null) {
    return "Non-exhaustive match on nullable type. Missing case: null.";
  }
  if (has_non_null_binding) {
    return std::nullopt;
  }
  if (!has_non_null_arm) {
    return "Non-exhaustive match on nullable type. Missing non-null case.";
  }
  Type inner = nullable_type;
  inner.nullable = false;
  if (inner.kind == TypeKind::Bool) {
    bool missing_true = false;
    bool missing_false = false;
    if (!arms_cover_bool(arms, missing_true, missing_false, true)) {
      std::string missing;
      if (missing_true) {
        missing = "true";
      }
      if (missing_false) {
        if (!missing.empty()) {
          missing += ", ";
        }
        missing += "false";
      }
      return "Non-exhaustive match on nullable type. Missing non-null case(s): " + missing + ".";
    }
    return std::nullopt;
  }
  if (inner.kind == TypeKind::Enum) {
    if (auto err = check_enum_arms_exhaustive(arms, inner, true)) {
      return "Non-exhaustive match on nullable type. " + *err;
    }
    return std::nullopt;
  }
  return "Non-exhaustive match on nullable type. Missing non-null case.";
}

std::string type_to_string(const Type &type) {
  if (type.kind == TypeKind::Struct || type.kind == TypeKind::Enum) {
    return type.name;
  }
  if (type.kind == TypeKind::Array && type.element_type) {
    return type_to_string(*type.element_type) + "[]";
  }
  std::ostringstream oss;
  oss << type.kind;
  return oss.str();
}

// Recursively replace type-parameter names with their concrete arguments
// anywhere they appear in a TypeExpr, including nested positions like the
// element of an array (`T[]` => `Array<T>`) or another generic's argument
// (`Box<T>`). A non-recursive, top-level-only substitution misses these.
ast::TypeExpr substitute_type_params(
    const ast::TypeExpr &expr,
    const std::unordered_map<std::string, ast::TypeExpr> &subst) {
  if (expr.type_args.empty()) {
    auto it = subst.find(expr.name);
    if (it != subst.end()) return it->second;
    return expr;
  }
  ast::TypeExpr result;
  result.name = expr.name;
  for (const auto &arg : expr.type_args) {
    result.type_args.push_back(substitute_type_params(arg, subst));
  }
  return result;
}

// Map a resolved type back to a source-level type expression, so an inferred
// argument type can flow through the same substitution path as an explicit
// type argument. Builtins use their source spelling; named types (structs,
// enums) fall back to their registered name.
ast::TypeExpr type_to_type_expr(const Type &t) {
  ast::TypeExpr te;
  switch (t.kind) {
    case TypeKind::Int: te.name = "int"; break;
    case TypeKind::Float: te.name = "float"; break;
    case TypeKind::Bool: te.name = "bool"; break;
    case TypeKind::Char: te.name = "char"; break;
    case TypeKind::String: te.name = "string"; break;
    default: te.name = t.name; break;
  }
  return te;
}

KirType kir_type_from(const Type &type) {
  switch (type.kind) {
  case TypeKind::Int:
    return KirType::Int;
  case TypeKind::Float:
    return KirType::Float;
  case TypeKind::Bool:
    return KirType::Bool;
  case TypeKind::Null:
    return KirType::Null;
  case TypeKind::Char:
    return KirType::Char;
  case TypeKind::String:
    return KirType::String;
  case TypeKind::Void:
    return KirType::Void;
  case TypeKind::Array:
    return KirType::Array;
  case TypeKind::Map:
    return KirType::Map;
  case TypeKind::Struct:
    return KirType::Struct;
  case TypeKind::Enum:
    return KirType::Enum;
  case TypeKind::Function:
    return KirType::Fn;
  }
  return KirType::Any;
}

KirFunctionSig kir_sig_from(const Type &func_type) {
  KirFunctionSig sig;
  for (const Type &param : func_type.param_types) {
    sig.param_types.push_back(kir_type_from(param));
  }
  if (func_type.return_type) {
    sig.return_type = kir_type_from(*func_type.return_type);
  }
  return sig;
}

} // namespace

Type TypeChecker::resolve_type_name(const std::string &name) const {
  if (name == "int" || name == "auto") {
    return int_type();
  }
  if (name == "float" || name == "double") {
    return float_type();
  }
  if (name == "bool") {
    return bool_type();
  }
  if (name == "string") {
    return string_type();
  }
  if (name == "char" || name == "byte") {
    return char_type();
  }
  if (name == "void") {
    return void_type();
  }
  auto it = type_registry_.find(name);
  if (it != type_registry_.end()) {
    return it->second;
  }
  Type err(TypeKind::Void);
  err.name = "<unknown:" + name + ">";
  return err;
}

Type TypeChecker::resolve_type_expr(const ast::TypeExpr &expr, ast::SourceLocation loc) {
  if (expr.name == "Nullable") {
    if (expr.type_args.size() != 1) {
      if (loc.line > 0) {
        error_at(loc, "Nullable type requires one inner type.");
      }
      return int_type();
    }
    Type inner = resolve_type_expr(expr.type_args[0], loc);
    inner.nullable = true;
    return inner;
  }
  if (expr.name == "Array") {
    if (expr.type_args.size() != 1) {
      if (loc.line > 0) {
        error_at(loc, "Array type requires one element type.");
      }
      return array_type(int_type());
    }
    return array_type(resolve_type_expr(expr.type_args[0], loc));
  }
  if (expr.name == "Map") {
    if (expr.type_args.size() != 2) {
      if (loc.line > 0) {
        error_at(loc, "Map type requires a key and a value type.");
      }
      return map_type(string_type(), int_type());
    }
    Type key = resolve_type_expr(expr.type_args[0], loc);
    if (key.kind != TypeKind::String && key.kind != TypeKind::Int && loc.line > 0) {
      error_at(loc, "Map key type must be string or int.");
    }
    return map_type(key, resolve_type_expr(expr.type_args[1], loc));
  }
  if (expr.type_args.empty()) {
    Type t = resolve_type_name(expr.name);
    if (t.name.find("<unknown:") == 0 && loc.line > 0) {
      error_at(loc, "Unknown type '" + expr.name + "'.");
    }
    return t;
  }
  std::string mangled = mangle_name(expr.name, expr.type_args);
  auto it = type_registry_.find(mangled);
  if (it != type_registry_.end()) {
    return it->second;
  }
  auto gen_it = generic_structs_.find(expr.name);
  if (gen_it != generic_structs_.end()) {
    instantiate_generic_struct(gen_it->second, expr.type_args);
    auto inst_it = type_registry_.find(mangled);
    if (inst_it != type_registry_.end()) {
      return inst_it->second;
    }
  }
  if (loc.line > 0) {
    error_at(loc, "Unknown type '" + expr.to_string() + "'.");
  }
  Type err(TypeKind::Void);
  err.name = "<unknown:" + expr.to_string() + ">";
  return err;
}

std::string TypeChecker::mangle_name(const std::string &base, const std::vector<ast::TypeExpr> &args) const {
  std::string result = base;
  for (const auto &arg : args) {
    result += "__" + arg.to_string();
  }
  return result;
}

void TypeChecker::instantiate_generic_struct(const ast::StructDecl *decl, const std::vector<ast::TypeExpr> &args) {
  std::string mangled = mangle_name(decl->name, args);
  if (instantiated_.count(mangled)) return;
  instantiated_.insert(mangled);

  if (args.size() != decl->type_params.size()) {
    errors_.push_back(TypeError{.location = decl->location,
                                .message = "Wrong number of type arguments for '" + decl->name + "'."});
    return;
  }

  std::unordered_map<std::string, ast::TypeExpr> subst;
  for (size_t i = 0; i < decl->type_params.size(); ++i) {
    subst[decl->type_params[i]] = args[i];
  }

  Type struct_type(TypeKind::Struct);
  struct_type.name = mangled;
  for (const auto &field : decl->fields) {
    ast::TypeExpr resolved_field_type = substitute_type_params(field.type, subst);
    Type ft = resolve_type_expr(resolved_field_type);
    struct_type.fields.push_back(
        FieldInfo{field.name, ft.kind, ft.name, std::make_shared<Type>(ft)});
  }
  type_registry_.insert_or_assign(mangled, struct_type);
}

// Insert forward-declaration placeholders (name + kind only) for every type a
// module exports, so that recursive variant payloads and cross-type references
// resolve to the type rather than Void when the body is processed later. This
// mirrors the local Pass 0 for imported modules.
void TypeChecker::forward_declare_imported_types(const ParsedModule &mod) {
  for (const ast::StructDecl *sd : mod.public_structs) {
    if (!sd->type_params.empty()) continue;
    Type fwd(TypeKind::Struct);
    fwd.name = sd->name;
    type_registry_.insert_or_assign(sd->name, fwd);
  }
  for (const ast::StructDecl *sd : mod.private_structs) {
    if (!sd->type_params.empty()) continue;
    Type fwd(TypeKind::Struct);
    fwd.name = sd->name;
    type_registry_.insert_or_assign(sd->name, fwd);
  }
  for (const ast::EnumDecl *ed : mod.public_enums) {
    Type fwd(TypeKind::Enum);
    fwd.name = ed->name;
    type_registry_.insert_or_assign(ed->name, fwd);
  }
  for (const ast::EnumDecl *ed : mod.private_enums) {
    Type fwd(TypeKind::Enum);
    fwd.name = ed->name;
    type_registry_.insert_or_assign(ed->name, fwd);
  }
}

TypeCheckResult TypeChecker::check(const ast::Program &program) {
  errors_.clear();
  scopes_.clear();
  used_.clear();
  opened_.clear();
  type_registry_.clear();
  concept_registry_.clear();
  method_registry_.clear();
  kir_function_sigs_.clear();

  push_scope();

  // Pass 0: insert forward-declaration placeholders for every named type so
  // that types declared later in the source can still be referenced by
  // struct fields, enum variant payloads, or function signatures that appear
  // earlier in the file (e.g. enum Expr referencing FieldInit[]).
  for (const ast::DeclPtr &decl : program.declarations) {
    if (const auto *sd = dynamic_cast<const ast::StructDecl *>(decl.get())) {
      if (!sd->type_params.empty()) continue;
      Type fwd(TypeKind::Struct);
      fwd.name = sd->name;
      type_registry_.insert_or_assign(sd->name, fwd);
    } else if (const auto *ed = dynamic_cast<const ast::EnumDecl *>(decl.get())) {
      Type fwd(TypeKind::Enum);
      fwd.name = ed->name;
      type_registry_.insert_or_assign(ed->name, fwd);
    }
  }

  // Pass 0b: forward-declare every type exported by imported modules (and their
  // direct dependencies) before any body is resolved, mirroring local Pass 0.
  // The per-import registration below resolves variant/field types inline, so
  // without this a recursive payload (ast::Expr containing Expr) or a reference
  // to a type declared later in another module collapses to Void across module
  // boundaries. Singleton module loading makes the repeated load() calls cheap.
  // Covers both `import "x" {}` (ImportDecl) and `import { ... }`
  // (ImportBlockDecl) forms.
  if (module_loader_) {
    auto collect_imports = [](const ast::Decl *decl,
                              std::vector<const ast::ImportDecl *> &out) {
      if (const auto *id = dynamic_cast<const ast::ImportDecl *>(decl)) {
        out.push_back(id);
      } else if (const auto *ib =
                     dynamic_cast<const ast::ImportBlockDecl *>(decl)) {
        for (const auto &imp : ib->imports) {
          if (const auto *id = dynamic_cast<const ast::ImportDecl *>(imp.get())) {
            out.push_back(id);
          }
        }
      }
    };

    std::vector<const ast::ImportDecl *> top_imports;
    for (const ast::DeclPtr &decl : program.declarations) {
      collect_imports(decl.get(), top_imports);
    }

    for (const ast::ImportDecl *id : top_imports) {
      auto result = module_loader_->load(id->path);
      if (!result.module) continue;
      forward_declare_imported_types(*result.module);
      // One level of transitive deps: a module's pub type may reference a type
      // that module itself imports.
      if (result.module->program) {
        std::string mod_dir =
            std::filesystem::path(result.module->resolved_path)
                .parent_path()
                .string();
        std::vector<const ast::ImportDecl *> inner_imports;
        for (const auto &inner : result.module->program->declarations) {
          collect_imports(inner.get(), inner_imports);
        }
        for (const ast::ImportDecl *iid : inner_imports) {
          auto inner_result = module_loader_->load_from(iid->path, mod_dir);
          if (inner_result.module) {
            forward_declare_imported_types(*inner_result.module);
          }
        }
      }
    }
  }

  // Pre-register the built-in CastError enum: every program sees it without
  // an explicit declaration. Variants align with VM CastTo failure mode.
  {
    Type cast_err(TypeKind::Enum);
    cast_err.name = "CastError";
    cast_err.variants = {"Empty", "NotANumber", "Overflow"};
    cast_err.variant_param_types = {{}, {string_type()}, {string_type()}};
    type_registry_.insert_or_assign(cast_err.name, cast_err);
  }

  // First pass: register types, using declarations, and function signatures
  for (const ast::DeclPtr &decl : program.declarations) {
    if (const auto *using_decl = dynamic_cast<const ast::UsingDecl *>(decl.get())) {
      if (using_decl->namespace_name != "io" && using_decl->namespace_name != "fs" &&
          using_decl->namespace_name != "sys" && !imported_namespaces_.count(using_decl->namespace_name)) {
        error_at(using_decl->location, "Unknown module '" + using_decl->namespace_name + "'.");
      }
      used_.insert(using_decl->namespace_name);
      if (using_decl->is_namespace) {
        opened_.insert(using_decl->namespace_name);
      }
      if (!using_decl->selected_symbols.empty()) {
        const std::string &ns = using_decl->namespace_name;
        const bool known_module = imported_namespaces_.count(ns) > 0;
        for (const auto &sym : using_decl->selected_symbols) {
          // Non-imported namespaces (io, fs, sys, …) expose native members
          // rather than symbol-table entries.  Alias the bare name so that
          // `out.line(...)` resolves identically to `io::out.line(...)`.
          if (!known_module) {
            using_aliases_[sym] = {ns, sym};
            imported_bare_names_.insert(sym);
            continue;
          }
          std::string qualified = ns + "::" + sym;
          auto resolved = lookup_var(qualified);
          if (resolved) {
            declare_var(sym, *resolved, false);
            imported_bare_names_.insert(sym);
            continue;
          }
          // The symbol could not be pulled in. Diagnose why, distinguishing a
          // non-pub symbol from one that does not exist. A pub struct/enum
          // type resolves through the type registry rather than as a variable,
          // so its absence from lookup_var is expected and not an error.
          if (!known_module) {
            continue;  // already reported as "Unknown module" above
          }
          const auto pub_it = module_public_symbols_.find(ns);
          if (pub_it != module_public_symbols_.end() && pub_it->second.count(sym)) {
            continue;  // pub type; usable via type registry, nothing to bind
          }
          const auto priv_it = module_private_symbols_.find(ns);
          if (priv_it != module_private_symbols_.end() && priv_it->second.count(sym)) {
            error_at(using_decl->location,
                     "'" + sym + "' is not pub in module '" + ns + "'.");
          } else {
            error_at(using_decl->location,
                     "Module '" + ns + "' has no symbol '" + sym + "'.");
          }
        }
      }
      continue;
    }
    if (const auto *struct_decl = dynamic_cast<const ast::StructDecl *>(decl.get())) {
      if (struct_decl->name == "Self") {
        error_at(struct_decl->location, "'Self' is a reserved type name.");
        continue;
      }
      if (!struct_decl->type_params.empty()) {
        generic_structs_[struct_decl->name] = struct_decl;
      } else {
        // Insert a forward-declaration placeholder so self-referencing
        // fields can resolve the struct type by name.
        Type fwd(TypeKind::Struct);
        fwd.name = struct_decl->name;
        type_registry_.insert_or_assign(struct_decl->name, fwd);

        Type struct_type(TypeKind::Struct);
        struct_type.name = struct_decl->name;
        for (const auto &field : struct_decl->fields) {
          Type ft = resolve_type_expr(field.type);
          struct_type.fields.push_back(
              FieldInfo{field.name, ft.kind, ft.name, std::make_shared<Type>(ft)});
        }
        type_registry_.insert_or_assign(struct_decl->name, struct_type);
      }
      continue;
    }
    if (const auto *enum_decl = dynamic_cast<const ast::EnumDecl *>(decl.get())) {
      if (enum_decl->name == "Self") {
        error_at(enum_decl->location, "'Self' is a reserved type name.");
        continue;
      }
      // Insert a forward-declaration placeholder so self-referencing
      // variants (e.g. Unary(UnaryOp, Expr, int, int)) can resolve the
      // enum type by name.
      Type fwd(TypeKind::Enum);
      fwd.name = enum_decl->name;
      type_registry_.insert_or_assign(enum_decl->name, fwd);

      Type enum_type(TypeKind::Enum);
      enum_type.name = enum_decl->name;
      for (const auto &v : enum_decl->variants) {
        enum_type.variants.push_back(v.name);
        std::vector<Type> ptypes;
        for (const auto &pt : v.param_types) {
          ptypes.push_back(resolve_type_expr(pt));
        }
        enum_type.variant_param_types.push_back(std::move(ptypes));
      }
      type_registry_.insert_or_assign(enum_decl->name, enum_type);
      continue;
    }
    if (const auto *func = dynamic_cast<const ast::FunctionDecl *>(decl.get())) {
      if (!func->type_params.empty()) {
        generic_functions_[func->name] = func;
        continue;
      }
      Type return_type = resolve_type_expr(func->return_type);
      std::vector<Type> param_types;
      for (const auto &param : func->params) {
        param_types.push_back(resolve_type_expr(param.type));
      }
      Type func_type(TypeKind::Function);
      func_type.param_types = std::move(param_types);
      func_type.return_type = std::make_unique<Type>(return_type);
      declare_var(func->name, func_type, false);
      kir_function_sigs_[func->name] = kir_sig_from(func_type);
      if (!func->params.empty()) {
        const std::string &receiver = func->params[0].type.name;
        auto st = type_registry_.find(receiver);
        if (st != type_registry_.end() && st->second.kind == TypeKind::Struct) {
          method_registry_[receiver + "::" + func->name] =
              MethodInfo{.decl = func, .target_type = receiver};
        }
      }
    }
    if (const auto *concept_decl = dynamic_cast<const ast::ConceptDecl *>(decl.get())) {
      concept_registry_[concept_decl->name] = concept_decl;
      continue;
    }
    if (const auto *import_decl = dynamic_cast<const ast::ImportDecl *>(decl.get())) {
      // Check for duplicate symbols in selective import
      if (!import_decl->selected_symbols.empty()) {
        std::unordered_set<std::string> seen;
        for (const auto &s : import_decl->selected_symbols) {
          if (!seen.insert(s).second) {
            error_at(import_decl->location, "Duplicate symbol '" + s + "' in import list.");
          }
        }
      }
      if (module_loader_) {
        auto result = module_loader_->load(import_decl->path);
        if (result.module) {
          const auto &mod = *result.module;
          // Recursively process any imports declared inside the loaded module
          // so that types it depends on (e.g. Token in scanner.kl) are
          // registered before we try to resolve its function signatures.
          // Resolve transitive imports relative to the loaded module's
          // directory, not the main program's base directory.
          if (mod.program) {
            std::string mod_dir = std::filesystem::path(mod.resolved_path).parent_path().string();
            for (const auto &inner_decl : mod.program->declarations) {
              if (const auto *inner_import = dynamic_cast<const ast::ImportDecl *>(inner_decl.get())) {
                auto inner_result = module_loader_->load_from(inner_import->path, mod_dir);
                if (inner_result.module) {
                  const auto &inner_mod = *inner_result.module;
                  for (const auto *sd : inner_mod.public_structs) {
                    if (sd->type_params.empty()) {
                      Type st(TypeKind::Struct);
                      st.name = sd->name;
                      for (const auto &field : sd->fields) {
                        Type ft = resolve_type_expr(field.type);
                        st.fields.push_back(FieldInfo{field.name, ft.kind, ft.name, std::make_shared<Type>(ft)});
                      }
                      type_registry_.insert_or_assign(sd->name, st);
                    } else {
                      generic_structs_[sd->name] = sd;
                    }
                  }
                  for (const auto *ed : inner_mod.public_enums) {
                    Type et(TypeKind::Enum);
                    et.name = ed->name;
                    for (const auto &v : ed->variants) {
                      et.variants.push_back(v.name);
                      std::vector<Type> ptypes;
                      for (const auto &pt : v.param_types) ptypes.push_back(resolve_type_expr(pt));
                      et.variant_param_types.push_back(std::move(ptypes));
                    }
                    type_registry_.insert_or_assign(ed->name, et);
                  }
                }
              }
            }
          }
          std::string ns = import_decl->alias.empty() ? mod.namespace_name : import_decl->alias;
          imported_namespaces_.insert(ns);
          // Record exported / private symbol names so `using mod { sym };` can
          // diagnose a missing or non-pub symbol precisely.
          {
            auto &pub_set = module_public_symbols_[ns];
            for (const auto *fn : mod.public_functions) pub_set.insert(fn->name);
            for (const auto *sd : mod.public_structs) pub_set.insert(sd->name);
            for (const auto *ed : mod.public_enums) pub_set.insert(ed->name);
            auto &priv_set = module_private_symbols_[ns];
            for (const auto *fn : mod.private_functions) priv_set.insert(fn->name);
            for (const auto *sd : mod.private_structs) priv_set.insert(sd->name);
            for (const auto *ed : mod.private_enums) priv_set.insert(ed->name);
          }
          // Validate selected symbols exist as public exports
          if (!import_decl->selected_symbols.empty()) {
            std::unordered_set<std::string> pub_names;
            for (const auto *fn : mod.public_functions) pub_names.insert(fn->name);
            for (const auto *sd : mod.public_structs) pub_names.insert(sd->name);
            for (const auto *ed : mod.public_enums) pub_names.insert(ed->name);
            for (const auto &s : import_decl->selected_symbols) {
              if (!pub_names.count(s)) {
                error_at(import_decl->location, "'" + s + "' is not a public symbol in module '" + ns + "'.");
              }
            }
          }
          // Register structs and enums FIRST so function return types resolve.
          for (const auto *sd : mod.public_structs) {
            if (!import_decl->selected_symbols.empty()) {
              bool found = false;
              for (const auto &s : import_decl->selected_symbols) {
                if (s == sd->name) { found = true; break; }
              }
              if (!found) continue;
            }
            if (sd->type_params.empty()) {
              Type struct_type(TypeKind::Struct);
              struct_type.name = sd->name;
              for (const auto &field : sd->fields) {
                Type ft = resolve_type_expr(field.type);
                struct_type.fields.push_back(
                    FieldInfo{field.name, ft.kind, ft.name, std::make_shared<Type>(ft)});
              }
              type_registry_.insert_or_assign(sd->name, struct_type);
            } else {
              generic_structs_[sd->name] = sd;
            }
          }
          for (const auto *ed : mod.public_enums) {
            if (!import_decl->selected_symbols.empty()) {
              bool found = false;
              for (const auto &s : import_decl->selected_symbols) {
                if (s == ed->name) { found = true; break; }
              }
              if (!found) continue;
            }
            Type enum_type(TypeKind::Enum);
            enum_type.name = ed->name;
            for (const auto &v : ed->variants) {
              enum_type.variants.push_back(v.name);
              std::vector<Type> ptypes;
              for (const auto &pt : v.param_types) {
                ptypes.push_back(resolve_type_expr(pt));
              }
              enum_type.variant_param_types.push_back(std::move(ptypes));
            }
            type_registry_.insert_or_assign(ed->name, enum_type);
          }
          // Also register private structs/enums (needed by private helper fns).
          for (const auto *sd : mod.private_structs) {
            if (sd->type_params.empty()) {
              Type st(TypeKind::Struct);
              st.name = sd->name;
              for (const auto &field : sd->fields) {
                Type ft = resolve_type_expr(field.type);
                st.fields.push_back(FieldInfo{field.name, ft.kind, ft.name, std::make_shared<Type>(ft)});
              }
              type_registry_.insert_or_assign(sd->name, st);
            } else {
              generic_structs_[sd->name] = sd;
            }
          }
          for (const auto *ed : mod.private_enums) {
            Type et(TypeKind::Enum);
            et.name = ed->name;
            for (const auto &v : ed->variants) {
              et.variants.push_back(v.name);
              std::vector<Type> ptypes;
              for (const auto &pt : v.param_types) ptypes.push_back(resolve_type_expr(pt));
              et.variant_param_types.push_back(std::move(ptypes));
            }
            type_registry_.insert_or_assign(ed->name, et);
          }
          // Now register functions (return types can reference the structs above).
          for (const auto *fn : mod.public_functions) {
            if (!import_decl->selected_symbols.empty()) {
              bool found = false;
              for (const auto &s : import_decl->selected_symbols) {
                if (s == fn->name) { found = true; break; }
              }
              if (!found) continue;
            }
            Type return_type = resolve_type_expr(fn->return_type);
            std::vector<Type> param_types;
            for (const auto &param : fn->params) {
              param_types.push_back(resolve_type_expr(param.type));
            }
            Type func_type(TypeKind::Function);
            func_type.param_types = std::move(param_types);
            func_type.return_type = std::make_unique<Type>(return_type);
            declare_var(ns + "::" + fn->name, func_type, false);
            kir_function_sigs_[ns + "::" + fn->name] = kir_sig_from(func_type);
            if (!import_decl->selected_symbols.empty()) {
              Type ft2(TypeKind::Function);
              ft2.param_types = func_type.param_types;
              ft2.return_type = std::make_unique<Type>(*func_type.return_type);
              declare_var(fn->name, ft2, false);
              kir_function_sigs_[fn->name] = kir_sig_from(ft2);
              imported_bare_names_.insert(fn->name);
            }
          }
        }
      }
    }
    if (const auto *import_block = dynamic_cast<const ast::ImportBlockDecl *>(decl.get())) {
      for (const auto &imp : import_block->imports) {
        if (const auto *import_decl = dynamic_cast<const ast::ImportDecl *>(imp.get())) {
          if (module_loader_) {
            auto result = module_loader_->load(import_decl->path);
            if (result.module) {
              const auto &mod = *result.module;
              std::string ns = import_decl->alias.empty() ? mod.namespace_name : import_decl->alias;
              imported_namespaces_.insert(ns);
              // Record exported / private symbol names so `using mod { sym };`
              // diagnostics work for the block-import form too (without this,
              // `using mod { SomeStruct }` misreports the type as missing).
              {
                auto &pub_set = module_public_symbols_[ns];
                for (const auto *fn : mod.public_functions) pub_set.insert(fn->name);
                for (const auto *sd : mod.public_structs) pub_set.insert(sd->name);
                for (const auto *ed : mod.public_enums) pub_set.insert(ed->name);
                auto &priv_set = module_private_symbols_[ns];
                for (const auto *fn : mod.private_functions) priv_set.insert(fn->name);
                for (const auto *sd : mod.private_structs) priv_set.insert(sd->name);
                for (const auto *ed : mod.private_enums) priv_set.insert(ed->name);
              }
              for (const auto *sd : mod.public_structs) {
                if (sd->type_params.empty()) {
                  Type struct_type(TypeKind::Struct);
                  struct_type.name = sd->name;
                  for (const auto &field : sd->fields) {
                    Type ft = resolve_type_expr(field.type);
                    struct_type.fields.push_back(
                        FieldInfo{field.name, ft.kind, ft.name, std::make_shared<Type>(ft)});
                  }
                  type_registry_.insert_or_assign(sd->name, struct_type);
                } else {
                  generic_structs_[sd->name] = sd;
                }
              }
              for (const auto *ed : mod.public_enums) {
                Type enum_type(TypeKind::Enum);
                enum_type.name = ed->name;
                for (const auto &v : ed->variants) {
                  enum_type.variants.push_back(v.name);
                  std::vector<Type> ptypes;
                  for (const auto &pt : v.param_types) ptypes.push_back(resolve_type_expr(pt));
                  enum_type.variant_param_types.push_back(std::move(ptypes));
                }
                type_registry_.insert_or_assign(ed->name, enum_type);
              }
              for (const auto *fn : mod.public_functions) {
                Type return_type = resolve_type_expr(fn->return_type);
                std::vector<Type> param_types;
                for (const auto &param : fn->params) {
                  param_types.push_back(resolve_type_expr(param.type));
                }
                Type func_type(TypeKind::Function);
                func_type.param_types = std::move(param_types);
                func_type.return_type = std::make_unique<Type>(return_type);
                declare_var(ns + "::" + fn->name, func_type, false);
                kir_function_sigs_[ns + "::" + fn->name] = kir_sig_from(func_type);
              }
            }
          }
        }
      }
    }
  }

  // Second pass: check function bodies
  for (const ast::DeclPtr &decl : program.declarations) {
    if (dynamic_cast<const ast::UsingDecl *>(decl.get())) {
      continue;
    }
    if (dynamic_cast<const ast::StructDecl *>(decl.get())) {
      continue;
    }
    if (dynamic_cast<const ast::EnumDecl *>(decl.get())) {
      continue;
    }
    if (const auto *func = dynamic_cast<const ast::FunctionDecl *>(decl.get())) {
      if (func->type_params.empty()) {
        check_function(*func);
      }
    }
    if (const auto *top = dynamic_cast<const ast::TopLevelStmtDecl *>(decl.get())) {
      Type void_type(TypeKind::Void);
      check_stmt(*top->stmt, void_type);
    }
  }

  pop_scope();

  return TypeCheckResult{.errors = std::move(errors_)};
}

void TypeChecker::check_function(const ast::FunctionDecl &function) {
  Type return_type = resolve_type_expr(function.return_type, function.location);

  push_scope();

  for (const auto &param : function.params) {
    Type param_type = resolve_type_expr(param.type, function.location);
    declare_var(param.name, param_type, true);
  }

  if (function.body) {
    implicit_return_stmt_ = nullptr;
    if (return_type.kind != TypeKind::Void) {
      if (const auto *block = dynamic_cast<const ast::BlockStmt *>(function.body.get())) {
        if (!block->statements.empty()) {
          if (const auto *last_expr = dynamic_cast<const ast::ExprStmt *>(block->statements.back().get())) {
            implicit_return_stmt_ = last_expr;
          }
        }
      }
    }
    check_stmt(*function.body, return_type);
    implicit_return_stmt_ = nullptr;
  }

  pop_scope();
}

void TypeChecker::check_stmt(const ast::Stmt &stmt, const Type &expected_return) {
  if (const auto *block = dynamic_cast<const ast::BlockStmt *>(&stmt)) {
    push_scope();
    bool returned = false;
    for (const ast::StmtPtr &statement : block->statements) {
      if (returned) {
        warn_at(statement->location, "Unreachable code.");
        break;
      }
      check_stmt(*statement, expected_return);
      if (dynamic_cast<const ast::ReturnStmt *>(statement.get()) ||
          dynamic_cast<const ast::BreakStmt *>(statement.get()) ||
          dynamic_cast<const ast::ContinueStmt *>(statement.get())) {
        returned = true;
      }
    }
    pop_scope();
    return;
  }

  if (const auto *return_stmt = dynamic_cast<const ast::ReturnStmt *>(&stmt)) {
    if (return_stmt->value) {
      Type value_type = check_expr(*return_stmt->value);
      if (!value_type.is_compatible_with(expected_return)) {
        error_at(return_stmt->location,
                 "Cannot return " + type_to_string(value_type) + " from function returning " +
                     type_to_string(expected_return) + ".");
      }
    } else if (expected_return.kind != TypeKind::Void) {
      error_at(return_stmt->location, "Non-void function must return a value.");
    }
    return;
  }

  if (const auto *var_decl = dynamic_cast<const ast::VarDeclStmt *>(&stmt)) {
    Type var_type = resolve_type_expr(var_decl->type, var_decl->location);
    if (var_decl->init) {
      Type init_type = check_expr(*var_decl->init);
      if (var_decl->type.name == "auto") {
        var_type = init_type;
      } else if (!init_type.is_compatible_with(var_type)) {
        error_at(var_decl->location,
                 "Cannot assign " + type_to_string(init_type) + " to variable of type " +
                     type_to_string(var_type) + ".");
      }
    }
    bool is_mutable = var_decl->storage != "const";
    declare_var(var_decl->name, var_type, is_mutable, var_decl->location);
    return;
  }

  if (const auto *unpack = dynamic_cast<const ast::UnpackDeclStmt *>(&stmt)) {
    Type init_type = check_expr(*unpack->init);
    if (init_type.kind != TypeKind::Array) {
      error_at(unpack->location, "Destructuring requires an array value.");
      return;
    }
    Type elem_type = init_type.element_type ? *init_type.element_type : int_type();
    for (const auto &name : unpack->names) {
      declare_var(name, elem_type, true, unpack->location);
    }
    if (!unpack->rest_name.empty()) {
      declare_var(unpack->rest_name, init_type, true, unpack->location);
    }
    return;
  }

  if (const auto *expr_stmt = dynamic_cast<const ast::ExprStmt *>(&stmt)) {
    Type result_type = check_expr(*expr_stmt->expr);
    if (result_type.kind != TypeKind::Void) {
      bool suppress = false;
      if (expr_stmt == implicit_return_stmt_)
        suppress = true;
      // Don't suppress for imported function calls — the semicolon
      // suggests intentional discard, even when auto-returned.
      if (suppress) {
        if (const auto *call = dynamic_cast<const ast::CallExpr *>(expr_stmt->expr.get())) {
          if (const auto *ns = dynamic_cast<const ast::NamespaceAccessExpr *>(call->callee.get())) {
            if (imported_namespaces_.count(ns->namespace_name))
              suppress = false;
          } else if (const auto *var = dynamic_cast<const ast::IdentifierExpr *>(call->callee.get())) {
            if (imported_bare_names_.count(var->name))
              suppress = false;
          }
        }
      }
      if (dynamic_cast<const ast::AssignExpr *>(expr_stmt->expr.get()))
        suppress = true;
      if (dynamic_cast<const ast::FieldAssignExpr *>(expr_stmt->expr.get()))
        suppress = true;
      if (dynamic_cast<const ast::IndexAssignExpr *>(expr_stmt->expr.get()))
        suppress = true;
      if (const auto *call = dynamic_cast<const ast::CallExpr *>(expr_stmt->expr.get())) {
        if (const auto *ns = dynamic_cast<const ast::NamespaceAccessExpr *>(call->callee.get())) {
          if (ns->namespace_name == "io" && (ns->member_name == "out" || ns->member_name == "err"))
            suppress = true;
        }
        if (const auto *fa = dynamic_cast<const ast::FieldAccessExpr *>(call->callee.get())) {
          if (const auto *ns = dynamic_cast<const ast::NamespaceAccessExpr *>(fa->object.get())) {
            if (ns->namespace_name == "io" && (ns->member_name == "out" || ns->member_name == "err"))
              suppress = true;
          }
          const std::string &m = fa->field_name;
          if (m == "push" || m == "pop" || m == "remove" || m == "clear" ||
              m == "insert" || m == "reverse" || m == "line")
            suppress = true;
        }
      }
      if (!suppress)
        warn_at(expr_stmt->location, "Expression result is unused.");
    }
    return;
  }

  if (const auto *if_stmt = dynamic_cast<const ast::IfStmt *>(&stmt)) {
    Type cond_type = check_expr(*if_stmt->condition);
    if (cond_type.kind != TypeKind::Bool && cond_type.kind != TypeKind::Int) {
      error_at(if_stmt->location, "Condition must be Bool or Int.");
    }
    if (const auto *lit = dynamic_cast<const ast::BoolLiteralExpr *>(if_stmt->condition.get())) {
      warn_at(if_stmt->condition->location,
              std::string("Condition is always ") + (lit->value ? "true" : "false") + ".");
    }
    check_stmt(*if_stmt->then_branch, expected_return);
    if (if_stmt->else_branch) {
      check_stmt(*if_stmt->else_branch, expected_return);
    }
    return;
  }

  if (const auto *guard_stmt = dynamic_cast<const ast::GuardStmt *>(&stmt)) {
    Type cond_type = check_expr(*guard_stmt->condition);
    if (cond_type.kind != TypeKind::Bool && cond_type.kind != TypeKind::Int) {
      error_at(guard_stmt->location, "Guard condition must be Bool or Int.");
    }
    check_stmt(*guard_stmt->else_body, expected_return);
    return;
  }

  if (const auto *while_stmt = dynamic_cast<const ast::WhileStmt *>(&stmt)) {
    Type cond_type = check_expr(*while_stmt->condition);
    if (cond_type.kind != TypeKind::Bool && cond_type.kind != TypeKind::Int) {
      error_at(while_stmt->location, "Condition must be Bool or Int.");
    }
    if (const auto *lit = dynamic_cast<const ast::BoolLiteralExpr *>(while_stmt->condition.get())) {
      if (!lit->value) {
        warn_at(while_stmt->condition->location, "Condition is always false; loop body never executes.");
      }
    }
    ++loop_depth_;
    check_stmt(*while_stmt->body, expected_return);
    --loop_depth_;
    return;
  }

  if (const auto *for_stmt = dynamic_cast<const ast::ForStmt *>(&stmt)) {
    push_scope();
    if (for_stmt->init) {
      check_stmt(*for_stmt->init, expected_return);
    }
    if (for_stmt->condition) {
      Type cond_type = check_expr(*for_stmt->condition);
      if (cond_type.kind != TypeKind::Bool && cond_type.kind != TypeKind::Int) {
        error_at(for_stmt->location, "Condition must be Bool or Int.");
      }
    }
    if (for_stmt->step) {
      check_stmt(*for_stmt->step, expected_return);
    }
    ++loop_depth_;
    check_stmt(*for_stmt->body, expected_return);
    --loop_depth_;
    pop_scope();
    return;
  }

  if (dynamic_cast<const ast::BreakStmt *>(&stmt)) {
    if (loop_depth_ <= 0) {
      error_at(stmt.location, "break must be inside a loop.");
    }
    return;
  }

  if (dynamic_cast<const ast::ContinueStmt *>(&stmt)) {
    if (loop_depth_ <= 0) {
      error_at(stmt.location, "continue must be inside a loop.");
    }
    return;
  }

  if (const auto *try_catch = dynamic_cast<const ast::TryCatchStmt *>(&stmt)) {
    check_stmt(*try_catch->body, expected_return);
    for (const ast::CatchArm &arm : try_catch->catches) {
      push_scope();
      // Accept any resolvable type as the caught error type: builtins such as
      // `string`, user-declared enums, and the built-in CastError. An
      // unresolvable name is reported by resolve_type_expr itself.
      Type err_type = resolve_type_expr(arm.error_type, stmt.location);
      declare_var(arm.binding_name, err_type, false, stmt.location);
      check_stmt(*arm.body, expected_return);
      pop_scope();
    }
    return;
  }
}

Type TypeChecker::check_expr(const ast::Expr &expr) {
  if (dynamic_cast<const ast::IntLiteralExpr *>(&expr)) {
    return int_type();
  }

  if (dynamic_cast<const ast::CharLiteralExpr *>(&expr)) {
    return char_type();
  }

  if (dynamic_cast<const ast::FloatLiteralExpr *>(&expr)) {
    return float_type();
  }

  if (dynamic_cast<const ast::StringLiteralExpr *>(&expr)) {
    return string_type();
  }

  if (dynamic_cast<const ast::BoolLiteralExpr *>(&expr)) {
    return bool_type();
  }

  if (dynamic_cast<const ast::NullLiteralExpr *>(&expr)) {
    return null_type();
  }

  if (const auto *ns_access = dynamic_cast<const ast::NamespaceAccessExpr *>(&expr)) {
    if (ns_access->namespace_name == "io") {
      if (used_.count("io") == 0) {
        error_at(ns_access->location,
                 "Module 'io' is not imported. Add 'using io;' at the top of the file.");
        return void_type();
      }
      if (ns_access->member_name == "out" || ns_access->member_name == "err") {
        Type fn(TypeKind::Function);
        fn.name = "native_fn";
        fn.return_type = std::make_shared<Type>(void_type());
        return fn;
      }
      if (ns_access->member_name == "in") {
        Type fn(TypeKind::Function);
        fn.name = "native_fn";
        fn.return_type = std::make_shared<Type>(string_type());
        return fn;
      }
    }
    if (ns_access->namespace_name == "fs") {
      if (used_.count("fs") == 0) {
        error_at(ns_access->location,
                 "Module 'fs' is not imported. Add 'using fs;' at the top of the file.");
        return void_type();
      }
      if (ns_access->member_name == "__read") {
        Type fn(TypeKind::Function);
        fn.name = "native_fn";
        fn.return_type = std::make_shared<Type>(string_type());
        return fn;
      }
      if (ns_access->member_name == "__write") {
        Type fn(TypeKind::Function);
        fn.name = "native_fn";
        fn.return_type = std::make_shared<Type>(void_type());
        return fn;
      }
    }
    if (ns_access->namespace_name == "sys") {
      if (used_.count("sys") == 0) {
        error_at(ns_access->location,
                 "Module 'sys' is not imported. Add 'using sys;' at the top of the file.");
        return void_type();
      }
      if (ns_access->member_name == "args") {
        Type fn(TypeKind::Function);
        fn.name = "native_fn";
        fn.return_type = std::make_shared<Type>(array_type(string_type()));
        return fn;
      }
    }
    // Check for imported function (e.g. math::add)
    auto imported = lookup_var(ns_access->namespace_name + "::" + ns_access->member_name);
    if (imported.has_value()) return *imported;

    auto enum_type = lookup_type(ns_access->namespace_name);
    if (enum_type.has_value() && enum_type->kind == TypeKind::Enum) {
      bool found = false;
      for (const auto &v : enum_type->variants) {
        if (v == ns_access->member_name) { found = true; break; }
      }
      if (!found) {
        error_at(ns_access->location, "Enum '" + ns_access->namespace_name +
                                          "' has no variant '" + ns_access->member_name + "'.");
      }
      return *enum_type;
    }
    // Check for concept namespace (e.g. Printable::to_string)
    if (concept_registry_.count(ns_access->namespace_name)) {
      return void_type();
    }
    return void_type();
  }

  if (const auto *identifier = dynamic_cast<const ast::IdentifierExpr *>(&expr)) {
    if (identifier->name == "_") {
      return null_type();
    }
    if (opened_.count("io") != 0) {
      if (identifier->name == "out" || identifier->name == "err") {
        Type fn(TypeKind::Function);
        fn.name = "native_fn";
        fn.return_type = std::make_shared<Type>(void_type());
        return fn;
      }
      if (identifier->name == "in") {
        Type fn(TypeKind::Function);
        fn.name = "native_fn";
        fn.return_type = std::make_shared<Type>(string_type());
        return fn;
      }
    }
    auto var_type = lookup_var(identifier->name);
    if (!var_type.has_value()) {
      // Check if this bare name aliases a system-namespace member (e.g. `out`
      // from `using io { out }`). Return the same native_fn type that
      // NamespaceAccessExpr would produce for the qualified form.
      auto alias_it = using_aliases_.find(identifier->name);
      if (alias_it != using_aliases_.end()) {
        const auto &[ns, member] = alias_it->second;
        used_.insert(ns);
        if (ns == "io") {
          if (member == "out" || member == "err") {
            Type fn(TypeKind::Function);
            fn.name = "native_fn";
            fn.return_type = std::make_shared<Type>(void_type());
            return fn;
          }
          if (member == "in") {
            Type fn(TypeKind::Function);
            fn.name = "native_fn";
            fn.return_type = std::make_shared<Type>(string_type());
            return fn;
          }
        }
        if (ns == "sys") {
          if (member == "args") {
            Type fn(TypeKind::Function);
            fn.name = "native_fn";
            fn.return_type = std::make_shared<Type>(array_type(string_type()));
            return fn;
          }
        }
      }
      error_at(identifier->location, "Undeclared variable '" + identifier->name + "'.");
      return int_type();
    }
    return var_type.value();
  }

  if (const auto *unary = dynamic_cast<const ast::UnaryExpr *>(&expr)) {
    Type right_type = check_expr(*unary->right);
    switch (unary->op) {
    case ast::UnaryOp::Neg:
      if (!right_type.is_numeric()) {
        error_at(unary->location, "Cannot negate non-numeric type.");
      }
      return right_type;
    case ast::UnaryOp::Not:
      return bool_type();
    case ast::UnaryOp::BitNot:
      if (right_type.kind != TypeKind::Int) {
        error_at(unary->location, "Bitwise NOT requires an integer operand.");
      }
      return int_type();
    default:
      error_at(unary->location, "Unsupported unary operator.");
      return int_type();
    }
  }

  if (const auto *binary = dynamic_cast<const ast::BinaryExpr *>(&expr)) {
    Type left_type = check_expr(*binary->left);
    Type right_type = check_expr(*binary->right);

    switch (binary->op) {
    case ast::BinaryOp::Add:
      if (left_type.kind == TypeKind::String || right_type.kind == TypeKind::String) {
        return string_type();
      }
      [[fallthrough]];
    case ast::BinaryOp::Sub:
    case ast::BinaryOp::Mul:
    case ast::BinaryOp::Div:
    case ast::BinaryOp::Mod:
      if (!left_type.is_numeric() || !right_type.is_numeric()) {
        error_at(binary->location, "Arithmetic operands must be numeric.");
        return int_type();
      }
      return Type::promote(left_type, right_type);

    case ast::BinaryOp::Eq:
    case ast::BinaryOp::Neq:
    case ast::BinaryOp::Lt:
    case ast::BinaryOp::Gt:
    case ast::BinaryOp::Le:
    case ast::BinaryOp::Ge:
      if (!left_type.is_compatible_with(right_type)) {
        error_at(binary->location, "Cannot compare " + type_to_string(left_type) + " and " +
                                       type_to_string(right_type) + ".");
      }
      return bool_type();

    case ast::BinaryOp::And:
    case ast::BinaryOp::Or:
      return bool_type();

    case ast::BinaryOp::BitAnd:
    case ast::BinaryOp::BitOr:
    case ast::BinaryOp::BitXor:
    case ast::BinaryOp::Shl:
    case ast::BinaryOp::Shr:
      if (left_type.kind != TypeKind::Int || right_type.kind != TypeKind::Int) {
        error_at(binary->location, "Bitwise operators require integer operands.");
      }
      return int_type();
    }
  }

  if (const auto *assign = dynamic_cast<const ast::AssignExpr *>(&expr)) {
    auto var_type = lookup_var(assign->name);
    if (!var_type.has_value()) {
      error_at(assign->location, "Assignment to undeclared variable '" + assign->name + "'.");
      return int_type();
    }
    Type value_type = check_expr(*assign->value);
    if (!value_type.is_compatible_with(var_type.value())) {
      error_at(assign->location, "Cannot assign " + type_to_string(value_type) + " to " +
                                     type_to_string(var_type.value()) + ".");
    }
    return var_type.value();
  }

  if (const auto *call_expr = dynamic_cast<const ast::CallExpr *>(&expr)) {
    // Intercept built-in functions
    const auto *callee_id = dynamic_cast<const ast::IdentifierExpr *>(call_expr->callee.get());
    // Handle bare io:: members when 'using namespace io;' is in effect
    if (callee_id && opened_.count("io") != 0) {
      if (callee_id->name == "out") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          check_expr(*arg);
        }
        return void_type();
      }
      if (callee_id->name == "err") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          check_expr(*arg);
        }
        return void_type();
      }
      if (callee_id->name == "in") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          check_expr(*arg);
        }
        return string_type();
      }
    }

    const auto *ns_callee =
        dynamic_cast<const ast::NamespaceAccessExpr *>(call_expr->callee.get());
    // Type-qualified methods: int::bits(float) -> int
    if (ns_callee && ns_callee->namespace_name == "int" && ns_callee->member_name == "bits") {
      if (call_expr->args.size() != 1) {
        error_at(call_expr->location, "int::bits() expects exactly 1 argument.");
        return int_type();
      }
      Type arg_type = check_expr(*call_expr->args[0]);
      if (arg_type.kind != TypeKind::Float) {
        error_at(call_expr->location,
                 "int::bits() expects a float argument, got " + type_to_string(arg_type) + ".");
      }
      return int_type();
    }
    if (ns_callee && ns_callee->namespace_name == "io") {
      if (used_.count("io") == 0) {
        error_at(ns_callee->location,
                 "Module 'io' is not imported. Add 'using io;' at the top of the file.");
        return void_type();
      }
      if (ns_callee->member_name == "out" || ns_callee->member_name == "err") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          check_expr(*arg);
        }
        check_fmt_args(call_expr->args, call_expr->location);
        return void_type();
      }
      if (ns_callee->member_name == "in") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          check_expr(*arg);
        }
        return string_type();
      }
    }

    // Handle fs::__read(path) -> string, fs::__write(path, content) -> void.
    if (ns_callee && ns_callee->namespace_name == "fs") {
      if (used_.count("fs") == 0) {
        error_at(ns_callee->location,
                 "Module 'fs' is not imported. Add 'using fs;' at the top of the file.");
        return void_type();
      }
      if (ns_callee->member_name == "__read") {
        if (call_expr->args.size() != 1) {
          error_at(call_expr->location, "fs::__read expects exactly one argument (path).");
        } else if (check_expr(*call_expr->args[0]).kind != TypeKind::String) {
          error_at(call_expr->args[0]->location, "fs::__read expects a string path.");
        }
        return string_type();
      }
      if (ns_callee->member_name == "__write") {
        if (call_expr->args.size() != 2) {
          error_at(call_expr->location, "fs::__write expects exactly two arguments (path, content).");
        } else {
          if (check_expr(*call_expr->args[0]).kind != TypeKind::String) {
            error_at(call_expr->args[0]->location, "fs::__write expects a string path.");
          }
          if (check_expr(*call_expr->args[1]).kind != TypeKind::String) {
            error_at(call_expr->args[1]->location, "fs::__write expects string content.");
          }
        }
        return void_type();
      }
      error_at(ns_callee->location, "Unknown fs member '" + ns_callee->member_name + "'.");
      return void_type();
    }

    // Handle sys::args() -> string[].
    if (ns_callee && ns_callee->namespace_name == "sys") {
      if (used_.count("sys") == 0) {
        error_at(ns_callee->location,
                 "Module 'sys' is not imported. Add 'using sys;' at the top of the file.");
        return void_type();
      }
      if (ns_callee->member_name == "args") {
        if (!call_expr->args.empty()) {
          error_at(call_expr->location, "sys::args expects no arguments.");
        }
        return array_type(string_type());
      }
      error_at(ns_callee->location, "Unknown sys member '" + ns_callee->member_name + "'.");
      return void_type();
    }

    // Handle enum variant construction with payload: Shape::Circle(1.0)
    if (ns_callee) {
      auto enum_type = lookup_type(ns_callee->namespace_name);
      if (enum_type.has_value() && enum_type->kind == TypeKind::Enum) {
        int variant_idx = -1;
        for (int i = 0; i < static_cast<int>(enum_type->variants.size()); ++i) {
          if (enum_type->variants[static_cast<std::size_t>(i)] == ns_callee->member_name) {
            variant_idx = i;
            break;
          }
        }
        if (variant_idx < 0) {
          error_at(ns_callee->location, "Enum '" + ns_callee->namespace_name +
                                            "' has no variant '" + ns_callee->member_name + "'.");
          return *enum_type;
        }
        const auto &expected_params = enum_type->variant_param_types[static_cast<std::size_t>(variant_idx)];
        if (call_expr->args.size() != expected_params.size()) {
          error_at(call_expr->location, "Enum variant '" + ns_callee->member_name + "' expects " +
                   std::to_string(expected_params.size()) + " argument(s), got " +
                   std::to_string(call_expr->args.size()) + ".");
        }
        for (std::size_t i = 0; i < call_expr->args.size(); ++i) {
          check_expr(*call_expr->args[i]);
        }
        return *enum_type;
      }
    }

    if (ns_callee && concept_registry_.count(ns_callee->namespace_name)) {
      if (call_expr->args.empty()) {
        error_at(call_expr->location, "Concept method '" + ns_callee->namespace_name + "::" +
                                        ns_callee->member_name + "' expects at least one argument.");
        return int_type();
      }
      Type arg_ty = check_expr(*call_expr->args[0]);
      auto fn_ty = lookup_var(ns_callee->member_name);
      if (!fn_ty.has_value() || fn_ty->kind != TypeKind::Function) {
        error_at(call_expr->location, "No implementation of '" + ns_callee->namespace_name + "::" +
                                        ns_callee->member_name + "' for type '" +
                                        type_to_string(arg_ty) + "'.");
        return int_type();
      }
      if (call_expr->args.size() != fn_ty->param_types.size()) {
        error_at(call_expr->location, "Wrong number of arguments.");
        return fn_ty->return_type ? *fn_ty->return_type : int_type();
      }
      for (std::size_t i = 0; i < call_expr->args.size(); ++i) {
        Type at = check_expr(*call_expr->args[i]);
        if (!at.is_compatible_with(fn_ty->param_types[i])) {
          error_at(call_expr->args[i]->location, "Argument type mismatch.");
        }
      }
      return fn_ty->return_type ? *fn_ty->return_type : int_type();
    }

    // Handle io::out.line(...), io::err.line(...), io::in.secret(...)
    const auto *field_callee =
        dynamic_cast<const ast::FieldAccessExpr *>(call_expr->callee.get());
    if (field_callee) {
      const auto *id_obj =
          dynamic_cast<const ast::IdentifierExpr *>(field_callee->object.get());
      if (id_obj && opened_.count("io") != 0) {
        if ((id_obj->name == "out" || id_obj->name == "err") &&
            field_callee->field_name == "line") {
          for (const ast::ExprPtr &arg : call_expr->args) {
            check_expr(*arg);
          }
          check_fmt_args(call_expr->args, call_expr->location);
          return void_type();
        }
        if (id_obj->name == "in" && field_callee->field_name == "secret") {
          for (const ast::ExprPtr &arg : call_expr->args) {
            check_expr(*arg);
          }
          return string_type();
        }
      }
      const auto *ns_obj =
          dynamic_cast<const ast::NamespaceAccessExpr *>(field_callee->object.get());
      if (ns_obj && ns_obj->namespace_name == "io" && used_.count("io") != 0) {
        if ((ns_obj->member_name == "out" || ns_obj->member_name == "err") &&
            field_callee->field_name == "line") {
          for (const ast::ExprPtr &arg : call_expr->args) {
            check_expr(*arg);
          }
          check_fmt_args(call_expr->args, call_expr->location);
          return void_type();
        }
        if (ns_obj->member_name == "in" && field_callee->field_name == "secret") {
          for (const ast::ExprPtr &arg : call_expr->args) {
            check_expr(*arg);
          }
          return string_type();
        }
      }
      if (ns_obj && used_.count(ns_obj->namespace_name) == 0) {
        error_at(ns_obj->location,
                 "Module '" + ns_obj->namespace_name +
                     "' is not imported. Add 'using " + ns_obj->namespace_name +
                     ";' at the top of the file.");
        return void_type();
      }
    }

    // Handle array method calls: arr.len(), arr.push(x), etc.
    if (field_callee) {
      Type obj_type = check_expr(*field_callee->object);
      if (obj_type.kind == TypeKind::Map) {
        const std::string &method = field_callee->field_name;
        if (method == "len") {
          if (!call_expr->args.empty()) {
            error_at(call_expr->location, "len() takes no arguments.");
          }
          return int_type();
        }
        if (method == "has" || method == "remove") {
          if (call_expr->args.size() != 1) {
            error_at(call_expr->location, method + "() takes exactly 1 argument (key).");
          } else {
            Type k = check_expr(*call_expr->args[0]);
            if (obj_type.key_type && !k.is_compatible_with(*obj_type.key_type)) {
              error_at(call_expr->args[0]->location,
                       method + "() key must be " + type_to_string(*obj_type.key_type) +
                           ", got " + type_to_string(k) + ".");
            }
          }
          return method == "has" ? bool_type() : void_type();
        }
        if (method == "keys") {
          if (!call_expr->args.empty()) {
            error_at(call_expr->location, "keys() takes no arguments.");
          }
          return array_type(obj_type.key_type ? *obj_type.key_type : string_type());
        }
        error_at(call_expr->location, "Map has no method '" + method + "'.");
        return int_type();
      }
      if (obj_type.kind == TypeKind::Array) {
        const std::string &method = field_callee->field_name;
        if (method == "len") {
          if (!call_expr->args.empty()) {
            error_at(call_expr->location, "len() takes no arguments.");
          }
          return int_type();
        }
        if (method == "push") {
          if (call_expr->args.size() != 1) {
            error_at(call_expr->location, "push() takes exactly 1 argument.");
          } else if (obj_type.element_type) {
            Type arg_type = check_expr(*call_expr->args[0]);
            if (!arg_type.is_compatible_with(*obj_type.element_type)) {
              error_at(call_expr->args[0]->location,
                       "push() expects " + type_to_string(*obj_type.element_type) +
                           ", got " + type_to_string(arg_type) + ".");
            }
          }
          return void_type();
        }
        if (method == "resize") {
          if (call_expr->args.size() != 2) {
            error_at(call_expr->location, "resize() takes exactly 2 arguments (count, default).");
          } else {
            Type count_type = check_expr(*call_expr->args[0]);
            if (count_type.kind != TypeKind::Int) {
              error_at(call_expr->args[0]->location, "resize() count must be an Int.");
            }
            Type default_type = check_expr(*call_expr->args[1]);
            if (obj_type.element_type &&
                !default_type.is_compatible_with(*obj_type.element_type)) {
              error_at(call_expr->args[1]->location,
                       "resize() default expects " + type_to_string(*obj_type.element_type) +
                           ", got " + type_to_string(default_type) + ".");
            }
          }
          return void_type();
        }
        if (method == "pop") {
          if (!call_expr->args.empty()) {
            error_at(call_expr->location, "pop() takes no arguments.");
          }
          if (obj_type.element_type) return *obj_type.element_type;
          return int_type();
        }
        if (method == "remove") {
          if (call_expr->args.size() != 1) {
            error_at(call_expr->location, "remove() takes exactly 1 argument.");
          } else {
            Type arg_type = check_expr(*call_expr->args[0]);
            if (arg_type.kind != TypeKind::Int) {
              error_at(call_expr->args[0]->location, "remove() index must be Int.");
            }
          }
          if (obj_type.element_type) return *obj_type.element_type;
          return int_type();
        }
        if (method == "contains") {
          if (call_expr->args.size() != 1) {
            error_at(call_expr->location, "contains() takes exactly 1 argument.");
          } else {
            check_expr(*call_expr->args[0]);
          }
          return bool_type();
        }
        if (method == "clear") {
          if (!call_expr->args.empty()) {
            error_at(call_expr->location, "clear() takes no arguments.");
          }
          return void_type();
        }
        if (method == "insert") {
          if (call_expr->args.size() != 2) {
            error_at(call_expr->location, "insert() takes exactly 2 arguments (index, value).");
          } else {
            Type idx_type = check_expr(*call_expr->args[0]);
            if (idx_type.kind != TypeKind::Int) {
              error_at(call_expr->args[0]->location, "insert() index must be Int.");
            }
            check_expr(*call_expr->args[1]);
          }
          return void_type();
        }
        if (method == "index_of") {
          if (call_expr->args.size() != 1) {
            error_at(call_expr->location, "index_of() takes exactly 1 argument.");
          } else {
            check_expr(*call_expr->args[0]);
          }
          return int_type();
        }
        if (method == "slice") {
          if (call_expr->args.size() != 2) {
            error_at(call_expr->location, "slice() takes exactly 2 arguments (start, end).");
          } else {
            Type start_type = check_expr(*call_expr->args[0]);
            Type end_type = check_expr(*call_expr->args[1]);
            if (start_type.kind != TypeKind::Int) {
              error_at(call_expr->args[0]->location, "slice() start must be Int.");
            }
            if (end_type.kind != TypeKind::Int) {
              error_at(call_expr->args[1]->location, "slice() end must be Int.");
            }
          }
          return array_type(obj_type.element_type ? *obj_type.element_type : int_type());
        }
        if (method == "reverse") {
          if (!call_expr->args.empty()) {
            error_at(call_expr->location, "reverse() takes no arguments.");
          }
          return void_type();
        }
        error_at(call_expr->location, "Array has no method '" + method + "'.");
        return int_type();
      }
      if (obj_type.kind == TypeKind::String) {
        const std::string &method = field_callee->field_name;
        if (method == "len") {
          if (!call_expr->args.empty())
            error_at(call_expr->location, "len() takes no arguments.");
          return int_type();
        }
        if (method == "contains" || method == "starts_with" || method == "ends_with") {
          if (call_expr->args.size() != 1)
            error_at(call_expr->location, method + "() takes exactly 1 argument.");
          else {
            Type arg = check_expr(*call_expr->args[0]);
            if (arg.kind != TypeKind::String)
              error_at(call_expr->args[0]->location, method + "() argument must be string.");
          }
          return bool_type();
        }
        if (method == "index_of") {
          if (call_expr->args.size() != 1)
            error_at(call_expr->location, "index_of() takes exactly 1 argument.");
          else {
            Type arg = check_expr(*call_expr->args[0]);
            if (arg.kind != TypeKind::String)
              error_at(call_expr->args[0]->location, "index_of() argument must be string.");
          }
          return int_type();
        }
        if (method == "slice") {
          if (call_expr->args.size() != 2)
            error_at(call_expr->location, "slice() takes exactly 2 arguments.");
          else {
            Type s = check_expr(*call_expr->args[0]);
            Type e = check_expr(*call_expr->args[1]);
            if (s.kind != TypeKind::Int)
              error_at(call_expr->args[0]->location, "slice() start must be Int.");
            if (e.kind != TypeKind::Int)
              error_at(call_expr->args[1]->location, "slice() end must be Int.");
          }
          return string_type();
        }
        if (method == "replace") {
          if (call_expr->args.size() != 2)
            error_at(call_expr->location, "replace() takes exactly 2 arguments.");
          else {
            Type a = check_expr(*call_expr->args[0]);
            Type b = check_expr(*call_expr->args[1]);
            if (a.kind != TypeKind::String)
              error_at(call_expr->args[0]->location, "replace() arguments must be string.");
            if (b.kind != TypeKind::String)
              error_at(call_expr->args[1]->location, "replace() arguments must be string.");
          }
          return string_type();
        }
        if (method == "split") {
          if (call_expr->args.size() != 1)
            error_at(call_expr->location, "split() takes exactly 1 argument.");
          else {
            Type arg = check_expr(*call_expr->args[0]);
            if (arg.kind != TypeKind::String)
              error_at(call_expr->args[0]->location, "split() argument must be string.");
          }
          return array_type(string_type());
        }
        if (method == "trim" || method == "to_upper" || method == "to_lower") {
          if (!call_expr->args.empty())
            error_at(call_expr->location, method + "() takes no arguments.");
          return string_type();
        }
        error_at(call_expr->location, "String has no method '" + method + "'.");
        return int_type();
      }
      if (obj_type.kind == TypeKind::Struct) {
        const std::string method_key = obj_type.name + "::" + field_callee->field_name;
        auto method_it = method_registry_.find(method_key);
        if (method_it != method_registry_.end() && method_it->second.decl) {
          const ast::FunctionDecl *decl = method_it->second.decl;
          if (call_expr->args.size() + 1 != decl->params.size()) {
            error_at(call_expr->location,
                     "Expected " + std::to_string(decl->params.size() - 1) + " arguments, got " +
                         std::to_string(call_expr->args.size()) + ".");
          }
          Type receiver_type = check_expr(*field_callee->object);
          if (!receiver_type.is_compatible_with(resolve_type_expr(decl->params[0].type))) {
            error_at(field_callee->object->location, "UFCS receiver type mismatch.");
          }
          for (std::size_t i = 0; i < call_expr->args.size(); ++i) {
            Type arg_type = check_expr(*call_expr->args[i]);
            Type param_type = resolve_type_expr(decl->params[i + 1].type);
            if (!arg_type.is_compatible_with(param_type)) {
              error_at(call_expr->args[i]->location, "Argument type mismatch.");
            }
          }
          return resolve_type_expr(decl->return_type);
        }
      }
    }

    {
      const auto *callee_id = dynamic_cast<const ast::IdentifierExpr *>(call_expr->callee.get());
      if (callee_id) {
        auto gen_it = generic_functions_.find(callee_id->name);
        if (gen_it != generic_functions_.end()) {
          const ast::FunctionDecl *decl = gen_it->second;

          // Check arguments once: their types both drive inference (when no
          // explicit type arguments are written) and feed the compatibility
          // check below.
          std::vector<Type> arg_types;
          arg_types.reserve(call_expr->args.size());
          for (const auto &arg : call_expr->args) {
            arg_types.push_back(check_expr(*arg));
          }

          // Type arguments are explicit, or inferred by matching each argument
          // against a parameter written as a bare type parameter (e.g. `T x`).
          std::vector<ast::TypeExpr> type_args = call_expr->type_args;
          if (type_args.empty()) {
            std::unordered_map<std::string, ast::TypeExpr> inferred;
            for (size_t i = 0; i < decl->params.size() && i < arg_types.size(); ++i) {
              const ast::TypeExpr &pt = decl->params[i].type;
              if (!pt.type_args.empty() || inferred.count(pt.name)) continue;
              if (std::find(decl->type_params.begin(), decl->type_params.end(), pt.name) !=
                  decl->type_params.end()) {
                inferred[pt.name] = type_to_type_expr(arg_types[i]);
              }
            }
            for (const std::string &tp : decl->type_params) {
              auto it = inferred.find(tp);
              if (it != inferred.end()) type_args.push_back(it->second);
            }
          }

          if (type_args.size() != decl->type_params.size()) {
            error_at(call_expr->location, "Wrong number of type arguments for '" + callee_id->name + "'.");
            return int_type();
          }
          std::unordered_map<std::string, ast::TypeExpr> subst;
          for (size_t i = 0; i < decl->type_params.size(); ++i) {
            subst[decl->type_params[i]] = type_args[i];
          }
          auto substitute = [&](const ast::TypeExpr &te) -> ast::TypeExpr {
            if (te.type_args.empty()) {
              auto s_it = subst.find(te.name);
              if (s_it != subst.end()) return s_it->second;
            }
            return te;
          };
          Type ret = resolve_type_expr(substitute(decl->return_type));
          std::vector<Type> param_types;
          for (const auto &param : decl->params) {
            param_types.push_back(resolve_type_expr(substitute(param.type)));
          }
          if (arg_types.size() != param_types.size()) {
            error_at(call_expr->location,
                     "Expected " + std::to_string(param_types.size()) + " arguments, got " +
                         std::to_string(arg_types.size()) + ".");
            return ret;
          }
          for (size_t i = 0; i < arg_types.size(); ++i) {
            if (!arg_types[i].is_compatible_with(param_types[i])) {
              error_at(call_expr->args[i]->location,
                       "Expected " + type_to_string(param_types[i]) + ", got " +
                           type_to_string(arg_types[i]) + ".");
            }
          }
          return ret;
        }
      }
    }

    Type callee_type = check_expr(*call_expr->callee);
    if (callee_type.kind != TypeKind::Function) {
      if (const auto *ns = dynamic_cast<const ast::NamespaceAccessExpr *>(call_expr->callee.get());
          ns && used_.count(ns->namespace_name) == 0) {
        return void_type();
      }
      if (const auto *fa = dynamic_cast<const ast::FieldAccessExpr *>(call_expr->callee.get())) {
        if (const auto *ns = dynamic_cast<const ast::NamespaceAccessExpr *>(fa->object.get());
            ns && used_.count(ns->namespace_name) == 0) {
          return void_type();
        }
      }
      error_at(call_expr->location, "Cannot call non-function type.");
      return int_type();
    }
    if (callee_type.name == "native_fn") {
      for (std::size_t i = 0; i < call_expr->args.size(); ++i) {
        check_expr(*call_expr->args[i]);
      }
      return callee_type.return_type ? *callee_type.return_type : void_type();
    }
    if (call_expr->args.size() != callee_type.param_types.size()) {
      error_at(call_expr->location,
               "Expected " + std::to_string(callee_type.param_types.size()) + " arguments, got " +
                   std::to_string(call_expr->args.size()) + ".");
      return callee_type.return_type ? *callee_type.return_type : int_type();
    }
    for (std::size_t i = 0; i < call_expr->args.size(); ++i) {
      Type arg_type = check_expr(*call_expr->args[i]);
      if (!arg_type.is_compatible_with(callee_type.param_types[i])) {
        error_at(call_expr->args[i]->location,
                 "Expected " + type_to_string(callee_type.param_types[i]) + ", got " +
                     type_to_string(arg_type) + ".");
      }
    }
    return callee_type.return_type ? *callee_type.return_type : int_type();
  }

  if (const auto *binding = dynamic_cast<const ast::BindingPattern *>(&expr)) {
    (void)binding;
    return null_type();
  }

  if (const auto *arr_pat = dynamic_cast<const ast::ArrayPattern *>(&expr)) {
    (void)arr_pat;
    return null_type();
  }

  if (const auto *match_expr = dynamic_cast<const ast::MatchExpr *>(&expr)) {
    Type value_type = check_expr(*match_expr->value);
    Type element_type = (value_type.kind == TypeKind::Array && value_type.element_type)
                            ? *value_type.element_type
                            : int_type();
    Type result_type = null_type();
    for (const ast::MatchArm &arm : match_expr->arms) {
      const auto *binding = dynamic_cast<const ast::BindingPattern *>(arm.pattern.get());
      const auto *arr_pat = dynamic_cast<const ast::ArrayPattern *>(arm.pattern.get());
      const auto *enum_pat = dynamic_cast<const ast::EnumPattern *>(arm.pattern.get());
      if (binding) {
        push_scope();
        declare_var(binding->name, value_type, false, binding->location);
      } else if (arr_pat) {
        push_scope();
        for (const auto &elem : arr_pat->elements) {
          const auto *elem_binding = dynamic_cast<const ast::BindingPattern *>(elem.get());
          if (elem_binding) {
            declare_var(elem_binding->name, element_type, false, elem_binding->location);
          }
        }
      } else if (enum_pat) {
        push_scope();
        auto type_opt = lookup_type(enum_pat->enum_name);
        if (!type_opt.has_value() || type_opt->kind != TypeKind::Enum) {
          error_at(enum_pat->location, "'" + enum_pat->enum_name + "' is not an enum type.");
        } else {
          int variant_idx = -1;
          for (std::size_t i = 0; i < type_opt->variants.size(); ++i) {
            if (type_opt->variants[i] == enum_pat->variant_name) {
              variant_idx = static_cast<int>(i);
              break;
            }
          }
          if (variant_idx < 0) {
            error_at(enum_pat->location, "Enum '" + enum_pat->enum_name + "' has no variant '" + enum_pat->variant_name + "'.");
          } else {
            const auto &param_types = type_opt->variant_param_types[static_cast<std::size_t>(variant_idx)];
            if (enum_pat->fields.size() != param_types.size()) {
              error_at(enum_pat->location, "Variant '" + enum_pat->variant_name + "' expects " + std::to_string(param_types.size()) + " field(s), got " + std::to_string(enum_pat->fields.size()) + ".");
            } else {
              for (std::size_t i = 0; i < enum_pat->fields.size(); ++i) {
                const auto *field_binding = dynamic_cast<const ast::BindingPattern *>(enum_pat->fields[i].get());
                if (field_binding) {
                  declare_var(field_binding->name, param_types[i], false, field_binding->location);
                }
              }
            }
          }
        }
      }
      if (!binding && !arr_pat && !enum_pat) {
        Type pattern_type = check_expr(*arm.pattern);
        if (pattern_type.kind != TypeKind::Null && !pattern_type.is_compatible_with(value_type)) {
          error_at(arm.pattern->location, "Pattern type " + type_to_string(pattern_type) +
                                              " does not match value type " +
                                              type_to_string(value_type) + ".");
        }
      }
      if (arm.guard) {
        Type guard_type = check_expr(*arm.guard);
        if (guard_type.kind != TypeKind::Bool) {
          error_at(arm.guard->location, "Guard expression must be bool.");
        }
      }
      Type body_type = check_expr(*arm.body);
      if (binding || arr_pat || enum_pat) {
        pop_scope();
      }
      if (result_type.kind == TypeKind::Null) {
        result_type = body_type;
      }
    }

    if (value_type.nullable) {
      if (auto err = check_nullable_arms_exhaustive(match_expr->arms, value_type)) {
        error_at(match_expr->location, *err);
      }
    } else if (value_type.kind == TypeKind::Bool) {
      bool missing_true = false;
      bool missing_false = false;
      if (!arms_cover_bool(match_expr->arms, missing_true, missing_false)) {
        std::string missing;
        if (missing_true) {
          missing = "true";
        }
        if (missing_false) {
          if (!missing.empty()) {
            missing += ", ";
          }
          missing += "false";
        }
        error_at(match_expr->location, "Non-exhaustive match on bool. Missing case(s): " + missing + ".");
      }
    } else if (value_type.kind == TypeKind::Enum) {
      if (auto err = check_enum_arms_exhaustive(match_expr->arms, value_type)) {
        error_at(match_expr->location, *err);
      }
    }

    return result_type;
  }

  if (const auto *struct_lit = dynamic_cast<const ast::StructLiteralExpr *>(&expr)) {
    // A generic struct literal written without type arguments (e.g. `Box { 7 }`)
    // infers them from its field values: each field declared as a bare type
    // parameter pins that parameter to the field's value type. It then resolves
    // through the normal instantiation path.
    ast::TypeExpr lit_type = struct_lit->struct_type;
    if (lit_type.type_args.empty()) {
      auto gen_it = generic_structs_.find(lit_type.name);
      if (gen_it != generic_structs_.end()) {
        const ast::StructDecl *decl = gen_it->second;
        std::unordered_map<std::string, ast::TypeExpr> inferred;
        for (size_t i = 0; i < decl->fields.size() && i < struct_lit->fields.size(); ++i) {
          const ast::TypeExpr &ft = decl->fields[i].type;
          if (!ft.type_args.empty() || inferred.count(ft.name)) continue;
          if (std::find(decl->type_params.begin(), decl->type_params.end(), ft.name) !=
              decl->type_params.end()) {
            inferred[ft.name] = type_to_type_expr(check_expr(*struct_lit->fields[i].value));
          }
        }
        std::vector<ast::TypeExpr> targs;
        for (const std::string &tp : decl->type_params) {
          auto it = inferred.find(tp);
          if (it != inferred.end()) targs.push_back(it->second);
        }
        if (targs.size() == decl->type_params.size()) lit_type.type_args = std::move(targs);
      }
    }
    Type resolved = resolve_type_expr(lit_type, struct_lit->location);
    std::string type_name = struct_lit->struct_type.to_string();
    auto type_opt = lookup_type(resolved.name);
    if (!type_opt.has_value()) {
      error_at(struct_lit->location, "Unknown struct type '" + type_name + "'.");
      return int_type();
    }
    if (type_opt->kind != TypeKind::Struct) {
      error_at(struct_lit->location, "'" + type_name + "' is not a struct type.");
      return int_type();
    }
    const auto &fields_def = type_opt->fields;
    if (struct_lit->fields.size() != fields_def.size()) {
      error_at(struct_lit->location,
               "Struct '" + type_name + "' expects " + std::to_string(fields_def.size()) +
                   " field(s), got " + std::to_string(struct_lit->fields.size()) + ".");
    }
    for (size_t i = 0; i < struct_lit->fields.size() && i < fields_def.size(); ++i) {
      Type val_type = check_expr(*struct_lit->fields[i].value);
      Type expected(fields_def[i].type_kind);
      if (fields_def[i].type_kind == TypeKind::Struct && !fields_def[i].type_name.empty()) {
        auto reg_it = type_registry_.find(fields_def[i].type_name);
        if (reg_it != type_registry_.end()) expected = reg_it->second;
      } else if (fields_def[i].type) {
        expected = *fields_def[i].type;
      }
      if (!val_type.is_compatible_with(expected)) {
        error_at(struct_lit->fields[i].value->location,
                 "Field '" + fields_def[i].name + "' expects " + type_to_string(expected) +
                     ", got " + type_to_string(val_type) + ".");
      }
    }
    for (size_t i = fields_def.size(); i < struct_lit->fields.size(); ++i) {
      check_expr(*struct_lit->fields[i].value);
    }
    return *type_opt;
  }

  if (const auto *field_access = dynamic_cast<const ast::FieldAccessExpr *>(&expr)) {
    // Handle io::out.line, io::err.line, io::in.secret as callable methods
    const auto *ns_obj =
        dynamic_cast<const ast::NamespaceAccessExpr *>(field_access->object.get());
    if (ns_obj && ns_obj->namespace_name == "io" && used_.count("io") != 0) {
      if ((ns_obj->member_name == "out" || ns_obj->member_name == "err") &&
          field_access->field_name == "line") {
        Type fn(TypeKind::Function);
        fn.name = "native_fn";
        fn.return_type = std::make_shared<Type>(void_type());
        return fn;
      }
      if (ns_obj->member_name == "in" && field_access->field_name == "secret") {
        Type fn(TypeKind::Function);
        fn.name = "native_fn";
        fn.return_type = std::make_shared<Type>(string_type());
        return fn;
      }
    }

    // Handle `using namespace io;` bare out.line / err.line / in.secret.
    if (const auto *id_obj = dynamic_cast<const ast::IdentifierExpr *>(field_access->object.get())) {
      if (opened_.count("io") != 0) {
        if ((id_obj->name == "out" || id_obj->name == "err") &&
            field_access->field_name == "line") {
          Type fn(TypeKind::Function);
          fn.name = "native_fn";
          fn.return_type = std::make_shared<Type>(void_type());
          return fn;
        }
        if (id_obj->name == "in" && field_access->field_name == "secret") {
          Type fn(TypeKind::Function);
          fn.name = "native_fn";
          fn.return_type = std::make_shared<Type>(string_type());
          return fn;
        }
      }
    }

    // Handle aliased bare names from `using io { out }`: out.line(...)
    // behaves identically to io::out.line(...).
    if (const auto *id_obj = dynamic_cast<const ast::IdentifierExpr *>(field_access->object.get())) {
      auto alias_it = using_aliases_.find(id_obj->name);
      if (alias_it != using_aliases_.end()) {
        const auto &[ns, member] = alias_it->second;
        if (ns == "io" && (member == "out" || member == "err") &&
            field_access->field_name == "line") {
          Type fn(TypeKind::Function);
          fn.name = "native_fn";
          fn.return_type = std::make_shared<Type>(void_type());
          return fn;
        }
        if (ns == "io" && member == "in" && field_access->field_name == "secret") {
          Type fn(TypeKind::Function);
          fn.name = "native_fn";
          fn.return_type = std::make_shared<Type>(string_type());
          return fn;
        }
      }
    }

    Type obj_type = check_expr(*field_access->object);
    if (obj_type.kind == TypeKind::Map) {
      const std::string &method = field_access->field_name;
      if (method == "len" || method == "has" || method == "remove" || method == "keys") {
        return void_type();
      }
      error_at(field_access->location, "Map has no method '" + method + "'.");
      return int_type();
    }
    if (obj_type.kind == TypeKind::Array) {
      const std::string &method = field_access->field_name;
      if (method == "len" || method == "push" || method == "pop" ||
          method == "remove" || method == "contains" || method == "clear" ||
          method == "insert" || method == "index_of" || method == "slice" ||
          method == "reverse" || method == "resize") {
        return void_type();
      }
      error_at(field_access->location, "Array has no method '" + method + "'.");
      return int_type();
    }
    if (obj_type.kind == TypeKind::String) {
      const std::string &method = field_access->field_name;
      if (method == "len" || method == "contains" || method == "starts_with" ||
          method == "ends_with" || method == "index_of" || method == "slice" ||
          method == "replace" || method == "split" || method == "trim" ||
          method == "to_upper" || method == "to_lower") {
        return void_type();
      }
      error_at(field_access->location, "String has no method '" + method + "'.");
      return int_type();
    }
    if (obj_type.kind != TypeKind::Struct) {
      if (const auto *ns = dynamic_cast<const ast::NamespaceAccessExpr *>(field_access->object.get());
          ns && used_.count(ns->namespace_name) == 0) {
        return void_type();
      }
      error_at(field_access->location, "Cannot access field on non-struct type.");
      return int_type();
    }
    for (const auto &f : obj_type.fields) {
      if (f.name == field_access->field_name) {
        if (f.type_kind == TypeKind::Struct && !f.type_name.empty()) {
          auto reg_it = type_registry_.find(f.type_name);
          if (reg_it != type_registry_.end()) return reg_it->second;
        }
        // Prefer the full resolved type, which retains array element_type and
        // other detail that type_kind alone drops.
        if (f.type) return *f.type;
        return Type(f.type_kind);
      }
    }
    std::string method_key = obj_type.name + "::" + field_access->field_name;
    auto method_it = method_registry_.find(method_key);
    if (method_it != method_registry_.end()) {
      const auto *method_decl = method_it->second.decl;
      Type fn(TypeKind::Function);
      if (method_decl) {
        fn.return_type = std::make_unique<Type>(resolve_type_expr(method_decl->return_type));
        for (const auto &param : method_decl->params) {
          if (param.name == "self") continue;
          fn.param_types.push_back(resolve_type_expr(param.type));
        }
      }
      if (!fn.return_type) {
        fn.return_type = std::make_unique<Type>(void_type());
      }
      return fn;
    }
    error_at(field_access->location, "Struct '" + obj_type.name + "' has no field '" +
                                         field_access->field_name + "'.");
    return int_type();
  }

  if (const auto *field_assign = dynamic_cast<const ast::FieldAssignExpr *>(&expr)) {
    Type obj_type = check_expr(*field_assign->object);
    Type value_type = check_expr(*field_assign->value);
    if (obj_type.kind != TypeKind::Struct) {
      error_at(field_assign->location, "Cannot assign field on non-struct type.");
      return int_type();
    }
    for (const auto &f : obj_type.fields) {
      if (f.name == field_assign->field_name) {
        Type field_type(f.type_kind);
        if (f.type_kind == TypeKind::Struct && !f.type_name.empty()) {
          auto reg_it = type_registry_.find(f.type_name);
          if (reg_it != type_registry_.end()) field_type = reg_it->second;
        } else if (f.type) {
          field_type = *f.type;
        }
        if (!value_type.is_compatible_with(field_type)) {
          error_at(field_assign->location,
                   "Cannot assign " + type_to_string(value_type) + " to field '" +
                       f.name + "' of type " + type_to_string(field_type) + ".");
        }
        return field_type;
      }
    }
    error_at(field_assign->location, "Struct '" + obj_type.name + "' has no field '" +
                                         field_assign->field_name + "'.");
    return int_type();
  }

  if (const auto *array_lit = dynamic_cast<const ast::ArrayLiteralExpr *>(&expr)) {
    Type element_type = null_type();
    bool has_element_type = false;
    for (const ast::ExprPtr &element : array_lit->elements) {
      Type current = check_expr(*element);
      if (!has_element_type || element_type.kind == TypeKind::Null) {
        element_type = current;
        has_element_type = true;
        continue;
      }
      if (!current.is_compatible_with(element_type)) {
        error_at(element->location, "Array elements must have compatible types.");
        continue;
      }
      if (current.is_numeric() && element_type.is_numeric()) {
        element_type = Type::promote(element_type, current);
      }
    }
    return array_type(has_element_type ? element_type : null_type());
  }

  if (const auto *map_lit = dynamic_cast<const ast::MapLiteralExpr *>(&expr)) {
    Type key_type = null_type();
    Type val_type = null_type();
    bool has_kv = false;
    for (std::size_t i = 0; i < map_lit->keys.size(); ++i) {
      Type k = check_expr(*map_lit->keys[i]);
      Type v = check_expr(*map_lit->values[i]);
      if (k.kind != TypeKind::String && k.kind != TypeKind::Int) {
        error_at(map_lit->keys[i]->location, "Map key must be a string or int.");
      }
      if (!has_kv) {
        key_type = k;
        val_type = v;
        has_kv = true;
      } else {
        if (!k.is_compatible_with(key_type)) {
          error_at(map_lit->keys[i]->location, "Map keys must have compatible types.");
        }
        if (!v.is_compatible_with(val_type)) {
          error_at(map_lit->values[i]->location, "Map values must have compatible types.");
        }
      }
    }
    return map_type(has_kv ? key_type : null_type(), has_kv ? val_type : null_type());
  }

  if (const auto *index_expr = dynamic_cast<const ast::IndexExpr *>(&expr)) {
    Type object_type = check_expr(*index_expr->object);
    Type index_type = check_expr(*index_expr->index);
    // Map subscript: key type must match; value returned (null on missing key
    // at runtime, so the static type is the declared value type).
    if (object_type.kind == TypeKind::Map) {
      if (object_type.key_type && !index_type.is_compatible_with(*object_type.key_type)) {
        error_at(index_expr->index->location,
                 "Map key must be " + type_to_string(*object_type.key_type) + ", got " +
                     type_to_string(index_type) + ".");
      }
      return object_type.element_type ? *object_type.element_type : null_type();
    }
    if (index_type.kind != TypeKind::Int) {
      error_at(index_expr->index->location, "Array index must be an Int.");
    }
    if (object_type.kind == TypeKind::String) {
      return char_type();
    }
    if (object_type.kind != TypeKind::Array || !object_type.element_type) {
      error_at(index_expr->location, "Cannot index non-array type.");
      return int_type();
    }
    return *object_type.element_type;
  }

  if (const auto *cast = dynamic_cast<const ast::CastExpr *>(&expr)) {
    Type src = check_expr(*cast->value);
    const std::string &target = cast->target_type.name;
    TypeKind src_k = src.kind;
    auto src_is = [&](TypeKind k) { return src_k == k; };
    if (target == "int") {
      if (src_is(TypeKind::Int) || src_is(TypeKind::Float) || src_is(TypeKind::String) ||
          src_is(TypeKind::Char) || src_is(TypeKind::Enum)) {
        return int_type();
      }
    } else if (target == "float") {
      if (src_is(TypeKind::Int) || src_is(TypeKind::Float) || src_is(TypeKind::String)) {
        return float_type();
      }
    } else if (target == "string") {
      if (src_is(TypeKind::Int) || src_is(TypeKind::Float) || src_is(TypeKind::String) ||
          src_is(TypeKind::Char) || src_is(TypeKind::Bool) || src_is(TypeKind::Null)) {
        return string_type();
      }
    } else if (target == "char" || target == "byte") {
      // char/byte ↔ int: infallible; string → char: fallible (empty → CastError)
      if (src_is(TypeKind::Int) || src_is(TypeKind::Char) || src_is(TypeKind::String)) {
        return char_type();
      }
    } else {
      error_at(cast->location,
               "Cast target '" + target + "' is not supported. Use int, float, or string.");
      return null_type();
    }
    error_at(cast->location,
             "Cannot cast " + type_to_string(src) + " to " + target + ".");
    return null_type();
  }

  if (const auto *ternary = dynamic_cast<const ast::TernaryExpr *>(&expr)) {
    Type cond_type = check_expr(*ternary->condition);
    if (cond_type.kind != TypeKind::Bool && cond_type.kind != TypeKind::Int) {
      error_at(ternary->location,
               "Ternary condition must be bool or int, got " + type_to_string(cond_type) + ".");
    }
    Type then_type = check_expr(*ternary->then_expr);
    Type else_type = check_expr(*ternary->else_expr);
    if (!then_type.is_compatible_with(else_type)) {
      error_at(ternary->location,
               "Ternary branches have incompatible types: " + type_to_string(then_type) +
                   " vs " + type_to_string(else_type) + ".");
    }
    return then_type;
  }

  if (const auto *null_coalesce = dynamic_cast<const ast::NullCoalesceExpr *>(&expr)) {
    Type left_type = check_expr(*null_coalesce->left);
    const bool has_binding = !null_coalesce->err_binding.empty();
    if (has_binding) {
      const auto *cast_lhs = dynamic_cast<const ast::CastExpr *>(null_coalesce->left.get());
      if (cast_lhs == nullptr) {
        error_at(null_coalesce->location,
                 "?: let-binding requires a fallible cast on the left-hand side.");
      } else {
        push_scope();
        auto err_it = type_registry_.find("CastError");
        Type err_ty = (err_it != type_registry_.end()) ? err_it->second : null_type();
        declare_var(null_coalesce->err_binding, err_ty, false, null_coalesce->location);
        Type right_type = check_expr(*null_coalesce->right);
        pop_scope();
        if (!right_type.is_compatible_with(left_type)) {
          error_at(null_coalesce->right->location,
                   "?: fallback type " + type_to_string(right_type) +
                       " does not match left-hand type " + type_to_string(left_type) + ".");
        }
        return left_type;
      }
    }
    Type right_type = check_expr(*null_coalesce->right);
    if (!right_type.is_compatible_with(left_type)) {
      error_at(null_coalesce->right->location,
               "?: fallback type " + type_to_string(right_type) +
                   " does not match left-hand type " + type_to_string(left_type) + ".");
    }
    return left_type;
  }

  if (const auto *prop = dynamic_cast<const ast::PropagateExpr *>(&expr)) {
    Type inner = check_expr(*prop->value);
    return inner;
  }

  if (const auto *index_assign = dynamic_cast<const ast::IndexAssignExpr *>(&expr)) {
    Type object_type = check_expr(*index_assign->object);
    Type index_type = check_expr(*index_assign->index);
    Type value_type = check_expr(*index_assign->value);
    // Map insert/update: key must match key type, value must match value type.
    if (object_type.kind == TypeKind::Map) {
      if (object_type.key_type && !index_type.is_compatible_with(*object_type.key_type)) {
        error_at(index_assign->index->location,
                 "Map key must be " + type_to_string(*object_type.key_type) + ", got " +
                     type_to_string(index_type) + ".");
      }
      if (object_type.element_type && !value_type.is_compatible_with(*object_type.element_type)) {
        error_at(index_assign->value->location,
                 "Cannot assign " + type_to_string(value_type) + " to map value of type " +
                     type_to_string(*object_type.element_type) + ".");
      }
      return object_type.element_type ? *object_type.element_type : value_type;
    }
    if (index_type.kind != TypeKind::Int) {
      error_at(index_assign->index->location, "Array index must be an Int.");
    }
    if (object_type.kind != TypeKind::Array || !object_type.element_type) {
      error_at(index_assign->location, "Cannot assign indexed value on non-array type.");
      return value_type;
    }
    if (!value_type.is_compatible_with(*object_type.element_type)) {
      error_at(index_assign->value->location,
               "Cannot assign " + type_to_string(value_type) + " to array element of type " +
                   type_to_string(*object_type.element_type) + ".");
    }
    return *object_type.element_type;
  }

  return int_type();
}

void TypeChecker::push_scope() {
  scopes_.emplace_back();
}

void TypeChecker::pop_scope() {
  if (!scopes_.empty()) {
    for (const auto &[name, info] : scopes_.back()) {
      if (!info.used && name != "_" && info.location.line > 0) {
        warn_at(info.location, "Unused variable '" + name + "'.");
      }
    }
    scopes_.pop_back();
  }
}

void TypeChecker::declare_var(const std::string &name, const Type &type, bool is_mutable, ast::SourceLocation loc) {
  if (scopes_.empty()) {
    return;
  }
  auto &scope = scopes_.back();
  if (scope.find(name) != scope.end()) {
    error_at(loc, "Variable '" + name + "' already declared.");
    return;
  }
  scope.insert_or_assign(name, VarInfo{.type = type, .is_mutable = is_mutable, .used = false, .location = loc});
}

std::optional<Type> TypeChecker::lookup_var(const std::string &name) {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) {
      found->second.used = true;
      return found->second.type;
    }
  }
  return std::nullopt;
}

std::optional<Type> TypeChecker::lookup_type(const std::string &name) const {
  auto it = type_registry_.find(name);
  if (it != type_registry_.end()) {
    return it->second;
  }
  return std::nullopt;
}

void TypeChecker::error_at(ast::SourceLocation location, std::string message) {
  errors_.push_back(TypeError{.location = location, .message = std::move(message), .severity = DiagnosticSeverity::Error});
}

void TypeChecker::warn_at(ast::SourceLocation location, std::string message) {
  errors_.push_back(TypeError{.location = location, .message = std::move(message), .severity = DiagnosticSeverity::Warning});
}

void TypeChecker::populate_kir_types(KirModule *module) const {
  if (module == nullptr) {
    return;
  }

  module->function_signatures.assign(module->function_names.size(), KirFunctionSig{});
  for (std::size_t i = 0; i < module->function_names.size(); ++i) {
    const std::string &name = module->function_names[i];
    auto it = kir_function_sigs_.find(name);
    if (it != kir_function_sigs_.end()) {
      module->function_signatures[i] = it->second;
      continue;
    }
    for (const auto &[key, sig] : kir_function_sigs_) {
      const auto pos = key.rfind("::");
      if (pos != std::string::npos && key.substr(pos + 2) == name) {
        module->function_signatures[i] = sig;
        break;
      }
    }
  }

  for (KirStructMeta &meta : module->struct_metas) {
    meta.field_types.clear();
    const auto it = type_registry_.find(meta.name);
    if (it == type_registry_.end()) {
      continue;
    }
    for (const FieldInfo &field : it->second.fields) {
      if (field.type) {
        meta.field_types.push_back(kir_type_from(*field.type));
      } else {
        Type fallback(field.type_kind);
        fallback.name = field.type_name;
        meta.field_types.push_back(kir_type_from(fallback));
      }
    }
  }

  for (KirFunction &fn : module->functions) {
    auto it = kir_function_sigs_.find(fn.name);
    if (it == kir_function_sigs_.end()) {
      for (const auto &[key, sig] : kir_function_sigs_) {
        const auto pos = key.rfind("::");
        if (pos != std::string::npos && key.substr(pos + 2) == fn.name) {
          fn.param_types = sig.param_types;
          fn.return_type = sig.return_type;
          break;
        }
      }
      continue;
    }
    fn.param_types = it->second.param_types;
    fn.return_type = it->second.return_type;
  }
}

void TypeChecker::check_fmt_args(const std::vector<ast::ExprPtr> &args, ast::SourceLocation location) {
  if (args.empty()) return;
  const auto *str_lit = dynamic_cast<const ast::StringLiteralExpr *>(args[0].get());
  if (!str_lit) return;
  const std::string &fmt = str_lit->value;
  std::size_t placeholder_count = 0;
  for (std::size_t i = 0; i + 1 < fmt.size(); ++i) {
    if (fmt[i] == '{' && fmt[i + 1] == '}') {
      ++placeholder_count;
      ++i;
    }
  }
  if (placeholder_count == 0) return;
  std::size_t value_count = args.size() - 1;
  if (value_count < placeholder_count) {
    warn_at(location, "Format string has " + std::to_string(placeholder_count) +
                          " placeholder(s) but only " + std::to_string(value_count) +
                          " argument(s) provided.");
  } else if (value_count > placeholder_count) {
    warn_at(location, "Format string has " + std::to_string(placeholder_count) +
                          " placeholder(s) but " + std::to_string(value_count) +
                          " argument(s) provided.");
  }
}

} // namespace kinglet
