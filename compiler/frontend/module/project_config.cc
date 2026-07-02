// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "frontend/module/project_config.h"

#include "frontend/module/nest_parser.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace kinglet {

bool parse_target_kind(const std::string &s, TargetKind &out) {
  if (s == "binary" || s == "executable") {
    out = TargetKind::Binary;
    return true;
  }
  if (s == "library") {
    out = TargetKind::Library;
    return true;
  }
  if (s == "test") {
    out = TargetKind::Test;
    return true;
  }
  if (s == "object") {
    out = TargetKind::Object;
    return true;
  }
  return false;
}

const TargetConfig *find_target(const ProjectConfig &config, const std::string &name) {
  for (const auto &t : config.targets) {
    if (t.name == name) {
      return &t;
    }
  }
  return nullptr;
}

std::vector<std::string> resolve_target_sources(const ProjectConfig &config,
                                                const TargetConfig &target) {
  std::vector<std::string> out;
  const std::filesystem::path root(config.root_dir);
  std::error_code ec;

  for (const std::string &entry : target.sources) {
    const std::filesystem::path abs = root / entry;
    const bool looks_like_dir = (!entry.empty() && (entry.back() == '/' || entry.back() == '\\')) ||
                                std::filesystem::is_directory(abs, ec);
    if (looks_like_dir) {
      std::vector<std::string> dir_files;
      for (std::filesystem::directory_iterator it(abs, ec), end; it != end; it.increment(ec)) {
        if (ec) {
          break;
        }
        const std::filesystem::path &p = it->path();
        if (!std::filesystem::is_regular_file(p, ec)) {
          continue;
        }
        if (p.extension() != ".kl") {
          continue;
        }
        dir_files.push_back(p.lexically_normal().string());
      }
      std::sort(dir_files.begin(), dir_files.end());
      out.insert(out.end(), dir_files.begin(), dir_files.end());
    } else {
      out.push_back(abs.lexically_normal().string());
    }
  }
  return out;
}

namespace {

// Cheap syntactic check for a top-level `int main(` in a source file. Kinglet's
// entry function is `int main()`; scanning for the signature avoids a full parse.
bool file_defines_main(const std::string &path) {
  std::ifstream file(path, std::ios::in);
  if (!file) {
    return false;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  const std::string source = buffer.str();
  // Match `int main` followed by optional whitespace and `(`, not preceded by an
  // identifier char (so it doesn't match e.g. `uint main`).
  static const std::regex main_re(R"((^|[^A-Za-z0-9_])int\s+main\s*\()");
  return std::regex_search(source, main_re);
}

} // namespace

std::optional<std::string> resolve_target_entry(const ProjectConfig &config,
                                                const TargetConfig &target, std::string &error) {
  const std::vector<std::string> sources = resolve_target_sources(config, target);
  std::vector<std::string> mains;
  for (const std::string &src : sources) {
    if (file_defines_main(src)) {
      mains.push_back(src);
    }
  }
  if (mains.empty()) {
    error = "target '" + target.name + "' has no source defining 'int main()'";
    return std::nullopt;
  }
  if (mains.size() > 1) {
    error = "target '" + target.name + "' has multiple sources defining 'int main()': " + mains[0] +
            ", " + mains[1];
    return std::nullopt;
  }
  return mains.front();
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
