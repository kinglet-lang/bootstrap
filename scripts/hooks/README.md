# Git hooks

Install once per clone:

```sh
./scripts/hooks/install.sh
```

This sets `core.hooksPath` to `scripts/hooks/` in the current repository.

## Pre-commit: formatting and comments

### clang-format

Staged `.cc` and `.h` files are auto-formatted with `clang-format -i -style=file`
before the comment check runs. If `clang-format` is not installed the step is
silently skipped.

### Required file header

C/C++:

```cpp
// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT
```

Kinglet (`.kl`):

```kl
// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT
```

Place the header at the top of the file, before `#include` / `#pragma once` /
`export module`.

### Banned comment patterns

Do not put planning or ADR metadata in source comments:

- `ADR 0003`, `See ADR …`
- `Amendment 2026-…`
- `Phase A`, `Phase B`, `Phase A2`, …
- `L1 —`, `L2 —`, …
- `stdlib tree`
- Meta-style boilerplate (`LICENSE file in the root directory of this source tree`)
- `Pass 0` / `Pass 0b` (describe the step in plain language instead)

## Commit-msg: subject line and style

The first line must follow [Conventional Commits](https://www.conventionalcommits.org/):

```
type(scope): description
```

Allowed types: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`,
`chore`, `build`, `ci`, `revert`.

The `commit-msg` hook enforces, in addition to the structure:

- **Imperative mood** — `add …`, not `added` / `adding` / `adds`.
- **Lowercase start, no trailing period** — the type prefix opens the
  sentence (`feat(parser): add …`). Identifiers keep their case (`LLVM`).
- **No vague descriptions** — `fix bug`, `update`, `changes`, `wip`,
  `cleanup`, … are rejected.
- At least four characters after the colon.
- No `Phase A/B/…` or `ADR ####` references.
- Subject length is a warning (aim ≤ 50, keep under 72), not a failure.

It also rewraps body prose to 72 columns via `format_commit_body.py`. The full
rationale and examples are in
[CONTRIBUTING.md → Commit messages](../../CONTRIBUTING.md#commit-messages),
following Google's
[CL description guidance](https://google.github.io/eng-practices/review/developer/cl-descriptions.html).

Merge commits, `fixup!`, `squash!`, and `Revert …` subjects are exempt.

## Manual check

```sh
python3 scripts/hooks/check_comments.py path/to/file.cc
python3 scripts/hooks/check_commit_msg.py /tmp/msg
```
