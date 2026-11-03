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
