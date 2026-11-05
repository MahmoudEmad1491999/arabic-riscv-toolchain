# Arabic assembler specification (v0 draft)

This is the normative spec. Working assumptions that are not yet confirmed are listed in [assumptions.md](assumptions.md); where they matter they are also marked here.

## The editing environment (why the rules look like this)

Source is written in Vim Arabic mode (rightleft) on a terminal with NO BiDi and NO bracket mirroring. The first character typed appears at the far right. Nothing is reordered or mirrored for the programmer, so the programmer acts as the BiDi algorithm by hand: the line flows right-to-left, and number runs are left-to-right islands that are typed backwards.

"File order" = typing order = the order of code points in the file. All rules below are in file order.

## Lexical spec (assembler)

Encoding: UTF-8. The lexer decodes to code points; columns in diagnostics count code points.

### Numbers
- Digits: Arabic-Indic U+0660–U+0669 only. ASCII 0-9 are rejected (error). Persian digits U+06F0–U+06F9 also rejected unless the user decides otherwise.
- A number run is stored LEAST significant digit first. The user types 1234 as ٤ then ٣ then ٢ then ١, so the file contains ٤٣٢١ and its value is 1234. On the user's screen it reads 1234.
- A number token always starts (in file order) with an Arabic-Indic digit. This keeps it distinct from identifiers.
- Base markers come FIRST in file order (they show on screen at the right end, like an Intel `FFh` suffix):
  - `٠س` then hex digits, least significant first. Hex digits: ٠–٩ plus أ=10 ب=11 ج=12 د=13 ه=14 و=15 (abjad order; ه is U+0647, أ is U+0623).
    Example: 0x1A is file ٠ س أ ١ ; 0xFF is file ٠ س و و.
  - `٠ث` then binary digits, least significant first. 0b101 is file ٠ ث ١ ٠ ١.
  - Only ٠ followed by س or ث is a marker; otherwise ٠ is an ordinary digit.
- Digit grouping: Arabic thousands separator ٬ (U+066C) allowed between digits, ignored for the value.
- Lexer method: collect the whole number run, then interpret it least-significant-first (or reverse it and parse normally). One shared helper formats numbers back into file order; every tool that emits source (compiler codegen, listings, disassembly) MUST use it.
- Minus: `-` is an operator in the right-to-left flow, written BEFORE the number in file order (typed `-` then digits). Binary minus works the same way: `مؤشر_المكدس - ٨`.

### Brackets (grouping)
Brackets are allowed and used for grouping in expressions. Because the terminal does not mirror, the programmer types the glyph that looks right on screen: in file order `)` OPENS and `(` CLOSES; likewise `]` opens and `[` closes. The lexer maps them accordingly. (Taken as the working assumption; the user was asked to confirm it in Vim.) The same mirrored rule will apply to `<` `>` in the language later.

### Identifiers
- Arabic letters, `_`, and Arabic-Indic digits (not as first character).
- Reject in identifiers: tatweel ـ U+0640, tashkeel (diacritics, U+064B–U+065F, U+0670), Arabic presentation forms (U+FB50–U+FDFF, U+FE70–U+FEFF).
- No normalisation: أ إ آ ا, ى/ي, ة/ه are distinct letters.
- Digits inside user identifiers are plain characters (identity = byte equality, no reversal). Only where a digit run has a numeric meaning (register numbers, local labels) is it read least-significant-first. Example: to see سجل٣١ on screen the user types سجل then ١ then ٣; the file holds سجل١٣ and it means register 31.

### Strings and characters
- Strings: `"..."`, stored exactly as typed (including any Latin text; no reordering).
- Escapes: `\س` newline (سطر), `\ر` carriage return (رجوع), `\ج` tab (جدولة), `\٠` NUL, `\\`, `\"`.
- Character literal `'أ'` has the value of the Unicode code point. Using a non-ASCII character literal where a single byte is required (e.g. `.بايت`) is an error.

