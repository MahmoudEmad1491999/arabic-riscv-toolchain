/* main.c - رابط: the static linker.
 * Usage: رابط [--عاري] output input.o... (assumptions A29, A61)
 * --عاري: a bare-metal image at 0x80000000 for qemu-system-riscv64 -bios none */
#include "../common/diag.h"
#include "../common/rt.h"
#include "ld.h"

#include <string.h>

int main(int argc, char **argv)
{
    Arena arena;
    Linker ld;
    int i, first = 1;
    int rc = 0;
    int bare = argc > 1 && strcmp(argv[1], "--عاري") == 0;

    if (bare) first = 2;
    if (argc < first + 2) {
        diag_plain(D_LINK_USAGE, NULL);
        return 2;
    }
    arena_init(&arena);
    ld_init(&ld, &arena);
    if (bare) ld.base = LD_BASE_BARE;
    for (i = first + 1; i < argc; i++) {
        if (ld_read_object(&ld, argv[i]) != 0) {
            rc = 1;
        }
    }
    if (rc == 0 && ld_link(&ld) != 0) {
        rc = 1;
    }
    if (rc == 0 && ld_write(&ld, argv[first]) != 0) {
        diag_plain(D_CANNOT_WRITE, argv[first]);
        rc = 2;
    }
    ld_free(&ld);
    arena_free(&arena);
    return rc;
}
