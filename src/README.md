# `src/` — bootstrap compiler layout

C++ reference (Ref) compiler for Kinglet. Language design, self-host tests, and
`kinglet prove` live in [kinglet-lang/kinglet](https://github.com/kinglet-lang/kinglet).
Editor/LSP tooling is a separate repo (planned).

## Pipeline

```
.kl source
  → lexer/     scan tokens
  → parser/    AST
  → checker/   types + diagnostics
  → compiler/  AST → KIR (via ir/kir_recorder) + VM bytecode (bytecode_emitter)
  → ir/        KIR in memory (typing, specialize, dump)
       ├→ vm/           execute .kbc (or in-process Chunk)
       └→ codegen/llvm/ link libkinglet_rt → native executable
```

User-facing CLI: `kinglet/` (`cli_driver` for init/build/run/prune; `main` for
dev flags and REPL). Optional `kinglet-vm` runs the VM host only.

## Directories

| Path | Role |
|------|------|
| `lexer/` | `Token`, `Scanner` |
| `parser/` | Recursive-descent parser → `ast::Program` |
| `ast/` | AST node definitions |
| `types/` | `TypeExpr`, width helpers (`numeric`) |
| `checker/` | `TypeChecker` — inference, match exhaustiveness, warnings |
| `module/` | `ModuleLoader` (imports), `project_config` (`kinglet.toml`) |
| `ir/` | KIR structs, record from compiler, typing/specialize passes |
| `compiler/` | Main compile driver, bytecode emission, width/dense array helpers |
| `vm/` | `Chunk`, opcodes, RC/COW `Value`, `Vm` interpreter |
| `codegen/llvm/` | `KirToLlvm` — optional (`enable_llvm=true`) |
| `kinglet/` | `main.cc`, `cli_driver.cc`, `vm_main.cc` |

Top-level sibling: `runtime/` (`libkinglet_rt`) — linked into **user** native
binaries, not into the compiler itself (except LLVM link step).

## GN dependency graph (libraries)

```
ast
lexer → parser
ast + lexer + parser → module
ast + types + module + ir → checker
ast + types + vm + module + ir → compiler
ast + types + vm → ir
ir → codegen/llvm (optional)
```

Note: `ir/` currently includes `vm/chunk.h` for KIR recording — a known coupling;
VM opcodes and KIR share representation during bootstrap.

## Binaries (`//BUILD.gn`)

| Target | Output | Deps (summary) |
|--------|--------|----------------|
| `kinglet` | `kinglet` | Full frontend + compiler + vm; + llvm + rt when enabled |
| `kinglet-vm` | `kinglet-vm` | `vm` only |
| `kinglet_rt` | `libkinglet_rt.a` | `runtime/` |

## Tests

Ref regression: `tests/cli/run_golden.sh` (from repo root).

Shadow parity and toolchain stamps: `kinglet-self` repo (`./kinglet prove`).
