// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "driver/preen/config.h"

#include <algorithm>

namespace kinglet::preen {

FmtConfig FmtConfig::defaults() {
  return FmtConfig{};
}

FmtConfig FmtConfig::merge(const FmtConfig &base, const FmtConfig &overlay) {
  FmtConfig out = base;
  if (overlay.indent > 0) {
    out.indent = overlay.indent;
  }
  if (overlay.max_width > 0) {
    out.max_width = overlay.max_width;
  }
  out.newline = overlay.newline;
  out.trailing_comma = overlay.trailing_comma;
  if (!overlay.extensions.empty()) {
    out.extensions = overlay.extensions;
  }
  return out;
}

FmtConfig fmt_config_from_project(const kinglet::ProjectConfig &project) {
  FmtConfig config = FmtConfig::defaults();
  if (project.fmt.indent > 0) {
    config.indent = project.fmt.indent;
  }
  if (project.fmt.max_width > 0) {
    config.max_width = project.fmt.max_width;
  }
  if (project.fmt.newline == "crlf") {
    config.newline = NewlineStyle::Crlf;
  } else if (project.fmt.newline == "lf") {
    config.newline = NewlineStyle::Lf;
  }
  if (project.fmt.trailing_comma_set) {
    config.trailing_comma = project.fmt.trailing_comma;
  }
  if (!project.fmt.extensions.empty()) {
    config.extensions = project.fmt.extensions;
  }
  for (const auto &[name, enabled] : project.fmt.extension_entries) {
    if (!enabled) {
      config.extensions.erase(std::remove(config.extensions.begin(), config.extensions.end(), name),
                              config.extensions.end());
      continue;
    }
    if (std::find(config.extensions.begin(), config.extensions.end(), name) ==
        config.extensions.end()) {
      config.extensions.push_back(name);
    }
  }
  return config;
}

std::string FmtConfig::newline_string() const {
  return newline == NewlineStyle::Crlf ? "\r\n" : "\n";
}

bool FmtConfig::extension_enabled(std::string_view name) const {
  return std::find(extensions.begin(), extensions.end(), name) != extensions.end();
}

} // namespace kinglet::preen
