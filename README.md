# Arabic RISC-V toolchain

An assembler, linker and compiler for RISC-V whose source files are written in Arabic. Written first in C, then rewritten in its own language and bootstrapped.

Sources are written in Vim Arabic mode on a terminal with no BiDi, so files are stored in **typing order**. Numbers are typed least significant digit first, so 1234 is typed ٤ ٣ ٢ ١ and reads correctly on screen. Files will look wrong in BiDi-aware viewers, including GitHub's file view. That is expected: the toolchain is for personal use.

## Status

Phase 1 is done: `مجمع`, the RV64I assembler, produces ELF objects identical to GNU as (`-mno-relax`) on every test. Next is Phase 2. See [docs/roadmap.md](docs/roadmap.md).

## Documents

- [docs/spec-assembler.md](docs/spec-assembler.md): the assembler language: numbers, brackets, identifiers, registers, mnemonics, directives
- [docs/assumptions.md](docs/assumptions.md): working assumptions that are not yet confirmed
- [docs/decisions.md](docs/decisions.md): decisions made by the owner
- [docs/roadmap.md](docs/roadmap.md): phases and status

## Using the assembler

```sh
make                                   # builds build/مجمع
./build/مجمع برنامج.تجميع برنامج.o
riscv64-linux-gnu-ld -e _بداية برنامج.o -o برنامج    # our linker comes in Phase 3
qemu-riscv64 ./برنامج
```

Errors are printed in Arabic, reversed for a plain left-to-right terminal (set `DIAG_ORDER=file` for file order).

## Layout

```
src/common/    shared C modules: runtime layer (rt), UTF-8, Arabic character classes, numbers,
               byte buffers, ELF constants, Arabic diagnostics
src/as/        the assembler: lexer, tables (registers/mnemonics/directives), two-pass core + ELF writer
tests/unit/    C unit tests
tests/asm/     Arabic assembly test programs (.تجميع) + expected stdout (.out) / exit code (.code);
               .norun marks encoding-only tests
tests/errors/  programs that must fail, with the expected Arabic message (.err)
tests/gen/     generators for the big tests (they write numbers in file order for you)
tools/         translit.py (Arabic -> GNU syntax, oracle only), elfcmp.py, run_*.sh, diff_fuzz.py
```

## Building and testing

Requirements (Debian/Ubuntu):

```sh
sudo apt install build-essential python3 binutils-riscv64-linux-gnu qemu-user
```

```sh
make unit     # C unit tests (built with ASan/UBSan)
make oracle   # every tests/asm/*.تجميع -> translit.py -> GNU as/ld -> qemu-riscv64
make asm      # our assembler vs GNU as on every test + error tests
make test     # all of the above
make fuzz     # differential fuzzing against GNU as (slower)
```

The oracle path is the reference that the assembler will be checked against from Phase 1 on: same source, our assembler versus GNU as, byte-for-byte.
