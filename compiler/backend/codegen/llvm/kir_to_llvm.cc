// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "backend/codegen/llvm/kir_to_llvm.h"
#include "backend/codegen/llvm/llvm_module_cache_internal.h"

#include "ir/kir.h"
#include "ir/kir_field_resolve.h"
#include "ir/kir_numeric.h"
#include "ir/kir_specialize.h"
#include "ir/kir_typing.h"

#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/SHA256.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#if LLVM_VERSION_MAJOR >= 16
#include <llvm/TargetParser/Host.h>
#else
#include <llvm/Support/Host.h>
#endif

#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#include "frontend/module/native_symbol.h"

#include "backend/codegen/llvm/llvm_function_lowerer.h"
namespace kinglet {

NativeCompileResult KirToLlvm::compile_executable(const KirModule &module,
                                                  const std::string &out_path,
                                                  const std::string &rt_lib_path,
                                                  const NativeCompileOptions &options) {
  std::string error;
  KirModule typed_module = module;
  infer_kir_types(&typed_module);
  specialize_kir_arithmetic(&typed_module);
  resolve_kir_field_operands(typed_module);
  const KirModule &module_ref = typed_module;
  std::map<std::string, KirModule> shards;
  for (const KirFunction &fn : module_ref.functions) {
    const std::string key = fn.source_path.empty() ? "__entry__" : fn.source_path;
    shards[key].functions.push_back(fn);
  }
  if (shards.empty()) {
    return {.ok = false, .error = "KIR module has no functions"};
  }

  const bool use_cache = !options.object_cache_dir.empty();
  if (use_cache) {
    std::error_code cache_ec;
    std::filesystem::create_directories(options.object_cache_dir, cache_ec);
    if (cache_ec) {
      return {.ok = false,
              .error = "cannot create object cache dir " + options.object_cache_dir + ": " +
                       cache_ec.message()};
    }
  }

  std::vector<std::string> obj_paths;
  // Only freshly emitted (non-cached) objects are removed after linking.
  std::vector<std::string> temp_objs;
  std::size_t shard_idx = 0;
  for (auto &[key, shard] : shards) {
    copy_kir_metadata(&shard, module_ref);

    std::string obj_path;
    if (use_cache) {
      const std::string stamp = shard_stamp(shard, module_ref, options);
      // Derive a readable sub-path from the source key so cached objects
      // are organised per source file (e.g. src/main.kl/abc123.o).
      std::string rel_key = key;
      if (!options.source_prefix.empty() && key.starts_with(options.source_prefix)) {
        rel_key = key.substr(options.source_prefix.size());
        if (!rel_key.empty() && (rel_key[0] == '/' || rel_key[0] == '\\')) {
          rel_key = rel_key.substr(1);
        }
      }
      const std::filesystem::path cache_subdir(options.object_cache_dir);
      std::filesystem::path cache_path = cache_subdir;
      if (!rel_key.empty()) {
        cache_path /= rel_key;
        std::error_code dir_ec;
        std::filesystem::create_directories(cache_path, dir_ec);
      }
      obj_path = (cache_path / (stamp + ".o")).string();
      if (std::filesystem::exists(obj_path)) {
        obj_paths.push_back(obj_path);
        ++shard_idx;
        continue;
      }
    } else {
      obj_path = temp_object_path(key, out_path);
      temp_objs.push_back(obj_path);
    }

    llvm::LLVMContext context;
    llvm::Module llvm_module("kinglet_mod_" + std::to_string(shard_idx++), context);
    if (!lower_user_functions(&llvm_module, shard, module_ref, options.debug_info, &error)) {
      return {.ok = false, .error = error};
    }
    if (llvm::verifyModule(llvm_module, &llvm::errs())) {
      return {.ok = false, .error = "invalid LLVM module after lowering"};
    }
    if (use_cache) {
      // Emit to a private temp and rename so concurrent builds see either a
      // complete object or none.
      const std::string tmp_path = obj_path + ".tmp";
      if (!emit_object(llvm_module, tmp_path, &error)) {
        return {.ok = false, .error = error};
      }
      std::error_code rename_ec;
      std::filesystem::rename(tmp_path, obj_path, rename_ec);
      if (rename_ec) {
        return {.ok = false, .error = "cannot move object into cache: " + rename_ec.message()};
      }
    } else if (!emit_object(llvm_module, obj_path, &error)) {
      return {.ok = false, .error = error};
    }
    obj_paths.push_back(obj_path);
  }

  llvm::LLVMContext entry_context;
  llvm::Module entry_module("kinglet_entry", entry_context);
  if (!lower_entry_shim(&entry_module, &error)) {
    return {.ok = false, .error = error};
  }
  if (llvm::verifyModule(entry_module, &llvm::errs())) {
    return {.ok = false, .error = "invalid LLVM entry module"};
  }
  const std::string entry_obj = temp_object_path("entry", out_path);
  if (!emit_object(entry_module, entry_obj, &error)) {
    return {.ok = false, .error = error};
  }
  obj_paths.push_back(entry_obj);
  temp_objs.push_back(entry_obj);

  if (!link_objects(obj_paths, rt_lib_path, out_path, &error)) {
    std::error_code ec;
    for (const std::string &obj_path : temp_objs) {
      std::filesystem::remove(obj_path, ec);
    }
    return {.ok = false, .error = error};
  }

#if defined(__APPLE__)
  // Mach-O keeps DWARF in the object files (debug map); bake a dSYM before the
  // temporary objects are removed.
  if (options.debug_info) {
    const std::string dsym_cmd = "dsymutil \"" + out_path + "\"";
    if (std::system(dsym_cmd.c_str()) != 0) {
      std::error_code ec;
      for (const std::string &obj_path : temp_objs) {
        std::filesystem::remove(obj_path, ec);
      }
      return {.ok = false, .error = "dsymutil failed for " + out_path};
    }
  }
#endif

  std::error_code ec;
  for (const std::string &obj_path : temp_objs) {
    std::filesystem::remove(obj_path, ec);
  }
  return {.ok = true};
}

} // namespace kinglet