### Other punctuation
- Operand separator: Arabic comma ، (U+060C).
- Comment: `#` to end of line.
- Label definition: `اسم:`.
- Local labels: a number then `:` defines; a reference is the number followed by ق (قادم, forward) or م (ماضي, backward). Number digits least significant first.
- Policy: only symmetric characters are used where direction matters (+ - * / = : | # @ _ . " '), except brackets as defined above.

## Operand syntax
- Operand order is the same as GAS (rd, rs1, rs2/imm), read right-to-left.
- Memory operand: an address expression `سجل + إزاحة`, `سجل - إزاحة`, or just `سجل`. No brackets. GAS `lw a0, 8(sp)` is `حمل_كلمة وسيط٠، مؤشر_المكدس + ٨`. Stores: `خزن_كلمة مصدر، أساس + إزاحة`. jalr: `قفز_وربط_بسجل وجهة، أساس + إزاحة`.
- Relocation operators: `عالي:رمز` (%hi), `أدنى:رمز` (%lo), `عالي_نسبي:رمز` (%pcrel_hi, on `جمع_علوي_للعداد`) and `أدنى_نسبي:تسمية` (%pcrel_lo; the operand is the label of the matching `جمع_علوي_للعداد`, used in 12-bit immediates, loads, stores and `قفز_وربط_بسجل`). The operator takes one primary; for symbol + offset use brackets, e.g. `عالي:) رمز + ٤ (` in file order (A16). Allowed in: `حمل_علوي_فوري` (عالي), 12-bit immediates of I-type, loads, stores and `قفز_وربط_بسجل` (أدنى). PC-relative forms for Phase 2 (proposed: `عالي_نسبي:رمز`, `أدنى_نسبي:رمز`, to be confirmed).
- Assembler expressions in v1: + - * / and brackets, left-associative in file order, `* /` before `+ -` (A8). Bitwise/shift operators in expressions: open item.
- Branch targets must be labels in the same section (A13); `قفز_وربط` may target any symbol.

## Naming policy
Full Arabic words. Abbreviation is an exception that needs the user's approval.
Mnemonic pattern: verb, then width, then modifiers in this order: `_فوري` (immediate), `_بلا_إشارة` (unsigned), `_كلمة` (32-bit W operation). Widths: بايت (8), نصف_كلمة (16), كلمة (32), كلمة_مزدوجة (64). Shifts are named by meaning, not screen direction: رفع = toward the most significant bit, خفض = toward the least significant.

## Registers
| Arabic | GAS |
|---|---|
| سجل + 0..31 | x0..x31 |
| صفر | zero |
| عنوان_العودة | ra |
| مؤشر_المكدس | sp |
| المؤشر_العام | gp |
| مؤشر_الخيط | tp |
| مؤقت + 0..6 | t0..t6 |
| محفوظ + 0..11 | s0..s11 (s0 is also fp) |
| وسيط + 0..7 | a0..a7 |

## RV64I mnemonics
| Arabic | GAS |
|---|---|
| جمع | add |
| جمع_فوري | addi |
| طرح | sub |
| جمع_كلمة | addw |
| جمع_فوري_كلمة | addiw |
| طرح_كلمة | subw |
| و_منطقي | and |
| و_منطقي_فوري | andi |
| أو_منطقي | or |
| أو_منطقي_فوري | ori |
| أو_حصري | xor |
| أو_حصري_فوري | xori |
| رفع | sll |
| رفع_فوري | slli |
| خفض_منطقي | srl |
| خفض_منطقي_فوري | srli |
| خفض_حسابي | sra |
| خفض_حسابي_فوري | srai |
| رفع_كلمة | sllw |
| رفع_فوري_كلمة | slliw |
| خفض_منطقي_كلمة | srlw |
| خفض_منطقي_فوري_كلمة | srliw |
| خفض_حسابي_كلمة | sraw |
| خفض_حسابي_فوري_كلمة | sraiw |
| ضبط_إذا_أصغر | slt |
| ضبط_إذا_أصغر_فوري | slti |
| ضبط_إذا_أصغر_بلا_إشارة | sltu |
| ضبط_إذا_أصغر_فوري_بلا_إشارة | sltiu |
| حمل_بايت | lb |
| حمل_نصف_كلمة | lh |
| حمل_كلمة | lw |
| حمل_كلمة_مزدوجة | ld |
| حمل_بايت_بلا_إشارة | lbu |
| حمل_نصف_كلمة_بلا_إشارة | lhu |
| حمل_كلمة_بلا_إشارة | lwu |
| خزن_بايت | sb |
| خزن_نصف_كلمة | sh |
| خزن_كلمة | sw |
| خزن_كلمة_مزدوجة | sd |
| تفرع_إذا_يساوي | beq |
| تفرع_إذا_لا_يساوي | bne |
| تفرع_إذا_أصغر | blt |
| تفرع_إذا_أكبر_أو_يساوي | bge |
| تفرع_إذا_أصغر_بلا_إشارة | bltu |
| تفرع_إذا_أكبر_أو_يساوي_بلا_إشارة | bgeu |
| قفز_وربط | jal |
| قفز_وربط_بسجل | jalr |
| حمل_علوي_فوري | lui |
| جمع_علوي_للعداد | auipc |
| سياج | fence |
| نداء_البيئة | ecall |
| نقطة_توقف | ebreak |

In loads/stores, كلمة is the width (lw/sw); elsewhere a trailing _كلمة is the W modifier. Mnemonics are matched by exact table lookup, so there is no ambiguity.

Pseudo-instructions (Phase 2) need names following the same policy; propose them to the user before implementing.

## Directives
| Arabic | GAS |
|---|---|
| .قسم | .section |
| .شفرة | .text |
| .بيانات | .data |
| .بيانات_للقراءة | .rodata |
| .بيانات_صفرية | .bss |
| .عام | .globl |
| .محاذاة | .align |
| .بايت | .byte |
| .نصف_كلمة | .half |
| .كلمة | .word |
| .كلمة_مزدوجة | .dword |
| .سلسلة | .string |
| .فراغ | .zero |
| .ثابت | .equ |

Arabic section names map to the standard ELF names (.text, .data, .rodata, .bss) in the output so GNU tools and loaders work. Arabic symbol names are written to ELF as UTF-8 bytes unchanged.

## Diagnostics
In Arabic. Plan: build each message in file-order convention (same as source, numbers least-significant-first via the shared helper), then reverse the whole line code-point-wise when printing to the plain left-to-right terminal, which reproduces what Vim rightleft shows. Known limitation: the terminal does no Arabic shaping, so letters will not be joined. Keep all message text in one table so wording can change in one place.


## M extension (approved, decision 14)
| Arabic | GAS |
|---|---|
| ضرب | mul |
| ضرب_عالي | mulh |
| ضرب_عالي_بإشارة_وبلا_إشارة | mulhsu |
| ضرب_عالي_بلا_إشارة | mulhu |
| قسمة | div |
| قسمة_بلا_إشارة | divu |
| باقي | rem |
| باقي_بلا_إشارة | remu |
| ضرب_كلمة | mulw |
| قسمة_كلمة | divw |
| قسمة_بلا_إشارة_كلمة | divuw |
| باقي_كلمة | remw |
| باقي_بلا_إشارة_كلمة | remuw |

## Control and status registers, privileged instructions (proposed, A64)

For the operating system (decision 39). Zicsr and the privileged instructions; the names are proposals awaiting the owner's review.

| Arabic | GNU | Operands |
|---|---|---|
| قراءة_وكتابة_تحكم | csrrw | rd، csr، rs1 |
| قراءة_وضبط_تحكم | csrrs | rd، csr، rs1 |
| قراءة_ومسح_تحكم | csrrc | rd، csr، rs1 |
| قراءة_وكتابة_تحكم_فوري | csrrwi | rd، csr، uimm5 |
| قراءة_وضبط_تحكم_فوري | csrrsi | rd، csr، uimm5 |
| قراءة_ومسح_تحكم_فوري | csrrci | rd، csr، uimm5 |
| قراءة_تحكم | csrr (pseudo) | rd، csr |
| كتابة_تحكم / ضبط_تحكم / مسح_تحكم | csrw / csrs / csrc (pseudo) | csr، rs1 |
| كتابة_تحكم_فوري / ضبط_تحكم_فوري / مسح_تحكم_فوري | csrwi / csrsi / csrci (pseudo) | csr، uimm5 |
| عودة_من_الآلة | mret | — |
| عودة_من_المشرف | sret | — |
| انتظار_مقاطعة | wfi | — |
| سياج_الترجمة | sfence.vma | nothing, or rs1، rs2 |

A `csr` operand is one of the names below, or a constant from ٠ to ٥٩٠٤ (4095). `uimm5` is a constant from ٠ to ١٣ (31).

| Name | GNU | Number |
|---|---|---|
| حالة_المشرف | sstatus | 0x100 |
| تمكين_مقاطعات_المشرف | sie | 0x104 |
| متجه_فخ_المشرف | stvec | 0x105 |
| تمكين_عدادات_المشرف | scounteren | 0x106 |
| مسودة_المشرف | sscratch | 0x140 |
| عنوان_استثناء_المشرف | sepc | 0x141 |
| سبب_فخ_المشرف | scause | 0x142 |
| قيمة_فخ_المشرف | stval | 0x143 |
| مقاطعات_المشرف_المعلقة | sip | 0x144 |
| جدول_الصفحات | satp | 0x180 |
| حالة_الآلة | mstatus | 0x300 |
| امتدادات_الآلة | misa | 0x301 |
| تفويض_استثناءات_الآلة | medeleg | 0x302 |
| تفويض_مقاطعات_الآلة | mideleg | 0x303 |
| تمكين_مقاطعات_الآلة | mie | 0x304 |
| متجه_فخ_الآلة | mtvec | 0x305 |
| تمكين_عدادات_الآلة | mcounteren | 0x306 |
| بيئة_الآلة | menvcfg | 0x30A |
| مسودة_الآلة | mscratch | 0x340 |
| عنوان_استثناء_الآلة | mepc | 0x341 |
| سبب_فخ_الآلة | mcause | 0x342 |
| قيمة_فخ_الآلة | mtval | 0x343 |
| مقاطعات_الآلة_المعلقة | mip | 0x344 |
| إعداد_الحماية٠ | pmpcfg0 | 0x3A0 |
| عنوان_الحماية٠ | pmpaddr0 | 0x3B0 |
| الدورات | cycle | 0xC00 |
| الوقت | time | 0xC01 |
| رقم_الخيط | mhartid | 0xF14 |

## Pseudo-instructions (approved)
Each expands exactly as GNU as does with `-mno-relax`.

| Arabic | GAS | Expansion |
|---|---|---|
| `حمل_قيمة سجل، ثابت` | li | `جمع_فوري` alone if the value fits 12 bits, otherwise GNU's load_const sequence (`حمل_علوي_فوري`/`جمع_فوري_كلمة`, then `رفع_فوري` + `جمع_فوري` steps for 64-bit values) |
| `حمل_عنوان سجل، رمز` | la (non-PIC) | `جمع_علوي_للعداد` + `جمع_فوري` with pc-relative relocations |
| `نسخ أ، ب` | mv | `جمع_فوري أ، ب، ٠` |
| `نفي أ، ب` | not | `أو_حصري_فوري أ، ب، -١` |
| `سالب أ، ب` | neg | `طرح أ، صفر، ب` |
| `لا_عملية` | nop | `جمع_فوري صفر، صفر، ٠` |
| `قفز هدف` | j | `قفز_وربط صفر، هدف` |
| `قفز_بسجل سجل` | jr | `قفز_وربط_بسجل صفر، سجل` |
| `استدعاء رمز` | call | `جمع_علوي_للعداد عنوان_العودة` + `قفز_وربط_بسجل عنوان_العودة، عنوان_العودة` (R_RISCV_CALL_PLT) |
| `استدعاء_أخير رمز` | tail | same through `مؤقت١`, linking to `صفر` |
| `عودة` | ret | `قفز_وربط_بسجل صفر، عنوان_العودة` |
| `تفرع_إذا_صفر` / `تفرع_إذا_ليس_صفرا` | beqz / bnez | compare with `صفر` |
| `تفرع_إذا_أكبر` / `تفرع_إذا_أصغر_أو_يساوي` (+`_بلا_إشارة`) | bgt / ble (+u) | `تفرع_إذا_أصغر` / `تفرع_إذا_أكبر_أو_يساوي` with the registers swapped |
