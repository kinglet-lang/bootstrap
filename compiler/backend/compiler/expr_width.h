// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "frontend/ast/ast.h"
#include "backend/vm/chunk.h"

#include <string>
#include <unordered_map>

namespace kinglet {

std::string infer_expr_type_name(const ast::Expr &expr,
                                 const std::unordered_map<std::string, std::string> &local_types);

OpCode width_arithmetic_opcode(ast::BinaryOp op, const std::string &width_name);

} // namespace kinglet
