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

namespace kinglet {

namespace fs = std::filesystem;

namespace {

std::string default_output_name(const ProjectConfig &config, const std::string &target_name) {
  // Special-case the self-host compiler target name if present.
  if (target_name == "compiler" || target_name == "core") {
    return "compiler";
  }
  if (!target_name.empty()) {
    return target_name;
  }
  if (!config.name.empty()) {
    return config.name;
  }
  return "app";
}

} // namespace

std::string build_output_name(const ProjectConfig &config) {
  return default_output_name(config, config.build_default);
}

int cmd_build(int argc, char **argv, const std::string &self_executable) {
  bool quiet = false;
  std::string root_arg;
  std::string target_arg;
  for (int i = 2; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--quiet") {
      quiet = true;
      continue;
    }
    if (arg.starts_with('-')) {
      print_error("build", std::string("unknown option ") + std::string(arg));
      return 64;
    }
    // First positional = target name to build.
    target_arg = std::string(arg);
    break;
  }

  const std::string start = fs::current_path().string();
  const auto config = find_project_config(start);
  if (!config) {
    print_error("build", "no kinglet.nest found (run kinglet init)");
    return 2;
  }

  // Choose the target: explicit arg, else build.default.
  const std::string target_name = target_arg.empty() ? config->build_default : target_arg;
  if (target_name.empty()) {
    print_error("build", "no target specified and no build.default in kinglet.nest");
    return 2;
  }
  const TargetConfig *target = find_target(*config, target_name);
  if (target == nullptr) {
    print_error("build", "unknown target '" + target_name + "' in kinglet.nest");
    return 2;
  }
  if (target->kind == TargetKind::Library || target->kind == TargetKind::Object) {
    print_error("build", "target '" + target_name + "' is not runnable (kind is library/object)");
    return 2;
  }

  std::string entry_err;
  const auto entry_path = resolve_target_entry(*config, *target, entry_err);
  if (!entry_path) {
    print_error("build", entry_err);
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

  const std::string out_name = default_output_name(*config, target_name);
  const fs::path out_path = out_dir / out_name;

  std::vector<std::string> args;
#ifndef KINGLET_HAVE_LLVM
  print_error("build", "native backend not available (rebuild with enable_llvm=true)");
  return 78;
#else
  const fs::path obj_cache = fs::path(config->root_dir) / ".kinglet/objects/native";
  ensure_dir(obj_cache);
  args = {"-o",
          out_path.string(),
          "--obj-cache",
          obj_cache.string(),
          "--source-prefix",
          config->root_dir,
          entry.string()};
#endif
  if (args.empty()) {
    return 78;
  }

  const ui::Painter &p = ui::g_err;
  if (!quiet) {
    std::cerr << p.dim("›") << "  Building " << p.bold(out_name) << "\n";
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
