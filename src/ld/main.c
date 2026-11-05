/* main.c - رابط: the static linker.
 * Usage: رابط [--عاري | --برنامج] output input.o... (assumptions A29, A61)
 * --برنامج: our executable format (decision 53), system imports left for the loader
 * --عاري: a bare-metal image at 0x80000000 for qemu-system-riscv64 -bios none */
#include "../common/diag.h"
#include "../common/rt.h"
#include "ld.h"
#include "../common/obj.h"

#include <string.h>

int main(int argc, char **argv)
{
    Arena arena;
    Linker ld;
    int i, first = 1;
    int rc = 0;
    int bare = argc > 1 && strcmp(argv[1], "--عاري") == 0;
    int program = argc > 1 && strcmp(argv[1], "--برنامج") == 0;

    if (bare || program) first = 2;
    if (argc < first + 2) {
        diag_plain(D_LINK_USAGE, NULL);
        return 2;
    }
    arena_init(&arena);
    ld_init(&ld, &arena);
    if (bare) ld.base = LD_BASE_BARE;
    if (program) {
        ld.program = 1;
        ld.base = OBJ_PROGRAM_BASE;
    }
    for (i = first + 1; i < argc; i++) {
        if (ld_read_object(&ld, argv[i]) != 0) {
            rc = 1;
        }
    }
    if (rc == 0 && ld_link(&ld) != 0) {
        rc = 1;
    }
    if (rc == 0 && (program ? ld_write_program(&ld, argv[first]) : ld_write(&ld, argv[first])) != 0) {
        diag_plain(D_CANNOT_WRITE, argv[first]);
        rc = 2;
    }
    ld_free(&ld);
    arena_free(&arena);
    return rc;
}
