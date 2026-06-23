// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

// Internal helpers for LLVM module cache operations.
// Not part of the public codegen interface.

#pragma once

#include "ir/kir.h"

#include <string>

namespace kinglet {

struct NativeCompileOptions;

void copy_kir_metadata(KirModule *dst, const KirModule &src);
int first_instr_line(const KirFunction &fn);
std::string temp_object_path(const std::string &tag, const std::string &out_path);
std::string shard_fingerprint_text(const KirModule &shard, const KirModule &full,
                                   const NativeCompileOptions &options);
std::string shard_stamp(const KirModule &shard, const KirModule &full,
                        const NativeCompileOptions &options);

} // namespace kinglet
