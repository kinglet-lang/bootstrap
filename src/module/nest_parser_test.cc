// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "module/nest_parser.h"

#include <fstream>
#include <iostream>
#include <sstream>
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
  std::ifstream in("../../../test/kinglet.nest");
  if (!in) {
    std::cerr << "FAIL: cannot open ../../test/kinglet.nest\n";
    ++failures;
    return;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  kinglet::ProjectConfig config;
  expect(kinglet::parse_nest_manifest(buffer.str(), config), "parse demo nest");
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

} // namespace

int main() {
  test_demo_nest();
  if (failures == 0) {
    std::cout << "All nest parser tests passed.\n";
    return 0;
  }
  std::cerr << failures << " nest parser test(s) failed.\n";
  return 1;
}
