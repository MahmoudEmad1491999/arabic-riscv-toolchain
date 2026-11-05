/* main.c - مترجم: the غرة compiler.
 * Usage: مترجم رئيسي.غرة خرج.تجميع [مسار_بحث ...]
 * Follows the imports from the root module, checks the whole program and
 * writes one Arabic assembly file (assumption A37). */
#include "cc.h"

#include "../common/chars.h"
#include "../common/diag.h"
#include "../common/rt.h"
#include "../common/utf8.h"

#include <string.h>

typedef struct {
    Arena *arena;
    const char **dirs;
    size_t ndirs;
    Buf modules;          /* Module*, dependencies first */
    int failed;
} Loader;

void cc_set_file(const char *path)
{
    diag_set_file(path);
}

static char *join_path(Arena *a, const char *dir, const uint8_t *dotted, size_t len)
{
    size_t dl = strlen(dir), i, n = 0;
    char *p = arena_alloc(a, dl + len + 16);
    memcpy(p, dir, dl);
    n = dl;
    if (n > 0 && p[n - 1] != '/') p[n++] = '/';
    for (i = 0; i < len; i++) p[n++] = dotted[i] == '.' ? '/' : (char)dotted[i];
    memcpy(p + n, ".غرة", strlen(".غرة"));
    n += strlen(".غرة");
    p[n] = 0;
    return p;
}

static Module *find_loaded(Loader *l, const uint8_t *name, size_t len)
{
    Module **ms = (Module **)l->modules.data;
    size_t n = l->modules.len / sizeof *ms, i;
    for (i = 0; i < n; i++) {
        if (bytes_eq(ms[i]->name, ms[i]->name_len, name, len)) return ms[i];
    }
    return NULL;
}

static Module *visiting_head;

/* A module name must be usable as an identifier (it becomes part of the
 * assembly symbol names): Arabic letters, _, digits (not first). */
static int valid_module_name(const uint8_t *s, size_t len)
{
    size_t pos = 0;
    int first = 1;
    while (pos < len) {
        uint32_t cp;
        if (utf8_decode(s, len, &pos, &cp) != 0) return 0;
        if (!(ch_is_letter(cp) || cp == '_' || (!first && ch_digit_value(cp) >= 0))) return 0;
        first = 0;
    }
    return len > 0;
}

static int parse_file(Loader *l, Module *m, RtBuf *src)
{
    uint32_t *cps;
    size_t count, bad, ntoks;
    Tok *toks;
    cc_set_file(m->path);
    if (utf8_decode_all(src->data, src->len, l->arena, &cps, &count, &bad) != 0) {
        diag_error(1, 1, D_BAD_UTF8, NULL, 0);
        return -1;
    }
    if (cc_lex(cps, count, l->arena, &toks, &ntoks) != 0) return -1;
    return parse_module(l->arena, m, toks, ntoks);
}

static Module *load(Loader *l, const uint8_t *name, size_t len, const char *path, const Tok *from);

static int load_imports(Loader *l, Module *m)
{
    size_t i;
    int rc = 0;
    for (i = 0; i < m->nimports; i++) {
        Import *im = &m->imports[i];
        size_t j;
        for (j = 0; j < i; j++) {
            if (bytes_eq(m->imports[j].alias, m->imports[j].alias_len, im->alias, im->alias_len)) {
                cc_set_file(m->path);
                diag_error(im->tok.line, im->tok.col, D_CC_REDEFINED, im->alias, im->alias_len);
                rc = -1;
            }
        }
        im->module = load(l, im->path, im->path_len, NULL, &im->tok);
        if (im->module == NULL) {
            rc = -1;
        }
        cc_set_file(m->path);
    }
    return rc;
}

