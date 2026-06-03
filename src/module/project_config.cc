#include "module/project_config.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace kinglet {

namespace {

std::string trim(const std::string &s) {
  auto start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  auto end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

std::string unquote(const std::string &s) {
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
    return s.substr(1, s.size() - 2);
  }
  return s;
}

} // namespace

std::optional<ProjectConfig> find_project_config(const std::string &start_dir) {
  std::filesystem::path dir(start_dir);
  while (true) {
    std::filesystem::path candidate = dir / "kinglet.toml";
    if (std::filesystem::exists(candidate)) {
      std::ifstream file(candidate, std::ios::in);
      if (!file) return std::nullopt;

      ProjectConfig config;
      config.root_dir = dir.string();

      std::string line;
      std::string current_section;
      while (std::getline(file, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        if (trimmed.front() == '[' && trimmed.back() == ']') {
          current_section = trimmed.substr(1, trimmed.size() - 2);
          continue;
        }

        auto eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(trimmed.substr(0, eq));
        std::string value = trim(trimmed.substr(eq + 1));
        value = unquote(value);

        if (current_section == "project") {
          if (key == "name") config.name = value;
          else if (key == "version") config.version = value;
        } else if (current_section == "dependencies") {
          // Parse inline table: name = { path = "..." }
          auto brace_start = value.find('{');
          if (brace_start != std::string::npos) {
            auto path_pos = value.find("path");
            if (path_pos != std::string::npos) {
              auto path_eq = value.find('=', path_pos);
              if (path_eq != std::string::npos) {
                auto q1 = value.find('"', path_eq);
                auto q2 = value.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos) {
                  config.dependencies[key] = value.substr(q1 + 1, q2 - q1 - 1);
                }
              }
            }
          } else {
            config.dependencies[key] = value;
          }
        }
      }
      return config;
    }

    auto parent = dir.parent_path();
    if (parent == dir) break;
    dir = parent;
  }
  return std::nullopt;
}

} // namespace kinglet
