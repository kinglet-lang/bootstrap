#pragma once

#include "ir/kir.h"

#include <string>

namespace kinglet {

struct NativeCompileResult {
  bool ok = false;
  std::string error;
};

class KirToLlvm {
public:
  // Compile the user `main` function from KIR into a native executable.
  static NativeCompileResult compile_executable(const KirModule &module,
                                                const std::string &out_path,
                                                const std::string &rt_lib_path);
};

} // namespace kinglet
