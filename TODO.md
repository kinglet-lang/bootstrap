# TODO

## Next

- [ ] Error message improvements (suggest `using io;` when `io::` used without it)
- [ ] `using io::out;` selective import syntax

## P2: Types & Patterns

- [ ] struct definitions
- [ ] enum definitions
- [ ] Exhaustiveness checking for inspect
- [ ] Full pattern matching (destructuring, guards)
- [ ] NaN-boxing migration (optional)

## P3: Stdlib & Toolchain

- [ ] Trait system
- [ ] Standard library
- [ ] Module system / package manager

## P4: Concurrency

- [ ] spawn / channel / select
- [ ] Work-stealing scheduler

## Known Gaps

- [ ] Multi-function calls (hoist `main()`-only limitation)
- [ ] Closures / lambda
- [ ] Array/Map literals
- [ ] Generics `<T>`
- [ ] Scanner: `identifier_type()` builds unordered_map every call
- [ ] Consider visitor pattern for `dynamic_cast` dispatch

## Done

- [x] LSP: diagnostics, scope-aware completion, go-to-definition, hover
- [x] LSP: snippet completions (if/for/while/inspect/main/using)
- [x] LSP: `using io;` triggers `out/err/in` → `io::out` completion
- [x] LSP: type keywords prioritized in completion
- [x] LSP: keyword completions insert trailing space
- [x] VSCode extension migrated to vscode-languageclient
- [x] VSCode file icon theme (light/dark)
- [x] Remove `print()` builtin (replaced by `io::out`)
- [x] Remove `import` keyword
- [x] REPL: auto-detect return type, strip trailing `;`, suppress null
- [x] `io::` requires `using io;` (not unconditional)
