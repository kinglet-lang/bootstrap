#include "preen/preen.h"

#include "preen/emitter.h"
#include "preen/extension.h"
#include "preen/fmt_lexer.h"
#include "preen/span_map.h"
#include "preen/token_trivia.h"
#include "preen/trivia.h"
#include "parser/parser.h"

#include <fstream>
#include <sstream>

namespace kinglet::preen {

namespace {

bool has_unknown_extension(const FmtConfig &config, std::string &unknown) {
  static const char *kKnown[] = {"align-imports", "group-using", "align-struct-fields"};
  for (const std::string &name : config.extensions) {
    bool found = false;
    for (const char *known : kKnown) {
      if (name == known) {
        found = true;
        break;
      }
    }
    if (!found) {
      unknown = name;
      return true;
    }
  }
  return false;
}

} // namespace

FormatResult format_string(std::string_view source, const FmtConfig &config) {
  FormatResult result;
  std::string unknown;
  if (has_unknown_extension(config, unknown)) {
    result.error = "unknown formatting extension: " + unknown;
    return result;
  }

  FmtLexer lexer{std::string(source)};
  const std::vector<FmtToken> fmt_tokens = lexer.scan_all();
  const std::vector<Token> parse_tokens = FmtLexer::to_parse_tokens(fmt_tokens);

  for (const Token &token : parse_tokens) {
    if (token.type == TokenType::ERROR) {
      result.error = std::string(token.lexeme);
      return result;
    }
  }

  Parser parser(parse_tokens);
  ParseResult parsed = parser.parse();
  if (!parsed.errors.empty()) {
    std::ostringstream out;
    out << "parse error at " << parsed.errors.front().line << ":" << parsed.errors.front().column
        << ": " << parsed.errors.front().message;
    result.error = out.str();
    return result;
  }
  if (!parsed.program) {
    result.error = "parse error: empty program";
    return result;
  }

  TriviaMap trivia(fmt_tokens);
  TokenTriviaIndex token_trivia(fmt_tokens, parse_tokens);
  SpanMap spans(*parsed.program, parse_tokens);
  Emitter emitter(config, &trivia, &token_trivia, &spans, &parse_tokens);
  std::string formatted = emitter.emit_program(*parsed.program);
  formatted = apply_extensions(formatted, config);

  result.text = formatted;
  result.changed = formatted != std::string(source);
  return result;
}

FormatResult format_file(const std::filesystem::path &path, const FmtConfig &config) {
  FormatResult result;
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    result.error = "cannot read " + path.string();
    return result;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  result = format_string(buffer.str(), config);
  return result;
}

} // namespace kinglet::preen
