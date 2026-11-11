/* gen.c - code generation: Arabic assembly for مجمع, RISC-V LP64 ABI.
 *
 * A simple stack machine. An expression leaves its value in:
 *   scalars (integers, booleans, characters, enumerations, pointers): وسيط٠,
 *     always in canonical form (sign-extended if signed, zero-extended if not);
 *   slices: وسيط٠ = pointer, وسيط١ = length;
 *   arrays and structures: وسيط٠ = the address of the value.
 * Intermediate values are pushed on the stack in 16-byte slots, which keeps
 * the stack pointer 16-byte aligned at every call. Locals live at negative
 * offsets from the frame pointer محفوظ٠. Numbers are written in file order
 * through number.c (least significant digit first).
 */
#include "cc.h"

#include "../common/number.h"
#include "../common/utf8.h"

#include <string.h>

typedef struct {
    Program *prog;
    Buf *out;          /* where emit() writes */
    Buf text;          /* current function body */
    int label;         /* label counter */
    int32_t frame;     /* bytes below the frame pointer in use */
    Decl *fn;
    int ret_label;
    int32_t hidden_ret_slot; /* where the caller's result pointer is kept */
    int break_label[64], cont_label[64];
    int loop_depth;
    /* --تنقيح (decision 64, A89): rows written at the end of each function
     * (functions, scopes, variables) and of the file (types and the rest) */
    int debug;
    int nfuncs, nscopes, cur_scope;
    Buf scopes;        /* DbgScope of the current function */
    Buf vars;          /* DbgVar of the current function */
    Buf tymap;         /* Type* per row of the types table */
    Buf types;         /* DbgLine per row of the types table */
    Buf fields, members, params;   /* the directives, as text */
    int nfields, nmembers, nparams;
} Gen;

typedef struct {
    int parent;        /* -1: none */
    int start, end;    /* labels */
} DbgScope;

typedef struct {
    Local *local;
    int scope;
    int param;
} DbgVar;

typedef struct {
    const uint8_t *text;
    size_t len;
} DbgLine;

/* ------------------------------------------------------------------ */
/* Text output                                                         */

static void emit(Gen *g, const char *s) { buf_bytes(g->out, s, strlen(s)); }
static void emitb(Gen *g, const uint8_t *s, size_t n) { buf_bytes(g->out, s, n); }

static void emit_num(Gen *g, int64_t v)
{
    uint32_t cps[24];
    uint64_t mag = v < 0 ? 0u - (uint64_t)v : (uint64_t)v;
    size_t n = num_format_dec(mag, cps), i;
    if (v < 0) emit(g, "-");
    for (i = 0; i < n; i++) {
        uint8_t e[4];
        emitb(g, e, utf8_encode(cps[i], e));
    }
}

static void emit_unum(Gen *g, uint64_t v)
{
    uint32_t cps[24];
    size_t n = num_format_dec(v, cps), i;
    for (i = 0; i < n; i++) {
        uint8_t e[4];
        emitb(g, e, utf8_encode(cps[i], e));
    }
}

static void emit_label_name(Gen *g, int n)
{
    emit(g, "ت_");
    emit_num(g, n);
}

static void emit_label_def(Gen *g, int n)
{
    emit_label_name(g, n);
    emit(g, ":\n");
}

static int new_label(Gen *g) { return ++g->label; }

/* instruction with operands separated by the Arabic comma */
static void ins(Gen *g, const char *mn, const char *a, const char *b, const char *c)
{
    emit(g, "    ");
    emit(g, mn);
    if (a) { emit(g, " "); emit(g, a); }
    if (b) { emit(g, "، "); emit(g, b); }
    if (c) { emit(g, "، "); emit(g, c); }
    emit(g, "\n");
}

static void ins_imm(Gen *g, const char *mn, const char *a, const char *b, int64_t imm)
{
    emit(g, "    ");
    emit(g, mn);
    emit(g, " ");
    emit(g, a);
    emit(g, "، ");
    if (b) { emit(g, b); emit(g, "، "); }
    emit_num(g, imm);
    emit(g, "\n");
}

static void li(Gen *g, const char *r, int64_t v) { ins_imm(g, "حمل_قيمة", r, NULL, v); }

static void jump(Gen *g, int label)
{
    emit(g, "    قفز ");
    emit_label_name(g, label);
    emit(g, "\n");
}

/* if register is zero / not zero, jump to label (far-safe: branch over a jump) */
static void jump_if(Gen *g, const char *r, int when_nonzero, int label)
{
    int skip = new_label(g);
    emit(g, when_nonzero ? "    تفرع_إذا_صفر " : "    تفرع_إذا_ليس_صفرا ");
    emit(g, r);
    emit(g, "، ");
    emit_label_name(g, skip);
    emit(g, "\n");
    jump(g, label);
    emit_label_def(g, skip);
}

static void emit_sym(Gen *g, const Decl *d) { emitb(g, d->asm_name, d->asm_len); }

/* load/store at base + off; large offsets go through مؤقت٦ */
static void mem_op(Gen *g, const char *mn, const char *reg, const char *base, int64_t off)
{
    if (off < -2048 || off > 2047) {
        li(g, "مؤقت٦", off);
        ins(g, "جمع", "مؤقت٦", base, "مؤقت٦");
        base = "مؤقت٦";
        off = 0;
    }
    emit(g, "    ");
    emit(g, mn);
    emit(g, " ");
    emit(g, reg);
    emit(g, "، ");
    emit(g, base);
    if (off < 0) { emit(g, " - "); emit_num(g, -off); }
    else if (off > 0) { emit(g, " + "); emit_num(g, off); }
    emit(g, "\n");
}

static void add_imm(Gen *g, const char *rd, const char *rs, int64_t v)
{
    if (v >= -2048 && v <= 2047) {
        ins_imm(g, "جمع_فوري", rd, rs, v);
    } else {
        li(g, "مؤقت٦", v);
        ins(g, "جمع", rd, rs, "مؤقت٦");
    }
}

static void push(Gen *g, const char *r)
{
    ins_imm(g, "جمع_فوري", "مؤشر_المكدس", "مؤشر_المكدس", -16);
    mem_op(g, "خزن_كلمة_مزدوجة", r, "مؤشر_المكدس", 0);
}

static void push2(Gen *g, const char *r0, const char *r1)
{
    ins_imm(g, "جمع_فوري", "مؤشر_المكدس", "مؤشر_المكدس", -16);
    mem_op(g, "خزن_كلمة_مزدوجة", r0, "مؤشر_المكدس", 0);
    mem_op(g, "خزن_كلمة_مزدوجة", r1, "مؤشر_المكدس", 8);
}

static void pop(Gen *g, const char *r)
{
    mem_op(g, "حمل_كلمة_مزدوجة", r, "مؤشر_المكدس", 0);
    ins_imm(g, "جمع_فوري", "مؤشر_المكدس", "مؤشر_المكدس", 16);
}

static void pop2(Gen *g, const char *r0, const char *r1)
{
    mem_op(g, "حمل_كلمة_مزدوجة", r0, "مؤشر_المكدس", 0);
    mem_op(g, "حمل_كلمة_مزدوجة", r1, "مؤشر_المكدس", 8);
    ins_imm(g, "جمع_فوري", "مؤشر_المكدس", "مؤشر_المكدس", 16);
}

static void call_sym(Gen *g, const char *name)
{
    emit(g, "    استدعاء ");
    emit(g, name);
    emit(g, "\n");
}

/* ------------------------------------------------------------------ */
/* Frame slots                                                         */

static int32_t alloc_slot(Gen *g, uint64_t size, uint64_t align)
{
    uint64_t a = align < 8 ? 8 : align;
    uint64_t sz = (size + 7) & ~(uint64_t)7;
    g->frame = (int32_t)(((uint64_t)g->frame + sz + a - 1) / a * a);
    return -g->frame;
}

static int32_t local_slot(Gen *g, Local *l)
{
    if (l->offset == 0) {
        uint64_t s = ty_size_of(l->type);
        l->offset = alloc_slot(g, s == 0 ? 8 : s, ty_align_of(l->type));
    }
    return l->offset;
}

/* ------------------------------------------------------------------ */
/* Loads, stores, normalisation                                        */

static const Type *scalar_repr(const Type *t)
{
    return t->kind == T_ENUM ? t->decl->underlying : t;
}

/* bring وسيط٠ (or reg) into canonical form for t */
static void normalize(Gen *g, const char *r, const Type *t)
{
    t = scalar_repr(t);
    if (t->kind == T_FLOAT && t->bits == 32) {
        ins_imm(g, "جمع_فوري_كلمة", r, r, 0); /* the bits, sign-extended like حمل_كلمة */
        return;
    }
    if (t->kind != T_INT || t->bits == 64) return;
    if (t->bits == 32) {
        if (t->is_signed) {
            ins_imm(g, "جمع_فوري_كلمة", r, r, 0);
        } else {
            ins_imm(g, "رفع_فوري", r, r, 32);
            ins_imm(g, "خفض_منطقي_فوري", r, r, 32);
        }
    } else if (t->is_signed) {
        ins_imm(g, "رفع_فوري", r, r, 64 - t->bits);
        ins_imm(g, "خفض_حسابي_فوري", r, r, 64 - t->bits);
    } else if (t->bits == 8) {
        ins_imm(g, "و_منطقي_فوري", r, r, 255);
    } else {
        ins_imm(g, "رفع_فوري", r, r, 48);
        ins_imm(g, "خفض_منطقي_فوري", r, r, 48);
    }
}

static void load_scalar(Gen *g, const char *r, const char *base, int64_t off, const Type *t)
{
    const Type *s = scalar_repr(t);
    uint64_t size = ty_size_of(s);
    int sg = (s->kind == T_INT && s->is_signed) || s->kind == T_FLOAT;
    const char *mn = size == 1 ? (sg ? "حمل_بايت" : "حمل_بايت_بلا_إشارة")
                   : size == 2 ? (sg ? "حمل_نصف_كلمة" : "حمل_نصف_كلمة_بلا_إشارة")
                   : size == 4 ? (sg ? "حمل_كلمة" : "حمل_كلمة_بلا_إشارة")
                   : "حمل_كلمة_مزدوجة";
    mem_op(g, mn, r, base, off);
}

static void store_scalar(Gen *g, const char *r, const char *base, int64_t off, const Type *t)
{
    uint64_t size = ty_size_of(scalar_repr(t));
    const char *mn = size == 1 ? "خزن_بايت" : size == 2 ? "خزن_نصف_كلمة"
                   : size == 4 ? "خزن_كلمة" : "خزن_كلمة_مزدوجة";
    mem_op(g, mn, r, base, off);
}

/* copy size bytes: وسيط٠ = destination, وسيط١ = source */
static void copy_mem(Gen *g, uint64_t size)
{
    li(g, "وسيط٢", (int64_t)size);
    call_sym(g, "_غرة_انسخ");
}

/* the value in وسيط٠[/وسيط١] (per convention) is stored at the address in reg */
static void store_value(Gen *g, const char *addr, const Type *t)
{
    if (t->kind == T_SLICE) {
        mem_op(g, "خزن_كلمة_مزدوجة", "وسيط٠", addr, 0);
        mem_op(g, "خزن_كلمة_مزدوجة", "وسيط١", addr, 8);
    } else if (ty_is_aggregate(t)) {
        ins(g, "نسخ", "وسيط١", "وسيط٠", NULL);
        ins(g, "نسخ", "وسيط٠", addr, NULL);
        copy_mem(g, ty_size_of(t));
    } else {
        store_scalar(g, "وسيط٠", addr, 0, t);
    }
}

