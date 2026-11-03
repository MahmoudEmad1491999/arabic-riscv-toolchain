# Linker specification (رابط, Phase 3)

`رابط` links RV64 ELF relocatable objects (ours, or GNU's) into a static Linux executable.

## Usage

```
رابط ملف_الخرج ملف_كائن١.o ملف_كائن٢.o ...
```

The output comes first, then the objects in link order (A29). The entry point is the global symbol `_بداية` (A31). Exit codes: 0 ok, 1 link errors, 2 usage or I/O. Errors are in Arabic, like the assembler's.

## Layout (A30)

| Segment | Address | Contents |
|---|---|---|
| 1, read + execute | 0x10000 | ELF header, program headers, `.text`, `.rodata` |
| 2, read + write | next page; address congruent to the file offset (mod 4 KiB) | `.data`, then `.bss` |

Inside each output section the input sections are placed in command-line order, each at its own alignment (the same placement as GNU ld's `*(.text .text.*)`). Input sections are mapped by name (A33): `.text*` → `.text`, `.rodata*` / `.srodata*` → `.rodata`, `.data*` / `.sdata*` → `.data`, `.bss*` / `.sbss*` → `.bss`. Sections without the ALLOC flag (attributes, comments, debug info) are dropped. Any other allocated section is an error.

## Symbols

- Global symbols are resolved across all objects. Two strong definitions are an error. A strong definition beats a weak one, and an undefined weak symbol resolves to 0 (A34).
- An undefined strong symbol is an error naming the symbol and the object.
- The output symbol table holds the named local and global symbols, for debugging with objdump/gdb. Numeric `.L` labels are left out (A35).

## Relocations (A32)

| Type | Value written |
|---|---|
| R_RISCV_32 / R_RISCV_64 | S + A (32-bit: must fit) |
| R_RISCV_BRANCH | S + A − P, ±4 KiB |
| R_RISCV_JAL | S + A − P, ±1 MiB |
| R_RISCV_CALL / CALL_PLT | S + A − P split over auipc + jalr |
| R_RISCV_HI20 / LO12_I / LO12_S | S + A (absolute) |
| R_RISCV_PCREL_HI20 | S + A − P |
| R_RISCV_PCREL_LO12_I / S | low 12 bits of the PCREL_HI20 found at the label it points to |
| R_RISCV_RELAX | ignored: no linker relaxation |

Any other type (ALIGN, GOT, TLS, ...) is an error. Instruction fields are cleared and rewritten, whatever the assembler left in them.

## Not yet (A36)

Static archives (`.a`), linker scripts, shared libraries and PIE, relaxation, `--gc-sections`.
