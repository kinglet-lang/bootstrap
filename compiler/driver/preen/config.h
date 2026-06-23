// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "frontend/module/project_config.h"

#include <string>
#include <string_view>
#include <vector>

namespace kinglet::preen {

enum class NewlineStyle {
  Lf,
  Crlf,
};

struct ExtensionSpec {
  std::string name;
  bool enabled = true;
};

struct FmtConfig {
  int indent = 2;
  int max_width = 100;
  NewlineStyle newline = NewlineStyle::Lf;
  bool trailing_comma = false;
  std::vector<std::string> extensions;

  static FmtConfig defaults();
  static FmtConfig merge(const FmtConfig &base, const FmtConfig &overlay);

  std::string newline_string() const;
  bool extension_enabled(std::string_view name) const;
};

FmtConfig fmt_config_from_project(const kinglet::ProjectConfig &project);

} // namespace kinglet::preen
