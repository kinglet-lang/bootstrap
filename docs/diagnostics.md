# Kinglet Diagnostic Codes

Stable identifiers for compiler diagnostics. See [ADR 0031](../../ADRs/0031-diagnostic-system.md)
for the scheme, categories, and rendering rules.

## Format

- Codes are `K` + a category digit (`0-9`, or `10+` for later categories) + a
  serial number: e.g. `K2001`, `K10002`.
- Codes are **stable** once assigned. When a diagnostic is retired the code is
  retired with it — never reused for a different meaning.
- Message text may change across versions. The code identifies the *category*,
  not the message wording.

## Categories

| Range | Category |
|-------|----------|
| `K0xxx` | Lexical, syntax, and parse |
| `K1xxx` | Names, declarations, and scope |
| `K2xxx` | Type system and type inference |
| `K3xxx` | Value category, mutability, and assignment |
| `K4xxx` | Ownership, transfer, and move |
| `K5xxx` | Borrow, reference, and lifetime |
| `K6xxx` | Initialization, destruction, and resource lifecycle |
| `K7xxx` | Functions, parameters, and calls |
| `K8xxx` | Control flow and reachability |
| `K9xxx` | Expressions and operators |
| `K10xxx` | Optional, fallible cast, and unwrap |
| `K11xxx` | Arrays, indexing, slicing, and dimensions |
| `K12xxx` | Generics and compile-time evaluation |
| `K13xxx` | Pattern matching and exhaustiveness |
| `K14xxx` | FFI, ABI, and external symbols |
| `K15xxx` | Modules, imports, and visibility |
| `K16xxx` | Concurrency, atomics, and thread safety |
| `K17xxx` | Attributes, built-in capabilities, and language constraints |
| `K18xxx` | Unused code, style, and suspicious patterns |
| `K19xxx` | Compiler limits and internal diagnostics |

## Assigned codes (initial batch)

| Code | Severity | Description | Example message | Secondary label |
|------|----------|-------------|-----------------|-----------------|
| `K1001` | error | Name resolution failure — unknown type or undeclared variable. | `Unknown type 'Foo'.` / `Undeclared variable 'x'.` | — |
| `K1002` | error | Redeclaration of a name in the same scope. | `Variable 'x' already declared.` | first declaration site |
| `K2001` | error | Type mismatch on assignment or initialization. | `Cannot assign string to variable of type int.` | declaration site of the target |
| `K3002` | error | Assignment to a `const` binding after initialization. | `Cannot assign to const variable 'x'.` | declaration site of the const |
| `K4001` | error | Use of a value that was moved / transferred elsewhere. | `Variable 'x' was transferred and is no longer valid.` | transfer site |
| `K5001` | error | Conflicting borrow — a new borrow overlaps a live one, or a use overlaps a live mutable borrow. | `Conflicting borrow of 'x'.` / `Cannot use 'x' while it is mutably borrowed.` | earlier borrow site |
| `K7001` | error | Overload resolution failed — no viable candidate for the call. | `No matching overload.` | every candidate's declaration |
| `K7007` | error | Non-void function reaches a return statement with no value. | `Non-void function must return a value.` | — |
| `K7010` | warning | Expression statement's result is discarded silently. | `Expression result is unused.` | — |
| `K10002` | error | Fallible cast (`string → int`, etc.) not handled with `?:` or postfix `?`. | `Fallible cast from string to int must be handled with '?:' or postfix '?'.` | — |
| `K15001` | error | Module resolution failure — `using`/`import` references an unknown module. | `Unknown module 'foo'.` | — |
| `K18001` | warning | Local variable declared but never read. | `Unused variable 'x'.` | — |

Codes not yet assigned in-tree but reserved by ADR 0031:

- `K2008` (recursive layout), `K4005` (partial move), `K5004` (lifetime),
  `K6001` (definite assignment), `K8001` (unreachable), `K10001` (`T?`
  where `T` is required), `K11001` (out-of-bounds), `K13001` (non-exhaustive
  match).

New codes are added by appending to this table — no separate ADR required
(ADR 0031 D2).
