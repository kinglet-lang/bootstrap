// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "module/module_id.h"

namespace kinglet {

std::string module_id_to_qualifier(const std::string &module_id) {
  std::string out;
  out.reserve(module_id.size() + 4);
  for (char c : module_id) {
    if (c == '.') {
      out += "::";
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::string qualify_module_symbol(const std::string &module_id, const std::string &symbol) {
  return module_id_to_qualifier(module_id) + "::" + symbol;
}

} // namespace kinglet
