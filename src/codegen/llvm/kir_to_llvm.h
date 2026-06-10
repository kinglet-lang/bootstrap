#pragma once

#include "ir/kir.h"

#include <string>

namespace kinglet {

struct NativeCompileResult {
  bool ok = false;
  std::string error;
};

struct NativeCompileOptions {
  // Emit DWARF debug info derived from KIR line tables.
  bool debug_info = false;
  // When set, per-module object files are cached here keyed by a content
  // stamp; unchanged modules skip LLVM lowering and codegen on rebuilds.
  std::string object_cache_dir;
  // Compiler identity mixed into every stamp (e.g. a hash of the kinglet
  // binary) so codegen changes invalidate cached objects.
  std::string cache_salt;
};

class KirToLlvm {
public:
  // Compile the user `main` function from KIR into a native executable.
  static NativeCompileResult compile_executable(const KirModule &module,
                                                const std::string &out_path,
                                                const std::string &rt_lib_path,
                                                const NativeCompileOptions &options = {});
};

} // namespace kinglet
