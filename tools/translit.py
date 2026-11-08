#!/usr/bin/env python3
"""Transliterate Arabic assembly (.تجميع) into GNU as syntax.

This is TEST INFRASTRUCTURE ONLY. It is the independent path used as an
oracle: Arabic source -> this script -> GNU as. Our own assembler's output is
compared against it. Keep it simple and obviously correct; it does not need
to produce good error messages.

Conventions (see docs/spec-assembler.md):
  * numbers are stored least significant digit first; ٠س = hex, ٠ث = binary
  * ، separates operands; # starts a comment
  * ) opens and ( closes a group (assumption A1); same for ] [
  * memory operands are `reg + expr` / `reg - expr` / `reg`
  * عالي:sym -> %hi(sym), أدنى:sym -> %lo(sym)
"""
import sys

DIGITS = {chr(0x0660 + i): i for i in range(10)}
HEX_LETTERS = {"أ": 10, "ب": 11, "ج": 12, "د": 13, "ه": 14, "و": 15}
THOUSANDS = "٬"
COMMA = "،"

REGISTERS_FIXED = {
    "صفر": "zero",
    "عنوان_العودة": "ra",
    "مؤشر_المكدس": "sp",
    "المؤشر_العام": "gp",
    "مؤشر_الخيط": "tp",
    # rounding modes of the F and D instructions (A83): GAS writes them as operands
    "أقرب_زوجي": "rne", "نحو_الصفر": "rtz", "للأسفل": "rdn", "للأعلى": "rup",
    "أقرب_أبعد": "rmm", "حسب_النمط": "dyn",
}
REGISTER_FAMILIES = {  # prefix -> (gas prefix, max index)
    "سجل": ("x", 31),
    "مؤقت": ("t", 6),
    "محفوظ": ("s", 11),
    "وسيط": ("a", 7),
    # floating-point registers (decision 69)
    "عشري": ("f", 31),
    "مؤقت_عشري": ("ft", 11),
    "محفوظ_عشري": ("fs", 11),
    "وسيط_عشري": ("fa", 7),
}

