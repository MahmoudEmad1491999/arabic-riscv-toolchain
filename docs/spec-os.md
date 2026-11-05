# Operating system specification (Phase 5)

The operating system is written in غرة, plus the minimum of our Arabic assembly, and runs bare-metal on `qemu-system-riscv64 -machine virt -bios none -m 128M` (decision 39). Build it with `tools/build_os.sh` (user programs first, then the kernel with them embedded); `make os` boots the test kernels.

## Layers

| Layer | Mode | Files |
|---|---|---|
| Start, trap entry and exit, context switch | machine / supervisor | `os/بدء.تجميع`, `os/معالج.تجميع` |
| Machine layer: protection, delegation, timer (A65) | machine | `os/آلة.غرة` |
| Kernel: pages, Sv39 tables, traps, processes, scheduler, system calls | supervisor | `os/ذاكرة/…`, `os/مشرف/…`, `os/نواة.غرة` |
| Devices: UART, test device (power-off), CLINT timer | — | `os/جهاز/…` |
| User library: `نظام` (system services), `طباعة` | user | `os/مستخدم/…` |
| Programs (embedded in the kernel image, decision 46) | user | `os/برامج/…` |

## Processes (decisions 44, 47; A71)

- At most 8 processes. A process has a number (starting at 1), its own Sv39 page table (the kernel mapped without the user bit, its own pages with it) and its own 8 KiB kernel stack.
- States: `حرة` (free slot), `جاهزة` (ready), `تعمل` (running), `تنتظر` (waiting for a child), `منتهية` (finished, not yet collected).
- `شغل` starts an embedded program in a new process; there is no `fork` (decision 44). The kernel starts the program `أول` at boot and powers off when no process is left.
- Scheduling is round-robin; every timer tick (10 ms) taken in user mode makes the process give up the processor (decision 47). The kernel itself is not preempted.
- An exception in a user process (page fault, illegal instruction …) prints its cause and ends that process with code 102; an exception in the kernel stops the system with code 102.

## User address space (A71)

| Range | Contents |
|---|---|
| from `0x10000` | the program (code, read-only data; then data and `.bss` from the next page), as linked by `رابط` |
| up to `0x40000000` | the stack: 4 pages ending at `0x40000000`; `مؤشر_المكدس` starts at `0x40000000` |
| from `0x40000000` | nothing for the user; the kernel is mapped at its physical addresses (from `0x80000000`) without the user bit |

## The system interface `نظام` (decision 43)

Programs import the module `نظام`. Stage 4 binds its functions at link time to `ecall` stubs (`os/مستخدم/runtime/تشغيل.تجميع`); the loader of stage 6 will bind them at load time. The call number is in `وسيط٧`, the arguments in `وسيط٠` …, the result in `وسيط٠` (and `وسيط١`).

| Number | `نظام.` | Arguments | Result |
|---|---|---|---|
| ١ | `اكتب` | the bytes (`][ثابت طبيعي٨`: address, length) | number of bytes written, or all ones if the bytes are not readable by the process |
| ٢ | `اخرج` | exit code | does not return |
| ٣ | `انتظر` | — | `نتيجة_انتظار}رقم، رمز{` of a finished child; number ٠ when the process has no children |
| ٤ | `تخل` | — | — (gives up the processor) |
| ٥ | `رقمي` | — | the process number |
| ٦ | `شغل` | program name (`][ثابت طبيعي٨`) | the new process number, or ٠ |

Language stops in a program (index out of range, overflow, failed conversion) print a message and end the program with code 101.
