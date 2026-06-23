# Kinglet Bootstrap — Agent Instructions

## Project Overview

C++20 **reference compiler** (stage0) for Kinglet: lexer → parser → checker →
compiler → KIR → LLVM native. Language semantics and
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

See [compiler/README.md](compiler/README.md) for the pipeline diagram and GN deps.

```
compiler/                         # the bootstrap compiler (C++20)
├── frontend/                     # lexer, parser, ast, types, checker, module
├── ir/                           # KIR (shared middle-end IR)
├── backend/                      # compiler (AST→KIR), vm (types/opcodes), codegen/llvm (optional)
└── driver/                       # kinglet (main, cli_driver), preen (formatter)

runtime/                          # libkinglet_rt (user program native RT; ABI-stable, independent)
build/                            # GN toolchains, llvm.gni, embed.gni
tests/                            # ADR 0012 layout (see tests/README.md)
```

## Code Conventions

- C++20, GN + ninja; system clang on macOS
- `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion`
- Namespace: `kinglet`, `kinglet::ast`
- `dynamic_cast` for AST dispatch (no visitor yet)
- Each module: `compiler/<tier>/<name>/BUILD.gn` static_library; wire new libs in root `BUILD.gn`
- `include_dirs = ["//compiler"]` — includes are `"frontend/lexer/scanner.h"` style

## Ref vs Shadow

| | Bootstrap (this repo) | kinglet-self |
|--|----------------------|--------------|
| Role | Ref, fast, authoritative for `kinglet build` | Shadow, `kinglet prove` |
| Sources | C++ | Kinglet `.kl` under `core/`, `compiler/`, … |
| Parity | — | Native output identity (ADR 0013) |

## Large Files (split when touching)

- `frontend/checker/type_checker.cc`
- `backend/compiler/compiler.cc`
- `frontend/parser/parser.cc`
