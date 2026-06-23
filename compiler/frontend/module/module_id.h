// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include <string>

namespace kinglet {

// "compiler.ast" -> "compiler::ast"
std::string module_id_to_qualifier(const std::string &module_id);

std::string qualify_module_symbol(const std::string &module_id, const std::string &symbol);

} // namespace kinglet
