// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "frontend/module/nest_parser.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool cond, const std::string &msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

void test_demo_nest() {
  const std::string content = R"(project "kinglet-demo" version "0.1.0"

target calc {
  kind    = "binary"
  sources = [
    "src/calc.kl",
    "math/",
    "utils/config.kl",
  ]
  deps = ["core"]
}

target core {
  kind    = "library"
  sources = ["lib/core/"]
}

build {
  default = "calc"
  out = ".kinglet/out"
}
)";
  kinglet::ProjectConfig config;
  expect(kinglet::parse_nest_manifest(content, config), "parse demo nest");
  expect(config.name == "kinglet-demo", "project name");
  expect(config.version == "0.1.0", "project version");
  expect(config.targets.size() == 2, "target count");
  const kinglet::TargetConfig *calc = kinglet::find_target(config, "calc");
  expect(calc != nullptr, "calc target found");
  if (calc != nullptr) {
    expect(calc->kind == kinglet::TargetKind::Binary, "calc kind binary");
    expect(calc->sources.size() == 3, "calc source count");
    expect(calc->sources[0] == "src/calc.kl", "calc first source");
    expect(calc->sources[1] == "math/", "calc dir source");
    expect(calc->deps.size() == 1 && calc->deps[0] == "core", "calc deps");
  }
  const kinglet::TargetConfig *core = kinglet::find_target(config, "core");
  expect(core != nullptr, "core target found");
  if (core != nullptr) {
    expect(core->kind == kinglet::TargetKind::Library, "core kind library");
    expect(core->sources.size() == 1 && core->sources[0] == "lib/core/", "core source");
  }
  expect(config.build_default == "calc", "build default");
  expect(config.out_dir == ".kinglet/out", "build out");
}

void test_target_kind_parse() {
  kinglet::TargetKind k;
  expect(kinglet::parse_target_kind("binary", k) && k == kinglet::TargetKind::Binary, "binary");
  expect(kinglet::parse_target_kind("library", k) && k == kinglet::TargetKind::Library, "library");
  expect(kinglet::parse_target_kind("test", k) && k == kinglet::TargetKind::Test, "test");
  expect(kinglet::parse_target_kind("object", k) && k == kinglet::TargetKind::Object, "object");
  expect(!kinglet::parse_target_kind("bogus", k), "reject bogus kind");
}

void test_fmt_block() {
  const std::string content = R"(project "fmt" version "0.1.0"

fmt {
  extensions = "align-imports,group-using"
  indent = 4
}
)";
  kinglet::ProjectConfig config;
  expect(kinglet::parse_nest_manifest(content, config), "parse fmt nest");
  expect(config.fmt.extensions.size() == 2, "fmt extension count");
  expect(config.fmt.extensions[0] == "align-imports", "first extension");
  expect(config.fmt.indent == 4, "fmt indent");
}

} // namespace

int main() {
  test_demo_nest();
  test_target_kind_parse();
  test_fmt_block();
  if (failures == 0) {
    std::cout << "All nest parser tests passed.\n";
    return 0;
  }
  std::cerr << failures << " nest parser test(s) failed.\n";
  return 1;
}
