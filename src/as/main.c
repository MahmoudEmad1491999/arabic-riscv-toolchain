/* main.c - مجمع: the Arabic RISC-V assembler.
 * Usage: مجمع input.تجميع output.o */
#include <string.h>

#include "../common/diag.h"
#include "../common/rt.h"
#include "../common/utf8.h"
#include "as.h"
#include "lexer.h"

static uint32_t line_of_offset(const RtBuf *src, size_t offset)
{
    uint32_t line = 1;
    size_t i;
    for (i = 0; i < offset && i < src->len; i++) {
        if (src->data[i] == '\n') {
            line++;
        }
    }
    return line;
}

/* A name ending in .كائن selects our object format (A53). */
static int wants_obj_format(const char *path)
{
    static const char ext[] = ".\xD9\x83\xD8\xA7\xD8\xA6\xD9\x86";
    size_t n = strlen(path), e = sizeof ext - 1;
    return n >= e && memcmp(path + n - e, ext, e) == 0;
}

static int finish(int code, RtBuf *src, Arena *arena, Assembler *as)
{
    if (as != NULL) {
        as_free(as);
    }
    arena_free(arena);
    rt_free(src->data);
    return code;
}

int main(int argc, char **argv)
{
    RtBuf src;
    Arena arena;
    uint32_t *cps;
    size_t count, bad;
    TokenList tl;
    Assembler as;

    if (argc != 3) {
        diag_plain(D_USAGE, NULL);
        return 2;
    }
    diag_set_file(argv[1]);
    if (rt_read_file(argv[1], &src) != 0) {
        diag_plain(D_CANNOT_READ, argv[1]);
        return 2;
    }
    arena_init(&arena);
    if (utf8_decode_all(src.data, src.len, &arena, &cps, &count, &bad) != 0) {
        diag_error(line_of_offset(&src, bad), 1, D_BAD_UTF8, NULL, 0);
        return finish(1, &src, &arena, NULL);
    }
    lex(cps, count, &arena, &tl);
    if (diag_count() != 0) {
        return finish(1, &src, &arena, NULL);
    }
    as_init(&as, &arena, &tl);
    if (as_assemble(&as) != 0) {
        return finish(1, &src, &arena, &as);
    }
    if ((wants_obj_format(argv[2]) ? as_write_obj(&as, argv[2], NULL, 0)
                                    : as_write_elf(&as, argv[2])) != 0) {
        diag_plain(D_CANNOT_WRITE, argv[2]);
        return finish(2, &src, &arena, &as);
    }
    return finish(0, &src, &arena, &as);
}
