// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace kinglet {

std::string resolve_self_executable(const char *argv0);
int spawn_reexec(const std::string &self_executable, const std::vector<std::string> &args);
int spawn_and_wait(const std::string &self_executable, const std::vector<std::string> &args);

#if defined(_WIN32)
// Run `program` with `args` (as argv[1..]), inheriting stdio, wait for it to
// finish, and return its exit code. Windows-only: POSIX uses execv/fork inline
// at the call sites. On CreateProcess failure prints an error and returns 71.
int run_process_wait(const std::string &program, const std::vector<std::string> &args);
#endif

} // namespace kinglet
