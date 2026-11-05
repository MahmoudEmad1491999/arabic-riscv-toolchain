# Decision log

Decisions made by the project owner. Newest last.

1. Target RV64I on Linux user mode (qemu-riscv64) first. GNU binutils are the reference for testing.
2. Numbers are stored least significant digit first in file order (typed 4, 3, 2, 1 for 1234). ASCII digits are rejected.
3. Brackets are allowed and used for grouping (direction: see assumption A1).
4. Names are full Arabic words. Abbreviations are exceptions that need approval. The register, mnemonic and directive tables in the spec are approved.
5. Diagnostics are in Arabic (display method: assumption A2).
6. Latin text inside strings is stored exactly as typed.
7. The toolchain is for personal use. Portability and converter tools come later.
8. Code lives in a private GitHub repository.
9. Pseudo-instruction names approved: حمل_قيمة، حمل_عنوان، نسخ، نفي، سالب، لا_عملية، قفز، قفز_بسجل، استدعاء، استدعاء_أخير، عودة، تفرع_إذا_صفر، تفرع_إذا_ليس_صفرا، تفرع_إذا_أكبر، تفرع_إذا_أصغر_أو_يساوي (+_بلا_إشارة); pc-relative operators عالي_نسبي: / أدنى_نسبي:.
10. One codebase, two object formats: ELF (kept permanently, it is the bridge to GNU's tools for testing) and our own format. No separate long-lived branches.
11. Our object format is designed in Phase 4, together with the language's linkage model, so each symbol kind maps one-to-one to a language keyword.
12. Our format comes in two forms: binary (what the tools and the loader read) and text (for people to read; never executed).
13. We will have our own executable format and our own loader (Phase 5).
14. M extension names approved (ضرب، ضرب_عالي، ضرب_عالي_بإشارة_وبلا_إشارة، ضرب_عالي_بلا_إشارة، قسمة، قسمة_بلا_إشارة، باقي، باقي_بلا_إشارة and the _كلمة forms). Assumption A24 is now a decision.

### Language (Phase 4)

15. Integer arithmetic wraps (two's complement) for every type; checked and saturating operations are explicit. Shift amounts and division by zero are defined as RISC-V defines them. No warnings: compile errors only.
16. *(Superseded by 28.)* Slice indexing is bounds-checked by default (the program stops on a violation); an explicitly named unchecked index exists for hot loops; checks on constant indexes into fixed arrays are removed at compile time.
17. Memory is managed manually, like C. Use-after-free and dangling pointers are the documented undefined behaviour.
18. Brackets are not "mirrored": each glyph simply has its own meaning. In file order `)` `}` `]` open and `(` `{` `[` close.
19. Every statement ends with the Arabic semicolon `؛`.
20. Type names: `صحيح` + width (signed), `طبيعي` + width (unsigned), `منطقي`, `حرف` (one Unicode code point), `حجم` (sizes and indexes; its own type, 64-bit on RV64, 32-bit on RV32). There is no `بايت`: bytes are `طبيعي٨`.
21. Comparison glyphs carry their on-screen meaning, like brackets: in file order `أ > ب` means "أ is less than ب" and `أ < ب` means "أ is greater than ب" (`>=` / `<=` likewise).
22. Conditionals: `إذا`، `وإلا إذا`، `وإلا` (`وإلا إذا` is `وإلا` followed by an `إذا` statement; no extra keyword).
23. Logical operators are words: `و`، `أو` (short-circuit), `ليس`. Their operands must be `منطقي`.
24. Size and alignment of a type: the built-ins `مقاس)نوع(` and `محاذاة)نوع(`, both of type `حجم`. `.طول` exists only on sequences (arrays, slices, strings: bytes) and is of type `حجم`.
25. Code-point escape in character and string literals: `\}…{` (braces in file order, the number follows the number rules), e.g. `'\}٠سج٠٠٢{'` is U+200C.
26. Every declaration states its type, with no exception: variables, constants (`ثابت`) and parameters, even when the initial value is a literal or already typed. There is no type inference and there are no typeless named constants.
27. Bitwise operators are words with the approved short suffix `_ب`: `و_ب`، `أو_ب`، `أو_حصري_ب`، `نفي_ب` (an approved exception to the full-word rule). `^` stays free for pointer dereference.
28. Plain indexing `ش]ف[` and slicing are **not** checked at run time; out of range is undefined behaviour. Checked access is explicit: `عنصر_بفحص)ش، ف(` and `شريحة_بفحص)ش، أ، ب(`. Constant indexes into arrays are still checked at compile time. Supersedes 16.
29. The language is called **غرة** (Ghora). Source files use `.غرة`. The standard library is also called `غرة` (`استورد غرة.نصوص؛`).
30. Approved from the draft spec: no hidden padding in structures (`بنية مضغوطة` for packed ones); enumerations with an explicit underlying type; pointer syntax (`^`, `@`, `اختياري`, `لاشيء`, `إذا وجد`); array/slice types; the conversions `حول`، `حول_بفحص`، `اقتطع`، `اعتبر`; the loop forms (`كلما … طالما` runs the body first); functions with the exact RISC-V LP64 ABI and `رئيسية` as the entry; modules with interfaces in objects, interface hashes, libraries, `خارجي`; no compound assignment.
