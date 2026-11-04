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
