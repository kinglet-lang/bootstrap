// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "driver/kinglet/cmd_fmt.h"

#include "driver/kinglet/cli_internal.h"
#include "frontend/module/project_config.h"
#include "driver/preen/config.h"
#include "driver/preen/preen.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace kinglet {

namespace fs = std::filesystem;

namespace {

std::vector<fs::path> collect_kl_files(const fs::path &root) {
  std::vector<fs::path> files;
  std::error_code ec;
  if (!fs::exists(root, ec)) {
    return files;
  }
  if (fs::is_regular_file(root) && root.extension() == ".kl") {
    files.push_back(root);
    return files;
  }
  for (const fs::directory_entry &entry : fs::recursive_directory_iterator(root, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_regular_file()) {
      continue;
    }
    const fs::path path = entry.path();
    if (path.extension() != ".kl") {
      continue;
    }
    const std::string rel = path.lexically_relative(root).string();
    if (rel.rfind(".kinglet", 0) == 0) {
      continue;
    }
    files.push_back(path);
  }
  std::sort(files.begin(), files.end());
  return files;
}

} // namespace

int cmd_fmt(int argc, char **argv) {
  bool check = false;
  bool write = false;
  bool stdin_mode = false;
  std::string config_path;
  std::vector<fs::path> paths;
  for (int i = 2; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--check") {
      check = true;
      continue;
    }
    if (arg == "--write") {
      write = true;
      continue;
    }
    if (arg == "--stdin") {
      stdin_mode = true;
      continue;
    }
    if (arg == "--config") {
      if (i + 1 >= argc) {
        print_error("fmt", "--config requires a path");
        return 64;
      }
      config_path = argv[++i];
      continue;
    }
    if (arg.starts_with('-')) {
      print_error("fmt", std::string("unknown option ") + std::string(arg));
      return 64;
    }
    paths.emplace_back(argv[i]);
  }

  if (!check && !write && !stdin_mode) {
    write = true;
  }
  if (stdin_mode && (!paths.empty() || check)) {
    print_error("fmt", "--stdin cannot be combined with paths or --check");
    return 64;
  }

  kinglet::preen::FmtConfig fmt_config = kinglet::preen::FmtConfig::defaults();
  if (!config_path.empty()) {
    const fs::path manifest = fs::absolute(config_path);
    std::optional<ProjectConfig> config;
    if (manifest.filename() == "kinglet.nest") {
      config = load_nest_config_file(manifest);
    }
    if (!config) {
      config = find_project_config(manifest.parent_path().string());
    }
    if (!config) {
      print_error("fmt", "no kinglet.nest found for --config");
      return 2;
    }
    fmt_config = kinglet::preen::fmt_config_from_project(*config);
  } else if (!paths.empty()) {
    const auto config = find_project_config(fs::absolute(paths.front()).parent_path().string());
    if (config) {
      fmt_config = kinglet::preen::fmt_config_from_project(*config);
    }
  } else {
    const auto config = find_project_config(fs::current_path().string());
    if (config) {
      fmt_config = kinglet::preen::fmt_config_from_project(*config);
    }
  }

  if (stdin_mode) {
    std::string input;
    std::string line;
    while (std::getline(std::cin, line)) {
      if (!input.empty()) {
        input.push_back('\n');
      }
      input += line;
    }
    const auto result = kinglet::preen::format_string(input, fmt_config);
    if (!result.error.empty()) {
      print_error("fmt", result.error);
      return 1;
    }
    std::cout << result.text;
    return 0;
  }

  if (paths.empty()) {
    const auto config = find_project_config(fs::current_path().string());
    if (!config) {
      print_error("fmt", "no paths given and no kinglet.nest found");
      return 2;
    }
    paths = collect_kl_files(fs::path(config->root_dir));
    if (paths.empty()) {
      const auto entry_path = resolve_build_entry_path(*config);
      if (entry_path && fs::exists(*entry_path)) {
        paths.push_back(*entry_path);
      }
    }
  } else {
    std::vector<fs::path> expanded;
    for (const fs::path &path : paths) {
      const auto collected = collect_kl_files(fs::absolute(path));
      if (collected.empty() && fs::exists(path) && path.extension() == ".kl") {
        expanded.push_back(fs::absolute(path));
      } else {
        expanded.insert(expanded.end(), collected.begin(), collected.end());
      }
    }
    paths = std::move(expanded);
  }

  if (paths.empty()) {
    print_error("fmt", "no .kl files found to format");
    return 2;
  }

  int changed_count = 0;
  bool had_error = false;
  for (const fs::path &path : paths) {
    const auto result = kinglet::preen::format_file(path, fmt_config);
    if (!result.error.empty()) {
      print_error("fmt", display_path(path) + ": " + result.error);
      had_error = true;
      continue;
    }
    if (!result.changed) {
      continue;
    }
    ++changed_count;
    if (check) {
      std::cout << display_path(path) << '\n';
      continue;
    }
    if (write) {
      std::ofstream out(path, std::ios::binary | std::ios::trunc);
      if (!out) {
        print_error("fmt", "cannot write " + path.string());
        had_error = true;
        continue;
      }
      out << result.text;
    }
  }

  if (had_error) {
    return 1;
  }
  if (check && changed_count > 0) {
    return 1;
  }
  if (!check && write && changed_count > 0) {
    const ui::Painter &p = ui::g_out;
    std::cout << p.green("✓") << "  Formatted " << changed_count << " file(s)\n";
  }
  return 0;
}

} // namespace kinglet
