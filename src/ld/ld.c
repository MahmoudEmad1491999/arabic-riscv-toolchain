/* ld.c - رابط: static linker for RV64 ELF relocatable objects. */
#include "ld.h"

#include "../common/diag.h"
#include "../common/elf.h"
#include "../common/obj.h"

#include <string.h>

#define R_RISCV_NONE     0
#define R_RISCV_CALL     18
#define R_RISCV_RELAX    51

static const char *const out_names[OUT_COUNT] = {".text", ".rodata", ".data", ".bss"};

/* ------------------------------------------------------------------ */
/* Little-endian access with bounds checks                             */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); }
static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void wr64(uint8_t *p, uint64_t v) { wr32(p, (uint32_t)v); wr32(p + 4, (uint32_t)(v >> 32)); }

static int in_file(const RtBuf *f, uint64_t off, uint64_t size)
{
    return off <= f->len && size <= f->len - off;
}

/* A NUL-terminated string inside [base, base+size) of the file, or NULL. */
static const char *str_at(const RtBuf *f, const InSec *strtab, uint64_t off)
{
    uint64_t i;
    if (strtab == NULL || off >= strtab->size || strtab->type == SHT_NOBITS ||
        !in_file(f, strtab->offset, strtab->size)) {
        return NULL;
    }
    for (i = strtab->offset + off; i < strtab->offset + strtab->size; i++) {
        if (f->data[i] == 0) {
            return (const char *)f->data + strtab->offset + off;
        }
    }
    return NULL;
}

static uint64_t align_up(uint64_t v, uint64_t a)
{
    return a <= 1 ? v : (v + a - 1) / a * a;
}

/* ------------------------------------------------------------------ */
/* Reading objects                                                     */

static int bad_object(const char *path)
{
    diag_plain2(D_LINK_BAD_OBJECT, path, NULL);
    return -1;
}

static int is_prefix(const char *name, const char *prefix)
{
    size_t n = strlen(prefix);
    return strncmp(name, prefix, n) == 0 && (name[n] == 0 || name[n] == '.');
}

/* Which output section an input section goes to. */
static int classify(const InSec *s)
{
    if (!(s->flags & SHF_ALLOC)) {
        return OUT_NONE;
    }
    if (is_prefix(s->name, ".text")) return OUT_TEXT;
    if (is_prefix(s->name, ".rodata") || is_prefix(s->name, ".srodata")) return OUT_RODATA;
    if (is_prefix(s->name, ".data") || is_prefix(s->name, ".sdata")) return OUT_DATA;
    if (is_prefix(s->name, ".bss") || is_prefix(s->name, ".sbss")) return OUT_BSS;
    return -2;
}

void ld_init(Linker *ld, Arena *a)
{
    int k;
    memset(ld, 0, sizeof *ld);
    ld->arena = a;
    ld->base = LD_BASE;
    buf_init(&ld->libs);
    buf_init(&ld->lib_files);
    buf_init(&ld->globals);
    for (k = 0; k < OUT_COUNT; k++) {
        buf_init(&ld->out_data[k]);
        ld->out_align[k] = 1;
    }
}

void ld_free(Linker *ld)
{
    size_t i;
    int k;
    for (i = 0; i < ld->nobjs; i++) {
        rt_free(ld->objs[i].file.data);
    }
    rt_free(ld->objs);
    buf_free(&ld->globals);
    {
        RtBuf *f = (RtBuf *)ld->lib_files.data;
        size_t n = ld->lib_files.len / sizeof *f;
        for (i = 0; i < n; i++) {
            rt_free(f[i].data);
        }
        buf_free(&ld->lib_files);
        buf_free(&ld->libs);
    }
    for (k = 0; k < OUT_COUNT; k++) {
        buf_free(&ld->out_data[k]);
    }
}

/* Our own format (.كائن) is turned into the same in-memory shape an ELF
 * object has, so one relocation engine serves both (assumption A54):
 * section indices 1..4 are text, data, rodata, bss; one RELA-like section
 * per section with relocations; every PC-relative low relocation gets a
 * hidden label at the offset of its high instruction. */
