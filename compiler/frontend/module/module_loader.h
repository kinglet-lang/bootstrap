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
  LoadResult load_by_logical_name(const std::string &module_id);

  // Directory-as-module: when `import <module_id>;` is not a manifest entry,
  // treat <module_id> (dots -> slashes) as a directory under the project root
  // and load every `<dir>/*.kl` as a submodule `<module_id>.<stem>`. This is
  // the auto-import path that removes the need for a `_dir.kl` manifest.
  struct DirectoryImportResult {
    bool is_directory = false;             // did <root>/<module_id>/ exist?
    std::vector<const ParsedModule *> modules;  // successfully loaded submodules
    std::string error;                     // first error encountered, if any
  };
  DirectoryImportResult load_directory_import(const std::string &module_id);

  // Unified logical resolution: manifest entry first, then directory-as-module.
  // Returns the loaded module(s) — one for a manifest hit, N for a directory —
  // and sets `error` when nothing was found or a submodule failed to load.
  struct LogicalResolveResult {
    std::vector<const ParsedModule *> modules;
    std::string error;
  };
  LogicalResolveResult resolve_logical(const std::string &module_id);

private:
  std::string resolve_path(const std::string &relative_path) const;
  std::string resolve_path_from(const std::string &relative_path, const std::string &base) const;
  std::string derive_namespace(const std::string &path) const;

  std::string base_dir_;
  std::optional<ProjectConfig> project_config_;
  std::unordered_map<std::string, ParsedModule> cache_;
  std::unordered_set<std::string> loading_;
  std::unordered_set<std::string> source_files_;
};

} // namespace kinglet
