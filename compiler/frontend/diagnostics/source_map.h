// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT
//
// SourceMap keeps compiled source text alive by path so the renderer can
// render snippet lines under caret arrows (ADR 0031 D7). The driver reads
// each entry file once and registers it here before the pipeline runs; the
// renderer looks it up when a Diagnostic's primary label points at a known
// file. Missing entries fall back to the single-line "path:L:C: severity:
// message" layout — the renderer never invents source it cannot see.
//
// Ownership: the map stores the source text as a `std::string` by value.
// The driver owns the SourceMap and keeps it alive until every diagnostic
// has rendered. The renderer only reads through a `const *` pointer stashed
// in RenderOptions; concurrent access is not modelled because the pipeline
// is single-threaded today.

#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace kinglet::diag {

class SourceMap {
public:
  // Register a file's full text. Overwrites any previous entry for the same
  // path — the driver reads each source exactly once, so overwrites indicate
  // a caller bug rather than a legitimate mid-run change.
  void add(std::string path, std::string content) {
    files_.insert_or_assign(std::move(path), std::move(content));
  }

  // Look up the text registered for `path`. Returns nullptr when absent so
  // the renderer can fall back cleanly instead of rendering a phantom line.
  const std::string *lookup(std::string_view path) const {
    // std::unordered_map lacks heterogeneous lookup on GCC libstdc++'s
    // string map — hop through a temporary string to avoid an ambiguous
    // overload. The map is tiny (one entry per source file this run), so
    // the copy cost is negligible.
    const auto it = files_.find(std::string(path));
    return it == files_.end() ? nullptr : &it->second;
  }

private:
  std::unordered_map<std::string, std::string> files_;
};

} // namespace kinglet::diag
