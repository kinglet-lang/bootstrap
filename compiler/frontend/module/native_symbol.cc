// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "frontend/module/native_symbol.h"

#include <filesystem>

namespace kinglet {

std::string native_link_bare_name(const std::string &lookup_name) {
  const auto pos = lookup_name.rfind("::");
  if (pos == std::string::npos) {
    return lookup_name;
  }
  return lookup_name.substr(pos + 2);
}

std::string mangled_native_symbol(const std::string &lookup_name, const std::string &source_path) {
  const std::string bare = native_link_bare_name(lookup_name);
  if (bare == "main") {
    return "kinglet_user_main";
  }
  if (!source_path.empty()) {
    return "kinglet_fn_" + std::filesystem::path(source_path).stem().string() + "_" + bare;
  }
  return "kinglet_fn_" + bare;
}

} // namespace kinglet
