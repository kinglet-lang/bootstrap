// C1 exploration tool for ADR 0024: feed TypeChecker::check() a partial AST
// (i.e. one produced despite parse errors) and print what it does — this
// path is never exercised today because both the CLI (main.cc) and perch's
// analysis.cc bail out before calling TypeChecker::check() whenever the
// parser reported any error. Not wired into the production build; this is
// a throwaway diagnostic binary for the C1 investigation only.
//
// Usage: sema_probe <file.kl>
// Always exits 0 (this is a probe, not a pass/fail gate). Prints parse
// errors, then unconditionally runs TypeChecker::check() on whatever AST
// was produced and prints every diagnostic it reports, plus a crash
// indicator if the process aborts/segfaults (bash wrapper checks $?).

#include "frontend/checker/type_checker.h"
#include "frontend/lexer/scanner.h"
#include "frontend/parser/parser.h"

#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: sema_probe <file.kl>\n";
    return 2;
  }

  std::ifstream in(argv[1]);
  if (!in) {
    std::cerr << "cannot open " << argv[1] << "\n";
    return 2;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  std::string source = ss.str();

  kinglet::Scanner scanner(source);
  auto tokens = scanner.scan_tokens();

  kinglet::Parser parser(tokens);
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
