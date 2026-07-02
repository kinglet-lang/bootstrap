// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "frontend/ast/ast.h"
#include "frontend/module/project_config.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kinglet {

struct ParsedModule {
  std::unique_ptr<ast::Program> program;
  std::string namespace_name;
  std::string resolved_path;
  std::vector<const ast::FunctionDecl *> public_functions;
  std::vector<const ast::FunctionDecl *> private_functions;
  std::vector<const ast::StructDecl *> public_structs;
  std::vector<const ast::StructDecl *> private_structs;
  std::vector<const ast::EnumDecl *> public_enums;
  std::vector<const ast::EnumDecl *> private_enums;
};

class ModuleLoader {
public:
  explicit ModuleLoader(std::string base_dir);
  const std::string &base_dir() const { return base_dir_; }
  const std::optional<ProjectConfig> &project_config() const { return project_config_; }

  struct LoadResult {
    const ParsedModule *module = nullptr;
    std::string error;
  };

  void register_source_file(const std::string &path);
  void discover_project_root(const std::string &source_file_dir);
  LoadResult load(const std::string &path);
  LoadResult load_from(const std::string &path, const std::string &importing_file_dir);

  // Unified logical resolution against the project module index (built from all
  // targets' sources). `import a.b;` resolves to the single file that declares
  // `export module a.b;`. A bare `import x;` that is not itself a module name
  // resolves to every module whose name starts with `x.` (group import).
  // Returns the loaded module(s), or sets `error` when nothing matched.
  struct LogicalResolveResult {
    std::vector<const ParsedModule *> modules;
    std::string error;
  };
  LogicalResolveResult resolve_logical(const std::string &module_id);

private:
  std::string resolve_path(const std::string &relative_path) const;
  std::string resolve_path_from(const std::string &relative_path, const std::string &base) const;
  std::string derive_namespace(const std::string &path) const;
  // Shared body of load() / load_from(): given an already-resolved canonical
  // path, run the self/circular/cache guards, read+lex+parse, classify decls,
  // and cache the result. `path` is the original import specifier (for errors).
  LoadResult load_resolved(const std::string &resolved, const std::string &path);

  std::string base_dir_;
  std::optional<ProjectConfig> project_config_;
  std::unordered_map<std::string, ParsedModule> cache_;
  std::unordered_set<std::string> loading_;
  std::unordered_set<std::string> source_files_;
  // module name (`export module X;`) → resolved source file. Built from every
  // target's sources when the project root is discovered.
  std::unordered_map<std::string, std::string> module_index_;
  bool module_index_built_ = false;

  // Scan all targets' sources for `export module <name>;` and populate
  // module_index_. Idempotent.
  void build_module_index();
};

} // namespace kinglet
