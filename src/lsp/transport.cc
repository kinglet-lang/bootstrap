#include "lsp/transport.h"

#include <iostream>
#include <sstream>

namespace kinglet::lsp {

std::string Transport::read_message() {
  std::string line;
  long length = -1;
  while (std::getline(std::cin, line)) {
    if (line.empty() || line == "\r") continue;
    if (line.starts_with("Content-Length:")) {
      length = std::stol(line.substr(15));
      std::getline(std::cin, line);
      break;
    }
  }
  if (length <= 0) return "";
  std::string content(static_cast<std::size_t>(length), '\0');
  std::cin.read(content.data(), length);
  return content;
}

void Transport::write_message(const json::Value &msg) {
  std::string body = json::to_string(msg);
  std::ostringstream header;
  header << "Content-Length: " << body.size() << "\r\n\r\n";
  std::cout << header.str() << body << std::flush;
}

} // namespace kinglet::lsp
