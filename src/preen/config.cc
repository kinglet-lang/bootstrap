#include "preen/config.h"

#include <algorithm>

namespace kinglet::preen {

FmtConfig FmtConfig::defaults() { return FmtConfig{}; }

FmtConfig FmtConfig::merge(const FmtConfig &base, const FmtConfig &overlay) {
  FmtConfig out = base;
  if (overlay.indent > 0) {
    out.indent = overlay.indent;
  }
  if (overlay.max_width > 0) {
    out.max_width = overlay.max_width;
  }
  out.newline = overlay.newline;
  out.trailing_comma = overlay.trailing_comma;
  if (!overlay.extensions.empty()) {
    out.extensions = overlay.extensions;
  }
  return out;
}

std::string FmtConfig::newline_string() const {
  return newline == NewlineStyle::Crlf ? "\r\n" : "\n";
}

bool FmtConfig::extension_enabled(std::string_view name) const {
  return std::find(extensions.begin(), extensions.end(), name) != extensions.end();
}

} // namespace kinglet::preen
