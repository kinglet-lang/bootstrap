// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "driver/preen/config.h"

#include <filesystem>
#include <string>

namespace kinglet::preen {

struct FormatResult {
  std::string text;
  bool changed = false;
  std::string error;
};

FormatResult format_string(std::string_view source, const FmtConfig &config);
FormatResult format_file(const std::filesystem::path &path, const FmtConfig &config);

} // namespace kinglet::preen
