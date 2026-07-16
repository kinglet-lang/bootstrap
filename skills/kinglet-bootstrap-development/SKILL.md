---
name: kinglet-bootstrap-development
description: "Use when working on the kinglet-lang/bootstrap compiler repo: inspect language behavior, implement ADR-backed changes, run focused builds/tests, update docs and vault notes, and open fork PRs against canon."
version: 1.0.0
author: Hermes Agent
license: MIT
metadata:
  hermes:
    tags: [kinglet, bootstrap, compiler, adr, testing, github]
    related_skills: [systematic-debugging, test-driven-development, github-pr-workflow]
---

# Kinglet Bootstrap Development

## Overview

This skill captures the working loop for `kinglet-lang/bootstrap`, the Cxx bootstrap compiler for Kinglet. Use it to keep implementation, tests, ADRs, and the Obsidian knowledge base in sync without guessing from stale summaries.

The active development branch in the upstream repository is `canon`. Fork PRs target `kinglet-lang/bootstrap:canon` from `sentomk/bootstrap` branches.

## When to Use

Use this skill when:

- implementing or reviewing language behavior in `kinglet-lang/bootstrap`;
- checking whether a syntax form or API is actually accepted by the compiler;
- changing frontend parser/type-checker behavior, KIR lowering, LLVM codegen, or runtime native functions;
- adding or updating exec/sema tests;
- preparing a GitHub PR against `canon`;
- updating ADRs or the Kinglet Obsidian vault after behavior changes.

Do not use this skill as a substitute for source verification. For language facts, compile a small `.kl` sample or read the relevant parser/checker/runtime code.

## Repository Facts

- Repo path on the VPS: `/home/sentomk/Code/kinglet-lang/bootstrap`
- Upstream remote: `kinglet-lang/bootstrap`, branch `canon`
- Fork remote: `sentomk/bootstrap`
- Main local build output used by tests: `out/Llvm/kinglet`
- Fast local compiler build: `ninja -C out/Default kinglet`
- LLVM harness compiler build: `ninja -C out/Llvm kinglet`
- Full exec harness: `bash tests/exec/run.sh`
- Full sema harness: `bash tests/sema/run.sh`

## Standard Workflow

1. **Start clean from canon.**
   ```bash
   cd /home/sentomk/Code/kinglet-lang/bootstrap
   git fetch upstream --prune
   git checkout -B <branch-name> upstream/canon
   git status --short --branch
   ```
   Done when the branch is based on current `upstream/canon` and the worktree state is known.

2. **Locate the real implementation path.**
   Search and read source before editing. Common areas:
   - parser: `compiler/frontend/parser/`
   - type checker: `compiler/frontend/checker/type_checker.cc`
   - AST to KIR compiler: `compiler/backend/compiler/compiler.cc`
   - KIR op definitions: `compiler/ir/kir.h`, `compiler/ir/lowering_op.h`, related `.cc` files
   - LLVM lowering: `compiler/backend/codegen/llvm/llvm_function_lowerer.cc`
   - runtime native functions: `runtime/kinglet_rt_native.cc`, `runtime/kinglet_rt_value.h`

   Done when every edited subsystem is accounted for and no behavior claim relies only on prior chat context.

3. **Prove current behavior before changing it.**
   For syntax/API questions, create a small temporary `.kl` file and run:
   ```bash
   out/Default/kinglet --check /tmp/probe.kl
   out/Default/kinglet /tmp/probe.kl
   ```
   Done when the answer is backed by compiler output or source code.

4. **Edit with matching tests.**
   Prefer small, focused tests:
   - exec behavior: `tests/exec/cases/<feature>.kl`
   - type-check failures: `tests/sema/fail/<feature>.kl`
   - type-check passes: `tests/sema/pass/<feature>.kl`

   Done when new behavior has a passing test and invalid behavior has a failing sema test when applicable.

5. **Build both relevant compiler outputs.**
   ```bash
   ninja -C out/Default kinglet
   ninja -C out/Llvm kinglet
   ```
   Done when both builds complete. Existing warnings may remain; do not introduce avoidable new warnings.

6. **Run verification.**
   Minimum before PR:
   ```bash
   git diff --check
   bash tests/sema/run.sh
   bash tests/exec/run.sh
   ```
   Done when sema and exec summaries are green. If CI has broader suites, watch PR checks after opening.

7. **Update design docs and vault when behavior changes.**
   - ADR repo: `/home/sentomk/Code/kinglet-lang/ADRs`
   - Obsidian vault: `/home/sentomk/notes/kinglet`
   - Headless sync: `su - sentomk -c 'export PATH="/root/.hermes/node/bin:$PATH"; ob sync --path /home/sentomk/notes/kinglet 2>&1'`

   Done when docs no longer describe removed or changed behavior and `ob sync` reports fully synced.

8. **Open a fork PR.**
   ```bash
   git add <files>
   git commit -m "<conventional message>"
   git push -u origin <branch-name>
   gh pr create -R kinglet-lang/bootstrap --base canon --head sentomk:<branch-name> --title "<title>" --body '<body>'
   ```
   Done when the PR URL is returned and checks are running.

## PR Body Shape

Use exactly these headings:

```markdown
**Summary**

<short summary>

**Changes**

- <change>
- <change>

**Testing**

- `git diff --check`
- `ninja -C out/Default kinglet`
- `ninja -C out/Llvm kinglet`
- `bash tests/sema/run.sh`
- `bash tests/exec/run.sh`
```

Keep descriptions factual and avoid internal scratch labels. If the user has requested no special labels, describe fixes by behavior, not by temporary bug numbers.

## Language Behavior Verification

For parser syntax, inspect parser code and run probes. Examples:

- `if` and `while` use `condition_expression()`, so parenthesized and unparenthesized conditions are both accepted.
- C-style `for` consumes an opening parenthesis after `for`, so `for (...)` is required.
- Runtime namespaces such as `io`, `fs`, `sys`, and `txt` are compiler-known names gated by `using`.

Do not write ADRs or vault notes from memory alone. Verify against source or compiler output first.

## Common Pitfalls

1. **Editing only the type checker.** Many features also need AST to KIR lowering, KIR op names, LLVM lowering, runtime declarations, and runtime implementations.

2. **Forgetting `out/Llvm`.** The harness uses `out/Llvm/kinglet`; building only `out/Default` can miss test failures.

3. **Trusting old docs.** The vault and ADRs can lag implementation. Source and test output win.

4. **Leaving stale public API mentions.** After removing an API, search bootstrap, ADRs, and vault notes for old spellings.

5. **Mac link differences.** If runtime code uses platform library symbols, ensure generated program linking works on macOS as well as Linux and Windows.

6. **Assuming current session skill discovery.** A newly added repo skill may not be visible to the running session's skill loader. Verify by file contents and a future fresh load.

## Verification Checklist

- [ ] Worktree branch is based on current `upstream/canon`
- [ ] Source paths for parser/checker/lowering/runtime changes are all inspected
- [ ] New or changed behavior has exec or sema coverage
- [ ] `git diff --check` passes
- [ ] `ninja -C out/Default kinglet` passes
- [ ] `ninja -C out/Llvm kinglet` passes when runtime or LLVM behavior changed
- [ ] `bash tests/sema/run.sh` passes
- [ ] `bash tests/exec/run.sh` passes
- [ ] ADR and vault updates are made when behavior changed
- [ ] PR body includes `**Summary**`, `**Changes**`, and `**Testing**`
- [ ] PR checks are watched to completion when opened
