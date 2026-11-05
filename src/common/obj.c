/* obj.c - our object format: writer, validating reader, text dump. */
#include "obj.h"

#include <string.h>

#include "number.h"
#include "utf8.h"

static const uint8_t MAGIC[8] = {0xD9, 0x83, 0xD8, 0xA7, 0xD8, 0xA6, 0xD9, 0x86}; /* كائن */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

static uint64_t up8(uint64_t v) { return (v + 7u) & ~(uint64_t)7u; }

uint64_t obj_fnv1a(const uint8_t *data, size_t len)
{
    uint64_t h = 14695981039346656037u;
    size_t i;
    for (i = 0; i < len; i++) {
        h = (h ^ data[i]) * 1099511628211u;
    }
    return h;
}

void obj_iface_encode(const uint8_t *module, size_t module_len, uint64_t source_hash,
                      size_t nimports, const uint8_t *const *names, const size_t *lens,
                      const uint64_t *hashes, const uint8_t *text, size_t text_len, Buf *out)
{
    size_t i;
    buf_u32(out, (uint32_t)module_len);
    buf_bytes(out, module, module_len);
    buf_u64(out, obj_fnv1a(text, text_len));
    buf_u64(out, source_hash);
    buf_u32(out, (uint32_t)nimports);
    for (i = 0; i < nimports; i++) {
        buf_u32(out, (uint32_t)lens[i]);
        buf_bytes(out, names[i], lens[i]);
        buf_u64(out, hashes[i]);
    }
    buf_u32(out, (uint32_t)text_len);
    buf_bytes(out, text, text_len);
}

int obj_iface_decode(const uint8_t *d, size_t len, ObjIface *f)
{
    size_t pos = 0;
    uint32_t i, n;
    memset(f, 0, sizeof *f);
    if (len < 4) return -1;
    f->module_len = rd32(d);
    pos = 4;
    if (f->module_len == 0 || len - pos < (size_t)f->module_len + 20) return -1;
    f->module = d + pos;
    pos += f->module_len;
    f->hash = rd64(d + pos);
    f->source_hash = rd64(d + pos + 8);
    f->nimports = rd32(d + pos + 16);
    pos += 20;
    f->imports = d + pos;
    for (i = 0; i < f->nimports; i++) {
        if (len - pos < 4) return -1;
        n = rd32(d + pos);
        if (n == 0 || len - pos - 4 < (size_t)n + 8) return -1;
        pos += 4 + (size_t)n + 8;
    }
    if (len - pos < 4) return -1;
    f->text_len = rd32(d + pos);
    pos += 4;
    if (len - pos != f->text_len) return -1;
    f->text = d + pos;
    return obj_fnv1a(f->text, f->text_len) == f->hash ? 0 : -1;
}

void obj_iface_import(const ObjIface *f, uint32_t i, const uint8_t **name, uint32_t *len,
                      uint64_t *hash)
{
    const uint8_t *p = f->imports;
    uint32_t k;
    for (k = 0; k < i; k++) {
        p += 4 + rd32(p) + 8;
    }
    *len = rd32(p);
    *name = p + 4;
    *hash = rd64(p + 4 + *len);
}

int obj_iface(const ObjView *v, ObjIface *out)
{
    if (v->iface_size == 0) return -1;
    return obj_iface_decode(v->file + v->iface_off, (size_t)v->iface_size, out);
}

