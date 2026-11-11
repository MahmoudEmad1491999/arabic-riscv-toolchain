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
| Header | 88 bytes | see below |
| Section contents | text, data, rodata sizes | the bytes of `نص`، `بيانات`، `ثوابت` in this order; `أصفار` (bss) has no bytes |
| Symbols | 24 × nsyms | symbol entries |
| Relocations | 24 × nrelocs | relocation entries |
| Names | names_size | the UTF-8 names, back to back |
| Interface | interface_size | module interface (see below; 0 for hand-written assembly) |
| Debug | debug_size | debug information `تنقيح` (see below; 0 if none), at an 8-byte boundary |

Header:

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | magic: the UTF-8 bytes of `كائن` |
| 8 | 4 | format version (2; version 1 had no debug region and is no longer read) |
| 12 | 4 | flags (0) |
| 16 | 4 × 8 | size of text, data, rodata, bss |
| 48 | 4 × 4 | alignment of text, data, rodata, bss (power of two, at most 4096) |
| 64 | 4 | nsyms |
| 68 | 4 | nrelocs |
| 72 | 4 | names_size |
| 76 | 4 | interface_size |
| 80 | 4 | debug_size |
| 84 | 4 | reserved (0) |

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

## Debug information (the Debug region, decision 64)

Tables of fixed-size rows that refer to each other by index; no attribute forms, no bytecode, no state machine. Every name is (offset, length) into the region's own names. `بلا` (0xFFFFFFFF) is the "none" index. A code position is an offset into this object's `نص`; in an executable (and in the `.تنقيح` section of an ELF executable) it is an address. Symbol indexes refer to this object's symbol table (or are `بلا`); in an executable everything is resolved: function symbols are `بلا` and a global variable's `where` is its address (A88).

Region header (48 bytes): the row count of each of the 9 tables below (9 × u32), names_size u32, 8 reserved bytes (0). Then the tables in this order, then the names.

| # | Table | Row | Fields |
|---|---|---|---|
| 0 | Files | 16 | name_off u32, name_len u32, FNV-1a hash of the file's bytes u64 |
| 1 | Lines | 24 | code u64, file u32, line u32, column u32, reserved u32. In code order; a row covers the code up to the next row |
| 2 | Functions | 48 | name_off, name_len, symbol u32, file u32, start u64, end u64, line u32, frame size u32, result type u32 (or بلا), reserved u32 |
| 3 | Scopes | 24 | function u32, parent scope u32 (or بلا; parents come before their children), start u64, end u64 |
| 4 | Variables | 32 | name_off, name_len, type u32, scope u32, kind u8 (0 parameter, 1 local, 2 global), 7 reserved bytes, where i64: the offset from `محفوظ٠` for parameters and locals, the symbol index for globals (whose scope is بلا) |
| 5 | Types | 32 | kind u8, flags u8, reserved u16, name_off u32, name_len u32, a u32, b u64, c u32, d u32 |
| 6 | Fields | 24 | name_off, name_len, type u32, reserved u32, offset u64 |
| 7 | Members | 16 | name_off, name_len, value i64 |
| 8 | Parameters | 8 | type u32, reserved u32 (the parameter types of function types) |

Type kinds and the meaning of a, b, c, d (unused ones are 0; only structures and enumerations have a name):

| Kind | Name | a | b | c, d |
|---|---|---|---|---|
| 1 | integer | width in bits | | |
| 2 | boolean | | | |
| 3 | character | | | |
| 4 | real | width in bits | | |
| 5 | pointer | target type | | |
| 6 | optional pointer | the pointer type | | |
| 7 | array | element type | count | |
| 8 | slice | element type | | |
| 9 | structure | alignment | size | first field, field count |
| 10 | enumeration | underlying type | | first member, member count |
| 11 | function | result type (or بلا) | | first parameter, parameter count |

Flags: 1 signed, 2 `حجم`, 4 `ثابت`, 8 `متطاير`, 16 packed. Function frames are always the standard one (frame pointer `محفوظ٠`, return address at −8, the caller's frame pointer at −16), so there are no unwinding rules.

`عارض` prints one line per row after the interface: `تنقيح ملف`، `تنقيح سطر`، `تنقيح دالة`، `تنقيح نطاق`، `تنقيح متغير`، `تنقيح نوع`، `تنقيح حقل`، `تنقيح عضو`، `تنقيح معامل`.

## Executables (decision 53)

A program for our operating system is a `.كائن` too, written by `رابط --برنامج` (or `مترجم … --برنامج`):

- header `flags` bit 0 (`1`) marks an executable (`عارض` prints `كائن تنفيذي`); a linker refuses an executable as input;
- the sections have fixed addresses: `نص` at `0x10000`; `ثوابت` right after it at its alignment; `بيانات` at the next page boundary (then its alignment); `أصفار` after `بيانات` at its alignment. Symbol values stay relative to their section, as in objects;
- the entry point is the exported symbol `_بداية`;
- the only imported symbols are the system interface, names starting with `_نظام_`; every other undefined name is a link error. The relocations left in the file all refer to them: `نداء`، `نسبي_عال`، `نسبي_منخفض_ي/خ` (paired with a high part that refers to an import) and `كلمة_مزدوجة`. Any other kind against an import is a link error. The loader binds them (docs/spec-os.md).

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