/* the value at the address in وسيط٠ becomes a value (per convention) */
static void load_value(Gen *g, const Type *t)
{
    if (t->kind == T_SLICE) {
        mem_op(g, "حمل_كلمة_مزدوجة", "وسيط١", "وسيط٠", 8);
        mem_op(g, "حمل_كلمة_مزدوجة", "وسيط٠", "وسيط٠", 0);
    } else if (!ty_is_aggregate(t)) {
        load_scalar(g, "وسيط٠", "وسيط٠", 0, t);
    }
}

/* Byte-by-byte access for locations inside packed structures, which may
 * be misaligned (decision 30, assumption A51). */
static void load_bytes(Gen *g, const char *dst, const char *base, int64_t off, uint64_t n)
{
    uint64_t i = n;
    li(g, dst, 0);
    while (i > 0) {
        i--;
        mem_op(g, "حمل_بايت_بلا_إشارة", "مؤقت١", base, off + (int64_t)i);
        ins_imm(g, "رفع_فوري", dst, dst, 8);
        ins(g, "أو_منطقي", dst, dst, "مؤقت١");
    }
}

static void store_bytes(Gen *g, const char *src, const char *base, int64_t off, uint64_t n)
{
    uint64_t i;
    for (i = 0; i < n; i++) {
        if (i == 0) {
            mem_op(g, "خزن_بايت", src, base, off);
        } else {
            ins_imm(g, "خفض_منطقي_فوري", "مؤقت١", src, (int64_t)(8 * i));
            mem_op(g, "خزن_بايت", "مؤقت١", base, off + (int64_t)i);
        }
    }
}

/* the value at the (possibly misaligned) address in وسيط٠ */
static void load_value_unaligned(Gen *g, const Type *t)
{
    if (t->kind == T_SLICE) {
        load_bytes(g, "مؤقت٢", "وسيط٠", 8, 8);
        load_bytes(g, "مؤقت٣", "وسيط٠", 0, 8);
        ins(g, "نسخ", "وسيط٠", "مؤقت٣", NULL);
        ins(g, "نسخ", "وسيط١", "مؤقت٢", NULL);
    } else if (!ty_is_aggregate(t)) {
        load_bytes(g, "مؤقت٣", "وسيط٠", 0, ty_size_of(scalar_repr(t)));
        ins(g, "نسخ", "وسيط٠", "مؤقت٣", NULL);
        normalize(g, "وسيط٠", t);
    }
}

/* store the value in وسيط٠[/وسيط١] at the (possibly misaligned) address in addr */
static void store_value_unaligned(Gen *g, const char *addr, const Type *t)
{
    if (t->kind == T_SLICE) {
        store_bytes(g, "وسيط٠", addr, 0, 8);
        store_bytes(g, "وسيط١", addr, 8, 8);
    } else if (ty_is_aggregate(t)) {
        store_value(g, addr, t); /* byte copy anyway */
    } else {
        store_bytes(g, "وسيط٠", addr, 0, ty_size_of(scalar_repr(t)));
    }
}

/* same rule as the checker: reached through a field of a packed structure */
static int in_packed(const Expr *e)
{
    switch (e->kind) {
    case E_MEMBER:
        if (e->ref != REF_FIELD) return 0;
        return e->a->type->decl->packed || in_packed(e->a);
    case E_INDEX: return e->a->type->kind == T_ARRAY && in_packed(e->a);
    case E_BUILTIN: return e->tok.kind == K_INDEX_CHECKED && e->args[0]->type->kind == T_ARRAY &&
                           in_packed(e->args[0]);
    default: return 0;
    }
}

static void load_lvalue(Gen *g, const Expr *e)
{
    if (in_packed(e)) load_value_unaligned(g, e->type);
    else load_value(g, e->type);
}

/* ------------------------------------------------------------------ */
/* Expressions                                                         */

static void gen_expr(Gen *g, Expr *e);
static void gen_addr(Gen *g, Expr *e);

static void stop(Gen *g, const char *routine)
{
    call_sym(g, routine);
}

static int is_signed_type(const Type *t)
{
    t = scalar_repr(t);
    return t->kind == T_INT && t->is_signed;
}

static int bits_of(const Type *t)
{
    t = scalar_repr(t);
    return t->kind == T_INT ? t->bits : (int)ty_size_of(t) * 8;
}

static void gen_string(Gen *g, Expr *e)
{
    emit(g, "    حمل_عنوان وسيط٠، ن_");
    emit_num(g, e->string_id);
    emit(g, "\n");
    li(g, "وسيط١", (int64_t)e->tok.str_len);
}

/* address of an element: base (array address or slice pointer) + index * size */
static void gen_element_addr(Gen *g, Expr *base, Expr *idx, int checked)
{
    Type *bt = base->type;
    uint64_t es = ty_size_of(bt->elem);
    if (bt->kind == T_ARRAY) {
        gen_expr(g, base);              /* address of the array */
        li(g, "وسيط١", (int64_t)bt->count);
    } else {
        gen_expr(g, base);              /* pointer, length */
    }
    push2(g, "وسيط٠", "وسيط١");
    gen_expr(g, idx);
    pop2(g, "مؤقت٠", "مؤقت١");
    if (checked) {
        int ok = new_label(g);
        emit(g, "    تفرع_إذا_أصغر_بلا_إشارة وسيط٠، مؤقت١، ");
        emit_label_name(g, ok);
        emit(g, "\n");
        stop(g, "_غرة_خارج_الحدود");
        emit_label_def(g, ok);
    }
    if ((es & (es - 1)) == 0) {
        int sh = 0;
        while (((uint64_t)1 << sh) < es) sh++;
        if (sh) ins_imm(g, "رفع_فوري", "وسيط٠", "وسيط٠", sh);
    } else {
        li(g, "مؤقت٢", (int64_t)es);
        ins(g, "ضرب", "وسيط٠", "وسيط٠", "مؤقت٢");
    }
    ins(g, "جمع", "وسيط٠", "مؤقت٠", "وسيط٠");
}

static void gen_slice(Gen *g, Expr *base, Expr *lo, Expr *hi, int checked)
{
    Type *bt = base->type;
    uint64_t es = ty_size_of(bt->elem);
    if (bt->kind == T_ARRAY) {
        gen_addr(g, base);
        li(g, "وسيط١", (int64_t)bt->count);
    } else {
        gen_expr(g, base);
    }
    push2(g, "وسيط٠", "وسيط١");
    gen_expr(g, lo);
    push(g, "وسيط٠");
    gen_expr(g, hi);
    ins(g, "نسخ", "مؤقت٢", "وسيط٠", NULL);   /* hi */
    pop(g, "مؤقت٣");                         /* lo */
    pop2(g, "مؤقت٠", "مؤقت١");              /* pointer, length */
    if (checked) {
        int bad = new_label(g), ok = new_label(g);
        emit(g, "    تفرع_إذا_أكبر_بلا_إشارة مؤقت٣، مؤقت٢، ");
        emit_label_name(g, bad);
        emit(g, "\n    تفرع_إذا_أكبر_بلا_إشارة مؤقت٢، مؤقت١، ");
        emit_label_name(g, bad);
        emit(g, "\n");
        jump(g, ok);
        emit_label_def(g, bad);
        stop(g, "_غرة_خارج_الحدود");
        emit_label_def(g, ok);
    }
    ins(g, "طرح", "وسيط١", "مؤقت٢", "مؤقت٣");
    li(g, "مؤقت٤", (int64_t)es);
    ins(g, "ضرب", "مؤقت٣", "مؤقت٣", "مؤقت٤");
    ins(g, "جمع", "وسيط٠", "مؤقت٠", "مؤقت٣");
}

/* ABI: words a value of type t occupies in argument registers */
static int abi_words(const Type *t)
{
    if (t->kind == T_SLICE) return 2;
    if (ty_is_aggregate(t)) {
        uint64_t s = ty_size_of(t);
        return s <= 8 ? 1 : s <= 16 ? 2 : 1;
    }
    return 1;
}

static int is_u32(const Type *t)
{
    t = scalar_repr(t);
    return (t->kind == T_INT && t->bits == 32 && !t->is_signed) || t->kind == T_CHAR;
}

static const char *const areg[16] = {
    "وسيط٠", "وسيط١", "وسيط٢", "وسيط٣", "وسيط٤", "وسيط٥", "وسيط٦", "وسيط٧",
    "وسيط_عشري٠", "وسيط_عشري١", "وسيط_عشري٢", "وسيط_عشري٣",
    "وسيط_عشري٤", "وسيط_عشري٥", "وسيط_عشري٦", "وسيط_عشري٧"
};

/* a real number in an integer register <-> a float register */
#define FM(t, base) ((t)->bits == 32 ? base "_عشري" : base "_مزدوج")
static void to_freg(Gen *g, const char *f, const char *x, const Type *t)
{
    ins(g, FM(t, "نسخ_بتات_إلى"), f, x, NULL);
}
static void from_freg(Gen *g, const char *x, const char *f, const Type *t)
{
    ins(g, FM(t, "نسخ_بتات_من"), x, f, NULL);   /* 32 bits: sign-extended */
}

/* Where each word of each argument travels (RISC-V LP64 ABI):
 * registers وسيط٠…وسيط٧ in order, then the stack in 8-byte words. A
 * two-word argument with only وسيط٧ left is split: first word in وسيط٧,
 * second word on the stack. */
typedef struct {
    int reg[2];       /* register number, or -1 */
    int stack[2];     /* byte offset in the outgoing stack area, or -1 */
    int words;
} ArgPlace;

static int plan_args(Type **params, size_t n, int hidden, ArgPlace *out)
{
    int r = hidden ? 1 : 0, st = 0, fr = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        int w = abi_words(params[i]), k;
        out[i].words = w;
        for (k = 0; k < 2; k++) { out[i].reg[k] = -1; out[i].stack[k] = -1; }
        if (params[i]->kind == T_FLOAT && fr < 8) {
            out[i].reg[0] = 8 + fr++;   /* LP64D: وسيط_عشري٠…٧ first */
            continue;
        }
        for (k = 0; k < w; k++) {
            if (r < 8) {
                out[i].reg[k] = r++;
            } else {
                out[i].stack[k] = st;
                st += 8;
            }
        }
    }
    return (st + 15) / 16 * 16;
}

