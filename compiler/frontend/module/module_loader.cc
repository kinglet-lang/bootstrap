// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "frontend/module/module_loader.h"

#include "frontend/lexer/scanner.h"
#include "frontend/parser/parser.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace kinglet {

ModuleLoader::ModuleLoader(std::string base_dir) : base_dir_(std::move(base_dir)) {}

void ModuleLoader::discover_project_root(const std::string &source_file_dir) {
  project_config_ = find_project_config(source_file_dir);
}

std::string ModuleLoader::resolve_path(const std::string &relative_path) const {
  std::filesystem::path base(base_dir_);
  std::filesystem::path resolved = base / relative_path;
  return std::filesystem::canonical(resolved).string();
}

std::string ModuleLoader::resolve_path_from(const std::string &relative_path,
                                            const std::string &base) const {
  std::filesystem::path b(base);
  std::filesystem::path resolved = b / relative_path;
  return std::filesystem::canonical(resolved).string();
}

std::string ModuleLoader::derive_namespace(const std::string &path) const {
  std::filesystem::path p(path);
  return p.stem().string();
}

namespace {

std::string export_module_name(const ast::Program &program) {
  for (const auto &decl : program.declarations) {
    if (const auto *exported = dynamic_cast<const ast::ExportModuleDecl *>(decl.get())) {
      return exported->name;
    }
  }
  return "";
}

void assign_namespace(ParsedModule &mod, const std::string &fallback_path) {
  const std::string stem = std::filesystem::path(fallback_path).stem().string();
  if (!mod.program) {
    mod.namespace_name = stem;
    return;
  }
  const std::string exported = export_module_name(*mod.program);
  mod.namespace_name = exported.empty() ? stem : exported;
}

} // namespace

void ModuleLoader::register_source_file(const std::string &path) {
  std::filesystem::path p(path);
  std::filesystem::path target = p.is_absolute() ? p : std::filesystem::path(base_dir_) / path;
  std::error_code ec;
  std::filesystem::path canonical = std::filesystem::canonical(target, ec);
  if (ec) {
    // File may not exist on disk yet (e.g. unsaved LSP buffer); fall back to
    // a lexical normalization so registration never throws.
    canonical = std::filesystem::weakly_canonical(target, ec);
    if (ec)
      canonical = target.lexically_normal();
  }
  source_files_.insert(canonical.string());
}

ModuleLoader::LoadResult ModuleLoader::load(const std::string &path) {
  std::string resolved;
  try {
    if (path.size() >= 2 && path[0] == '/' && path[1] == '/') {
      if (!project_config_) {
        return {nullptr, "Cannot resolve '//' path: no project manifest found"};
      }
      std::filesystem::path root(project_config_->root_dir);
      std::string relative = path.substr(2);
      resolved = std::filesystem::canonical(root / relative).string();
    } else {
      resolved = resolve_path(path);
    }
  } catch (const std::filesystem::filesystem_error &) {
    return {nullptr, "Cannot resolve import path: " + path};
  }

  return load_resolved(resolved, path);
}

ModuleLoader::LoadResult ModuleLoader::load_from(const std::string &path,
                                                 const std::string &importing_file_dir) {
  std::string resolved;
  try {
    if (path.size() >= 2 && path[0] == '/' && path[1] == '/') {
      // Project-root-relative path: //parser/ast.kl
      if (!project_config_) {
        return {nullptr, "Cannot resolve '//' path: no project manifest found"};
      }
      std::string relative = path.substr(2);
      std::filesystem::path root(project_config_->root_dir);
      resolved = std::filesystem::canonical(root / relative).string();
    } else {
      resolved = resolve_path_from(path, importing_file_dir);
    }
  } catch (const std::filesystem::filesystem_error &) {
    return {nullptr, "Cannot resolve import path: " + path};
  }

  return load_resolved(resolved, path);
}

