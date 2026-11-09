#!/usr/bin/env python3
"""Render a spec source with file-order placeholders into the final document.

Examples in the specs are stored in FILE ORDER, which is easy to get wrong by
hand. Write the value you mean and let this script produce the file-order form:

  {{n:255}}     decimal number            -> ٥٥٢      (least significant digit first)
  {{n:-128}}    negative number           -> -٨٢١     (minus first, then the digits)
  {{n:1.25}}    real number               -> ٥٢٫١     (the whole literal reversed, decision 68)
  {{h:0x200C}}  hex number                -> ٠سج٠٠٢
  {{t:s32}}     type name, signed 32      -> صحيح٢٣   (digits in a name: typed so they read 32)
  {{t:u8}}      type name, unsigned 8     -> طبيعي٨
  {{t:f32}}     real, 32 bits             -> حقيقي٢٣  ({{t:f64}} -> حقيقي٤٦)
  {{t:size}}    -> حجم,  {{t:bool}} -> منطقي,  {{t:char}} -> حرف

Usage: tools/gen_spec.py docs/spec-language.src.md docs/spec-language.md
"""
import re
import sys

D = "٠١٢٣٤٥٦٧٨٩"
HEX = D + "أبجدهو"
SPECIAL = {"size": "حجم", "bool": "منطقي", "char": "حرف"}


def dec(v):
    if isinstance(v, str):  # a real literal: every character reversed, ٫ for the point
        sign = "-" if v.startswith("-") else ""
        return sign + "".join("٫" if c == "." else D[int(c)] for c in reversed(v.lstrip("-")))
    sign = "-" if v < 0 else ""
    return sign + "".join(D[int(c)] for c in reversed(str(abs(v))))


def hexa(v):
    return "٠س" + "".join(HEX[int(c, 16)] for c in reversed(format(v, "x")))


def typ(name):
    if name in SPECIAL:
        return SPECIAL[name]
    if name in ("f32", "f64"):
        return "حقيقي" + dec(int(name[1:]))
    m = re.fullmatch(r"([su])(8|16|32|64)", name)
    if not m:
        raise SystemExit(f"unknown type placeholder {name}")
    base = "صحيح" if m.group(1) == "s" else "طبيعي"
    return base + dec(int(m.group(2)))


def render(text):
    def sub(m):
        kind, arg = m.group(1), m.group(2)
        if kind == "n":
            return dec(arg) if "." in arg else dec(int(arg))
        if kind == "h":
            return hexa(int(arg, 16))
        return typ(arg)
    return re.sub(r"\{\{([nht]):([^}]+)\}\}", sub, text)


if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    out = render(open(src, encoding="utf-8").read())
    banner = f"<!-- Generated from {src.split('/')[-1]} by tools/gen_spec.py: edit the source, not this file. -->\n\n"
    open(dst, "w", encoding="utf-8").write(banner + out)