static void gen_call(Gen *g, Expr *e)
{
    Type *ft = e->a->type;
    Type *rt = ft->elem;
    int direct = (e->a->kind == E_NAME || e->a->kind == E_MEMBER) && e->a->ref == REF_FUNC;
    int hidden = rt != NULL && ty_is_aggregate(rt) && ty_size_of(rt) > 16;
    int32_t ret_slot = 0;
    size_t i, n = e->nargs;
    ArgPlace *place = arena_alloc(g->prog->arena, (n + 1) * sizeof *place);
    int area = plan_args(ft->params, n, hidden, place);
    int64_t temps;

    if (!direct) {
        gen_expr(g, e->a);
        push(g, "وسيط٠");
    }
    if (rt != NULL && ty_is_aggregate(rt)) {
        ret_slot = alloc_slot(g, ty_size_of(rt) < 16 ? 16 : ty_size_of(rt), ty_align_of(rt));
    }
    /* evaluate every argument in reading order; each occupies one 16-byte
     * slot: one word at +0, or two words at +0 and +8 */
    for (i = 0; i < n; i++) {
        Type *pt = ft->params[i];
        gen_expr(g, e->args[i]);
        if (pt->kind == T_SLICE) {
            push2(g, "وسيط٠", "وسيط١");
        } else if (ty_is_aggregate(pt)) {
            uint64_t size = ty_size_of(pt);
            int32_t tmp = alloc_slot(g, size < 16 ? 16 : size, ty_align_of(pt));
            /* copy into a private slot (the callee may modify it) */
            ins(g, "نسخ", "وسيط١", "وسيط٠", NULL);
            add_imm(g, "وسيط٠", "محفوظ٠", tmp);
            if (size < 16) {
                mem_op(g, "خزن_كلمة_مزدوجة", "صفر", "وسيط٠", 0);
                mem_op(g, "خزن_كلمة_مزدوجة", "صفر", "وسيط٠", 8);
            }
            copy_mem(g, size);
            add_imm(g, "وسيط٠", "محفوظ٠", tmp);
            if (size <= 16) {
                mem_op(g, "حمل_كلمة_مزدوجة", "وسيط١", "وسيط٠", 8);
                mem_op(g, "حمل_كلمة_مزدوجة", "وسيط٠", "وسيط٠", 0);
                push2(g, "وسيط٠", "وسيط١");
            } else {
                push(g, "وسيط٠");
            }
        } else {
            push(g, "وسيط٠");
        }
    }
    /* outgoing stack area below the temporaries; argument i's slot is at
     * area + 16 * (n - 1 - i) from the new stack pointer */
    if (area) add_imm(g, "مؤشر_المكدس", "مؤشر_المكدس", -area);
    for (i = 0; i < n; i++) {
        int k;
        int64_t slot = area + 16 * (int64_t)(n - 1 - i);
        for (k = 0; k < place[i].words; k++) {
            const char *dst = place[i].reg[k] >= 0 ? areg[place[i].reg[k]] : "مؤقت٠";
            if (place[i].reg[k] >= 8) {
                /* حمل_عشري boxes a 32-bit value as the ABI wants */
                mem_op(g, FM(ft->params[i], "حمل"), dst, "مؤشر_المكدس", slot);
                continue;
            }
            mem_op(g, "حمل_كلمة_مزدوجة", dst, "مؤشر_المكدس", slot + 8 * k);
            if (place[i].words == 1 && is_u32(ft->params[i])) {
                ins_imm(g, "جمع_فوري_كلمة", dst, dst, 0); /* ABI: 32-bit values sign-extended */
            }
            if (place[i].reg[k] < 0) {
                mem_op(g, "خزن_كلمة_مزدوجة", "مؤقت٠", "مؤشر_المكدس", place[i].stack[k]);
            }
        }
    }
    if (hidden) add_imm(g, "وسيط٠", "محفوظ٠", ret_slot);
    temps = 16 * (int64_t)n;
    if (direct) {
        emit(g, "    استدعاء ");
        emit_sym(g, e->a->decl);
        emit(g, "\n");
    } else {
        mem_op(g, "حمل_كلمة_مزدوجة", "مؤقت٠", "مؤشر_المكدس", area + temps);
        ins(g, "قفز_وربط_بسجل", "عنوان_العودة", "مؤقت٠", NULL);
        temps += 16;
    }
    add_imm(g, "مؤشر_المكدس", "مؤشر_المكدس", area + temps);
    if (rt == NULL) return;
    if (ty_is_aggregate(rt)) {
        if (!hidden) {
            add_imm(g, "مؤقت٠", "محفوظ٠", ret_slot);
            mem_op(g, "خزن_كلمة_مزدوجة", "وسيط٠", "مؤقت٠", 0);
            mem_op(g, "خزن_كلمة_مزدوجة", "وسيط١", "مؤقت٠", 8);
        }
        add_imm(g, "وسيط٠", "محفوظ٠", ret_slot);
    } else if (rt->kind == T_FLOAT) {
        from_freg(g, "وسيط٠", "وسيط_عشري٠", rt);
    } else if (rt->kind != T_SLICE) {
        normalize(g, "وسيط٠", rt);
    }
}

static void gen_compare(Gen *g, Kind k, const Type *t)
{
    /* وسيط٠ = left, وسيط١ = right */
    int sg = is_signed_type(t);
    const char *lt = sg ? "ضبط_إذا_أصغر" : "ضبط_إذا_أصغر_بلا_إشارة";
    switch (k) {
    case K_EQ:
        ins(g, "طرح", "وسيط٠", "وسيط٠", "وسيط١");
        ins_imm(g, "ضبط_إذا_أصغر_فوري_بلا_إشارة", "وسيط٠", "وسيط٠", 1);
        break;
    case K_NE:
        ins(g, "طرح", "وسيط٠", "وسيط٠", "وسيط١");
        ins(g, "ضبط_إذا_أصغر_بلا_إشارة", "وسيط٠", "صفر", "وسيط٠");
        break;
    case K_LT: ins(g, lt, "وسيط٠", "وسيط٠", "وسيط١"); break;
    case K_GT: ins(g, lt, "وسيط٠", "وسيط١", "وسيط٠"); break;
    case K_LE:
        ins(g, lt, "وسيط٠", "وسيط١", "وسيط٠");
        ins_imm(g, "أو_حصري_فوري", "وسيط٠", "وسيط٠", 1);
        break;
    default: /* K_GE */
        ins(g, lt, "وسيط٠", "وسيط٠", "وسيط١");
        ins_imm(g, "أو_حصري_فوري", "وسيط٠", "وسيط٠", 1);
        break;
    }
}

static void gen_arith(Gen *g, Kind k, const Type *t)
{
    /* وسيط٠ = left, وسيط١ = right, result in وسيط٠, then normalised */
    int w32 = bits_of(t) == 32;
    int sg = is_signed_type(t);
    const char *mn = NULL;
    switch (k) {
    case K_PLUS: mn = w32 ? "جمع_كلمة" : "جمع"; break;
    case K_MINUS: mn = w32 ? "طرح_كلمة" : "طرح"; break;
    case K_STAR: mn = w32 ? "ضرب_كلمة" : "ضرب"; break;
    case K_SLASH:
        mn = w32 ? (sg ? "قسمة_كلمة" : "قسمة_بلا_إشارة_كلمة") : (sg ? "قسمة" : "قسمة_بلا_إشارة");
        break;
    case K_REM:
        mn = w32 ? (sg ? "باقي_كلمة" : "باقي_بلا_إشارة_كلمة") : (sg ? "باقي" : "باقي_بلا_إشارة");
        break;
    case K_BAND: mn = "و_منطقي"; break;
    case K_BOR: mn = "أو_منطقي"; break;
    case K_BXOR: mn = "أو_حصري"; break;
    case K_SHL:
    case K_SHR: {
        int bits = bits_of(t);
        ins_imm(g, "و_منطقي_فوري", "وسيط١", "وسيط١", bits - 1); /* mask the amount */
        if (k == K_SHL) mn = w32 ? "رفع_كلمة" : "رفع";
        else if (sg) mn = w32 ? "خفض_حسابي_كلمة" : "خفض_حسابي";
        else mn = w32 ? "خفض_منطقي_كلمة" : "خفض_منطقي";
        break;
    }
    default: break;
    }
    ins(g, mn, "وسيط٠", "وسيط٠", "وسيط١");
    normalize(g, "وسيط٠", t);
}

/* checked / saturating arithmetic */
static void gen_checked_arith(Gen *g, Expr *e)
{
    Kind k = e->tok.kind;
    const Type *t = e->type;
    int sg = is_signed_type(t), bits = bits_of(t);
    int saturate = k == K_ADD_SAT || k == K_SUB_SAT || k == K_MUL_SAT;
    Kind op = (k == K_ADD_CHECKED || k == K_ADD_SAT) ? K_PLUS
            : (k == K_SUB_CHECKED || k == K_SUB_SAT) ? K_MINUS : K_STAR;
    int ok = new_label(g), done = new_label(g);

    gen_expr(g, e->args[0]);
    push(g, "وسيط٠");
    gen_expr(g, e->args[1]);
    ins(g, "نسخ", "وسيط١", "وسيط٠", NULL);
    pop(g, "وسيط٠");
    ins(g, "نسخ", "مؤقت٤", "وسيط٠", NULL);  /* a */
    ins(g, "نسخ", "مؤقت٥", "وسيط١", NULL);  /* b */

    if (bits < 64) {
        /* exact in 64 bits: overflow if the normalised result differs */
        ins(g, op == K_PLUS ? "جمع" : op == K_MINUS ? "طرح" : "ضرب", "وسيط٠", "وسيط٠", "وسيط١");
        ins(g, "نسخ", "مؤقت٠", "وسيط٠", NULL);
        normalize(g, "مؤقت٠", t);
        emit(g, "    تفرع_إذا_يساوي مؤقت٠، وسيط٠، ");
        emit_label_name(g, ok);
        emit(g, "\n");
    } else if (op == K_STAR) {
        ins(g, "ضرب", "وسيط٠", "مؤقت٤", "مؤقت٥");
        if (sg) {
            ins(g, "ضرب_عالي", "مؤقت٠", "مؤقت٤", "مؤقت٥");
            ins_imm(g, "خفض_حسابي_فوري", "مؤقت١", "وسيط٠", 63);
            emit(g, "    تفرع_إذا_يساوي مؤقت٠، مؤقت١، ");
        } else {
            ins(g, "ضرب_عالي_بلا_إشارة", "مؤقت٠", "مؤقت٤", "مؤقت٥");
            emit(g, "    تفرع_إذا_يساوي مؤقت٠، صفر، ");
        }
        emit_label_name(g, ok);
        emit(g, "\n");
    } else {
        ins(g, op == K_PLUS ? "جمع" : "طرح", "وسيط٠", "مؤقت٤", "مؤقت٥");
        if (sg) {
            /* add: overflow if sign(a) == sign(b) != sign(r); sub: sign(a) != sign(b) == sign(r) */
            ins(g, "أو_حصري", "مؤقت٠", "مؤقت٤", "وسيط٠");
            ins(g, "أو_حصري", "مؤقت١", op == K_PLUS ? "مؤقت٥" : "مؤقت٤", op == K_PLUS ? "وسيط٠" : "مؤقت٥");
            ins(g, "و_منطقي", "مؤقت٠", "مؤقت٠", "مؤقت١");
            emit(g, "    تفرع_إذا_أكبر_أو_يساوي مؤقت٠، صفر، ");
        } else if (op == K_PLUS) {
            emit(g, "    تفرع_إذا_أكبر_أو_يساوي_بلا_إشارة وسيط٠، مؤقت٤، ");
        } else {
            emit(g, "    تفرع_إذا_أكبر_أو_يساوي_بلا_إشارة مؤقت٤، مؤقت٥، ");
        }
        emit_label_name(g, ok);
        emit(g, "\n");
    }
    /* overflow */
    if (!saturate) {
        stop(g, "_غرة_فيض");
    } else {
        /* result is the maximum unless it went below the minimum */
        uint64_t max = sg ? (bits == 64 ? (uint64_t)INT64_MAX : ((uint64_t)1 << (bits - 1)) - 1)
                          : (bits == 64 ? UINT64_MAX : ((uint64_t)1 << bits) - 1);
        int64_t min = sg ? (bits == 64 ? INT64_MIN : -((int64_t)1 << (bits - 1))) : 0;
        int to_min = new_label(g);
        if (!sg) {
            if (op == K_MINUS) { li(g, "وسيط٠", 0); jump(g, done); }
            else { li(g, "وسيط٠", (int64_t)max); jump(g, done); }
        } else {
            if (op == K_PLUS) {
                emit(g, "    تفرع_إذا_أصغر مؤقت٥، صفر، ");
            } else if (op == K_MINUS) {
                emit(g, "    تفرع_إذا_أكبر_أو_يساوي مؤقت٥، صفر، ");
            } else {
                ins(g, "أو_حصري", "مؤقت٠", "مؤقت٤", "مؤقت٥");
                emit(g, "    تفرع_إذا_أصغر مؤقت٠، صفر، ");
            }
            emit_label_name(g, to_min);
            emit(g, "\n");
            li(g, "وسيط٠", (int64_t)max);
            jump(g, done);
            emit_label_def(g, to_min);
            li(g, "وسيط٠", min);
            jump(g, done);
        }
    }
    emit_label_def(g, ok);
    normalize(g, "وسيط٠", t);
    emit_label_def(g, done);
}

