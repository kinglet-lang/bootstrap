// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace kinglet {

struct ProjectFmtSection {
  int indent = 0; // 0 = unset
  int max_width = 0;
  std::string newline; // empty = unset; "lf" or "crlf"
  bool trailing_comma_set = false;
  bool trailing_comma = false;
  std::vector<std::string> extensions;
  std::vector<std::pair<std::string, bool>> extension_entries; // [[fmt.extensions]]
};

struct ProjectConfig {
  std::string root_dir;
  std::string name;
  std::string version;
  std::string build_root = "src/main.kl";
  std::string cache_dir = ".kinglet/cache";
  std::string out_dir = ".kinglet/out";
  std::string default_backend = "native";
  std::string build_default;
  std::unordered_map<std::string, std::string> modules; // logical id → path (nest)
  ProjectFmtSection fmt;
};

// Resolve the default build entry file from nest `build.default` + `modules { }`.
std::optional<std::string> resolve_build_entry_path(const ProjectConfig &config);

std::optional<ProjectConfig> find_project_config(const std::string &start_dir);
std::optional<ProjectConfig> load_nest_config_file(const std::filesystem::path &manifest_path);
std::optional<ProjectConfig> find_nest_config(const std::string &start_dir);

} // namespace kinglet
