// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "driver/preen/token_trivia.h"

namespace kinglet::preen {

TokenTriviaIndex::TokenTriviaIndex(const std::vector<FmtToken> &fmt_tokens,
                                   const std::vector<Token> &parse_tokens) {
  trivia_.resize(parse_tokens.size());
  std::size_t parse_idx = 0;
  std::string pending;
  int last_token_line = -1;
  std::size_t last_token_idx = parse_tokens.size();

  for (const FmtToken &token : fmt_tokens) {
    if (token.kind == FmtTokenKind::Token) {
      if (parse_idx >= parse_tokens.size() || token.token.type == TokenType::END_OF_FILE) {
        break;
      }
      if (last_token_idx < parse_tokens.size() && token.line == last_token_line) {
        trivia_[last_token_idx].trailing += pending;
      } else {
        trivia_[parse_idx].leading += pending;
      }
      pending.clear();
      last_token_idx = parse_idx;
      last_token_line = token.line;
      ++parse_idx;
      continue;
    }

    if (token.kind == FmtTokenKind::Newline) {
      if (last_token_idx < parse_tokens.size()) {
        trivia_[last_token_idx].trailing += token.text;
      }
      pending += token.text;
      last_token_line = -1;
      continue;
    }

    if (token.kind == FmtTokenKind::LineComment && last_token_idx < parse_tokens.size() &&
        token.line == last_token_line) {
      trivia_[last_token_idx].trailing += token.text;
      continue;
    }

    pending += token.text;
  }
}

const TokenTrivia &TokenTriviaIndex::at(std::size_t index) const {
  static const TokenTrivia kEmpty;
  if (index >= trivia_.size()) {
    return kEmpty;
  }
  return trivia_[index];
}

} // namespace kinglet::preen
