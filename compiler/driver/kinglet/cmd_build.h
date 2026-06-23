// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include <string>

namespace kinglet {

struct ProjectConfig;

std::string build_output_name(const ProjectConfig &config);
int cmd_build(int argc, char **argv, const std::string &self_executable);

} // namespace kinglet