void obj_serialize(const ObjOut *o, Buf *out)
{
    Buf names;
    size_t i;
    int k;

    buf_init(&names);
    /* names are placed first so their offsets are known */
    buf_bytes(out, MAGIC, 8);
    buf_u32(out, OBJ_VERSION);
    buf_u32(out, 0);
    for (k = 0; k < OBJ_NSEC; k++) {
        buf_u64(out, o->size[k]);
    }
    for (k = 0; k < OBJ_NSEC; k++) {
        buf_u32(out, o->align[k]);
    }
    for (i = 0; i < o->nsyms; i++) {
        buf_bytes(&names, o->syms[i].name, o->syms[i].len);
    }
    buf_u32(out, (uint32_t)o->nsyms);
    buf_u32(out, (uint32_t)o->nrelocs);
    buf_u32(out, (uint32_t)names.len);
    buf_u32(out, (uint32_t)o->iface_len);

    for (k = 0; k < OBJ_SEC_BSS; k++) {
        buf_align(out, 8);
        if (o->size[k] != 0) {
            buf_bytes(out, o->content[k], (size_t)o->size[k]);
        }
    }
    buf_align(out, 8);
    {
        uint32_t off = 0;
        for (i = 0; i < o->nsyms; i++) {
            const ObjSym *s = &o->syms[i];
            buf_u32(out, off);
            buf_u32(out, s->len);
            buf_u8(out, s->kind);
            buf_u8(out, s->shape);
            buf_u8(out, s->section);
            buf_u8(out, 0);
            buf_u32(out, s->size);
            buf_u64(out, s->value);
            off += s->len;
        }
    }
    for (i = 0; i < o->nrelocs; i++) {
        const ObjReloc *r = &o->relocs[i];
        buf_u64(out, r->offset);
        buf_u8(out, r->section);
        buf_u8(out, r->kind);
        buf_u16(out, 0);
        buf_u32(out, r->sym);
        buf_u64(out, (uint64_t)r->extra);
    }
    buf_bytes(out, names.data, names.len);
    if (o->iface_len != 0) {
        buf_bytes(out, o->iface, o->iface_len);
    }
    buf_free(&names);
}

int obj_is_object(const uint8_t *data, size_t len)
{
    return len >= 8 && memcmp(data, MAGIC, 8) == 0;
}

int obj_parse(const uint8_t *d, size_t len, ObjView *v)
{
    uint64_t pos;
    int k;

    memset(v, 0, sizeof *v);
    if (len < OBJ_HEADER_SIZE || !obj_is_object(d, len) || rd32(d + 8) != OBJ_VERSION) {
        return -1;
    }
    v->file = d;
    v->len = len;
    for (k = 0; k < OBJ_NSEC; k++) {
        v->size[k] = rd64(d + 16 + 8 * k);
        v->align[k] = rd32(d + 48 + 4 * k);
        if (v->align[k] != 0 && (v->align[k] > OBJ_MAX_ALIGN || (v->align[k] & (v->align[k] - 1)) != 0)) {
            return -1;
        }
        if (v->size[k] > len) {
            return -1; /* also guards the sums below */
        }
    }
    v->nsyms = rd32(d + 64);
    v->nrelocs = rd32(d + 68);
    v->names_size = rd32(d + 72);
    v->iface_size = rd32(d + 76);
    pos = OBJ_HEADER_SIZE;
    for (k = 0; k < OBJ_SEC_BSS; k++) {
        pos = up8(pos);
        v->content_off[k] = pos;
        pos += v->size[k];
        if (pos > len) {
            return -1;
        }
    }
    pos = up8(pos);
    v->syms_off = pos;
    pos += (uint64_t)v->nsyms * OBJ_SYM_SIZE;
    v->relocs_off = pos;
    pos += (uint64_t)v->nrelocs * OBJ_RELOC_SIZE;
    v->names_off = pos;
    pos += v->names_size;
    v->iface_off = pos;
    pos += v->iface_size;
    if (pos != len) {
        return -1;
    }
    if (v->iface_size != 0) {
        ObjIface f;
        if (obj_iface_decode(d + v->iface_off, (size_t)v->iface_size, &f) != 0) {
            return -1;
        }
    }
    {
        uint32_t i;
        uint32_t seen_global = 0;
        for (i = 0; i < v->nsyms; i++) {
            const uint8_t *e = d + v->syms_off + (uint64_t)i * OBJ_SYM_SIZE;
            uint32_t off = rd32(e), n = rd32(e + 4);
            uint8_t kind = e[8], shape = e[9], sec = e[10];
            if ((uint64_t)off + n > v->names_size || kind > OBJ_IMPORTED || shape > OBJ_SHAPE_DATA) {
                return -1;
            }
            if (kind == OBJ_PRIVATE ? seen_global : (seen_global = 1, 0)) {
                return -1; /* private symbols come first */
            }
            if (kind == OBJ_IMPORTED ? sec != OBJ_SEC_UNDEF
                                     : (sec > OBJ_SEC_ABS)) {
                return -1;
            }
        }
        for (i = 0; i < v->nrelocs; i++) {
            const uint8_t *e = d + v->relocs_off + (uint64_t)i * OBJ_RELOC_SIZE;
            uint64_t off = rd64(e);
            uint8_t sec = e[8], kind = e[9];
            uint32_t sym = rd32(e + 12);
            uint64_t need = (kind == OBJ_REL_DWORD || kind == OBJ_REL_CALL) ? 8 : 4;
            int lo = kind == OBJ_REL_PCREL_LO_I || kind == OBJ_REL_PCREL_LO_S;
            if (sec >= OBJ_SEC_BSS || kind < 1 || kind > OBJ_REL_LAST ||
                off > v->size[sec] || v->size[sec] - off < need) {
                return -1;
            }
            if (lo ? sym != OBJ_NO_SYMBOL : sym >= v->nsyms) {
                return -1;
            }
        }
    }
    return 0;
}

