#include "codegen/llvm/kir_to_llvm.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

namespace kinglet {

NativeCompileResult KirToLlvm::compile_executable(const KirModule &module,
                                                  const std::string &out_path,
                                                  const std::string &rt_lib_path) {
  (void)module;
  (void)out_path;
  (void)rt_lib_path;

  llvm::LLVMContext context;
  llvm::Module module_ir("kinglet", context);

  return {.ok = false, .error = "KIR lowering not implemented"};
}

} // namespace kinglet
