#pragma once

#include "lsp/json.h"

#include <string>

namespace kinglet::lsp {

class Transport {
public:
  std::string read_message();
  void write_message(const json::Value &msg);
};

} // namespace kinglet::lsp
