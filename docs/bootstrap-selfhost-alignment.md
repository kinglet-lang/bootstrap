# Bootstrap ← Selfhost Alignment

Bootstrap C++ compiler tracks the self-hosted `compiler.kbc` as the authoritative
language implementation. Behavior (stdout + exit code) must match; bytecode layout
may differ.

## Gap closure targets

| Probe / case | Layer | Selfhost reference |
|--------------|-------|-------------------|
| `?:` / Elvis | parser, checker, compiler | `parser/expressions.kl`, `compiler/codegen.kl` |
| top-level `const` | compiler | `compiler/compiler.kl` `maybe_register_global_const` |
| `Printable::method` | checker, compiler | `checker/checker.kl` `check_qualified_call` |
| UFCS `p.fn()` | checker, compiler | `register_function_sig` → `Type::fn` alias |
| `using namespace io` + `out.line` | checker, compiler | opened-namespace paths |

## Verification

From `kinglet-self`:

```bash
bash tests/differential/run_matrix.sh   # bs-only-fail=0, behavior identical
bash tests/differential/run.sh
```

`?:` (null / CastError Elvis) is the sole coalesce operator, aligned with selfhost.
`??` is rejected at parse time.
