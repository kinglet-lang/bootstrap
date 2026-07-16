// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "frontend/diagnostics/renderer.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ostream>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace kinglet::diag {

namespace {

// ANSI SGR escapes — kept private so nothing outside this file depends on the
// exact codes. Mirrors driver/kinglet/cli_ui.cc's palette to keep the CLI's
// look consistent, but does not share code with it (see renderer.h rationale).
constexpr std::string_view kReset = "\033[0m";
constexpr std::string_view kBold = "\033[1m";
constexpr std::string_view kDim = "\033[2m";
constexpr std::string_view kCyan = "\033[36m";
constexpr std::string_view kBoldRed = "\033[1;31m";
constexpr std::string_view kBoldYellow = "\033[1;33m";
constexpr std::string_view kBoldCyan = "\033[1;36m";
constexpr std::string_view kBoldBlue = "\033[1;34m";

bool fd_is_tty(int fd) {
#if defined(_WIN32)
  return _isatty(fd) != 0;
#else
  return isatty(fd) != 0;
#endif
}

std::string_view severity_label(Severity s) {
  switch (s) {
  case Severity::Error:
    return "error";
  case Severity::Warning:
    return "warning";
  case Severity::Note:
    return "note";
  case Severity::Help:
    return "help";
  }
  return "error";
}

// Primary color for the severity label and the "^^^" underline.
std::string_view severity_color(Severity s) {
  switch (s) {
  case Severity::Error:
    return kBoldRed;
  case Severity::Warning:
    return kBoldYellow;
  case Severity::Note:
    return kBoldCyan;
  case Severity::Help:
    return kBoldCyan;
  }
  return kBoldRed;
}

class Painter {
public:
  explicit Painter(bool on) : on_(on) {}

  bool on() const { return on_; }

  void wrap(std::ostream &out, std::string_view code, std::string_view text) const {
    if (on_) {
      out << code << text << kReset;
    } else {
      out << text;
    }
  }

  void begin(std::ostream &out, std::string_view code) const {
    if (on_)
      out << code;
  }

