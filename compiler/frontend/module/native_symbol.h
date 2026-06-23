// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include <string>

namespace kinglet {

// Strip a module/type qualifier prefix for native link names.
// "import_add_lib::add" -> "add", "main" -> "main".
std::string native_link_bare_name(const std::string &lookup_name);

// Stable LLVM / native linker symbol for a Kinglet function.
std::string mangled_native_symbol(const std::string &lookup_name, const std::string &source_path);

} // namespace kinglet
