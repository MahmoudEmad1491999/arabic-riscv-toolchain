# Object format specification (`.كائن`, Phase 4)

Our own relocatable object format. It lives next to ELF in the same codebase (decision 10): `مجمع` writes either, `رابط` reads either, and both produce the identical executable (the Phase 4 exit test). ELF stays as the bridge to GNU's tools.

Decided names (decision 33): extension `.كائن`; viewer tool `عارض`; symbol kinds `خاص` (private), `مصدر` (exported), `مستورد` (imported). Private symbols are stored by name (decision 34). The interface hash is 64-bit FNV-1a (decision 35).

## Conventions

- All integers in the binary file are little-endian. Regions start at 8-byte boundaries.
- Names are UTF-8, stored with their length; there is no terminating zero byte.
- The binary file is what the tools read. The text form (`عارض`) is for people only: no tool reads it back.

## Binary layout

| Region | Size | Contents |
|---|---|---|
| Header | 80 bytes | see below |
| Section contents | text, data, rodata sizes | the bytes of `نص`، `بيانات`، `ثوابت` in this order; `أصفار` (bss) has no bytes |
| Symbols | 24 × nsyms | symbol entries |
| Relocations | 24 × nrelocs | relocation entries |
| Names | names_size | the UTF-8 names, back to back |
| Interface | interface_size | module interface (see below; 0 for hand-written assembly) |

Header:

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | magic: the UTF-8 bytes of `كائن` |
| 8 | 4 | format version (1) |
| 12 | 4 | flags (0) |
| 16 | 4 × 8 | size of text, data, rodata, bss |
| 48 | 4 × 4 | alignment of text, data, rodata, bss (power of two, at most 4096) |
| 64 | 4 | nsyms |
| 68 | 4 | nrelocs |
| 72 | 4 | names_size |
| 76 | 4 | interface_size |

The file length must equal the sum of the regions exactly; the reader rejects anything else.

## Sections

Always the same four, with fixed numbers: 0 `نص` (text, code), 1 `بيانات` (data), 2 `ثوابت` (read-only data), 3 `أصفار` (zero-initialised). No section name table.

## Symbols

Entry (24 bytes): name_off u32, name_len u32, kind u8, shape u8, section u8, reserved u8, size u32, value u64.

- kind: 0 `خاص`، 1 `مصدر`، 2 `مستورد`.
- shape: 0 unknown (the assembler does not know), 1 function, 2 data. Reserved for the compiler's interface step.
- section: 0-3 as above, 4 absolute (value is the constant), 255 undefined (only kind `مستورد`).
- Private symbols come first in the table, then exported and imported ones, each group in creation order. Relocations refer to symbols by index.
- An exported symbol is defined; an imported one is not. Two exported definitions of one name are a link error, an imported name with no definition anywhere is a link error.
- Assembler labels of the form `.L…` that no relocation needs are not stored. The GNU-style hidden label that `la` needs for its PC-relative pair does not exist in our format (see below).

## Relocations

Entry (24 bytes): offset u64, section u8, kind u8, reserved u16, symbol u32, extra i64.

`offset` is inside `section`. `extra` is the addend, except for the two `نسبي_منخفض` kinds where it is the offset of the paired `نسبي_عال` instruction in the same section (the symbol field is then 0xFFFFFFFF). So the PC-relative high/low pair needs no extra label.

| Kind | Name | Meaning (same arithmetic as the linker spec) |
|---|---|---|
| 1 | `فرع` | conditional branch, ±4 KiB |
| 2 | `قفزة` | `jal`, ±1 MiB |
| 3 | `نداء` | `auipc`+`jalr` pair |
| 4 | `عال` | absolute high 20 bits |
| 5 | `منخفض_ي` | absolute low 12 bits, I-form |
| 6 | `منخفض_خ` | absolute low 12 bits, S-form |
| 7 | `نسبي_عال` | PC-relative high 20 bits |
| 8 | `نسبي_منخفض_ي` | low 12 bits of the pair, I-form |
| 9 | `نسبي_منخفض_خ` | low 12 bits of the pair, S-form |
| 10 | `كلمة` | 32-bit absolute value |
| 11 | `كلمة_مزدوجة` | 64-bit absolute value |

There is no relaxation marker: we never relax (A36).

## Text form (`عارض ملف.كائن`)

Prints to standard output, one item per line, in file order, numbers LSD-first like everywhere else:

```
كائن إصدار <n>
قسم <name> حجم <n> محاذاة <n>
رمز <خاص|مصدر|مستورد> <name> قسم <name|مطلق|غير_معرف> قيمة <n>
إعادة_توطين <kind> قسم <name> موضع <n> رمز <name> إضافة <n>
```

## Module interface (the Interface region)

Written by `مترجم` for every module it compiles (decisions 35-36). Objects made by `مجمع` from hand-written assembly have none (`interface_size` 0).

| Field | Size |
|---|---|
| module name length, then the dotted module name (`غرة.نصوص`) | 4 + n |
| interface hash: FNV-1a 64 of the text below (offset basis 14695981039346656037, prime 1099511628211) | 8 |
| source hash: FNV-1a 64 of the module's source file | 8 |
| number of imports, then for each: name length, name, the interface hash it was compiled against | 4 + … |
| text length, then the text | 4 + n |

The text is غرة source, in file order like every source file: the module's `استورد … باسم …؛` lines, every structure and enumeration (private ones without `عام`: exported signatures may use them, importers cannot name them), and the exported constants (evaluated), variables (`= غير_مهيأ`) and functions (signature, `؛` instead of a body). The reader rejects a region whose hash does not match its text.

- **Importing a compiled module:** when `a/b.غرة` is not found, the compiler looks for `a/b.كائن` in the same search paths, then in the libraries given on the command line, and parses the interface text.
- **Stale objects:** `رابط` compares, for every object with an interface, the recorded hash of each import with the interface hash of the object that provides that module. A mismatch is an error naming the module and the stale object. The same module twice in one link is an error.

## Libraries (`.مكتبة`, decision 38)

Made by `محزم مكتبة.مكتبة أ.كائن ب.كائن…`.

| Offset | Size | Field |
|---|---|---|
| 0 | 10 | magic: the UTF-8 bytes of `مكتبة` |
| 10 | 2 | version (1) |
| 12 | 4 | number of members |
| 16 | 24 × n | per member: offset u64, size u64, name offset u32, name length u32 |
| … | | member names, then the members (each a whole `.كائن`, at 8-byte boundaries) |

`رابط` links a member only when it exports a name that the program still needs (the entry `_بداية` counts as needed), repeating until nothing changes. Members carry their own symbol tables, so there is no separate index that could drift (A59).