static Module *load(Loader *l, const uint8_t *name, size_t len, const char *path, const Tok *from)
{
    Module *m = find_loaded(l, name, len);
    RtBuf src;
    size_t i;
    (void)visiting_head;
    if (m != NULL) return m;
    m = arena_alloc(l->arena, sizeof *m);
    m->name = name;
    m->name_len = len;
    if (path == NULL) {
        for (i = 0; i < l->ndirs && path == NULL; i++) {
            char *candidate = join_path(l->arena, l->dirs[i], name, len);
            if (rt_read_file(candidate, &src) == 0) {
                path = candidate;
            }
        }
        if (path == NULL) {
            diag_error(from->line, from->col, D_CC_MODULE_NOT_FOUND, name, len);
            return NULL;
        }
    } else if (rt_read_file(path, &src) != 0) {
        diag_plain(D_CANNOT_READ, path);
        return NULL;
    }
    m->path = path;
    if (parse_file(l, m, &src) != 0) {
        rt_free(src.data);
        return NULL;
    }
    rt_free(src.data);
    /* cycle detection: a module being loaded must not be imported again */
    m->visiting = 1;
    buf_bytes(&l->modules, &m, sizeof m); /* registered so cycles find it */
    for (i = 0; i < m->nimports; i++) {
        Module *dep = find_loaded(l, m->imports[i].path, m->imports[i].path_len);
        if (dep != NULL && dep->visiting) {
            cc_set_file(m->path);
            diag_error(m->imports[i].tok.line, m->imports[i].tok.col, D_CC_IMPORT_CYCLE,
                       m->imports[i].path, m->imports[i].path_len);
            return NULL;
        }
    }
    if (load_imports(l, m) != 0) {
        m->visiting = 0;
        return NULL;
    }
    m->visiting = 0;
    m->loaded = 1;
    /* move to the end: dependencies come first */
    {
        Module **ms = (Module **)l->modules.data;
        size_t n = l->modules.len / sizeof *ms, k;
        for (k = 0; k < n; k++) {
            if (ms[k] == m) {
                memmove(&ms[k], &ms[k + 1], (n - k - 1) * sizeof *ms);
                ms[n - 1] = m;
                break;
            }
        }
    }
    return m;
}

int main(int argc, char **argv)
{
    Arena arena;
    Loader l;
    Program prog;
    Buf out;
    const char *input, *slash;
    char *rootdir;
    uint8_t *stem;
    size_t stem_len, i;
    int rc = 0;

    if (argc < 3) {
        diag_plain(D_CC_USAGE, NULL);
        return 2;
    }
    input = argv[1];
    arena_init(&arena);
    types_init(&arena);
    memset(&l, 0, sizeof l);
    l.arena = &arena;
    buf_init(&l.modules);

    /* search order: the root module's directory, then the given paths */
    slash = strrchr(input, '/');
    rootdir = arena_alloc(&arena, strlen(input) + 2);
    if (slash) {
        memcpy(rootdir, input, (size_t)(slash - input));
        rootdir[slash - input] = 0;
    } else {
        rootdir[0] = '.';
        rootdir[1] = 0;
    }
    l.ndirs = (size_t)(argc - 3) + 1;
    l.dirs = arena_alloc(&arena, l.ndirs * sizeof *l.dirs);
    l.dirs[0] = rootdir;
    for (i = 3; i < (size_t)argc; i++) l.dirs[i - 2] = argv[i];

    /* the root module is named after its file */
    {
        const char *base = slash ? slash + 1 : input;
        const char *ext = strstr(base, ".غرة");
        stem_len = ext ? (size_t)(ext - base) : strlen(base);
        stem = arena_alloc(&arena, stem_len + 1);
        memcpy(stem, base, stem_len);
    }

    if (!valid_module_name(stem, stem_len)) {
        diag_plain(D_CC_BAD_MODULE_NAME, input);
        arena_free(&arena);
        return 1;
    }

    memset(&prog, 0, sizeof prog);
    prog.arena = &arena;
    buf_init(&prog.strings);
    prog.root = load(&l, stem, stem_len, input, NULL);
    if (prog.root == NULL || diag_count() != 0) {
        rc = 1;
    } else {
        prog.modules = (Module **)l.modules.data;
        prog.nmodules = l.modules.len / sizeof(Module *);
        if (check_program(&prog) != 0 || diag_count() != 0) {
            rc = 1;
        } else {
            buf_init(&out);
            gen_program(&prog, &out);
            if (rt_write_file(argv[2], out.data, out.len) != 0) {
                diag_plain(D_CANNOT_WRITE, argv[2]);
                rc = 2;
            }
            buf_free(&out);
        }
    }
    buf_free(&prog.strings);
    buf_free(&l.modules);
    arena_free(&arena);
    return rc;
}
