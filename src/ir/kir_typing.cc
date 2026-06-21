// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "ir/kir_typing.h"

#include "ir/kir_container.h"
#include "ir/kir_numeric.h"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace kinglet {

namespace {

std::vector<const KirInstr *> linear_instrs(const KirFunction &fn) {
  std::vector<const KirInstr *> out;
  for (const KirBasicBlock &bb : fn.blocks) {
    for (const KirInstr &instr : bb.instrs) {
      out.push_back(&instr);
    }
  }
  return out;
}

int max_local_slot(const KirFunction &fn) {
  int max_slot = fn.param_count - 1;
  for (const KirBasicBlock &bb : fn.blocks) {
    for (const KirInstr &instr : bb.instrs) {
      if (instr.op == KirOpcode::LoadLocal || instr.op == KirOpcode::StoreLocal) {
        if (!instr.operands.empty()) {
          max_slot = std::max(max_slot, instr.operands[0]);
        }
      }
    }
  }
  return std::max(max_slot, 0);
}

struct FlowState {
  std::vector<KirType> stack;
  std::vector<KirContainerType> container_stack;
  std::vector<KirType> locals;
  std::vector<KirContainerType> local_containers;
};

bool kir_type_is_container(KirType type) {
  return type == KirType::Array || type == KirType::Map;
}

KirContainerType array_container(KirType element_type, const KirContainerType *inner = nullptr) {
  KirContainerType out;
  out.shape = KirContainerShape::Array;
  out.element_type = element_type;
  if (inner != nullptr && kir_type_is_container(element_type)) {
    out.nested_shape = inner->shape;
    out.nested_element_type = inner->element_type;
    out.nested_key_type = inner->key_type;
  }
  return out;
}

KirContainerType map_container(KirType key_type, KirType value_type) {
  KirContainerType out;
  out.shape = KirContainerShape::Map;
  out.key_type = key_type;
  out.element_type = value_type;
  return out;
}

void push_typed(FlowState *state, KirType type, KirContainerType container = {}) {
  state->stack.push_back(type);
  state->container_stack.push_back(container);
}

KirType pop_type(FlowState *state, KirContainerType *container_out = nullptr) {
  KirContainerType container;
  if (!state->container_stack.empty()) {
    container = state->container_stack.back();
    state->container_stack.pop_back();
  }
  if (container_out != nullptr) {
    *container_out = container;
  }
  if (state->stack.empty()) {
    return KirType::Any;
  }
  const KirType t = state->stack.back();
  state->stack.pop_back();
  return t;
}

KirType operand_type(const FlowState &state, const std::vector<KirType> &instr_types, int idx) {
  if (idx < 0 || static_cast<std::size_t>(idx) >= instr_types.size()) {
    return KirType::Any;
  }
  return instr_types[static_cast<std::size_t>(idx)];
}

KirContainerType join_container(KirContainerType a, KirContainerType b) {
  if (a.shape == KirContainerShape::None) {
    return b;
  }
  if (b.shape == KirContainerShape::None) {
    return a;
  }
  if (a.shape != b.shape) {
    KirContainerType out = a;
    out.element_type = KirType::Any;
    out.key_type = KirType::Any;
    out.nested_shape = KirContainerShape::None;
    out.nested_element_type = KirType::Any;
    out.nested_key_type = KirType::Any;
    return out;
  }
  KirContainerType out = a;
  out.element_type = kir_type_join(a.element_type, b.element_type);
  out.key_type = kir_type_join(a.key_type, b.key_type);
  if (a.nested_shape != KirContainerShape::None && b.nested_shape != KirContainerShape::None &&
      a.nested_shape == b.nested_shape) {
    out.nested_element_type = kir_type_join(a.nested_element_type, b.nested_element_type);
    out.nested_key_type = kir_type_join(a.nested_key_type, b.nested_key_type);
  } else if (a.nested_shape != KirContainerShape::None || b.nested_shape != KirContainerShape::None) {
    out.nested_shape = KirContainerShape::None;
    out.nested_element_type = KirType::Any;
    out.nested_key_type = KirType::Any;
  }
  return out;
}

void merge_slot_types(std::vector<KirType> *dst, const std::vector<KirType> &src) {
  if (dst->size() < src.size()) {
    dst->resize(src.size(), KirType::Any);
  }
  for (std::size_t i = 0; i < src.size(); ++i) {
    if (i >= dst->size()) {
      dst->push_back(src[i]);
    } else {
      // Join across control-flow paths. A slot that is Any on either side is
      // Any after the merge (Any is the top type) — never let a concrete type
      // overwrite an Any, or a slot reused for an enum handle and an int gets
      // mis-typed as int32 and its 64-bit handle is truncated on load.
      (*dst)[i] = kir_type_join((*dst)[i], src[i]);
    }
  }
}

void merge_slot_containers(std::vector<KirContainerType> *dst,
                           const std::vector<KirContainerType> &src) {
  if (dst->size() < src.size()) {
    dst->resize(src.size(), KirContainerType{});
  }
  for (std::size_t i = 0; i < src.size(); ++i) {
    if (i >= dst->size()) {
      dst->push_back(src[i]);
    } else {
      (*dst)[i] = join_container((*dst)[i], src[i]);
    }
  }
}

void merge_stack_types(std::vector<KirType> *dst, const std::vector<KirType> &src) {
  const std::size_t depth = std::max(dst->size(), src.size());
  dst->resize(depth, KirType::Any);
  for (std::size_t d = 0; d < depth; ++d) {
    const KirType rhs = d < src.size() ? src[d] : KirType::Any;
    if ((*dst)[d] == KirType::Any) {
      (*dst)[d] = rhs;
    } else {
      (*dst)[d] = kir_type_join((*dst)[d], rhs);
    }
  }
}

void merge_stack_containers(std::vector<KirContainerType> *dst,
                            const std::vector<KirContainerType> &src) {
  const std::size_t depth = std::max(dst->size(), src.size());
  dst->resize(depth, KirContainerType{});
  for (std::size_t d = 0; d < depth; ++d) {
    const KirContainerType rhs = d < src.size() ? src[d] : KirContainerType{};
    (*dst)[d] = join_container((*dst)[d], rhs);
  }
}

void merge_flow_into(FlowState *dst, const FlowState &src) {
  merge_slot_types(&dst->locals, src.locals);
  merge_slot_containers(&dst->local_containers, src.local_containers);
  merge_stack_types(&dst->stack, src.stack);
  merge_stack_containers(&dst->container_stack, src.container_stack);
}

std::size_t block_start_pc(std::size_t pc, const std::set<std::size_t> &leaders) {
  auto it = leaders.upper_bound(pc);
  if (it == leaders.begin()) {
    return 0;
  }
  --it;
  return *it;
}

std::vector<std::size_t> predecessor_blocks(std::size_t leader_pc,
                                            const std::vector<const KirInstr *> &linear,
                                            const std::set<std::size_t> &leaders) {
  std::vector<std::size_t> preds;
  auto add_pred = [&](std::size_t block_pc) {
    if (std::find(preds.begin(), preds.end(), block_pc) == preds.end()) {
      preds.push_back(block_pc);
    }
  };

  std::vector<std::size_t> sim_handlers;
  for (std::size_t j = 0; j < linear.size(); ++j) {
    const KirInstr *jump = linear[j];
    if (jump->op == KirOpcode::PushHandler) {
      sim_handlers.push_back(j + 1 + static_cast<std::size_t>(jump->operands[0]));
    } else if (jump->op == KirOpcode::PopHandler) {
      if (!sim_handlers.empty()) {
        sim_handlers.pop_back();
      }
    }
    if (jump->op == KirOpcode::Br) {
      if (j + 1 + static_cast<std::size_t>(jump->operands[0]) == leader_pc) {
        add_pred(block_start_pc(j, leaders));
      }
    } else if (jump->op == KirOpcode::CondBr || jump->op == KirOpcode::JmpIfErr) {
      if (j + 1 == leader_pc) {
        add_pred(block_start_pc(j, leaders));
      }
      if (j + 1 + static_cast<std::size_t>(jump->operands[0]) == leader_pc) {
        add_pred(block_start_pc(j, leaders));
      }
    } else if (jump->op == KirOpcode::PropagateErr) {
      if (j + 1 == leader_pc) {
        add_pred(block_start_pc(j, leaders));
      }
      if (!sim_handlers.empty() && sim_handlers.back() == leader_pc) {
        add_pred(block_start_pc(j, leaders));
      }
    }
  }

  auto prev_it = leaders.lower_bound(leader_pc);
  if (prev_it != leaders.begin()) {
    --prev_it;
    const std::size_t prev_leader = *prev_it;
    if (prev_leader < leader_pc) {
      bool can_fallthrough = true;
      for (std::size_t j = prev_leader; j < leader_pc; ++j) {
        const KirInstr *instr = linear[j];
        if (instr->op == KirOpcode::Br || instr->op == KirOpcode::Ret ||
            instr->op == KirOpcode::CondBr || instr->op == KirOpcode::JmpIfErr ||
            instr->op == KirOpcode::PropagateErr) {
          can_fallthrough = false;
          break;
        }
      }
      if (can_fallthrough) {
        add_pred(prev_leader);
      }
    }
  }
  return preds;
}

bool is_block_terminator(KirOpcode op) {
  return op == KirOpcode::Br || op == KirOpcode::Ret || op == KirOpcode::CondBr ||
         op == KirOpcode::JmpIfErr || op == KirOpcode::PropagateErr;
}

KirType arithmetic_type(KirOpcode op, KirType lhs, KirType rhs) {
  if (op == KirOpcode::IAdd && (lhs == KirType::String || rhs == KirType::String)) {
    return KirType::String;
  }
  if (kir_type_is_float(lhs) || kir_type_is_float(rhs)) {
    return kir_type_join_numeric(lhs, rhs);
  }
  if (kir_type_is_integer(lhs) && kir_type_is_integer(rhs)) {
    if (lhs == KirType::Char && rhs == KirType::Char && op != KirOpcode::IAdd) {
      return KirType::Int;
    }
    return kir_type_join_numeric(lhs, rhs);
  }
  return KirType::Any;
}

void infer_function(KirFunction *fn, const KirModule &module) {
  const std::vector<const KirInstr *> linear = linear_instrs(*fn);
  fn->instr_types.assign(linear.size(), KirType::Void);
  fn->local_types.assign(static_cast<std::size_t>(max_local_slot(*fn) + 1), KirType::Any);
  fn->slot_containers.assign(fn->local_types.size(), KirContainerType{});
  for (int i = 0; i < fn->param_count; ++i) {
    if (static_cast<std::size_t>(i) < fn->param_types.size()) {
      fn->local_types[static_cast<std::size_t>(i)] = fn->param_types[static_cast<std::size_t>(i)];
    }
    if (static_cast<std::size_t>(i) < fn->slot_containers.size()) {
      // slot_containers may already be filled from checker for params.
    }
  }

  FlowState entry_state;
  entry_state.locals = fn->local_types;
  entry_state.local_containers = fn->slot_containers;
  if (entry_state.local_containers.size() < entry_state.locals.size()) {
    entry_state.local_containers.resize(entry_state.locals.size(), KirContainerType{});
  }

  std::set<std::size_t> leaders;
  leaders.insert(0);
  for (std::size_t i = 0; i < linear.size(); ++i) {
    const KirInstr *instr = linear[i];
    if (instr->op == KirOpcode::PushHandler) {
      if (!instr->operands.empty()) {
        leaders.insert(i + 1 + static_cast<std::size_t>(instr->operands[0]));
      }
    } else if (instr->op == KirOpcode::Br || instr->op == KirOpcode::CondBr ||
               instr->op == KirOpcode::JmpIfErr) {
      if (!instr->operands.empty()) {
        const int rel = instr->operands[0];
        const int target = static_cast<int>(i) + 1 + rel;
        if (target >= 0 && static_cast<std::size_t>(target) <= linear.size()) {
          leaders.insert(static_cast<std::size_t>(target));
        }
        if (instr->op == KirOpcode::CondBr || instr->op == KirOpcode::JmpIfErr) {
          leaders.insert(i + 1);
        }
      }
    } else if (instr->op == KirOpcode::PropagateErr) {
      leaders.insert(i + 1);
    }
  }

  std::map<std::size_t, FlowState> exit_states;
  FlowState state = entry_state;

  auto merge_at_leader = [&](std::size_t leader_pc) {
    if (leader_pc == 0) {
      state = entry_state;
      state.stack.clear();
      state.container_stack.clear();
      return;
    }
    const std::vector<std::size_t> preds = predecessor_blocks(leader_pc, linear, leaders);
    if (preds.empty()) {
      state.stack.clear();
      state.container_stack.clear();
      return;
    }
    bool have_state = false;
    FlowState merged;
    for (std::size_t pred : preds) {
      const auto it = exit_states.find(pred);
      if (it == exit_states.end()) {
        continue;
      }
      if (!have_state) {
        merged = it->second;
        have_state = true;
      } else {
        merge_flow_into(&merged, it->second);
      }
    }
    if (!have_state) {
      state.stack.clear();
      state.container_stack.clear();
      return;
    }
    state = std::move(merged);
  };

  for (std::size_t i = 0; i < linear.size(); ++i) {
    const KirInstr *instr = linear[i];
    const std::size_t current_block = block_start_pc(i, leaders);
    if (leaders.count(i) > 0) {
      if (i > 0) {
        const std::size_t prev_block = block_start_pc(i - 1, leaders);
        if (prev_block != current_block && !is_block_terminator(linear[i - 1]->op)) {
          exit_states[prev_block] = state;
        }
      }
      merge_at_leader(i);
    }
    KirType result = KirType::Void;

    switch (instr->op) {
    case KirOpcode::ConstInt:
    case KirOpcode::ConstI32:
    case KirOpcode::ConstI64:
    case KirOpcode::ConstU8:
    case KirOpcode::ConstF32:
    case KirOpcode::ConstF64:
      result = kir_const_opcode_result_type(instr->op);
      push_typed(&state, result);
      break;
    case KirOpcode::ConstFloat:
      result = KirType::Float;
      push_typed(&state, result);
      break;
    case KirOpcode::ConstBool:
      result = KirType::Bool;
      push_typed(&state, result);
      break;
    case KirOpcode::ConstNull:
      result = KirType::Null;
      push_typed(&state, result);
      break;
    case KirOpcode::ConstString:
      result = KirType::String;
      push_typed(&state, result);
      break;
    case KirOpcode::ConstFn:
      result = KirType::Fn;
      push_typed(&state, result);
      break;
    case KirOpcode::LoadLocal: {
      const int slot = instr->operands[0];
      KirContainerType container;
      result = KirType::Any;
      if (slot >= 0 && static_cast<std::size_t>(slot) < state.locals.size()) {
        result = state.locals[static_cast<std::size_t>(slot)];
        if (static_cast<std::size_t>(slot) < state.local_containers.size()) {
          container = state.local_containers[static_cast<std::size_t>(slot)];
        }
      }
      push_typed(&state, result, container);
      break;
    }
    case KirOpcode::LoadLocalAddr:
      result = KirType::Int64;
      push_typed(&state, result);
      break;
    case KirOpcode::DerefLoad:
      pop_type(&state);
      result = KirType::Any;
      push_typed(&state, result);
      break;
    case KirOpcode::DerefStore:
      pop_type(&state);
      pop_type(&state);
      result = KirType::Null;
      push_typed(&state, result);
      break;
    case KirOpcode::StoreLocal: {
      const int slot = instr->operands[0];
      const KirType value = state.stack.empty() ? KirType::Any : state.stack.back();
      const KirContainerType container =
          state.container_stack.empty() ? KirContainerType{} : state.container_stack.back();
      if (slot >= 0) {
        if (static_cast<std::size_t>(slot) >= state.locals.size()) {
          state.locals.resize(static_cast<std::size_t>(slot) + 1, KirType::Any);
        }
        if (static_cast<std::size_t>(slot) >= state.local_containers.size()) {
          state.local_containers.resize(static_cast<std::size_t>(slot) + 1, KirContainerType{});
        }
        if (state.locals[static_cast<std::size_t>(slot)] == KirType::Any) {
          state.locals[static_cast<std::size_t>(slot)] = value;
        } else {
          state.locals[static_cast<std::size_t>(slot)] =
              kir_type_join(state.locals[static_cast<std::size_t>(slot)], value);
        }
        state.local_containers[static_cast<std::size_t>(slot)] = join_container(
            state.local_containers[static_cast<std::size_t>(slot)], container);
      }
      break;
    }
    case KirOpcode::Pop:
      pop_type(&state);
      break;
    case KirOpcode::IAdd:
    case KirOpcode::ISub:
    case KirOpcode::IMul:
    case KirOpcode::IDiv:
    case KirOpcode::IMod:
    case KirOpcode::IAdd32:
    case KirOpcode::IAdd64:
    case KirOpcode::ISub32:
    case KirOpcode::ISub64:
    case KirOpcode::IMul32:
    case KirOpcode::IMul64:
    case KirOpcode::IDiv32:
    case KirOpcode::IDiv64:
    case KirOpcode::IMod32:
    case KirOpcode::IMod64:
    case KirOpcode::FAdd32:
    case KirOpcode::FAdd64:
    case KirOpcode::FSub32:
    case KirOpcode::FSub64:
    case KirOpcode::FMul32:
    case KirOpcode::FMul64:
    case KirOpcode::FDiv32:
    case KirOpcode::FDiv64: {
      KirType rhs;
      KirType lhs;
      if (instr->operands.size() >= 2) {
        rhs = operand_type(state, fn->instr_types, instr->operands[1]);
        lhs = operand_type(state, fn->instr_types, instr->operands[0]);
      } else {
        rhs = pop_type(&state);
        lhs = pop_type(&state);
      }
      {
        const KirBinopSpec spec = kir_binop_spec(instr->op);
        const KirOpcode generic = spec.specialized ? spec.generic : instr->op;
        result = arithmetic_type(generic, lhs, rhs);
        if (spec.specialized && result == KirType::Any) {
          result = spec.width;
        }
      }
      push_typed(&state, result);
      break;
    }
    case KirOpcode::ICmpEq:
    case KirOpcode::ICmpNeq:
    case KirOpcode::ICmpLt:
    case KirOpcode::ICmpGt:
    case KirOpcode::ICmpLe:
    case KirOpcode::ICmpGe:
      pop_type(&state);
      pop_type(&state);
      result = KirType::Bool;
      push_typed(&state, result);
      break;
    case KirOpcode::Not:
      pop_type(&state);
      result = KirType::Bool;
      push_typed(&state, result);
      break;
    case KirOpcode::BitNot:
    case KirOpcode::BitAnd:
    case KirOpcode::BitOr:
    case KirOpcode::BitXor:
    case KirOpcode::Shl:
    case KirOpcode::Shr:
      pop_type(&state);
      if (instr->operands.size() >= 2) {
        pop_type(&state);
      }
      result = KirType::Int;
      push_typed(&state, result);
      break;
    case KirOpcode::INeg: {
      const KirType inner = pop_type(&state);
      result = inner == KirType::Float ? KirType::Float : KirType::Int;
      if (inner == KirType::Any) {
        result = KirType::Any;
      }
      push_typed(&state, result);
      break;
    }
    case KirOpcode::Call: {
      const int argc = instr->operands[0];
      for (int arg = 0; arg < argc; ++arg) {
        pop_type(&state);
      }
      pop_type(&state);
      result = KirType::Any;
      push_typed(&state, result);
      break;
    }
    case KirOpcode::StructNew: {
      const int packed = instr->operands[0];
      const int field_count = packed & 0xFFFF;
      for (int fi = 0; fi < field_count; ++fi) {
        pop_type(&state);
      }
      result = KirType::Struct;
      push_typed(&state, result);
      break;
    }
    case KirOpcode::BorrowFieldMut: {
      pop_type(&state);
      result = KirType::Int64;
      push_typed(&state, result);
      break;
    }
    case KirOpcode::FieldGet: {
      pop_type(&state);
      const int pool_idx = instr->operands[0];
      result = KirType::Any;
      if (pool_idx >= 0 && static_cast<std::size_t>(pool_idx) < module.constant_strings.size()) {
        const std::string &field_name = module.constant_strings[static_cast<std::size_t>(pool_idx)];
        KirType found = KirType::Any;
        int matches = 0;
        for (const KirStructMeta &meta : module.struct_metas) {
          for (std::size_t fi = 0; fi < meta.field_names.size(); ++fi) {
            if (meta.field_names[fi] == field_name && fi < meta.field_types.size()) {
              found = meta.field_types[fi];
              ++matches;
              break;
            }
          }
        }
        if (matches == 1) {
          result = found;
        }
      }
      if (result == KirType::Void) {
        result = KirType::Any;
      }
      push_typed(&state, result);
      break;
    }
    case KirOpcode::FieldSet:
      pop_type(&state);
      pop_type(&state);
      pop_type(&state);
      break;
    case KirOpcode::ArraySlice: {
      // Stack: [object, start, end] -> slice. The result is the same kind as
      // the sliced object (a string slice is a String, an array slice an
      // Array); critically it must NOT inherit a stale Int type, or a slot
      // holding the slice would be mistyped and later string-concat / display
      // lowering would format the heap handle as an integer.
      pop_type(&state); // end
      pop_type(&state); // start
      KirContainerType object_container;
      const KirType object = pop_type(&state, &object_container);
      if (object == KirType::String) {
        result = KirType::String;
        push_typed(&state, result);
      } else {
        result = KirType::Array;
        push_typed(&state, result, object_container);
      }
      break;
    }
    case KirOpcode::ArrayNew:
    case KirOpcode::DenseArrayNew:
    case KirOpcode::ArrayPush:
    case KirOpcode::ArrayResize:
    case KirOpcode::ArrayPop:
    case KirOpcode::ArrayRemove:
    case KirOpcode::ArrayClear:
    case KirOpcode::ArrayInsert:
    case KirOpcode::ArrayReverse:
    case KirOpcode::MapNew:
    case KirOpcode::MapKeys:
    case KirOpcode::IndexSet:
      while (!state.stack.empty() && instr->op != KirOpcode::ArrayPop) {
        // Operand count varies; conservative pop handled per-op below.
        break;
      }
      if (instr->op == KirOpcode::ArrayNew) {
        const int element_count = instr->operands.empty() ? 0 : instr->operands[0];
        KirType element_type = KirType::Any;
        const KirContainerType *element_container = nullptr;
        KirContainerType first_elem_container;
        for (int ei = 0; ei < element_count; ++ei) {
          KirContainerType elem_container;
          const KirType elem = pop_type(&state, &elem_container);
          if (element_type == KirType::Any) {
            element_type = elem;
            first_elem_container = elem_container;
            element_container = &first_elem_container;
          } else {
            element_type = kir_type_join(element_type, elem);
          }
        }
        result = KirType::Array;
        push_typed(&state, result, array_container(element_type, element_container));
      } else if (instr->op == KirOpcode::DenseArrayNew) {
        const int rows = instr->operands.size() > 0 ? instr->operands[0] : 0;
        const int cols = instr->operands.size() > 1 ? instr->operands[1] : 0;
        const int element_count = rows > 0 && cols > 0 ? rows * cols : 0;
        KirType element_type = KirType::Any;
        KirContainerType first_elem_container;
        for (int ei = 0; ei < element_count; ++ei) {
          KirContainerType elem_container;
          const KirType elem = pop_type(&state, &elem_container);
          if (element_type == KirType::Any) {
            element_type = elem;
            first_elem_container = elem_container;
          } else {
            element_type = kir_type_join(element_type, elem);
          }
        }
        KirContainerType inner = array_container(element_type, nullptr);
        result = KirType::Array;
        push_typed(&state, result, array_container(KirType::Array, &inner));
      } else if (instr->op == KirOpcode::MapNew) {
        const int entry_count = instr->operands.empty() ? 0 : instr->operands[0];
        KirType key_type = KirType::Any;
        KirType value_type = KirType::Any;
        KirContainerType first_value_container;
        for (int ei = 0; ei < entry_count; ++ei) {
          KirContainerType val_container;
          const KirType value = pop_type(&state, &val_container);
          const KirType key = pop_type(&state);
          if (key_type == KirType::Any) {
            key_type = key;
          } else {
            key_type = kir_type_join(key_type, key);
          }
          if (value_type == KirType::Any) {
            value_type = value;
            first_value_container = val_container;
          } else {
            value_type = kir_type_join(value_type, value);
          }
        }
        result = KirType::Map;
        KirContainerType map_cont = map_container(key_type, value_type);
        if (value_type != KirType::Any && kir_type_is_container(value_type)) {
          map_cont.nested_shape = first_value_container.shape;
          map_cont.nested_element_type = first_value_container.element_type;
          map_cont.nested_key_type = first_value_container.key_type;
        }
        push_typed(&state, result, map_cont);
      } else if (instr->op == KirOpcode::ArrayPop || instr->op == KirOpcode::ArrayRemove) {
        KirContainerType container;
        pop_type(&state, &container);
        result = kir_container_is_array(container) ? container.element_type : KirType::Any;
        push_typed(&state, result);
      } else if (instr->op == KirOpcode::MapKeys) {
        pop_type(&state);
        result = KirType::Array;
        push_typed(&state, result);
      } else {
        pop_type(&state);
        if (instr->op == KirOpcode::IndexSet) {
          pop_type(&state);
          pop_type(&state);
        }
      }
      break;
    case KirOpcode::IndexGet: {
      KirContainerType key_container;
      const KirType key = pop_type(&state, &key_container);
      KirContainerType object_container;
      const KirType object = pop_type(&state, &object_container);
      KirContainerType result_container;
      if (kir_container_is_array(object_container) &&
          object_container.element_type != KirType::Any) {
        result = object_container.element_type;
        if (kir_type_is_container(result) && object_container.nested_shape != KirContainerShape::None) {
          result_container = kir_container_peel_element(object_container);
        }
      } else if (kir_container_is_map(object_container) &&
                 object_container.element_type != KirType::Any) {
        result = object_container.element_type;
        if (kir_type_is_container(result) && object_container.nested_shape != KirContainerShape::None) {
          result_container = kir_container_peel_element(object_container);
        }
      } else if (object == KirType::String &&
                 (key == KirType::Int || key == KirType::Int32 || key == KirType::Char)) {
        result = KirType::Int8;
      } else {
        result = KirType::Any;
      }
      if (kir_type_is_container(result) && result_container.shape != KirContainerShape::None) {
        push_typed(&state, result, result_container);
      } else if (kir_type_is_container(result)) {
        push_typed(&state, result);
      } else {
        push_typed(&state, result);
      }
      break;
    }
    case KirOpcode::ArrayLen:
    case KirOpcode::ArrayContains:
    case KirOpcode::ArrayIndexOf:
    case KirOpcode::MapHas:
    case KirOpcode::StrStartsWith:
    case KirOpcode::StrEndsWith:
      pop_type(&state);
      if (instr->op != KirOpcode::ArrayLen && instr->op != KirOpcode::MapHas) {
        pop_type(&state);
      }
      result = KirType::Bool;
      if (instr->op == KirOpcode::ArrayLen) {
        result = KirType::Int;
      }
      if (instr->op == KirOpcode::ArrayIndexOf) {
        result = KirType::Int;
      }
      push_typed(&state, result);
      break;
    case KirOpcode::StrReplace:
    case KirOpcode::StrSplit:
      pop_type(&state);
      pop_type(&state);
      if (instr->op == KirOpcode::StrReplace) {
        pop_type(&state);
      }
      result = instr->op == KirOpcode::StrSplit ? KirType::Array : KirType::String;
      push_typed(&state, result);
      break;
    case KirOpcode::StrTrim:
    case KirOpcode::StrToUpper:
    case KirOpcode::StrToLower:
      pop_type(&state);
      result = KirType::String;
      push_typed(&state, result);
      break;
    case KirOpcode::EnumVariant:
      result = KirType::Enum;
      push_typed(&state, result);
      break;
    case KirOpcode::EnumVariantPayload:
    case KirOpcode::EnumPayloadGet:
      pop_type(&state);
      result = KirType::Any;
      push_typed(&state, result);
      break;
    case KirOpcode::CastTo:
      pop_type(&state);
      result = kir_cast_target_type(instr->operands.empty() ? -1 : instr->operands[0]);
      push_typed(&state, result);
      break;
    case KirOpcode::FloatToBits:
      pop_type(&state);
      result = KirType::Int;
      push_typed(&state, result);
      break;
    case KirOpcode::BitsToFloat:
      pop_type(&state);
      result = KirType::Float;
      push_typed(&state, result);
      break;
    case KirOpcode::NativeOut:
    case KirOpcode::NativeOutLn:
    case KirOpcode::NativeErr:
    case KirOpcode::NativeErrLn: {
      const int argc = instr->operands[0];
      for (int arg = 0; arg < argc; ++arg) {
        pop_type(&state);
      }
      result = KirType::Null;
      push_typed(&state, result);
      break;
    }
    case KirOpcode::NativeIn:
    case KirOpcode::NativeInSecret: {
      const int argc = instr->operands[0];
      for (int arg = 0; arg < argc; ++arg) {
        pop_type(&state);
      }
      result = KirType::String;
      push_typed(&state, result);
      break;
    }
    case KirOpcode::NativeFsRead:
      pop_type(&state);
      result = KirType::String;
      push_typed(&state, result);
      break;
    case KirOpcode::NativeFsWrite:
      pop_type(&state);
      pop_type(&state);
      result = KirType::Null;
      push_typed(&state, result);
      break;
    case KirOpcode::NativeSysArgs:
      result = KirType::Array;
      push_typed(&state, result);
      break;
    case KirOpcode::CondBr:
      pop_type(&state);
      break;
    case KirOpcode::Ret:
    case KirOpcode::Br:
    case KirOpcode::JmpIfErr:
    case KirOpcode::PushHandler:
    case KirOpcode::PopHandler:
    case KirOpcode::PropagateErr:
    case KirOpcode::Switch:
    case KirOpcode::Unreachable:
    case KirOpcode::Nop:
      break;
    }

    fn->instr_types[i] = result;

    if (is_block_terminator(instr->op)) {
      exit_states[current_block] = state;
    }
  }

  for (const auto &[block_pc, exit_state] : exit_states) {
    (void)block_pc;
    merge_flow_into(&entry_state, exit_state);
  }
  fn->local_types = std::move(entry_state.locals);
  fn->slot_containers = std::move(entry_state.local_containers);
}

} // namespace

void infer_kir_types(KirModule *module) {
  if (module == nullptr) {
    return;
  }
  for (KirFunction &fn : module->functions) {
    infer_function(&fn, *module);
  }
}

} // namespace kinglet
