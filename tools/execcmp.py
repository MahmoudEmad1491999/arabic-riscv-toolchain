#!/usr/bin/env python3
"""Compare two RISC-V ELF64 executables by what gets loaded.

  execcmp.py script ours.exe out.ld
      write a GNU ld script that places .text/.rodata/.data/.bss at the
      addresses our linker chose, so GNU ld can be run on the same objects (use with -e _بداية)
  execcmp.py compare ours.exe ref.exe
      compare entry point, address/size/contents of .text .rodata .data,
      address/size of .bss, and the value of every global symbol of ours
Exit status 0 if equal.
"""
import struct
import sys

SECTIONS = (".text", ".rodata", ".data", ".bss")


def read_exe(path):
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF":
        raise SystemExit(f"{path}: not ELF")
    entry, = struct.unpack_from("<Q", data, 0x18)
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
    secs = []
    for i in range(shnum):
        f = struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize)
        secs.append(dict(name=f[0], type=f[1], flags=f[2], addr=f[3], off=f[4],
                         size=f[5], link=f[6], info=f[7]))
    strtab = secs[shstrndx]

    def cstr(sec, off):
        start = sec["off"] + off
        return data[start:data.index(b"\0", start)].decode("utf-8", "replace")

    for s in secs:
        s["name"] = cstr(strtab, s["name"])
    by = {s["name"]: s for s in secs}
    out = {}
    for name in SECTIONS:
        s = by.get(name)
        if s is None or s["size"] == 0:
            out[name] = (None, 0, b"")
        else:
            body = b"" if s["type"] == 8 else data[s["off"]:s["off"] + s["size"]]
            out[name] = (s["addr"], s["size"], body)
    syms = {}
    symtab = next((s for s in secs if s["type"] == 2), None)
    if symtab:
        names = secs[symtab["link"]]
        for i in range(symtab["size"] // 24):
            n, info, other, shndx, value, size = struct.unpack_from("<IBBHQQ", data, symtab["off"] + i * 24)
            if info >> 4 in (1, 2) and n:
                syms[cstr(names, n)] = value
    return entry, out, syms


def script(ours, path):
    _, secs, _ = read_exe(ours)
    lines = ["SECTIONS {"]  # entry is passed as -e: GNU ld scripts are ASCII only
    for name in SECTIONS:
        addr = secs[name][0]
        if addr is not None:
            lines.append(f"  {name} 0x{addr:x} : {{ *({name} {name}.*) }}")
    lines.append("}")
    open(path, "w", encoding="utf-8").write("\n".join(lines) + "\n")


def compare(ours, ref):
    e1, s1, y1 = read_exe(ours)
    e2, s2, y2 = read_exe(ref)
    ok = True
    if e1 != e2:
        ok = False
        print(f"  entry: ours {e1:#x}, ref {e2:#x}")
    for name in SECTIONS:
        a, b = s1[name], s2[name]
        if a[:2] != b[:2]:
            ok = False
            print(f"  {name}: ours at {a[0]} size {a[1]}, ref at {b[0]} size {b[1]}")
        elif a[2] != b[2]:
            ok = False
            print(f"  {name}: contents differ")
            for i in range(0, len(a[2]), 4):
                if a[2][i:i + 4] != b[2][i:i + 4]:
                    print(f"    {a[0] + i:#x}: ours {a[2][i:i+4].hex()} ref {b[2][i:i+4].hex()}")
    for name, value in sorted(y1.items()):
        if y2.get(name) != value:
            ok = False
            print(f"  symbol {name}: ours {value:#x}, ref {y2.get(name)}")
    return ok


def main():
    if len(sys.argv) == 4 and sys.argv[1] == "script":
        script(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 4 and sys.argv[1] == "compare":
        sys.exit(0 if compare(sys.argv[2], sys.argv[3]) else 1)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
