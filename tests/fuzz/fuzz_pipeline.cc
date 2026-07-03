// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT
//
// libFuzzer entry point for the full front-end pipeline: lex, parse, then
// type-check. Only inputs that parse cleanly are handed to the type checker,
// since the checker assumes a well-formed AST. The type checker must never
// crash or hang on any syntactically valid program; it may only accumulate
// TypeErrors. Deadly signals, sanitizer reports, and timeouts are bugs.

#include "frontend/checker/type_checker.h"
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
  kinglet::ParseResult parsed = parser.parse();

  // The type checker requires a well-formed AST. Skip inputs that failed to
  // parse or produced parse errors; feeding a partial AST would exercise
  // states the checker never sees in the real driver.
  if (parsed.program == nullptr || !parsed.errors.empty()) {
    return 0;
  }

  kinglet::TypeChecker checker;
  kinglet::TypeCheckResult result = checker.check(*parsed.program);
  volatile size_t error_count = result.errors.size();
  (void)error_count;
  return 0;
}
