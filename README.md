# Arabic RISC-V toolchain

An assembler, linker and compiler for RISC-V whose source files are written in Arabic. Written first in C, then rewritten in its own language and bootstrapped.

Sources are written in Vim Arabic mode on a terminal with no BiDi, so files are stored in **typing order**. Numbers are typed least significant digit first, so 1234 is typed ٤ ٣ ٢ ١ and reads correctly on screen. Files will look wrong in BiDi-aware viewers, including GitHub's file view. That is expected: the toolchain is for personal use.

## Status

Phase 0 (spec and test infrastructure) is done. Next is Phase 1, the assembler v0 in C. See [docs/roadmap.md](docs/roadmap.md).

## Documents

- [docs/spec-assembler.md](docs/spec-assembler.md): the assembler language: numbers, brackets, identifiers, registers, mnemonics, directives
- [docs/assumptions.md](docs/assumptions.md): working assumptions that are not yet confirmed
- [docs/decisions.md](docs/decisions.md): decisions made by the owner
- [docs/roadmap.md](docs/roadmap.md): phases and status

## Layout

```
src/common/    shared C modules: runtime layer (rt), UTF-8, Arabic character classes, numbers
tests/unit/    C unit tests
tests/asm/     Arabic assembly test programs (.تجميع) + expected stdout (.out) / exit code (.code)
tools/         translit.py (Arabic -> GNU as syntax, test oracle only), run_oracle.sh
```

## Building and testing

Requirements (Debian/Ubuntu):

```sh
sudo apt install build-essential python3 binutils-riscv64-linux-gnu qemu-user
```

```sh
make unit     # C unit tests (built with ASan/UBSan)
make oracle   # every tests/asm/*.تجميع -> translit.py -> GNU as/ld -> qemu-riscv64
make test     # both
```

The oracle path is the reference that the assembler will be checked against from Phase 1 on: same source, our assembler versus GNU as, byte-for-byte.
