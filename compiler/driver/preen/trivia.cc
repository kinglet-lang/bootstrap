// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "driver/preen/trivia.h"

namespace kinglet::preen {

namespace {

bool is_comment(const FmtToken &token) {
  return token.kind == FmtTokenKind::LineComment || token.kind == FmtTokenKind::BlockComment;
}

std::string normalize_trivia(std::string text) {
  if (!text.empty() && text.back() != '\n') {
    text.push_back('\n');
  }
  return text;
}

} // namespace

TriviaMap::TriviaMap(const std::vector<FmtToken> &tokens) {
  std::string pending;
  for (const FmtToken &token : tokens) {
    if (token.kind == FmtTokenKind::Whitespace) {
      continue;
    }
    if (token.kind == FmtTokenKind::Newline) {
      continue;
    }
    if (is_comment(token)) {
      pending += normalize_trivia(token.text);
      continue;
    }
    if (!pending.empty()) {
      leading_by_line_.push_back({token.line, pending});
      pending.clear();
    }
    if (token.kind == FmtTokenKind::Token && token.token.type == TokenType::END_OF_FILE) {
      break;
    }
  }
}

std::string TriviaMap::leading_for_line(int line) const {
  for (const auto &[decl_line, text] : leading_by_line_) {
    if (decl_line == line) {
      return text;
    }
  }
  return {};
}

} // namespace kinglet::preen
