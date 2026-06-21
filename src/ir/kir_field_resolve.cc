#include "ir/kir_field_resolve.h"

#include <cctype>

namespace kinglet {
namespace {

int field_index_for_name(const KirStructMeta &meta, const std::string &field_name) {
  for (std::size_t i = 0; i < meta.field_names.size(); ++i) {
    if (meta.field_names[i] == field_name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool is_identifier_like(const std::string &s) {
  if (s.empty()) {
    return false;
  }
  for (const char c : s) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
      return false;
    }
  }
  return true;
}

bool operand_is_field_name_pool_index(const KirModule &module, int operand) {
  if (operand < 0 ||
      static_cast<std::size_t>(operand) >= module.constant_strings.size()) {
    return false;
  }
  const std::string &s = module.constant_strings[static_cast<std::size_t>(operand)];
  if (!is_identifier_like(s)) {
    return false;
  }
  int matches = 0;
  for (const KirStructMeta &meta : module.struct_metas) {
    if (field_index_for_name(meta, s) >= 0) {
      ++matches;
    }
  }
  return matches > 0;
}

int unique_field_index_for_name(const KirModule &module, const std::string &field_name) {
  int found = -1;
  int matches = 0;
  for (const KirStructMeta &meta : module.struct_metas) {
    const int fi = field_index_for_name(meta, field_name);
    if (fi < 0) {
      continue;
    }
    if (found >= 0 && found != fi) {
      return -1;
    }
    found = fi;
    ++matches;
  }
  return matches > 0 ? found : -1;
}

} // namespace

void resolve_kir_field_operands(KirModule &module) {
  if (module.field_operands_resolved) {
    return;
  }
  for (KirFunction &fn : module.functions) {
    for (KirBasicBlock &bb : fn.blocks) {
      for (KirInstr &instr : bb.instrs) {
        if (instr.op != KirOpcode::FieldGet && instr.op != KirOpcode::FieldSet &&
            instr.op != KirOpcode::BorrowFieldMut) {
          continue;
        }
        if (instr.operands.empty()) {
          continue;
        }
        const int operand = instr.operands[0];
        if (!operand_is_field_name_pool_index(module, operand)) {
          continue;
        }
        const std::string &field_name =
            module.constant_strings[static_cast<std::size_t>(operand)];
        const int fi = unique_field_index_for_name(module, field_name);
        if (fi >= 0) {
          instr.operands[0] = fi;
        }
      }
    }
  }
  module.field_operands_resolved = true;
}

} // namespace kinglet
