#include "ir/kir.h"

#include "ir/kir_numeric.h"

#include <sstream>

namespace kinglet {

const char *kir_opcode_name(KirOpcode op) {
  switch (op) {
  case KirOpcode::ConstInt:
    return "const_int";
  case KirOpcode::ConstI32:
    return "const_i32";
  case KirOpcode::ConstI64:
    return "const_i64";
  case KirOpcode::ConstU8:
    return "const_u8";
  case KirOpcode::ConstF32:
    return "const_f32";
  case KirOpcode::ConstF64:
    return "const_f64";
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
  case KirOpcode::IAdd32:
    return "iadd32";
  case KirOpcode::IAdd64:
    return "iadd64";
  case KirOpcode::ISub32:
    return "isub32";
  case KirOpcode::ISub64:
    return "isub64";
  case KirOpcode::IMul32:
    return "imul32";
  case KirOpcode::IMul64:
    return "imul64";
  case KirOpcode::IDiv32:
    return "idiv32";
  case KirOpcode::IDiv64:
    return "idiv64";
  case KirOpcode::IMod32:
    return "imod32";
  case KirOpcode::IMod64:
    return "imod64";
  case KirOpcode::FAdd32:
    return "fadd32";
  case KirOpcode::FAdd64:
    return "fadd64";
  case KirOpcode::FSub32:
    return "fsub32";
  case KirOpcode::FSub64:
    return "fsub64";
  case KirOpcode::FMul32:
    return "fmul32";
  case KirOpcode::FMul64:
    return "fmul64";
  case KirOpcode::FDiv32:
    return "fdiv32";
  case KirOpcode::FDiv64:
    return "fdiv64";
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
  case KirOpcode::DenseArrayNew:
    return "dense_array_new";
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
  case KirType::Int8:
    return "int8";
  case KirType::Int16:
    return "int16";
  case KirType::Int32:
    return "int32";
  case KirType::Int64:
    return "int64";
  case KirType::UInt8:
    return "uint8";
  case KirType::UInt16:
    return "uint16";
  case KirType::UInt32:
    return "uint32";
  case KirType::UInt64:
    return "uint64";
  case KirType::Float32:
    return "float32";
  case KirType::Float64:
    return "float64";
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
  if ((kir_type_is_integer(a) && b == KirType::Char) ||
      (a == KirType::Char && kir_type_is_integer(b))) {
    return kir_type_join_numeric(kir_type_normalize(a == KirType::Char ? KirType::Int8 : a),
                                 kir_type_normalize(b == KirType::Char ? KirType::Int8 : b));
  }
  if (kir_type_is_integer(a) && kir_type_is_integer(b)) {
    return kir_type_join_numeric(a, b);
  }
  if (kir_type_is_float(a) && kir_type_is_float(b)) {
    return kir_type_join_numeric(a, b);
  }
  return KirType::Any;
}

bool kir_type_is_scalar(KirType type) {
  return kir_type_is_integer(type) || kir_type_is_float(type) || type == KirType::Bool ||
         type == KirType::Null || type == KirType::Char;
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
                              instr.op == KirOpcode::ConstI32 ||
                              instr.op == KirOpcode::ConstI64 ||
                              instr.op == KirOpcode::ConstU8 ||
                              instr.op == KirOpcode::ConstF32 ||
                              instr.op == KirOpcode::ConstF64 ||
                              instr.op == KirOpcode::ConstFloat ||
                              instr.op == KirOpcode::ConstBool ||
                              instr.op == KirOpcode::ConstNull ||
                              instr.op == KirOpcode::ConstString ||
                              instr.op == KirOpcode::LoadLocal ||
                              instr.op == KirOpcode::IAdd || instr.op == KirOpcode::ISub ||
                              instr.op == KirOpcode::IMul || instr.op == KirOpcode::IDiv ||
                              instr.op == KirOpcode::IMod || instr.op == KirOpcode::IAdd32 ||
                              instr.op == KirOpcode::IAdd64 || instr.op == KirOpcode::ISub32 ||
                              instr.op == KirOpcode::ISub64 || instr.op == KirOpcode::IMul32 ||
                              instr.op == KirOpcode::IMul64 || instr.op == KirOpcode::IDiv32 ||
                              instr.op == KirOpcode::IDiv64 || instr.op == KirOpcode::IMod32 ||
                              instr.op == KirOpcode::IMod64 || instr.op == KirOpcode::FAdd32 ||
                              instr.op == KirOpcode::FAdd64 || instr.op == KirOpcode::FSub32 ||
                              instr.op == KirOpcode::FSub64 || instr.op == KirOpcode::FMul32 ||
                              instr.op == KirOpcode::FMul64 || instr.op == KirOpcode::FDiv32 ||
                              instr.op == KirOpcode::FDiv64 || instr.op == KirOpcode::ICmpEq ||
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
