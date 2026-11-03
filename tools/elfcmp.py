#!/usr/bin/env python3
"""Compare two RISC-V ELF64 relocatable objects semantically.

Used by run_asm_tests.sh: ours (مجمع) vs reference (GNU as -mno-relax).
Compared:
  * contents of .text .data .rodata, size of .bss
  * symbols: (name, binding, section, value), ignoring section symbols,
    file symbols and GNU mapping symbols ($x, $d)
  * relocations per section: (offset, type, symbol, addend)
Not compared: section order, alignment fields, .riscv.attributes, layout.
Exit status 0 if equal, 1 otherwise (differences printed).
"""
import struct
import sys

COMPARED_SECTIONS = (".text", ".data", ".rodata")


def read_elf(path):
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise SystemExit(f"{path}: not an ELF64 little-endian file")
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
    secs = []
    for i in range(shnum):
        name, typ, flags, addr, off, size, link, info, align, ent = \
            struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize)
        secs.append(dict(name=name, type=typ, off=off, size=size, link=link, info=info))
    strtab = secs[shstrndx]

    def cstr(sec, off):
        start = sec["off"] + off
        end = data.index(b"\0", start)
        return data[start:end]

    for s in secs:
        s["name"] = cstr(strtab, s["name"]).decode("utf-8", "replace")
    by_name = {s["name"]: s for s in secs}

    syms = []
    symtab = next((s for s in secs if s["type"] == 2), None)
    if symtab:
        names = secs[symtab["link"]]
        for i in range(symtab["size"] // 24):
            name, info, other, shndx, value, size = \
                struct.unpack_from("<IBBHQQ", data, symtab["off"] + i * 24)
            nm = cstr(names, name).decode("utf-8", "replace") if name else ""
            if shndx == 0:
                sec = "UND"
            elif shndx == 0xFFF1:
                sec = "ABS"
            elif shndx < len(secs):
                sec = secs[shndx]["name"]
            else:
                sec = f"#{shndx}"
            syms.append(dict(name=nm, bind=info >> 4, type=info & 15, sec=sec, value=value))

    relocs = {}
    for s in secs:
        if s["type"] != 4:
            continue
        target = secs[s["info"]]["name"]
        lst = []
        for i in range(s["size"] // 24):
            off, info, addend = struct.unpack_from("<QQq", data, s["off"] + i * 24)
            sym = syms[info >> 32]
            symname = sym["sec"] if sym["type"] == 3 else sym["name"]
            lst.append((off, info & 0xFFFFFFFF, symname, addend))
        relocs[target] = sorted(lst)

    contents = {}
    for name in COMPARED_SECTIONS:
        s = by_name.get(name)
        contents[name] = data[s["off"]:s["off"] + s["size"]] if s else b""
    bss = by_name.get(".bss")
    bss_size = bss["size"] if bss else 0

    symset = set()
    for s in syms:
        if s["type"] in (3, 4) or not s["name"] or s["name"].startswith("$"):
            continue
        symset.add((s["name"], s["bind"], s["sec"], s["value"]))
    return contents, bss_size, symset, relocs


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: elfcmp.py ours.o reference.o")
    a = read_elf(sys.argv[1])
    b = read_elf(sys.argv[2])
    ok = True
    for name in COMPARED_SECTIONS:
        x, y = a[0][name], b[0][name]
        if x != y:
            ok = False
            print(f"  {name}: contents differ (ours {len(x)} bytes, ref {len(y)} bytes)")
            for i in range(0, max(len(x), len(y)), 4):
                if x[i:i + 4] != y[i:i + 4]:
                    print(f"    +{i:#06x}: ours {x[i:i+4].hex()}  ref {y[i:i+4].hex()}")
    if a[1] != b[1]:
        ok = False
        print(f"  .bss size: ours {a[1]}, ref {b[1]}")
    for s in sorted(a[2] - b[2]):
        ok = False
        print(f"  symbol only in ours: {s}")
    for s in sorted(b[2] - a[2]):
        ok = False
        print(f"  symbol only in ref:  {s}")
    for sec in sorted(set(a[3]) | set(b[3])):
        x, y = a[3].get(sec, []), b[3].get(sec, [])
        if x != y:
            ok = False
            print(f"  relocations in {sec} differ:")
            for r in sorted(set(x) - set(y)):
                print(f"    only ours: {r}")
            for r in sorted(set(y) - set(x)):
                print(f"    only ref:  {r}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
