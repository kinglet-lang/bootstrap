# Fuzzing

Coverage-guided fuzz targets for the Kinglet front end, built on libFuzzer.
Fuzzing feeds the compiler large volumes of mutated input to surface crashes,
sanitizer violations, hangs, and out-of-memory conditions that hand-written
tests miss. It is one of the highest-value testing techniques for a
compiler's lexer and parser.

## Targets

Three targets, each wrapping a successively deeper slice of the pipeline:

- `fuzz_lexer` — bytes through `Scanner::scan_tokens()`. Catches tokenizer
  crashes and UB.
- `fuzz_parser` — bytes through the lexer and `Parser::parse()`. Catches
  parser crashes, hangs, and no-progress loops. Reported `ParseError`s are
  expected and ignored; only a crash/hang/OOM is a bug.
- `fuzz_pipeline` — bytes through lex, parse, then `TypeChecker::check()`.
  Only cleanly-parsed programs reach the checker, matching how the real
  driver invokes it.

## Running

The `scripts/fuzz.sh` helper builds and runs the targets:

```bash
scripts/fuzz.sh                 # all targets, 60s each
scripts/fuzz.sh parser 300      # parser only, 5 minutes
scripts/fuzz.sh pipeline 30     # pipeline only, 30s smoke
```

It configures an `out/Fuzz` build with:

```
gn gen out/Fuzz --args='is_debug=false use_libfuzzer=true sanitizer="address,undefined"'
```

then runs each target against its seed corpus under `tests/fuzz/corpus/<target>`.
Any defect halts the run and writes the offending input as
`crash-*` / `oom-*` / `timeout-*` next to the corpus for triage.

To run a target directly (e.g. to reproduce a specific artifact):

```bash
ninja -C out/Fuzz fuzz_parser
./out/Fuzz/fuzz_parser path/to/crash-abcd1234       # replay one input
./out/Fuzz/fuzz_parser tests/fuzz/corpus/parser     # keep fuzzing
```

## Corpus

`tests/fuzz/corpus/<target>/` holds committed inputs:

- `seed-*` — real Kinglet programs copied from the test suites. These give
  the fuzzer a productive starting point so it spends time on interesting
  code paths instead of rediscovering basic syntax.
- `regress-*` — inputs that once triggered a defect. They are replayed on
  every CI run so a fixed bug cannot silently regress.

The committed corpus is intentionally small. During a run libFuzzer expands
its in-memory corpus with thousands of mutated inputs; those are working
state, not committed. When a run uncovers a genuinely new and interesting
input worth keeping as a permanent seed, add it deliberately.

## Adding a regression seed

When a fuzz run finds a defect, keep the artifact as a regression test:

```bash
# after a crash writes e.g. tests/fuzz/corpus/parser/crash-abc123
mv tests/fuzz/corpus/parser/crash-abc123 \
   tests/fuzz/corpus/parser/regress-<short-description>-abc123
```

Fix the underlying bug, confirm the seed now passes
(`./out/Fuzz/fuzz_parser tests/fuzz/corpus/parser/regress-...`), and commit
both the fix and the seed together.

## CI

The `fuzz-smoke` job in `.github/workflows/ci.yml` builds the targets with
ASan+UBSan and runs a 60s smoke of each on every pull request. This replays
all regression seeds and spends a short budget hunting for new defects.
Deeper campaigns are run manually with a larger time budget via
`scripts/fuzz.sh`.

## Requirements

- A Clang toolchain with libFuzzer (`-fsanitize=fuzzer`) and the ASan/UBSan
  runtimes. The toolchain installed by `scripts/setup.sh` satisfies this.
- Fuzz targets are only generated when `use_libfuzzer=true` is passed to
  `gn gen`; normal builds are unaffected.
