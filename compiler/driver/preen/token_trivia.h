// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "frontend/lexer/token.h"
#include "driver/preen/fmt_lexer.h"

#include <cstddef>
#include <string>
#include <vector>

namespace kinglet::preen {

struct TokenTrivia {
  std::string leading;
  std::string trailing;
};

class TokenTriviaIndex {
public:
  TokenTriviaIndex(const std::vector<FmtToken> &fmt_tokens, const std::vector<Token> &parse_tokens);

  std::size_t size() const { return trivia_.size(); }
  const TokenTrivia &at(std::size_t index) const;

private:
  std::vector<TokenTrivia> trivia_;
};

} // namespace kinglet::preen
