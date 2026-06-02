# TODO

## Next

- [ ] KBC P2: self-hosted compiler .kbc serialization
- [ ] KBC P3: variable-length operand encoding
- [ ] Remove `self` keyword (see sh decisions/0001)
- [ ] Redesign trait system (see sh decisions/0002)

## P1: Syntax & Expressiveness (WG21-inspired)

- [x] Array methods: `len()`, `push()`, `pop()`, `remove()`, `contains()`, `clear()`
- [x] Chained comparisons (P0893): `1 <= x <= 10` → parser desugars to `&&`
- [x] Pipeline operator (P2011): `data |> filter |> map |> sum`
- [x] Implicit return (P0927): last expression in block is the return value
- [x] Structured unpacking (P1858): `let [a, b, ...rest] = arr;`
- [x] `guard` early-exit: `guard x > 0 else { return -1; }` — compiler enforces else must terminate
- [ ] `once` lazy init block: memoize first evaluation, zero-cost on subsequent calls
- [ ] `retry N { ... }` loop: built-in retry semantics with optional delay
- [ ] Inline tests: `test "name" { ... }` blocks, compiled out in release, run with `kinglet test`
- [ ] `scope` resource management: auto-call `.close()` on scope exit without RAII classes

## P2: Types & Patterns

- [x] Pattern matching: `match` expression with enum, binding, array, wildcard, literal, and guard patterns
- [x] Pattern guards using `if (...)`
- [x] Explicit binding with `let` in match patterns
- [x] Structured binding patterns for arrays (`[a, b, ...rest] = arr`)
- [x] Exhaustiveness checking for enum match (warning on missing variants)
- [x] Error propagation `try` operator + null coalescing `??` operator
- [ ] Error propagation `?` postfix operator (requires Result/Optional type)
- [ ] Zero-overhead optional (P2723): `int? x = null;` with niche optimization
- [ ] `[[nodiscard]]` for functions (P1029): warn on unused return values
- [ ] Struct patterns in match
- [ ] Struct patterns in structured binding

## P3: Stdlib & Toolchain

- [x] Map literals and `Map` type
- [x] Trait system (Rust-style; redesign pending — see sh decisions/0002)
- [ ] Standard library (collections, result, option, string, math, iter)
- [ ] Module system / package manager
- [ ] Closures / lambda

## P4: Concurrency (deferred)

- [ ] Structured concurrency (P2504): `scope { spawn f(); spawn g(); }`
- [ ] spawn / channel / select
- [ ] Work-stealing scheduler

## P5: Performance & Safety

- [ ] NaN-boxing migration
- [ ] Trivial relocatability (P1144 / P2786): move as memcpy

## Done

- [x] Struct definitions
- [x] Enum definitions with payload variants
- [x] Dynamic arrays: `T[]`, array literals, indexing, assignment, and bounds checks
- [x] Operators: `%` (modulo), `&&`/`||` (short-circuit), `~` (bitwise NOT)
- [x] Generics `<T>` (monomorphization: structs + functions)
- [x] TypeChecker: report unknown type names (no longer silently treated as `int`)
- [x] TypeChecker: validate struct literal field count and value types
- [x] TypeChecker: validate field assignment value type
- [x] REPL: fix stale `import io;` fallback → `using io;`
- [x] Scanner: `identifier_type()` uses a static keyword map
- [x] Multi-function support (forward references, parameters, recursion)
- [x] LSP: diagnostics, scope-aware completion, go-to-definition, hover
- [x] LSP: snippet completions (if/for/while/inspect/main/using)
- [x] LSP: `using io;` triggers `out/err/in` → `io::out` completion
- [x] LSP: type keywords prioritized in completion
- [x] LSP: keyword completions insert trailing space
- [x] LSP: document symbols (outline view), signature help (parameter hints)
- [x] LSP: wider diagnostic underlines (full token length)
- [x] LSP: io method completion (io::out. → line, io::in. → secret)
- [x] Golden tests: all modules (arithmetic, comparison, logic, structs, enums, inspect, generics, control flow, recursion, io methods)
- [x] I/O: `io::out.line`, `io::err.line`, `io::in.secret` method syntax
- [x] CI workflow with golden tests on push/PR
- [x] VSCode extension migrated to vscode-languageclient
- [x] VSCode file icon theme (light/dark)
- [x] Remove `print()` builtin (replaced by `io::out`)
- [x] Remove `import` keyword
- [x] REPL: auto-detect return type, strip trailing `;`, suppress null
- [x] `io::` requires `using io;` (not unconditional)
- [x] KBC P0: strip debug mode (`--strip-debug`)
- [x] KBC P1: constant pool deduplication
- [x] Self-hosting round-trip verified (byte-identical bytecode)
- [x] Add CLI/golden tests for successful runs, diagnostics, bytecode dumps, and regressions
- [x] I/O API: `io::in.line(prompt)` and `io::in.secret(prompt)`
- [x] Error message improvements (suggest `using io;` when `io::` used without it)
