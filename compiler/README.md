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
  → backend/compiler/    AST → KIR (via ir/kir_recorder)
  → ir/                  KIR in memory (typing, specialize, dump)
       └→ backend/codegen/llvm/   link libkinglet_rt → native executable
```

User-facing CLI: `driver/kinglet/` — `cli_driver` dispatches subcommands to
`cmd_init`, `cmd_build`, `cmd_run`, `cmd_prune`, `cmd_fmt`; terminal styling in
`cli_ui`; process management in `cli_spawn`. `main.cc` handles dev flags
(`--check`, `--ir`, `--native`, etc.). Formatting: `driver/preen/` (`kinglet::preen`).

## Directories

| Path | Tier | Role |
|------|------|------|
| `frontend/lexer/` | frontend | `Token` (`uint8_t` base), `Scanner` |
| `frontend/parser/` | frontend | Recursive-descent parser (`parse_expr`, `parse_stmt`, `parse_decl`, `parse_type`) |
| `frontend/ast/` | frontend | AST node definitions, `ExprVisitor` / `StmtVisitor` |
| `frontend/types/` | frontend | `TypeKind`, `TypeId` (canonical enum), width helpers (`numeric`) |
| `frontend/checker/` | frontend | `TypeChecker` — split into per-concern files (`check_call`, `check_binary`, …) |
| `frontend/sema/` | frontend | `SemanticContext` — shared import/generic/concept state |
| `frontend/module/` | frontend | `ModuleLoader` (imports), `project_config` (`kinglet.nest`) |
| `driver/preen/` | driver | `kinglet::preen` formatter (`format_string`, extensions, `[fmt]` config) |
| `ir/` | ir | KIR structs, recorder, typing/specialize passes |
| `backend/compiler/` | backend | AST→KIR compiler, split into per-concern files (`compile_call`, `compile_binary`, …) |
| `backend/vm/` | backend | `Chunk` opcode/metadata types, RC/COW `Value` (execution backend removed) |
| `backend/codegen/llvm/` | backend | `KirToLlvm` — optional (`enable_llvm=true`), split into `llvm_function_lowerer` + helpers |
| `driver/kinglet/` | driver | `main.cc`, `cli_driver` (dispatch), `cli_ui`, `cli_spawn`, `cmd_{init,build,run,prune,fmt}` |

Top-level sibling: `runtime/` (`libkinglet_rt`) — linked into **user** native
binaries, not into the compiler itself (except LLVM link step).

## GN dependency graph (libraries)

```
frontend/ast
frontend/lexer → frontend/parser
frontend/ast + frontend/lexer + frontend/parser → frontend/module
frontend/ast + frontend/lexer + frontend/parser + frontend/module → driver/preen
frontend/ast + frontend/types + frontend/sema + frontend/module + ir → frontend/checker
frontend/ast + frontend/types + frontend/sema + backend/vm + frontend/module + ir → backend/compiler
frontend/ast + frontend/types + backend/vm → ir
ir → backend/codegen/llvm (optional)
```

Note: `backend/vm/` retains type and opcode definitions used during bootstrap
(RC/COW `Value`, `Chunk` metadata); the execution backend has been removed.

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
