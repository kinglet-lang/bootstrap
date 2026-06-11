#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kinglet {

// Flat KIR type for native lowering. Scalar types lower to raw LLVM registers;
// heap/reference types use the kl_h wire format.
enum class KirType : uint8_t {
  Void,
  Any,
  Int, // alias: Int64
  Float, // alias: Float32
  Int8,
  Int16,
  Int32,
  Int64,
  UInt8,
  UInt16,
  UInt32,
  UInt64,
  Float32,
  Float64,
  Bool,
  Null,
  Char,
  String,
  Array,
  Map,
  Struct,
  Enum,
  Fn,
};

struct KirFunctionSig {
  std::vector<KirType> param_types;
  KirType return_type = KirType::Any;
};

enum class KirOpcode {
  ConstInt,
  ConstI32,
  ConstI64,
  ConstU8,
  ConstF32,
  ConstF64,
  ConstFloat,
  ConstBool,
  ConstNull,
  ConstString,
  LoadLocal,
  StoreLocal,
  Pop,
  IAdd,
  ISub,
  IMul,
  IDiv,
  IMod,
  IAdd32,
  IAdd64,
  ISub32,
  ISub64,
  IMul32,
  IMul64,
  IDiv32,
  IDiv64,
  IMod32,
  IMod64,
  FAdd32,
  FAdd64,
  FSub32,
  FSub64,
  FMul32,
  FMul64,
  FDiv32,
  FDiv64,
  Not,
  BitNot,
  BitAnd,
  BitOr,
  BitXor,
  Shl,
  Shr,
  ICmpEq,
  ICmpNeq,
  ICmpLt,
  ICmpGt,
  ICmpLe,
  ICmpGe,
  ConstFn,
  Call,
  Ret,
  Br,
  CondBr,
  JmpIfErr,
  PushHandler,
  PopHandler,
  PropagateErr,
  Switch,
  StructNew,
  FieldGet,
  FieldSet,
  ArrayNew,
  ArraySlice,
  IndexGet,
  IndexSet,
  ArrayLen,
  ArrayPush,
  ArrayResize,
  ArrayPop,
  ArrayRemove,
  ArrayContains,
  ArrayClear,
  ArrayInsert,
  ArrayIndexOf,
  ArrayReverse,
  StrStartsWith,
  StrEndsWith,
  StrReplace,
  StrSplit,
  StrTrim,
  StrToUpper,
  StrToLower,
  MapNew,
  MapHas,
  MapKeys,
  EnumVariant,
  EnumVariantPayload,
  EnumPayloadGet,
  CastTo,
  FloatToBits,
  NativeOut,
  NativeOutLn,
  NativeErr,
  NativeErrLn,
  NativeIn,
  NativeInSecret,
  NativeFsRead,
  NativeFsWrite,
  NativeSysArgs,
  INeg,
  Unreachable,
  Nop,
};

struct KirStructMeta {
  std::string name;
  std::vector<std::string> field_names;
  std::vector<KirType> field_types;
};

struct KirEnumMeta {
  std::string name;
  std::vector<std::string> variants;
  std::vector<int32_t> variant_param_counts;
};

struct KirInstr {
  KirOpcode op = KirOpcode::Nop;
  std::vector<int32_t> operands;
  int line = 0;
  int col = 0;
};

struct KirBasicBlock {
  std::string label;
  std::vector<KirInstr> instrs;
};

struct KirFunction {
  std::string name;
  std::string source_path;
  int param_count = 0;
  std::vector<std::string> param_names;
  std::vector<KirType> param_types;
  KirType return_type = KirType::Any;
  std::vector<KirType> local_types;
  std::vector<KirType> instr_types;
  std::vector<KirBasicBlock> blocks;
};

struct KirModule {
  std::vector<KirFunction> functions;
  // Parallel to compile-time constant pool indices (string entries used by FieldGet).
  std::vector<std::string> constant_strings;
  std::vector<KirStructMeta> struct_metas;
  std::vector<KirEnumMeta> enum_metas;
  // Chunk function pool indices (ConstFn operands) → symbol names.
  std::vector<std::string> function_names;
  std::vector<std::string> function_symbols;
  std::vector<int32_t> function_param_counts;
  std::vector<KirFunctionSig> function_signatures;
};

const char *kir_type_name(KirType type);
KirType kir_type_join(KirType a, KirType b);
bool kir_type_is_scalar(KirType type);
bool kir_type_is_heap(KirType type);
KirType kir_cast_target_type(int kind);

const char *kir_opcode_name(KirOpcode op);
std::string dump_kir_module(const KirModule &module);
std::string dump_kir_function(const KirFunction &function);

} // namespace kinglet
