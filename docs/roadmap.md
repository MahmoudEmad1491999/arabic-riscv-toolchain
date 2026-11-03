# Roadmap

| Phase | Scope | Done when |
|---|---|---|
| 0 | Spec, assumptions, reference tools (GNU riscv64 binutils, qemu-riscv64), transliterator to GAS, shared C modules (runtime layer, UTF-8, numbers). | Unit tests pass; Arabic hello world assembles via the transliterator + GNU as and runs under qemu-riscv64. |
| 1 | Assembler v0 in C: RV64I, one file, no pseudo-instructions, two passes, `عالي:`/`أدنى:` relocations, ELF64 relocatable `.o`. | Every test file produces section bytes, symbols and relocations identical to GNU as on the transliteration; linked programs run under qemu. |
| 2 | Assembler v1: pseudo-instructions, pc-relative relocations, full expressions, all directives, M extension. | Same byte comparison on a larger corpus. |
| 3 | Linker: static ELF linking. | Output matches GNU ld behaviour; programs run. |
| 4 | Language design and compiler in C, emitting Arabic assembly. | Compiler test suite runs end to end on our assembler and linker. |
| 5 | Bootstrap: assembler, linker and compiler rewritten in the new language. | Stage 2 and stage 3 binaries are byte-identical. |
| 6 | Expansion: C, F/D, A extensions, RV32, debug info. | — |

## Phase 0 status

- [x] Spec and assumptions documented
- [x] Reference tools installed in the dev environment (see README)
- [x] Shared C modules: runtime layer, UTF-8, character classes, numbers (+ unit tests)
- [x] Transliterator (tools/translit.py) and oracle runner
- [x] Arabic hello world runs via the oracle path
