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
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
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

#if defined(_WIN32)
namespace {

// Quote one argv element for a Windows command line, following the Microsoft
// CRT rules so CreateProcess reproduces the intended argv in the child.
std::string windows_quote_arg(const std::string &s) {
  const bool need_quotes = s.empty() || s.find_first_of(" \t\n\"") != std::string::npos;
  if (!need_quotes) {
    return s;
  }
  std::string out = "\"";
  size_t backslashes = 0;
  for (char c : s) {
    if (c == '\\') {
      ++backslashes;
      out += '\\';
    } else if (c == '"') {
      out += std::string(backslashes + 1, '\\');
      out += '"';
      backslashes = 0;
    } else {
      out += c;
      backslashes = 0;
    }
  }
  out += std::string(backslashes, '\\');
  out += '"';
  return out;
}

std::wstring utf8_to_wide(const std::string &s) {
  if (s.empty()) {
    return L"";
  }
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) {
    return L"";
  }
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string wide_to_utf8(const wchar_t *w) {
  if (w == nullptr || *w == L'\0') {
    return "";
  }
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1) {
    return "";
  }
  std::string s(static_cast<size_t>(n - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}

} // namespace
#endif

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
#elif defined(_WIN32)
  wchar_t wbuf[MAX_PATH];
  const DWORD wlen = ::GetModuleFileNameW(nullptr, wbuf, MAX_PATH);
  if (wlen > 0 && wlen < MAX_PATH) {
    return wide_to_utf8(wbuf);
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
  return run_process_wait(self_executable, args);
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
  return run_process_wait(self_executable, args);
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

#if defined(_WIN32)
int run_process_wait(const std::string &program, const std::vector<std::string> &args) {
  std::string cmdline = windows_quote_arg(program);
  for (const std::string &arg : args) {
    cmdline += " ";
    cmdline += windows_quote_arg(arg);
  }

  const std::wstring wprogram = utf8_to_wide(program);
  std::wstring wcmdline = utf8_to_wide(cmdline);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  // lpApplicationName pins the exact executable (survives spaces / no PATH
  // search); lpCommandLine carries argv[0] + the rest for the child.
  if (!CreateProcessW(wprogram.c_str(), wcmdline.data(), nullptr, nullptr, TRUE, 0, nullptr,
                      nullptr, &si, &pi)) {
    std::cerr << g_prog << ": failed to run " << program << ": error " << ::GetLastError() << '\n';
    return 71;
  }
  ::WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 0;
  ::GetExitCodeProcess(pi.hProcess, &code);
  ::CloseHandle(pi.hProcess);
  ::CloseHandle(pi.hThread);
  return static_cast<int>(code);
}
#endif

} // namespace kinglet
