// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "ir/kir.h"

#include <llvm/IR/Module.h>

#include <string>
#include <vector>

namespace kinglet {

bool emit_object(llvm::Module &module, const std::string &obj_path, std::string *error);

bool link_objects(const std::vector<std::string> &obj_paths, const std::string &rt_lib_path,
                  const std::string &out_path, std::string *error);

bool lower_user_functions(llvm::Module *module, const KirModule &functions,
                          const KirModule &metadata, bool debug_info, std::string *error);

bool lower_entry_shim(llvm::Module *module, std::string *error);

} // namespace kinglet
