// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "driver/kinglet/cmd_build.h"

#include "driver/kinglet/cli_internal.h"
#include "driver/kinglet/cli_spawn.h"
#include "frontend/module/project_config.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#ifndef KINGLET_VERSION
#define KINGLET_VERSION "0.1.0-dev"
#endif

namespace kinglet {

namespace fs = std::filesystem;

namespace {

std::string default_output_name(const ProjectConfig &config) {
  if (!config.build_default.empty()) {
    if (config.build_default == "core.main") {
      return "compiler";
    }
    const auto dot = config.build_default.rfind('.');
    if (dot != std::string::npos && dot + 1 < config.build_default.size()) {
      return config.build_default.substr(dot + 1);
    }
    return config.build_default;
  }
  if (config.build_root == "core/main.kl") {
    return "compiler";
  }
  if (!config.name.empty()) {
    return config.name;
  }
  return "app";
}

} // namespace

std::string build_output_name(const ProjectConfig &config) {
  return default_output_name(config);
}

int cmd_build(int argc, char **argv, const std::string &self_executable) {
  bool quiet = false;
  std::string backend;
  std::string root_arg;
  for (int i = 2; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--quiet") {
      quiet = true;
      continue;
    }
    if (arg == "--backend") {
      if (i + 1 >= argc || std::string_view(argv[i + 1]) != "native") {
        print_error("build", "--backend requires native");
        return 64;
      }
      backend = "native";
      ++i;
      continue;
    }
    if (arg.starts_with('-')) {
      print_error("build", std::string("unknown option ") + std::string(arg));
      return 64;
    }
    root_arg = std::string(arg);
    break;
  }

  const std::string start =
      root_arg.empty() ? fs::current_path().string() : fs::absolute(root_arg).string();
  const auto config = find_project_config(start);
  if (!config) {
    print_error("build", "no kinglet.nest found (run kinglet init)");
    return 2;
  }
  if (backend.empty()) {
    backend = config->default_backend;
  }
  if (backend != "native") {
    print_error("build", "only native backend is supported (vm removed)");
    return 64;
  }

  const auto entry_path = resolve_build_entry_path(*config);
  if (!entry_path) {
    print_error("build", "build.default module not found in kinglet.nest modules table");
    return 2;
  }
  const fs::path entry = *entry_path;
  if (!fs::exists(entry)) {
    print_error("build", "entry not found: " + entry.string());
    return 2;
  }

  const fs::path out_dir = fs::path(config->root_dir) / config->out_dir;
  ensure_dir(out_dir);
  ensure_dir(fs::path(config->root_dir) / ".kinglet/objects");

  const std::string out_name = default_output_name(*config);
  const fs::path out_path = out_dir / out_name;

  std::vector<std::string> args;
#ifndef KINGLET_HAVE_LLVM
  print_error("build", "native backend not available (rebuild with enable_llvm=true)");
  return 78;
#else
  const fs::path obj_cache = fs::path(config->root_dir) / ".kinglet/objects/native";
  ensure_dir(obj_cache);
  args = {"--backend", "native", "-o", out_path.string(), "--obj-cache", obj_cache.string(),
          entry.string()};
#endif
  if (args.empty()) {
    return 78;
  }

  const ui::Painter &p = ui::g_err;
  if (!quiet) {
    std::cerr << p.dim("›") << "  Building " << p.bold(out_name) << "  "
              << p.dim("(" + backend + " backend)") << "\n";
  }

  const auto t0 = std::chrono::steady_clock::now();
  const int rc = spawn_and_wait(self_executable, args);
  const auto t1 = std::chrono::steady_clock::now();

  if (!quiet) {
    if (rc == 0) {
      const double secs =
          std::chrono::duration_cast<std::chrono::duration<double>>(t1 - t0).count();
      char elapsed[32];
      std::snprintf(elapsed, sizeof(elapsed), "%.2fs", secs);
      std::cerr << p.green("✓") << "  Built " << p.bold(out_name) << "  "
                << p.dim("→ " + display_path(out_path)) << "  " << p.dim(elapsed) << "\n";
    } else {
      print_error("build", "build failed (exit " + std::to_string(rc) + ")");
    }
  }
  return rc;
}

} // namespace kinglet
