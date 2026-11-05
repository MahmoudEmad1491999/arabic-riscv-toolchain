#!/usr/bin/env python3
"""docs_pdf.py - the Markdown in docs/ as PDF, with Arabic shown as in the editor.

Decision 48. Arabic source is stored in file order (typing order) and the
editor shows it right to left exactly as typed, with no Unicode bidi
reordering: numbers are least significant digit first (٥٢ is twenty-five),
`)` opens, `أ > ب` means less, and nothing is mirrored. A browser or PDF
renderer would apply the bidi algorithm and show it differently (٥٢ read as
fifty-two, brackets mirrored), and the engines disagree on explicit bidi
overrides. So this tool does the layout itself:

  1. the Arabic letters of a line are shaped (joined) in typing order, with
     arabic_reshaper, into their contextual presentation forms;
  2. the line is reversed into visual order; a Latin run inside it (e.g.
     "QEMU -bios none") stays one left-to-right unit, as in the diagnostics;
  3. every character is its own inline box, so the renderer has nothing to
     reorder or mirror: what is drawn is exactly what the editor shows.

Text inside <samp>...</samp> in the Markdown is shown in storage order: each
code point in its own box, left to right, unjoined. Use it where the document
talks about the stored characters themselves ("the file contains
<samp>٤٣٢١</samp>").

Which text gets this treatment:
  * a line of a code block whose first strong character is Arabic
    (right-aligned);
  * an inline code span that contains Arabic;
  * a text run outside code whose first strong character is Arabic;
  * in English text, an Arabic word that contains Arabic digits.
Everything else is ordinary text (an Arabic word without digits looks the
same either way and keeps the font's own shaping).

Usage: docs_pdf.py OUT_DIR file.md...   |   docs_pdf.py --self-test
Needs: pandoc; Python packages weasyprint and arabic-reshaper.
Text copied out of the PDF is in visual order.
"""
import html
import os
import subprocess
import sys
import unicodedata
from html.parser import HTMLParser

import arabic_reshaper

RESHAPER = arabic_reshaper.ArabicReshaper(configuration={
    "delete_harakat": False,
    "support_ligatures": True,
})


def is_arabic(c):
    return "؀" <= c <= "ۿ" or "ݐ" <= c <= "ݿ" or \
        "ࢠ" <= c <= "ࣿ" or "ﭐ" <= c <= "﷿" or "ﹰ" <= c <= "﻿"


def is_latin(c):
    return c.isascii() and c.isalnum()


def first_strong_arabic(s):
    for c in s:
        b = unicodedata.bidirectional(c)
        if b in ("AL", "R"):
            return True
        if b == "L":
            return False
    return False


def has_arabic(s):
    return any(is_arabic(c) for c in s)


def latin_run_end(s, i):
    """End of the Latin run starting at i, or i if none starts there. A run
    is ASCII words with the punctuation and single spaces between them:
    "QEMU -bios none", "0x80000000", "a.out"."""
    n = len(s)

    def word_start(k):
        return k < n and (is_latin(s[k]) or (s[k] == "-" and k + 1 < n and is_latin(s[k + 1])))

    if not word_start(i):
        return i
    j = i
    while j < n:
        if is_latin(s[j]):
            j += 1
        elif s[j] in "-./:'_+" and j + 1 < n and is_latin(s[j + 1]):
            j += 1
        elif s[j] == " " and word_start(j + 1):
            j += 1
        else:
            break
    return j


def visual_tokens(s):
    """The line in visual order (left to right on the page): a list of
    ("latin", text) and ("char", c)."""
    shaped = RESHAPER.reshape(s)
    toks, i = [], 0
    while i < len(shaped):
        j = latin_run_end(shaped, i)
        if j > i:
            toks.append(("latin", shaped[i:j]))
            i = j
        else:
            toks.append(("char", shaped[i]))
            i += 1
    toks.reverse()
    return toks


def visual_html(s, cls):
    parts = []
    for kind, text in visual_tokens(s):
        if kind == "latin":
            parts.append('<span class="vl">' + html.escape(text, quote=False) + "</span>")
        else:
            parts.append('<span class="vc">' +
                         ("&#160;" if text == " " else html.escape(text, quote=False)) + "</span>")
    return f'<span class="{cls}">' + "".join(parts) + "</span>"


def storage_html(s):
    """<samp>: the stored code points themselves, one box each, left to
    right in storage order, unjoined."""
    return '<span class="vspan">' + "".join(
        '<span class="vc">' + ("&#160;" if c == " " else html.escape(c, quote=False)) + "</span>"
        for c in s) + "</span>"


def english_text(s):
    """English text: Arabic words with Arabic digits laid out visually."""
    out, i, n = [], 0, len(s)
    while i < n:
        if is_arabic(s[i]):
            j = i
            while j < n and (is_arabic(s[j]) or (s[j] in "_." and j + 1 < n and is_arabic(s[j + 1]))):
                j += 1
            word = s[i:j]
            if any(unicodedata.bidirectional(c) == "AN" for c in word):
                out.append(visual_html(word, "vw"))
            else:
                out.append(html.escape(word, quote=False))
            i = j
        else:
            out.append(html.escape(s[i], quote=False))
            i += 1
    return "".join(out)