MNEMONICS = {
    "جمع": "add", "جمع_فوري": "addi", "طرح": "sub",
    "جمع_كلمة": "addw", "جمع_فوري_كلمة": "addiw", "طرح_كلمة": "subw",
    "و_منطقي": "and", "و_منطقي_فوري": "andi",
    "أو_منطقي": "or", "أو_منطقي_فوري": "ori",
    "أو_حصري": "xor", "أو_حصري_فوري": "xori",
    "رفع": "sll", "رفع_فوري": "slli",
    "خفض_منطقي": "srl", "خفض_منطقي_فوري": "srli",
    "خفض_حسابي": "sra", "خفض_حسابي_فوري": "srai",
    "رفع_كلمة": "sllw", "رفع_فوري_كلمة": "slliw",
    "خفض_منطقي_كلمة": "srlw", "خفض_منطقي_فوري_كلمة": "srliw",
    "خفض_حسابي_كلمة": "sraw", "خفض_حسابي_فوري_كلمة": "sraiw",
    "ضبط_إذا_أصغر": "slt", "ضبط_إذا_أصغر_فوري": "slti",
    "ضبط_إذا_أصغر_بلا_إشارة": "sltu", "ضبط_إذا_أصغر_فوري_بلا_إشارة": "sltiu",
    "حمل_بايت": "lb", "حمل_نصف_كلمة": "lh", "حمل_كلمة": "lw",
    "حمل_كلمة_مزدوجة": "ld", "حمل_بايت_بلا_إشارة": "lbu",
    "حمل_نصف_كلمة_بلا_إشارة": "lhu", "حمل_كلمة_بلا_إشارة": "lwu",
    "خزن_بايت": "sb", "خزن_نصف_كلمة": "sh", "خزن_كلمة": "sw",
    "خزن_كلمة_مزدوجة": "sd",
    "تفرع_إذا_يساوي": "beq", "تفرع_إذا_لا_يساوي": "bne",
    "تفرع_إذا_أصغر": "blt", "تفرع_إذا_أكبر_أو_يساوي": "bge",
    "تفرع_إذا_أصغر_بلا_إشارة": "bltu",
    "تفرع_إذا_أكبر_أو_يساوي_بلا_إشارة": "bgeu",
    "قفز_وربط": "jal", "قفز_وربط_بسجل": "jalr",
    "حمل_علوي_فوري": "lui", "جمع_علوي_للعداد": "auipc",
    "سياج": "fence", "نداء_البيئة": "ecall", "نقطة_توقف": "ebreak",
    # M extension
    "ضرب": "mul", "ضرب_عالي": "mulh", "ضرب_عالي_بإشارة_وبلا_إشارة": "mulhsu",
    "ضرب_عالي_بلا_إشارة": "mulhu", "قسمة": "div", "قسمة_بلا_إشارة": "divu",
    "باقي": "rem", "باقي_بلا_إشارة": "remu", "ضرب_كلمة": "mulw",
    "قسمة_كلمة": "divw", "قسمة_بلا_إشارة_كلمة": "divuw", "باقي_كلمة": "remw",
    "باقي_بلا_إشارة_كلمة": "remuw",
    # Zicsr and privileged (A64)
    "قراءة_وكتابة_تحكم": "csrrw", "قراءة_وضبط_تحكم": "csrrs", "قراءة_ومسح_تحكم": "csrrc",
    "قراءة_وكتابة_تحكم_فوري": "csrrwi", "قراءة_وضبط_تحكم_فوري": "csrrsi",
    "قراءة_ومسح_تحكم_فوري": "csrrci", "قراءة_تحكم": "csrr", "كتابة_تحكم": "csrw",
    "ضبط_تحكم": "csrs", "مسح_تحكم": "csrc", "كتابة_تحكم_فوري": "csrwi",
    "ضبط_تحكم_فوري": "csrsi", "مسح_تحكم_فوري": "csrci",
    "عودة_من_الآلة": "mret", "عودة_من_المشرف": "sret", "انتظار_مقاطعة": "wfi",
    "سياج_الترجمة": "sfence.vma",
    # pseudo-instructions
    "حمل_قيمة": "li", "حمل_عنوان": "la", "نسخ": "mv", "نفي": "not",
    "سالب": "neg", "لا_عملية": "nop", "قفز": "j", "قفز_بسجل": "jr",
    "استدعاء": "call", "استدعاء_أخير": "tail", "عودة": "ret",
    "تفرع_إذا_صفر": "beqz", "تفرع_إذا_ليس_صفرا": "bnez",
    "تفرع_إذا_أكبر": "bgt", "تفرع_إذا_أصغر_أو_يساوي": "ble",
    "تفرع_إذا_أكبر_بلا_إشارة": "bgtu",
    "تفرع_إذا_أصغر_أو_يساوي_بلا_إشارة": "bleu",
}
# F and D extensions (decision 69): the integer name + _عشري (single) / _مزدوج (double)
def _float_mnemonics():
    m = {}
    for suf, s in (("عشري", "s"), ("مزدوج", "d")):
        for ar, gas in (("جمع", "fadd"), ("طرح", "fsub"), ("ضرب", "fmul"), ("قسمة", "fdiv"),
                        ("جذر", "fsqrt"), ("نسخ_الإشارة", "fsgnj"), ("نسخ_عكس_الإشارة", "fsgnjn"),
                        ("ضرب_الإشارة", "fsgnjx"), ("أصغر", "fmin"), ("أكبر", "fmax"),
                        ("ضبط_إذا_يساوي", "feq"), ("ضبط_إذا_أصغر", "flt"),
                        ("ضبط_إذا_أصغر_أو_يساوي", "fle"), ("صنف", "fclass"),
                        ("نسخ", "fmv"), ("سالب", "fneg"), ("مطلق", "fabs"),
                        ("ضرب_جمع", "fmadd"), ("ضرب_طرح", "fmsub"),
                        ("سالب_ضرب_طرح", "fnmsub"), ("سالب_ضرب_جمع", "fnmadd")):
            m[f"{ar}_{suf}"] = f"{gas}.{s}"
        w = "w" if s == "s" else "d"
        m[f"حمل_{suf}"] = f"fl{w}"
        m[f"خزن_{suf}"] = f"fs{w}"
        m[f"نسخ_بتات_من_{suf}"] = f"fmv.x.{w}"
        m[f"نسخ_بتات_إلى_{suf}"] = f"fmv.{w}.x"
        for ar, g in (("كلمة", "w"), ("كلمة_بلا_إشارة", "wu"), ("كلمة_مزدوجة", "l"),
                      ("كلمة_مزدوجة_بلا_إشارة", "lu")):
            m[f"تحويل_{ar}_من_{suf}"] = f"fcvt.{g}.{s}"
            m[f"تحويل_{suf}_من_{ar}"] = f"fcvt.{s}.{g}"
    m["تحويل_عشري_من_مزدوج"] = "fcvt.s.d"
    m["تحويل_مزدوج_من_عشري"] = "fcvt.d.s"
    return m


