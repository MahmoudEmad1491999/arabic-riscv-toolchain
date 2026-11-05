# Arabic RISC-V toolchain

An assembler, linker and compiler for RISC-V whose source files are written in Arabic. Written first in C, then rewritten in its own language and bootstrapped.

Sources are written in Vim Arabic mode on a terminal with no BiDi, so files are stored in **typing order**. Numbers are typed least significant digit first, so 1234 is typed ٤ ٣ ٢ ١ and reads correctly on screen. Files will look wrong in BiDi-aware viewers, including GitHub's file view. That is expected: the toolchain is for personal use.

## Status

Phases 1–3 are done. `مجمع` assembles RV64IM plus the pseudo-instructions (`حمل_قيمة`, `حمل_عنوان`, `استدعاء`, `عودة`, ...) into ELF objects identical to GNU as (`-mno-relax`). `رابط` links them into static executables whose loaded image matches GNU ld at the same addresses. Phase 4 is under way: the language غرة (Ghora) is specified and its compiler `مترجم` compiles real programs. Still to come in Phase 4: our own object format (binary + readable text) next to ELF, with separate compilation. Phase 5 adds our own executable format and loader. See [docs/roadmap.md](docs/roadmap.md).

## Documents

- [docs/spec-assembler.md](docs/spec-assembler.md): the assembler language: numbers, brackets, identifiers, registers, mnemonics, directives
- [docs/spec-linker.md](docs/spec-linker.md): the linker: usage, layout, symbols, relocations
- [docs/spec-language.md](docs/spec-language.md): the language, **غرة** (Ghora), draft under discussion; generated from `spec-language.src.md` by `make spec`
- [docs/architecture.md](docs/architecture.md): how the assembler and linker are built: diagrams, data structures, algorithms
- [docs/assumptions.md](docs/assumptions.md): working assumptions that are not yet confirmed
- [docs/decisions.md](docs/decisions.md): decisions made by the owner
- [docs/roadmap.md](docs/roadmap.md): phases and status

## Using the assembler

```sh
make                                   # builds build/مجمع and build/رابط
./build/مجمع برنامج.تجميع برنامج.o
./build/مجمع مكتبة.تجميع مكتبة.o
./build/رابط برنامج برنامج.o مكتبة.o   # output first, then objects
qemu-riscv64 ./برنامج
```

A غرة program:

```sh
./build/مترجم برنامج.غرة برنامج.تجميع lib      # lib/ holds the standard library غرة
./build/مجمع برنامج.تجميع برنامج.o
./build/مجمع lib/runtime/تشغيل.تجميع تشغيل.o
./build/رابط برنامج برنامج.o تشغيل.o
qemu-riscv64 ./برنامج
```

Assembly programs start at the global label `_بداية`. Errors are printed in Arabic, reversed for a plain left-to-right terminal (set `DIAG_ORDER=file` for file order).

## Layout

```
src/common/    shared C modules: runtime layer (rt), UTF-8, Arabic character classes, numbers,
               byte buffers, ELF constants, Arabic diagnostics
src/as/        the assembler: lexer, tables (registers/mnemonics/directives), two-pass core + ELF writer
src/ld/        the linker: object reader, symbol resolution, layout, relocations, executable writer
src/cc/        the غرة compiler: lexer, recursive-descent parser, type checker, code generator
lib/           the standard library غرة (in غرة) and the runtime (in Arabic assembly)
tests/unit/    C unit tests
tests/asm/     Arabic assembly test programs (.تجميع) + expected stdout (.out) / exit code (.code);
               .norun marks encoding-only tests
tests/errors/  programs that must fail, with the expected Arabic message (.err)
tests/link/    multi-file programs (one directory each) + expected .out/.code
tests/link-errors/  programs that must fail to link, with the expected message
tests/cc/      غرة programs + expected .out/.code; subdirectories hold imported test modules
tests/cc-errors/  غرة programs that must not compile, with the expected message
tests/gen/     generators for the big tests (they write numbers in file order for you)
tools/         translit.py (Arabic -> GNU syntax, oracle only), elfcmp.py, execcmp.py, run_*.sh, diff_fuzz.py
```

## Building and testing

Requirements (Debian/Ubuntu):

```sh
sudo apt install build-essential python3 binutils-riscv64-linux-gnu qemu-user qemu-system-misc
```

```sh
make unit     # C unit tests (built with ASan/UBSan)
make oracle   # every tests/asm/*.تجميع -> translit.py -> GNU as/ld -> qemu-riscv64
make asm      # our assembler vs GNU as on every test + error tests (linked with رابط)
make link     # our linker vs GNU ld: multi-file programs, coverage, errors
make cc       # the غرة compiler: programs run under qemu + compile-error tests
make os       # the operating system: bare-metal kernels on qemu-system-riscv64
              # (build and run it: tools/build_os.sh, then qemu-system-riscv64 -machine virt
              #  -bios none -m 128M -nographic -kernel os/بناء/نواة)
make docs     # docs/*.md as PDF in build/docs, Arabic shown as in the editor
              # (needs pandoc and: pip install weasyprint arabic-reshaper)
make test     # all of the above
make fuzz     # differential fuzzing against GNU as and GNU ld (slower)
```

The oracle path is the reference that the assembler will be checked against from Phase 1 on: same source, our assembler versus GNU as, byte-for-byte.