static void gen_checked_convert(Gen *g, Expr *e)
{
    const Type *from = e->a->type, *to = e->type;
    int bad = new_label(g), ok = new_label(g);
    gen_expr(g, e->a);
    if (to->kind == T_ENUM) {
        size_t i;
        Decl *d = to->decl;
        for (i = 0; i < d->nmembers; i++) {
            li(g, "مؤقت٠", (int64_t)cv_bits(d->members[i].value));
            emit(g, "    تفرع_إذا_يساوي وسيط٠، مؤقت٠، ");
            emit_label_name(g, ok);
            emit(g, "\n");
        }
        jump(g, bad);
    } else {
        int fsg = is_signed_type(from);
        const Type *tt = to;
        uint64_t max;
        int64_t min = 0;
        int check_min = 0;
        if (to->kind == T_CHAR) {
            max = 0x10FFFF;
        } else {
            int tb = tt->is_size ? 64 : tt->bits;
            max = tt->is_signed ? ((uint64_t)1 << (tb - 1)) - 1 : (tb == 64 ? UINT64_MAX : ((uint64_t)1 << tb) - 1);
            if (tt->is_signed) min = tb == 64 ? INT64_MIN : -((int64_t)1 << (tb - 1));
        }
        if (fsg) {
            /* negative values: below the minimum, or any for unsigned targets */
            int nonneg = new_label(g);
            emit(g, "    تفرع_إذا_أكبر_أو_يساوي وسيط٠، صفر، ");
            emit_label_name(g, nonneg);
            emit(g, "\n");
            if (to->kind == T_INT && to->is_signed) {
                check_min = 1;
                li(g, "مؤقت٠", min);
                emit(g, "    تفرع_إذا_أصغر وسيط٠، مؤقت٠، ");
                emit_label_name(g, bad);
                emit(g, "\n");
                jump(g, ok);
            } else {
                jump(g, bad);
            }
            emit_label_def(g, nonneg);
        }
        (void)check_min;
        /* non-negative: compare as unsigned against the maximum */
        li(g, "مؤقت٠", (int64_t)max);
        emit(g, "    تفرع_إذا_أكبر_بلا_إشارة وسيط٠، مؤقت٠، ");
        emit_label_name(g, bad);
        emit(g, "\n");
        if (to->kind == T_CHAR) {
            li(g, "مؤقت٠", 0xD800);
            emit(g, "    تفرع_إذا_أصغر_بلا_إشارة وسيط٠، مؤقت٠، ");
            emit_label_name(g, ok);
            emit(g, "\n");
            li(g, "مؤقت٠", 0xDFFF);
            emit(g, "    تفرع_إذا_أكبر_بلا_إشارة وسيط٠، مؤقت٠، ");
            emit_label_name(g, ok);
            emit(g, "\n");
            jump(g, bad);
        } else {
            jump(g, ok);
        }
    }
    emit_label_def(g, bad);
    stop(g, "_غرة_تحويل_فاشل");
    emit_label_def(g, ok);
    normalize(g, "وسيط٠", to);
}

/* ------------------------------------------------------------------ */
/* Real numbers (decisions 66-68). Values travel as bits in وسيط٠; each
 * operation moves them to مؤقت_عشري… and back. Every rounding instruction
 * names أقرب_زوجي, so the result never depends on نمط_التقريب. */

static void real_op(Gen *g, const char *mn, const char *a, const char *b, const char *c, const char *d)
{
    emit(g, "    ");
    emit(g, mn);
    emit(g, " ");
    emit(g, a);
    emit(g, "، ");
    emit(g, b);
    if (c) { emit(g, "، "); emit(g, c); }
    if (d) { emit(g, "، "); emit(g, d); }
    emit(g, "\n");
}

/* stop with routine unless (the flags raised since the last clear & mask) == 0 */
static void clear_flags(Gen *g)
{
    ins(g, "كتابة_تحكم", "أعلام_العشري", "صفر", NULL);
}
static void stop_on_flags(Gen *g, int mask, const char *routine)
{
    int ok = new_label(g);
    ins(g, "قراءة_تحكم", "مؤقت٠", "أعلام_العشري", NULL);
    ins_imm(g, "و_منطقي_فوري", "مؤقت٠", "مؤقت٠", mask);
    jump_if(g, "مؤقت٠", 0, ok);
    stop(g, routine);
    emit_label_def(g, ok);
}

#define FLAG_NX 1
#define FLAG_NV 16

/* وسيط٠ = left, وسيط١ = right, both of real type t */
static void gen_real_binary(Gen *g, Kind k, const Type *t)
{
    to_freg(g, "مؤقت_عشري٠", "وسيط٠", t);
    to_freg(g, "مؤقت_عشري١", "وسيط١", t);
    if (k == K_PLUS || k == K_MINUS || k == K_STAR || k == K_SLASH) {
        const char *mn = k == K_PLUS ? FM(t, "جمع") : k == K_MINUS ? FM(t, "طرح")
                       : k == K_STAR ? FM(t, "ضرب") : FM(t, "قسمة");
        real_op(g, mn, "مؤقت_عشري٠", "مؤقت_عشري٠", "مؤقت_عشري١", "أقرب_زوجي");
        from_freg(g, "وسيط٠", "مؤقت_عشري٠", t);
        return;
    }
    {
        /* decision 67: comparing a NaN stops the program */
        int ok = new_label(g);
        ins(g, FM(t, "ضبط_إذا_يساوي"), "مؤقت٠", "مؤقت_عشري٠", "مؤقت_عشري٠");
        ins(g, FM(t, "ضبط_إذا_يساوي"), "مؤقت١", "مؤقت_عشري١", "مؤقت_عشري١");
        ins(g, "و_منطقي", "مؤقت٠", "مؤقت٠", "مؤقت١");
        jump_if(g, "مؤقت٠", 1, ok);
        stop(g, "_غرة_ليس_عددا");
        emit_label_def(g, ok);
    }
    switch (k) {
    case K_EQ: ins(g, FM(t, "ضبط_إذا_يساوي"), "وسيط٠", "مؤقت_عشري٠", "مؤقت_عشري١"); break;
    case K_NE:
        ins(g, FM(t, "ضبط_إذا_يساوي"), "وسيط٠", "مؤقت_عشري٠", "مؤقت_عشري١");
        ins_imm(g, "أو_حصري_فوري", "وسيط٠", "وسيط٠", 1);
        break;
    case K_LT: ins(g, FM(t, "ضبط_إذا_أصغر"), "وسيط٠", "مؤقت_عشري٠", "مؤقت_عشري١"); break;
    case K_GT: ins(g, FM(t, "ضبط_إذا_أصغر"), "وسيط٠", "مؤقت_عشري١", "مؤقت_عشري٠"); break;
    case K_LE: ins(g, FM(t, "ضبط_إذا_أصغر_أو_يساوي"), "وسيط٠", "مؤقت_عشري٠", "مؤقت_عشري١"); break;
    default: ins(g, FM(t, "ضبط_إذا_أصغر_أو_يساوي"), "وسيط٠", "مؤقت_عشري١", "مؤقت_عشري٠"); break;
    }
}

static void gen_real_builtin(Gen *g, Expr *e)
{
    Kind k = e->tok.kind;
    const Type *to = e->type, *from;
    if (k == K_IS_NAN) {
        from = e->args[0]->type;
        gen_expr(g, e->args[0]);
        to_freg(g, "مؤقت_عشري٠", "وسيط٠", from);
        ins(g, FM(from, "ضبط_إذا_يساوي"), "وسيط٠", "مؤقت_عشري٠", "مؤقت_عشري٠");
        ins_imm(g, "أو_حصري_فوري", "وسيط٠", "وسيط٠", 1);
        return;
    }
    if (k == K_FMA) {
        gen_expr(g, e->args[0]);
        push(g, "وسيط٠");
        gen_expr(g, e->args[1]);
        push(g, "وسيط٠");
        gen_expr(g, e->args[2]);
        to_freg(g, "مؤقت_عشري٢", "وسيط٠", to);
        pop(g, "وسيط٠");
        to_freg(g, "مؤقت_عشري١", "وسيط٠", to);
        pop(g, "وسيط٠");
        to_freg(g, "مؤقت_عشري٠", "وسيط٠", to);
        emit(g, "    ");
        emit(g, FM(to, "ضرب_جمع"));
        emit(g, " مؤقت_عشري٠، مؤقت_عشري٠، مؤقت_عشري١، مؤقت_عشري٢، أقرب_زوجي\n");
        from_freg(g, "وسيط٠", "مؤقت_عشري٠", to);
        return;
    }
    from = e->a->type;
    gen_expr(g, e->a);
    if (k == K_REINTERPRET) {
        normalize(g, "وسيط٠", to);
        return;
    }
    if (from->kind == T_FLOAT && to->kind == T_FLOAT) {
        if (from->bits == to->bits) return;
        to_freg(g, "مؤقت_عشري٠", "وسيط٠", from);
        if (to->bits == 64) {
            ins(g, "تحويل_مزدوج_من_عشري", "مؤقت_عشري٠", "مؤقت_عشري٠", NULL); /* exact */
        } else {
            if (k == K_CONVERT_CHECKED) clear_flags(g);
            real_op(g, "تحويل_عشري_من_مزدوج", "مؤقت_عشري٠", "مؤقت_عشري٠", "أقرب_زوجي", NULL);
            if (k == K_CONVERT_CHECKED) stop_on_flags(g, FLAG_NX, "_غرة_تحويل_فاشل");
        }
        from_freg(g, "وسيط٠", "مؤقت_عشري٠", to);
        return;
    }
    if (to->kind == T_FLOAT) {
        /* integer to real: the integer is canonical in 64 bits */
        int u64 = !from->is_signed && (from->is_size || from->bits == 64);
        const char *mn = to->bits == 32
            ? (u64 ? "تحويل_عشري_من_كلمة_مزدوجة_بلا_إشارة" : "تحويل_عشري_من_كلمة_مزدوجة")
            : (u64 ? "تحويل_مزدوج_من_كلمة_مزدوجة_بلا_إشارة" : "تحويل_مزدوج_من_كلمة_مزدوجة");
        if (k == K_CONVERT_CHECKED) clear_flags(g);
        real_op(g, mn, "مؤقت_عشري٠", "وسيط٠", "أقرب_زوجي", NULL);
        if (k == K_CONVERT_CHECKED) stop_on_flags(g, FLAG_NX, "_غرة_تحويل_فاشل");
        from_freg(g, "وسيط٠", "مؤقت_عشري٠", to);
        return;
    }
    {
        /* real to integer: a 64-bit conversion with the asked rounding,
         * stopping on NaN or overflow (NV), then the range of the target */
        int sg = to->is_signed;
        const char *rm = k == K_ROUND_NEAREST ? "أقرب_زوجي" : k == K_FLOOR ? "للأسفل"
                       : k == K_CEIL ? "للأعلى" : "نحو_الصفر";
        const char *mn = from->bits == 32
            ? (sg ? "تحويل_كلمة_مزدوجة_من_عشري" : "تحويل_كلمة_مزدوجة_بلا_إشارة_من_عشري")
            : (sg ? "تحويل_كلمة_مزدوجة_من_مزدوج" : "تحويل_كلمة_مزدوجة_بلا_إشارة_من_مزدوج");
        to_freg(g, "مؤقت_عشري٠", "وسيط٠", from);
        clear_flags(g);
        real_op(g, mn, "وسيط٠", "مؤقت_عشري٠", rm, NULL);
        stop_on_flags(g, k == K_CONVERT_CHECKED ? FLAG_NV | FLAG_NX : FLAG_NV, "_غرة_تحويل_فاشل");
        if (!to->is_size && to->bits < 64) {
            int ok = new_label(g);
            ins(g, "نسخ", "مؤقت١", "وسيط٠", NULL);
            normalize(g, "مؤقت١", to);
            emit(g, "    تفرع_إذا_يساوي وسيط٠، مؤقت١، ");
            emit_label_name(g, ok);
            emit(g, "\n");
            stop(g, "_غرة_تحويل_فاشل");
            emit_label_def(g, ok);
        }
    }
}

