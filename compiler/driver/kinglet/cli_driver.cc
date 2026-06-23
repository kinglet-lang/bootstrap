// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "driver/kinglet/cli_driver.h"

#include "frontend/module/project_config.h"
#include "driver/preen/config.h"
#include "driver/preen/preen.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <cstring>
#include <filesystem>
#if defined(_WIN32)
#include <io.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <stdlib.h>
#elif defined(__linux__)
#include <unistd.h>
#endif
#if !defined(_WIN32)
#include <unistd.h>
#include <sys/wait.h>
#endif
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#ifndef KINGLET_VERSION
#define KINGLET_VERSION "0.1.0-dev"
#endif

namespace kinglet {
namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Terminal styling. Colors are emitted only when the target stream is an
// interactive TTY and NO_COLOR is unset, so piped output and golden tests stay
// byte-for-byte plain.
// ---------------------------------------------------------------------------
namespace ui {

constexpr const char *kReset = "\033[0m";
constexpr const char *kBold = "\033[1m";
constexpr const char *kDim = "\033[2m";
constexpr const char *kRed = "\033[31m";
constexpr const char *kGreen = "\033[32m";
constexpr const char *kYellow = "\033[33m";
constexpr const char *kCyan = "\033[36m";

bool fd_is_tty(int fd) {
#if defined(_WIN32)
  return _isatty(fd) != 0;
#else
  return isatty(fd) != 0;
#endif
}

bool stream_supports_color(int fd) {
  if (std::getenv("NO_COLOR") != nullptr) {
    return false;
  }
  const char *term = std::getenv("TERM");
  if (term != nullptr && std::string_view(term) == "dumb") {
    return false;
  }
#if defined(_WIN32)
  // Avoid garbage on legacy consoles: only colorize when a VT-capable terminal
  // advertises itself (Windows Terminal, or a TERM provided by an emulator).
  if (std::getenv("WT_SESSION") == nullptr && term == nullptr) {
    return false;
  }
#endif
  return fd_is_tty(fd);
}

struct Painter {
  bool on = false;

  std::string wrap(const char *code, std::string_view text) const {
    if (!on) {
      return std::string(text);
    }
    std::string r;
    r.reserve(text.size() + 12);
    r += code;
    r += text;
    r += kReset;
    return r;
  }

  std::string bold(std::string_view t) const { return wrap(kBold, t); }
  std::string dim(std::string_view t) const { return wrap(kDim, t); }
  std::string red(std::string_view t) const { return wrap(kRed, t); }
  std::string green(std::string_view t) const { return wrap(kGreen, t); }
  std::string yellow(std::string_view t) const { return wrap(kYellow, t); }
  std::string cyan(std::string_view t) const { return wrap(kCyan, t); }
};

Painter g_out;
Painter g_err;

void init() {
#if defined(_WIN32)
  g_out.on = stream_supports_color(_fileno(stdout));
  g_err.on = stream_supports_color(_fileno(stderr));
#else
  g_out.on = stream_supports_color(STDOUT_FILENO);
  g_err.on = stream_supports_color(STDERR_FILENO);
#endif
}

} // namespace ui

// Invoked program name (kinglet or its klet alias), resolved from argv[0].
std::string g_prog = "kinglet";

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

// Path relative to the current directory when possible, for readable output.
std::string display_path(const fs::path &p) {
  std::error_code ec;
  const fs::path rel = fs::relative(p, fs::current_path(), ec);
  if (!ec && !rel.empty() && rel.native()[0] != '.') {
    return rel.string();
  }
  if (!ec && !rel.empty() && rel.string().rfind("..", 0) != 0) {
    return rel.string();
  }
  return p.string();
}

// Consistent error line: "✗ <prog> <subcommand>: <message>" with a red glyph on
// a TTY. The program name follows the invoked alias (kinglet or klet).
void print_error(std::string_view subcommand, std::string_view message) {
  const std::string label =
      subcommand.empty() ? g_prog : (g_prog + " " + std::string(subcommand));
  std::cerr << ui::g_err.red("\u2717") << " " << ui::g_err.bold(label) << ": " << message << '\n';
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
  cmd("--bytecode, --ir", "Bytecode / KIR dump");
  cmd("--native <out>", "Native executable (requires LLVM build)");
  out << '\n';
}

bool ensure_dir(const fs::path &path) {
  std::error_code ec;
  fs::create_directories(path, ec);
  return !ec;
}

std::string trim_copy(std::string s) {
  const auto start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  const auto end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

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

// Redraw the just-answered prompt as a single "✓ Label · value" line, after
// clearing the `lines` rows the question occupied (create-vue / create-tauri
// style). Only used on a color TTY.
void redraw_answer(const std::string &label, const std::string &value, int lines) {
  const ui::Painter &p = ui::g_out;
  for (int i = 0; i < lines; ++i) {
    std::cout << "\033[1A\033[2K";
  }
  std::cout << '\r' << p.green("\u2713") << "  " << p.bold(label) << ' ' << p.dim("\u00b7") << ' '
            << value << '\n';
}

// A free-text question. Shows the default as a dim hint; empty input keeps it.
std::string ask_line(const std::string &label, const std::string &def) {
  const ui::Painter &p = ui::g_out;
  std::cout << p.cyan("?") << "  " << p.bold(label) << ' ' << p.dim("(" + def + ")") << ' '
            << p.dim("\u203a") << ' ' << std::flush;
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

// Expand a leading "~" to $HOME (the shell does not run on interactive input).
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

// Resolve where the project should be created. A positional argument wins and
// is used as the full target path. Otherwise, on an interactive terminal, ask
// for the project name and the parent location separately, then combine them.
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

// execv does not search PATH; argv[0] is often just "kinglet" when invoked via PATH.
std::string resolve_self_executable(const char *argv0) {
  const fs::path from_argv(argv0);
  if (from_argv.is_absolute() && fs::exists(from_argv)) {
    return from_argv.string();
  }

#if defined(__linux__)
  char buf[4096];
  const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n > 0) {
    buf[n] = '\0';
    return std::string(buf);
  }
#elif defined(__APPLE__)
  char buf[4096];
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) == 0) {
    char resolved[PATH_MAX];
    if (::realpath(buf, resolved) != nullptr) {
      return std::string(resolved);
    }
    return std::string(buf);
  }
#endif

