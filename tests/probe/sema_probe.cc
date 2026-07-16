// LSP completion diagnostic tool: feeds a source file through the
// CompletionDriver pipeline (Scanner-injected-COMPLETION → Parser →
// TypeChecker-with-callback) and prints every result.  Not shipped.
//
// Usage: sema_probe [--completion-index <N>] <file.kl>
// Always exits 0 (this is a probe, not a pass/fail gate).

#include "driver/lsp/completion_entry.h"
#include "frontend/lexer/scanner.h"
#include "frontend/lexer/token.h"

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

  // Inject COMPLETION token if requested (mirrors perch's completion_token.cc).
  std::size_t effective_index = tokens.size();
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
    }
  }

  kinglet::lsp::CompletionRequest req;
  req.tokens = std::move(tokens);
  req.completion_index = effective_index;
  kinglet::lsp::CompletionResponse resp = kinglet::lsp::run_completion(req);

  std::cout << "=== parse errors (" << resp.parse_errors.size() << ") ===\n";
  for (const auto &err : resp.parse_errors) {
    std::cout << err.line << ":" << err.column << ": " << err.message << "\n";
  }

  if (!resp.program) {
    std::cout << "=== no AST produced ===\n";
    return 0;
  }

  std::cout << "=== declarations parsed: " << resp.program->declarations.size() << " ===\n";

  for (const auto &decl : resp.program->declarations) {
    const auto *fn = dynamic_cast<const kinglet::ast::FunctionDecl *>(decl.get());
    std::cout << "  decl";
    if (fn) {
      std::cout << " name=" << fn->name << " has_body=" << (fn->body != nullptr);
    }
    std::cout << "\n";
  }

  if (completion_index.has_value()) {
    std::cout << "=== parser completion: " << (resp.parser_completion.has_value() ? "set" : "none")
              << " ===\n";
    std::cout << "=== sema completion: " << (resp.sema_completion.has_value() ? "set" : "none")
              << " ===\n";
    if (resp.sema_completion.has_value()) {
      std::cout << "=== receiver type kind: "
                << static_cast<int>(resp.sema_completion->receiver_type.kind) << " ===\n";
      std::cout << "=== scopes depth: " << resp.sema_completion->scopes.size() << " ===\n";
    }
  }

  std::cout << "=== TypeChecker diagnostics (" << resp.type_errors.size() << ") ===\n";
  for (const auto &err : resp.type_errors) {
    const char *label = err.severity == kinglet::Severity::Warning ? "warning" : "error";
    const auto span = err.labels.empty() ? kinglet::SourceSpan{} : err.labels.front().span;
    std::cout << span.line << ":" << span.column << ": " << label << ": " << err.message << "\n";
  }

  std::cout << "=== probe completed without crash ===\n";
  return 0;
}