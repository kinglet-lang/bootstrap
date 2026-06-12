#include "ir/kir_typing.h"

#include "ir/kir_container.h"
#include "ir/kir_numeric.h"

#include <algorithm>
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

  FlowState state;
  state.locals = fn->local_types;
  state.local_containers = fn->slot_containers;
  if (state.local_containers.size() < state.locals.size()) {
    state.local_containers.resize(state.locals.size(), KirContainerType{});
  }

  for (std::size_t i = 0; i < linear.size(); ++i) {
    const KirInstr *instr = linear[i];
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
        if (state.local_containers[static_cast<std::size_t>(slot)].shape ==
            KirContainerShape::None) {
          state.local_containers[static_cast<std::size_t>(slot)] = container;
        }
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
    case KirOpcode::StructNew:
      pop_type(&state);
      result = KirType::Struct;
      push_typed(&state, result);
      break;
    case KirOpcode::FieldGet: {
      pop_type(&state);
      const int pool_idx = instr->operands[0];
      if (pool_idx >= 0 && static_cast<std::size_t>(pool_idx) < module.constant_strings.size()) {
        const std::string &field_name = module.constant_strings[static_cast<std::size_t>(pool_idx)];
        for (const KirStructMeta &meta : module.struct_metas) {
          for (std::size_t fi = 0; fi < meta.field_names.size(); ++fi) {
            if (meta.field_names[fi] == field_name &&
                fi < meta.field_types.size()) {
              result = meta.field_types[fi];
              break;
            }
          }
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
    case KirOpcode::ArrayNew:
    case KirOpcode::ArraySlice:
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
  }

  fn->local_types = std::move(state.locals);
  fn->slot_containers = std::move(state.local_containers);
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
