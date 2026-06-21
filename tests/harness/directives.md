# Harness directive language (bootstrap)

Test cases are Kinglet source files (`.kl`) with **directives** in leading `//`
comments. `tests/harness/run.sh` parses them and runs the declared pipeline.

Directives must appear in a contiguous block at the top of the file. The block
ends at the first line that is neither a directive nor a blank line.

## Pipelines (`RUN`)

| Value | Behavior |
|-------|----------|
| `run` | `kinglet <file> [args…]` — compile + native execute |
| `check` | `kinglet --check <file>` |
| `bytecode` | `kinglet --bytecode <file>`; compare `.bytecode` golden or `CHECK` |
| `ir` | `kinglet --ir <file>`; assert `CHECK` substrings on stdout |
| `ast` | `kinglet --ast <file>`; assert `CHECK` substrings on stdout |

`run` is skipped when `KINGLET_SKIP_RUN=1` (Windows CI).

## Extra run arguments

| Directive | Meaning |
|-----------|---------|
| `RUN-ARGS: <tokens…>` | Extra argv after the source file (`run` only) |
| `RUN-ARGS: $HARNESS_TMP` | Substitute harness temp directory (for `fs` tests) |
| `RUN-ARGS: $CASE_DIR/<file>` | Path relative to the case directory |

Program arguments may also be listed one per line in `<case>.args`.

## Assertions

| Directive | Applies to |
|-----------|------------|
| `EXPECT-STDOUT: <text>` | exact stdout (after CRLF strip) |
| `EXPECT-STDERR: <text>` | exact stderr |
| `EXPECT-EXIT: <n>` | process exit code (default `0`) |
| `CHECK: <substr>` | stdout must contain substring |
| `CHECK-NOT: <substr>` | stdout must not contain substring |
| `CHECK-ERR: <substr>` | stderr must contain substring |
| `CHECK-ERR-AT: <line>:<col>` | stderr must contain `line:col:` |
| `COMPILE-FAIL` | compile/check/run must fail (non-zero exit) |

## Sidecar files

| File | Maps to |
|------|---------|
| `<case>.expected` | `EXPECT-STDOUT` |
| `<case>.exit` | `EXPECT-EXIT` |
| `<case>.stderr` | `EXPECT-STDERR` |
| `<case>.stderr_contains` | `CHECK-ERR` |
| `<case>.bytecode` | golden for `bytecode` pipeline |
| `<case>.args` | extra program arguments (`run`) |

## Examples

```kl
// RUN: run
// EXPECT-STDOUT: 42
using io;
int main() { io::out.line(42); return 0; }
```

```kl
// RUN: check
// COMPILE-FAIL
// EXPECT-EXIT: 65
// CHECK-ERR: Cannot assign
int main() { int32 x = 42; return 0; }
```

Suite runners: `tests/exec/run.sh`, `tests/sema/run.sh`, `tests/codegen/run.sh`,
`tests/ir/run.sh`, `tests/parser/run.sh`.
