/* types.c - type representation, identity and layout. */
#include "cc.h"

#include "../common/number.h"
#include "../common/utf8.h"

#include <string.h>

Type *ty_void, *ty_bool, *ty_char, *ty_size, *ty_untyped, *ty_null;
static Type *ints[2][4]; /* [signed][8,16,32,64] */
static Arena *types_arena;

int bytes_eq(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen)
{
    return alen == blen && (alen == 0 || memcmp(a, b, alen) == 0);
}

int bytes_eq_str(const uint8_t *a, size_t alen, const char *s)
{
    return bytes_eq(a, alen, (const uint8_t *)s, strlen(s));
}

Type *ty_new(Arena *a, TypeKind kind)
{
    Type *t = arena_alloc(a, sizeof *t);
    t->kind = kind;
    return t;
}

static int bit_index(int bits)
{
    return bits == 8 ? 0 : bits == 16 ? 1 : bits == 32 ? 2 : 3;
}

void types_init(Arena *a)
{
    int s, i;
    types_arena = a;
    ty_void = ty_new(a, T_VOID);
    ty_bool = ty_new(a, T_BOOL);
    ty_char = ty_new(a, T_CHAR);
    ty_untyped = ty_new(a, T_UNTYPED);
    ty_null = ty_new(a, T_NULLLIT);
    for (s = 0; s < 2; s++) {
        for (i = 0; i < 4; i++) {
            Type *t = ty_new(a, T_INT);
            t->bits = 8 << i;
            t->is_signed = s;
            ints[s][i] = t;
        }
    }
    ty_size = ty_new(a, T_INT);
    ty_size->bits = 64;
    ty_size->is_size = 1;
}

Type *ty_int(int bits, int is_signed)
{
    return ints[is_signed ? 1 : 0][bit_index(bits)];
}

/* Scalar type names as stored in the file: the digits are typed so they
 * read correctly on screen, so 32 is stored ٢٣ (decision 20). */
Type *ty_scalar_by_name(const uint8_t *name, size_t len)
{
    static const char *const names[] = {
        "صحيح٨", "صحيح٦١", "صحيح٢٣", "صحيح٤٦",
        "طبيعي٨", "طبيعي٦١", "طبيعي٢٣", "طبيعي٤٦"
    };
    int i;
    for (i = 0; i < 8; i++) {
        if (bytes_eq_str(name, len, names[i])) {
            return ty_int(8 << (i % 4), i < 4);
        }
    }
    if (bytes_eq_str(name, len, "حجم")) return ty_size;
    if (bytes_eq_str(name, len, "منطقي")) return ty_bool;
    if (bytes_eq_str(name, len, "حرف")) return ty_char;
    return NULL;
}

int ty_equal(const Type *a, const Type *b)
{
    size_t i;
    if (a == b) return 1;
    if (a == NULL || b == NULL || a->kind != b->kind) return 0;
    switch (a->kind) {
    case T_INT:
        return a->bits == b->bits && a->is_signed == b->is_signed && a->is_size == b->is_size;
    case T_PTR:
    case T_SLICE:
        return a->readonly == b->readonly && ty_equal(a->elem, b->elem);
    case T_OPT:
        return ty_equal(a->elem, b->elem);
    case T_ARRAY:
        return a->count == b->count && ty_equal(a->elem, b->elem);
    case T_STRUCT:
    case T_ENUM:
        return a->decl == b->decl;
    case T_FUNC:
        if (a->nparams != b->nparams) return 0;
        if ((a->elem == NULL) != (b->elem == NULL)) return 0;
        if (a->elem && !ty_equal(a->elem, b->elem)) return 0;
        for (i = 0; i < a->nparams; i++) {
            if (!ty_equal(a->params[i], b->params[i])) return 0;
        }
        return 1;
    default:
        return 1;
    }
}

uint64_t ty_size_of(const Type *t)
{
    switch (t->kind) {
    case T_INT: return (uint64_t)t->bits / 8;
    case T_BOOL: return 1;
    case T_CHAR: return 4;
    case T_PTR: case T_OPT: case T_FUNC: return 8;
    case T_SLICE: return 16;
    case T_ARRAY: return t->count * ty_size_of(t->elem);
    case T_STRUCT: return t->decl->size;
    case T_ENUM: return ty_size_of(t->decl->underlying);
    default: return 0;
    }
}

uint64_t ty_align_of(const Type *t)
{
    switch (t->kind) {
    case T_SLICE: return 8;
    case T_ARRAY: return ty_align_of(t->elem);
    case T_STRUCT: return t->decl->align;
    default: {
        uint64_t s = ty_size_of(t);
        return s == 0 ? 1 : s;
    }
    }
}

int ty_is_int(const Type *t)
{
    return t != NULL && t->kind == T_INT;
}

int ty_is_aggregate(const Type *t)
{
    return t->kind == T_ARRAY || t->kind == T_STRUCT;
}

/* ---- names for messages (file order) ---- */

static char name_buf[512];
static size_t name_len;

static void nb_str(const char *s)
{
    size_t n = strlen(s);
    if (name_len + n < sizeof name_buf) {
        memcpy(name_buf + name_len, s, n);
        name_len += n;
    }
}

static void nb_bytes(const uint8_t *s, size_t n)
{
    if (name_len + n < sizeof name_buf) {
        memcpy(name_buf + name_len, s, n);
        name_len += n;
    }
}

static void nb_num(uint64_t v)
{
    uint32_t cps[24];
    size_t n = num_format_dec(v, cps), i;
    for (i = 0; i < n; i++) {
        uint8_t e[4];
        nb_bytes(e, utf8_encode(cps[i], e));
    }
}

static void nb_type(const Type *t)
{
    switch (t->kind) {
    case T_VOID: nb_str("لا_قيمة"); break;
    case T_INT:
        if (t->is_size) {
            nb_str("حجم");
        } else {
            static const char *const digits[] = {"٨", "٦١", "٢٣", "٤٦"};
            nb_str(t->is_signed ? "صحيح" : "طبيعي");
            nb_str(digits[bit_index(t->bits)]);
        }
        break;
    case T_BOOL: nb_str("منطقي"); break;
    case T_CHAR: nb_str("حرف"); break;
    case T_PTR: nb_str("^"); if (t->readonly) nb_str("ثابت "); nb_type(t->elem); break;
    case T_OPT: nb_str("اختياري "); nb_type(t->elem); break;
    case T_ARRAY: nb_str("]"); nb_num(t->count); nb_str("["); nb_type(t->elem); break;
    case T_SLICE: nb_str("]["); if (t->readonly) nb_str("ثابت "); nb_type(t->elem); break;
    case T_STRUCT: case T_ENUM: nb_bytes(t->decl->name, t->decl->len); break;
    case T_FUNC: nb_str("دالة"); break;
    case T_UNTYPED: nb_str("عدد"); break;
    case T_NULLLIT: nb_str("لاشيء"); break;
    case T_NAMED: nb_bytes(t->tname, t->tname_len); break;
    }
}

const char *ty_name(const Type *t)
{
    name_len = 0;
    if (t == NULL) {
        nb_str("لا_قيمة");
    } else {
        nb_type(t);
    }
    name_buf[name_len] = 0;
    return name_buf;
}
