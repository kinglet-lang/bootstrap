#include "preen/extension.h"

#include <algorithm>
#include <sstream>
#include <string_view>
#include <vector>

namespace kinglet::preen {

namespace {

bool starts_with(std::string_view line, std::string_view prefix) {
  return line.rfind(prefix, 0) == 0;
}

std::string trim_right(std::string line) {
  while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
    line.pop_back();
  }
  return line;
}

std::vector<std::string> split_lines(const std::string &text) {
  std::vector<std::string> lines;
  std::stringstream ss(text);
  std::string line;
  while (std::getline(ss, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }
  return lines;
}

std::string join_lines(const std::vector<std::string> &lines, const std::string &newline) {
  std::string out;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) {
      out += newline;
    }
    out += lines[i];
  }
  if (!out.empty() && !lines.empty()) {
    out += newline;
  }
  return out;
}

bool is_using_line(const std::string &line) {
  const std::string trimmed = trim_right(line);
  return starts_with(trimmed, "using ");
}

bool is_import_line(const std::string &line) {
  const std::string trimmed = trim_right(line);
  return starts_with(trimmed, "import ");
}

std::string align_import_block(std::vector<std::string> lines) {
  struct Entry {
    std::size_t index = 0;
    std::string prefix;
    std::string body;
  };
  std::vector<Entry> entries;
  std::size_t max_body = 0;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const std::string trimmed = trim_right(lines[i]);
    std::string prefix;
    std::string body;
    if (starts_with(trimmed, "using ")) {
      prefix = "using ";
      body = trimmed.substr(6);
      if (!body.empty() && body.back() == ';') {
        body.pop_back();
      }
    } else if (starts_with(trimmed, "import ")) {
      prefix = "import ";
      body = trimmed.substr(7);
    } else {
      continue;
    }
    max_body = std::max(max_body, body.size());
    entries.push_back({i, prefix, body});
  }
  if (entries.size() < 2) {
    return join_lines(lines, "\n");
  }
  for (const Entry &entry : entries) {
    std::string padded = entry.body;
    if (padded.size() < max_body) {
      padded += std::string(max_body - padded.size(), ' ');
    }
    lines[entry.index] = entry.prefix + padded + (entry.prefix == "using " ? ";" : "");
  }
  return join_lines(lines, "\n");
}

std::string group_using_block(std::vector<std::string> lines) {
  std::vector<std::string> out;
  enum class Section { None, Import, Using, Other };
  Section current = Section::None;
  for (const std::string &line : lines) {
    if (line.empty()) {
      continue;
    }
    Section next = Section::Other;
    if (is_import_line(line)) {
      next = Section::Import;
    } else if (is_using_line(line)) {
      next = Section::Using;
    }
    if (current != Section::None && next != Section::None && next != current) {
      out.push_back("");
    }
    out.push_back(line);
    if (next != Section::None) {
      current = next;
    } else if (!line.empty()) {
      current = Section::Other;
    }
  }
  return join_lines(out, "\n");
}

} // namespace

std::string apply_extensions(const std::string &formatted, const FmtConfig &config) {
  std::string out = formatted;
  if (config.extension_enabled("align-imports")) {
    out = align_import_block(split_lines(out));
  }
  if (config.extension_enabled("group-using")) {
    out = group_using_block(split_lines(out));
  }
  if (config.newline == NewlineStyle::Crlf) {
    std::string normalized;
    normalized.reserve(out.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
      if (out[i] == '\n' && (i == 0 || out[i - 1] != '\r')) {
        normalized += "\r\n";
      } else if (out[i] != '\r') {
        normalized.push_back(out[i]);
      }
    }
    out = std::move(normalized);
  }
  return out;
}

} // namespace kinglet::preen