ModuleLoader::LoadResult ModuleLoader::load_resolved(const std::string &resolved,
                                                     const std::string &path) {
  if (source_files_.count(resolved)) {
    return {nullptr, "File cannot import itself: " + path};
  }

  if (loading_.count(resolved)) {
    return {nullptr, "Circular import detected: " + path};
  }

  auto it = cache_.find(resolved);
  if (it != cache_.end()) {
    return {&it->second, ""};
  }

  std::ifstream file(resolved, std::ios::in | std::ios::binary);
  if (!file) {
    return {nullptr, "Cannot open file: " + path};
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  std::string source = buffer.str();

  loading_.insert(resolved);

  Scanner scanner(std::move(source));
  auto tokens = scanner.scan_tokens();
  for (const auto &token : tokens) {
    if (token.type == TokenType::ERROR) {
      loading_.erase(resolved);
      return {nullptr, "Lexer error in " + path + ": " + std::string(token.lexeme)};
    }
  }

  Parser parser(tokens);
  auto parse_result = parser.parse();
  if (!parse_result.errors.empty()) {
    loading_.erase(resolved);
    return {nullptr, "Parse error in " + path + ": " + parse_result.errors[0].message};
  }

  ParsedModule mod;
  mod.resolved_path = resolved;
  mod.program = std::move(parse_result.program);
  assign_namespace(mod, path);

  for (const auto &decl : mod.program->declarations) {
    if (const auto *func = dynamic_cast<const ast::FunctionDecl *>(decl.get())) {
      if (func->is_public) {
        mod.public_functions.push_back(func);
      } else {
        mod.private_functions.push_back(func);
      }
    } else if (const auto *sd = dynamic_cast<const ast::StructDecl *>(decl.get())) {
      if (sd->is_public) {
        mod.public_structs.push_back(sd);
      } else {
        mod.private_structs.push_back(sd);
      }
    } else if (const auto *ed = dynamic_cast<const ast::EnumDecl *>(decl.get())) {
      if (ed->is_public) {
        mod.public_enums.push_back(ed);
      } else {
        mod.private_enums.push_back(ed);
      }
    }
  }

  loading_.erase(resolved);
  auto [inserted, _] = cache_.emplace(resolved, std::move(mod));
  return {&inserted->second, ""};
}

std::vector<std::pair<std::string, std::string>> ModuleLoader::module_index_entries() {
  build_module_index();
  std::vector<std::pair<std::string, std::string>> out(module_index_.begin(), module_index_.end());
  std::sort(out.begin(), out.end());
  return out;
}

namespace {

// Read a file and extract its `export module <name>;` declaration name, if any.
// Cheap line scan — avoids a full parse just to index module names.
std::string read_export_module_name(const std::string &path) {
  std::ifstream file(path, std::ios::in);
  if (!file) {
    return {};
  }
  std::string line;
  while (std::getline(file, line)) {
    // Trim leading whitespace.
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
      ++i;
    }
    const std::string_view rest(line.data() + i, line.size() - i);
    static constexpr std::string_view kPrefix = "export module ";
    if (rest.substr(0, kPrefix.size()) == kPrefix) {
      std::string name(rest.substr(kPrefix.size()));
      // Strip trailing `;` and whitespace.
      const auto semi = name.find(';');
      if (semi != std::string::npos) {
        name = name.substr(0, semi);
      }
      // Trim.
      const auto start = name.find_first_not_of(" \t\r\n");
      const auto end = name.find_last_not_of(" \t\r\n");
      if (start == std::string::npos) {
        return {};
      }
      return name.substr(start, end - start + 1);
    }
  }
  return {};
}

} // namespace

void ModuleLoader::build_module_index() {
  if (module_index_built_ || !project_config_) {
    module_index_built_ = true;
    return;
  }
  module_index_built_ = true;
  for (const TargetConfig &target : project_config_->targets) {
    for (const std::string &src : resolve_target_sources(*project_config_, target)) {
      const std::string name = read_export_module_name(src);
      if (!name.empty()) {
        // First declaration wins; duplicates across targets map to the same file
        // in practice (shared sources), so a stable pick is fine.
        module_index_.emplace(name, src);
      }
    }
  }
}

ModuleLoader::LogicalResolveResult ModuleLoader::resolve_logical(const std::string &module_id) {
  LogicalResolveResult out;
  if (!project_config_) {
    out.error = "Cannot resolve import '" + module_id + "': no kinglet.nest found";
    return out;
  }
  build_module_index();

  // Exact module match: `import a.b;` where a.b is a declared module.
  const auto exact = module_index_.find(module_id);
  if (exact != module_index_.end()) {
    LoadResult result = load_from(exact->second, project_config_->root_dir);
    if (!result.module) {
      out.error = result.error;
      return out;
    }
    out.modules.push_back(result.module);
    return out;
  }

  // Group import: `import x;` loads every module named `x.*`.
  const std::string prefix = module_id + ".";
  std::vector<std::pair<std::string, std::string>> group; // (name, file), sorted by name
  for (const auto &[name, file] : module_index_) {
    if (name.rfind(prefix, 0) == 0) {
      group.emplace_back(name, file);
    }
  }
  std::sort(group.begin(), group.end());
  for (const auto &[name, file] : group) {
    LoadResult result = load_from(file, project_config_->root_dir);
    if (!result.module) {
      if (out.error.empty()) {
        out.error = result.error;
      }
      continue;
    }
    out.modules.push_back(result.module);
  }

  if (out.modules.empty() && out.error.empty()) {
    out.error =
        "Unknown module '" + module_id + "': no matching 'export module' in any target's sources";
  }
  return out;
}

} // namespace kinglet
