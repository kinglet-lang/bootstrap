// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "driver/kinglet/cmd_init.h"

#include "driver/kinglet/cli_internal.h"
#include "frontend/module/project_config.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace kinglet {

namespace fs = std::filesystem;

namespace {

bool is_valid_project_name(const std::string &name) {
  if (name.empty() || name == "." || name == "..") {
    return false;
  }
  return name.find('/') == std::string::npos && name.find('\\') == std::string::npos;
}

bool stdin_is_interactive() {
#if defined(_WIN32)
  return _isatty(_fileno(stdin)) != 0;
#else
  return isatty(STDIN_FILENO) != 0;
#endif
}

void redraw_answer(const std::string &label, const std::string &value, int lines) {
  const ui::Painter &p = ui::g_out;
  for (int i = 0; i < lines; ++i) {
    std::cout << "\033[1A\033[2K";
  }
  std::cout << '\r' << p.green("✓") << "  " << p.bold(label) << ' ' << p.dim("·") << ' '
            << value << '\n';
}

std::string ask_line(const std::string &label, const std::string &def) {
  const ui::Painter &p = ui::g_out;
  std::cout << p.cyan("?") << "  " << p.bold(label) << ' ' << p.dim("(" + def + ")") << ' '
            << p.dim("›") << ' ' << std::flush;
  std::string line;
  if (!std::getline(std::cin, line)) {
    line.clear();
  }
  line = trim_copy(line);
  const std::string value = line.empty() ? def : line;
  if (p.on) {
    redraw_answer(label, value, 1);
  }
  return value;
}

std::string expand_tilde(const std::string &path) {
  if (path != "~" && path.rfind("~/", 0) != 0) {
    return path;
  }
  const char *home = std::getenv("HOME");
  if (home == nullptr || *home == '\0') {
    return path;
  }
  return std::string(home) + path.substr(1);
}

std::string resolve_init_target(int argc, char **argv) {
  if (argc >= 3) {
    return std::string(argv[2]);
  }
  if (stdin_is_interactive()) {
    const ui::Painter &p = ui::g_out;
    std::cout << '\n' << p.bold("Create a new Kinglet project") << "\n\n";
    const std::string name = ask_line("Project name", "kinglet-app");
    const std::string location = expand_tilde(ask_line("Location", "."));
    std::cout << '\n';
    return (fs::path(location) / name).string();
  }
  return "kinglet-app";
}

std::string toml_escape(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 2);
  for (char c : value) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

} // namespace

int cmd_init(int argc, char **argv) {
  std::string target = trim_copy(resolve_init_target(argc, argv));
  while (target.size() > 1 && (target.back() == '/' || target.back() == '\\')) {
    target.pop_back();
  }
  if (target.empty()) {
    print_error("init", "missing project path");
    return 64;
  }
  const fs::path dir = fs::absolute(fs::path(target).lexically_normal());
  const std::string project_name = dir.filename().string();
  if (!is_valid_project_name(project_name)) {
    print_error("init", "invalid project name '" + project_name + "'");
    return 64;
  }

  const fs::path manifest = dir / "kinglet.nest";
  if (fs::exists(manifest)) {
    print_error("init", "project '" + project_name + "' already exists");
    return 64;
  }
  if (!ensure_dir(dir)) {
    print_error("init", "cannot create " + dir.string());
    return 74;
  }
  {
    std::ofstream out(manifest);
    if (!out) {
      print_error("init", "cannot write " + manifest.string());
      return 74;
    }
    out << "project \"" << toml_escape(project_name) << "\" version \"0.1.0\"\n"
        << "\n"
        << "modules {\n"
        << "  app = \"src/main.kl\"\n"
        << "}\n"
        << "\n"
        << "build {\n"
        << "  default = \"app\"\n"
        << "  backend = native\n"
        << "  cache = \".kinglet/cache\"\n"
        << "  out = \".kinglet/out\"\n"
        << "}\n";
  }
  const fs::path src_dir = dir / "src";
  const fs::path main_kl = src_dir / "main.kl";
  if (!ensure_dir(src_dir)) {
    print_error("init", "cannot create " + src_dir.string());
    return 74;
  }
  if (!fs::exists(main_kl)) {
    std::ofstream out(main_kl);
    if (!out) {
      print_error("init", "cannot write " + main_kl.string());
      return 74;
    }
    out << "export module app;\n"
        << "using io;\n"
        << "\n"
        << "int main() {\n"
        << "  io::out.line(\"Hello from {}!\", \"" << toml_escape(project_name) << "\");\n"
        << "  return 0;\n"
        << "}\n";
  }

  const ui::Painter &p = ui::g_out;
  std::cout << p.green("✓") << "  Created project " << p.bold(project_name) << "\n\n";
  std::cout << "   " << p.dim(display_path(dir) + "/") << "\n";
  std::cout << "   " << p.cyan("kinglet.nest") << "\n";
  std::cout << "   " << p.cyan("src/main.kl") << "\n\n";
  std::cout << p.bold("   Next steps") << "\n";
  std::cout << p.dim("     cd " + display_path(dir)) << "\n";
  std::cout << p.dim("     " + g_prog + " build") << "\n";
  return 0;
}

} // namespace kinglet
