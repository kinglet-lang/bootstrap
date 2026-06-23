// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "driver/kinglet/cli_driver.h"

#include "driver/kinglet/cli_internal.h"
#include "driver/kinglet/cli_spawn.h"
#include "driver/kinglet/cmd_build.h"
#include "driver/kinglet/cmd_fmt.h"
#include "driver/kinglet/cmd_init.h"
#include "driver/kinglet/cmd_prune.h"
#include "driver/kinglet/cmd_run.h"

#include <filesystem>
#include <iostream>
#include <string_view>

#ifndef KINGLET_VERSION
#define KINGLET_VERSION "0.1.0-dev"
#endif

namespace kinglet {

namespace fs = std::filesystem;

namespace {

std::string program_name(const char *argv0) {
  if (argv0 == nullptr || *argv0 == '\0') {
    return "kinglet";
  }
  std::string name = fs::path(argv0).filename().string();
#if defined(_WIN32)
  if (name.size() > 4 && name.substr(name.size() - 4) == ".exe") {
    name = name.substr(0, name.size() - 4);
  }
#endif
  return name.empty() ? "kinglet" : name;
}

void print_cli_help(std::ostream &out) {
  const ui::Painter &p = (&out == &std::cout) ? ui::g_out : ui::g_err;
  auto cmd = [&](std::string_view name, std::string_view desc) {
    out << "  " << p.cyan(name);
    int pad = 36 - static_cast<int>(name.size());
    if (pad < 1) {
      pad = 1;
    }
    for (int i = 0; i < pad; ++i) {
      out << ' ';
    }
    out << p.dim(desc) << '\n';
  };

  out << p.bold("Kinglet") << ' ' << p.dim(KINGLET_VERSION) << "\n\n";
  out << p.bold("USAGE") << "\n";
  cmd(g_prog + " <file.kl> [args...]", "Compile and run (default)");
  cmd(g_prog + " init [path]", "Scaffold a new project (name = last segment)");
  cmd(g_prog + " build [--backend native]", "Build the project (--quiet, [root])");
  cmd(g_prog + " run [<file.kl> | args...]", "Run the built binary or a source file");
  cmd(g_prog + " prune [--all] [-n]", "Prune unreferenced Klos objects");
  cmd(g_prog + " fmt [--check] [paths...]", "Format sources (CI check or write)");
  cmd(g_prog + " -h, --help", "Show this help");
  cmd(g_prog + " -v, --version", "Show version");
  out << "\n" << p.bold("COMPILER MODES") << " " << p.dim("(flags)") << "\n";
  cmd("--check, --tokens, --ast", "Diagnostics, tokens, AST");
  cmd("--ir", "KIR dump");
  cmd("--native <out>", "Native executable (requires LLVM build)");
  out << '\n';
}

} // namespace

int run_cli_subcommand(int argc, char **argv) {
  if (argc < 2) {
    return -1;
  }
  ui::init();
  g_prog = program_name(argv[0]);
  const std::string_view cmd(argv[1]);
  if (cmd == "-h" || cmd == "--help") {
    print_cli_help(std::cout);
    return 0;
  }
  if (cmd == "-v" || cmd == "--version") {
    std::cout << g_prog << ' ' << KINGLET_VERSION << '\n';
    return 0;
  }
  if (cmd == "init") {
    return cmd_init(argc, argv);
  }
  if (cmd == "build") {
    return cmd_build(argc, argv, resolve_self_executable(argv[0]));
  }
  if (cmd == "run") {
    return cmd_run(argc, argv, resolve_self_executable(argv[0]));
  }
  if (cmd == "prune") {
    return cmd_prune(argc, argv);
  }
  if (cmd == "fmt") {
    return cmd_fmt(argc, argv);
  }
  if (cmd == "help") {
    print_cli_help(std::cout);
    return 0;
  }
  return -1;
}

} // namespace kinglet
