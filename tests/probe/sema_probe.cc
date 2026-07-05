// C1 exploration tool for ADR 0024: feed TypeChecker::check() a partial AST
// (i.e. one produced despite parse errors) and print what it does — this
// path is never exercised today because both the CLI (main.cc) and perch's
// analysis.cc bail out before calling TypeChecker::check() whenever the
// parser reported any error. Not wired into the production build; this is
// a throwaway diagnostic binary for the C1 investigation only.
//
// Usage: sema_probe [--completion-index <N>] <file.kl>
// Always exits 0 (this is a probe, not a pass/fail gate). Prints parse
// errors, then unconditionally runs TypeChecker::check() on whatever AST
// was produced and prints every diagnostic it reports, plus a crash
// indicator if the process aborts/segfaults (bash wrapper checks $?).

#include "frontend/checker/type_checker.h"
#include "frontend/lexer/scanner.h"
#include "frontend/lexer/token.h"
#include "frontend/parser/parser.h"

#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

int main(int argc, char **argv) {
  std::optional<std::size_t> completion_index;
  const char *file_arg = nullptr;

  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg == "--completion-index" && i + 1 < argc) {
      completion_index = static_cast<std::size_t>(std::stoul(argv[++i]));
    } else if (!file_arg) {
      file_arg = argv[i];
    }
  }

  if (!file_arg) {
    std::cerr << "usage: sema_probe [--completion-index <N>] <file.kl>\n";
    return 2;
  }

  std::ifstream in(file_arg);
  if (!in) {
    std::cerr << "cannot open " << file_arg << "\n";
    return 2;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  std::string source = ss.str();

  kinglet::Scanner scanner(source);
  auto tokens = scanner.scan_tokens();

  // If a completion index was specified, inject a COMPLETION token before
  // the token at that index.  This mirrors what perch's completion_token.cc
  // does in production: after scanning the original source, the completion
  // site's position is translated into a token index and a synthetic
  // COMPLETION token is spliced into the stream.  The Parser constructor
  // then receives the index of that injected token so at_completion()
  // fires exactly once when the cursor reaches it.
  std::size_t effective_index = tokens.size(); // never fires by default
  if (completion_index.has_value()) {
    std::size_t idx = *completion_index;
    if (idx < tokens.size()) {
      kinglet::Token comp_token;
      comp_token.type = kinglet::TokenType::COMPLETION;
      comp_token.line = tokens[idx].line;
      comp_token.column = tokens[idx].column;
      tokens.insert(tokens.begin() + static_cast<long>(idx), comp_token);
      effective_index = idx;
      std::cout << "=== injected COMPLETION at index " << idx << " ===\n";
    } else {
      std::cerr << "completion-index " << idx << " out of range (max " << tokens.size() << ")\n";
    }
  }

  kinglet::Parser parser(tokens, effective_index);
  kinglet::ParseResult result = parser.parse();

  std::cout << "=== parse errors (" << result.errors.size() << ") ===\n";
  for (const auto &err : result.errors) {
    std::cout << err.line << ":" << err.column << ": " << err.message << "\n";
  }

  if (!result.program) {
    std::cout << "=== no AST produced, cannot run TypeChecker ===\n";
    return 0;
  }

  std::cout << "=== declarations parsed: " << result.program->declarations.size() << " ===\n";

  if (completion_index.has_value()) {
    std::cout << "=== completion result: " << (parser.has_completion() ? "set" : "none")
              << " ===\n";
  }

  std::cout << "=== running TypeChecker::check() on partial AST ===\n";
  kinglet::TypeChecker checker;
  kinglet::TypeCheckResult type_result = checker.check(*result.program);

  std::cout << "=== TypeChecker diagnostics (" << type_result.errors.size() << ") ===\n";
  for (const auto &err : type_result.errors) {
    const char *label = err.severity == kinglet::DiagnosticSeverity::Warning ? "warning" : "error";
    std::cout << err.location.line << ":" << err.location.column << ": " << label << ": "
              << err.message << "\n";
  }

  std::cout << "=== probe completed without crash ===\n";
  return 0;
}