  void end(std::ostream &out) const {
    if (on_)
      out << kReset;
  }

private:
  bool on_;
};

// Grab one 1-indexed line from the source without allocating an intermediate
// vector of every line. Returns an empty view when `line` is out of range so
// the caller can silently fall back to no-snippet layout.
std::string_view extract_line(const std::string &text, int line_1based) {
  if (line_1based <= 0)
    return {};
  int current = 1;
  std::size_t start = 0;
  while (current < line_1based && start < text.size()) {
    const std::size_t nl = text.find('\n', start);
    if (nl == std::string::npos)
      return {};
    start = nl + 1;
    ++current;
  }
  if (current != line_1based || start > text.size())
    return {};
  std::size_t end = text.find('\n', start);
  if (end == std::string::npos)
    end = text.size();
  // Trim a trailing \r so CRLF sources render cleanly.
  if (end > start && text[end - 1] == '\r')
    --end;
  return std::string_view(text.data() + start, end - start);
}

int decimal_width(int n) {
  if (n < 0)
    n = -n;
  int w = 1;
  while (n >= 10) {
    n /= 10;
    ++w;
  }
  return w;
}

// Render a byte-oriented "column indicator" row: `column-1` spaces (each
// preserved as space or tab so the caret aligns under proportional widths),
// then a `^` for the primary label, then `~` runs for the label's length-1.
// Secondary labels use `-` in yellow. Column and length are byte counts today
// — a proper column-width pass (CJK, combining marks) is a follow-up.
void render_underline(std::ostream &out, std::string_view line, int column_1based, int length,
                      std::string_view caret_color, char caret_head, char caret_tail,
                      const Painter &paint, std::string_view message) {
  if (column_1based < 1)
    column_1based = 1;
  if (length < 1)
    length = 1;

  // Leading pad. Copy tabs verbatim so tab-aligned code lines stay aligned
  // with their carets; other characters become spaces.
  const std::size_t pad_bytes =
      std::min<std::size_t>(static_cast<std::size_t>(column_1based - 1), line.size());
  for (std::size_t i = 0; i < pad_bytes; ++i) {
    out << (line[i] == '\t' ? '\t' : ' ');
  }

  paint.begin(out, caret_color);
  out << caret_head;
  for (int i = 1; i < length; ++i)
    out << caret_tail;
  if (!message.empty()) {
    out << ' ' << message;
  }
  paint.end(out);
  out << '\n';
}

// Emit "  --> path:line:column" pointing at the primary span. Kept separate
// so the header row can be shared between snippet and no-snippet modes.
void render_locus(std::ostream &out, std::string_view path, const SourceSpan &span,
                  const std::string &gutter_pad, const Painter &paint) {
  out << gutter_pad;
  paint.wrap(out, kBoldBlue, "-->");
  out << ' ';
  if (!path.empty()) {
    out << path;
    out << ':';
  }
  out << span.line << ':' << span.column << '\n';
}

// Rich snippet layout — the rustc-style row-per-label picture. Precondition:
// the primary label's line exists in `text`. The caller ensures that; a
// missing line means "fall back to the compact one-line format".
void render_snippet(std::ostream &out, const Diagnostic &diag, const std::string &text,
                    std::string_view path, const Painter &paint) {
  const SourceSpan primary = diag.labels.front().span;

  // "error[Kxxxx]: message" header row.
  paint.begin(out, severity_color(diag.severity));
  out << severity_label(diag.severity);
  if (!diag.code.empty())
    out << '[' << diag.code << ']';
  paint.end(out);
  paint.begin(out, kBold);
  out << ": " << diag.message;
  paint.end(out);
  out << '\n';

  // Gutter width — the widest line number any label references. Includes
  // secondary labels so multi-span diagnostics line up under one gutter.
  int max_line = primary.line;
  for (const DiagnosticLabel &lbl : diag.labels)
    max_line = std::max(max_line, lbl.span.line);
  const int gutter = std::max(decimal_width(max_line), 1);
  const std::string gutter_pad(static_cast<std::size_t>(gutter) + 1, ' ');

  render_locus(out, path, primary, gutter_pad, paint);

  // Blank pipe row above the source line — visually separates the locus
  // from the code, matching rustc.
  out << gutter_pad;
  paint.wrap(out, kBoldBlue, "|");
  out << '\n';

  // Order labels by ascending line number so the reader sees them in the
  // same order they appear in the file. The primary label keeps its role
  // (it stays the one drawn with `^` and the diagnostic's severity color)
  // but we allow it to render below secondary labels when the declaration
  // sits above the use — that's the natural reading order.
  //
  // The primary is `labels[0]` by contract, so we remember its index and
  // sort a permutation of indices instead of moving the labels vector.
  std::vector<std::size_t> order;
  order.reserve(diag.labels.size());
  for (std::size_t i = 0; i < diag.labels.size(); ++i)
    order.push_back(i);
  std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    return diag.labels[a].span.line < diag.labels[b].span.line;
  });

  // For each label in order, emit its "N | code" row and the underline row.
  // The primary label uses the diagnostic's severity color; secondary labels
  // use yellow dashes so they read as annotations. Labels whose line is not
  // in `text` are silently dropped (a future extension will support labels
  // in a different file via DiagnosticLabel::file, ADR 0031 D8). When two
  // consecutive labels sit more than one line apart we drop a `...` divider
  // between them so a long gap does not read as "line N+1 immediately below
  // line N".
  int prev_line = -1;
  for (std::size_t oi = 0; oi < order.size(); ++oi) {
    const std::size_t idx = order[oi];
    const DiagnosticLabel &label = diag.labels[idx];
    const std::string_view line = extract_line(text, label.span.line);
    if (line.data() == nullptr)
      continue;

    if (prev_line >= 0 && label.span.line > prev_line + 1) {
      // "... | " row indicating omitted intermediate lines. Total column of
      // the pipe must line up with the numbered rows' pipes: those write
      // one leading space + `gutter` digits + one space, so we output
      // `gutter+1` dots and then a space to hit the same column.
      out << ' ' << std::string(static_cast<std::size_t>(gutter), '.') << ' ';
      paint.wrap(out, kBoldBlue, "|");
      out << '\n';
    }

    // "  N | source"
    paint.begin(out, kBoldBlue);
    const std::string lineno = std::to_string(label.span.line);
    out << ' '
        << std::string(static_cast<std::size_t>(gutter - decimal_width(label.span.line)), ' ')
        << lineno << " |";
    paint.end(out);
    out << ' ' << line << '\n';

    // "  | ^~~~ message"
    out << gutter_pad;
    paint.wrap(out, kBoldBlue, "|");
    out << ' ';
    const bool is_primary = (idx == 0);
    const std::string_view color = is_primary ? severity_color(diag.severity) : kBoldYellow;
    const char head = is_primary ? '^' : '-';
    const char tail = is_primary ? '~' : '-';
    render_underline(out, line, label.span.column, label.span.length, color, head, tail, paint,
                     label.message);
    prev_line = label.span.line;
  }

  // Closing blank pipe row so the block breathes at the bottom.
  out << gutter_pad;
  paint.wrap(out, kBoldBlue, "|");
  out << '\n';

  // Fix-its and help notes attach after the block as separate lines. Fix-its
  // render as "help: replace with `...`" for now; a diff-style presentation
  // waits until we have more than one fix-it in the wild.
  for (const FixIt &fix : diag.fixes) {
    (void)fix; // Currently no producer emits fixes; keep the loop as the
               // integration point once the first ones land.
  }
}

