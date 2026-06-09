#include "codegen/llvm/kir_to_llvm.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace kinglet {

namespace {

const KirFunction *find_main(const KirModule &module) {
  for (const KirFunction &fn : module.functions) {
    if (fn.name == "main") {
      return &fn;
    }
  }
  return nullptr;
}

bool lower_main(const KirFunction &fn, llvm::Module *llvm_module, std::string *error) {
  if (fn.blocks.size() != 1) {
    *error = "native L0 expects a single basic block in main";
    return false;
  }

  llvm::LLVMContext &context = llvm_module->getContext();
  llvm::IRBuilder<> builder(context);
  auto *fn_type = llvm::FunctionType::get(llvm::Type::getInt32Ty(context), false);
  auto *func =
      llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, "kinglet_main", llvm_module);
  auto *entry = llvm::BasicBlock::Create(context, "entry", func);
  builder.SetInsertPoint(entry);

  const KirBasicBlock &bb = fn.blocks[0];
  std::vector<llvm::Value *> temps;
  bool saw_ret = false;

  for (const KirInstr &instr : bb.instrs) {
    switch (instr.op) {
    case KirOpcode::ConstInt:
      temps.push_back(llvm::ConstantInt::get(context, llvm::APInt(32, instr.operands[0], true)));
      break;
    case KirOpcode::Ret:
      saw_ret = true;
      if (instr.operands.empty()) {
        builder.CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0));
      } else {
        const int idx = instr.operands[0];
        if (idx < 0 || static_cast<std::size_t>(idx) >= temps.size()) {
          *error = "ret references missing value";
          return false;
        }
        builder.CreateRet(temps[static_cast<std::size_t>(idx)]);
      }
      break;
    default:
      *error = "unsupported KIR opcode in native L0";
      return false;
    }
  }

  if (!saw_ret) {
    *error = "main missing ret";
    return false;
  }
  return true;
}

bool emit_object(llvm::Module &module, const std::string &obj_path, std::string *error) {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  std::string triple_err;
  const llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
  module.setTargetTriple(triple);

  const llvm::Target *target = llvm::TargetRegistry::lookupTarget(triple.str(), triple_err);
  if (target == nullptr) {
    *error = "LLVM target lookup failed: " + triple_err;
    return false;
  }

  llvm::TargetOptions options;
  std::string cpu = llvm::sys::getHostCPUName().str();
  std::string features;
  llvm::TargetMachine *machine = target->createTargetMachine(
      triple, cpu, features, options, llvm::Reloc::Model::PIC_);
  if (machine == nullptr) {
    *error = "LLVM target machine creation failed";
    return false;
  }

  module.setDataLayout(machine->createDataLayout());

  std::error_code ec;
  llvm::raw_fd_ostream dest(obj_path, ec, llvm::sys::fs::OF_None);
  if (ec) {
    *error = "failed to open object file: " + ec.message();
    return false;
  }

  llvm::legacy::PassManager pass;
  if (machine->addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
    *error = "LLVM cannot emit an object file for this target";
    return false;
  }
  pass.run(module);
  dest.flush();
  return true;
}

bool link_executable(const std::string &obj_path, const std::string &rt_lib_path,
                     const std::string &out_path, std::string *error) {
  std::ostringstream cmd;
  cmd << "clang++ -o ";
  cmd << '"' << out_path << "\" ";
  cmd << '"' << obj_path << "\" ";
  cmd << '"' << rt_lib_path << '"';
  const int rc = std::system(cmd.str().c_str());
  if (rc != 0) {
    *error = "link failed (exit " + std::to_string(rc) + "): " + cmd.str();
    return false;
  }
  return true;
}

} // namespace

NativeCompileResult KirToLlvm::compile_executable(const KirModule &module,
                                                  const std::string &out_path,
                                                  const std::string &rt_lib_path) {
  const KirFunction *main_fn = find_main(module);
  if (main_fn == nullptr) {
    return {.ok = false, .error = "KIR module missing main"};
  }

  llvm::LLVMContext context;
  llvm::Module llvm_module("kinglet", context);

  std::string error;
  if (!lower_main(*main_fn, &llvm_module, &error)) {
    return {.ok = false, .error = error};
  }

  if (llvm::verifyModule(llvm_module, &llvm::errs())) {
    return {.ok = false, .error = "invalid LLVM module after lowering"};
  }

  const std::filesystem::path obj_path =
      std::filesystem::temp_directory_path() /
      ("kinglet-" + std::to_string(static_cast<unsigned long>(std::hash<std::string>{}(out_path))) +
       ".o");

  if (!emit_object(llvm_module, obj_path.string(), &error)) {
    return {.ok = false, .error = error};
  }

  if (!link_executable(obj_path.string(), rt_lib_path, out_path, &error)) {
    std::error_code ec;
    std::filesystem::remove(obj_path, ec);
    return {.ok = false, .error = error};
  }

  std::error_code ec;
  std::filesystem::remove(obj_path, ec);
  return {.ok = true};
}

} // namespace kinglet
