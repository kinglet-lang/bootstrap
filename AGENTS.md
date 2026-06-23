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
├── frontend/
│   ├── lexer/                    # Scanner, Token
│   ├── parser/                   # recursive-descent parser (split into parse_expr/stmt/decl/type)
│   ├── ast/                      # AST nodes + ExprVisitor/StmtVisitor
│   ├── types/                    # TypeKind, TypeId, numeric width helpers
│   ├── checker/                  # TypeChecker (split into check_expr/stmt/call/… per-concern files)
│   ├── sema/                     # SemanticContext (shared state: imports, generics, concepts)
│   └── module/                   # ModuleLoader, project_config
├── ir/                           # KIR structs, recorder, typing, specialize passes
├── backend/
│   ├── compiler/                 # AST→KIR compiler (split into compile_expr/stmt/… per-concern files)
│   ├── vm/                       # Value/ValueType (types only; execution backend removed)
│   └── codegen/llvm/             # KirToLlvm — optional (enable_llvm=true)
└── driver/
    ├── kinglet/                  # CLI: cli_driver (dispatch), cli_ui, cli_spawn,
    │                             #      cmd_init, cmd_build, cmd_run, cmd_prune, cmd_fmt
    └── preen/                    # kinglet fmt formatter

runtime/                          # libkinglet_rt (user program native RT; ABI-stable, independent)
build/                            # GN toolchains, llvm.gni, embed.gni
tests/                            # ADR 0012 layout (see tests/README.md)
```

## Code Conventions

- C++20, GN + ninja; system clang on macOS
- `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion`
- Static analysis: `.clang-format` (LLVM base, 100-col) and `.clang-tidy` at repo root
- Namespace: `kinglet`, `kinglet::ast`
- AST dispatch via `ExprVisitor` / `StmtVisitor` (see `frontend/ast/visitor.h`)
- Each module: `compiler/<tier>/<name>/BUILD.gn` static_library; wire new libs in root `BUILD.gn`
- `include_dirs = ["//compiler"]` — includes are `"frontend/lexer/scanner.h"` style

## Ref vs Shadow

| | Bootstrap (this repo) | kinglet-self |
|--|----------------------|--------------|
| Role | Ref, fast, authoritative for `kinglet build` | Shadow, `kinglet prove` |
| Sources | C++ | Kinglet `.kl` under `core/`, `compiler/`, … |
| Parity | — | Native output identity (ADR 0013) |
