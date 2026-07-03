# Building the Bootstrap Compiler

## Prerequisites

- **C++20 compiler** — Clang 14+ or GCC 12+
- **LLVM 18+** — for the native backend (`enable_llvm=true`); optional (compile-only without it)
- **Python 3.8+** — for GN

Run `scripts/setup.sh` once per machine to fetch pinned GN and Ninja binaries and
detect LLVM:

```bash
bash scripts/setup.sh          # Unix
pwsh -File scripts/setup.ps1   # Windows
```

## Quick Build

The fastest way is `scripts/build.sh`, which detects LLVM, runs `gn gen` +
`ninja`, and stages the binary under `tools/bin/`:

```bash
bash scripts/build.sh
./tools/bin/kinglet --version
```

For CI or custom builds, pass `--gn` to append GN args and `BUILD_CI=1` to skip
binary staging:

```bash
BUILD_CI=1 bash scripts/build.sh --out out/Debug --gn 'sanitizer="address,undefined"'
```

Common flags:

| Flag | Effect |
|------|--------|
| `--out out/Dir` | Build output directory (default: `out/Default`) |
| `--debug` | Debug build (`-g`, no optimisations) |
| `--no-llvm` | Compile-only, no LLVM native backend |
| `--gn 'key=val …'` | Append arbitrary GN args (`sanitizer`, `coverage`, `optimize`, …) |
| `BUILD_CI=1` | Skip binary staging (for CI/automation) |

The script reads `LLVM_CONFIG` from the environment; `setup.sh --install` sets
this automatically on CI.

## Manual Build

If you prefer to call `gn` and `ninja` directly:

```bash
source tools/env.sh
gn gen out/Default --args='is_debug=false enable_llvm=true llvm_config="'"$(which llvm-config)"'"'
ninja -C out/Default kinglet kinglet_rt
```

## Build Configurations

| GN args | Config |
|---------|--------|
| (none) | Default: `is_debug=false`. LLVM auto-detected through `build.sh`. |
| `is_debug=true` | Debug symbols, no optimisations. |
| `enable_llvm=true llvm_config="…"` | LLVM native backend (AOT compilation). |
| `sanitizer="address,undefined,leak"` | ASan + UBSan + LSan (Clang only). |
| `coverage=true` | `--coverage` instrumentation for `llvm-cov` / Codecov. |
| `use_libfuzzer=true sanitizer="address,undefined"` | libFuzzer fuzz targets (Clang only). |

All GN args are merged with `scripts/build.sh` via `--gn`:
`bash scripts/build.sh --gn 'sanitizer="address,undefined" coverage=true'`.

## Fuzzing

The front-end (lexer, parser, full pipeline) is fuzzed with
[libFuzzer](https://llvm.org/docs/LibFuzzer.html). See
[tests/fuzz/README.md](../tests/fuzz/README.md) for the full guide.

```bash
# Build fuzz targets
bash scripts/build.sh --out out/Fuzz --gn 'use_libfuzzer=true sanitizer="address,undefined"'

# Quick smoke (60 s per target)
bash scripts/fuzz.sh all 60

# Deep campaign (15 min per target)
bash scripts/fuzz.sh all 900
```

`fuzz-smoke` runs on every PR as a **required** CI check. Crash reproducers are
committed as regression seeds under `tests/fuzz/corpus/`.

## CI Checks

Every PR to `canon` must pass these **7 required checks** before auto-merge:

- `build-and-test-unix` (ubuntu + macos)
- `build-and-test-windows`
- `clang-format`
- `clang-tidy`
- `commit-style`
- `fuzz-smoke`
- All 7 → green → auto-merge (bot: `kinglet-merge-bot`, squash).

Additional non-blocking jobs (`coverage`, `release-build`, `sanitizers`,
`benchmarks`, `codecov/patch`) also run on every PR.

## Troubleshooting

**`gn: command not found`** — run `source tools/env.sh` or `scripts/setup.sh`
first to add the pinned GN/Ninja to `PATH`.

**`llvm-config: command not found`** — install LLVM 18+ (`apt install llvm-dev`
on Debian/Ubuntu, `brew install llvm` on macOS), or pass `--no-llvm` for a
compile-only build.

**`kinglet: execution requires LLVM native backend`** — rebuild with
`enable_llvm=true`, or run with `--check` / `--ir` (compile-only modes).

**Stack overflow during parse** — the parser has a recursion depth guard
(`kMaxRecursionDepth = 48`). If you hit it on deep input (> 24 nesting levels),
the input is pathological; reduce nesting.
