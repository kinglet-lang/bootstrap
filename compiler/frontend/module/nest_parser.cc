// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "frontend/module/nest_parser.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace kinglet {

namespace {

std::string trim(const std::string &s) {
  auto start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  auto end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

bool starts_with(const std::string &s, const std::string &prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string parse_quoted(const std::string &s, size_t &i) {
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
    ++i;
  }
  if (i >= s.size() || s[i] != '"') {
    return "";
  }
  ++i;
  std::string out;
  while (i < s.size()) {
    const char c = s[i++];
    if (c == '"') {
      return out;
    }
    if (c == '\\' && i < s.size()) {
      out.push_back(s[i++]);
      continue;
    }
    out.push_back(c);
  }
  return "";
}

std::string parse_identifier(const std::string &s, size_t &i) {
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
    ++i;
  }
  size_t start = i;
  while (i < s.size() &&
         (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) {
    ++i;
  }
  if (start == i) {
    return "";
  }
  return s.substr(start, i - start);
}

std::string parse_value_token(const std::string &s, size_t &i) {
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
    ++i;
  }
  if (i < s.size() && s[i] == '"') {
    return parse_quoted(s, i);
  }
  size_t start = i;
  while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) {
    ++i;
  }
  return s.substr(start, i - start);
}

bool parse_project_line(const std::string &line, ProjectConfig &config) {
  if (!starts_with(line, "project ")) {
    return false;
  }
  size_t i = 8;
  config.name = parse_quoted(line, i);
  while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
    ++i;
  }
  if (!starts_with(line.substr(i), "version ")) {
    return false;
  }
  i += 8;
  config.version = parse_quoted(line, i);
  return true;
}

bool parse_kv_line(const std::string &line, std::string &key, std::string &value) {
  const auto eq = line.find('=');
  if (eq == std::string::npos) {
    return false;
  }
  key = trim(line.substr(0, eq));
  std::string rhs = trim(line.substr(eq + 1));
  if (rhs.size() >= 2 && rhs.front() == '"' && rhs.back() == '"') {
    value = rhs.substr(1, rhs.size() - 2);
  } else {
    value = rhs;
  }
  return !key.empty();
}

void append_extension_list(const std::string &raw, ProjectFmtSection &fmt) {
  std::string trimmed = trim(raw);
  if (trimmed.empty()) {
    return;
  }
  if (trimmed.front() == '[' && trimmed.back() == ']') {
    trimmed = trimmed.substr(1, trimmed.size() - 2);
  }
  std::stringstream ss(trimmed);
  std::string part;
  while (std::getline(ss, part, ',')) {
    std::string item = trim(part);
    if (item.size() >= 2 && item.front() == '"' && item.back() == '"') {
      item = item.substr(1, item.size() - 2);
    }
    if (!item.empty()) {
      fmt.extensions.push_back(item);
    }
  }
}

bool parse_bool_token(const std::string &value, bool &out) {
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

void parse_block_body(const std::string &body, const std::string &block_name, ProjectConfig &config) {
  std::istringstream in(body);
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::string key;
    std::string value;
    if (!parse_kv_line(line, key, value)) {
      continue;
    }
    if (block_name == "modules") {
      config.modules[key] = value;
    } else if (block_name == "build") {
      if (key == "default") {
        config.build_default = value;
      } else if (key == "out") {
        config.out_dir = value;
      } else if (key == "cache") {
        config.cache_dir = value;
      }
    } else if (block_name == "fmt") {
      if (key == "indent") {
        config.fmt.indent = std::stoi(value);
      } else if (key == "max_width") {
        config.fmt.max_width = std::stoi(value);
      } else if (key == "newline") {
        config.fmt.newline = value;
      } else if (key == "trailing_comma") {
        config.fmt.trailing_comma_set = parse_bool_token(value, config.fmt.trailing_comma);
      } else if (key == "extensions") {
        append_extension_list(value, config.fmt);
      }
    }
  }
}

std::string extract_block(const std::string &content, size_t &pos, const std::string &name) {
  while (pos < content.size()) {
    size_t line_start = pos;
    size_t line_end = content.find('\n', pos);
    if (line_end == std::string::npos) {
      line_end = content.size();
    }
    std::string line = trim(content.substr(line_start, line_end - line_start));
    pos = line_end + 1;
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (starts_with(line, name + " ")) {
      size_t brace = line.find('{');
      if (brace == std::string::npos) {
        continue;
      }
      std::string body;
      int depth = 1;
      body.append(line.substr(brace + 1));
      body.push_back('\n');
      while (pos < content.size() && depth > 0) {
        line_start = pos;
        line_end = content.find('\n', pos);
        if (line_end == std::string::npos) {
          line_end = content.size();
        }
        line = content.substr(line_start, line_end - line_start);
        pos = line_end + 1;
        for (char c : line) {
          if (c == '{') {
            ++depth;
          } else if (c == '}') {
            --depth;
          }
        }
        if (depth > 0) {
          body.append(line);
          body.push_back('\n');
        } else {
          const auto close = line.find('}');
          if (close != std::string::npos) {
            body.append(line.substr(0, close));
          }
        }
      }
      return body;
    }
  }
  return {};
}

} // namespace

bool parse_nest_manifest(const std::string &content, ProjectConfig &config) {
  size_t pos = 0;
  while (pos < content.size()) {
    size_t line_start = pos;
    size_t line_end = content.find('\n', pos);
    if (line_end == std::string::npos) {
      line_end = content.size();
    }
    std::string line = trim(content.substr(line_start, line_end - line_start));
    pos = line_end + 1;
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (starts_with(line, "project ")) {
      parse_project_line(line, config);
      continue;
    }
    if (starts_with(line, "modules ")) {
      pos = line_start;
      const std::string body = extract_block(content, pos, "modules");
      parse_block_body(body, "modules", config);
      continue;
    }
    if (starts_with(line, "build ")) {
      pos = line_start;
      const std::string body = extract_block(content, pos, "build");
      parse_block_body(body, "build", config);
      continue;
    }
    if (starts_with(line, "fmt ")) {
      pos = line_start;
      const std::string body = extract_block(content, pos, "fmt");
      parse_block_body(body, "fmt", config);
      continue;
    }
    // targets { } and other blocks are ignored in this phase.
    if (line.find('{') != std::string::npos) {
      pos = line_start;
      const auto space = line.find(' ');
      const std::string block_name = space == std::string::npos ? line : line.substr(0, space);
      (void)extract_block(content, pos, block_name);
    }
  }
  return true;
}

std::optional<ProjectConfig> load_nest_config_file(const std::filesystem::path &manifest_path) {
  std::ifstream file(manifest_path, std::ios::in);
  if (!file) {
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  ProjectConfig config;
  config.root_dir = manifest_path.parent_path().string();
  if (!parse_nest_manifest(buffer.str(), config)) {
    return std::nullopt;
  }
  return config;
}

} // namespace kinglet
