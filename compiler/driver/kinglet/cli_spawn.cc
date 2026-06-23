// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "driver/kinglet/cli_spawn.h"

#include "driver/kinglet/cli_internal.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#if defined(_WIN32)
#include <io.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <stdlib.h>
#elif defined(__linux__)
#include <unistd.h>
#endif
#if !defined(_WIN32)
#include <climits>
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace kinglet {

namespace fs = std::filesystem;

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

} // namespace kinglet
