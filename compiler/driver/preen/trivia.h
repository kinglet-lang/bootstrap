// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "driver/preen/fmt_lexer.h"

#include <string>
#include <vector>

namespace kinglet::preen {

struct TriviaBundle {
  std::string leading;
};

class TriviaMap {
public:
  explicit TriviaMap(const std::vector<FmtToken> &tokens);

  std::string leading_for_line(int line) const;

private:
  std::vector<std::pair<int, std::string>> leading_by_line_;
};

} // namespace kinglet::preen
