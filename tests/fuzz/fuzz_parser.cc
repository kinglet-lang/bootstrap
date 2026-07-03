// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT
//
// libFuzzer entry point for the parser. Feeds arbitrary bytes through the
// lexer and into the recursive-descent parser. The parser must gracefully
// report ParseErrors for malformed input, never crash or hang. Reported
// parse errors are expected and ignored; only deadly signals, sanitizer
// reports, and timeouts constitute bugs.

#include "frontend/lexer/scanner.h"
#include "frontend/parser/parser.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  std::string source(reinterpret_cast<const char *>(data), size);
  kinglet::Scanner scanner(std::move(source));
  auto tokens = scanner.scan_tokens();

  kinglet::Parser parser(tokens);
  kinglet::ParseResult result = parser.parse();

  // Touch the produced program so the optimizer cannot elide the parse.
  volatile bool has_program = result.program != nullptr;
  (void)has_program;
  return 0;
}
