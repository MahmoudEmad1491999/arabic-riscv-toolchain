/* tools.c - the assembler and the linker as functions of مترجم.
 * Kept apart from the compiler's headers: both sides have their own lexer. */
#include "tools.h"

#include "../as/as.h"
#include "../as/lexer.h"
#include "../common/diag.h"
#include "../common/rt.h"
#include "../common/utf8.h"
#include "../ld/ld.h"

int tool_assemble(const uint8_t *text, size_t len, const char *name, const char *out,
                  const uint8_t *iface, size_t iface_len)
{
    Arena arena;
    uint32_t *cps;
    size_t count, bad;
    TokenList tl;
    Assembler as;
    int rc = 0;

    diag_set_file(name);
    arena_init(&arena);
    if (utf8_decode_all(text, len, &arena, &cps, &count, &bad) != 0) {
        diag_plain(D_BAD_UTF8, name);
        arena_free(&arena);
        return -1;
    }
    lex(cps, count, &arena, &tl);
    if (diag_count() != 0) {
        arena_free(&arena);
        return -1;
    }
    as_init(&as, &arena, &tl);
    as_set_source(&as, name);
    if (as_assemble(&as) != 0) {
        rc = -1;
    } else if (as_write_obj(&as, out, iface, iface_len) != 0) {
        diag_plain(D_CANNOT_WRITE, out);
        rc = -1;
    }
    as_free(&as);
    arena_free(&arena);
    return rc;
}

int tool_link(const char *out, const char *const *inputs, size_t n, int bare)
{
    Arena arena;
    Linker ld;
    size_t i;
    int rc = 0;

    arena_init(&arena);
    ld_init(&ld, &arena);
    if (bare) ld.base = LD_BASE_BARE;
    for (i = 0; i < n; i++) {
        if (ld_read_object(&ld, inputs[i]) != 0) {
            rc = -1;
        }
    }
    if (rc == 0 && ld_link(&ld) != 0) {
        rc = -1;
    }
    if (rc == 0 && ld_write(&ld, out) != 0) {
        diag_plain(D_CANNOT_WRITE, out);
        rc = -1;
    }
    ld_free(&ld);
    arena_free(&arena);
    return rc;
}
