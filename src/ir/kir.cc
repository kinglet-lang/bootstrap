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
  case KirOpcode::Not:
    return "not";
  case KirOpcode::BitNot:
    return "bit_not";
  case KirOpcode::BitAnd:
    return "bit_and";
  case KirOpcode::BitOr:
    return "bit_or";
  case KirOpcode::BitXor:
    return "bit_xor";
  case KirOpcode::Shl:
    return "shl";
  case KirOpcode::Shr:
    return "shr";
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
  case KirOpcode::JmpIfErr:
    return "jmp_if_err";
  case KirOpcode::PushHandler:
    return "push_handler";
  case KirOpcode::PopHandler:
    return "pop_handler";
  case KirOpcode::PropagateErr:
    return "propagate_err";
  case KirOpcode::Switch:
    return "switch";
  case KirOpcode::StructNew:
    return "struct_new";
  case KirOpcode::FieldGet:
    return "field_get";
  case KirOpcode::FieldSet:
    return "field_set";
  case KirOpcode::ArrayNew:
    return "array_new";
  case KirOpcode::IndexGet:
    return "index_get";
  case KirOpcode::IndexSet:
    return "index_set";
  case KirOpcode::ArrayLen:
    return "array_len";
  case KirOpcode::ArraySlice:
    return "array_slice";
  case KirOpcode::ArrayPush:
    return "array_push";
  case KirOpcode::ArrayResize:
    return "array_resize";
  case KirOpcode::ArrayPop:
    return "array_pop";
  case KirOpcode::ArrayRemove:
    return "array_remove";
  case KirOpcode::ArrayContains:
    return "array_contains";
  case KirOpcode::ArrayClear:
    return "array_clear";
  case KirOpcode::ArrayInsert:
    return "array_insert";
  case KirOpcode::ArrayIndexOf:
    return "array_index_of";
  case KirOpcode::ArrayReverse:
    return "array_reverse";
  case KirOpcode::StrStartsWith:
    return "str_starts_with";
  case KirOpcode::StrEndsWith:
    return "str_ends_with";
  case KirOpcode::StrReplace:
    return "str_replace";
  case KirOpcode::StrSplit:
    return "str_split";
  case KirOpcode::StrTrim:
    return "str_trim";
  case KirOpcode::StrToUpper:
    return "str_to_upper";
  case KirOpcode::StrToLower:
    return "str_to_lower";
  case KirOpcode::MapNew:
    return "map_new";
  case KirOpcode::MapHas:
    return "map_has";
  case KirOpcode::MapKeys:
    return "map_keys";
  case KirOpcode::EnumVariant:
    return "enum_variant";
  case KirOpcode::EnumVariantPayload:
    return "enum_variant_payload";
  case KirOpcode::EnumPayloadGet:
    return "enum_payload_get";
  case KirOpcode::CastTo:
    return "cast_to";
  case KirOpcode::FloatToBits:
    return "float_to_bits";
  case KirOpcode::NativeOut:
    return "native_out";
  case KirOpcode::NativeOutLn:
    return "native_out_ln";
  case KirOpcode::NativeErr:
    return "native_err";
  case KirOpcode::NativeErrLn:
    return "native_err_ln";
  case KirOpcode::NativeIn:
    return "native_in";
  case KirOpcode::NativeInSecret:
    return "native_in_secret";
  case KirOpcode::NativeFsRead:
    return "native_fs_read";
  case KirOpcode::NativeFsWrite:
    return "native_fs_write";
  case KirOpcode::NativeSysArgs:
    return "native_sys_args";
  case KirOpcode::INeg:
    return "ineg";
  case KirOpcode::Unreachable:
    return "unreachable";
  case KirOpcode::Nop:
    return "nop";
  }
  return "unknown";
}

const char *kir_type_name(KirType type) {
  switch (type) {
  case KirType::Void:
    return "void";
  case KirType::Any:
    return "any";
  case KirType::Int:
    return "int";
  case KirType::Float:
    return "float";
  case KirType::Bool:
    return "bool";
  case KirType::Null:
    return "null";
  case KirType::Char:
    return "char";
  case KirType::String:
    return "string";
  case KirType::Array:
    return "array";
  case KirType::Map:
    return "map";
  case KirType::Struct:
    return "struct";
  case KirType::Enum:
    return "enum";
  case KirType::Fn:
    return "fn";
  }
  return "unknown";
}

KirType kir_type_join(KirType a, KirType b) {
  if (a == b) {
    return a;
  }
  if (a == KirType::Void) {
    return b;
  }
  if (b == KirType::Void) {
    return a;
  }
  if (a == KirType::Any || b == KirType::Any) {
    return KirType::Any;
  }
  if ((a == KirType::Int && b == KirType::Char) || (a == KirType::Char && b == KirType::Int)) {
    return KirType::Int;
  }
  return KirType::Any;
}

bool kir_type_is_scalar(KirType type) {
  switch (type) {
  case KirType::Int:
  case KirType::Float:
  case KirType::Bool:
  case KirType::Null:
  case KirType::Char:
    return true;
  default:
    return false;
  }
}

KirType kir_cast_target_type(int kind) {
  switch (kind) {
  case 0:
    return KirType::Int;
  case 1:
    return KirType::Float;
  case 2:
    return KirType::String;
  case 3:
    return KirType::Char;
  default:
    return KirType::Any;
  }
}

bool kir_type_is_heap(KirType type) {
  switch (type) {
  case KirType::String:
  case KirType::Array:
  case KirType::Map:
  case KirType::Struct:
  case KirType::Enum:
  case KirType::Fn:
    return true;
  default:
    return false;
  }
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
    if (i < function.param_types.size()) {
      out << ": " << kir_type_name(function.param_types[i]);
    }
  }
  if (function.return_type != KirType::Any) {
    out << ") -> " << kir_type_name(function.return_type);
  } else {
    out << ')';
  }
  out << " {\n";

  int instr_idx = 0;
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
      if (static_cast<std::size_t>(instr_idx) < function.instr_types.size() &&
          function.instr_types[static_cast<std::size_t>(instr_idx)] != KirType::Void) {
        out << " : " << kir_type_name(function.instr_types[static_cast<std::size_t>(instr_idx)]);
      }
      out << '\n';
      ++instr_idx;
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