static void gen_aggregate_literal(Gen *g, Expr *e)
{
    Type *t = e->type;
    int32_t slot = alloc_slot(g, ty_size_of(t), ty_align_of(t));
    size_t i;
    if (e->kind == E_STRUCT_LIT) {
        Decl *d = t->decl;
        for (i = 0; i < e->nargs; i++) {
            size_t f;
            for (f = 0; f < d->nfields; f++) {
                if (bytes_eq(d->fields[f].name, d->fields[f].len, e->names[i], e->name_lens[i])) break;
            }
            gen_expr(g, e->args[i]);
            add_imm(g, "مؤقت٢", "محفوظ٠", slot + (int64_t)d->fields[f].offset);
            if (d->packed) store_value_unaligned(g, "مؤقت٢", d->fields[f].type);
            else store_value(g, "مؤقت٢", d->fields[f].type);
        }
    } else if (e->kind == E_ARRAY_LIT) {
        uint64_t es = ty_size_of(t->elem);
        for (i = 0; i < e->nargs; i++) {
            gen_expr(g, e->args[i]);
            add_imm(g, "مؤقت٢", "محفوظ٠", slot + (int64_t)(es * i));
            store_value(g, "مؤقت٢", t->elem);
        }
    } else {
        /* ]قيمة مكرر[ */
        uint64_t es = ty_size_of(t->elem);
        int top = new_label(g), end = new_label(g);
        int32_t idx = alloc_slot(g, 8, 8);
        int32_t val = alloc_slot(g, 16, 8);
        gen_expr(g, e->args[0]);
        if (t->elem->kind == T_SLICE) {
            mem_op(g, "خزن_كلمة_مزدوجة", "وسيط٠", "محفوظ٠", val);
            mem_op(g, "خزن_كلمة_مزدوجة", "وسيط١", "محفوظ٠", val + 8);
        } else {
            mem_op(g, "خزن_كلمة_مزدوجة", "وسيط٠", "محفوظ٠", val); /* scalar, or address */
        }
        mem_op(g, "خزن_كلمة_مزدوجة", "صفر", "محفوظ٠", idx);
        emit_label_def(g, top);
        mem_op(g, "حمل_كلمة_مزدوجة", "مؤقت٠", "محفوظ٠", idx);
        li(g, "مؤقت١", (int64_t)t->count);
        emit(g, "    تفرع_إذا_أكبر_أو_يساوي_بلا_إشارة مؤقت٠، مؤقت١، ");
        emit_label_name(g, end);
        emit(g, "\n");
        li(g, "مؤقت١", (int64_t)es);
        ins(g, "ضرب", "مؤقت٠", "مؤقت٠", "مؤقت١");
        add_imm(g, "مؤقت٢", "محفوظ٠", slot);
        ins(g, "جمع", "مؤقت٢", "مؤقت٢", "مؤقت٠");
        mem_op(g, "حمل_كلمة_مزدوجة", "وسيط٠", "محفوظ٠", val);
        if (t->elem->kind == T_SLICE) mem_op(g, "حمل_كلمة_مزدوجة", "وسيط١", "محفوظ٠", val + 8);
        store_value(g, "مؤقت٢", t->elem);
        mem_op(g, "حمل_كلمة_مزدوجة", "مؤقت٠", "محفوظ٠", idx);
        ins_imm(g, "جمع_فوري", "مؤقت٠", "مؤقت٠", 1);
        mem_op(g, "خزن_كلمة_مزدوجة", "مؤقت٠", "محفوظ٠", idx);
        jump(g, top);
        emit_label_def(g, end);
    }
    add_imm(g, "وسيط٠", "محفوظ٠", slot);
}

static void gen_decl_ref(Gen *g, Expr *e)
{
    switch (e->ref) {
    case REF_GLOBAL:
        emit(g, "    حمل_عنوان وسيط٠، ");
        emit_sym(g, e->decl);
        emit(g, "\n");
        if (e->decl->is_volatile) emit(g, "    سياج\n");
        load_value(g, e->type);
        if (e->decl->is_volatile) emit(g, "    سياج\n");
        return;
    case REF_FUNC:
        emit(g, "    حمل_عنوان وسيط٠، ");
        emit_sym(g, e->decl);
        emit(g, "\n");
        return;
    case REF_CONST:
        gen_expr(g, e->decl->init); /* a string constant */
        return;
    default:
        return;
    }
}

static void gen_expr(Gen *g, Expr *e)
{
    if (e->is_const && e->type->kind != T_SLICE && !ty_is_aggregate(e->type)) {
        if (e->type->kind == T_FLOAT && e->type->bits == 32) {
            li(g, "وسيط٠", (int64_t)(int32_t)(uint32_t)e->cval.mag); /* canonical: sign-extended */
            return;
        }
        li(g, "وسيط٠", (int64_t)cv_bits(e->cval));
        return;
    }
    switch (e->kind) {
    case E_STRING:
        gen_string(g, e);
        return;
    case E_NULL:
        li(g, "وسيط٠", 0);
        return;
    case E_NAME:
        if (e->ref == REF_LOCAL) {
            add_imm(g, "وسيط٠", "محفوظ٠", local_slot(g, e->local));
            load_value(g, e->type);
        } else {
            gen_decl_ref(g, e);
        }
        return;
    case E_MEMBER:
        if (e->ref == REF_LENGTH) {
            gen_expr(g, e->a);
            ins(g, "نسخ", "وسيط٠", "وسيط١", NULL);
        } else if (e->ref == REF_FIELD) {
            gen_addr(g, e);
            load_lvalue(g, e);
        } else {
            gen_decl_ref(g, e);
        }
        return;
    case E_INDEX:
    case E_DEREF:
        gen_addr(g, e);
        if (expr_is_volatile(e)) { /* decision 41: fences on both sides */
            emit(g, "    سياج\n");
            load_lvalue(g, e);
            emit(g, "    سياج\n");
        } else {
            load_lvalue(g, e);
        }
        return;
    case E_ADDR:
        gen_addr(g, e->a);
        return;
    case E_SLICE:
        gen_slice(g, e->a, e->b, e->c, 0);
        return;
    case E_CALL:
        gen_call(g, e);
        return;
    case E_UNARY:
        gen_expr(g, e->a);
        if (e->tok.kind == K_NOT) {
            ins_imm(g, "أو_حصري_فوري", "وسيط٠", "وسيط٠", 1);
        } else if (e->tok.kind == K_MINUS && e->type->kind == T_FLOAT) {
            /* flip the sign bit (bits 31..63 for the sign-extended 32-bit form) */
            if (e->type->bits == 32) {
                li(g, "مؤقت٠", -2147483648LL);
            } else {
                li(g, "مؤقت٠", 1);
                ins_imm(g, "رفع_فوري", "مؤقت٠", "مؤقت٠", 63);
            }
            ins(g, "أو_حصري", "وسيط٠", "وسيط٠", "مؤقت٠");
        } else if (e->tok.kind == K_MINUS) {
            ins(g, "طرح", "وسيط٠", "صفر", "وسيط٠");
            normalize(g, "وسيط٠", e->type);
        } else {
            ins_imm(g, "أو_حصري_فوري", "وسيط٠", "وسيط٠", -1);
            normalize(g, "وسيط٠", e->type);
        }
        return;
    case E_BINARY: {
        Kind k = e->tok.kind;
        if (k == K_AND || k == K_OR) {
            int end = new_label(g);
            gen_expr(g, e->a);
            jump_if(g, "وسيط٠", k == K_OR, end);
            gen_expr(g, e->b);
            emit_label_def(g, end);
            return;
        }
        gen_expr(g, e->a);
        push(g, "وسيط٠");
        gen_expr(g, e->b);
        ins(g, "نسخ", "وسيط١", "وسيط٠", NULL);
        pop(g, "وسيط٠");
        if (e->a->type->kind == T_FLOAT) {
            gen_real_binary(g, k, e->a->type);
        } else if (k == K_EQ || k == K_NE || k == K_LT || k == K_GT || k == K_LE || k == K_GE) {
            gen_compare(g, k, e->a->type);
        } else {
            gen_arith(g, k, e->type);
        }
        return;
    }
    case E_STRUCT_LIT:
    case E_ARRAY_LIT:
    case E_ARRAY_REPEAT:
        gen_aggregate_literal(g, e);
        return;
    case E_BUILTIN:
        if (e->tok.kind == K_IS_NAN || e->tok.kind == K_FMA || e->tok.kind == K_ROUND ||
            e->tok.kind == K_ROUND_NEAREST || e->tok.kind == K_FLOOR || e->tok.kind == K_CEIL ||
            ((e->tok.kind == K_CONVERT || e->tok.kind == K_TRUNCATE || e->tok.kind == K_REINTERPRET ||
              e->tok.kind == K_CONVERT_CHECKED) &&
             (e->type->kind == T_FLOAT || e->a->type->kind == T_FLOAT))) {
            gen_real_builtin(g, e);
            return;
        }
        switch (e->tok.kind) {
        case K_CONVERT:
        case K_TRUNCATE:
            gen_expr(g, e->a);
            normalize(g, "وسيط٠", e->type);
            return;
        case K_REINTERPRET:
            gen_expr(g, e->a);
            return;
        case K_CONVERT_CHECKED:
            gen_checked_convert(g, e);
            return;
        case K_INDEX_CHECKED:
            gen_element_addr(g, e->args[0], e->args[1], 1);
            load_lvalue(g, e);
            return;
        case K_SLICE_CHECKED:
            gen_slice(g, e->args[0], e->args[1], e->args[2], 1);
            return;
        case K_SLICE_FROM:
            gen_expr(g, e->args[0]);
            push(g, "وسيط٠");
            gen_expr(g, e->args[1]);
            ins(g, "نسخ", "وسيط١", "وسيط٠", NULL);
            pop(g, "وسيط٠");
            return;
        default:
            gen_checked_arith(g, e);
            return;
        }
    default:
        return;
    }
}

