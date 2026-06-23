# `compiler/` — bootstrap compiler layout

C++ reference (Ref) compiler for Kinglet. Language design, self-host tests, and
`kinglet prove` live in [kinglet-lang/kinglet](https://github.com/kinglet-lang/kinglet).
Editor/LSP tooling is a separate repo (planned).

Source is organized into four tiers: `frontend/` (parse + semantics),
`ir/` (middle-end), `backend/` (lowering + execution), `driver/` (CLI + tooling).

## Pipeline

```
.kl source
  → frontend/lexer/      scan tokens
  → frontend/parser/     AST
  → frontend/checker/    types + diagnostics
  → backend/compiler/    AST → KIR (via ir/kir_recorder) + VM bytecode (bytecode_emitter)
  → ir/                  KIR in memory (typing, specialize, dump)
       ├→ backend/vm/             execute .kbc (or in-process Chunk)
       └→ backend/codegen/llvm/   link libkinglet_rt → native executable
```

User-facing CLI: `driver/kinglet/` (`cli_driver` for init/build/run/prune/fmt;
`main` for dev flags and REPL). Optional `kinglet-vm` runs the VM host only.

Formatting: `driver/preen/` (`kinglet::preen`) — parse-then-emit formatter used by
`kinglet fmt` (CI/batch) and intended for in-process LSP integration.

## Directories

| Path | Tier | Role |
|------|------|------|
| `frontend/lexer/` | frontend | `Token`, `Scanner` |
| `frontend/parser/` | frontend | Recursive-descent parser → `ast::Program` |
| `frontend/ast/` | frontend | AST node definitions |
| `frontend/types/` | frontend | `TypeExpr`, width helpers (`numeric`) |
| `frontend/checker/` | frontend | `TypeChecker` — inference, match exhaustiveness, warnings |
| `frontend/module/` | frontend | `ModuleLoader` (imports), `project_config` (`kinglet.toml`) |
| `driver/preen/` | driver | `kinglet::preen` formatter (`format_string`, extensions, `[fmt]` config) |
| `ir/` | ir | KIR structs, record from compiler, typing/specialize passes |
| `backend/compiler/` | backend | Main compile driver, bytecode emission, width/dense array helpers |
| `backend/vm/` | backend | `Chunk`, opcodes, RC/COW `Value`, `Vm` interpreter |
| `backend/codegen/llvm/` | backend | `KirToLlvm` — optional (`enable_llvm=true`) |
| `driver/kinglet/` | driver | `main.cc`, `cli_driver.cc`, `vm_main.cc` |

Top-level sibling: `runtime/` (`libkinglet_rt`) — linked into **user** native
binaries, not into the compiler itself (except LLVM link step).

## GN dependency graph (libraries)

```
frontend/ast
frontend/lexer → frontend/parser
frontend/ast + frontend/lexer + frontend/parser → frontend/module
frontend/ast + frontend/lexer + frontend/parser + frontend/module → driver/preen
frontend/ast + frontend/types + frontend/module + ir → frontend/checker
frontend/ast + frontend/types + backend/vm + frontend/module + ir → backend/compiler
frontend/ast + frontend/types + backend/vm → ir
ir → backend/codegen/llvm (optional)
```

Note: `ir/` currently includes `backend/vm/chunk.h` for KIR recording — a known coupling;
VM opcodes and KIR share representation during bootstrap.

## Binaries (`//BUILD.gn`)

| Target | Output | Deps (summary) |
|--------|--------|----------------|
| `kinglet` | `kinglet` | Full frontend + compiler + vm; + llvm + rt when enabled |
| `kinglet-vm` | `kinglet-vm` | `backend/vm` only |
| `kinglet_rt` | `libkinglet_rt.a` | `runtime/` |

## Tests

Ref regression: `bash tests/run_all.sh` (from repo root). See [tests/README.md](../tests/README.md).

Formatter goldens: `tests/fmt/run_golden.sh`.

Shadow parity and toolchain stamps: `kinglet-self` repo (`./kinglet prove`).
