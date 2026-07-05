// LSP-mode completion entry point: runs Scanner → Parser → TypeChecker
// in a single pipeline that feeds the parsed AST (including any
// CompletionMarkerExpr nodes) through TypeChecker with an active
// completion callback.  Not linked into the `kinglet` CLI binary;
// only consumed by perch's kinglet-lsp and the `sema_probe` test harness.
//
// Usage sketch (perch side, after token injection):
//   auto tokens = scanner.scan_tokens();
//   inject_completion_token(tokens, completion_index);
//   auto result = CompletionDriver::run(request);

#pragma once

#include "frontend/ast/ast.h"
#include "frontend/checker/type_checker.h"
#include "frontend/lexer/token.h"
#include "frontend/parser/completion_context.h"
#include "frontend/parser/parser.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kinglet {
class ModuleLoader;
} // namespace kinglet

namespace kinglet::lsp {

struct CompletionRequest {
  // Pre-scanned token stream with a COMPLETION token already injected
  // at the cursor position by perch's completion_token.cc.
  std::vector<Token> tokens;
  // Index of the injected COMPLETION token in `tokens`.
  std::size_t completion_index;
  ModuleLoader *module_loader = nullptr;
};

struct CompletionResponse {
  // Parser-side completion context (CompletionPosition + receiver_type
  // string for perch's existing resolve_field_access path).
  std::optional<CompletionInfo> parser_completion;
  // TypeChecker-side completion context (resolved receiver Type + live
  // scope/method/type registries for the new TypeChecker callback path).
  std::optional<TypeChecker::CompletionContext> sema_completion;
  std::unique_ptr<ast::Program> program;
  std::vector<ParseError> parse_errors;
  std::vector<TypeError> type_errors;
};

CompletionResponse run_completion(const CompletionRequest &request);

} // namespace kinglet::lsp