// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

// Module cache utilities for LLVM native compilation.
// Extracted from kir_to_llvm.cc to reduce file size.

#include "backend/codegen/llvm/llvm_module_cache_internal.h"
#include "backend/codegen/llvm/kir_to_llvm.h"
#include "frontend/module/native_symbol.h"

#include <llvm/ADT/StringExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/SHA256.h>
#if LLVM_VERSION_MAJOR >= 16
#include <llvm/TargetParser/Host.h>
#else
#include <llvm/Support/Host.h>
#endif

#include <filesystem>
#include <sstream>

namespace kinglet {

void copy_kir_metadata(KirModule *dst, const KirModule &src) {
  dst->constant_strings = src.constant_strings;
  dst->struct_metas = src.struct_metas;
  dst->enum_metas = src.enum_metas;
  dst->function_names = src.function_names;
  dst->function_symbols = src.function_symbols;
  dst->function_param_counts = src.function_param_counts;
  dst->function_signatures = src.function_signatures;
  dst->field_operands_resolved = src.field_operands_resolved;
}

int first_instr_line(const KirFunction &fn) {
  for (const KirBasicBlock &bb : fn.blocks) {
    for (const KirInstr &instr : bb.instrs) {
      if (instr.line > 0) {
        return instr.line;
      }
    }
  }
  return 1;
}

std::string temp_object_path(const std::string &tag, const std::string &out_path) {
  const unsigned long tag_hash =
      static_cast<unsigned long>(std::hash<std::string>{}(out_path + tag));
  return (std::filesystem::temp_directory_path() /
          ("kinglet-" + std::to_string(tag_hash) + ".o"))
      .string();
}

// Canonical per-shard fingerprint for the object cache. Pool indices are
// resolved to their values (strings, function symbols) so that edits in one
// module which shift the shared constant pool do not invalidate other
// modules' cached objects. Struct and enum metadata stay global because
// lowering embeds module-wide type and field indices.
std::string shard_fingerprint_text(const KirModule &shard, const KirModule &full,
                                   const NativeCompileOptions &options) {
  std::ostringstream out;
  out << "salt:" << options.cache_salt << '\n';
  out << "triple:" << llvm::sys::getDefaultTargetTriple() << '\n';
  out << "debug:" << (options.debug_info ? 1 : 0) << '\n';
  for (const KirStructMeta &meta : full.struct_metas) {
    out << "struct:" << meta.name;
    for (const std::string &field : meta.field_names) {
      out << ',' << field;
    }
    out << '\n';
  }
  for (const KirEnumMeta &meta : full.enum_metas) {
    out << "enum:" << meta.name;
    for (std::size_t v = 0; v < meta.variants.size(); ++v) {
      out << ',' << meta.variants[v] << '/'
          << (v < meta.variant_param_counts.size() ? meta.variant_param_counts[v] : 0);
    }
    out << '\n';
  }
  for (const KirFunction &fn : shard.functions) {
    out << "fn:" << mangled_native_symbol(fn.name, fn.source_path) << ':' << fn.param_count << '\n';
    for (const KirBasicBlock &bb : fn.blocks) {
      for (const KirInstr &instr : bb.instrs) {
        out << kir_opcode_name(instr.op);
        const bool string_pool_op = instr.op == KirOpcode::ConstString ||
                                    instr.op == KirOpcode::FieldGet ||
                                    instr.op == KirOpcode::FieldSet;
        if (string_pool_op && !instr.operands.empty()) {
          const int idx = instr.operands[0];
          if (idx >= 0 && static_cast<std::size_t>(idx) < full.constant_strings.size()) {
            out << " s\"" << full.constant_strings[static_cast<std::size_t>(idx)] << '"';
          } else {
            out << " s?" << idx;
          }
        } else if (instr.op == KirOpcode::ConstFn && !instr.operands.empty()) {
          const int idx = instr.operands[0];
          if (idx >= 0 && static_cast<std::size_t>(idx) < full.function_symbols.size()) {
            out << " f" << full.function_symbols[static_cast<std::size_t>(idx)] << '/'
                << (static_cast<std::size_t>(idx) < full.function_param_counts.size()
                        ? full.function_param_counts[static_cast<std::size_t>(idx)]
                        : -1);
          } else {
            out << " f?" << idx;
          }
        } else {
          for (int32_t operand : instr.operands) {
            out << ' ' << operand;
          }
        }
        out << '@' << instr.line << ':' << instr.col << '\n';
      }
    }
  }
  return out.str();
}

std::string shard_stamp(const KirModule &shard, const KirModule &full,
                        const NativeCompileOptions &options) {
  llvm::SHA256 hasher;
  const std::string text = shard_fingerprint_text(shard, full, options);
  hasher.update(text);
#if LLVM_VERSION_MAJOR >= 16
  const auto digest = hasher.final();
  return llvm::toHex(llvm::ArrayRef<uint8_t>(digest.data(), digest.size()),
                     /*LowerCase=*/true);
#else
  const llvm::StringRef digest = hasher.final();
  return llvm::toHex(llvm::ArrayRef<uint8_t>(
                         reinterpret_cast<const uint8_t *>(digest.data()),
                         digest.size()),
                     /*LowerCase=*/true);
#endif
}

} // namespace kinglet
