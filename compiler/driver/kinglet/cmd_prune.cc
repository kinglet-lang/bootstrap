// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "driver/kinglet/cmd_prune.h"

#include "driver/kinglet/cli_internal.h"
#include "frontend/module/project_config.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <string_view>

namespace kinglet {

namespace fs = std::filesystem;

namespace {

std::set<std::string> collect_referenced_object_ids(const fs::path &stamps_dir) {
  std::set<std::string> referenced;
  std::error_code ec;
  if (!fs::exists(stamps_dir, ec)) {
    return referenced;
  }
  for (const fs::directory_entry &entry : fs::directory_iterator(stamps_dir, ec)) {
    if (ec || !entry.is_regular_file()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (!name.ends_with(".object")) {
      continue;
    }
    std::ifstream in(entry.path());
    std::string object_id;
    in >> object_id;
    object_id = trim_copy(object_id);
    if (!object_id.empty()) {
      referenced.insert(object_id);
    }
  }
  return referenced;
}

int remove_path(const fs::path &path, bool dry_run) {
  if (!fs::exists(path)) {
    return 0;
  }
  if (dry_run) {
    std::cout << "would remove " << path << '\n';
    return 1;
  }
  std::error_code ec;
  fs::remove(path, ec);
  if (ec) {
    std::cerr << "kinglet prune: cannot remove " << path << ": " << ec.message() << '\n';
    return 0;
  }
  return 1;
}

int prune_unreferenced_objects(const fs::path &objects_dir, bool dry_run) {
  std::error_code ec;
  if (!fs::exists(objects_dir, ec)) {
    return 0;
  }

  const std::set<std::string> referenced =
      collect_referenced_object_ids(objects_dir.parent_path() / "stamps");

  int removed = 0;
  for (const fs::directory_entry &entry : fs::directory_iterator(objects_dir, ec)) {
    if (ec) {
      break;
    }
    if (entry.is_directory()) {
      continue;
    }
    const fs::path path = entry.path();
    const std::string name = path.filename().string();
    if (name.ends_with(".meta")) {
      const std::string object_id = name.substr(0, name.size() - 5);
      if (referenced.count(object_id) > 0) {
        continue;
      }
      removed += remove_path(path, dry_run);
      removed += remove_path(objects_dir / object_id, dry_run);
      continue;
    }
    if (referenced.count(name) > 0) {
      continue;
    }
    if (fs::exists(objects_dir / (name + ".meta"))) {
      continue;
    }
    removed += remove_path(path, dry_run);
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
  const int removed = prune_unreferenced_objects(kinglet_dir / "objects", dry_run);
  if (dry_run) {
    std::cout << p.dim("•") << "  " << removed << " path(s) would be removed\n";
  } else if (removed > 0) {
    std::cout << p.green("✓") << "  Pruned " << removed << " unreferenced object path(s)\n";
  } else {
    std::cout << p.dim("•") << "  No unreferenced objects\n";
  }
  return 0;
}

} // namespace kinglet
