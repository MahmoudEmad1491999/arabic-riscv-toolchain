#!/usr/bin/env python3
"""render_diagrams.py - docs/diagrams/*.dot to SVG (docs) and PNG (slides, video).

Labels mix English and Arabic ("X.كائن", "_بداية", "os/برامج"). Graphviz lets
the renderer apply the Unicode bidi algorithm to every line, and a line whose
first strong character is Arabic becomes right-to-left: "X.كائن" turns into
"كائن.X". Every line of every label is therefore started with U+200E
(left-to-right mark), so lines read left to right like the English around
them, while each Arabic word is still written right to left. The .dot
sources stay clean; the mark is added here.

Usage: render_diagrams.py [file.dot...]   (default: every docs/diagrams/*.dot)
"""
import glob
import os
import re
import subprocess
import sys

LRM = "‎"


def mark_label(text):
    # starts of lines (\n, \l, \r escapes) and of record fields ({ | })
    text = re.sub(r'(\\[nlr])', lambda m: m.group(1) + LRM, text)
    text = re.sub(r'(?<!\\)([{|}]\s*(?:<[^>]*>\s*)?)', lambda m: m.group(1) + LRM, text)
    m = re.match(r'\s*<[^>]*>\s*', text)  # a port at the very start
    if m:
        return text[:m.end()] + LRM + text[m.end():]
    return LRM + text


def mark(dot):
    return re.sub(r'label="((?:[^"\\]|\\.)*)"', lambda m: 'label="' + mark_label(m.group(1)) + '"', dot)


def render(path):
    base = os.path.splitext(path)[0]
    src = mark(open(path, encoding="utf-8").read())
    for fmt, extra in (("svg", []), ("png", ["-Gdpi=180"])):
        subprocess.run(["dot", "-T" + fmt, *extra, "-o", base + "." + fmt],
                       input=src.encode("utf-8"), check=True)
    print(base + ".svg", base + ".png")


def main():
    files = sys.argv[1:] or sorted(glob.glob(os.path.join(os.path.dirname(__file__), "..", "docs", "diagrams", "*.dot")))
    for f in files:
        render(f)
    return 0


if __name__ == "__main__":
    sys.exit(main())
