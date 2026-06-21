# Kinglet Bootstrap — Agent Instructions

## Project Overview

C++20 **reference compiler** (stage0) for Kinglet: lexer → parser → checker →
compiler → KIR → VM bytecode and (optional) LLVM native. Language semantics and
self-host parity are defined in [kinglet-lang/kinglet](https://github.com/kinglet-lang/kinglet).

This repo is **not** the language spec repo. No editor extensions or LSP server
here — those belong in a separate `lsp` repo.

## Build

```bash
gn gen out/Debug
ninja -C out/Debug

# Native backend (LLVM):
gn gen out/Default --args='enable_llvm=true llvm_config="/opt/homebrew/opt/llvm/bin/llvm-config"'
ninja -C out/Default kinglet kinglet_rt
# Optional short name (same binary, Windows: hard link — not .cmd):
#   bash scripts/stage-klet-alias.sh out/Default
#   pwsh -File scripts/stage-klet-alias.ps1 out/Default
```

Run:

```bash
./out/Debug/kinglet tests/exec/cases/operators_arithmetic.kl
./out/Debug/klet --check path/to/file.kl   # if you `ln -s kinglet klet` locally
./out/Debug/kinglet build              # project build (needs kinglet.nest)
./out/Debug/kinglet init
```

Test suites: `bash tests/run_all.sh` (see [tests/README.md](tests/README.md))

## Directory Structure

See [src/README.md](src/README.md) for the pipeline diagram and GN deps.

```
src/
├── lexer/ parser/ ast/ types/    # frontend
├── checker/ module/              # semantics + imports + kinglet.toml
├── ir/                           # KIR (shared backend IR)
├── compiler/                     # AST → KIR + VM bytecode
├── vm/                           # bytecode interpreter
├── codegen/llvm/                 # KIR → native (optional)
└── kinglet/                      # main, cli_driver, vm_main

runtime/                          # libkinglet_rt (user program native RT)
build/                            # GN toolchains, llvm.gni, embed.gni
tests/                            # ADR 0012 layout (see tests/README.md)
```

## Code Conventions

- C++20, GN + ninja; system clang on macOS
- `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion`
- Namespace: `kinglet`, `kinglet::ast`
- `dynamic_cast` for AST dispatch (no visitor yet)
- Each module: `src/<name>/BUILD.gn` static_library; wire new libs in root `BUILD.gn`
- `include_dirs = ["//src"]` — includes are `"lexer/scanner.h"` style

## Ref vs Shadow

| | Bootstrap (this repo) | kinglet-self |
|--|----------------------|--------------|
| Role | Ref, fast, authoritative for `kinglet build` | Shadow, `kinglet prove` |
| Sources | C++ | Kinglet `.kl` under `core/`, `compiler/`, … |
| Parity | — | Bytecode identity `compiler.kbc == S3` (ADR 0013) |

## Large Files (split when touching)

- `checker/type_checker.cc`
- `compiler/compiler.cc`
- `codegen/llvm/kir_to_llvm.cc`
- `parser/parser.cc`
