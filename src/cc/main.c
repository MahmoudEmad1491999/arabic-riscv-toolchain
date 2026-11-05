/* main.c - مترجم: the غرة compiler.
 * Usage:
 *   مترجم رئيسي.غرة [مسار_بحث | ملف.o | ملف.كائن | ملف.مكتبة ...]
 *     build: one .كائن per module in بناء/ (only what is out of date), then
 *     link بناء/رئيسي (decision 37).
 *   مترجم رئيسي.غرة خرج.تجميع [مسار_بحث ...]
 *     one Arabic assembly file for the whole program (assumption A37). */
#include "cc.h"

#include "../common/chars.h"
#include "../common/diag.h"
#include "../common/obj.h"
#include "../common/rt.h"
#include "../common/utf8.h"

#include <string.h>

#include "tools.h"

typedef struct {
    Arena *arena;
    const char **dirs;
    size_t ndirs;
    Buf modules;          /* Module*, dependencies first */
    int failed;
    ObjLib *libs;         /* libraries (.مكتبة) given on the command line */
    const char **lib_paths;
    RtBuf *lib_files;
    size_t nlibs;
} Loader;

void cc_set_file(const char *path)
{
    diag_set_file(path);
}

/* dir/a/b<ext> for the module a.b */
static char *join_path_ext(Arena *a, const char *dir, const uint8_t *dotted, size_t len,
                           const char *ext)
{
    size_t dl = strlen(dir), el = strlen(ext), i, n = 0;
    char *p = arena_alloc(a, dl + len + el + 2);
    memcpy(p, dir, dl);
    n = dl;
    if (n > 0 && p[n - 1] != '/') p[n++] = '/';
    for (i = 0; i < len; i++) p[n++] = dotted[i] == '.' ? '/' : (char)dotted[i];
    memcpy(p + n, ext, el);
    n += el;
    p[n] = 0;
    return p;
}

static char *join_path(Arena *a, const char *dir, const uint8_t *dotted, size_t len)
{
    return join_path_ext(a, dir, dotted, len, ".غرة");
}

