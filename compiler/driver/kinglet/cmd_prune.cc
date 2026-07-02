// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "driver/kinglet/cmd_prune.h"

#include "driver/kinglet/cli_internal.h"
#include "frontend/module/project_config.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace kinglet {

namespace fs = std::filesystem;

namespace {

int remove_path(const fs::path &path, bool dry_run) {
  if (!fs::exists(path)) {
    return 0;
  }
  if (dry_run) {
    std::cout << "would remove " << path << '\n';
    return 1;
  }
  std::error_code ec;
  fs::remove_all(path, ec);
  if (ec) {
    std::cerr << "kinglet prune: cannot remove " << path << ": " << ec.message() << '\n';
    return 0;
  }
  return 1;
}

// Walk objects/native/ subdirectories. Each subdirectory corresponds to a
// source file (relative path from project root). Remove cache directories
// whose source file no longer exists on disk.
int prune_native_objects(const fs::path &objects_dir, const fs::path &project_root, bool dry_run) {
  const fs::path native_dir = objects_dir / "native";
  std::error_code ec;
  if (!fs::exists(native_dir, ec)) {
    return 0;
  }

  int removed = 0;

  // Collect all subdirectory paths (one level per source module path
  // component). Process deepest first so parent directories can be
  // removed after their children.
  std::vector<fs::path> subdirs;
  for (const fs::directory_entry &entry : fs::recursive_directory_iterator(native_dir, ec)) {
    if (ec)
      break;
    if (entry.is_directory()) {
      subdirs.push_back(entry.path());
    }
  }

  // Sort deepest first.
  std::sort(subdirs.begin(), subdirs.end(), [](const fs::path &a, const fs::path &b) {
    return std::distance(a.begin(), a.end()) > std::distance(b.begin(), b.end());
  });

  for (const fs::path &subdir : subdirs) {
    if (!fs::exists(subdir, ec))
      continue;

    // Reconstruct the relative source path from the cache directory.
    const fs::path rel = fs::relative(subdir, native_dir, ec);
    if (ec || rel.empty() || rel.string() == ".")
      continue;

    const fs::path source_path = project_root / rel;

    // Also check for stale .o files: if the directory exists but only
    // contains .o files whose corresponding source no longer needs them
    // (source was modified → hash changed), prune the oldest ones.
    // For now, keep any .o if the source file still exists.
    if (!fs::exists(source_path, ec)) {
      removed += remove_path(subdir, dry_run);
    }
  }

  return removed;
}

} // namespace

int cmd_prune(int argc, char **argv) {
  bool all = false;
  bool dry_run = false;
  std::string root_arg;
  for (int i = 2; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--all") {
      all = true;
      continue;
    }
    if (arg == "--dry-run" || arg == "-n") {
      dry_run = true;
      continue;
    }
    if (arg.starts_with('-')) {
      print_error("prune", std::string("unknown option ") + std::string(arg));
      return 64;
    }
    root_arg = std::string(arg);
    break;
  }

  const std::string start =
      root_arg.empty() ? fs::current_path().string() : fs::absolute(root_arg).string();
  const auto config = find_project_config(start);
  if (!config) {
    print_error("prune", "no kinglet.nest found");
    return 2;
  }

  const fs::path kinglet_dir = fs::path(config->root_dir) / ".kinglet";
  if (!fs::exists(kinglet_dir)) {
    if (!dry_run) {
      std::cout << ui::g_out.dim("•") << "  Nothing to do (" << display_path(kinglet_dir)
                << " missing)\n";
    }
    return 0;
  }

  if (all) {
    if (dry_run) {
      std::cout << ui::g_out.dim("•") << "  Would remove " << display_path(kinglet_dir) << "\n";
      return 0;
    }
    std::error_code ec;
    const auto removed = fs::remove_all(kinglet_dir, ec);
    if (ec) {
      print_error("prune", "cannot remove " + kinglet_dir.string() + ": " + ec.message());
      return 74;
    }
    std::cout << ui::g_out.green("✓") << "  Removed " << ui::g_out.bold(display_path(kinglet_dir))
              << "  " << ui::g_out.dim("(" + std::to_string(removed) + " entries)") << "\n";
    return 0;
  }

  const ui::Painter &p = ui::g_out;
  const fs::path objects_dir = kinglet_dir / "objects";
  const fs::path project_root = fs::path(config->root_dir);
  const int removed = prune_native_objects(objects_dir, project_root, dry_run);
  if (dry_run) {
    std::cout << p.dim("•") << "  " << removed << " path(s) would be removed\n";
  } else if (removed > 0) {
    std::cout << p.green("✓") << "  Pruned " << removed << " stale cache path(s)\n";
  } else {
    std::cout << p.dim("•") << "  No stale cache\n";
  }
  return 0;
}

} // namespace kinglet
