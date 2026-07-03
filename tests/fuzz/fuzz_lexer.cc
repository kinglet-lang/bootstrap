// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT
//
// libFuzzer entry point for the lexer. Feeds arbitrary bytes into the Scanner
// and exercises tokenization. A well-behaved lexer must never crash, hang, or
// invoke undefined behavior on any input; it may only produce error tokens.
// Any deadly signal, ASan/UBSan report, or timeout is a bug.

#include "frontend/lexer/scanner.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  std::string source(reinterpret_cast<const char *>(data), size);
  kinglet::Scanner scanner(std::move(source));
  volatile auto tokens = scanner.scan_tokens();
  (void)tokens;
  return 0;
}
