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
