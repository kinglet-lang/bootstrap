# Bootstrap test suite

Layout follows [kinglet decision 0012](../../kinglet/decisions/0012-test-suite-redesign.md),
adapted for the C++ bootstrap compiler (no selfhost `compiler.kbc` in this repo).

## Quick commands

```bash
bash tests/run_all.sh                 # all gating suites + probe snapshot
bash tests/harness/run.sh <path>      # ad-hoc harness on a file or directory
bash tests/exec/run.sh                # native compile + run (end-to-end)
bash tests/sema/run.sh                # type checker pass + fail
bash tests/codegen/run.sh             # bytecode shape checks
bash tests/ir/run.sh                  # KIR (`--ir`) dump checks
bash tests/parser/run.sh              # AST (`--ast`) checks
bash tests/probe/run_matrix.sh        # capability snapshot (non-gating)
```

Windows CI sets `KINGLET_SKIP_RUN=1` so `exec/` cases are skipped; sema/codegen/ir/parser still run.

## Layout

```
tests/
  harness/          run.sh, directives.md — shared directive runner
  exec/cases/       RUN: run — compile + native execute
  sema/
    pass/           RUN: check (must pass)
    fail/           RUN: check + COMPILE-FAIL
  codegen/cases/    RUN: bytecode
  ir/cases/         RUN: ir (bootstrap KIR dump)
  parser/cases/     RUN: ast
  probe/            capability matrix (snapshot, always exits 0)
  module/           logical module integration + hierarchical rejections
  fmt/              preen formatter goldens
  nest/             kinglet.nest manifest parser goldens
  abi/              native ABI integration (cross-module struct)
  common.sh         resolve_kinglet, strip_cr
  run_all.sh
```

Deprecated: `tests/cli/run_golden.sh` forwards to the suites above.

## Harness pipelines

See [harness/directives.md](harness/directives.md).

| `RUN:` | Purpose |
|--------|---------|
| `run` | `kinglet file.kl [args…]` |
| `check` | `kinglet --check file.kl` |
| `bytecode` | `kinglet --bytecode file.kl` |
| `ir` | `kinglet --ir file.kl` |
| `ast` | `kinglet --ast file.kl` |

Environment:

| Variable | Effect |
|----------|--------|
| `KINGLET` | bootstrap binary path |
| `KINGLET_SKIP_RUN` | skip `RUN: run` cases (Windows CI) |
| `KINGLET_SKIP_REBUILD` | skip `ninja` rebuild in harness |

## Adding cases

| Kind | Where | Directives |
|------|-------|------------|
| End-to-end behavior | `exec/cases/` | `RUN: run`, `.expected`, `.stderr`, `.args` |
| Type check must pass/fail | `sema/pass/` or `sema/fail/` | `RUN: check`, `COMPILE-FAIL`, `CHECK-ERR` |
| Bytecode shape | `codegen/cases/` | `RUN: bytecode`, `CHECK:` or `.bytecode` |
| KIR lowering | `ir/cases/` | `RUN: ir`, `CHECK:` |
| AST shape | `parser/cases/` | `RUN: ast`, `CHECK:` |
| Language probe | `probe/cases/` | `// EXPECT_OUT:` header |
