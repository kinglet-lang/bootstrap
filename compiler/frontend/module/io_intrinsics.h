// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <vector>

namespace kinglet::io_intrinsics {

// Kinglet return type produced by a member call. Kept intentionally tiny
// (not a dependency on frontend/types::Type) so this header stays usable
// from both the type checker and, eventually, the LSP completion resolver
// without pulling in the full type system.
enum class ReturnKind {
  Void,
  String,
};

// How call_expr arguments are validated for a member.
enum class ArgMode {
  // First argument must be a string literal fmt-string; remaining args are
  // type-checked and their count must match the fmt-string's placeholders
  // (see TypeChecker::check_fmt_args). Used by `line`.
  FmtArgs,
  // No arguments accepted; a diagnostic is raised if any are present. Used
  // by `flush`.
  NoArgs,
  // Arguments are individually type-checked but neither their count nor
  // shape is otherwise validated. Used by `secret`.
  Unchecked,
};

// Describes one callable member exposed on a built-in io stream object
// (`io::out`, `io::err`, `io::in`). The type checker (diagnostics) and the
// bytecode/LLVM compiler (codegen dispatch) both resolve against this same
// table instead of each hand-rolling their own `if (name == "line") ...`
// chain, so registering a new intrinsic here is the single place that
// makes it type-check *and* compile — no second manual sync step.
struct Member {
  std::string name;    // e.g. "line", "flush", "secret"
  std::string detail;  // human-readable one-liner, for diagnostics/tooling
  std::string snippet; // suggested call-site snippet body, e.g. "line($1)"
  ArgMode arg_mode;
  ReturnKind return_kind;
};

// Members valid on `io::out` / `io::err` (the "ostream" side).
const std::vector<Member> &ostream_members();

// Members valid on `io::in` (the "istream" side).
const std::vector<Member> &istream_members();

// Convenience lookups; returns nullptr when `name` is not a registered
// member of that stream side.
const Member *find_ostream_member(const std::string &name);
const Member *find_istream_member(const std::string &name);

} // namespace kinglet::io_intrinsics