static int read_native(Linker *ld, InObj *o)
{
    ObjView v;
    static const char *const names[OBJ_NSEC] = {".text", ".data", ".rodata", ".bss"};
    static const int outs[OBJ_NSEC] = {OUT_TEXT, OUT_DATA, OUT_RODATA, OUT_BSS};
    static const uint32_t rtypes[OBJ_REL_LAST + 1] = {
        0, R_RISCV_BRANCH, R_RISCV_JAL, R_RISCV_CALL_PLT, R_RISCV_HI20, R_RISCV_LO12_I,
        R_RISCV_LO12_S, R_RISCV_PCREL_HI20, R_RISCV_PCREL_LO12_I, R_RISCV_PCREL_LO12_S,
        R_RISCV_32, R_RISCV_64};
    size_t nlo = 0, hidden, i;
    uint32_t sec_relocs[OBJ_NSEC] = {0, 0, 0, 0};
    uint32_t rela_first[OBJ_NSEC];
    uint8_t *img;
    size_t base, nrela_secs = 0;
    int k;

    if (obj_parse(o->file.data, o->file.len, &v) != 0) {
        return bad_object(o->path);
    }
    for (i = 0; i < v.nrelocs; i++) {
        ObjReloc r;
        obj_reloc(&v, (uint32_t)i, &r);
        sec_relocs[r.section]++;
        if (r.kind == OBJ_REL_PCREL_LO_I || r.kind == OBJ_REL_PCREL_LO_S) {
            if ((uint64_t)r.extra >= v.size[r.section]) {
                return bad_object(o->path);
            }
            nlo++;
        }
    }
    for (i = 0; i < v.nsyms; i++) {
        ObjSym y;
        obj_sym(&v, (uint32_t)i, &y);
        if (memchr(y.name, 0, y.len) != NULL) {
            return bad_object(o->path);
        }
    }
    /* image = original file + RELA entries grouped by section */
    base = (o->file.len + 7u) & ~(size_t)7u;
    img = rt_alloc(base + (size_t)v.nrelocs * ELF_RELA_SIZE + 8);
    memcpy(img, o->file.data, o->file.len);
    memset(img + o->file.len, 0, base - o->file.len);
    for (k = 0; k < OBJ_NSEC; k++) {
        if (sec_relocs[k] != 0) {
            nrela_secs++;
        }
    }
    o->shnum = (uint16_t)(1 + OBJ_NSEC + nrela_secs);
    o->secs = arena_alloc(ld->arena, o->shnum * sizeof *o->secs);
    memset(o->secs, 0, o->shnum * sizeof *o->secs);
    for (k = 0; k < OBJ_NSEC; k++) {
        InSec *s = &o->secs[k + 1];
        s->name = names[k];
        s->type = k == OBJ_SEC_BSS ? SHT_NOBITS : SHT_PROGBITS;
        s->flags = SHF_ALLOC;
        s->offset = v.content_off[k];
        s->size = v.size[k];
        s->align = v.align[k];
        s->out = outs[k];
    }
    {
        uint32_t at = 0;
        size_t sec_i = 1 + OBJ_NSEC;
        for (k = 0; k < OBJ_NSEC; k++) {
            rela_first[k] = at;
            if (sec_relocs[k] != 0) {
                InSec *rs = &o->secs[sec_i++];
                rs->name = ".rela";
                rs->type = SHT_RELA;
                rs->out = OUT_NONE;
                rs->info = (uint32_t)k + 1;
                rs->offset = base + (uint64_t)at * ELF_RELA_SIZE;
                rs->size = (uint64_t)sec_relocs[k] * ELF_RELA_SIZE;
                at += sec_relocs[k];
            }
        }
    }
    /* symbols: 0 = null, then the file's symbols, then the hidden labels */
    hidden = 1 + v.nsyms;
    o->nsyms = hidden + nlo;
    o->syms = arena_alloc(ld->arena, (o->nsyms + 1) * sizeof *o->syms);
    memset(o->syms, 0, (o->nsyms + 1) * sizeof *o->syms);
    o->syms[0].name = "";
    for (i = 0; i < v.nsyms; i++) {
        ObjSym y;
        InSym *x = &o->syms[i + 1];
        char *name;
        obj_sym(&v, (uint32_t)i, &y);
        name = arena_alloc(ld->arena, (size_t)y.len + 1);
        memcpy(name, y.name, y.len);
        name[y.len] = 0;
        x->name = name;
        x->bind = y.kind == OBJ_PRIVATE ? STB_LOCAL : STB_GLOBAL;
        x->type = STT_NOTYPE;
        x->value = y.value;
        x->shndx = y.section == OBJ_SEC_UNDEF ? SHN_UNDEF
                 : y.section == OBJ_SEC_ABS ? (uint16_t)SHN_ABS
                 : (uint16_t)(y.section + 1);
    }
    {
        uint32_t fill[OBJ_NSEC];
        size_t next_hidden = hidden;
        for (k = 0; k < OBJ_NSEC; k++) {
            fill[k] = rela_first[k];
        }
        for (i = 0; i < v.nrelocs; i++) {
            ObjReloc r;
            uint8_t *e;
            uint64_t info;
            uint32_t symidx;
            obj_reloc(&v, (uint32_t)i, &r);
            if (r.kind == OBJ_REL_PCREL_LO_I || r.kind == OBJ_REL_PCREL_LO_S) {
                InSym *h = &o->syms[next_hidden];
                h->name = ".L0 ";
                h->bind = STB_LOCAL;
                h->type = STT_NOTYPE;
                h->shndx = (uint16_t)(r.section + 1);
                h->value = (uint64_t)r.extra;
                symidx = (uint32_t)next_hidden++;
                r.extra = 0;
            } else {
                symidx = r.sym + 1;
            }
            e = img + base + (size_t)fill[r.section]++ * ELF_RELA_SIZE;
            info = ((uint64_t)symidx << 32) | rtypes[r.kind];
            wr64(e, r.offset);
            wr64(e + 8, info);
            wr64(e + 16, (uint64_t)r.extra);
        }
    }
    if (v.iface_size != 0) {
        o->has_iface = obj_iface_decode(img + v.iface_off, (size_t)v.iface_size, &o->iface) == 0;
    }
    rt_free(o->file.data);
    o->file.data = img;
    o->file.len = base + (size_t)v.nrelocs * ELF_RELA_SIZE;
    return 0;
}

