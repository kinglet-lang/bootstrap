# Contributing to Kinglet Bootstrap

Thanks for your interest in contributing. This is the **bootstrap (reference)
compiler** for Kinglet — a C++20 implementation of the lexer, parser, type
checker, KIR middle-end, and LLVM native backend.

This is **not** the language-spec repository. Language design, ADRs,
self-host tests, and `kinglet build` live in
[kinglet-lang/kinglet](https://github.com/kinglet-lang/kinglet). Editor
extensions and the LSP server belong in a separate `perch` repo. If your change
is about language semantics rather than this compiler's implementation, please
open it upstream first.

## Table of contents

- [Issues and scope](#issues-and-scope)
- [Fork and branch workflow](#fork-and-branch-workflow)
- [Build](#build)
- [Tests](#tests)
- [Code style](#code-style)
- [File headers and comments](#file-headers-and-comments)
- [Commit messages](#commit-messages)
- [Pull requests](#pull-requests)
- [Licensing](#licensing)

## Issues and scope

- **Bug reports and feature ideas** belong in
  [GitHub Issues](https://github.com/kinglet-lang/bootstrap/issues). Please
  search existing issues before opening a new one.
- **Scope of this repo**: the bootstrap compiler (`compiler/`), the native
  runtime (`runtime/`), the build system (`build/`), and the test suite
  (`tests/`). Out of scope: language design changes, the LSP/editor tooling,
  and anything that belongs in `kinglet-lang/kinglet`.
- For large changes, please open an issue or discussion **before** writing
  code, so we can align on direction and avoid wasted work.

## Fork and branch workflow

External contributions use a fork-based flow. **There is no `main` branch** —
work lands on `canon` via pull request.

Remotes:

- `origin` → your fork (`github.com/<you>/bootstrap`)
- `upstream` → `kinglet-lang/bootstrap`

```bash
# One-time: point origin at your fork, keep upstream on the source repo.
git remote rename origin upstream
git remote add origin git@github.com:<you>/bootstrap.git
git fetch origin
```

Branch off `canon`, using a `<type>/<short-description>` name that matches the
commit type (for example `feat/parser-fold`, `fix/checker-nullable`,
`docs/build-guide`):

```bash
git checkout -b feat/your-change upstream/canon
git push -u origin feat/your-change
```

Keep your branch up to date by rebasing on upstream:

```bash
git fetch upstream
git rebase upstream/canon
```

Open the pull request against **`kinglet-lang/bootstrap:canon`**.

## Build

The full guide with prerequisites and troubleshooting is in
[docs/BUILD.md](docs/BUILD.md). Quick start (Unix, with the native backend):

```bash
bash scripts/bootstrap.sh        # one-time: pinned GN + Ninja into ./tools/bin
source tools/env.sh              # prepend ./tools/bin to PATH
gn gen out/Default --args='is_debug=false enable_llvm=true llvm_config="$(which llvm-config)"'
ninja -C out/Default kinglet kinglet_rt
```

The native LLVM backend requires **LLVM 18 or later**. macOS: `brew install llvm`.
Ubuntu: `sudo apt-get install llvm-dev clang`. If `llvm-config` is not on
`PATH`, set `LLVM_CONFIG` explicitly or pass `llvm_config="..."` in `--args`.

**Windows** builds are compile-only (no native backend yet): use
`pwsh -File scripts/bootstrap.ps1`, then `gn gen out/Debug --args='is_debug=false'`
and `ninja -C out/Debug kinglet`. See [docs/BUILD.md](docs/BUILD.md) for
details.

## Tests

The harness auto-rebuilds the binary pointed at by `KINGLET` and then runs
every gating suite:

```bash
KINGLET=out/Default/kinglet bash tests/run_all.sh
```

Set `KINGLET_SKIP_REBUILD=1` to skip the rebuild, and `KINGLET_SKIP_RUN=1` to
skip end-to-end `RUN: run` cases (used by Windows CI). Individual suites can
be run on their own — `tests/exec/run.sh`, `tests/sema/run.sh`,
`tests/codegen/run.sh`, `tests/ir/run.sh`, `tests/parser/run.sh`.

When adding or changing behavior, add a test case. Where each kind lives and
which directives it uses is documented in [tests/README.md](tests/README.md):

| Kind | Where | Directives |
|------|-------|------------|
| End-to-end behavior | `tests/exec/cases/` | `RUN: run`, `.expected`, `.stderr`, `.args` |
| Type check pass / fail | `tests/sema/pass/` or `tests/sema/fail/` | `RUN: check`, `COMPILE-FAIL`, `CHECK-ERR` |
| KIR shape | `tests/codegen/cases/` | `RUN: ir`, `CHECK:` |
| KIR lowering | `tests/ir/cases/` | `RUN: ir`, `CHECK:` |
| AST shape | `tests/parser/cases/` | `RUN: ast`, `CHECK:` |

See [tests/harness/directives.md](tests/harness/directives.md) for the
directive grammar.

## Code style

- **C++20**, built with GN + Ninja. System `clang` on macOS; `clang`/`gcc` on
  Linux; MSVC or Clang on Windows.
- Warnings are treated as errors in practice. The build compiles with
  `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion` — write code that
  is clean under all of them (mind narrowing conversions and signedness).
- Formatting and linting: [`.clang-format`](.clang-format) (LLVM base, 100
  columns) and [`.clang-tidy`](.clang-tidy) at the repo root. Run
  `clang-format` on changed files before committing.
- Namespace: `kinglet` (top-level) and `kinglet::ast` (AST nodes).
- AST dispatch goes through `ExprVisitor` / `StmtVisitor`
  (see `compiler/frontend/ast/visitor.h`).
- Includes are rooted at `compiler/`: write `"frontend/lexer/scanner.h"`, not
  a relative path. Each module is a GN `static_library` at
  `compiler/<tier>/<name>/BUILD.gn`; when you add a new library, wire it into
  the root [`BUILD.gn`](BUILD.gn).
- Match the surrounding code — naming, comment density, and idiom — and keep
  the [Ref vs. shadow](AGENTS.md#ref-vs-shadow) distinction in mind: this repo
  is the authoritative reference implementation.

## File headers and comments

Install the local git hooks once per clone:

```sh
./scripts/hooks/install.sh
```

This sets `core.hooksPath` to `scripts/hooks/`. The pre-commit hook enforces
the conventions below on staged core source files (`.cc`, `.h`, `.c`, `.kl`
under `compiler/`).

**Every core source file must begin with the SPDX header**, before any
`#include`, `#pragma once`, or module declaration:

```cpp
// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT
```

**Do not embed planning or metadata in source comments.** The following are
rejected by the pre-commit hook:

- `ADR 0003`, `See ADR …`
- `Amendment 2026-…`
- `Phase A`, `Phase B`, `Phase A2`, …
- `L1 —`, `L2 —`, …
- `stdlib tree`, `Pass 0` / `Pass 0b`
- Meta/Facebook-style license boilerplate (`LICENSE file in the root
  directory of this source tree`)

Describe the step or rationale in plain language instead. See
[scripts/hooks/README.md](scripts/hooks/README.md) for the full list and how
to run the checks manually.

## Commit messages

Commits follow [Conventional Commits](https://www.conventionalcommits.org/),
enforced both by the local `commit-msg` hook and by CI:

```
type(scope): short description
```

- **Allowed types**: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`,
  `test`, `build`, `ci`, `chore`, `revert`.
- **Scope** is optional, lowercase, and may contain `a-z0-9` plus `/._-`
  (for example `parser`, `checker`, `runtime`, `codegen/llvm`).
- A `!` before the colon marks a breaking change
  (`feat(parser)!: drop inspect keyword`).
- The description must be at least four characters after the colon.
- The subject **must not** reference `Phase A/B/…` or `ADR ####`.

Examples:

```
feat(parser): add pattern matching for fold expressions
fix(checker): reject nullable operands in binary arithmetic
docs: expand native backend troubleshooting
perf(runtime): block-buffer stdout and stderr
```

Squash-merge commits and `fixup!` / `squash!` / `Revert …` subjects are
exempt.

## Pull requests

CI validates **both** the PR title and every commit subject against the
conventional-commits rule above, so make sure they match.

The PR description must contain three **bold** sections (headings are
case-insensitive, but `**Summary**` / `**Changes**` / `**Testing**` is the
convention) and be at least 50 characters in total:

```markdown
**Summary**
<one-paragraph overview of the change and why>

**Changes**
- <change 1>
- <change 2>

**Testing**
<how you verified the change — commands run, suites added, etc.>
```

CI runs on **Ubuntu, macOS, and Windows** (Windows is compile-only). A passing
build compiles the compiler, builds the LLVM native backend (except Windows),
and runs the golden test suites. Merging is automated once CI is green, so
please make sure the branch is rebased onto the latest `canon` and all checks
pass.

Prefer **small, focused PRs** that address one concern. If a change is large,
split it into a stack of reviewable PRs or land preparatory refactors first.

## Licensing

All contributions are licensed under the [MIT License](LICENSE) — the same
license as the rest of this repository. The required SPDX header on each core
source file makes this explicit; by submitting a pull request you agree your
contributions are licensed under MIT.