/* address of an lvalue (or of an aggregate value) into وسيط٠ */
static void gen_addr(Gen *g, Expr *e)
{
    switch (e->kind) {
    case E_NAME:
        if (e->ref == REF_LOCAL) {
            add_imm(g, "وسيط٠", "محفوظ٠", local_slot(g, e->local));
        } else {
            emit(g, "    حمل_عنوان وسيط٠، ");
            emit_sym(g, e->decl);
            emit(g, "\n");
        }
        return;
    case E_MEMBER:
        if (e->ref == REF_FIELD) {
            Field *f = &e->a->type->decl->fields[e->member_index];
            if (ty_is_aggregate(e->a->type) && e->a->kind != E_NAME && e->a->kind != E_MEMBER &&
                e->a->kind != E_INDEX && e->a->kind != E_DEREF) {
                gen_expr(g, e->a); /* an aggregate value: its address */
            } else {
                gen_addr(g, e->a);
            }
            if (f->offset) add_imm(g, "وسيط٠", "وسيط٠", (int64_t)f->offset);
        } else if (e->ref == REF_GLOBAL) {
            emit(g, "    حمل_عنوان وسيط٠، ");
            emit_sym(g, e->decl);
            emit(g, "\n");
        }
        return;
    case E_INDEX:
        gen_element_addr(g, e->a, e->b, 0);
        return;
    case E_BUILTIN: /* عنصر_بفحص */
        gen_element_addr(g, e->args[0], e->args[1], 1);
        return;
    case E_DEREF:
        gen_expr(g, e->a);
        return;
    default:
        gen_expr(g, e); /* aggregate values already are addresses */
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Statements                                                          */

static void gen_stmt(Gen *g, Stmt *s);

static void gen_block(Gen *g, Stmt *b)
{
    size_t i;
    DbgScope sc;
    int saved = g->cur_scope;
    if (g->debug) {
        sc.parent = g->cur_scope;
        sc.start = new_label(g);
        sc.end = new_label(g);
        emit_label_def(g, sc.start);
        buf_bytes(&g->scopes, &sc, sizeof sc);
        g->cur_scope = g->nscopes++;
    }
    for (i = 0; i < b->nstmts; i++) gen_stmt(g, b->stmts[i]);
    if (g->debug) {
        emit_label_def(g, sc.end);
        g->cur_scope = saved;
    }
}

static void dbg_var(Gen *g, Local *l, int param)
{
    DbgVar v;
    if (!g->debug || l == NULL) return;
    v.local = l;
    v.scope = g->cur_scope;
    v.param = param;
    buf_bytes(&g->vars, &v, sizeof v);
}

static void assign_to_local(Gen *g, Local *l, Expr *value)
{
    gen_expr(g, value);
    add_imm(g, "مؤقت٢", "محفوظ٠", local_slot(g, l));
    store_value(g, "مؤقت٢", l->type);
}

static void gen_loop_body(Gen *g, Stmt *body, int brk, int cont)
{
    g->break_label[g->loop_depth] = brk;
    g->cont_label[g->loop_depth] = cont;
    g->loop_depth++;
    gen_stmt(g, body);
    g->loop_depth--;
}

static void gen_stmt(Gen *g, Stmt *s)
{
    if (g->debug && s->kind != S_BLOCK) {
        emit(g, ".موضع ");
        emit_num(g, g->fn->module->debug_file);
        emit(g, "، ");
        emit_num(g, s->tok.line);
        emit(g, "، ");
        emit_num(g, s->tok.col);
        emit(g, "\n");
    }
    if (s->kind == S_VAR || s->kind == S_IF_FOUND || s->kind == S_FOR_RANGE) dbg_var(g, s->local, 0);
    if (s->kind == S_FOR_EACH) {
        dbg_var(g, s->local, 0);
        dbg_var(g, s->local2, 0);
    }
    switch (s->kind) {
    case S_BLOCK:
        gen_block(g, s);
        return;
    case S_VAR:
        if (!s->uninit) assign_to_local(g, s->local, s->e1);
        return;
    case S_CONST:
        return;
    case S_ASSIGN:
        gen_addr(g, s->e1);
        push(g, "وسيط٠");
        gen_expr(g, s->e2);
        pop(g, "مؤقت٢");
        if (expr_is_volatile(s->e1)) emit(g, "    سياج\n");
        if (in_packed(s->e1)) store_value_unaligned(g, "مؤقت٢", s->e1->type);
        else store_value(g, "مؤقت٢", s->e1->type);
        if (expr_is_volatile(s->e1)) emit(g, "    سياج\n");
        return;
    case S_EXPR:
        gen_expr(g, s->e1);
        return;
    case S_IF: {
        int els = new_label(g), end = new_label(g);
        gen_expr(g, s->e1);
        jump_if(g, "وسيط٠", 0, els);
        gen_stmt(g, s->then_s);
        jump(g, end);
        emit_label_def(g, els);
        if (s->else_s) gen_stmt(g, s->else_s);
        emit_label_def(g, end);
        return;
    }
    case S_IF_FOUND: {
        int els = new_label(g), end = new_label(g);
        gen_expr(g, s->e1);
        jump_if(g, "وسيط٠", 0, els);
        add_imm(g, "مؤقت٢", "محفوظ٠", local_slot(g, s->local));
        mem_op(g, "خزن_كلمة_مزدوجة", "وسيط٠", "مؤقت٢", 0);
        gen_stmt(g, s->then_s);
        jump(g, end);
        emit_label_def(g, els);
        if (s->else_s) gen_stmt(g, s->else_s);
        emit_label_def(g, end);
        return;
    }
    case S_WHILE: {
        int top = new_label(g), end = new_label(g);
        emit_label_def(g, top);
        gen_expr(g, s->e1);
        jump_if(g, "وسيط٠", 0, end);
        gen_loop_body(g, s->then_s, end, top);
        jump(g, top);
        emit_label_def(g, end);
        return;
    }
    case S_DO: {
        int top = new_label(g), cont = new_label(g), end = new_label(g);
        emit_label_def(g, top);
        gen_loop_body(g, s->then_s, end, cont);
        emit_label_def(g, cont);
        gen_expr(g, s->e1);
        jump_if(g, "وسيط٠", 1, top);
        emit_label_def(g, end);
        return;
    }
    case S_FOR_RANGE: {
        int top = new_label(g), cont = new_label(g), end = new_label(g);
        Local *v = s->local;
        int32_t lim;
        assign_to_local(g, v, s->e1);
        gen_expr(g, s->e2);
        lim = alloc_slot(g, 8, 8);
        mem_op(g, "خزن_كلمة_مزدوجة", "وسيط٠", "محفوظ٠", lim);
        emit_label_def(g, top);
        load_scalar(g, "وسيط٠", "محفوظ٠", local_slot(g, v), v->type);
        mem_op(g, "حمل_كلمة_مزدوجة", "وسيط١", "محفوظ٠", lim);
        gen_compare(g, K_LT, v->type);
        jump_if(g, "وسيط٠", 0, end);
        gen_loop_body(g, s->then_s, end, cont);
        emit_label_def(g, cont);
        load_scalar(g, "وسيط٠", "محفوظ٠", local_slot(g, v), v->type);
        ins_imm(g, "جمع_فوري", "وسيط٠", "وسيط٠", 1);
        store_scalar(g, "وسيط٠", "محفوظ٠", local_slot(g, v), v->type);
        jump(g, top);
        emit_label_def(g, end);
        return;
    }
    case S_FOR_EACH: {
        int top = new_label(g), cont = new_label(g), end = new_label(g);
        Type *seq = s->e1->type;
        uint64_t es = ty_size_of(seq->elem);
        int32_t base = alloc_slot(g, 8, 8), len = alloc_slot(g, 8, 8), idx = alloc_slot(g, 8, 8);
        if (seq->kind == T_ARRAY) {
            gen_addr(g, s->e1);
            li(g, "وسيط١", (int64_t)seq->count);
        } else {
            gen_expr(g, s->e1);
        }
        mem_op(g, "خزن_كلمة_مزدوجة", "وسيط٠", "محفوظ٠", base);
        mem_op(g, "خزن_كلمة_مزدوجة", "وسيط١", "محفوظ٠", len);
        mem_op(g, "خزن_كلمة_مزدوجة", "صفر", "محفوظ٠", idx);
        emit_label_def(g, top);
        mem_op(g, "حمل_كلمة_مزدوجة", "مؤقت٠", "محفوظ٠", idx);
        mem_op(g, "حمل_كلمة_مزدوجة", "مؤقت١", "محفوظ٠", len);
        emit(g, "    تفرع_إذا_أصغر_بلا_إشارة مؤقت٠، مؤقت١، ");
        {
            int body = new_label(g);
            emit_label_name(g, body);
            emit(g, "\n");
            jump(g, end);
            emit_label_def(g, body);
        }
        if (s->local2) store_scalar(g, "مؤقت٠", "محفوظ٠", local_slot(g, s->local2), ty_size);
        li(g, "مؤقت١", (int64_t)es);
        ins(g, "ضرب", "مؤقت٠", "مؤقت٠", "مؤقت١");
        mem_op(g, "حمل_كلمة_مزدوجة", "وسيط٠", "محفوظ٠", base);
        ins(g, "جمع", "وسيط٠", "وسيط٠", "مؤقت٠");
        load_value(g, seq->elem);
        add_imm(g, "مؤقت٢", "محفوظ٠", local_slot(g, s->local));
        store_value(g, "مؤقت٢", seq->elem);
        gen_loop_body(g, s->then_s, end, cont);
        emit_label_def(g, cont);
        mem_op(g, "حمل_كلمة_مزدوجة", "مؤقت٠", "محفوظ٠", idx);
        ins_imm(g, "جمع_فوري", "مؤقت٠", "مؤقت٠", 1);
        mem_op(g, "خزن_كلمة_مزدوجة", "مؤقت٠", "محفوظ٠", idx);
        jump(g, top);
        emit_label_def(g, end);
        return;
    }
    case S_SWITCH: {
        int end = new_label(g), dflt = -1;
        int32_t v = alloc_slot(g, 8, 8);
        size_t i, j;
        int *labels = arena_alloc(g->prog->arena, (s->ncases + 1) * sizeof(int));
        gen_expr(g, s->e1);
        mem_op(g, "خزن_كلمة_مزدوجة", "وسيط٠", "محفوظ٠", v);
        for (i = 0; i < s->ncases; i++) {
            labels[i] = new_label(g);
            if (s->cases[i].values == NULL) { dflt = labels[i]; continue; }
            for (j = 0; j < s->cases[i].nvalues; j++) {
                int nxt = new_label(g);
                mem_op(g, "حمل_كلمة_مزدوجة", "وسيط٠", "محفوظ٠", v);
                li(g, "مؤقت٠", (int64_t)cv_bits(s->cases[i].values[j]->cval));
                emit(g, "    تفرع_إذا_لا_يساوي وسيط٠، مؤقت٠، ");
                emit_label_name(g, nxt);
                emit(g, "\n");
                jump(g, labels[i]);
                emit_label_def(g, nxt);
            }
        }
        jump(g, dflt >= 0 ? dflt : end);
        for (i = 0; i < s->ncases; i++) {
            emit_label_def(g, labels[i]);
            gen_stmt(g, s->cases[i].body);
            jump(g, end);
        }
        emit_label_def(g, end);
        return;
    }
    case S_BREAK:
        jump(g, g->break_label[g->loop_depth - 1]);
        return;
    case S_CONTINUE:
        jump(g, g->cont_label[g->loop_depth - 1]);
        return;
    case S_RETURN: {
        Type *rt = g->fn->ret;
        if (s->e1 != NULL) {
            gen_expr(g, s->e1);
            if (ty_is_aggregate(rt)) {
                uint64_t size = ty_size_of(rt);
                if (size > 16) {
                    ins(g, "نسخ", "وسيط١", "وسيط٠", NULL);
                    mem_op(g, "حمل_كلمة_مزدوجة", "وسيط٠", "محفوظ٠", g->hidden_ret_slot);
                    copy_mem(g, size);
                } else {
                    int32_t tmp = alloc_slot(g, 16, ty_align_of(rt));
                    ins(g, "نسخ", "وسيط١", "وسيط٠", NULL);
                    add_imm(g, "وسيط٠", "محفوظ٠", tmp);
                    mem_op(g, "خزن_كلمة_مزدوجة", "صفر", "وسيط٠", 0);
                    mem_op(g, "خزن_كلمة_مزدوجة", "صفر", "وسيط٠", 8);
                    copy_mem(g, size);
                    add_imm(g, "مؤقت٠", "محفوظ٠", tmp);
                    mem_op(g, "حمل_كلمة_مزدوجة", "وسيط٠", "مؤقت٠", 0);
                    mem_op(g, "حمل_كلمة_مزدوجة", "وسيط١", "مؤقت٠", 8);
                }
            } else if (rt->kind == T_FLOAT) {
                to_freg(g, "وسيط_عشري٠", "وسيط٠", rt);
            } else if (is_u32(rt)) {
                ins_imm(g, "جمع_فوري_كلمة", "وسيط٠", "وسيط٠", 0); /* ABI */
            }
        }
        jump(g, g->ret_label);
        return;
    }
    }
}

/* ------------------------------------------------------------------ */
/* Functions and data                                                  */

/* ------------------------------------------------------------------ */
/* Debug rows (--تنقيح)                                               */

static void emit_quoted(Gen *g, const uint8_t *s, size_t n)
{
    emit(g, "\"");
    emitb(g, s, n);
    emit(g, "\"");
}

static void emit_index(Gen *g, int i)
{
    if (i < 0) emit(g, "بلا");
    else emit_num(g, i);
}

/* the row of type t in the types table, added (with what it refers to) if new */
static int dbg_type(Gen *g, const Type *t)
{
    Type **map = (Type **)g->tymap.data;
    size_t n = g->tymap.len / sizeof *map, i;
    int idx, a = 0, first = 0, count = 0;
    Buf *saved = g->out;
    Buf line;
    for (i = 0; i < n; i++) {
        if (ty_equal(map[i], t) && map[i]->kind == t->kind) return (int)i;
    }
    idx = (int)n;
    buf_bytes(&g->tymap, &t, sizeof t);
    {
        DbgLine empty = {NULL, 0};
        buf_bytes(&g->types, &empty, sizeof empty);   /* filled below */
    }
    buf_init(&line);
    /* what the row refers to comes first, so nested fields stay together */
    switch (t->kind) {
    case T_PTR: case T_OPT: case T_ARRAY: case T_SLICE:
        a = dbg_type(g, t->elem);
        break;
    case T_ENUM:
        a = dbg_type(g, t->decl->underlying);
        break;
    case T_FUNC:
        a = t->elem != NULL ? dbg_type(g, t->elem) : -1;
        for (i = 0; i < t->nparams; i++) dbg_type(g, t->params[i]);
        break;
    case T_STRUCT:
        for (i = 0; i < t->decl->nfields; i++) dbg_type(g, t->decl->fields[i].type);
        break;
    default:
        break;
    }
    g->out = &line;
    emit(g, ".نوع ");
    switch (t->kind) {
    case T_INT:
        emit(g, "صحيح، ");
        emit_num(g, t->is_size ? 64 : t->bits);
        if (t->is_signed) emit(g, "، بإشارة");
        if (t->is_size) emit(g, "، حجم");
        break;
    case T_BOOL: emit(g, "منطقي"); break;
    case T_CHAR: emit(g, "حرف"); break;
    case T_FLOAT:
        emit(g, "حقيقي، ");
        emit_num(g, t->bits);
        break;
    case T_PTR:
        emit(g, "مؤشر، ");
        emit_num(g, a);
        if (t->readonly) emit(g, "، ثابت");
        if (t->vol) emit(g, "، متطاير");
        break;
    case T_OPT:
        emit(g, "اختياري، ");
        emit_num(g, a);
        break;
    case T_ARRAY:
        emit(g, "مصفوفة، ");
        emit_num(g, a);
        emit(g, "، ");
        emit_unum(g, t->count);
        break;
    case T_SLICE:
        emit(g, "شريحة، ");
        emit_num(g, a);
        if (t->readonly) emit(g, "، ثابت");
        break;
    case T_STRUCT: {
        Buf *saved2 = g->out;
        first = g->nfields;
        count = (int)t->decl->nfields;
        g->out = &g->fields;
        for (i = 0; i < t->decl->nfields; i++) {
            const Field *f = &t->decl->fields[i];
            emit(g, ".حقل ");
            emit_quoted(g, f->name, f->len);
            emit(g, "، ");
            emit_num(g, dbg_type(g, f->type));
            emit(g, "، ");
            emit_unum(g, f->offset);
            emit(g, "\n");
        }
        g->nfields += count;
        g->out = saved2;
        emit(g, "بنية، ");
        emit_quoted(g, t->decl->name, t->decl->len);
        emit(g, "، ");
        emit_unum(g, t->decl->size);
        emit(g, "، ");
        emit_unum(g, t->decl->align);
        emit(g, "، ");
        emit_num(g, first);
        emit(g, "، ");
        emit_num(g, count);
        if (t->decl->packed) emit(g, "، مضغوطة");
        break;
    }
    case T_ENUM: {
        Buf *saved2 = g->out;
        first = g->nmembers;
        count = (int)t->decl->nmembers;
        g->out = &g->members;
        for (i = 0; i < t->decl->nmembers; i++) {
            const EnumMember *m = &t->decl->members[i];
            emit(g, ".عضو ");
            emit_quoted(g, m->name, m->len);
            emit(g, "، ");
            if (m->value.neg) {
                emit(g, "-");
                emit_unum(g, m->value.mag);
            } else {
                emit_unum(g, m->value.mag);
            }
            emit(g, "\n");
        }
        g->nmembers += count;
        g->out = saved2;
        emit(g, "تعداد، ");
        emit_quoted(g, t->decl->name, t->decl->len);
        emit(g, "، ");
        emit_num(g, a);
        emit(g, "، ");
        emit_num(g, first);
        emit(g, "، ");
        emit_num(g, count);
        break;
    }
    default: { /* T_FUNC */
        Buf *saved2 = g->out;
        first = g->nparams;
        count = (int)t->nparams;
        g->out = &g->params;
        for (i = 0; i < t->nparams; i++) {
            emit(g, ".معامل ");
            emit_num(g, dbg_type(g, t->params[i]));
            emit(g, "\n");
        }
        g->nparams += count;
        g->out = saved2;
        emit(g, "دالة، ");
        emit_index(g, a);
        emit(g, "، ");
        emit_num(g, first);
        emit(g, "، ");
        emit_num(g, count);
        break;
    }
    }
    emit(g, "\n");
    g->out = saved;
    {
        /* rows are numbered by position: this line is row idx */
        DbgLine *rows = (DbgLine *)g->types.data;
        uint8_t *copy = arena_alloc(g->prog->arena, line.len);
        memcpy(copy, line.data, line.len);
        rows[idx].text = copy;
        rows[idx].len = line.len;
    }
    buf_free(&line);
    return idx;
}

/* the rows of a function, after its code: the function, its scopes (in
 * the order they were opened, so parents come first), its variables */
static void dbg_function(Gen *g, Decl *d, int64_t frame)
{
    DbgScope *sc = (DbgScope *)g->scopes.data;
    DbgVar *vs = (DbgVar *)g->vars.data;
    size_t n = g->scopes.len / sizeof *sc, nv = g->vars.len / sizeof *vs, i;
    int end = new_label(g), result = d->ret != NULL ? dbg_type(g, d->ret) : -1;
    int first_scope = g->nscopes - (int)n;
    emit_label_def(g, end);
    emit(g, ".دالة ");
    emit_quoted(g, d->name, d->len);
    emit(g, "، ");
    emit_sym(g, d);
    emit(g, "، ");
    emit_sym(g, d);
    emit(g, "، ");
    emit_label_name(g, end);
    emit(g, "، ");
    emit_num(g, d->module->debug_file);
    emit(g, "، ");
    emit_num(g, d->tok.line);
    emit(g, "، ");
    emit_num(g, frame);
    emit(g, "، ");
    emit_index(g, result);
    emit(g, "\n");
    for (i = 0; i < n; i++) {
        emit(g, ".نطاق ");
        emit_num(g, g->nfuncs);
        emit(g, "، ");
        emit_index(g, sc[i].parent);
        emit(g, "، ");
        emit_label_name(g, sc[i].start);
        emit(g, "، ");
        emit_label_name(g, sc[i].end);
        emit(g, "\n");
    }
    (void)first_scope;
    for (i = 0; i < nv; i++) {
        Local *l = vs[i].local;
        if (l->offset == 0) continue;   /* never given a place in the frame */
        emit(g, ".متغير ");
        emit_quoted(g, l->name, l->len);
        emit(g, "، ");
        emit_num(g, dbg_type(g, l->type));
        emit(g, vs[i].param ? "، معامل، " : "، محلي، ");
        emit_index(g, vs[i].scope);
        emit(g, "، ");
        emit_num(g, l->offset);
        emit(g, "\n");
    }
    g->nfuncs++;
}

static void gen_function(Gen *g, Decl *d)
{
    Buf *saved_out = g->out;
    size_t i;
    int hidden = d->ret != NULL && ty_is_aggregate(d->ret) && ty_size_of(d->ret) > 16;

    g->fn = d;
    g->frame = 16;          /* saved return address and frame pointer */
    if (g->debug) {
        size_t k;
        g->scopes.len = 0;
        g->vars.len = 0;
        g->cur_scope = g->nscopes;   /* parameters belong to the body's scope */
        for (k = 0; k < d->nparams; k++) dbg_var(g, d->params[k], 1);
        g->cur_scope = -1;
    }
    g->loop_depth = 0;
    g->ret_label = new_label(g);
    buf_init(&g->text);
    g->out = &g->text;

    /* save the eight argument registers first, then build each parameter
     * from its words (saved registers, or the caller's stack area at the
     * frame pointer) */
    {
        int nreal = 0, r;
        int32_t saved;
        ArgPlace *place = arena_alloc(g->prog->arena, (d->nparams + 1) * sizeof *place);
        Type **pts = arena_alloc(g->prog->arena, (d->nparams + 1) * sizeof *pts);
        for (i = 0; i < d->nparams; i++) nreal += d->params[i]->type->kind == T_FLOAT;
        /* real parameters arrive in وسيط_عشري…: saved after the integer ones */
        saved = alloc_slot(g, nreal ? 128 : 64, 8);
        for (r = 0; r < 8; r++) mem_op(g, "خزن_كلمة_مزدوجة", areg[r], "محفوظ٠", saved + 8 * r);
        for (r = 0; r < 8 && r < nreal; r++) mem_op(g, "خزن_مزدوج", areg[8 + r], "محفوظ٠", saved + 64 + 8 * r);
        if (hidden) {
            g->hidden_ret_slot = alloc_slot(g, 8, 8);
            mem_op(g, "حمل_كلمة_مزدوجة", "مؤقت٠", "محفوظ٠", saved);
            mem_op(g, "خزن_كلمة_مزدوجة", "مؤقت٠", "محفوظ٠", g->hidden_ret_slot);
        }
        for (i = 0; i < d->nparams; i++) pts[i] = d->params[i]->type;
        plan_args(pts, d->nparams, hidden, place);
        for (i = 0; i < d->nparams; i++) {
            Local *p = d->params[i];
            Type *t = p->type;
            int32_t off = local_slot(g, p);
            int32_t words = alloc_slot(g, 16, 8);
            int k;
            for (k = 0; k < place[i].words; k++) {
                if (place[i].reg[k] >= 0) {
                    mem_op(g, "حمل_كلمة_مزدوجة", "مؤقت٠", "محفوظ٠", saved + 8 * place[i].reg[k]);
                } else {
                    mem_op(g, "حمل_كلمة_مزدوجة", "مؤقت٠", "محفوظ٠", place[i].stack[k]);
                }
                mem_op(g, "خزن_كلمة_مزدوجة", "مؤقت٠", "محفوظ٠", words + 8 * k);
            }
            if (t->kind == T_SLICE) {
                add_imm(g, "وسيط١", "محفوظ٠", words);
                add_imm(g, "وسيط٠", "محفوظ٠", off);
                copy_mem(g, 16);
            } else if (ty_is_aggregate(t)) {
                uint64_t size = ty_size_of(t);
                if (size <= 16) {
                    add_imm(g, "وسيط١", "محفوظ٠", words);
                } else {
                    mem_op(g, "حمل_كلمة_مزدوجة", "وسيط١", "محفوظ٠", words); /* the caller's copy */
                }
                add_imm(g, "وسيط٠", "محفوظ٠", off);
                copy_mem(g, size);
            } else {
                mem_op(g, "حمل_كلمة_مزدوجة", "مؤقت٠", "محفوظ٠", words);
                if (is_u32(t)) normalize(g, "مؤقت٠", t); /* ABI sign-extended it */
                store_scalar(g, "مؤقت٠", "محفوظ٠", off, t);
            }
        }
    }
    gen_block(g, d->body);
    emit_label_def(g, g->ret_label);

    /* now the frame size is known: prologue, body, epilogue */
    g->out = saved_out;
    {
        int64_t frame = ((int64_t)g->frame + 15) / 16 * 16;
        emit(g, "\n");
        if (d->is_public) { /* exported: other objects may call it */
            emit(g, ".عام ");
            emit_sym(g, d);
            emit(g, "\n");
        }
        emit_sym(g, d);
        emit(g, ":\n");
        ins(g, "نسخ", "مؤقت١", "مؤشر_المكدس", NULL);
        mem_op(g, "خزن_كلمة_مزدوجة", "عنوان_العودة", "مؤقت١", -8);
        mem_op(g, "خزن_كلمة_مزدوجة", "محفوظ٠", "مؤقت١", -16);
        ins(g, "نسخ", "محفوظ٠", "مؤقت١", NULL);
        li(g, "مؤقت٠", frame);
        ins(g, "طرح", "مؤشر_المكدس", "مؤشر_المكدس", "مؤقت٠");
        emitb(g, g->text.data, g->text.len);
        mem_op(g, "حمل_كلمة_مزدوجة", "عنوان_العودة", "محفوظ٠", -8);
        ins(g, "نسخ", "مؤقت١", "محفوظ٠", NULL);
        mem_op(g, "حمل_كلمة_مزدوجة", "محفوظ٠", "مؤقت١", -16);
        ins(g, "نسخ", "مؤشر_المكدس", "مؤقت١", NULL);
        emit(g, "    عودة\n");
        if (g->debug) dbg_function(g, d, frame);
    }
    buf_free(&g->text);
}

/* constant data for a global's initial value */
static void gen_data(Gen *g, Expr *e, Type *t)
{
    size_t i;
    if (t->kind == T_SLICE) {
        if (e->kind == E_STRING || (e->kind == E_NAME && e->ref == REF_CONST)) {
            Expr *s = e->kind == E_STRING ? e : e->decl->init;
            emit(g, "    .كلمة_مزدوجة ن_");
            emit_num(g, s->string_id);
            emit(g, "، ");
            emit_unum(g, s->tok.str_len);
            emit(g, "\n");
        } else {
            emit(g, "    .فراغ ");
            emit_unum(g, 16);
            emit(g, "\n");
        }
        return;
    }
    if (t->kind == T_STRUCT) {
        Decl *d = t->decl;
        size_t f;
        for (f = 0; f < d->nfields; f++) {
            for (i = 0; i < e->nargs; i++) {
                if (bytes_eq(d->fields[f].name, d->fields[f].len, e->names[i], e->name_lens[i])) break;
            }
            gen_data(g, e->args[i], d->fields[f].type);
        }
        return;
    }
    if (t->kind == T_ARRAY) {
        for (i = 0; i < t->count; i++) {
            gen_data(g, e->kind == E_ARRAY_REPEAT ? e->args[0] : e->args[i], t->elem);
        }
        return;
    }
    {
        uint64_t size = ty_size_of(scalar_repr(t));
        emit(g, size == 1 ? "    .بايت " : size == 2 ? "    .نصف_كلمة " :
                size == 4 ? "    .كلمة " : "    .كلمة_مزدوجة ");
        emit_unum(g, e->kind == E_NULL ? 0 : (size == 8 ? cv_bits(e->cval)
                                                         : cv_bits(e->cval) & (((uint64_t)1 << (size * 8)) - 1)));
        emit(g, "\n");
    }
}

static void mangle(Gen *g, Decl *d)
{
    Buf b;
    uint8_t *name;
    size_t i;
    if (d->is_extern || d->raw_export || d == g->prog->main_func) {
        d->asm_name = d->name;
        d->asm_len = d->len;
        return;
    }
    buf_init(&b);
    for (i = 0; i < d->module->name_len; i++) {
        uint8_t ch = d->module->name[i];
        buf_u8(&b, ch == '.' ? '_' : ch);
    }
    buf_bytes(&b, "__", 2);
    buf_bytes(&b, d->name, d->len);
    name = arena_alloc(g->prog->arena, b.len + 1);
    memcpy(name, b.data, b.len);
    d->asm_name = name;
    d->asm_len = b.len;
    buf_free(&b);
}

/* Does this object hold the code and data of module m? */
static int emits(const Program *p, const Module *m)
{
    return !m->interface_only && (p->only == NULL || p->only == m);
}

/* String literals this object needs: its own, and those of constants of
 * any module (constants are used by value, so each user holds a copy). */
static int needs_string(const Program *p, const Expr *s)
{
    size_t i, j;
    if (p->only == NULL || s->str_module == p->only) return 1;
    for (i = 0; i < p->nmodules; i++) {
        const Module *m = p->modules[i];
        for (j = 0; j < m->ndecls; j++) {
            if (m->decls[j]->kind == D_CONST && m->decls[j]->init == s) return 1;
        }
    }
    return 0;
}

int gen_program(Program *p, Buf *out)
{
    Gen g;
    size_t i, j;
    memset(&g, 0, sizeof g);
    g.prog = p;
    g.out = out;

    for (i = 0; i < p->nmodules; i++) {
        Module *m = p->modules[i];
        for (j = 0; j < m->ndecls; j++) {
            if (m->decls[j]->kind == D_FUNC || m->decls[j]->kind == D_VAR) mangle(&g, m->decls[j]);
        }
    }

    emit(&g, "# ولده مترجم غرة\n.شفرة\n");
    g.debug = p->debug;
    if (g.debug) {
        int nfiles = 0;
        buf_init(&g.scopes);
        buf_init(&g.vars);
        buf_init(&g.tymap);
        buf_init(&g.types);
        buf_init(&g.fields);
        buf_init(&g.members);
        buf_init(&g.params);
        for (i = 0; i < p->nmodules; i++) {
            Module *m = p->modules[i];
            if (!emits(p, m)) continue;
            m->debug_file = nfiles++;
            emit(&g, ".ملف ");
            emit_quoted(&g, (const uint8_t *)m->path, strlen(m->path));
            emit(&g, "، ");
            emit_unum(&g, m->source_hash);
            emit(&g, "\n");
        }
    }
    for (i = 0; i < p->nmodules; i++) {
        Module *m = p->modules[i];
        if (!emits(p, m)) continue;
        for (j = 0; j < m->ndecls; j++) {
            Decl *d = m->decls[j];
            if (d->kind == D_FUNC && !d->is_extern && d->body != NULL) gen_function(&g, d);
        }
    }

    emit(&g, "\n.بيانات\n");
    for (i = 0; i < p->nmodules; i++) {
        Module *m = p->modules[i];
        if (!emits(p, m)) continue;
        for (j = 0; j < m->ndecls; j++) {
            Decl *d = m->decls[j];
            uint64_t a;
            int n = 0;
            if (d->kind != D_VAR) continue;
            if (d->is_public) {
                emit(&g, ".عام ");
                emit_sym(&g, d);
                emit(&g, "\n");
            }
            a = ty_align_of(d->type);
            while (((uint64_t)1 << n) < a) n++;
            emit(&g, ".محاذاة ");
            emit_num(&g, n);
            emit(&g, "\n");
            emit_sym(&g, d);
            emit(&g, ":\n");
            gen_data(&g, d->init, d->type);
            if (g.debug) {
                emit(&g, ".متغير ");
                emit_quoted(&g, d->name, d->len);
                emit(&g, "، ");
                emit_num(&g, dbg_type(&g, d->type));
                emit(&g, "، عام، ");
                emit_sym(&g, d);
                emit(&g, "\n");
            }
        }
    }

    emit(&g, "\n.بيانات_للقراءة\n");
    {
        Expr **strs = (Expr **)p->strings.data;
        size_t n = p->strings.len / sizeof *strs;
        for (i = 0; i < n; i++) {
            size_t k;
            if (!needs_string(p, strs[i])) continue;
            emit(&g, "ن_");
            emit_num(&g, strs[i]->string_id);
            emit(&g, ":\n");
            for (k = 0; k < strs[i]->tok.str_len; k++) {
                emit(&g, k % 16 == 0 ? "    .بايت " : "، ");
                emit_unum(&g, strs[i]->tok.str[k]);
                if (k % 16 == 15 || k + 1 == strs[i]->tok.str_len) emit(&g, "\n");
            }
        }
    }
    if (g.debug) {
        DbgLine *rows = (DbgLine *)g.types.data;
        size_t n = g.types.len / sizeof *rows;
        emit(&g, "\n");
        for (i = 0; i < n; i++) emitb(&g, rows[i].text, rows[i].len);
        emitb(&g, g.fields.data, g.fields.len);
        emitb(&g, g.members.data, g.members.len);
        emitb(&g, g.params.data, g.params.len);
        buf_free(&g.scopes);
        buf_free(&g.vars);
        buf_free(&g.tymap);
        buf_free(&g.types);
        buf_free(&g.fields);
        buf_free(&g.members);
        buf_free(&g.params);
    }
    return 0;
}
