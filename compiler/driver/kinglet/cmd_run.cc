// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "driver/kinglet/cmd_run.h"

#include "driver/kinglet/cli_internal.h"
#include "driver/kinglet/cli_spawn.h"
#include "driver/kinglet/cmd_build.h"
#include "frontend/module/project_config.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace kinglet {

namespace fs = std::filesystem;

int cmd_run(int argc, char **argv, const std::string &self_executable) {
  if (argc >= 3 && std::string_view(argv[2]).ends_with(".kl")) {
    std::vector<std::string> args;
    args.push_back(argv[2]);
    for (int i = 3; i < argc; ++i) {
      args.push_back(argv[i]);
    }
    return spawn_reexec(self_executable, args);
  }

  const auto config = find_project_config(fs::current_path().string());
  if (!config) {
    print_error("run", "no kinglet.nest; use kinglet <file.kl> or kinglet init");
    return 2;
  }

  const fs::path out_dir = fs::path(config->root_dir) / config->out_dir;
  const std::string out_name = build_output_name(*config);
  fs::path native_bin = out_dir / out_name;

  if (fs::exists(native_bin)) {
#if defined(_WIN32)
    print_error("run", "executing built binaries is not supported on Windows");
    return 78;
#else
    std::vector<char *> exec_argv;
    exec_argv.push_back(const_cast<char *>(native_bin.c_str()));
    for (int i = 2; i < argc; ++i) {
      exec_argv.push_back(argv[i]);
    }
    exec_argv.push_back(nullptr);
    execv(native_bin.c_str(), exec_argv.data());
    print_error("run", "exec failed for " + native_bin.string());
    return 71;
#endif
  }

  print_error("run", "no build output in " + display_path(out_dir) + " (run kinglet build)");
  return 2;
}

} // namespace kinglet
