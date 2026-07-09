# Clean up function overloading infrastructure

Fixes four architectural issues discovered during overload implementation.

## P0: SemanticContext sharing

**Problem:** TypeChecker and Compiler each own a separate `SemanticContext sema_` value member. Checker populates its copy; Compiler starts fresh with an empty one. Any state written during type checking is invisible to the compiler — we worked around this by writing resolved mangled names to AST nodes, but that's a bandage.

**Fix:**
- Add `SemanticContext *sema_ = nullptr` pointer (not value) to Compiler
- Add `SemanticContext &sema() const { return sema_; }` accessor to TypeChecker
- Add `void set_semantic_context(SemanticContext &sema)` to Compiler
- `main.cc`: `compiler.set_semantic_context(checker.sema());` after check
- Remove Compiler's `sema_.clear()` calls — TypeChecker owns lifecycle
- All `sema_.` → `sema_->` in compiler.cc

## P1: Overload map ownership

**Problem:** `function_overloads_` lives in `SemanticContext`, a shared bag of maps that exists mainly for import/module tracking. Overload resolution is purely a TypeChecker concern — the Compiler never reads overload sets (it uses AST annotations). Storing overloads in the shared context creates a false dependency and pollutes the boundary.

**Fix:**
- Move `function_overloads_` from `SemanticContext` to TypeChecker as a direct member
- Update all references in `type_checker.cc` from `sema_.function_overloads_` → `function_overloads_`
- Remove the `OverloadEntry`, `OverloadSet` includes from `semantic_context.h`

## P2: types_equal robustness

**Problem:** `types_equal` relies on `Type::type_id()` for scalar comparison. TypeId encodes width and signedness into a single enum value — correct today but fragile when typedefs or type aliases arrive, since the encoding has no room for user-defined types.

**Fix:** Replace `type_id()` comparison with structural field comparison:
- Compare `kind`, `nullable` first
- For Int: compare via `type_id()` (no separate width/signedness fields exist on Type — this is the only option for now; document the limitation)
- For other scalars (Bool, Char, String, Float, etc.): `type_id()` is definitive since there's exactly one TypeId per kind
- Containers: already handled recursively (no change needed)
- Struct/Enum: compare `name` (already done)

**Result:** The comparison logic becomes self-documenting about which fields matter and which encodings it relies on. No behavior change for any current type — strictly a documentation and structure improvement.

## P3: KIR function name dual purpose

**Problem:** `KirFunction::name` serves two roles: it's the display name for diagnostics/debug info, AND it's fed into `mangled_native_symbol()` for LLVM native symbols. With overloading, we stuff the mangled name into `KirFunction::name` so LLVM gets distinct symbols, but this breaks diagnostics (error messages show `id$int` instead of `id`). The `FunctionInfo` struct already has both `name` and `mangled_name` — `KirFunction` needs the same split.

**Fix:**
- Add `std::string mangled_name` to `KirFunction`
- `compile_function()`: pass both plain name and mangled name through to KIR
  - `fn.name` = plain name (for diagnostics)
  - `fn.mangled_name` = mangled name (for native symbol, empty if no overload)
- `attach_kir_metadata()`: use `fn.mangled_name` for `function_symbols`, keep `fn.name` for `function_names`
- `llvm_module_cache.cc`: use `fn.mangled_name` when computing cache keys
- `llvm_function_lowerer.cc`: use `fn.mangled_name` for LLVM function creation/lookup; use `fn.name` for debug metadata (`di->createFunction`)

## Files changed (estimated)
- `compiler/frontend/checker/type_checker.h` — add `function_overloads_`, `sema()` accessor
- `compiler/frontend/checker/type_checker.cc` — s/sema_.function_overloads_/function_overloads_/, types_equal doc
- `compiler/frontend/sema/semantic_context.h` — remove OverloadEntry/OverloadSet, remove `function_overloads_`
- `compiler/backend/compiler/compiler.h` — `sema_` value→pointer, add setter
- `compiler/backend/compiler/compiler.cc` — sema_.→sema_->, mangled name plumbing
- `compiler/ir/kir.h` — add `mangled_name` to KirFunction
- `compiler/ir/kir_recorder.cc` — set mangled_name in end_function
- `compiler/backend/codegen/llvm/llvm_module_cache.cc` — mangled_name for cache keys
- `compiler/backend/codegen/llvm/llvm_function_lowerer.cc` — mangled_name for native symbol, name for debug
- `compiler/driver/kinglet/main.cc` — wire compiler.set_semantic_context()
