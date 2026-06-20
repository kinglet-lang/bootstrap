// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "module/nest_parser.h"

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

modules {
  math = "lib/core/math.kl"
  util = "lib/core/util.kl"
  app = "apps/demo/main.kl"
  bench = "apps/bench/main.kl"
}

build {
  default = "demo"
  backend = native
  out = ".kinglet/out"
}
)";
  kinglet::ProjectConfig config;
  expect(kinglet::parse_nest_manifest(content, config), "parse demo nest");
  expect(config.name == "kinglet-demo", "project name");
  expect(config.version == "0.1.0", "project version");
  expect(config.modules.size() == 4, "module count");
  expect(config.modules.count("math") == 1, "math key");
  expect(config.modules.at("math") == "lib/core/math.kl", "math path");
  expect(config.modules.at("app") == "apps/demo/main.kl", "app path");
  expect(config.build_default == "demo", "build default");
  expect(config.default_backend == "native", "build backend");
  expect(config.out_dir == ".kinglet/out", "build out");
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
  test_fmt_block();
  if (failures == 0) {
    std::cout << "All nest parser tests passed.\n";
    return 0;
  }
  std::cerr << failures << " nest parser test(s) failed.\n";
  return 1;
}
