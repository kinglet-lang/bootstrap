// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "ir/kir.h"
#include "types/types.h"

#include <string_view>

namespace kinglet {

KirType kir_type_from_surface_type(const Type &type);
KirType kir_type_from_int_literal_suffix(std::string_view suffix, int64_t value);
KirType kir_type_from_float_literal_suffix(std::string_view suffix);
KirType kir_type_normalize(KirType type);
KirType kir_const_opcode_result_type(KirOpcode op);

struct KirBinopSpec {
  KirOpcode generic = KirOpcode::Nop;
  KirType width = KirType::Any;
  bool specialized = false;
};

KirBinopSpec kir_binop_spec(KirOpcode op);
bool kir_opcode_is_arithmetic(KirOpcode op);

bool kir_type_is_integer(KirType type);
bool kir_type_is_float(KirType type);
KirType kir_type_join_numeric(KirType a, KirType b);

} // namespace kinglet
