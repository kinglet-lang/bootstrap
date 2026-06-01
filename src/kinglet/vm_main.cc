#include "vm/chunk.h"
#include "vm/vm.h"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: kinglet-vm <program.kbc> [args...]\n"
              << "\n"
              << "Runs a pre-compiled Kinglet bytecode file.\n";
    return 64;
  }

  std::string bytecode_path = argv[1];
  std::vector<std::string> args;
  for (int i = 2; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }

  std::string error;
  kinglet::Chunk chunk = kinglet::Chunk::deserialize(bytecode_path, &error);
  if (!error.empty()) {
    std::cerr << "kinglet-vm: " << error << "\n";
    return 66;
  }

  kinglet::Vm vm;
  kinglet::VmResult result = vm.run(chunk, args);
  if (!result.ok) {
    std::cerr << "runtime error: " << result.error << "\n";
    return 70;
  }

  return 0;
}
