// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

// Internal helpers shared across the parser translation units
// (parse_decl.cc, parse_stmt.cc, parse_expr.cc, parse_type.cc, parser.cc).
// Not part of the public parser interface.

#pragma once

#include "frontend/ast/ast.h"
#include "frontend/lexer/token.h"

#include <vector>

namespace kinglet {

bool is_assignment_operator(TokenType type);
void skip_array_and_nullable_suffix(const std::vector<Token> &tokens, size_t &pos);
ast::AssignOp token_to_assign_op(TokenType type);
ast::BinaryOp token_to_binary_op(TokenType type);
ast::UnaryOp token_to_unary_op(TokenType type);

} // namespace kinglet
