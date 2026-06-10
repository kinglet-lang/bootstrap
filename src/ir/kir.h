#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kinglet {

enum class KirOpcode {
  ConstInt,
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
  Switch,
  StructNew,
  FieldGet,
  ArrayNew,
  IndexGet,
  ArrayLen,
  EnumVariant,
  INeg,
  Unreachable,
  Nop,
};

struct KirStructMeta {
  std::string name;
  std::vector<std::string> field_names;
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
  int param_count = 0;
  std::vector<std::string> param_names;
  std::vector<KirBasicBlock> blocks;
};

struct KirModule {
  std::vector<KirFunction> functions;
  // Parallel to compile-time constant pool indices (string entries used by FieldGet).
  std::vector<std::string> constant_strings;
  std::vector<KirStructMeta> struct_metas;
};

const char *kir_opcode_name(KirOpcode op);
std::string dump_kir_module(const KirModule &module);
std::string dump_kir_function(const KirFunction &function);

} // namespace kinglet
