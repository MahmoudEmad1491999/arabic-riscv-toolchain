# Proposal: debug information and floating point (Phase 7, first step)

Status: **accepted (decisions 64–69)**, with the type names `حقيقي٢٣` / `حقيقي٤٦` instead of `عشري…`. Each part moves into `spec-object.md`, `spec-language.src.md`, `spec-assembler.md`, `decisions.md` and `assumptions.md` as it is implemented.

The two features are planned together because they touch the same places: the assembler, the linker, the compiler, the runtime, the OS (it must save floating-point registers) and our object format.

---

## Part A: our debug format (`تنقيح`)

### Goals

- **One meaning per field.** No attribute lists, no "forms", no abbreviation tables, no bytecode, no state machine (DWARF's line program and CFI are both small virtual machines; we don't copy that).
- **Tables of fixed-size rows**, like our symbols and relocations, referring to each other by index. A reader needs no parser beyond "read N rows of M bytes".
- **Describes غرة as it is**, not C: slices, optional pointers, enumerations with an underlying type, `منطقي`, `حرف`, modules.
- **Only what our tools produce.** One frame layout (frame pointer `محفوظ٠`, return address at `-8`, caller's frame pointer at `-16`), so unwinding needs no per-function rules: the table just says "this function uses the standard frame".
- **Optional.** An object without it is still a complete object.

### Where it lives

A new region **after the Interface region** in `.كائن` files, with its size in the header (the header grows from 80 to 88 bytes, format version 2). Executables (`--برنامج`) keep it, merged by the linker. Hosted ELF executables carry the same bytes in an ELF section named `.تنقيح`, so Linux runs get the same information (no DWARF).

### Tables

Every table is an array of fixed-size rows; every reference is an index into another table; every code position is an offset into this object's `نص` section (the linker turns them into addresses when it lays out the program).

| Table | One row says | Fields |
|---|---|---|
| **Files** | a source file used by this object | name (UTF-8 path), FNV-1a hash of its bytes |
| **Lines** | "code from this offset up to the next row's offset comes from this source position" | code offset, file index, line, column |
| **Functions** | a function and its code | name, symbol index, start and end offset, file and line of its declaration, frame size, result type |
| **Scopes** | a block `} … {` inside a function | function index, start and end offset, parent scope |
| **Variables** | a parameter, a local, or a global | name, type index, kind (parameter / local / global), where: frame offset from `محفوظ٠` or symbol index, scope index |
| **Types** | one type | kind, plus the fields of that kind (below) |
| **Fields** | one field of a structure | name, type index, offset |
| **Members** | one member of an enumeration | name, value |

Type kinds, each with its own fixed meaning:

| Kind | Extra fields |
|---|---|
| integer | width in bits, signed or not, and whether it is `حجم` |
| boolean, character | none |
| floating | width in bits (32 or 64) |
| pointer | target type, `ثابت`, `متطاير` |
| optional pointer | the pointer type |
| array | element type, count |
| slice | element type, `ثابت` |
| structure | name, size, alignment, packed, first field, field count |
| enumeration | name, underlying type, first member, member count |
| function | result type (or none), first parameter type, parameter count |

### What uses it

1. **Run-time errors say where they happened.** Today `عنصر_بفحص` out of range, a failed `حول_بفحص` or an overflow in `جمع_بفحص` just stop the program. With the line table, the stop routine prints the file, line and column, and the call chain (by walking frame pointers).
2. **`عارض`** shows every table in the text form.
3. **Later:** a debugger (`مصحح`?) on Linux and on our OS. Not part of this step.

### Who makes it

- The **compiler** knows the source positions, types, variables and scopes. It writes them into the assembly with a few new directives:
  - `.موضع ملف، سطر، عمود` (the following code comes from there; like `.loc`, but nothing else);
  - and directives for functions, scopes, variables and types, referring to labels for code positions.
- The **assembler** turns those into the tables (code positions are its labels, so it knows the offsets).
- The **linker** concatenates the tables of all objects, renumbers the indexes and turns offsets into addresses.

---

## Part B: floating point

### Mistakes in C we avoid

| C's mistake | What غرة does |
|---|---|
| Implicit conversions between integers and floats, and between `float` and `double` (`int x = 3.7;`, `f * 2` promotes) | **No implicit conversion at all**, the same rule as integers: both operands have the same type. |
| `float → int` out of range is undefined behaviour | Every float→integer conversion says what happens: `حول_بفحص` stops the program on NaN or out of range; other forms are explicit about rounding. |
| Rounding mode is global hidden state (`fesetround`) that changes what every operation means | **One rounding mode, always:** round to nearest, ties to even, encoded in every instruction. No global state. |
| The compiler may compute with more precision than the type (x87, `FLT_EVAL_METHOD`) | **Every operation is computed in exactly its type.** |
| The compiler may fuse `a*b+c` into one FMA (different result) | **Never implicit.** A built-in `ضرب_جمع)أ، ب، ج(` does a fused multiply-add when you ask for it. |
| `0.1` and constant folding may round differently from run time | Literals are **correctly rounded** once; constant folding uses **exactly** the run-time operations, so a constant and the same expression at run time always agree. |
| `x == x` is false for NaN, silently; `<` with NaN is silently false | A comparison with a NaN operand stops the program (decision 67); `ليس_عددا)س(` tests for NaN first. |
| `errno` and math functions with hidden state | Library functions return their result; nothing hidden. |
| Signalling NaN, NaN payloads, the exception flags | Not part of the language. All NaNs are the same NaN; the flags are never read. |

Division by zero gives ±∞ and `0/0` gives NaN, as IEEE 754 says: defined behaviour, not a stop. Overflow gives ±∞.

### Types

Two types, the IEEE 754 binary32 and binary64 formats (RISC-V F and D extensions). Names: **`حقيقي٢٣`** and **`حقيقي٤٦`** (decision 66; in file order, like `صحيح٢٣`).

### Literals

A decimal literal with the Arabic decimal separator `٫` (U+066B), in file order like every number: the whole literal reversed character by character, so it reads correctly on screen. Optional exponent with a marker letter (proposed: `أس`). A literal is untyped until it meets a float type (as integer literals are), then correctly rounded to it.

### Conversions

| Built-in | From → to | Meaning |
|---|---|---|
| `حول` | integer → float, float32 → float64 | only when every value converts exactly (e.g. `صحيح٢٣ → عشري٤٦`); `صحيح٤٦ → عشري٤٦` is refused because large values round |
| `قرب` | integer → float, float64 → float32 | rounds to nearest even when it must (the explicit, inexact conversion) |
| `حول_بفحص` | float → integer | the value must be a whole number in range, else the program stops |
| `اقتطع` / `قرب_لأقرب` / `أرضية` / `سقف` | float → integer | rounding toward zero / to nearest even / down / up; stops if the result does not fit or the value is NaN |
| `اعتبر` | float ↔ integer of the same width | the bits, unchanged |

### The ABI

RISC-V **LP64D** (hard float, the GCC default on Linux): float arguments and results in `fa0…fa7`, `fs0…fs11` preserved, the rest as the integer ABI. A structure of one or two floats travels in float registers, as the ABI says. Every integer-only function keeps exactly the convention it has today.

### Assembler names (proposal)

Registers: `وسيط_عشري٠…٧` (fa0–fa7), `محفوظ_عشري٠…١١` (fs0–fs11), `مؤقت_عشري٠…١١` (ft0–ft11), and `عشري٠…٣١` by number.

Instructions follow one rule: the integer instruction's name, plus `_عشري` (single) or `_مزدوج` (double): `جمع_عشري` / `جمع_مزدوج` (fadd.s / fadd.d), `حمل_عشري` / `حمل_مزدوج` (flw / fld), `خزن_عشري` / `خزن_مزدوج`, `جذر_عشري` (fsqrt.s), `ضرب_جمع_مزدوج` (fmadd.d), `أصغر_مزدوج` (fmin.d), `يساوي_مزدوج` (feq.d), `تحويل_…` for the conversions. The full table goes into `spec-assembler.md` once the rule is accepted.

### The OS

The kernel turns the floating-point unit on (`mstatus.FS`/`sstatus.FS`), and every process gets its own 32 float registers and `fcsr`, saved and restored on every switch. To begin with they are saved always (simple and correct); saving only when used (the FS "dirty" state) can come later.

### Printing

The standard library gets one function that prints a float as the shortest text that reads back to exactly the same value, and one that reads it back. It is correct for every value.

---

## Order of work after the decisions

1. Specs, decisions and assumptions updated as it is implemented.
2. Assembler: F and D instructions in the C assembler, checked byte for byte against GNU as like every instruction; the ported assembler kept byte-identical.
3. Compiler: the float types, literals, operations, comparisons, conversions and the LP64D ABI; tests checked against GCC-compiled C for the arithmetic.
4. OS: FPU on and float registers saved; a test that two processes using floats don't disturb each other.
5. Debug format: object format v2, the directives, linker merging, `عارض`, then file:line and the call chain in run-time errors, on Linux and on our OS.
6. Every C change ported to the غرة tools, and stage 2 = stage 3 checked again.