void obj_sym(const ObjView *v, uint32_t i, ObjSym *out)
{
    const uint8_t *e = v->file + v->syms_off + (uint64_t)i * OBJ_SYM_SIZE;
    out->name = v->file + v->names_off + rd32(e);
    out->len = rd32(e + 4);
    out->kind = e[8];
    out->shape = e[9];
    out->section = e[10];
    out->size = rd32(e + 12);
    out->value = rd64(e + 16);
}

void obj_reloc(const ObjView *v, uint32_t i, ObjReloc *out)
{
    const uint8_t *e = v->file + v->relocs_off + (uint64_t)i * OBJ_RELOC_SIZE;
    out->offset = rd64(e);
    out->section = e[8];
    out->kind = e[9];
    out->sym = rd32(e + 12);
    out->extra = (int64_t)rd64(e + 16);
}

/* ------------------------------------------------------------------ */
/* Libraries                                                           */

static const uint8_t LIB_MAGIC[10] = {0xD9, 0x85, 0xD9, 0x83, 0xD8, 0xAA, 0xD8, 0xA8, 0xD8, 0xA9}; /* مكتبة */

int obj_is_lib(const uint8_t *data, size_t len)
{
    return len >= 10 && memcmp(data, LIB_MAGIC, 10) == 0;
}

int obj_lib_parse(const uint8_t *d, size_t len, ObjLib *lib)
{
    uint32_t i;
    uint64_t table;
    memset(lib, 0, sizeof *lib);
    if (len < OBJ_LIB_HEADER || !obj_is_lib(d, len) || (d[10] | (d[11] << 8)) != 1) {
        return -1;
    }
    lib->n = rd32(d + 12);
    table = OBJ_LIB_HEADER + (uint64_t)lib->n * OBJ_LIB_ENTRY;
    if (table > len) return -1;
    for (i = 0; i < lib->n; i++) {
        const uint8_t *e = d + OBJ_LIB_HEADER + (uint64_t)i * OBJ_LIB_ENTRY;
        uint64_t off = rd64(e), size = rd64(e + 8);
        uint64_t noff = rd32(e + 16), nlen = rd32(e + 20);
        if (off < table || off > len || size > len - off ||
            noff < table || noff > len || nlen > len - noff) {
            return -1;
        }
    }
    lib->file = d;
    lib->len = len;
    return 0;
}

int obj_lib_member(const ObjLib *lib, uint32_t i, ObjLibMember *m)
{
    const uint8_t *e;
    if (i >= lib->n) return -1;
    e = lib->file + OBJ_LIB_HEADER + (uint64_t)i * OBJ_LIB_ENTRY;
    m->data = lib->file + rd64(e);
    m->len = (size_t)rd64(e + 8);
    m->name = lib->file + rd32(e + 16);
    m->name_len = rd32(e + 20);
    return 0;
}

void obj_lib_serialize(size_t n, const uint8_t *const *names, const size_t *name_lens,
                       const uint8_t *const *datas, const size_t *sizes, Buf *out)
{
    uint64_t pos = OBJ_LIB_HEADER + (uint64_t)n * OBJ_LIB_ENTRY;
    uint64_t names_at = pos;
    size_t i;
    for (i = 0; i < n; i++) pos += name_lens[i];
    buf_bytes(out, LIB_MAGIC, 10);
    buf_u16(out, 1);
    buf_u32(out, (uint32_t)n);
    for (i = 0; i < n; i++) {
        pos = up8(pos);
        buf_u64(out, pos);
        buf_u64(out, sizes[i]);
        buf_u32(out, (uint32_t)names_at);
        buf_u32(out, (uint32_t)name_lens[i]);
        names_at += name_lens[i];
        pos += sizes[i];
    }
    for (i = 0; i < n; i++) buf_bytes(out, names[i], name_lens[i]);
    for (i = 0; i < n; i++) {
        buf_align(out, 8);
        buf_bytes(out, datas[i], sizes[i]);
    }
}

