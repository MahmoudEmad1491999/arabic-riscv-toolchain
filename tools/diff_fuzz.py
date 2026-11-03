#!/usr/bin/env python3
"""Differential fuzzing: ours vs GNU as on randomly reshuffled programs.

Takes one of the valid test programs, deletes / duplicates / swaps whole lines, and for every
variant OUR assembler accepts, assembles the transliteration with GNU as and
compares the objects with elfcmp.py. Known, documented differences
(docs/assumptions.md A14, A15) are skipped.
Usage: tools/diff_fuzz.py [iterations] [seed]
"""
import os
import random
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AS = os.path.join(ROOT, "build", "مجمع")
SEED_FILES = [os.path.join(ROOT, "tests", "asm", f)
              for f in ("كل_التعليمات.تجميع", "أشباه_التعليمات.تجميع", "طباعة_أعداد.تجميع")]
PREFIX = os.environ.get("RISCV_PREFIX", "riscv64-linux-gnu-")


def run(*cmd):
    return subprocess.run(cmd, capture_output=True)


def main():
    iterations = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    random.seed(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
    seeds = [open(f, encoding="utf-8").read().split("\n") for f in SEED_FILES]
    tmp = tempfile.mkdtemp()
    src, ours, ref_s, ref_o = (os.path.join(tmp, n) for n in
                               ("t.تجميع", "t.o", "t.s", "t.ref.o"))
    stats = dict(match=0, mismatch=0, skipped=0)
    for _ in range(iterations):
        lines = random.choice(seeds)
        prog = lines[:]
        for _ in range(random.randint(1, 8)):
            p = random.randrange(len(prog))
            r = random.random()
            if r < 0.4:
                del prog[p]
            elif r < 0.7:
                prog.insert(p, random.choice(lines))
            else:
                q = random.randrange(len(prog))
                prog[p], prog[q] = prog[q], prog[p]
        with open(src, "w", encoding="utf-8") as f:
            f.write("\n".join(prog))
        if run(AS, src, ours).returncode != 0:
            continue
        if run("python3", "-I", os.path.join(ROOT, "tools", "translit.py"), src, ref_s).returncode:
            stats["skipped"] += 1
            continue
        text = open(ref_s, encoding="utf-8").read().split(".data")[0]
        if re.search(r"^\.align [012]$", text, re.M):   # A15
            stats["skipped"] += 1
            continue
        g = run(PREFIX + "as", "-march=rv64im", "-mabi=lp64", "-mno-relax", ref_s, "-o", ref_o)
        if g.returncode:                                   # A14: GNU is stricter
            stats["skipped"] += 1
            continue
        c = run("python3", "-I", os.path.join(ROOT, "tools", "elfcmp.py"), ours, ref_o)
        if c.returncode:
            stats["mismatch"] += 1
            keep = os.path.join(ROOT, "build", f"mismatch{stats['mismatch']}.تجميع")
            with open(keep, "w", encoding="utf-8") as f:
                f.write("\n".join(prog))
            print(c.stdout.decode(), "kept as", keep)
        else:
            stats["match"] += 1
    print("differential fuzz:", stats)
    sys.exit(1 if stats["mismatch"] else 0)


if __name__ == "__main__":
    main()