MNEMONICS.update(_float_mnemonics())

# control and status registers (A64): Arabic name -> GAS name
CSRS = {
    "حالة_المشرف": "sstatus", "تمكين_مقاطعات_المشرف": "sie", "متجه_فخ_المشرف": "stvec",
    "تمكين_عدادات_المشرف": "scounteren", "مسودة_المشرف": "sscratch",
    "عنوان_استثناء_المشرف": "sepc", "سبب_فخ_المشرف": "scause", "قيمة_فخ_المشرف": "stval",
    "مقاطعات_المشرف_المعلقة": "sip", "جدول_الصفحات": "satp",
    "حالة_الآلة": "mstatus", "امتدادات_الآلة": "misa", "تفويض_استثناءات_الآلة": "medeleg",
    "تفويض_مقاطعات_الآلة": "mideleg", "تمكين_مقاطعات_الآلة": "mie", "متجه_فخ_الآلة": "mtvec",
    "تمكين_عدادات_الآلة": "mcounteren", "بيئة_الآلة": "menvcfg", "مسودة_الآلة": "mscratch",
    "عنوان_استثناء_الآلة": "mepc", "سبب_فخ_الآلة": "mcause", "قيمة_فخ_الآلة": "mtval",
    "مقاطعات_الآلة_المعلقة": "mip", "إعداد_الحماية٠": "pmpcfg0", "عنوان_الحماية٠": "pmpaddr0",
    "أعلام_العشري": "fflags", "نمط_التقريب": "frm", "تحكم_العشري": "fcsr",
    "الدورات": "cycle", "الوقت": "time", "رقم_الخيط": "mhartid",
}
# instructions whose operand number N is a control and status register
CSR_OPERAND = {"csrrw": 1, "csrrs": 1, "csrrc": 1, "csrrwi": 1, "csrrsi": 1, "csrrci": 1,
               "csrr": 1, "csrw": 0, "csrs": 0, "csrc": 0, "csrwi": 0, "csrsi": 0, "csrci": 0}
# instructions whose LAST operand is a memory operand (reg ± expr)
MEMORY_OPERAND = {"lb", "lh", "lw", "ld", "lbu", "lhu", "lwu", "flw", "fld", "fsw", "fsd",
                  "sb", "sh", "sw", "sd", "jalr"}

SECTIONS = {
    ".شفرة": ".text",
    ".بيانات": ".data",
    ".بيانات_للقراءة": ".rodata",
    ".بيانات_صفرية": ".bss",
}
DIRECTIVES = {
    ".تضمين": ".incbin",
    ".عام": ".globl", ".محاذاة": ".align", ".بايت": ".byte",
    ".نصف_كلمة": ".half", ".كلمة": ".word", ".كلمة_مزدوجة": ".dword",
    ".سلسلة": ".string", ".فراغ": ".zero", ".ثابت": ".equ",
}
RELOC_OPS = {"عالي": "%hi", "أدنى": "%lo",
             "عالي_نسبي": "%pcrel_hi", "أدنى_نسبي": "%pcrel_lo"}
ESCAPES = {"س": "\\n", "ر": "\\r", "ج": "\\t", "٠": "\\000",
           "\\": "\\\\", '"': '\\"'}
