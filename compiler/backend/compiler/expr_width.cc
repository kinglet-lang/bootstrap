// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "backend/compiler/expr_width.h"

#include "frontend/types/numeric.h"

namespace kinglet {

namespace {

std::string join_int_widths(const std::string &lhs, const std::string &rhs) {
  if (lhs.empty() || rhs.empty() || lhs != rhs) {
    if ((lhs == "int32" && rhs == "int64") || (lhs == "int64" && rhs == "int32")) {
      return "int64";
    }
    return {};
  }
  return lhs;
}

} // namespace

std::string infer_expr_type_name(const ast::Expr &expr,
                                 const std::unordered_map<std::string, std::string> &local_types) {
  if (const auto *lit = dynamic_cast<const ast::IntLiteralExpr *>(&expr)) {
    return int_literal_type_from_suffix(lit->width_suffix, lit->value).name;
  }
  if (const auto *char_lit = dynamic_cast<const ast::CharLiteralExpr *>(&expr)) {
    (void)char_lit;
    return "int8";
  }
  if (const auto *float_lit = dynamic_cast<const ast::FloatLiteralExpr *>(&expr)) {
    return float_literal_type_from_suffix(float_lit->width_suffix).name;
  }
  if (const auto *id = dynamic_cast<const ast::IdentifierExpr *>(&expr)) {
    const auto it = local_types.find(id->name);
    if (it != local_types.end()) {
      return it->second;
    }
    return {};
  }
  if (const auto *binary = dynamic_cast<const ast::BinaryExpr *>(&expr)) {
    switch (binary->op) {
    case ast::BinaryOp::Add:
    case ast::BinaryOp::Sub:
    case ast::BinaryOp::Mul:
    case ast::BinaryOp::Div:
    case ast::BinaryOp::Mod: {
      const std::string lhs = infer_expr_type_name(*binary->left, local_types);
      const std::string rhs = infer_expr_type_name(*binary->right, local_types);
      if (!lhs.empty() && !rhs.empty()) {
        if (lhs == "float32" || lhs == "float64" || rhs == "float32" || rhs == "float64") {
          if (lhs == rhs) {
            return lhs;
          }
          return "float64";
        }
        return join_int_widths(lhs, rhs);
      }
      return {};
    }
    default:
      break;
    }
  }
  return {};
}

OpCode width_arithmetic_opcode(ast::BinaryOp op, const std::string &width_name) {
  if (width_name == "int32") {
    switch (op) {
    case ast::BinaryOp::Add:
      return OpCode::AddI32;
    case ast::BinaryOp::Sub:
      return OpCode::SubtractI32;
    case ast::BinaryOp::Mul:
      return OpCode::MultiplyI32;
    case ast::BinaryOp::Div:
      return OpCode::DivideI32;
    case ast::BinaryOp::Mod:
      return OpCode::ModuloI32;
    default:
      break;
    }
  }
  switch (op) {
  case ast::BinaryOp::Add:
    return OpCode::Add;
  case ast::BinaryOp::Sub:
    return OpCode::Subtract;
  case ast::BinaryOp::Mul:
    return OpCode::Multiply;
  case ast::BinaryOp::Div:
    return OpCode::Divide;
  case ast::BinaryOp::Mod:
    return OpCode::Modulo;
  default:
    return OpCode::Add;
  }
}

} // namespace kinglet
