# Building Kinglet Bootstrap

The compiler is C++20, built with **GN + Ninja**, with an optional **LLVM**
native backend. This guide covers obtaining the toolchain, generating a build,
and running the test suite.

> TL;DR — Unix with native backend:
>
> ```bash
> bash scripts/bootstrap.sh        # one-time: pinned GN + Ninja + LLVM into ./tools
> source tools/env.sh
> gn gen out/Default --args='is_debug=false enable_llvm=true llvm_config="$PWD/tools/llvm/bin/llvm-config"'
> ninja -C out/Default kinglet kinglet_rt
> ```

## Prerequisites

| Tool | Why | Notes |
|---|---|---|
| Python 3 | GN runs build-time scripts (`build/scripts/*.py`) | Already required by GN. |
| A C/C++ compiler | Toolchain for the build | System `clang` on macOS; `clang`/`gcc` on Linux; MSVC or Clang on Windows. |
| `curl` or `wget` | `bootstrap` downloads | One is enough. |
| `unzip`, `tar` | `bootstrap` extracts archives | Standard on Unix. |

GN, Ninja, and (on Unix) LLVM are **fetched in pinned versions** by the
bootstrap scripts below — you do **not** need to install them system-wide.

## One-time toolchain setup

The bootstrap scripts fetch pinned GN and Ninja, and (on Unix) build LLVM from
source into `./tools/` (gitignored), so every developer and CI runner uses the
same toolchain — built for the host it runs on, with no prebuilt
system-library mismatch.

**Unix** (macOS / Linux):

```bash
bash scripts/bootstrap.sh
source tools/env.sh   # prepends ./tools/bin to PATH, sets LLVM_CONFIG
```

`bootstrap.sh` fetches GN + Ninja, then **builds LLVM 22.1.8 from source** into
`tools/llvm` (pinned via the `llvmorg-22.1.8` git tag). The first build takes
roughly 20–40 minutes; `tools/llvm` is reused on later runs (and CI caches it),
so subsequent bootstraps skip the build. Requires `cmake` and `git` on the host
(preinstalled on GitHub runners and most dev machines).

**Windows**:

```powershell
pwsh -File scripts/bootstrap.ps1
.\tools\env.ps1   # prepends .\tools\bin to PATH
```

Windows mirrors the CI policy: **GN + Ninja only, no LLVM** — builds are
compile-only (the native LLVM backend is not supported on Windows yet).

If you already have GN/Ninja/LLVM on your machine and prefer to use those, skip
bootstrap — the build picks up whatever is on `PATH` (and `LLVM_CONFIG` / the
`llvm_config` arg for LLVM).

## Generating a build

GN takes a build directory and an args block.

**Debug (no LLVM)** — fastest, but the native backend is unavailable:

```bash
gn gen out/Debug --args='is_debug=false'
ninja -C out/Debug
```

**Release with LLVM native backend** (the recommended configuration):

```bash
gn gen out/Default \
  --args='is_debug=false enable_llvm=true llvm_config="'"$PWD"'/tools/llvm/bin/llvm-config"'
ninja -C out/Default kinglet kinglet_rt
```

### Build arguments

| Arg | Default | Meaning |
|---|---|---|
| `is_debug` | `true` | `false` enables `-O2`; `true` is `-g` with no optimisation. |
| `enable_llvm` | `false` | Build the LLVM native backend (`KirToLlvm`) and link `kinglet_rt`. |
| `llvm_config` | `""` | Path to `llvm-config`. If empty, GN runs `build/scripts/find_llvm_config.py`, which checks `$LLVM_CONFIG`, the Homebrew paths, then `PATH`. |

> Build args persist per output directory — re-running `gn gen out/Default`
> without `--args` reuses the previous args. Change args with a fresh `--args`
> on the same dir.

## Running

```bash
./out/Default/kinglet tests/exec/cases/operators_arithmetic.kl   # compile + run
./out/Default/kinglet --check path/to/file.kl                    # type-check only
./out/Default/kinglet --ir path/to/file.kl                       # dump KIR
./out/Default/kinglet build                                       # project build (needs kinglet.nest)
```

## Tests

```bash
KINGLET=out/Default/kinglet bash tests/run_all.sh
```

`KINGLET` points the harness at a specific build; it also auto-rebuilds that
build before running. Set `KINGLET_SKIP_REBUILD=1` to skip the rebuild.
See [`tests/README.md`](../tests/README.md) for the suite layout.

## Troubleshooting

**`llvm-config not found` / `enable_llvm=true` fails to generate**
Run `bash scripts/bootstrap.sh` and `source tools/env.sh`, or point `llvm_config`
explicitly: `gn gen out/Default --args='enable_llvm=true llvm_config="/path/to/llvm-config"'`.
`build/scripts/find_llvm_config.py --help` shows the search order.

**`gn: command not found` after bootstrap**
You forgot to `source tools/env.sh` (Unix) or `.\tools\env.ps1` (Windows) in
the current shell. The scripts only modify the current shell's environment.

**Windows: native backend / `kinglet run`**
Not supported on Windows yet. Build with `--args='is_debug=false'` (no
`enable_llvm`) and use `--check` / `--ast` / `--ir` for compile-only work.

**Slow first build**
LLVM headers are heavy; the first `enable_llvm=true` build compiles a lot.
Subsequent builds are incremental via Ninja.
