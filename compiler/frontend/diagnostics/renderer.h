// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT
//
// Rendering of Diagnostic values into human-readable stderr text.
//
// The human-readable renderer prints stable error codes, source snippets,
// primary carets, and secondary labels. It falls back to a compact single-line
// layout when source text is unavailable.
//
// The renderer is deliberately self-contained: it owns its ANSI constants
// and its own tty/NO_COLOR detection, so `frontend/diagnostics` does not
// depend on `driver/kinglet/cli_ui`. This lets LSP, self-host, and any other
// consumer render diagnostics without pulling in the CLI.

#pragma once

#include "frontend/diagnostics/diagnostic.h"
#include "frontend/diagnostics/source_map.h"

#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace kinglet::diag {

enum class ColorMode {
  Auto,   // color if stream is a tty and NO_COLOR is unset and TERM != "dumb"
  Always, // force ANSI escapes
  Never,  // no ANSI escapes
};

struct RenderOptions {
  ColorMode color = ColorMode::Auto;
  // Human-readable path prepended to every diagnostic line. Empty means the
  // renderer omits the "path:" prefix.
  std::string source_path;
  // When non-null, the renderer draws rustc-style snippets under primary
  // labels whose file matches `source_path` and whose line is in-range. Left
  // null (or missing entry) means the renderer emits the single-line
  // fallback layout that Phase 1 shipped.
  const SourceMap *sources = nullptr;
};

// Decide once whether the given C stream (stderr / stdout) should receive
// ANSI escapes under the given mode. Exposed so a driver can precompute the
// effective mode and stick with it across multiple diagnostics.
bool color_enabled_for_stream(int fd, ColorMode mode);

// Render one diagnostic to `out`. Trailing newline is emitted. If `use_color`
// is true the renderer inserts ANSI escapes regardless of stream identity;
// callers that want tty auto-detection should pass the result of
// `color_enabled_for_stream(fileno(stream), opts.color)`.
void render(std::ostream &out, const Diagnostic &diag, const RenderOptions &opts, bool use_color);

// Batch helper: renders every diagnostic in order, sharing the same options
// and pre-resolved color decision.
void render_all(std::ostream &out, const std::vector<Diagnostic> &diags, const RenderOptions &opts,
                bool use_color);

} // namespace kinglet::diag
