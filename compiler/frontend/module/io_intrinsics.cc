// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "frontend/module/io_intrinsics.h"

#include <algorithm>

namespace kinglet::io_intrinsics {

const std::vector<Member> &ostream_members() {
  static const std::vector<Member> members = {
      {"line", "print with newline (fmt string + args)", "line($1)", ArgMode::FmtArgs,
       ReturnKind::Void},
      {"flush", "flush buffered output", "flush()", ArgMode::NoArgs, ReturnKind::Void},
  };
  return members;
}

const std::vector<Member> &istream_members() {
  static const std::vector<Member> members = {
      {"secret", "read a line from stdin without echoing it", "secret($1)", ArgMode::Unchecked,
       ReturnKind::String},
  };
  return members;
}

namespace {
const Member *find(const std::vector<Member> &members, const std::string &name) {
  auto it =
      std::find_if(members.begin(), members.end(), [&](const Member &m) { return m.name == name; });
  return it == members.end() ? nullptr : &*it;
}
} // namespace

const Member *find_ostream_member(const std::string &name) {
  return find(ostream_members(), name);
}

const Member *find_istream_member(const std::string &name) {
  return find(istream_members(), name);
}

} // namespace kinglet::io_intrinsics
