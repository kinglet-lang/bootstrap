// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "driver/kinglet/cli_internal.h"

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

namespace kinglet {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Terminal styling
// ---------------------------------------------------------------------------
namespace ui {

constexpr const char *kReset = "\033[0m";
constexpr const char *kBold = "\033[1m";
constexpr const char *kDim = "\033[2m";
constexpr const char *kRed = "\033[31m";
constexpr const char *kGreen = "\033[32m";
constexpr const char *kYellow = "\033[33m";
constexpr const char *kCyan = "\033[36m";

static bool fd_is_tty(int fd) {
#if defined(_WIN32)
  return _isatty(fd) != 0;
#else
  return isatty(fd) != 0;
#endif
}

static bool stream_supports_color(int fd) {
  if (std::getenv("NO_COLOR") != nullptr) {
    return false;
  }
  const char *term = std::getenv("TERM");
  if (term != nullptr && std::string_view(term) == "dumb") {
    return false;
  }
#if defined(_WIN32)
  if (std::getenv("WT_SESSION") == nullptr && term == nullptr) {
    return false;
  }
#endif
  return fd_is_tty(fd);
}

std::string Painter::wrap(const char *code, std::string_view text) const {
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

std::string Painter::bold(std::string_view t) const { return wrap(kBold, t); }
std::string Painter::dim(std::string_view t) const { return wrap(kDim, t); }
std::string Painter::red(std::string_view t) const { return wrap(kRed, t); }
std::string Painter::green(std::string_view t) const { return wrap(kGreen, t); }
std::string Painter::yellow(std::string_view t) const { return wrap(kYellow, t); }
std::string Painter::cyan(std::string_view t) const { return wrap(kCyan, t); }

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

std::string g_prog = "kinglet";

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

void print_error(std::string_view subcommand, std::string_view message) {
  const std::string label =
      subcommand.empty() ? g_prog : (g_prog + " " + std::string(subcommand));
  std::cerr << ui::g_err.red("✗") << " " << ui::g_err.bold(label) << ": " << message << '\n';
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

} // namespace kinglet
