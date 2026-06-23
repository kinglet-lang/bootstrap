// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "ir/kir_specialize.h"

#include "ir/kir.h"
#include "ir/kir_numeric.h"

#include <vector>

namespace kinglet {

namespace {

std::vector<KirInstr *> linear_mutable_instrs(KirFunction &fn) {
  std::vector<KirInstr *> out;
  for (KirBasicBlock &bb : fn.blocks) {
    for (KirInstr &instr : bb.instrs) {
      out.push_back(&instr);
    }
  }
  return out;
}

KirType pop_type(std::vector<KirType> *stack) {
  if (stack->empty()) {
    return KirType::Any;
  }
  const KirType t = stack->back();
  stack->pop_back();
  return t;
}

KirOpcode width_binop(KirOpcode generic, KirType width) {
  const KirType w = kir_type_normalize(width);
  switch (generic) {
  case KirOpcode::IAdd:
    if (w == KirType::Int32) return KirOpcode::IAdd32;
    if (w == KirType::Int64) return KirOpcode::IAdd64;
    if (w == KirType::Float32) return KirOpcode::FAdd32;
    if (w == KirType::Float64) return KirOpcode::FAdd64;
    break;
  case KirOpcode::ISub:
    if (w == KirType::Int32) return KirOpcode::ISub32;
    if (w == KirType::Int64) return KirOpcode::ISub64;
    if (w == KirType::Float32) return KirOpcode::FSub32;
    if (w == KirType::Float64) return KirOpcode::FSub64;
    break;
  case KirOpcode::IMul:
    if (w == KirType::Int32) return KirOpcode::IMul32;
    if (w == KirType::Int64) return KirOpcode::IMul64;
    if (w == KirType::Float32) return KirOpcode::FMul32;
    if (w == KirType::Float64) return KirOpcode::FMul64;
    break;
  case KirOpcode::IDiv:
    if (w == KirType::Int32) return KirOpcode::IDiv32;
    if (w == KirType::Int64) return KirOpcode::IDiv64;
    if (w == KirType::Float32) return KirOpcode::FDiv32;
    if (w == KirType::Float64) return KirOpcode::FDiv64;
    break;
  case KirOpcode::IMod:
    if (w == KirType::Int32) return KirOpcode::IMod32;
    if (w == KirType::Int64) return KirOpcode::IMod64;
    break;
  default:
    break;
  }
  return generic;
}

void specialize_function(KirFunction &fn) {
  std::vector<KirType> stack;
  std::vector<KirType> locals(static_cast<std::size_t>(fn.param_count), KirType::Any);
  for (int i = 0; i < fn.param_count; ++i) {
    if (static_cast<std::size_t>(i) < fn.param_types.size()) {
      locals[static_cast<std::size_t>(i)] = fn.param_types[static_cast<std::size_t>(i)];
    }
  }

  const std::vector<KirInstr *> linear = linear_mutable_instrs(fn);
  for (std::size_t idx = 0; idx < linear.size(); ++idx) {
    KirInstr *instr = linear[idx];
    switch (instr->op) {
    case KirOpcode::ConstInt:
    case KirOpcode::ConstI32:
    case KirOpcode::ConstI64:
    case KirOpcode::ConstU8:
    case KirOpcode::ConstF32:
    case KirOpcode::ConstF64:
    case KirOpcode::ConstFloat:
      stack.push_back(kir_const_opcode_result_type(instr->op));
      break;
    case KirOpcode::ConstBool:
      stack.push_back(KirType::Bool);
      break;
    case KirOpcode::ConstNull:
      stack.push_back(KirType::Null);
      break;
    case KirOpcode::ConstString:
      stack.push_back(KirType::String);
      break;
    case KirOpcode::LoadLocal: {
      const int slot = instr->operands[0];
      KirType loaded = KirType::Any;
      if (slot >= 0 && static_cast<std::size_t>(slot) < locals.size()) {
        loaded = locals[static_cast<std::size_t>(slot)];
      }
      stack.push_back(loaded);
      break;
    }
    case KirOpcode::StoreLocal: {
      const int slot = instr->operands[0];
      const KirType value = stack.empty() ? KirType::Any : stack.back();
      if (slot >= 0) {
        if (static_cast<std::size_t>(slot) >= locals.size()) {
          locals.resize(static_cast<std::size_t>(slot) + 1, KirType::Any);
        }
        if (locals[static_cast<std::size_t>(slot)] == KirType::Any) {
          locals[static_cast<std::size_t>(slot)] = value;
        } else {
          locals[static_cast<std::size_t>(slot)] =
              kir_type_join(locals[static_cast<std::size_t>(slot)], value);
        }
      }
      break;
    }
    case KirOpcode::Pop:
      pop_type(&stack);
      break;
    case KirOpcode::IAdd:
    case KirOpcode::ISub:
    case KirOpcode::IMul:
    case KirOpcode::IDiv:
    case KirOpcode::IMod: {
      pop_type(&stack);
      pop_type(&stack);
      // Drive specialization off the authoritative per-instruction type that
      // infer_kir_types() computed for this binop (it runs immediately before
      // this pass). The local abstract stack here does not model Call/ConstFn/
      // ArrayNew/etc. and desyncs on any non-trivial operand, so it must not be
      // trusted to derive the operand width — doing so could pick IAdd32 for a
      // string concat and lower a heap handle through a 32-bit integer add.
      KirType width = KirType::Any;
      if (idx < fn.instr_types.size()) {
        width = fn.instr_types[idx];
      }
      if (width != KirType::Any && width != KirType::String) {
        instr->op = width_binop(instr->op, width);
      }
      stack.push_back(width);
      break;
    }
    default:
      break;
    }
  }
}

} // namespace

void specialize_kir_arithmetic(KirModule *module) {
  if (module == nullptr) {
    return;
  }
  for (KirFunction &fn : module->functions) {
    specialize_function(fn);
  }
}

} // namespace kinglet
