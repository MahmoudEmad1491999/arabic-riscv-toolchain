# Working assumptions

These are decisions taken to keep moving while they are unconfirmed. Each one is implemented as written here. To change one, edit this file, the spec, and the decision log together.

| # | Assumption | Why | How to confirm or change |
|---|---|---|---|
| A1 | Brackets are mirrored by hand: in file order `)` opens and `(` closes; `]` opens and `[` closes. | The terminal does not mirror glyphs, so the opening bracket at the right end of a group must look like `)`. | Type a grouped expression in Vim Arabic mode and check it looks right. If not, swap the mapping in the lexer (one table). |
| A2 | Diagnostics are Arabic. Each message is built in file order (numbers least significant digit first), then the whole line is reversed code point by code point before printing. | A plain left-to-right terminal then shows the same picture Vim rightleft shows. | Run any failing assemble and read the error. Known limitation: no letter joining on the terminal. |
| A3 | The Vim Arabic keymap produces Arabic-Indic digits U+0660–U+0669 and the Arabic comma U+060C. | Spec choice. | In Vim, put the cursor on a typed digit / comma and press `ga`. If it shows U+06F0–U+06F9 (Persian digits), decide whether to accept them. |
| A4 | Persian digits U+06F0–U+06F9 are rejected. | Keeps one digit set; easy to relax later. | Same check as A3. |
| A5 | Arabic assembly source files use the extension `.تجميع`. | Full-word naming policy. | Rename freely; nothing depends on it except the test runner glob. |
| A6 | Identifiers use Arabic letters U+0621–U+063A and U+0641–U+064A, `_`, and Arabic-Indic digits (not first). Latin letters are not allowed in identifiers (only inside strings). Extended letters (U+0671 and later, e.g. پ چ گ) are rejected for now. | Smallest unambiguous set. | Widen the character class table if needed. |
| A7 | A number immediately followed by a letter is an error, except: hex letters after a `٠س` marker, and the local label suffixes ق / م after a decimal number. | Avoids silent mis-lexing. | — |
| A8 | Binary operators are left-associative in file order (= reading order on screen). `* /` bind tighter than `+ -`. | Matches how the line is read right to left. | — |
| A9 | Whitespace is ASCII space and tab; lines end with LF. The Arabic semicolon ؛ has no meaning yet (error). | Simplicity. | — |
| A10 | `.شفرة`, `.بيانات`, `.بيانات_للقراءة`, `.بيانات_صفرية` work both as stand-alone directives (like GAS `.text`) and as section names after `.قسم`. | Mirrors GAS. | — |
| A11 | Until pc-relative relocations exist (Phase 2), test programs take addresses with `حمل_علوي_فوري` + `عالي:` and `جمع_فوري` + `أدنى:` (absolute, non-PIE, GNU ld default base). | Phase 1 has no pseudo-instructions. | — |
| A12 | Proposed names, not yet used: pc-relative relocation operators `عالي_نسبي:` / `أدنى_نسبي:`. Pseudo-instruction names will be proposed before Phase 2. | Naming policy needs user approval. | Decide before Phase 2. |

## Added in Phase 1 (assembler v0)

| # | Assumption | Why | How to confirm or change |
|---|---|---|---|
| A13 | Branch targets (`تفرع_إذا_…`) must be a label in the **same section** plus an optional constant. Branches to undefined/external symbols are an error. | GNU as silently turns such a branch into an inverted branch plus a jump (two instructions). That is a pseudo-instruction job; it belongs to Phase 2. | Revisit when pseudo-instructions are designed. |
| A14 | A constant from `.ثابت` may be used in instruction operands **before** the line that defines it (resolved in pass 2). Places that need the value immediately (`.فراغ`, `.محاذاة`, `.ثابت`) need it defined earlier. | Natural two-pass behaviour. GNU as rejects the forward use, so the differential fuzzer skips those cases. | — |
| A15 | `.محاذاة` always aligns, also inside `.شفرة`. Code padding follows GNU: a zero byte to an even offset, a 2-byte `0x0001`, then 4-byte nops. `.شفرة` is padded at its end to its alignment, like GNU. | GNU as ignores `.align 0..2` inside code when data bytes made the offset odd; we treat that as a GNU quirk, not something to copy. | — |
| A16 | A relocation operator applies to one primary: `عالي:رمز`. For symbol + offset use brackets: `عالي:) رمز + ٤ (` (file order, `)` opens). The operator must be the whole operand. | Unambiguous and easy to read. | — |
| A17 | A symbol difference `أ - ب` is a constant when both labels are in the same section; anything else with two symbols is an error. | Enough for lengths (`.ثابت طول، نهاية - رسالة`). | — |
| A18 | Diagnostics display (refines A2): the line is reversed, then runs of Latin characters (paths) are reversed back so they stay readable. `DIAG_ORDER=file` prints file order (used by the tests). | Paths on the command line are usually Latin. | Look at a real error in your terminal and say if the layout is wrong. |
| A19 | The assembler binary is `مجمع`; usage `مجمع دخل.تجميع خرج.o`. No options yet. Exit codes: 0 ok, 1 source errors, 2 usage or I/O. | Full-word naming. | — |
| A20 | Only the four standard sections exist; `.قسم` with any other name is an error. | Enough for Phase 1. | Phase 2 if needed. |
| A21 | Objects are produced as GNU `as -mno-relax` would: no `R_RISCV_RELAX` and no linker relaxation. Relocations always name the target symbol (local labels included), with field contents exactly as GNU writes them. | Makes byte-for-byte comparison possible; relaxation can come with our own linker. | — |
| A22 | Numeric local labels are emitted as GNU-style `.L<n>^B<k>` local symbols, and only when referenced. | Identical symbol tables to GNU. | — |
| A23 | Register names are reserved: they (and `عالي`, `أدنى`) cannot be used as label or constant names. A register number written with a redundant zero (e.g. `وسيط٠٠`) is not a register. | Avoids ambiguity. | — |

## Added in Phase 2

| # | Assumption | Why | How to confirm or change |
|---|---|---|---|
| A24 | M extension names: ضرب، ضرب_عالي، ضرب_عالي_بإشارة_وبلا_إشارة، ضرب_عالي_بلا_إشارة، قسمة، قسمة_بلا_إشارة، باقي، باقي_بلا_إشارة، and the `_كلمة` forms (spec table). | Follow the naming policy; not yet approved by the owner. | Approve or rename (one table in src/as/tables.c + translit.py). |
| A25 | `حمل_عنوان` is always the non-PIC form (GNU `lla`): `جمع_علوي_للعداد` + `جمع_فوري` with R_RISCV_PCREL_HI20 / PCREL_LO12_I. The low part refers to an unnamed local label `.L0 ` at the first instruction, like GNU. | Static executables only for now. | Revisit if shared libraries / PIC are ever needed. |
| A26 | `حمل_قيمة` needs its value known at that line (number, earlier `.ثابت`, or a difference of earlier labels), because its length depends on the value. | Two-pass design without relaxation. | — |
| A27 | `استدعاء` / `استدعاء_أخير` always use the two-instruction form with R_RISCV_CALL_PLT (temporary `مؤقت١` for the tail call), like GNU without relaxation. | Byte-identical output; the linker can relax later. | — |
| A28 | The pseudo branches follow A13: the target must be a label in the same section. | Same reason as A13. | — |
