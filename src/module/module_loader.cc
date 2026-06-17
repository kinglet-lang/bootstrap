// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "module/module_loader.h"

#include "lexer/scanner.h"
#include "parser/parser.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace kinglet {

ModuleLoader::ModuleLoader(std::string base_dir)
    : base_dir_(std::move(base_dir)) {}

void ModuleLoader::discover_project_root(const std::string &source_file_dir) {
  project_config_ = find_project_config(source_file_dir);
}

std::string ModuleLoader::resolve_path(const std::string &relative_path) const {
  std::filesystem::path base(base_dir_);
  std::filesystem::path resolved = base / relative_path;
  return std::filesystem::canonical(resolved).string();
}

std::string ModuleLoader::resolve_path_from(const std::string &relative_path, const std::string &base) const {
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
  std::filesystem::path target = p.is_absolute()
                                     ? p
                                     : std::filesystem::path(base_dir_) / path;
  std::error_code ec;
  std::filesystem::path canonical = std::filesystem::canonical(target, ec);
  if (ec) {
    // File may not exist on disk yet (e.g. unsaved LSP buffer); fall back to
    // a lexical normalization so registration never throws.
    canonical = std::filesystem::weakly_canonical(target, ec);
    if (ec) canonical = target.lexically_normal();
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

ModuleLoader::LoadResult ModuleLoader::load_from(const std::string &path, const std::string &importing_file_dir) {
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

ModuleLoader::LoadResult ModuleLoader::load_by_logical_name(const std::string &module_id) {
  if (!project_config_) {
    return {nullptr, "Cannot resolve import '" + module_id + "': no kinglet.nest found"};
  }
  const auto it = project_config_->modules.find(module_id);
  if (it == project_config_->modules.end()) {
    return {nullptr, "Unknown module '" + module_id + "' in project manifest"};
  }
  auto result = load_from(it->second, project_config_->root_dir);
  if (!result.module) {
    return result;
  }
  if (result.module->namespace_name != module_id) {
    return {nullptr, "export module '" + result.module->namespace_name +
                          "' does not match manifest key '" + module_id + "'"};
  }
  return result;
}

} // namespace kinglet
