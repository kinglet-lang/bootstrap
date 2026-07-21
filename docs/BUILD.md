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

## Building on Windows

The native LLVM backend is built with the **MSYS2 MinGW LLVM** — that is the
only Windows distribution that ships `llvm-config` together with the matching
libraries and headers. (The official llvm.org installer omits them.) Install it
once:

```pwsh
pacman -S mingw-w64-x86-64-llvm mingw-w64-x86-64-clang   # inside an MSYS2 MinGW64 shell
```

Then build exactly as on Unix — the scripts detect LLVM and wire everything up:

```pwsh
pwsh -File scripts/setup.ps1     # GN + Ninja, detect LLVM
pwsh -File scripts/build.ps1     # gn gen + ninja; native backend if LLVM found
```

How it fits together:

- `scripts/setup.ps1` reports the detected `llvm-config`.
- `scripts/build.ps1` sets `enable_llvm=true llvm_config="…" clang_base_path="…"`
  where `clang_base_path` is the llvm-config bin dir (e.g.
  `C:/msys64/mingw64/bin`). This makes the whole build compile and link with
  that same MinGW `clang++`/`llvm-ar` — necessary because the MSVC-ABI clang
  from llvm.org cannot link the MinGW LLVM libraries.
- After building, the MinGW runtime DLLs and `libLLVM-20.dll` are staged next
  to `kinglet.exe` (transitive closure), so the binary is self-contained and
  runs without MSYS2 on `PATH`.

Flags mirror `build.sh`:

| Flag | Effect |
|------|--------|
| `-DebugBuild` | Debug build (`-g`, no optimisations) |
| `-NoLlm` | Compile-only, no LLVM native backend |
| `-Out out\Dir` | Build output directory (default: `out\Default`) |
| `-GnArgs 'key=val …'` | Append arbitrary GN args |
| `BUILD_CI=1` | Skip binary staging (for CI/automation) |

For the native backend to AOT-link user programs at runtime, set `KINGLET_CXX`
to the MinGW `clang++.exe` (otherwise `kinglet run`/`build` fall back to a
generic PATH `clang++`, which may be the wrong ABI):

```pwsh
$env:KINGLET_CXX = "C:/msys64/mingw64/bin/clang++.exe"
```

With `KINGLET_CXX` set and the MSYS2 `mingw64/bin` on `PATH` (so the
AOT-compiled user programs can find their MinGW runtime DLLs), `kinglet run`
and `kinglet build` work on Windows just as on Unix:

```pwsh
pwsh -File scripts/build.ps1
$env:KINGLET_CXX = "C:/msys64/mingw64/bin/clang++.exe"
kinglet run path/to/program.kl     # native compile + execute
kinglet build                      # build the project's default target
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
(`kMaxRecursionDepth = 16`). If valid input reaches the limit, reduce its nesting
depth; malformed input is rejected before exhausting the process stack.
