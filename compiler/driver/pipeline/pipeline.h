// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

namespace kinglet {

class TypeChecker;
struct KirModule;

// Bring a freshly-compiled KirModule to a codegen-ready state by running the
// KIR post-passes in order: attach typed function signatures sourced from the
// type checker, infer per-instruction result types, then specialize generic
// arithmetic opcodes to width-specific variants. This is the single entry
// point for the sequence every native build must run after compilation.
void prepare_kir(KirModule &kir, const TypeChecker &checker);

} // namespace kinglet