BRACKETS = {")": "(", "(": ")", "]": "(", "[": ")"}  # file order -> GAS


class TranslitError(Exception):
    pass


def is_letter(c):
    o = ord(c)
    return 0x0621 <= o <= 0x063A or 0x0641 <= o <= 0x064A


def is_ident_char(c):
    return is_letter(c) or c == "_" or c in DIGITS


def digits_value(text):
    """Digit run in file order (least significant first) -> int."""
    value, mult = 0, 1
    for c in text:
        value += DIGITS[c] * mult
        mult *= 10
    return value


def lex_number(line, i):
    """Returns (token, new_index). Token is ('num', int) or ('local', 'Nf')."""
    base = 10
    if line[i] == "٠" and i + 1 < len(line) and line[i + 1] in ("س", "ث"):
        base = 16 if line[i + 1] == "س" else 2
        i += 2
    digits = []
    while i < len(line):
        c = line[i]
        if c == THOUSANDS:
            i += 1
            continue
        if c in DIGITS:
            digits.append(DIGITS[c])
        elif base == 16 and c in HEX_LETTERS:
            digits.append(HEX_LETTERS[c])
        else:
            break
        i += 1
    if not digits or any(d >= base for d in digits):
        raise TranslitError("bad number")
    value = sum(d * base ** k for k, d in enumerate(digits))
    if base == 10 and i < len(line) and line[i] in ("ق", "م"):
        suffix = "f" if line[i] == "ق" else "b"
        return ("local", f"{value}{suffix}"), i + 1
    if i < len(line) and is_ident_char(line[i]):
        raise TranslitError("letter directly after number")
    return ("num", value), i


def lex_string(line, i):
    quote = line[i]
    i += 1
    out = []
    while True:
        if i >= len(line):
            raise TranslitError("unterminated string")
        c = line[i]
        if c == quote:
            return "".join(out), i + 1
        if c == "\\":
            esc = line[i + 1]
            if quote == '"' and esc in ESCAPES:
                out.append(ESCAPES[esc])
            elif quote == "'" and esc in ("س", "ر", "ج", "٠", "\\", "'"):
                out.append({"س": "\n", "ر": "\r", "ج": "\t", "٠": "\0"}.get(esc, esc))
            else:
                raise TranslitError(f"unknown escape \\{esc}")
            i += 2
            continue
        if quote == '"' and (ord(c) > 0x7E or c == "\t"):
            out.extend("\\%03o" % b for b in c.encode("utf-8"))
        else:
            out.append(c)
        i += 1


def tokenize(line):
    toks, i = [], 0
    while i < len(line):
        c = line[i]
        if c in " \t":
            i += 1
        elif c == "#":
            break
        elif c in DIGITS:
            tok, i = lex_number(line, i)
            toks.append(tok)
        elif c == '"':
            s, i = lex_string(line, i)
            toks.append(("str", s))
        elif c == "'":
            s, i = lex_string(line, i)
            if len(s) != 1:
                raise TranslitError("character literal must be one character")
            toks.append(("num", ord(s)))
        elif c == "." or is_letter(c) or c == "_":
            j = i + 1
            while j < len(line) and is_ident_char(line[j]):
                j += 1
            toks.append(("id", line[i:j]))
            i = j
        elif c == COMMA:
            toks.append(("comma", c))
            i += 1
        elif c in "+-*/:" or c in BRACKETS:
            toks.append(("punct", c))
            i += 1
        elif c in "0123456789":
            raise TranslitError("ASCII digits are not allowed")
        else:
            raise TranslitError(f"unexpected character {c!r} (U+{ord(c):04X})")
    return toks


def register(name):
    if name in REGISTERS_FIXED:
        return REGISTERS_FIXED[name]
    for prefix, (gas, top) in REGISTER_FAMILIES.items():
        rest = name[len(prefix):]
        if name.startswith(prefix) and rest and all(c in DIGITS for c in rest):
            n = digits_value(rest)
            if n > top:
                raise TranslitError(f"register {name} out of range")
            return f"{gas}{n}"
    return None


def symbol_text(name):
    if name in SECTIONS:
        return SECTIONS[name]
    return name


