# Building Kinglet Bootstrap

The compiler is C++20, built with **GN + Ninja**, with an optional **LLVM**
native backend. This guide covers obtaining the toolchain, generating a build,
and running the test suite.

> TL;DR — Unix with native backend:
>
> ```bash
> bash scripts/bootstrap.sh        # one-time: pinned GN + Ninja into ./tools/bin
> source tools/env.sh              # adds ./tools/bin to PATH
> # Install LLVM (see versions below), then:
> gn gen out/Default --args='is_debug=false enable_llvm=true llvm_config="$(which llvm-config)"'
> ninja -C out/Default kinglet kinglet_rt
> ```

## Prerequisites

| Tool | Why | Notes |
|---|---|---|
| Python 3 | GN runs build-time scripts (`build/scripts/*.py`) | Already required by GN. |
| A C/C++ compiler | Toolchain for the build | System `clang` on macOS; `clang`/`gcc` on Linux; MSVC or Clang on Windows. |
| `curl` or `wget` | `bootstrap` downloads | One is enough. |
| `unzip` | `bootstrap` extracts archives | Standard on Unix. |

### LLVM (optional, for native backend)

The native backend requires **LLVM 18 or later**. Install via your system package
manager:

| Platform | Command | Typical version |
|---|---|---|
| macOS | `brew install llvm` | latest Homebrew (22.x) |
| Ubuntu 24.04 | `sudo apt-get install llvm-dev clang` | 18.x |
| Ubuntu 22.04 | `sudo apt-get install llvm-18-dev clang-18` (add LLVM APT repo) | 18.x |

> `build/scripts/find_llvm_config.py` searches `$LLVM_CONFIG`, Homebrew paths,
> then `PATH`. Set `LLVM_CONFIG` explicitly if auto-detection fails.

## One-time toolchain setup

The bootstrap script fetches **pinned GN and Ninja** into `./tools/bin/`
(gitignored), so every developer and CI runner uses the same build-system
versions.

**Unix** (macOS / Linux):

```bash
bash scripts/bootstrap.sh
source tools/env.sh   # prepends ./tools/bin to PATH
```

**Windows**:

```powershell
pwsh -File scripts/bootstrap.ps1
.\tools\env.ps1   # prepends .\tools\bin to PATH
```

Windows mirrors the CI policy: **GN + Ninja only, no LLVM** — builds are
compile-only (the native LLVM backend is not supported on Windows yet).

If you already have GN/Ninja on `PATH`, skip bootstrap — the build picks up
whatever is available.

## Generating a build

GN takes a build directory and an args block.

**Debug (no LLVM)** — fastest, but the native backend is unavailable:

```bash
gn gen out/Debug --args='is_debug=false'
ninja -C out/Debug
```

**Release with LLVM native backend** (the recommended configuration):

```bash
# Assumes llvm-config is on PATH (or set LLVM_CONFIG)
gn gen out/Default --args='is_debug=false enable_llvm=true llvm_config="$(which llvm-config)"'
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
Install LLVM 18+ via your system package manager (see Prerequisites above),
or point `llvm_config` explicitly:
`gn gen out/Default --args='enable_llvm=true llvm_config="/path/to/llvm-config"'`.
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
