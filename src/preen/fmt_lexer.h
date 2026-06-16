#pragma once

#include "lexer/token.h"

#include <string>
#include <string_view>
#include <vector>

namespace kinglet::preen {

enum class FmtTokenKind {
  Token,
  LineComment,
  BlockComment,
  Whitespace,
  Newline,
};

struct FmtToken {
  FmtTokenKind kind = FmtTokenKind::Token;
  Token token{};
  std::string text;
  int line = 0;
  int column = 0;
  std::size_t start = 0;
  std::size_t end = 0;
};

class FmtLexer {
public:
  explicit FmtLexer(std::string source);

  std::vector<FmtToken> scan_all();
  static std::vector<Token> to_parse_tokens(const std::vector<FmtToken> &tokens);

private:
  bool is_at_end() const;
  char advance();
  bool match(char expected);
  char peek() const;
  char peek_next() const;

  void scan_trivia(std::vector<FmtToken> &out);
  Token scan_token();
  Token make_token(TokenType type) const;
  Token make_error(std::string_view message) const;
  Token identifier();
  Token number();
  Token string_literal();
  Token char_literal();
  std::string scan_int_suffix();
  std::string scan_float_suffix();
  TokenType identifier_type() const;
  std::string current_lexeme_without_separators() const;

  std::string source_;
  std::size_t start_ = 0;
  std::size_t current_ = 0;
  int line_ = 1;
  int column_ = 1;
  int token_line_ = 1;
  int token_column_ = 1;
};

} // namespace kinglet::preen