int ld_read_object(Linker *ld, const char *path)
{
    InObj o;
    const uint8_t *d;
    uint64_t shoff;
    uint16_t shentsize, shstrndx;
    InSec *symtab = NULL;
    size_t i;

    memset(&o, 0, sizeof o);
    o.path = path;
    if (rt_read_file(path, &o.file) != 0) {
        diag_plain2(D_CANNOT_READ, path, NULL);
        return -1;
    }
    if (obj_is_lib(o.file.data, o.file.len)) {
        ObjLib lib;
        uint32_t k;
        LibMember lm;
        if (obj_lib_parse(o.file.data, o.file.len, &lib) != 0) {
            rt_free(o.file.data);
            return bad_object(path);
        }
        buf_bytes(&ld->lib_files, &o.file, sizeof o.file);
        for (k = 0; obj_lib_member(&lib, k, &lm.mem) == 0; k++) {
            lm.lib = path;
            lm.used = 0;
            buf_bytes(&ld->libs, &lm, sizeof lm);
        }
        return 0;
    }
    if (obj_is_object(o.file.data, o.file.len)) {
        if (read_native(ld, &o) != 0) {
            rt_free(o.file.data);
            return -1;
        }
        ld->objs = rt_realloc(ld->objs, (ld->nobjs + 1) * sizeof *ld->objs);
        ld->objs[ld->nobjs++] = o;
        return 0;
    }
    d = o.file.data;
    if (o.file.len < ELF_EHDR_SIZE || memcmp(d, "\177ELF", 4) != 0 || d[4] != 2 ||
        d[5] != 1 || rd16(d + 16) != ET_REL || rd16(d + 18) != EM_RISCV) {
        rt_free(o.file.data);
        return bad_object(path);
    }
    shoff = rd64(d + 0x28);
    shentsize = rd16(d + 0x3A);
    o.shnum = rd16(d + 0x3C);
    shstrndx = rd16(d + 0x3E);
    if (shentsize != ELF_SHDR_SIZE || o.shnum == 0 || shstrndx >= o.shnum ||
        !in_file(&o.file, shoff, (uint64_t)o.shnum * ELF_SHDR_SIZE)) {
        rt_free(o.file.data);
        return bad_object(path);
    }
    o.secs = arena_alloc(ld->arena, o.shnum * sizeof *o.secs);
    for (i = 0; i < o.shnum; i++) {
        const uint8_t *h = d + shoff + i * ELF_SHDR_SIZE;
        InSec *s = &o.secs[i];
        s->type = rd32(h + 4);
        s->flags = rd64(h + 8);
        s->offset = rd64(h + 24);
        s->size = rd64(h + 32);
        s->link = rd32(h + 40);
        s->info = rd32(h + 44);
        s->align = rd64(h + 48);
        if (s->type != SHT_NOBITS && s->type != SHT_NULL && !in_file(&o.file, s->offset, s->size)) {
            rt_free(o.file.data);
            return bad_object(path);
        }
        s->name_off = rd32(h);
    }
    for (i = 0; i < o.shnum; i++) {
        InSec *s = &o.secs[i];
        const char *name = str_at(&o.file, &o.secs[shstrndx], s->name_off);
        if (name == NULL) {
            rt_free(o.file.data);
            return bad_object(path);
        }
        s->name = name;
        s->out = classify(s);
        if (s->out == -2) {
            diag_plain2(D_LINK_UNSUPPORTED_SECTION, path, name);
            rt_free(o.file.data);
            return -1;
        }
        if (s->type == SHT_SYMTAB) {
            if (symtab != NULL || s->link >= o.shnum || !in_file(&o.file, s->offset, s->size)) {
                rt_free(o.file.data);
                return bad_object(path);
            }
            symtab = s;
        }
    }
    if (symtab != NULL) {
        InSec *strtab = &o.secs[symtab->link];
        o.nsyms = symtab->size / ELF_SYM_SIZE;
        o.syms = arena_alloc(ld->arena, (o.nsyms + 1) * sizeof *o.syms);
        for (i = 0; i < o.nsyms; i++) {
            const uint8_t *e = d + symtab->offset + i * ELF_SYM_SIZE;
            InSym *y = &o.syms[i];
            y->name = str_at(&o.file, strtab, rd32(e));
            y->bind = (uint8_t)(e[4] >> 4);
            y->type = (uint8_t)(e[4] & 15);
            y->shndx = rd16(e + 6);
            y->value = rd64(e + 8);
            if (y->name == NULL ||
                (y->shndx != SHN_UNDEF && y->shndx != SHN_ABS && y->shndx >= o.shnum)) {
                rt_free(o.file.data);
                return bad_object(path);
            }
        }
    }

    ld->objs = rt_realloc(ld->objs, (ld->nobjs + 1) * sizeof *ld->objs);
    ld->objs[ld->nobjs++] = o;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Global symbols                                                      */

static uint32_t hash_str(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) {
        h = (h ^ (uint8_t)*s++) * 16777619u;
    }
    return h;
}

static GlobalSym *global_get(Linker *ld, const char *name)
{
    uint32_t b = hash_str(name) % LD_BUCKETS;
    GlobalSym *g;
    for (g = ld->buckets[b]; g != NULL; g = g->next) {
        if (strcmp(g->name, name) == 0) {
            return g;
        }
    }
    g = arena_alloc(ld->arena, sizeof *g);
    g->name = name;
    g->next = ld->buckets[b];
    ld->buckets[b] = g;
    buf_bytes(&ld->globals, &g, sizeof g);
    return g;
}

static int is_global_bind(uint8_t bind)
{
    return bind == STB_GLOBAL || bind == 2 /* STB_WEAK */;
}