  if (const char *path_env = std::getenv("PATH")) {
    const std::string name = from_argv.filename().string();
    std::string_view rest(path_env);
    while (!rest.empty()) {
      const auto sep = rest.find(':');
      const std::string_view entry = rest.substr(0, sep);
      if (!entry.empty()) {
        const fs::path candidate = fs::path(entry) / name;
        std::error_code ec;
        if (fs::exists(candidate, ec)) {
          return fs::absolute(candidate).string();
        }
      }
      if (sep == std::string_view::npos) {
        break;
      }
      rest = rest.substr(sep + 1);
    }
  }

  return from_argv.string();
}

int spawn_reexec(const std::string &self_executable, const std::vector<std::string> &args) {
#if defined(_WIN32)
  (void)self_executable;
  (void)args;
  std::cerr << g_prog << ": subcommand re-exec is not supported on Windows\n";
  return 78;
#else
  std::vector<char *> exec_argv;
  exec_argv.push_back(const_cast<char *>(self_executable.c_str()));
  for (const std::string &arg : args) {
    exec_argv.push_back(const_cast<char *>(arg.c_str()));
  }
  exec_argv.push_back(nullptr);
  execv(self_executable.c_str(), exec_argv.data());
  std::cerr << g_prog << ": failed to re-exec " << self_executable << ": " << std::strerror(errno)
            << '\n';
  return 71;
#endif
}

// Like spawn_reexec, but waits for the child instead of replacing this process,
// so the caller can print a completion line. Returns the child exit code.
int spawn_and_wait(const std::string &self_executable, const std::vector<std::string> &args) {
#if defined(_WIN32)
  (void)self_executable;
  (void)args;
  print_error("build", "building is not supported on Windows yet");
  return 78;
#else
  std::vector<char *> exec_argv;
  exec_argv.push_back(const_cast<char *>(self_executable.c_str()));
  for (const std::string &arg : args) {
    exec_argv.push_back(const_cast<char *>(arg.c_str()));
  }
  exec_argv.push_back(nullptr);

  const pid_t pid = fork();
  if (pid < 0) {
    print_error("build", std::string("fork failed: ") + std::strerror(errno));
    return 71;
  }
  if (pid == 0) {
    execv(self_executable.c_str(), exec_argv.data());
    std::cerr << g_prog << " build: failed to exec " << self_executable << ": "
              << std::strerror(errno) << '\n';
    _exit(71);
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno == EINTR) {
      continue;
    }
    return 71;
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status);
  }
  return 71;
#endif
}

int cmd_init(int argc, char **argv) {
  // The target may be a path (e.g. "path/to/app" or an absolute path); the
  // project name is taken from its last segment.
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
  std::cout << p.green("\u2713") << "  Created project " << p.bold(project_name) << "\n\n";
  std::cout << "   " << p.dim(display_path(dir) + "/") << "\n";
  std::cout << "   " << p.cyan("kinglet.nest") << "\n";
  std::cout << "   " << p.cyan("src/main.kl") << "\n\n";
  std::cout << p.bold("   Next steps") << "\n";
  std::cout << p.dim("     cd " + display_path(dir)) << "\n";
  std::cout << p.dim("     " + g_prog + " build") << "\n";
  return 0;
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

  const std::string start = root_arg.empty() ? fs::current_path().string() : fs::absolute(root_arg).string();
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
    std::cerr << p.dim("\u203a") << "  Building " << p.bold(out_name) << "  "
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
      std::cerr << p.green("\u2713") << "  Built " << p.bold(out_name) << "  "
                << p.dim("\u2192 " + display_path(out_path)) << "  " << p.dim(elapsed) << "\n";
    } else {
      print_error("build", "build failed (exit " + std::to_string(rc) + ")");
    }
  }
  return rc;
}

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
  const std::string out_name = default_output_name(*config);
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

