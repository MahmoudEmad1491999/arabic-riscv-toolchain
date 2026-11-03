# Notes for Claude

- The spec lives in docs/. Read docs/spec-assembler.md (and docs/spec-linker.md for the linker) and docs/assumptions.md before changing any lexer, parser, linker or test file.
- The owner decides the spec. Never change a rule silently. Propose options, and once decided update spec + assumptions + decisions together.
- Arabic test files are in FILE ORDER: numbers are least significant digit first (25 is written ٥٢). Double-check every literal you write.
- C code: C11, no dependencies, simple subset (it will be ported to the new language for bootstrapping). OS access only through src/common/rt.*. Number reading/printing only through src/common/number.*.
- Run `make test` before committing, and check its exit status (don't pipe it into tail).
