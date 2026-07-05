#include "driver/lsp/completion_entry.h"

#include "frontend/parser/parser.h"

namespace kinglet::lsp {

CompletionResponse run_completion(const CompletionRequest &request) {
  CompletionResponse response;

  Parser parser(request.tokens, request.completion_index);
  auto parse_result = parser.parse();
  response.parse_errors = std::move(parse_result.errors);
  response.parser_completion =
      parser.has_completion() ? std::optional(parser.completion_result()) : std::nullopt;

  if (!parse_result.program) {
    return response;
  }

  TypeChecker checker;
  if (request.module_loader) {
    checker.set_module_loader(request.module_loader);
  }

  std::optional<TypeChecker::CompletionContext> captured;
  checker.set_completion_callback(
      [&captured](const TypeChecker::CompletionContext &ctx) { captured = ctx; });

  auto type_result = checker.check(*parse_result.program);
  response.type_errors = std::move(type_result.errors);
  response.sema_completion = std::move(captured);
  response.program = std::move(parse_result.program);
  return response;
}

} // namespace kinglet::lsp