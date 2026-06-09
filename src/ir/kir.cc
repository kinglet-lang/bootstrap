#include "ir/kir.h"

#include <sstream>

namespace kinglet {

const char *kir_opcode_name(KirOpcode op) {
  switch (op) {
  case KirOpcode::ConstInt:
    return "const_int";
  case KirOpcode::ConstFloat:
    return "const_float";
  case KirOpcode::ConstBool:
    return "const_bool";
  case KirOpcode::ConstNull:
    return "const_null";
  case KirOpcode::ConstString:
    return "const_string";
  case KirOpcode::LoadLocal:
    return "load_local";
  case KirOpcode::StoreLocal:
    return "store_local";
  case KirOpcode::Pop:
    return "pop";
  case KirOpcode::IAdd:
    return "iadd";
  case KirOpcode::ISub:
    return "isub";
  case KirOpcode::IMul:
    return "imul";
  case KirOpcode::IDiv:
    return "idiv";
  case KirOpcode::IMod:
    return "imod";
  case KirOpcode::ICmpEq:
    return "icmp_eq";
  case KirOpcode::ICmpNeq:
    return "icmp_neq";
  case KirOpcode::ICmpLt:
    return "icmp_lt";
  case KirOpcode::ICmpGt:
    return "icmp_gt";
  case KirOpcode::ICmpLe:
    return "icmp_le";
  case KirOpcode::ICmpGe:
    return "icmp_ge";
  case KirOpcode::ConstFn:
    return "const_fn";
  case KirOpcode::Call:
    return "call";
  case KirOpcode::Ret:
    return "ret";
  case KirOpcode::Br:
    return "br";
  case KirOpcode::CondBr:
    return "cond_br";
  case KirOpcode::Switch:
    return "switch";
  case KirOpcode::Unreachable:
    return "unreachable";
  case KirOpcode::Nop:
    return "nop";
  }
  return "unknown";
}

static void dump_operands(std::ostream &out, const KirInstr &instr) {
  for (std::size_t i = 0; i < instr.operands.size(); ++i) {
    if (i > 0) {
      out << ' ';
    }
    out << instr.operands[i];
  }
}

std::string dump_kir_function(const KirFunction &function) {
  std::ostringstream out;
  out << "fn " << function.name << '(';
  for (std::size_t i = 0; i < function.param_names.size(); ++i) {
    if (i > 0) {
      out << ", ";
    }
    out << function.param_names[i];
  }
  out << ") {\n";

  for (const KirBasicBlock &bb : function.blocks) {
    out << "  " << bb.label << ":\n";
    int temp = 0;
    for (const KirInstr &instr : bb.instrs) {
      const bool has_result = instr.op == KirOpcode::ConstInt ||
                              instr.op == KirOpcode::ConstFloat ||
                              instr.op == KirOpcode::ConstBool ||
                              instr.op == KirOpcode::ConstNull ||
                              instr.op == KirOpcode::ConstString ||
                              instr.op == KirOpcode::LoadLocal ||
                              instr.op == KirOpcode::IAdd || instr.op == KirOpcode::ISub ||
                              instr.op == KirOpcode::IMul || instr.op == KirOpcode::IDiv ||
                              instr.op == KirOpcode::IMod || instr.op == KirOpcode::ICmpEq ||
                              instr.op == KirOpcode::ICmpNeq || instr.op == KirOpcode::ICmpLt ||
                              instr.op == KirOpcode::ICmpGt || instr.op == KirOpcode::ICmpLe ||
                              instr.op == KirOpcode::ICmpGe || instr.op == KirOpcode::ConstFn ||
                              instr.op == KirOpcode::Call;
      if (has_result) {
        out << "    %" << temp++ << " = ";
      } else {
        out << "    ";
      }
      out << kir_opcode_name(instr.op);
      if (!instr.operands.empty()) {
        out << ' ';
        dump_operands(out, instr);
      }
      out << '\n';
    }
  }
  out << "}\n";
  return out.str();
}

std::string dump_kir_module(const KirModule &module) {
  std::ostringstream out;
  for (const KirFunction &fn : module.functions) {
    out << dump_kir_function(fn);
    if (!out.str().empty() && out.str().back() != '\n') {
      out << '\n';
    }
  }
  return out.str();
}

} // namespace kinglet
