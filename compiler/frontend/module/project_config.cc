// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "frontend/module/project_config.h"

#include "frontend/module/nest_parser.h"

#include <filesystem>

namespace kinglet {

std::optional<std::string> resolve_build_entry_path(const ProjectConfig &config) {
  if (!config.build_default.empty()) {
    const auto it = config.modules.find(config.build_default);
    if (it == config.modules.end()) {
      return std::nullopt;
    }
    return (std::filesystem::path(config.root_dir) / it->second).string();
  }
  if (!config.build_root.empty()) {
    return (std::filesystem::path(config.root_dir) / config.build_root).string();
  }
  return std::nullopt;
}

std::optional<ProjectConfig> find_nest_config(const std::string &start_dir) {
  std::filesystem::path dir(start_dir);
  while (true) {
    const std::filesystem::path candidate = dir / "kinglet.nest";
    if (std::filesystem::exists(candidate)) {
      return load_nest_config_file(candidate);
    }
    const auto parent = dir.parent_path();
    if (parent == dir) {
      break;
    }
    dir = parent;
  }
  return std::nullopt;
}

std::optional<ProjectConfig> find_project_config(const std::string &start_dir) {
  return find_nest_config(start_dir);
}

} // namespace kinglet