class Transform(HTMLParser):
    """Copies pandoc's HTML, changing only text (never tags or attributes)."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.out = []
        self.pre = 0
        self.code = 0
        self.samp = 0   # <samp>: storage order, shown as is (no layout)

    def handle_starttag(self, tag, attrs):
        self.out.append(self.get_starttag_text())
        self.pre += tag == "pre"
        self.code += tag == "code"
        self.samp += tag == "samp"

    def handle_startendtag(self, tag, attrs):
        self.out.append(self.get_starttag_text())

    def handle_endtag(self, tag):
        self.out.append(f"</{tag}>")
        self.pre -= tag == "pre"
        self.code -= tag == "code"
        self.samp -= tag == "samp"

    def handle_data(self, data):
        if self.samp:
            self.out.append(storage_html(data))
        elif self.pre:
            res = []
            lines = data.split("\n")
            arabic_block = any(first_strong_arabic(x) for x in lines)
            for line in lines:
                neutral = not any(unicodedata.bidirectional(c) in ("L", "R", "AL") for c in line)
                if first_strong_arabic(line) or (arabic_block and neutral and line.strip()):
                    res.append(visual_html(line, "vline"))
                else:
                    res.append(english_text(line))
            self.out.append("\n".join(res))
        elif self.code and has_arabic(data):
            self.out.append(visual_html(data, "vspan"))
        elif first_strong_arabic(data):
            self.out.append(visual_html(data, "vspan"))
        else:
            self.out.append(english_text(data))

    def handle_comment(self, data):
        self.out.append(f"<!--{data}-->")

    def handle_decl(self, decl):
        self.out.append(f"<!{decl}>")


CSS = """
@page { size: A4; margin: 18mm 16mm;
        @bottom-center { content: counter(page); font-size: 8pt; color: #888; } }
body { font-family: "Noto Sans", "Noto Naskh Arabic", "DejaVu Sans", sans-serif; font-size: 10pt;
       line-height: 1.5; color: #1a1a1a; }
h1, h2, h3, h4 { line-height: 1.25; }
h1 { font-size: 19pt; border-bottom: 2px solid #333; padding-bottom: 4px; }
h2 { font-size: 14pt; margin-top: 1.5em; border-bottom: 1px solid #bbb; }
h3 { font-size: 11.5pt; }
code, pre { font-family: "DejaVu Sans Mono", monospace; font-size: 9pt; }
code { background: #f1f1f1; padding: 0 2px; border-radius: 2px; }
pre { background: #f6f6f6; border: 1px solid #ddd; padding: 7px 10px; border-radius: 4px;
      white-space: pre-wrap; }
pre code { background: none; padding: 0; }
.vline { display: block; text-align: right; }
.vspan, .vw { white-space: nowrap; }
.vc, .vl { display: inline-block; white-space: pre; }
table { border-collapse: collapse; margin: 0.7em 0; font-size: 9pt; }
th, td { border: 1px solid #ccc; padding: 3px 6px; vertical-align: top; }
th { background: #eee; }
img { max-width: 100%; }
samp { font-family: "DejaVu Sans Mono", monospace; font-size: 9pt; background: #fff6d6; padding: 0 2px; }
samp .vc { padding: 0 1px; }
"""


def convert(md_path, out_dir):
    import weasyprint  # here, so --self-test works without it
    name = os.path.splitext(os.path.basename(md_path))[0]
    body = subprocess.run(["pandoc", "-f", "gfm", "-t", "html5", md_path],
                          check=True, capture_output=True, text=True).stdout
    t = Transform()
    t.feed(body)
    t.close()
    page = (f'<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">'
            f"<title>{html.escape(name)}</title><style>{CSS}</style></head>"
            f"<body>{''.join(t.out)}</body></html>")
    html_path = os.path.join(out_dir, name + ".html")
    pdf_path = os.path.join(out_dir, name + ".pdf")
    with open(html_path, "w", encoding="utf-8") as f:
        f.write(page)
    base = os.path.dirname(os.path.abspath(md_path)) + "/"
    weasyprint.HTML(string=page, base_url=base).write_pdf(pdf_path)
    return pdf_path


def self_test():
    """The layout rules on known lines (visual order, left to right)."""
    def plain(s):
        return "".join(t for _, t in visual_tokens(s))
    assert plain("٥٢") == "٢٥", plain("٥٢")                    # file ٥٢ (25) reads ٢٥
    assert plain("أ)ب(") == "(" + plain("ب") + ")" + plain("أ")  # nothing mirrored
    toks = visual_tokens("ش QEMU -bios none ص")
    assert ("latin", "QEMU -bios none") in toks, toks
    assert first_strong_arabic("    استورد غرة؛") and not first_strong_arabic("make test # غرة")
    print("docs_pdf self-test: ok")
    return 0


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return self_test()
    if len(sys.argv) < 3:
        print("usage: docs_pdf.py OUT_DIR file.md...  |  docs_pdf.py --self-test", file=sys.stderr)
        return 2
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    for md in sys.argv[2:]:
        print(convert(md, out_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
