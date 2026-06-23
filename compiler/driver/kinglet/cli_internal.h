// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

// Internal header shared by cli_driver.cc and cmd_*.cc / cli_*.cc.
// Not part of the public API.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace kinglet {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Terminal color painter (initialized once in run_cli_subcommand).
// ---------------------------------------------------------------------------
namespace ui {

struct Painter {
  bool on = false;

  std::string wrap(const char *code, std::string_view text) const;
  std::string bold(std::string_view t) const;
  std::string dim(std::string_view t) const;
  std::string red(std::string_view t) const;
  std::string green(std::string_view t) const;
  std::string yellow(std::string_view t) const;
  std::string cyan(std::string_view t) const;
};

extern Painter g_out;
extern Painter g_err;

void init();

} // namespace ui

// Invoked program name (kinglet or its klet alias).
extern std::string g_prog;

// Path relative to CWD when possible, for readable output.
std::string display_path(const fs::path &p);

// Consistent "✗ prog subcommand: message" error line.
void print_error(std::string_view subcommand, std::string_view message);

// Ensure a directory exists; returns false on failure.
bool ensure_dir(const fs::path &path);

// Strip leading/trailing whitespace.
std::string trim_copy(std::string s);

} // namespace kinglet
