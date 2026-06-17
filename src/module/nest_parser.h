// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "module/project_config.h"

#include <filesystem>
#include <optional>
#include <string>

namespace kinglet {

bool parse_nest_manifest(const std::string &content, ProjectConfig &config);
std::optional<ProjectConfig> load_nest_config_file(const std::filesystem::path &manifest_path);

} // namespace kinglet