static int resolve_symbols(Linker *ld)
{
    size_t i, j;
    int rc = 0;

    /* definitions */
    for (i = 0; i < ld->nobjs; i++) {
        InObj *o = &ld->objs[i];
        for (j = 1; j < o->nsyms; j++) {
            InSym *y = &o->syms[j];
            GlobalSym *g;
            int weak;
            if (!is_global_bind(y->bind)) {
                continue;
            }
            g = global_get(ld, y->name);
            y->global = g;
            if (y->shndx == SHN_UNDEF) {
                continue;
            }
            weak = y->bind != STB_GLOBAL;
            if (g->defined && !g->weak && !weak) {
                diag_plain2(D_LINK_DUPLICATE, y->name, o->path);
                rc = -1;
                continue;
            }
            if (!g->defined || (g->weak && !weak)) {
                g->defined = 1;
                g->weak = weak;
                g->obj = o;
                g->sym = j;
            }
        }
    }
    /* names the linker defines when no object does (A67) */
    {
        static const char *const special[] = {"_نهاية", "_بداية_البيانات"};
        int k;
        for (k = 0; k < 2; k++) {
            GlobalSym *g = global_get(ld, special[k]);
            if (!g->defined) {
                g->defined = 1;
                g->special = k + 1;
            }
        }
    }
    /* every strong undefined reference must now be defined */
    for (i = 0; i < ld->nobjs; i++) {
        InObj *o = &ld->objs[i];
        for (j = 1; j < o->nsyms; j++) {
            InSym *y = &o->syms[j];
            if (y->global != NULL && y->shndx == SHN_UNDEF && !y->global->defined &&
                y->bind == STB_GLOBAL) {
                diag_plain2(D_LINK_UNDEFINED, y->name, o->path);
                y->global->defined = -1; /* report each name once */
                rc = -1;
            }
        }
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* Layout                                                              */

static int layout(Linker *ld)
{
    size_t i, j;
    int k;
    uint64_t hdr, end1, fileoff;

    for (i = 0; i < ld->nobjs; i++) {
        InObj *o = &ld->objs[i];
        for (j = 0; j < o->shnum; j++) {
            InSec *s = &o->secs[j];
            uint64_t a = s->align == 0 ? 1 : s->align;
            if (s->out < 0) {
                continue;
            }
            if (a > LD_PAGE || (a & (a - 1)) != 0) {
                diag_plain2(D_LINK_ALIGN, o->path, s->name);
                return -1;
            }
            if (a > ld->out_align[s->out]) {
                ld->out_align[s->out] = a;
            }
            s->out_off = align_up(ld->out_size[s->out], a);
            ld->out_size[s->out] = s->out_off + s->size;
        }
    }

    ld->nphdr = (ld->out_size[OUT_DATA] + ld->out_size[OUT_BSS]) != 0 ? 2 : 1;
    hdr = ELF_EHDR_SIZE + (uint64_t)ld->nphdr * 56u;
    /* hosted: the headers are part of the first segment (like GNU ld).
     * bare (A61): the code starts exactly at the base address, where the
     * machine starts executing, and the headers stay outside the image. */
    ld->seg_off = ld->base == LD_BASE_BARE ? LD_PAGE : 0;
    ld->out_addr[OUT_TEXT] = ld->seg_off ? ld->base
                                         : align_up(ld->base + hdr, ld->out_align[OUT_TEXT]);
    ld->out_addr[OUT_RODATA] = align_up(ld->out_addr[OUT_TEXT] + ld->out_size[OUT_TEXT],
                                        ld->out_align[OUT_RODATA]);
    end1 = ld->out_addr[OUT_RODATA] + ld->out_size[OUT_RODATA];
    ld->out_fileoff[OUT_TEXT] = ld->out_addr[OUT_TEXT] - ld->base + ld->seg_off;
    ld->out_fileoff[OUT_RODATA] = ld->out_addr[OUT_RODATA] - ld->base + ld->seg_off;

    /* data segment: next page, with the address congruent to the file offset */
    fileoff = align_up(end1 - ld->base + ld->seg_off, ld->out_align[OUT_DATA] > ld->out_align[OUT_BSS]
                                           ? ld->out_align[OUT_DATA] : ld->out_align[OUT_BSS]);
    ld->out_fileoff[OUT_DATA] = fileoff;
    ld->out_addr[OUT_DATA] = align_up(end1, LD_PAGE) + (fileoff % LD_PAGE);
    ld->out_addr[OUT_BSS] = align_up(ld->out_addr[OUT_DATA] + ld->out_size[OUT_DATA],
                                     ld->out_align[OUT_BSS]);
    ld->out_fileoff[OUT_BSS] = ld->out_fileoff[OUT_DATA] + ld->out_size[OUT_DATA];

    /* copy contents */
    for (k = 0; k < OUT_COUNT; k++) {
        if (k != OUT_BSS) {
            buf_zeros(&ld->out_data[k], (size_t)ld->out_size[k]);
        }
    }
    for (i = 0; i < ld->nobjs; i++) {
        InObj *o = &ld->objs[i];
        for (j = 0; j < o->shnum; j++) {
            InSec *s = &o->secs[j];
            if (s->out < 0 || s->out == OUT_BSS || s->type == SHT_NOBITS || s->size == 0) {
                continue;
            }
            memcpy(ld->out_data[s->out].data + s->out_off, o->file.data + s->offset, (size_t)s->size);
        }
    }
    return 0;
}

/* Address of a symbol after layout. ok = 0 if it has no address. */
static uint64_t sym_addr(Linker *ld, InObj *o, size_t idx, int *ok)
{
    InSym *y = &o->syms[idx];
    *ok = 1;
    if (y->global != NULL) {
        GlobalSym *g = y->global;
        if (g->defined != 1) {
            return 0; /* undefined weak: address 0 */
        }
        if (g->special == 1) {
            return ld->out_addr[OUT_BSS] + ld->out_size[OUT_BSS]; /* end of the image */
        }
        if (g->special == 2) {
            return ld->out_addr[OUT_DATA]; /* first writable byte, on its own page */
        }
        o = g->obj;
        y = &o->syms[g->sym];
    }
    if (y->shndx == SHN_ABS) {
        return y->value;
    }
    if (y->shndx == SHN_UNDEF || y->shndx >= o->shnum || o->secs[y->shndx].out < 0) {
        *ok = 0;
        return 0;
    }
    return ld->out_addr[o->secs[y->shndx].out] + o->secs[y->shndx].out_off + y->value;
}

/* ------------------------------------------------------------------ */
/* Relocations                                                         */

static int64_t lo12(int64_t x)
{
    return (int64_t)(((uint64_t)x & 0xFFFu) ^ 0x800u) - 0x800;
}

static int fits_hi20(int64_t v)
{
    int64_t hi = (int64_t)((uint64_t)v + 0x800u) >> 12;
    return hi >= -(1 << 19) && hi < (1 << 19);
}

static uint32_t hi20(int64_t v)
{
    return (uint32_t)((((uint64_t)v + 0x800u) >> 12) & 0xFFFFFu);
}

static uint32_t set_u(uint32_t insn, int64_t v)
{
    return (insn & 0xFFFu) | (hi20(v) << 12);
}

static uint32_t set_i(uint32_t insn, int64_t v)
{
    return (insn & 0x000FFFFFu) | (((uint32_t)v & 0xFFFu) << 20);
}

static uint32_t set_s(uint32_t insn, int64_t v)
{
    uint32_t u = (uint32_t)v;
    return (insn & 0x01FFF07Fu) | (((u >> 5) & 0x7Fu) << 25) | ((u & 0x1Fu) << 7);
}

static uint32_t set_b(uint32_t insn, int64_t v)
{
    uint32_t u = (uint32_t)v;
    return (insn & 0x01FFF07Fu) | (((u >> 12) & 1u) << 31) | (((u >> 5) & 0x3Fu) << 25) |
           (((u >> 1) & 0xFu) << 8) | (((u >> 11) & 1u) << 7);
}

static uint32_t set_j(uint32_t insn, int64_t v)
{
    uint32_t u = (uint32_t)v;
    return (insn & 0xFFFu) | (((u >> 20) & 1u) << 31) | (((u >> 1) & 0x3FFu) << 21) |
           (((u >> 11) & 1u) << 20) | (((u >> 12) & 0xFFu) << 12);
}

typedef struct {
    uint64_t offset;
    uint32_t type;
    uint32_t sym;
    int64_t addend;
} Rela;

static Rela rela_at(const InObj *o, const InSec *rs, size_t i)
{
    const uint8_t *e = o->file.data + rs->offset + i * ELF_RELA_SIZE;
    Rela r;
    uint64_t info = rd64(e + 8);
    r.offset = rd64(e);
    r.type = (uint32_t)info;
    r.sym = (uint32_t)(info >> 32);
    r.addend = (int64_t)rd64(e + 16);
    return r;
}

static int range_error(const InObj *o, uint32_t sym)
{
    diag_plain2(D_LINK_RANGE, o->syms[sym].name[0] ? o->syms[sym].name : "?", o->path);
    return -1;
}

/* Value of the R_RISCV_PCREL_HI20 that an R_RISCV_PCREL_LO12 points at:
 * the HI20 relocation must sit at the address of the label. */
static int pcrel_hi_value(Linker *ld, InObj *o, const InSec *rs, const InSec *target,
                          uint64_t label_addr, int64_t *out)
{
    size_t n = rs->size / ELF_RELA_SIZE;
    size_t i;
    uint64_t base = ld->out_addr[target->out] + target->out_off;
    for (i = 0; i < n; i++) {
        Rela r = rela_at(o, rs, i);
        if (r.type == R_RISCV_PCREL_HI20 && base + r.offset == label_addr) {
            int ok;
            uint64_t s = sym_addr(ld, o, r.sym, &ok);
            if (!ok) {
                return -1;
            }
            *out = (int64_t)(s + (uint64_t)r.addend - label_addr);
            return 0;
        }
    }
    return -1;
}

static int apply_relocs(Linker *ld, InObj *o, InSec *rs)
{
    InSec *target;
    size_t n, i;
    int rc = 0;

    if (rs->info >= o->shnum || rs->link >= o->shnum || o->syms == NULL) {
        return bad_object(o->path);
    }
    target = &o->secs[rs->info];
    if (target->out < 0) {
        return 0; /* relocations for debug info etc. */
    }
    n = rs->size / ELF_RELA_SIZE;
    for (i = 0; i < n; i++) {
        Rela r = rela_at(o, rs, i);
        uint8_t *place;
        uint64_t p = ld->out_addr[target->out] + target->out_off + r.offset;
        uint64_t s;
        int64_t v;
        int ok;

        if (r.type == R_RISCV_RELAX || r.type == R_RISCV_NONE) {
            continue;
        }
        {
            uint64_t need = (r.type == R_RISCV_64 || r.type == R_RISCV_CALL ||
                             r.type == R_RISCV_CALL_PLT) ? 8 : 4;
            if (r.sym >= o->nsyms || target->out == OUT_BSS || r.offset > target->size ||
                target->size - r.offset < need) {
                return bad_object(o->path);
            }
        }
        place = ld->out_data[target->out].data + target->out_off + r.offset;
        s = sym_addr(ld, o, r.sym, &ok);
        if (!ok) {
            diag_plain2(D_LINK_UNDEFINED, o->syms[r.sym].name, o->path);
            rc = -1;
            continue;
        }
        v = (int64_t)(s + (uint64_t)r.addend);

        switch (r.type) {
        case R_RISCV_64:
            wr64(place, (uint64_t)v);
            break;
        case R_RISCV_32:
            if (v < INT32_MIN || v > (int64_t)UINT32_MAX) {
                rc = range_error(o, r.sym);
                break;
            }
            wr32(place, (uint32_t)v);
            break;
        case R_RISCV_BRANCH:
            v -= (int64_t)p;
            if ((v & 1) || v < -4096 || v > 4094) {
                rc = range_error(o, r.sym);
                break;
            }
            wr32(place, set_b(rd32(place), v));
            break;
        case R_RISCV_JAL:
            v -= (int64_t)p;
            if ((v & 1) || v < -(1 << 20) || v > (1 << 20) - 2) {
                rc = range_error(o, r.sym);
                break;
            }
            wr32(place, set_j(rd32(place), v));
            break;
        case R_RISCV_CALL:
        case R_RISCV_CALL_PLT:
            v -= (int64_t)p;
            if (!fits_hi20(v)) {
                rc = range_error(o, r.sym);
                break;
            }
            wr32(place, set_u(rd32(place), v));
            wr32(place + 4, set_i(rd32(place + 4), lo12(v)));
            break;
        case R_RISCV_HI20:
            if (!fits_hi20(v)) {
                rc = range_error(o, r.sym);
                break;
            }
            wr32(place, set_u(rd32(place), v));
            break;
        case R_RISCV_LO12_I:
            wr32(place, set_i(rd32(place), lo12(v)));
            break;
        case R_RISCV_LO12_S:
            wr32(place, set_s(rd32(place), lo12(v)));
            break;
        case R_RISCV_PCREL_HI20:
            v -= (int64_t)p;
            if (!fits_hi20(v)) {
                rc = range_error(o, r.sym);
                break;
            }
            wr32(place, set_u(rd32(place), v));
            break;
        case R_RISCV_PCREL_LO12_I:
        case R_RISCV_PCREL_LO12_S: {
            int64_t hv;
            if (pcrel_hi_value(ld, o, rs, target, s, &hv) != 0) {
                diag_plain2(D_LINK_PCREL_LO, o->path, NULL);
                rc = -1;
                break;
            }
            if (r.type == R_RISCV_PCREL_LO12_I) {
                wr32(place, set_i(rd32(place), lo12(hv)));
            } else {
                wr32(place, set_s(rd32(place), lo12(hv)));
            }
            break;
        }
        default:
            diag_plain2(D_LINK_UNSUPPORTED_RELOC, o->path, NULL);
            return -1;
        }
    }
    return rc;
}

static const char *cstr(Linker *ld, const uint8_t *s, size_t n)
{
    char *c = arena_alloc(ld->arena, n + 1);
    memcpy(c, s, n);
    c[n] = 0;
    return c;
}

/* Is a global name defined / referenced but undefined in the linked objects? */
static int name_state(Linker *ld, const uint8_t *name, size_t n, int *defined)
{
    size_t i, j;
    int referenced = 0;
    *defined = 0;
    for (i = 0; i < ld->nobjs; i++) {
        InObj *o = &ld->objs[i];
        for (j = 1; j < o->nsyms; j++) {
            InSym *y = &o->syms[j];
            if (!is_global_bind(y->bind) || strlen(y->name) != n || memcmp(y->name, name, n) != 0) {
                continue;
            }
            if (y->shndx == SHN_UNDEF) referenced = 1; else *defined = 1;
        }
    }
    return referenced;
}

/* Library members are linked only when they define a name that is still
 * needed (the entry point counts as needed); repeated until nothing changes. */
static int pull_members(Linker *ld)
{
    LibMember *ms = (LibMember *)ld->libs.data;
    size_t n = ld->libs.len / sizeof *ms, i;
    int changed = 1;
    static const char entry[] = "_بداية";
    while (changed) {
        changed = 0;
        for (i = 0; i < n; i++) {
            ObjView v;
            uint32_t k;
            int want = 0;
            if (ms[i].used) continue;
            if (obj_parse(ms[i].mem.data, ms[i].mem.len, &v) != 0) {
                return bad_object(ms[i].lib);
            }
            for (k = 0; k < v.nsyms && !want; k++) {
                ObjSym y;
                int defined;
                obj_sym(&v, k, &y);
                if (y.kind != OBJ_EXPORTED) continue;
                want = name_state(ld, y.name, y.len, &defined) && !defined;
                if (!defined && y.len == sizeof entry - 1 && memcmp(y.name, entry, y.len) == 0) {
                    want = 1;
                }
            }
            if (want) {
                InObj o;
                size_t pl = strlen(ms[i].lib);
                char *p = arena_alloc(ld->arena, pl + ms[i].mem.name_len + 3);
                memset(&o, 0, sizeof o);
                memcpy(p, ms[i].lib, pl);
                p[pl] = '(';
                memcpy(p + pl + 1, ms[i].mem.name, ms[i].mem.name_len);
                p[pl + 1 + ms[i].mem.name_len] = ')';
                p[pl + 2 + ms[i].mem.name_len] = 0;
                o.path = p;
                o.file.len = ms[i].mem.len;
                o.file.data = rt_alloc(o.file.len + 1);
                memcpy(o.file.data, ms[i].mem.data, o.file.len);
                if (read_native(ld, &o) != 0) {
                    rt_free(o.file.data);
                    return -1;
                }
                ld->objs = rt_realloc(ld->objs, (ld->nobjs + 1) * sizeof *ld->objs);
                ld->objs[ld->nobjs++] = o;
                ms[i].used = 1;
                changed = 1;
            }
        }
    }
    return 0;
}

/* Every object must have been compiled against the interfaces of the
 * modules it is linked with (spec-language 8.2): same hash, or stale. */
static int check_interfaces(Linker *ld)
{
    size_t i, j;
    uint32_t k;
    int rc = 0;
    for (i = 0; i < ld->nobjs; i++) {
        InObj *a = &ld->objs[i];
        if (!a->has_iface) continue;
        for (j = 0; j < i; j++) {
            InObj *b = &ld->objs[j];
            if (b->has_iface && b->iface.module_len == a->iface.module_len &&
                memcmp(b->iface.module, a->iface.module, a->iface.module_len) == 0) {
                diag_plain2(D_LINK_DUP_MODULE, cstr(ld, a->iface.module, a->iface.module_len), a->path);
                rc = -1;
            }
        }
    }
    for (i = 0; i < ld->nobjs; i++) {
        InObj *a = &ld->objs[i];
        if (!a->has_iface) continue;
        for (k = 0; k < a->iface.nimports; k++) {
            const uint8_t *name;
            uint32_t n;
            uint64_t h;
            obj_iface_import(&a->iface, k, &name, &n, &h);
            for (j = 0; j < ld->nobjs; j++) {
                InObj *b = &ld->objs[j];
                if (b->has_iface && b->iface.module_len == n &&
                    memcmp(b->iface.module, name, n) == 0 && b->iface.hash != h) {
                    diag_plain2(D_LINK_STALE, cstr(ld, name, n), a->path);
                    rc = -1;
                }
            }
        }
    }
    return rc;
}

int ld_link(Linker *ld)
{
    size_t i, j;
    int rc = 0;
    GlobalSym *entry;

    if (pull_members(ld) != 0 || check_interfaces(ld) != 0 || resolve_symbols(ld) != 0 || layout(ld) != 0) {
        return -1;
    }
    for (i = 0; i < ld->nobjs; i++) {
        InObj *o = &ld->objs[i];
        for (j = 0; j < o->shnum; j++) {
            if (o->secs[j].type == SHT_RELA && apply_relocs(ld, o, &o->secs[j]) != 0) {
                rc = -1;
            }
        }
    }
    entry = global_get(ld, "_بداية");
    if (entry->defined != 1) {
        diag_plain2(D_LINK_NO_ENTRY, NULL, NULL);
        return -1;
    }
    {
        int ok;
        ld->entry = sym_addr(ld, entry->obj, entry->sym, &ok);
    }
    if (ld->base == LD_BASE_BARE && ld->entry != ld->base) {
        diag_plain(D_LINK_BARE_ENTRY, NULL); /* the machine starts at the base address */
        return -1;
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* Output                                                              */

static void put_phdr(Buf *b, uint32_t flags, uint64_t off, uint64_t addr,
                     uint64_t filesz, uint64_t memsz)
{
    buf_u32(b, 1);          /* PT_LOAD */
    buf_u32(b, flags);
    buf_u64(b, off);
    buf_u64(b, addr);
    buf_u64(b, addr);
    buf_u64(b, filesz);
    buf_u64(b, memsz);
    buf_u64(b, LD_PAGE);
}

static void put_shdr(Buf *b, uint32_t name, uint32_t type, uint64_t flags, uint64_t addr,
                     uint64_t off, uint64_t size, uint32_t link, uint32_t info,
                     uint64_t align, uint64_t entsize)
{
    buf_u32(b, name);
    buf_u32(b, type);
    buf_u64(b, flags);
    buf_u64(b, addr);
    buf_u64(b, off);
    buf_u64(b, size);
    buf_u32(b, link);
    buf_u32(b, info);
    buf_u64(b, align);
    buf_u64(b, entsize);
}

static uint32_t add_str(Buf *b, const char *s)
{
    uint32_t off = (uint32_t)b->len;
    buf_bytes(b, s, strlen(s) + 1);
    return off;
}

/* Output symbol table: named local symbols, then global ones. */
static void build_symtab(Linker *ld, Buf *symtab, Buf *strtab, uint32_t *first_global)
{
    size_t i, j;
    int pass;

    buf_u8(strtab, 0);
    buf_zeros(symtab, ELF_SYM_SIZE);
    for (pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            *first_global = (uint32_t)(symtab->len / ELF_SYM_SIZE);
        }
        for (i = 0; i < ld->nobjs; i++) {
            InObj *o = &ld->objs[i];
            for (j = 1; j < o->nsyms; j++) {
                InSym *y = &o->syms[j];
                uint16_t shndx;
                uint64_t addr;
                int ok;
                int global = y->global != NULL;
                if (global != pass || y->type == STT_SECTION || y->type == 4 /* FILE */ ||
                    y->name[0] == 0 || strncmp(y->name, ".L", 2) == 0 || y->name[0] == '$') {
                    continue;
                }
                if (global && (y->global->obj != o || y->global->sym != j)) {
                    continue; /* emit each global once, from its definition */
                }
                addr = sym_addr(ld, o, j, &ok);
                if (!ok) {
                    continue;
                }
                shndx = y->shndx == SHN_ABS ? (uint16_t)SHN_ABS
                                            : (uint16_t)(o->secs[y->shndx].out + 1);
                buf_u32(symtab, add_str(strtab, y->name));
                buf_u8(symtab, (uint8_t)(((global ? STB_GLOBAL : STB_LOCAL) << 4) | STT_NOTYPE));
                buf_u8(symtab, 0);
                buf_u16(symtab, shndx);
                buf_u64(symtab, addr);
                buf_u64(symtab, 0);
            }
        }
    }
}

int ld_write(Linker *ld, const char *path)
{
    Buf out, symtab, strtab, shstr;
    uint32_t first_global = 1;
    uint32_t names[OUT_COUNT], n_symtab, n_strtab, n_shstr;
    uint64_t symtab_off, strtab_off, shstr_off, shoff;
    int k;
    int rc;

    buf_init(&out);
    buf_init(&symtab);
    buf_init(&strtab);
    buf_init(&shstr);
    build_symtab(ld, &symtab, &strtab, &first_global);
    buf_u8(&shstr, 0);
    for (k = 0; k < OUT_COUNT; k++) {
        names[k] = add_str(&shstr, out_names[k]);
    }
    n_symtab = add_str(&shstr, ".symtab");
    n_strtab = add_str(&shstr, ".strtab");
    n_shstr = add_str(&shstr, ".shstrtab");

    /* ELF header */
    {
        static const uint8_t ident[16] = {0x7F, 'E', 'L', 'F', 2, 1, 1, 0};
        buf_bytes(&out, ident, 16);
        buf_u16(&out, ET_EXEC);
        buf_u16(&out, EM_RISCV);
        buf_u32(&out, 1);
        buf_u64(&out, ld->entry);
        buf_u64(&out, ELF_EHDR_SIZE);   /* phoff */
        buf_u64(&out, 0);               /* shoff, patched below */
        buf_u32(&out, 0);               /* flags */
        buf_u16(&out, ELF_EHDR_SIZE);
        buf_u16(&out, 56);
        buf_u16(&out, (uint16_t)ld->nphdr);
        buf_u16(&out, ELF_SHDR_SIZE);
        buf_u16(&out, 8);
        buf_u16(&out, 7);
    }
    put_phdr(&out, 5 /* R+X */, ld->seg_off, ld->base,
             ld->out_fileoff[OUT_RODATA] + ld->out_size[OUT_RODATA] - ld->seg_off,
             ld->out_fileoff[OUT_RODATA] + ld->out_size[OUT_RODATA] - ld->seg_off);
    if (ld->nphdr == 2) {
        put_phdr(&out, 6 /* R+W */, ld->out_fileoff[OUT_DATA], ld->out_addr[OUT_DATA],
                 ld->out_size[OUT_DATA],
                 ld->out_addr[OUT_BSS] + ld->out_size[OUT_BSS] - ld->out_addr[OUT_DATA]);
    }
    for (k = 0; k < OUT_COUNT; k++) {
        if (k == OUT_BSS) {
            continue;
        }
        if (out.len < ld->out_fileoff[k]) {
            buf_zeros(&out, (size_t)(ld->out_fileoff[k] - out.len));
        }
        buf_bytes(&out, ld->out_data[k].data, ld->out_data[k].len);
    }
    buf_align(&out, 8);
    symtab_off = out.len;
    buf_bytes(&out, symtab.data, symtab.len);
    strtab_off = out.len;
    buf_bytes(&out, strtab.data, strtab.len);
    shstr_off = out.len;
    buf_bytes(&out, shstr.data, shstr.len);
    buf_align(&out, 8);
    shoff = out.len;

    {
        Buf sh;
        static const uint64_t flags[OUT_COUNT] = {
            SHF_ALLOC | SHF_EXECINSTR, SHF_ALLOC, SHF_ALLOC | SHF_WRITE, SHF_ALLOC | SHF_WRITE
        };
        buf_init(&sh);
        put_shdr(&sh, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        for (k = 0; k < OUT_COUNT; k++) {
            put_shdr(&sh, names[k], k == OUT_BSS ? SHT_NOBITS : SHT_PROGBITS, flags[k],
                     ld->out_addr[k], ld->out_fileoff[k], ld->out_size[k], 0, 0,
                     ld->out_align[k], 0);
        }
        put_shdr(&sh, n_symtab, SHT_SYMTAB, 0, 0, symtab_off, symtab.len, 6, first_global, 8,
                 ELF_SYM_SIZE);
        put_shdr(&sh, n_strtab, SHT_STRTAB, 0, 0, strtab_off, strtab.len, 0, 0, 1, 0);
        put_shdr(&sh, n_shstr, SHT_STRTAB, 0, 0, shstr_off, shstr.len, 0, 0, 1, 0);
        buf_bytes(&out, sh.data, sh.len);
        buf_free(&sh);
    }
    wr64(out.data + 0x28, shoff);

    rc = rt_write_file(path, out.data, out.len);
    if (rc == 0) {
        rc = rt_make_executable(path);
    }
    buf_free(&out);
    buf_free(&symtab);
    buf_free(&strtab);
    buf_free(&shstr);
    return rc;
}
