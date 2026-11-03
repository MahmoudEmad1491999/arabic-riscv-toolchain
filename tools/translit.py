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
}
REGISTER_FAMILIES = {  # prefix -> (gas prefix, max index)
    "سجل": ("x", 31),
    "مؤقت": ("t", 6),
    "محفوظ": ("s", 11),
    "وسيط": ("a", 7),
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
}
# instructions whose LAST operand is a memory operand (reg ± expr)
MEMORY_OPERAND = {"lb", "lh", "lw", "ld", "lbu", "lhu", "lwu",
                  "sb", "sh", "sw", "sd", "jalr"}

SECTIONS = {
    ".شفرة": ".text",
    ".بيانات": ".data",
    ".بيانات_للقراءة": ".rodata",
    ".بيانات_صفرية": ".bss",
}
DIRECTIVES = {
    ".عام": ".globl", ".محاذاة": ".align", ".بايت": ".byte",
    ".نصف_كلمة": ".half", ".كلمة": ".word", ".كلمة_مزدوجة": ".dword",
    ".سلسلة": ".string", ".فراغ": ".zero", ".ثابت": ".equ",
}
RELOC_OPS = {"عالي": "%hi", "أدنى": "%lo"}
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
        out.append(SECTIONS[head])
    elif head == ".قسم":
        out.append(".section " + ", ".join(expr_text(o) for o in ops))
    elif head in DIRECTIVES:
        out.append(DIRECTIVES[head] + " " + ", ".join(expr_text(o) for o in ops))
    elif head in MNEMONICS:
        gas = MNEMONICS[head]
        texts = [expr_text(o) for o in ops]
        if gas in MEMORY_OPERAND and ops:
            texts[-1] = memory_operand(ops[-1])
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
