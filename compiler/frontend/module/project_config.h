// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kinglet {

struct ProjectFmtSection {
  int indent = 0; // 0 = unset
  int max_width = 0;
  std::string newline; // empty = unset; "lf" or "crlf"
  bool trailing_comma_set = false;
  bool trailing_comma = false;
  std::vector<std::string> extensions;
  std::vector<std::pair<std::string, bool>> extension_entries; // [[fmt.extensions]]
};

// A build target: a named unit that produces an artifact (binary/library/...) from a
// set of source files. Module identity lives in the .kl sources themselves
// (`export module X;` / `import X;`, C++20 style); the target only declares which
// files belong to it and which other targets it depends on.
enum class TargetKind {
  Binary,  // executable; exactly one source defines `int main()`
  Library, // no main; exports modules to dependents via deps
  Test,    // executable run by `kinglet test` (built like Binary for now)
  Object,  // compile to .o only, no link (parse-only support for now)
};

struct TargetConfig {
  std::string name;
  TargetKind kind = TargetKind::Binary;
  std::vector<std::string> sources; // files and "dir/" globs, verbatim from nest
  std::vector<std::string> deps;    // other target names
};

struct ProjectConfig {
  std::string root_dir;
  std::string name;
  std::string version;
  std::string cache_dir = ".kinglet/cache";
  std::string out_dir = ".kinglet/out";
  std::string build_default;         // names a target
  std::vector<TargetConfig> targets; // target { } blocks
  ProjectFmtSection fmt;
};

// Parse a "binary"/"library"/"test"/"object" string into a TargetKind.
// Returns false if the string is not a recognised kind.
bool parse_target_kind(const std::string &s, TargetKind &out);

// Find a target by name; nullptr if absent.
const TargetConfig *find_target(const ProjectConfig &config, const std::string &name);

// Expand a target's `sources` entries (files + "dir/" globs) into a sorted list of
// absolute .kl paths under root_dir. Directory entries glob `*.kl` (sorted).
std::vector<std::string> resolve_target_sources(const ProjectConfig &config,
                                                const TargetConfig &target);

// Resolve the entry (main) file of a binary/test target by auto-detecting the single
// source containing `int main()`. Returns the path on success; sets `error` and returns
// nullopt on zero or multiple mains.
std::optional<std::string> resolve_target_entry(const ProjectConfig &config,
                                                const TargetConfig &target, std::string &error);

std::optional<ProjectConfig> find_project_config(const std::string &start_dir);
std::optional<ProjectConfig> load_nest_config_file(const std::filesystem::path &manifest_path);
std::optional<ProjectConfig> find_nest_config(const std::string &start_dir);

} // namespace kinglet
