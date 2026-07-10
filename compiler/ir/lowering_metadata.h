// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <vector>

namespace kinglet {

struct FunctionInfo {
  std::string name;
  std::string mangled_name;
  std::size_t entry = 0;
  int param_count = 0;
};

struct StructMeta {
  std::string name;
  std::vector<std::string> field_names;
};

struct EnumMeta {
  std::string name;
  std::vector<std::string> variants;
  std::vector<int> variant_param_counts;
};

} // namespace kinglet
