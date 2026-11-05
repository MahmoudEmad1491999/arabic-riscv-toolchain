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
    buf_u32(out, 0); /* interface_size */

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