def expr_text(toks):
    out, i = [], 0
    while i < len(toks):
        kind, val = toks[i]
        if kind == "id" and val in RELOC_OPS and i + 2 < len(toks) \
                and toks[i + 1] == ("punct", ":"):
            j = i + 3
            if toks[i + 2] in (("punct", ")"), ("punct", "]")):
                depth = 0
                for j in range(i + 2, len(toks)):
                    if toks[j] in (("punct", ")"), ("punct", "]")):
                        depth += 1
                    elif toks[j] in (("punct", "("), ("punct", "[")):
                        depth -= 1
                        if depth == 0:
                            break
                j += 1
            out.append(f"{RELOC_OPS[val]}({expr_text(toks[i + 2:j])})")
            i = j
            continue
        if kind == "num":
            out.append(str(val))
        elif kind == "local":
            out.append(val)
        elif kind == "id":
            reg = register(val)
            out.append(reg if reg else symbol_text(val))
        elif kind == "str":
            out.append('"' + val + '"')
        elif kind == "punct":
            out.append(BRACKETS.get(val, val))
        i += 1
    return "".join(out)


def memory_operand(toks):
    if not toks or toks[0][0] != "id" or register(toks[0][1]) is None:
        raise TranslitError("memory operand must start with a register")
    reg = register(toks[0][1])
    rest = toks[1:]
    if not rest:
        return f"0({reg})"
    sign = rest[0]
    if sign not in (("punct", "+"), ("punct", "-")):
        raise TranslitError("expected + or - after base register")
    inner = expr_text(rest[1:])
    if sign[1] == "-":
        return f"-({inner})({reg})"
    return f"{inner}({reg})"


def split_operands(toks):
    ops, cur = [], []
    for t in toks:
        if t[0] == "comma":
            ops.append(cur)
            cur = []
        else:
            cur.append(t)
    if cur or ops:
        ops.append(cur)
    return ops


def translate_line(line):
    toks = tokenize(line)
    out = []
    # labels: `name :` or local `N :`
    while len(toks) >= 2 and toks[1] == ("punct", ":") and \
            (toks[0][0] == "id" and not toks[0][1].startswith(".") or toks[0][0] == "num"):
        out.append(f"{toks[0][1]}:")
        toks = toks[2:]
    if not toks:
        return " ".join(out)
    kind, head = toks[0]
    if kind != "id":
        raise TranslitError("expected instruction or directive")
    ops = split_operands(toks[1:])
    if head in SECTIONS:
        # GAS has .text/.data/.bss directives but no .rodata one
        out.append(".section .rodata" if SECTIONS[head] == ".rodata" else SECTIONS[head])
    elif head == ".قسم":
        out.append(".section " + ", ".join(expr_text(o) for o in ops))
    elif head in DIRECTIVES:
        out.append(DIRECTIVES[head] + " " + ", ".join(expr_text(o) for o in ops))
    elif head in MNEMONICS:
        gas = MNEMONICS[head]
        texts = [expr_text(o) for o in ops]
        if gas in MEMORY_OPERAND and ops:
            texts[-1] = memory_operand(ops[-1])
        k = CSR_OPERAND.get(gas)
        if k is not None and k < len(ops) and len(ops[k]) == 1 and ops[k][0][0] == "id" \
                and ops[k][0][1] in CSRS:
            texts[k] = CSRS[ops[k][0][1]]
        out.append(gas + (" " + ", ".join(texts) if texts else ""))
    else:
        raise TranslitError(f"unknown instruction or directive {head}")
    return " ".join(out)


def translate(text):
    lines = []
    for n, line in enumerate(text.split("\n"), 1):
        try:
            lines.append(translate_line(line))
        except TranslitError as e:
            raise TranslitError(f"line {n}: {e}") from None
    return "\n".join(lines) + "\n"


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: translit.py input.تجميع output.s")
    with open(sys.argv[1], encoding="utf-8") as f:
        text = f.read()
    try:
        result = translate(text)
    except TranslitError as e:
        sys.exit(f"{sys.argv[1]}: {e}")
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        f.write(result)


if __name__ == "__main__":
    main()
