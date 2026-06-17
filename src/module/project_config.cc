#include "module/project_config.h"

#include "module/nest_parser.h"

#include <algorithm>
#include <cctype>
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

std::vector<std::string> parse_string_array(const std::string &value) {
  std::vector<std::string> items;
  std::string trimmed = trim(value);
  if (trimmed.empty() || trimmed.front() != '[' || trimmed.back() != ']') {
    return items;
  }
  std::string inner = trimmed.substr(1, trimmed.size() - 2);
  std::stringstream ss(inner);
  std::string part;
  while (std::getline(ss, part, ',')) {
    std::string item = unquote(trim(part));
    if (!item.empty()) {
      items.push_back(item);
    }
  }
  return items;
}

bool parse_bool(const std::string &value, bool &out) {
  const std::string v = trim(value);
  if (v == "true") {
    out = true;
    return true;
  }
  if (v == "false") {
    out = false;
    return true;
  }
  return false;
}

std::string section_name(const std::string &trimmed) {
  if (trimmed.size() >= 4 && trimmed.rfind("[[", 0) == 0 && trimmed.substr(trimmed.size() - 2) == "]]") {
    return trimmed.substr(2, trimmed.size() - 4);
  }
  if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
    return trimmed.substr(1, trimmed.size() - 2);
  }
  return {};
}

} // namespace

bool parse_manifest_stream(std::istream &file, ProjectConfig &config) {
  std::string line;
  std::string current_section;
  while (std::getline(file, line)) {
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') continue;

    if (trimmed.front() == '[' && trimmed.back() == ']') {
      current_section = section_name(trimmed);
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
    } else if (current_section == "build") {
      if (key == "root") config.build_root = value;
      else if (key == "cache_dir") config.cache_dir = value;
      else if (key == "out_dir") config.out_dir = value;
    } else if (current_section == "build.compiler" || current_section == "compiler") {
      if (key == "default_backend") config.default_backend = value;
    } else if (current_section == "fmt") {
      if (key == "indent") {
        config.fmt.indent = std::stoi(value);
      } else if (key == "max_width") {
        config.fmt.max_width = std::stoi(value);
      } else if (key == "newline") {
        config.fmt.newline = value;
      } else if (key == "trailing_comma") {
        config.fmt.trailing_comma_set = parse_bool(value, config.fmt.trailing_comma);
      } else if (key == "extensions") {
        config.fmt.extensions = parse_string_array(value);
      }
    } else if (current_section == "fmt.extensions") {
      if (key == "name") {
        config.fmt.extension_entries.push_back({value, true});
      } else if (key == "enabled" && !config.fmt.extension_entries.empty()) {
        bool enabled = true;
        if (parse_bool(value, enabled)) {
          config.fmt.extension_entries.back().second = enabled;
        }
      }
    } else if (current_section == "dependencies") {
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
  return true;
}

std::optional<ProjectConfig> load_project_config_file(const std::filesystem::path &manifest_path) {
  std::ifstream file(manifest_path, std::ios::in);
  if (!file) {
    return std::nullopt;
  }
  ProjectConfig config;
  config.root_dir = manifest_path.parent_path().string();
  if (!parse_manifest_stream(file, config)) {
    return std::nullopt;
  }
  return config;
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
  std::filesystem::path dir(start_dir);
  while (true) {
    const std::filesystem::path nest = dir / "kinglet.nest";
    if (std::filesystem::exists(nest)) {
      return load_nest_config_file(nest);
    }
    const std::filesystem::path toml = dir / "kinglet.toml";
    if (std::filesystem::exists(toml)) {
      return load_project_config_file(toml);
    }

    const auto parent = dir.parent_path();
    if (parent == dir) {
      break;
    }
    dir = parent;
  }
  return std::nullopt;
}

} // namespace kinglet
