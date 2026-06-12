#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace kinglet {

struct ProjectConfig {
  std::string root_dir;
  std::string name;
  std::string version;
  std::string build_root = "src/main.kl";
  std::string cache_dir = ".kinglet/cache";
  std::string out_dir = ".kinglet/out";
  std::string default_backend = "native";
  std::unordered_map<std::string, std::string> dependencies; // name → path
};

std::optional<ProjectConfig> find_project_config(const std::string &start_dir);

} // namespace kinglet
