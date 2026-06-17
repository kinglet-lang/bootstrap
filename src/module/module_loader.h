#pragma once

#include "ast/ast.h"
#include "module/project_config.h"

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
