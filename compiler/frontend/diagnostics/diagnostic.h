// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT
//
// Unified diagnostic representation shared by lexer/parser/checker/backend.
// Producers attach stable codes to established diagnostic families while
// diagnostics that have not completed semantic review may leave `code` empty.

#pragma once

#include "frontend/ast/ast.h"

#include <cstdint>
#include <string>
#include <vector>

namespace kinglet {

enum class Severity : std::uint8_t {
  Error,
  Warning,
  Note,
  Help,
};

// One span in the source. The renderer resolves the file path from the
// enclosing `Diagnostic` (ADR 0031 D8); multi-file diagnostics are not yet
// modelled — a `SourceSpan` carries no file identity of its own.
struct SourceSpan {
  int line = 0;
  int column = 0;
  int length = 1;
};

inline SourceSpan span_from(ast::SourceLocation loc) {
  return SourceSpan{loc.line, loc.column, loc.length};
}

// A labeled span attached to a diagnostic. The first label in
// `Diagnostic::labels` is the primary span; subsequent labels are secondary
// (ADR 0031 D4). Labels may carry an empty message.
struct DiagnosticLabel {
  SourceSpan span;
  std::string message;
};

// A suggested source replacement. Fix-its are only produced when the fix is
// semantically reliable (ADR 0031 D5); otherwise use a Help-severity note.
struct FixIt {
  SourceSpan span;
  std::string replacement;
};

struct Diagnostic {
  // Stable error code, e.g. "K4001". The renderer omits the "[Kxxxx]" tag when
  // this is empty for a diagnostic that has not completed semantic review.
  std::string code;
  Severity severity = Severity::Error;
  std::string message;
  std::vector<DiagnosticLabel> labels;
  std::vector<FixIt> fixes;
};

// Convenience: single-span error/warning constructor used at migration
// call sites where no secondary label or fix-it is available yet.
inline Diagnostic make_diagnostic(Severity severity, ast::SourceLocation loc, std::string message) {
  Diagnostic d;
  d.severity = severity;
  d.message = std::move(message);
  d.labels.push_back(DiagnosticLabel{span_from(loc), std::string{}});
  return d;
}

inline Diagnostic make_diagnostic(Severity severity, SourceSpan span, std::string message) {
  Diagnostic d;
  d.severity = severity;
  d.message = std::move(message);
  d.labels.push_back(DiagnosticLabel{span, std::string{}});
  return d;
}

// Same, with an explicit K-code. Prefer this at migrated call sites so the
// stable identifier lands in `error[Kxxxx]: …` output and can be referenced
// by `-Wno-Kxxxx` or LSP.
inline Diagnostic make_diagnostic(Severity severity, ast::SourceLocation loc, std::string code,
                                  std::string message) {
  Diagnostic d;
  d.severity = severity;
  d.code = std::move(code);
  d.message = std::move(message);
  d.labels.push_back(DiagnosticLabel{span_from(loc), std::string{}});
  return d;
}

} // namespace kinglet