static int ends_with(const char *s, const char *suffix)
{
    size_t n = strlen(s), k = strlen(suffix);
    return n >= k && memcmp(s + n - k, suffix, k) == 0;
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
static int load_imports(Loader *l, Module *m);

/* move to the end: dependencies come first */
static void move_to_end(Loader *l, Module *m)
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

/* Parses an interface text (decision 36) as a module without bodies. */
static int parse_interface(Loader *l, Module *m, const ObjIface *f)
{
    RtBuf text;
    text.data = (uint8_t *)f->text;
    text.len = f->text_len;
    m->interface_only = 1;
    m->iface_hash = f->hash;
    return parse_file(l, m, &text);
}

/* Interface of module m from an object image; 1 if it is that module. */
static int iface_of(const uint8_t *data, size_t len, const Module *m, ObjIface *f)
{
    ObjView v;
    return obj_parse(data, len, &v) == 0 && obj_iface(&v, f) == 0 &&
           bytes_eq(f->module, f->module_len, m->name, m->name_len);
}

/* Looks for the module as dir/a/b.كائن, then in the given libraries.
 * Returns 1 if found and parsed, 0 if not found, -1 on error. */
static int load_compiled(Loader *l, Module *m, const Tok *from)
{
    size_t i;
    for (i = 0; i < l->ndirs; i++) {
        char *candidate = join_path_ext(l->arena, l->dirs[i], m->name, m->name_len, ".كائن");
        RtBuf file;
        ObjIface f;
        if (rt_read_file(candidate, &file) != 0) continue;
        if (!iface_of(file.data, file.len, m, &f)) {
            diag_plain2(D_LINK_BAD_OBJECT, candidate, NULL);
            rt_free(file.data);
            return -1;
        }
        m->path = candidate;
        m->obj_path = candidate;
        if (parse_interface(l, m, &f) != 0) {
            rt_free(file.data);
            return -1;
        }
        rt_free(file.data);
        return 1;
    }
    for (i = 0; i < l->nlibs; i++) {
        ObjLibMember mem;
        ObjIface f;
        uint32_t k;
        for (k = 0; obj_lib_member(&l->libs[i], k, &mem) == 0; k++) {
            if (iface_of(mem.data, mem.len, m, &f)) {
                m->path = l->lib_paths[i];
                m->obj_path = l->lib_paths[i];
                return parse_interface(l, m, &f) == 0 ? 1 : -1;
            }
        }
    }
    (void)from;
    return 0;
}

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
            /* a compiled module: its object carries the interface */
            int found = load_compiled(l, m, from);
            if (found < 0) return NULL;
            if (found > 0) {
                m->visiting = 1;
                buf_bytes(&l->modules, &m, sizeof m);
                if (load_imports(l, m) != 0) return NULL;
                m->visiting = 0;
                m->loaded = 1;
                move_to_end(l, m);
                return m;
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
    m->source_hash = obj_fnv1a(src.data, src.len);
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
    move_to_end(l, m);
    return m;
}

/* ------------------------------------------------------------------ */
/* Build mode (decision 37)                                            */

static char *concat(Arena *a, const char *x, const char *y)
{
    size_t nx = strlen(x), ny = strlen(y);
    char *p = arena_alloc(a, nx + ny + 1);
    memcpy(p, x, nx);
    memcpy(p + nx, y, ny + 1);
    return p;
}

/* Creates the directories above a file path. */
static int make_parent(Arena *a, const char *file)
{
    const char *slash = strrchr(file, '/');
    char *dir;
    if (slash == NULL) return 0;
    dir = arena_alloc(a, (size_t)(slash - file) + 1);
    memcpy(dir, file, (size_t)(slash - file));
    dir[slash - file] = 0;
    return rt_make_dirs(dir);
}

/* The interface region this module's object must carry now. */
static int encode_iface(Arena *a, Module *m, Buf *region)
{
    Buf text;
    const uint8_t **names = arena_alloc(a, (m->nimports + 1) * sizeof *names);
    size_t *lens = arena_alloc(a, (m->nimports + 1) * sizeof *lens);
    uint64_t *hashes = arena_alloc(a, (m->nimports + 1) * sizeof *hashes);
    size_t i;
    buf_init(&text);
    if (iface_text(m, &text) != 0) {
        cc_set_file(m->path);
        diag_error(1, 1, D_CC_IFACE, m->name, m->name_len);
        buf_free(&text);
        return -1;
    }
    m->iface_hash = obj_fnv1a(text.data, text.len);
    for (i = 0; i < m->nimports; i++) {
        names[i] = m->imports[i].module->name;
        lens[i] = m->imports[i].module->name_len;
        hashes[i] = m->imports[i].module->iface_hash;
    }
    obj_iface_encode(m->name, m->name_len, m->source_hash, m->nimports, names, lens, hashes,
                     text.data, text.len, region);
    buf_free(&text);
    return 0;
}

/* Up to date: the object exists and carries exactly this interface region
 * (same source hash, same hashes of the imported interfaces). */
static int up_to_date(const char *path, const Buf *region)
{
    RtBuf file;
    ObjView v;
    int same;
    if (rt_read_file(path, &file) != 0) return 0;
    same = obj_parse(file.data, file.len, &v) == 0 && v.iface_size == region->len &&
           memcmp(file.data + v.iface_off, region->data, region->len) == 0;
    rt_free(file.data);
    return same;
}

static int build(Arena *a, Program *prog, const char *rootdir, const uint8_t *stem,
                 size_t stem_len, const char **dirs, size_t ndirs, const char **extras,
                 size_t nextras)
{
    const char *out_dir = rootdir[0] == '.' && rootdir[1] == 0 ? "بناء" : concat(a, rootdir, "/بناء");
    Buf inputs;
    size_t i;
    int rc = 0;
    char *exe;

    buf_init(&inputs);
    for (i = 0; i < prog->nmodules && rc == 0; i++) {
        Module *m = prog->modules[i];
        Buf region, text;
        char *obj;
        if (m->interface_only) {
            buf_bytes(&inputs, &m->obj_path, sizeof m->obj_path);
            continue;
        }
        obj = join_path_ext(a, out_dir, m->name, m->name_len, ".كائن");
        buf_bytes(&inputs, &obj, sizeof obj);
        buf_init(&region);
        if (encode_iface(a, m, &region) != 0) {
            rc = 1;
        } else if (!up_to_date(obj, &region)) {
            char *asm_name = join_path_ext(a, out_dir, m->name, m->name_len, ".تجميع");
            buf_init(&text);
            prog->only = m;
            gen_program(prog, &text);
            if (make_parent(a, obj) != 0) {
                diag_plain(D_CANNOT_WRITE, obj);
                rc = 2;
            } else if (tool_assemble(text.data, text.len, asm_name, obj, region.data, region.len) != 0) {
                rc = 1;
            }
            buf_free(&text);
        }
        buf_free(&region);
    }
    /* the start file: lib/runtime/تشغيل.تجميع from a search path */
    for (i = 1; i < ndirs && rc == 0; i++) {
        char *rt_src = concat(a, dirs[i], "/runtime/تشغيل.تجميع");
        RtBuf src;
        if (rt_read_file(rt_src, &src) == 0) {
            char *obj = concat(a, out_dir, "/تشغيل.كائن");
            if (rt_make_dirs(out_dir) != 0 ||
                tool_assemble(src.data, src.len, rt_src, obj, NULL, 0) != 0) {
                rc = 1;
            }
            rt_free(src.data);
            buf_bytes(&inputs, &obj, sizeof obj);
            break;
        }
    }
    for (i = 0; i < nextras; i++) {
        buf_bytes(&inputs, &extras[i], sizeof extras[i]);
    }
    if (rc == 0) {
        char *name = arena_alloc(a, stem_len + 2);
        name[0] = '/';
        memcpy(name + 1, stem, stem_len);
        name[stem_len + 1] = 0;
        exe = concat(a, out_dir, name);
        if (tool_link(exe, (const char *const *)inputs.data, inputs.len / sizeof(char *)) != 0) {
            rc = 1;
        }
    }
    buf_free(&inputs);
    return rc;
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
    int build_mode;
    int first_path;
    const char **extras;
    size_t nextras = 0;

    if (argc < 2) {
        diag_plain(D_CC_USAGE, NULL);
        return 2;
    }
    input = argv[1];
    build_mode = argc < 3 || !ends_with(argv[2], ".تجميع");
    first_path = build_mode ? 2 : 3;
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
    l.dirs = arena_alloc(&arena, ((size_t)argc + 1) * sizeof *l.dirs);
    l.libs = arena_alloc(&arena, ((size_t)argc + 1) * sizeof *l.libs);
    l.lib_paths = arena_alloc(&arena, ((size_t)argc + 1) * sizeof *l.lib_paths);
    l.lib_files = arena_alloc(&arena, ((size_t)argc + 1) * sizeof *l.lib_files);
    extras = arena_alloc(&arena, ((size_t)argc + 1) * sizeof *extras);
    l.dirs[l.ndirs++] = rootdir;
    for (i = (size_t)first_path; i < (size_t)argc; i++) {
        const char *arg = argv[i];
        if (ends_with(arg, ".مكتبة")) {
            RtBuf *f = &l.lib_files[l.nlibs];
            if (rt_read_file(arg, f) != 0) {
                diag_plain(D_CANNOT_READ, arg);
                rc = 2;
            } else if (obj_lib_parse(f->data, f->len, &l.libs[l.nlibs]) != 0) {
                diag_plain2(D_LINK_BAD_OBJECT, arg, NULL);
                rt_free(f->data);
                rc = 1;
            } else {
                l.lib_paths[l.nlibs++] = arg;
            }
        } else if (ends_with(arg, ".o") || ends_with(arg, ".كائن")) {
            extras[nextras++] = arg;
        } else {
            l.dirs[l.ndirs++] = arg;
        }
    }

    /* the root module is named after its file */
    {
        const char *base = slash ? slash + 1 : input;
        const char *ext = strstr(base, ".غرة");
        stem_len = ext ? (size_t)(ext - base) : strlen(base);
        stem = arena_alloc(&arena, stem_len + 1);
        memcpy(stem, base, stem_len);
    }

    if (rc == 0 && !valid_module_name(stem, stem_len)) {
        diag_plain(D_CC_BAD_MODULE_NAME, input);
        rc = 1;
    }

    memset(&prog, 0, sizeof prog);
    prog.arena = &arena;
    buf_init(&prog.strings);
    if (rc == 0) {
        prog.root = load(&l, stem, stem_len, input, NULL);
    }
    if (rc != 0) {
        /* already reported */
    } else if (prog.root == NULL || diag_count() != 0) {
        rc = 1;
    } else {
        prog.modules = (Module **)l.modules.data;
        prog.nmodules = l.modules.len / sizeof(Module *);
        if (check_program(&prog) != 0 || diag_count() != 0) {
            rc = 1;
        } else if (build_mode) {
            rc = build(&arena, &prog, rootdir, stem, stem_len, l.dirs, l.ndirs,
                       extras, nextras);
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
    for (i = 0; i < l.nlibs; i++) {
        rt_free(l.lib_files[i].data);
    }
    buf_free(&prog.strings);
    buf_free(&l.modules);
    arena_free(&arena);
    return rc;
}
