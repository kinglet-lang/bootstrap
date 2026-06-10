#include "ir/kir_typing.h"

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
  std::vector<KirType> locals;
};

KirType pop_type(FlowState *state) {
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
  if (lhs == KirType::Float || rhs == KirType::Float) {
    return KirType::Float;
  }
  if (lhs == KirType::Int && rhs == KirType::Int) {
    return KirType::Int;
  }
  if (lhs == KirType::Char && rhs == KirType::Char && op != KirOpcode::IAdd) {
    return KirType::Int;
  }
  return KirType::Any;
}

void infer_function(KirFunction *fn, const KirModule &module) {
  const std::vector<const KirInstr *> linear = linear_instrs(*fn);
  fn->instr_types.assign(linear.size(), KirType::Void);
  fn->local_types.assign(static_cast<std::size_t>(max_local_slot(*fn) + 1), KirType::Any);
  for (int i = 0; i < fn->param_count; ++i) {
    if (static_cast<std::size_t>(i) < fn->param_types.size()) {
      fn->local_types[static_cast<std::size_t>(i)] = fn->param_types[static_cast<std::size_t>(i)];
    }
  }

  FlowState state;
  state.locals = fn->local_types;

  for (std::size_t i = 0; i < linear.size(); ++i) {
    const KirInstr *instr = linear[i];
    KirType result = KirType::Void;

    switch (instr->op) {
    case KirOpcode::ConstInt:
      result = KirType::Int;
      state.stack.push_back(result);
      break;
    case KirOpcode::ConstFloat:
      result = KirType::Float;
      state.stack.push_back(result);
      break;
    case KirOpcode::ConstBool:
      result = KirType::Bool;
      state.stack.push_back(result);
      break;
    case KirOpcode::ConstNull:
      result = KirType::Null;
      state.stack.push_back(result);
      break;
    case KirOpcode::ConstString:
      result = KirType::String;
      state.stack.push_back(result);
      break;
    case KirOpcode::ConstFn:
      result = KirType::Fn;
      state.stack.push_back(result);
      break;
    case KirOpcode::LoadLocal: {
      const int slot = instr->operands[0];
      result = (slot >= 0 && static_cast<std::size_t>(slot) < state.locals.size())
                   ? state.locals[static_cast<std::size_t>(slot)]
                   : KirType::Any;
      state.stack.push_back(result);
      break;
    }
    case KirOpcode::StoreLocal: {
      const int slot = instr->operands[0];
      const KirType value = pop_type(&state);
      if (slot >= 0) {
        if (static_cast<std::size_t>(slot) >= state.locals.size()) {
          state.locals.resize(static_cast<std::size_t>(slot) + 1, KirType::Any);
        }
        state.locals[static_cast<std::size_t>(slot)] =
            kir_type_join(state.locals[static_cast<std::size_t>(slot)], value);
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
    case KirOpcode::IMod: {
      KirType rhs;
      KirType lhs;
      if (instr->operands.size() >= 2) {
        rhs = operand_type(state, fn->instr_types, instr->operands[1]);
        lhs = operand_type(state, fn->instr_types, instr->operands[0]);
      } else {
        rhs = pop_type(&state);
        lhs = pop_type(&state);
      }
      result = arithmetic_type(instr->op, lhs, rhs);
      state.stack.push_back(result);
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
      state.stack.push_back(result);
      break;
    case KirOpcode::Not:
      pop_type(&state);
      result = KirType::Bool;
      state.stack.push_back(result);
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
      state.stack.push_back(result);
      break;
    case KirOpcode::INeg: {
      const KirType inner = pop_type(&state);
      result = inner == KirType::Float ? KirType::Float : KirType::Int;
      if (inner == KirType::Any) {
        result = KirType::Any;
      }
      state.stack.push_back(result);
      break;
    }
    case KirOpcode::Call: {
      const int argc = instr->operands[0];
      for (int arg = 0; arg < argc; ++arg) {
        pop_type(&state);
      }
      pop_type(&state);
      result = KirType::Any;
      state.stack.push_back(result);
      break;
    }
    case KirOpcode::StructNew:
      pop_type(&state);
      result = KirType::Struct;
      state.stack.push_back(result);
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
      state.stack.push_back(result);
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
        pop_type(&state);
        result = KirType::Array;
        state.stack.push_back(result);
      } else if (instr->op == KirOpcode::MapNew) {
        pop_type(&state);
        result = KirType::Map;
        state.stack.push_back(result);
      } else if (instr->op == KirOpcode::ArrayPop || instr->op == KirOpcode::ArrayRemove) {
        pop_type(&state);
        result = KirType::Any;
        state.stack.push_back(result);
      } else if (instr->op == KirOpcode::MapKeys) {
        pop_type(&state);
        result = KirType::Array;
        state.stack.push_back(result);
      } else {
        pop_type(&state);
        if (instr->op == KirOpcode::IndexSet) {
          pop_type(&state);
          pop_type(&state);
        }
      }
      break;
    case KirOpcode::IndexGet: {
      const KirType key = pop_type(&state);
      const KirType object = pop_type(&state);
      if (object == KirType::String && key == KirType::Int) {
        result = KirType::Int;
      } else {
        result = KirType::Any;
      }
      state.stack.push_back(result);
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
      state.stack.push_back(result);
      break;
    case KirOpcode::StrReplace:
    case KirOpcode::StrSplit:
      pop_type(&state);
      pop_type(&state);
      if (instr->op == KirOpcode::StrReplace) {
        pop_type(&state);
      }
      result = instr->op == KirOpcode::StrSplit ? KirType::Array : KirType::String;
      state.stack.push_back(result);
      break;
    case KirOpcode::StrTrim:
    case KirOpcode::StrToUpper:
    case KirOpcode::StrToLower:
      pop_type(&state);
      result = KirType::String;
      state.stack.push_back(result);
      break;
    case KirOpcode::EnumVariant:
      result = KirType::Enum;
      state.stack.push_back(result);
      break;
    case KirOpcode::EnumVariantPayload:
    case KirOpcode::EnumPayloadGet:
      pop_type(&state);
      result = KirType::Any;
      state.stack.push_back(result);
      break;
    case KirOpcode::CastTo:
      pop_type(&state);
      result = kir_cast_target_type(instr->operands.empty() ? -1 : instr->operands[0]);
      state.stack.push_back(result);
      break;
    case KirOpcode::FloatToBits:
      pop_type(&state);
      result = KirType::Int;
      state.stack.push_back(result);
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
      state.stack.push_back(result);
      break;
    }
    case KirOpcode::NativeIn:
    case KirOpcode::NativeInSecret: {
      const int argc = instr->operands[0];
      for (int arg = 0; arg < argc; ++arg) {
        pop_type(&state);
      }
      result = KirType::String;
      state.stack.push_back(result);
      break;
    }
    case KirOpcode::NativeFsRead:
      pop_type(&state);
      result = KirType::String;
      state.stack.push_back(result);
      break;
    case KirOpcode::NativeFsWrite:
      pop_type(&state);
      pop_type(&state);
      result = KirType::Null;
      state.stack.push_back(result);
      break;
    case KirOpcode::NativeSysArgs:
      result = KirType::Array;
      state.stack.push_back(result);
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
