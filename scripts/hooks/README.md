# Git hooks

Install once per clone:

```sh
./scripts/hooks/install.sh
```

This sets `core.hooksPath` to `scripts/hooks/` in the current repository.

## Pre-commit: core source comments

Applies to staged `.cc`, `.h`, `.c`, and `.kl` files under the compiler
toolchain (parser, checker, compiler, module loader, IR, etc.).

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

## Commit-msg: subject line

The first line must follow [Conventional Commits](https://www.conventionalcommits.org/):

```
type(scope): short description
```

Allowed types: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`,
`chore`, `build`, `ci`, `revert`.

The subject must be at least four characters after the colon, must not mention
`Phase A/B/…`, and must not reference `ADR ####`.

Merge commits, `fixup!`, `squash!`, and `Revert …` subjects are exempt.

## Manual check

```sh
python3 scripts/hooks/check_comments.py path/to/file.cc
python3 scripts/hooks/check_commit_msg.py /tmp/msg
```
