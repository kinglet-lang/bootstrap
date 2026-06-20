# Bootstrap ← Selfhost Alignment

Bootstrap C++ compiler tracks the self-hosted `compiler.kbc` as the authoritative
language implementation. Behavior (stdout + exit code) must match; bytecode layout
may differ.

## Module and `using` rules (ADR 0018)

Project manifest: **`kinglet.nest`** only (`kinglet.toml` retired).

Supported:

- `export module <id>;` + `import <module-id>;` (logical modules via `kinglet.nest`)
- `using alias = module-id;` — short qualifier
- `using namespace module-id;` — open `pub` members (user modules)
- Platform IO: `using io;` → `io::out.line(...)`, or `using namespace io;` → `out.line(...)`

**Not supported** (parse error in bootstrap and selfhost):

- `using module { sym };` — selective blocks (user modules and platform)
- `using module::*;` — wildcard (use `using namespace module` instead)

## Gap closure targets

| Probe / case | Layer | Selfhost reference |
|--------------|-------|-------------------|
| `?:` / Elvis | parser, checker, compiler | `parser/expressions.kl`, `compiler/codegen.kl` |
| top-level `const` | compiler | `compiler/compiler.kl` `maybe_register_global_const` |
| `Printable::method` | checker, compiler | `checker/checker.kl` `check_qualified_call` |
| UFCS `p.fn()` | checker, compiler | `register_function_sig` → `Type::fn` alias |
| `using namespace io` + `out.line` | checker, compiler | opened-namespace paths |
| struct `float64` field read | kir typing, native unbox | `kir_field_type_for_name` unique-name rule |

## Verification (bootstrap repo)

```bash
ninja -C out/Default kinglet
bash tests/cli/run_golden.sh
bash tests/probe/run_matrix.sh          # capability snapshot (30 probes)
bash tests/module/hierarchical/run.sh   # logical modules + reject { } / ::*
```

## Verification (kinglet repo — differential)

Requires a built `compiler.kbc` (`bash scripts/build/build.sh --backend vm`).

```bash
export KINGLET_BOOTSTRAP=../kinglet-bootstrap/out/Default/kinglet
bash tests/differential/run_matrix.sh   # snapshot: target bs-only-fail=0
bash tests/differential/run.sh          # gating: bootstrap vs selfhost behavior
```

`?:` (null / CastError Elvis) is the sole coalesce operator, aligned with selfhost.
`??` is rejected at parse time.
