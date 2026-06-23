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

} // namespace kinglet
