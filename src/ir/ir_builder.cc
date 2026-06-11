#include "ir/ir_builder.h"

#include "ir/kir_numeric.h"

namespace kinglet {

namespace {

KirInstr make_instr(KirOpcode op, std::vector<int32_t> operands, ast::SourceLocation loc) {
  KirInstr instr;
  instr.op = op;
  instr.operands = std::move(operands);
  instr.line = loc.line;
  instr.col = loc.column;
  return instr;
}

} // namespace

bool IrBuilder::build_expr_into(KirFunction *fn, KirBasicBlock *bb, const ast::Expr &expr,
                                int *out_value) const {
  if (const auto *lit = dynamic_cast<const ast::IntLiteralExpr *>(&expr)) {
    const KirType width =
        kir_type_from_int_literal_suffix(lit->width_suffix, lit->value);
    KirOpcode op = KirOpcode::ConstInt;
    std::vector<int32_t> operands;
    if (kir_type_normalize(width) == KirType::Int32) {
      op = KirOpcode::ConstI32;
      operands = {static_cast<int32_t>(lit->value)};
    } else {
      op = KirOpcode::ConstI64;
      operands = {static_cast<int32_t>(lit->value),
                  static_cast<int32_t>(static_cast<uint64_t>(lit->value) >> 32)};
    }
    bb->instrs.push_back(make_instr(op, std::move(operands), expr.location));
    *out_value = static_cast<int>(bb->instrs.size()) - 1;
    return true;
  }

  if (const auto *bin = dynamic_cast<const ast::BinaryExpr *>(&expr)) {
    int lhs = -1;
    int rhs = -1;
    if (!build_expr_into(fn, bb, *bin->left, &lhs) ||
        !build_expr_into(fn, bb, *bin->right, &rhs)) {
      return false;
    }
    KirOpcode op = KirOpcode::Nop;
    switch (bin->op) {
    case ast::BinaryOp::Add:
      op = KirOpcode::IAdd;
      break;
    case ast::BinaryOp::Sub:
      op = KirOpcode::ISub;
      break;
    case ast::BinaryOp::Mul:
      op = KirOpcode::IMul;
      break;
    case ast::BinaryOp::Div:
      op = KirOpcode::IDiv;
      break;
    case ast::BinaryOp::Mod:
      op = KirOpcode::IMod;
      break;
    default:
      return false;
    }
    (void)fn;
    bb->instrs.push_back(make_instr(op, {lhs, rhs}, expr.location));
    *out_value = static_cast<int>(bb->instrs.size()) - 1;
    return true;
  }

  return false;
}

std::optional<KirFunction> IrBuilder::build_expr_function(const std::string &name,
                                                          const ast::Expr &expr) const {
  KirFunction fn;
  fn.name = name;
  fn.param_count = 0;
  KirBasicBlock bb;
  bb.label = "bb0";
  int value_id = -1;
  if (!build_expr_into(&fn, &bb, expr, &value_id)) {
    return std::nullopt;
  }
  bb.instrs.push_back(make_instr(KirOpcode::Ret, {value_id}, expr.location));
  fn.blocks.push_back(std::move(bb));
  return fn;
}

} // namespace kinglet
