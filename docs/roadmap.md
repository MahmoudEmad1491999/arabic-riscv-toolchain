# Roadmap

| Phase | Scope | Done when |
|---|---|---|
| 0 | Spec, assumptions, reference tools (GNU riscv64 binutils, qemu-riscv64), transliterator to GAS, shared C modules (runtime layer, UTF-8, numbers). | Unit tests pass; Arabic hello world assembles via the transliterator + GNU as and runs under qemu-riscv64. |
| 1 | Assembler v0 in C: RV64I, one file, no pseudo-instructions, two passes, `عالي:`/`أدنى:` relocations, ELF64 relocatable `.o`. | Every test file produces section bytes, symbols and relocations identical to GNU as on the transliteration; linked programs run under qemu. |
| 2 | Assembler v1: pseudo-instructions, pc-relative relocations, full expressions, all directives, M extension. | Same byte comparison on a larger corpus. |
| 3 | Linker: static ELF linking. | Output matches GNU ld behaviour; programs run. |
| 4 | Language design and compiler in C, emitting Arabic assembly. Together with the language's linkage model (which kinds of symbols the language has), design our own object format: a binary form for the tools and a text form for people to read (read-only, never executed). Add it to `مجمع` and `رابط` next to ELF, in the same codebase. | Compiler test suite runs end to end on our assembler and linker. Every test links to the identical executable from ELF objects and from our objects. |
| 5 | Own executable format and our own loader. The Linux kernel only starts ELF, so the loader is a small static ELF program, built with our tools: it reads an executable in our format, maps its segments, sets up the stack (arguments, environment) and jumps to the entry point. | Every test program runs both as an ELF executable and through our loader, with the same output and exit code. |
| 6 | Bootstrap: assembler, linker, compiler and loader rewritten in the new language. | Stage 2 and stage 3 binaries are byte-identical. |
| 7 | Expansion: C, F/D, A extensions, RV32, debug info, a bare-metal loader for `qemu-system-riscv64` (no Linux underneath). | — |

## Phase 0 status

- [x] Spec and assumptions documented
- [x] Reference tools installed in the dev environment (see README)
- [x] Shared C modules: runtime layer, UTF-8, character classes, numbers (+ unit tests)
- [x] Transliterator (tools/translit.py) and oracle runner
- [x] Arabic hello world runs via the oracle path

## Phase 1 status

- [x] Lexer with every lexical rule and error of the spec
- [x] Two-pass assembler: all RV64I instructions, all directives, `عالي:`/`أدنى:` relocations, numeric local labels
- [x] ELF64 relocatable output, linkable with GNU ld
- [x] Arabic diagnostics (A2, A18)
- [x] Tests: every instruction compared with GNU as (`tests/asm/كل_التعليمات.تجميع`), programs run under qemu, 42 error tests, differential fuzzing (`make fuzz`)



## Phase 2 status

- [x] Pseudo-instructions with the approved names, expanded exactly like GNU (`حمل_قيمة` follows GNU's load_const algorithm)
- [x] pc-relative relocations `عالي_نسبي:` / `أدنى_نسبي:`, R_RISCV_CALL_PLT
- [x] M extension (names: assumption A24)
- [x] Tests: `أشباه_التعليمات` (every pseudo, 220+ `حمل_قيمة` values, all new relocations) byte-identical to GNU; `طباعة_أعداد` runs under qemu (call/return, division, printing numbers); new error tests; fuzzing over all test programs
- [ ] Bitwise and shift operators inside expressions: needs names (open item)
- [ ] More directives (e.g. `.space`/fill values, `.balign`): only if the compiler needs them


## Phase 3 status

- [x] `رابط`: reads RV64 ELF objects, resolves global/weak symbols, lays out two segments, applies every relocation our assembler emits, writes a static executable with a symbol table
- [x] Arabic link errors: undefined symbol, duplicate definition, missing `_بداية`, value out of range, unmatched `أدنى_نسبي`, bad object, unsupported section/relocation
- [x] Tests: every runnable program is linked by `رابط` and by GNU ld at the same addresses, and the loaded images are compared; a three-file program; the encoding tests linked against a stub (all relocation types); link-error tests; corrupted-object fuzzing; differential fuzzing now links too
- [ ] Static archives (`.a`): with the runtime library in Phase 4


## Phase 4 status

- [x] Language designed and specified (docs/spec-language.md, decisions 15–32)
- [x] `مترجم` first version, hand-written in C: lexer (shared character and number rules), recursive-descent parser, type checker with every compile-time rule (exact literal typing, no implicit conversions, no hidden padding, definite assignment, all-paths-return, exhaustive `حسب`, no shadowing), code generator to Arabic assembly following the RISC-V LP64 ABI (slices, structures by value, function values)
- [x] Runtime in our assembly (`lib/runtime`) and the first standard-library module `غرة.نصوص`, written in غرة
- [x] Tests: programs compiled → assembled → linked by our tools and run under qemu (arithmetic semantics, structures, enumerations, loops, pointers, optionals, modules, run-time stops), 37 compile-error tests, compiler fuzzing under sanitizers
- [ ] Our own object format (binary + text) with module interfaces; separate compilation; `مترجم` building the executable in one command
- [ ] `بنية مضغوطة`, calls with more than eight argument words
- [ ] More of the standard library (files, memory, strings)