/* ------------------------------------------------------------------ */
/* Text form                                                           */

static void put_text(Buf *b, const char *s)
{
    buf_bytes(b, s, strlen(s));
}

static void put_cps(Buf *b, const uint32_t *cps, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        uint8_t u[4];
        size_t k = utf8_encode(cps[i], u);
        buf_bytes(b, u, k);
    }
}

static void put_num(Buf *b, uint64_t v)
{
    uint32_t cps[24];
    put_cps(b, cps, num_format_dec(v, cps));
}

static void put_hex(Buf *b, uint64_t v)
{
    uint32_t cps[24];
    put_cps(b, cps, num_format_hex(v, cps));
}

static void put_signed(Buf *b, int64_t v)
{
    if (v < 0) {
        put_text(b, "-");
        put_num(b, (uint64_t)0 - (uint64_t)v);
    } else {
        put_num(b, (uint64_t)v);
    }
}

static const char *const SEC_NAMES[OBJ_NSEC] = {"نص", "بيانات", "ثوابت", "أصفار"};
static const char *const KIND_NAMES[] = {"خاص", "مصدر", "مستورد"};
static const char *const REL_NAMES[] = {
    "؟", "فرع", "قفزة", "نداء", "عال", "منخفض_ي", "منخفض_خ", "نسبي_عال",
    "نسبي_منخفض_ي", "نسبي_منخفض_خ", "كلمة", "كلمة_مزدوجة"
};

static void put_sec(Buf *b, unsigned sec)
{
    if (sec < OBJ_NSEC) {
        put_text(b, SEC_NAMES[sec]);
    } else if (sec == OBJ_SEC_ABS) {
        put_text(b, "مطلق");
    } else {
        put_text(b, "غير_معرف");
    }
}

void obj_dump(const ObjView *v, Buf *out)
{
    uint32_t i;
    int k;

    put_text(out, "كائن إصدار ");
    put_num(out, OBJ_VERSION);
    put_text(out, "\n");
    for (k = 0; k < OBJ_NSEC; k++) {
        put_text(out, "قسم ");
        put_text(out, SEC_NAMES[k]);
        put_text(out, " حجم ");
        put_num(out, v->size[k]);
        put_text(out, " محاذاة ");
        put_num(out, v->align[k]);
        put_text(out, "\n");
    }
    for (i = 0; i < v->nsyms; i++) {
        ObjSym s;
        obj_sym(v, i, &s);
        put_text(out, "رمز ");
        put_text(out, KIND_NAMES[s.kind]);
        put_text(out, " ");
        buf_bytes(out, s.name, s.len);
        put_text(out, " قسم ");
        put_sec(out, s.section);
        put_text(out, " قيمة ");
        put_num(out, s.value);
        put_text(out, "\n");
    }
    {
        ObjIface f;
        if (obj_iface(v, &f) == 0) {
            uint32_t k;
            put_text(out, "وحدة ");
            buf_bytes(out, f.module, f.module_len);
            put_text(out, " بصمة_الواجهة ");
            put_hex(out, f.hash);
            put_text(out, " بصمة_المصدر ");
            put_hex(out, f.source_hash);
            put_text(out, "\n");
            for (k = 0; k < f.nimports; k++) {
                const uint8_t *name;
                uint32_t n;
                uint64_t h;
                obj_iface_import(&f, k, &name, &n, &h);
                put_text(out, "مبني_على ");
                buf_bytes(out, name, n);
                put_text(out, " بصمة ");
                put_hex(out, h);
                put_text(out, "\n");
            }
            put_text(out, "واجهة }\n");
            buf_bytes(out, f.text, f.text_len);
            put_text(out, "{\n");
        }
    }
    for (i = 0; i < v->nrelocs; i++) {
        ObjReloc r;
        obj_reloc(v, i, &r);
        put_text(out, "إعادة_توطين ");
        put_text(out, REL_NAMES[r.kind]);
        put_text(out, " قسم ");
        put_sec(out, r.section);
        put_text(out, " موضع ");
        put_num(out, r.offset);
        if (r.sym != OBJ_NO_SYMBOL) {
            ObjSym s;
            obj_sym(v, r.sym, &s);
            put_text(out, " رمز ");
            buf_bytes(out, s.name, s.len);
            put_text(out, " إضافة ");
        } else {
            put_text(out, " مقترن ");
        }
        put_signed(out, r.extra);
        put_text(out, "\n");
    }
}