std::set<std::string> collect_referenced_object_ids(const fs::path &stamps_dir) {
  std::set<std::string> referenced;
  std::error_code ec;
  if (!fs::exists(stamps_dir, ec)) {
    return referenced;
  }
  for (const fs::directory_entry &entry : fs::directory_iterator(stamps_dir, ec)) {
    if (ec || !entry.is_regular_file()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (!name.ends_with(".object")) {
      continue;
    }
    std::ifstream in(entry.path());
    std::string object_id;
    in >> object_id;
    object_id = trim_copy(object_id);
    if (!object_id.empty()) {
      referenced.insert(object_id);
    }
  }
  return referenced;
}

int remove_path(const fs::path &path, bool dry_run) {
  if (!fs::exists(path)) {
    return 0;
  }
  if (dry_run) {
    std::cout << "would remove " << path << '\n';
    return 1;
  }
  std::error_code ec;
  fs::remove(path, ec);
  if (ec) {
    std::cerr << "kinglet prune: cannot remove " << path << ": " << ec.message() << '\n';
    return 0;
  }
  return 1;
}

int prune_unreferenced_objects(const fs::path &objects_dir, bool dry_run) {
  std::error_code ec;
  if (!fs::exists(objects_dir, ec)) {
    return 0;
  }

  const std::set<std::string> referenced =
      collect_referenced_object_ids(objects_dir.parent_path() / "stamps");

  int removed = 0;
  for (const fs::directory_entry &entry : fs::directory_iterator(objects_dir, ec)) {
    if (ec) {
      break;
    }
    if (entry.is_directory()) {
      continue;
    }
    const fs::path path = entry.path();
    const std::string name = path.filename().string();
    if (name.ends_with(".meta")) {
      const std::string object_id = name.substr(0, name.size() - 5);
      if (referenced.count(object_id) > 0) {
        continue;
      }
      removed += remove_path(path, dry_run);
      removed += remove_path(objects_dir / object_id, dry_run);
      continue;
    }
    if (referenced.count(name) > 0) {
      continue;
    }
    if (fs::exists(objects_dir / (name + ".meta"))) {
      continue;
    }
    removed += remove_path(path, dry_run);
  }
  return removed;
}

int cmd_prune(int argc, char **argv) {
  bool all = false;
  bool dry_run = false;
  std::string root_arg;
  for (int i = 2; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--all") {
      all = true;
      continue;
    }
    if (arg == "--dry-run" || arg == "-n") {
      dry_run = true;
      continue;
    }
    if (arg.starts_with('-')) {
      print_error("prune", std::string("unknown option ") + std::string(arg));
      return 64;
    }
    root_arg = std::string(arg);
    break;
  }

  const std::string start = root_arg.empty() ? fs::current_path().string() : fs::absolute(root_arg).string();
  const auto config = find_project_config(start);
  if (!config) {
    print_error("prune", "no kinglet.nest found");
    return 2;
  }

  const fs::path kinglet_dir = fs::path(config->root_dir) / ".kinglet";
  if (!fs::exists(kinglet_dir)) {
    if (!dry_run) {
      std::cout << ui::g_out.dim("\u2022") << "  Nothing to do (" << display_path(kinglet_dir)
                << " missing)\n";
    }
    return 0;
  }

  if (all) {
    if (dry_run) {
      std::cout << ui::g_out.dim("\u2022") << "  Would remove " << display_path(kinglet_dir) << "\n";
      return 0;
    }
    std::error_code ec;
    const auto removed = fs::remove_all(kinglet_dir, ec);
    if (ec) {
      print_error("prune", "cannot remove " + kinglet_dir.string() + ": " + ec.message());
      return 74;
    }
    std::cout << ui::g_out.green("\u2713") << "  Removed " << ui::g_out.bold(display_path(kinglet_dir))
              << "  " << ui::g_out.dim("(" + std::to_string(removed) + " entries)") << "\n";
    return 0;
  }

  const ui::Painter &p = ui::g_out;
  const int removed = prune_unreferenced_objects(kinglet_dir / "objects", dry_run);
  if (dry_run) {
    std::cout << p.dim("\u2022") << "  " << removed << " path(s) would be removed\n";
  } else if (removed > 0) {
    std::cout << p.green("\u2713") << "  Pruned " << removed << " unreferenced object path(s)\n";
  } else {
    std::cout << p.dim("\u2022") << "  No unreferenced objects\n";
  }
  return 0;
}

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
    std::cout << p.green("\u2713") << "  Formatted " << changed_count << " file(s)\n";
  }
  return 0;
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
