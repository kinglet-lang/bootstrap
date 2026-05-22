# TODO

## Next

- [ ] Error message improvements (suggest `using io;`, `using namespace io;`)
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
- [ ] import/export semantics
- [ ] REPL: re-run from sourced strings (hoist local `println` hack)
- [ ] Scanner: `identifier_type()` builds unordered_map every call
- [ ] Consider visitor pattern for `dynamic_cast` dispatch