// Compact single-line layout — used when snippet rendering is unavailable
// (no SourceMap, unregistered path, label out of range). Preserves the
// Phase 1 output shape so existing tools that grep stderr still work.
void render_compact(std::ostream &out, const Diagnostic &diag, const RenderOptions &opts,
                    const Painter &paint) {
  const SourceSpan primary = diag.labels.empty() ? SourceSpan{} : diag.labels.front().span;

  if (!opts.source_path.empty()) {
    paint.wrap(out, kDim, opts.source_path);
    out << ':';
  }
  paint.wrap(out, kDim, std::to_string(primary.line));
  out << ':';
  paint.wrap(out, kDim, std::to_string(primary.column));
  out << ": ";

  paint.begin(out, severity_color(diag.severity));
  out << severity_label(diag.severity);
  if (!diag.code.empty())
    out << '[' << diag.code << ']';
  paint.end(out);
  out << ": " << diag.message << '\n';

  // Attached label messages after the primary line. Emit them as `note:`
  // lines so the shape stays a single "row per diagnostic" family and does
  // not pretend to be the full source-snippet layout.
  for (std::size_t i = 0; i < diag.labels.size(); ++i) {
    const DiagnosticLabel &label = diag.labels[i];
    if (i == 0 && label.message.empty())
      continue;
    if (label.message.empty())
      continue;
    if (!opts.source_path.empty()) {
      paint.wrap(out, kDim, opts.source_path);
      out << ':';
    }
    paint.wrap(out, kDim, std::to_string(label.span.line));
    out << ':';
    paint.wrap(out, kDim, std::to_string(label.span.column));
    out << ": ";
    paint.wrap(out, kCyan, "note");
    out << ": " << label.message << '\n';
  }
}

// Decide whether we have enough context to draw a snippet. Right now we only
// support single-file diagnostics: every label must implicitly live in
// `opts.source_path` (DiagnosticLabel::file is not modelled yet). If the
// primary label's line is not in the registered source, we bail so the
// compact fallback runs instead.
bool can_render_snippet(const Diagnostic &diag, const RenderOptions &opts) {
  if (opts.sources == nullptr)
    return false;
  if (opts.source_path.empty())
    return false;
  if (diag.labels.empty())
    return false;
  const std::string *text = opts.sources->lookup(opts.source_path);
  if (text == nullptr)
    return false;
  return !extract_line(*text, diag.labels.front().span.line).empty() ||
         diag.labels.front().span.line >= 1;
  // The extract_line == empty test above still lets legitimately blank
  // source lines through (they return a zero-length view whose data() is
  // non-null); the second clause guards against negative or zero line
  // numbers slipping through.
}

void render_one(std::ostream &out, const Diagnostic &diag, const RenderOptions &opts,
                const Painter &paint) {
  if (can_render_snippet(diag, opts)) {
    const std::string *text = opts.sources->lookup(opts.source_path);
    render_snippet(out, diag, *text, opts.source_path, paint);
  } else {
    render_compact(out, diag, opts, paint);
  }
}

} // namespace

bool color_enabled_for_stream(int fd, ColorMode mode) {
  switch (mode) {
  case ColorMode::Never:
    return false;
  case ColorMode::Always:
    return true;
  case ColorMode::Auto:
    break;
  }
  if (const char *no_color = std::getenv("NO_COLOR"); no_color != nullptr && no_color[0] != '\0') {
    return false;
  }
  if (const char *term = std::getenv("TERM"); term != nullptr) {
    if (std::strcmp(term, "dumb") == 0)
      return false;
  }
#if defined(_WIN32)
  if (std::getenv("WT_SESSION") == nullptr && std::getenv("TERM") == nullptr) {
    return false;
  }
#endif
  return fd_is_tty(fd);
}

void render(std::ostream &out, const Diagnostic &diag, const RenderOptions &opts, bool use_color) {
  Painter paint(use_color);
  render_one(out, diag, opts, paint);
}

void render_all(std::ostream &out, const std::vector<Diagnostic> &diags, const RenderOptions &opts,
                bool use_color) {
  Painter paint(use_color);
  for (const Diagnostic &d : diags) {
    render_one(out, d, opts, paint);
  }
}

} // namespace kinglet::diag
