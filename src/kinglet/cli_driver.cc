#include "kinglet/cli_driver.h"

#include "module/project_config.h"

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
#endif
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

void print_cli_help(std::ostream &out) {
  out << "Kinglet " << KINGLET_VERSION << "\n"
      << "\n"
      << "Usage:\n"
      << "  kinglet <file.kl> [args...]     Compile and run (default)\n"
      << "  kinglet init [name]             New project dir (prompts; default kinglet-app)\n"
      << "  kinglet build [--backend native|vm] [--quiet] [project_root]\n"
      << "  kinglet run [<file.kl> | built binary args...]\n"
      << "  kinglet prune [--all] [-n] [project_root]\n"
      << "                                  Prune unreferenced Klos objects (or --all)\n"
      << "  kinglet -h, --help              Show this help\n"
      << "  kinglet -v, --version           Show version\n"
      << "\n"
      << "Developer / compiler modes (flags):\n"
      << "  --check, --tokens, --ast, --bytecode, --ir\n"
      << "  --save-bytecode <out.kbc>, --run <program.kbc>\n"
      << "  --native <out> | --backend native -o <out>  (requires LLVM build)\n"
      << "  --repl\n"
      << "\n"
      << "Shadow parity (kinglet-lang/kinglet repo): ./kinglet prove\n";
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

std::string prompt_project_name() {
  std::cout << "Project name [kinglet-app]: " << std::flush;
  std::string line;
  if (!std::getline(std::cin, line)) {
    return "kinglet-app";
  }
  line = trim_copy(line);
  return line.empty() ? "kinglet-app" : line;
}

std::string resolve_init_project_name(int argc, char **argv) {
  if (argc >= 3) {
    return std::string(argv[2]);
  }
  if (stdin_is_interactive()) {
    return prompt_project_name();
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
  std::cerr << "kinglet: subcommand re-exec is not supported on Windows\n";
  return 78;
#else
  std::vector<char *> exec_argv;
  exec_argv.push_back(const_cast<char *>(self_executable.c_str()));
  for (const std::string &arg : args) {
    exec_argv.push_back(const_cast<char *>(arg.c_str()));
  }
  exec_argv.push_back(nullptr);
  execv(self_executable.c_str(), exec_argv.data());
  std::cerr << "kinglet: failed to re-exec " << self_executable << ": " << std::strerror(errno)
            << '\n';
  return 71;
#endif
}

int cmd_init(int argc, char **argv) {
  const std::string project_name = resolve_init_project_name(argc, argv);
  if (!is_valid_project_name(project_name)) {
    std::cerr << "kinglet init: invalid project name '" << project_name << "'\n";
    return 64;
  }

  const fs::path dir = fs::absolute(fs::current_path() / project_name);
  const fs::path manifest = dir / "kinglet.toml";
  if (fs::exists(manifest)) {
    std::cerr << "kinglet init: " << manifest << " already exists\n";
    return 64;
  }
  if (!ensure_dir(dir)) {
    std::cerr << "kinglet init: cannot create " << dir << '\n';
    return 74;
  }
  {
    std::ofstream out(manifest);
    if (!out) {
      std::cerr << "kinglet init: cannot write " << manifest << '\n';
      return 74;
    }
    out << "[project]\n"
        << "name = \"" << toml_escape(project_name) << "\"\n"
        << "version = \"0.1.0\"\n"
        << "\n"
        << "[build]\n"
        << "root = \"src/main.kl\"\n"
        << "cache_dir = \".kinglet/cache\"\n"
        << "out_dir = \".kinglet/out\"\n"
        << "\n"
        << "[build.compiler]\n"
        << "engine = \"ref\"\n"
        << "default_backend = \"native\"\n";
  }
  const fs::path src_dir = dir / "src";
  const fs::path main_kl = src_dir / "main.kl";
  if (!ensure_dir(src_dir)) {
    std::cerr << "kinglet init: cannot create " << src_dir << '\n';
    return 74;
  }
  if (!fs::exists(main_kl)) {
    std::ofstream out(main_kl);
    if (!out) {
      std::cerr << "kinglet init: cannot write " << main_kl << '\n';
      return 74;
    }
    out << "int main() {\n"
        << "  return 0;\n"
        << "}\n";
  }
  std::cout << "Created project '" << project_name << "' at " << dir << '\n';
  std::cout << "  " << manifest.filename().string() << '\n';
  std::cout << "  src/main.kl\n";
  std::cout << "\nNext: cd " << project_name << " && kinglet build\n";
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
      if (i + 1 >= argc) {
        std::cerr << "kinglet build: --backend requires native or vm\n";
        return 64;
      }
      backend = argv[++i];
      continue;
    }
    if (arg.starts_with('-')) {
      std::cerr << "kinglet build: unknown option " << arg << '\n';
      return 64;
    }
    root_arg = std::string(arg);
    break;
  }

  const std::string start = root_arg.empty() ? fs::current_path().string() : fs::absolute(root_arg).string();
  const auto config = find_project_config(start);
  if (!config) {
    std::cerr << "kinglet build: no kinglet.toml found (run kinglet init)\n";
    return 2;
  }
  if (backend.empty()) {
    backend = config->default_backend;
  }
  if (backend != "native" && backend != "vm") {
    std::cerr << "kinglet build: unsupported backend '" << backend << "'\n";
    return 64;
  }

  const fs::path entry = fs::path(config->root_dir) / config->build_root;
  if (!fs::exists(entry)) {
    std::cerr << "kinglet build: entry not found: " << entry << '\n';
    return 2;
  }

  const fs::path out_dir = fs::path(config->root_dir) / config->out_dir;
  ensure_dir(out_dir);
  ensure_dir(fs::path(config->root_dir) / ".kinglet/objects");

  const std::string out_name = default_output_name(*config);
  const fs::path out_path =
      backend == "native" ? out_dir / out_name : out_dir / (out_name + ".kbc");

  if (!quiet) {
    std::cerr << "kinglet build: " << backend << " → " << out_path << " from " << entry << '\n';
  }

  std::vector<std::string> args;
  if (backend == "native") {
#ifndef KINGLET_HAVE_LLVM
    std::cerr << "kinglet build: native backend not available (rebuild with enable_llvm=true)\n";
    return 78;
#else
    const fs::path obj_cache = fs::path(config->root_dir) / ".kinglet/objects/native";
    ensure_dir(obj_cache);
    args = {"--backend", "native", "-o", out_path.string(), "--obj-cache", obj_cache.string(),
            entry.string()};
#endif
  } else {
    args = {"--save-bytecode", out_path.string(), entry.string()};
  }
  if (args.empty()) {
    return 78;
  }
  const int rc = spawn_reexec(self_executable, args);
  if (rc == 0 && !quiet) {
    std::cout << out_path.string() << '\n';
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
    std::cerr << "kinglet run: no kinglet.toml; use kinglet <file.kl> or kinglet init\n";
    return 2;
  }

  const fs::path out_dir = fs::path(config->root_dir) / config->out_dir;
  const std::string out_name = default_output_name(*config);
  fs::path native_bin = out_dir / out_name;
  fs::path kbc = out_dir / (out_name + ".kbc");

  if (fs::exists(native_bin)) {
#if defined(_WIN32)
    std::cerr << "kinglet run: executing built binaries is not supported on Windows\n";
    return 78;
#else
    std::vector<char *> exec_argv;
    exec_argv.push_back(const_cast<char *>(native_bin.c_str()));
    for (int i = 2; i < argc; ++i) {
      exec_argv.push_back(argv[i]);
    }
    exec_argv.push_back(nullptr);
    execv(native_bin.c_str(), exec_argv.data());
    std::cerr << "kinglet run: exec failed for " << native_bin << '\n';
    return 71;
#endif
  }
  if (fs::exists(kbc)) {
    std::vector<std::string> args = {"--run", kbc.string()};
    for (int i = 2; i < argc; ++i) {
      args.push_back(argv[i]);
    }
    return spawn_reexec(self_executable, args);
  }

  std::cerr << "kinglet run: no build output in " << out_dir << " (run kinglet build)\n";
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
      std::cerr << "kinglet prune: unknown option " << arg << '\n';
      return 64;
    }
    root_arg = std::string(arg);
    break;
  }

  const std::string start = root_arg.empty() ? fs::current_path().string() : fs::absolute(root_arg).string();
  const auto config = find_project_config(start);
  if (!config) {
    std::cerr << "kinglet prune: no kinglet.toml found\n";
    return 2;
  }

  const fs::path kinglet_dir = fs::path(config->root_dir) / ".kinglet";
  if (!fs::exists(kinglet_dir)) {
    if (!dry_run) {
      std::cout << "kinglet prune: nothing to do (" << kinglet_dir << " missing)\n";
    }
    return 0;
  }

  if (all) {
    if (dry_run) {
      std::cout << "would remove " << kinglet_dir << '\n';
      return 0;
    }
    std::error_code ec;
    const auto removed = fs::remove_all(kinglet_dir, ec);
    if (ec) {
      std::cerr << "kinglet prune: cannot remove " << kinglet_dir << ": " << ec.message() << '\n';
      return 74;
    }
    std::cout << "kinglet prune: removed " << kinglet_dir << " (" << removed << " entries)\n";
    return 0;
  }

  const int removed = prune_unreferenced_objects(kinglet_dir / "objects", dry_run);
  if (dry_run) {
    std::cout << "kinglet prune: " << removed << " path(s) would be removed\n";
  } else if (removed > 0) {
    std::cout << "kinglet prune: removed " << removed << " unreferenced object path(s)\n";
  } else {
    std::cout << "kinglet prune: no unreferenced objects\n";
  }
  return 0;
}

} // namespace

int run_cli_subcommand(int argc, char **argv) {
  if (argc < 2) {
    return -1;
  }
  const std::string_view cmd(argv[1]);
  if (cmd == "-h" || cmd == "--help") {
    print_cli_help(std::cout);
    return 0;
  }
  if (cmd == "-v" || cmd == "--version") {
    std::cout << "kinglet " << KINGLET_VERSION << '\n';
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
  if (cmd == "help") {
    print_cli_help(std::cout);
    return 0;
  }
  return -1;
}

} // namespace kinglet
