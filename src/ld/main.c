/* main.c - رابط: the static linker.
 * Usage: رابط output input.o... (assumption A29) */
#include "../common/diag.h"
#include "../common/rt.h"
#include "ld.h"

int main(int argc, char **argv)
{
    Arena arena;
    Linker ld;
    int i;
    int rc = 0;

    if (argc < 3) {
        diag_plain(D_LINK_USAGE, NULL);
        return 2;
    }
    arena_init(&arena);
    ld_init(&ld, &arena);
    for (i = 2; i < argc; i++) {
        if (ld_read_object(&ld, argv[i]) != 0) {
            rc = 1;
        }
    }
    if (rc == 0 && ld_link(&ld) != 0) {
        rc = 1;
    }
    if (rc == 0 && ld_write(&ld, argv[1]) != 0) {
        diag_plain(D_CANNOT_WRITE, argv[1]);
        rc = 2;
    }
    ld_free(&ld);
    arena_free(&arena);
    return rc;
}
