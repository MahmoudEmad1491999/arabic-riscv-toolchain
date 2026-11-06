# Operating system specification (Phase 5; stage 7 in Phase 6)

The operating system is written in غرة, plus the minimum of our Arabic assembly, and runs bare-metal on `qemu-system-riscv64 -machine virt -bios none -m 128M` (decision 39). Build it with `tools/build_os.sh` (user programs first, then the kernel with them embedded); `make os` boots the test kernels.

## Layers

| Layer | Mode | Files |
|---|---|---|
| Start, trap entry and exit, context switch | machine / supervisor | `os/بدء.تجميع`, `os/معالج.تجميع` |
| Machine layer: protection, delegation, timer (A65) | machine | `os/آلة.غرة` |
| Kernel: pages, Sv39 tables, traps, processes, scheduler, system calls | supervisor | `os/ذاكرة/…`, `os/مشرف/…`, `os/نواة.غرة` |
| Devices: UART, test device (power-off), CLINT timer, virtio disk | — | `os/جهاز/…` |
| File system (docs/spec-fs.md) | supervisor | `os/مشرف/ملفات.غرة` |
| User library: `نظام` (system services), `طباعة` | user | `os/مستخدم/…` |
| Programs, in our executable format, on the disk (decisions 53, 54) | user | `os/برامج/…` |

## Processes (decisions 44, 47; A71)

- At most 8 processes. A process has a number (starting at 1), its own Sv39 page table (the kernel mapped without the user bit, its own pages with it) and its own 8 KiB kernel stack.
- States: `حرة` (free slot), `جاهزة` (ready), `تعمل` (running), `تنتظر` (waiting for a child), `منتهية` (finished, not yet collected).
- `شغل` loads a program from the disk into a new process, with its arguments; there is no `fork` (decision 44). The kernel starts the program `أول` at boot and powers off when no process is left. `أول` runs the system tests and then starts the shell `بوابة`.
- **Arguments (decision 62, A78).** `شغل` takes the arguments one after another, each followed by a zero byte (at most 2048 bytes and 64 arguments; the program name at most 256 bytes). The kernel builds the top of the new stack as Linux does: the strings at the end of the top page, then from the stack pointer (16-byte aligned) the count, the pointers (the program name first), a zero, and a zero for an empty environment. The start file therefore starts a program the same way on both platforms.
- Scheduling is round-robin; every timer tick (10 ms) taken in user mode makes the process give up the processor (decision 47). The kernel itself is not preempted.
- An exception in a user process (page fault, illegal instruction …) prints its cause and ends that process with code 102; an exception in the kernel stops the system with code 102.
- Powering off turns address translation off first: the test device is not mapped in the processes' page tables, so a program may use those addresses.

## User address space (A71)

| Range | Contents |
|---|---|
| from `0x10000` | the program (code, read-only data; then data and `.bss` from the next page), as linked by `رابط` |
| below `0x10000000` | the program must end here: the kernel's devices are mapped from there (A81) |
| `0x20000000` – `0x3F000000` | the heap: pages mapped by `احجز`, from the bottom up (decision 60, A77) |
| up to `0x40000000` | the stack: 64 pages ending at `0x40000000`, with the arguments at the top |
| from `0x40000000` | nothing for the user; the kernel is mapped at its physical addresses (from `0x80000000`) without the user bit |

## The system interface `نظام` (decision 43)

Programs import the module `نظام`, whose functions call the imports `_نظام_<name>`. The executable leaves those unresolved; the loader binds each one to its entry in the **system page**, a page of `ecall` stubs (3 instructions each, in call-number order) that the kernel maps into every process at `0x3FF00000` (decisions 43, 53). The call number is in `وسيط٧`, the arguments in `وسيط٠` …, the result in `وسيط٠` (and `وسيط١`).

| Number | `نظام.` | Arguments | Result |
|---|---|---|---|
| ١ | `اكتب` | the bytes (`][ثابت طبيعي٨`: address, length) | number of bytes written, or all ones if the bytes are not readable by the process |
| ٢ | `اخرج` | exit code | does not return |
| ٣ | `انتظر` | — | `نتيجة_انتظار}رقم، رمز{` of a finished child; number ٠ when the process has no children |
| ٤ | `تخل` | — | — (gives up the processor) |
| ٥ | `رقمي` | — | the process number |
| ٦ | `شغل` | program path (`][ثابت طبيعي٨`), arguments (`][ثابت طبيعي٨`, each followed by a zero byte) | the new process number, or ٠ |
| ٧–١٠ | `افتح`، `اقرأ`، `اكتب_في`، `أغلق` | see docs/spec-fs.md | |
| ١١ | `اقرأ_سطرا` | buffer (`][طبيعي٨`) | the length of a line typed on the keyboard (the serial port), without its end; typed characters are echoed, backspace erases a whole character |
| ١٢ | `مدخل_الدليل` | directory path, position, buffer | see docs/spec-fs.md |
| ١٣ | `احجز` | size in bytes | the address of that many zeroed bytes (whole pages) in the heap, or ٠ |
| ١٤ | `أنشئ_دليل` | path | see docs/spec-fs.md |

Language stops in a program (index out of range, overflow, failed conversion) print a message and end the program with code 101.

## The shell `بوابة` (decision 55)

One command per line: `اعرض` lists the root (`اعرض دليل` that directory; directories end in `/`), `اطبع ملف` prints a file, `خروج` ends the shell; any other first word runs the program at that path, with the following words as its arguments (decision 62), and reports its exit code. `صدى` prints its arguments.

## Running it

```
tools/build_os.sh            # programs, kernel, and os/بناء/قرص.صورة
qemu-system-riscv64 -machine virt -bios none -m 128M -nographic \
    -global virtio-mmio.force-legacy=false \
    -drive file=os/بناء/قرص.صورة,if=none,format=raw,id=x0 \
    -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
    -kernel os/بناء/نواة
```

Without a disk the kernel prints `لا قرص` and cannot start `أول`. Type commands for `بوابة` in the terminal; the serial port is the keyboard and the screen.
