/* check.c - name resolution, types and every compile-time rule of غرة.
 *
 * Order: module scopes and duplicates, then types (structures are laid out
 * here, with the no-hidden-padding rule), enumerations, constants, globals
 * and function signatures (all lazily, with cycle detection, since the
 * order of declarations does not matter), then function bodies:
 * expressions, statements, definite assignment of غير_مهيأ variables and
 * the all-paths-return rule.
 */
#include "cc.h"

#include "../common/diag.h"
#include "../common/number.h"
#include "../common/utf8.h"

#include <string.h>

#define MAX_UNINIT 256

typedef struct {
    uint8_t b[MAX_UNINIT];
} DaSet;

typedef struct {
    Program *prog;
    Module *mod;
    Decl *fn;
    Buf scope;            /* Local* */
    int loop_depth;
    DaSet *da;            /* definitely-assigned set on the current path */
    int nuninit;
    int lhs;              /* checking the target of an assignment */
} Ctx;

static Type *ty_string;   /* ][ثابت طبيعي٨ */

/* ------------------------------------------------------------------ */
/* Errors                                                              */

static int err(const Tok *t, DiagId id, const char *extra)
{
    diag_error(t->line, t->col, id, (const uint8_t *)extra, extra ? strlen(extra) : 0);
    return -1;
}

static int err_name(const Tok *t, DiagId id, const uint8_t *name, size_t len)
{
    diag_error(t->line, t->col, id, name, len);
    return -1;
}

static void use_module(Ctx *c, Module *m)
{
    c->mod = m;
    cc_set_file(m->path);
}

/* ------------------------------------------------------------------ */
/* Constant values: exact sign + magnitude                             */

ConstVal cv_from_u64(uint64_t v)
{
    ConstVal c;
    c.neg = 0;
    c.mag = v;
    return c;
}

static ConstVal cv_make(int neg, uint64_t mag)
{
    ConstVal c;
    c.neg = mag == 0 ? 0 : neg;
    c.mag = mag;
    return c;
}

uint64_t cv_bits(ConstVal v)
{
    return v.neg ? 0u - v.mag : v.mag;
}

static uint64_t ty_max(const Type *t)
{
    int bits = t->is_size ? 64 : t->bits;
    if (t->is_signed) return ((uint64_t)1 << (bits - 1)) - 1;
    return bits == 64 ? UINT64_MAX : ((uint64_t)1 << bits) - 1;
}

/* magnitude of the most negative value, 0 for unsigned types */
static uint64_t ty_min_mag(const Type *t)
{
    return t->is_signed ? (uint64_t)1 << (t->bits - 1) : 0;
}

int cv_fits(ConstVal v, const Type *t)
{
    if (t->kind == T_ENUM) t = t->decl->underlying;
    if (t->kind != T_INT) return 0;
    if (v.neg) return v.mag <= ty_min_mag(t);
    return v.mag <= ty_max(t);
}

/* exact arithmetic on untyped constants; returns -1 when beyond 64 bits */
static int cv_add(ConstVal a, ConstVal b, ConstVal *r)
{
    if (a.neg == b.neg) {
        if (a.mag > UINT64_MAX - b.mag) return -1;
        *r = cv_make(a.neg, a.mag + b.mag);
    } else if (a.mag >= b.mag) {
        *r = cv_make(a.neg, a.mag - b.mag);
    } else {
        *r = cv_make(b.neg, b.mag - a.mag);
    }
    return 0;
}

static int cv_mul(ConstVal a, ConstVal b, ConstVal *r)
{
    if (a.mag != 0 && b.mag > UINT64_MAX / a.mag) return -1;
    *r = cv_make(a.neg != b.neg, a.mag * b.mag);
    return 0;
}

/* value of `bits` interpreted in type t (wrapping), as an exact constant */
static ConstVal cv_wrap(uint64_t bits, const Type *t)
{
    int w;
    if (t->kind == T_ENUM) t = t->decl->underlying;
    if (t->kind == T_BOOL) return cv_from_u64(bits & 1u);
    if (t->kind == T_CHAR) return cv_from_u64(bits & 0xFFFFFFFFu);
    w = t->bits;
    if (w < 64) bits &= ((uint64_t)1 << w) - 1;
    if (t->is_signed && w < 64 && (bits >> (w - 1)) & 1u) {
        return cv_make(1, ((uint64_t)1 << w) - bits);
    }
    if (t->is_signed && w == 64 && (bits >> 63)) {
        return cv_make(1, 0u - bits);
    }
    return cv_from_u64(bits);
}

static void set_const(Expr *e, ConstVal v)
{
    e->is_const = 1;
    e->cval = v;
}

/* ------------------------------------------------------------------ */
/* Scopes                                                              */

static Decl *module_decl(Module *m, const uint8_t *name, size_t len)
{
    size_t i;
    for (i = 0; i < m->ndecls; i++) {
        if (bytes_eq(m->decls[i]->name, m->decls[i]->len, name, len)) return m->decls[i];
    }
    return NULL;
}

static Import *module_import(Module *m, const uint8_t *name, size_t len)
{
    size_t i;
    for (i = 0; i < m->nimports; i++) {
        if (bytes_eq(m->imports[i].alias, m->imports[i].alias_len, name, len)) return &m->imports[i];
    }
    return NULL;
}

static Local *find_local(Ctx *c, const uint8_t *name, size_t len)
{
    Local **ls = (Local **)c->scope.data;
    size_t n = c->scope.len / sizeof *ls;
    while (n > 0) {
        n--;
        if (bytes_eq(ls[n]->name, ls[n]->len, name, len)) return ls[n];
    }
    return NULL;
}

/* No shadowing: a local may not reuse a visible local or a module name. */
static int declare_local(Ctx *c, Local *l)
{
    if (find_local(c, l->name, l->len) || module_decl(c->mod, l->name, l->len) ||
        module_import(c->mod, l->name, l->len)) {
        return err_name(&l->tok, D_CC_REDEFINED, l->name, l->len);
    }
    buf_bytes(&c->scope, &l, sizeof l);
    return 0;
}

static size_t scope_mark(Ctx *c) { return c->scope.len; }
static void scope_pop(Ctx *c, size_t mark) { c->scope.len = mark; }

/* ------------------------------------------------------------------ */
/* Types                                                               */

static int layout_struct(Ctx *c, Decl *d);
static int resolve_enum(Ctx *c, Decl *d);
static int resolve_const(Ctx *c, Decl *d);
static int check_expr(Ctx *c, Expr *e, Type *want);
static int coerce(Ctx *c, Expr *e, Type *t);
static int check_member_path_enum(Ctx *c, Expr *e, Decl *d);

/* Named type lookup, in the module of the current context. */
static Decl *lookup_type_decl(Ctx *c, const Type *t)
{
    Module *m = c->mod;
    Decl *d;
    if (t->qual != NULL) {
        Import *im = module_import(m, t->qual, t->qual_len);
        if (im == NULL) {
            err_name(&t->tok, D_CC_UNKNOWN_NAME, t->qual, t->qual_len);
            return NULL;
        }
        m = im->module;
        d = module_decl(m, t->tname, t->tname_len);
        if (d != NULL && !d->is_public) {
            err_name(&t->tok, D_CC_PRIVATE, t->tname, t->tname_len);
            return NULL;
        }
    } else {
        d = module_decl(m, t->tname, t->tname_len);
    }
    if (d == NULL || (d->kind != D_STRUCT && d->kind != D_ENUM)) {
        err_name(&t->tok, D_CC_UNKNOWN_TYPE, t->tname, t->tname_len);
        return NULL;
    }
    return d;
}

static Type *self_type(Ctx *c, Decl *d)
{
    if (d->self_type == NULL) {
        d->self_type = ty_new(c->prog->arena, d->kind == D_STRUCT ? T_STRUCT : T_ENUM);
        d->self_type->decl = d;
    }
    return d->self_type;
}

/* Resolves names and array lengths. by_value: the type's size is needed
 * (a field or element stored inline), so structures must be laid out. */
static Type *resolve_type(Ctx *c, Type *t, int by_value)
{
    size_t i;
    switch (t->kind) {
    case T_NAMED: {
        Decl *d = lookup_type_decl(c, t);
        Module *saved = c->mod;
        int rc = 0;
        if (d == NULL) return NULL;
        if (by_value || d->kind == D_ENUM) {
            use_module(c, d->module);
            rc = d->kind == D_STRUCT ? layout_struct(c, d) : resolve_enum(c, d);
            use_module(c, saved);
        }
        return rc == 0 ? self_type(c, d) : NULL;
    }
    case T_PTR:
    case T_OPT:
    case T_SLICE:
        t->elem = resolve_type(c, t->elem, 0);
        if (t->elem == NULL) return NULL;
        if (t->kind == T_OPT && t->elem->kind != T_PTR) return NULL;
        return t;
    case T_ARRAY:
        if (t->count_expr != NULL) {
            Expr *n = t->count_expr;
            if (check_expr(c, n, ty_size) != 0 || coerce(c, n, ty_size) != 0) return NULL;
            if (!n->is_const || n->cval.mag == 0) {
                err(&n->tok, D_CC_ARRAY_SIZE, NULL);
                return NULL;
            }
            t->count = n->cval.mag;
            t->count_expr = NULL;
        }
        t->elem = resolve_type(c, t->elem, 1);
        return t->elem ? t : NULL;
    case T_FUNC:
        for (i = 0; i < t->nparams; i++) {
            t->params[i] = resolve_type(c, t->params[i], 1);
            if (t->params[i] == NULL) return NULL;
        }
        if (t->elem != NULL) {
            t->elem = resolve_type(c, t->elem, 1);
            if (t->elem == NULL) return NULL;
        }
        return t;
    default:
        return t;
    }
}

/* Struct layout with the no-hidden-padding rule (decision 30). */
static int layout_struct(Ctx *c, Decl *d)
{
    uint64_t off = 0, align = 1;
    size_t i;
    if (d->state == 2) return 0;
    if (d->state == 1) return err_name(&d->tok, D_CC_RECURSIVE_TYPE, d->name, d->len);
    if (d->packed) return err(&d->tok, D_CC_NOT_SUPPORTED, "بنية مضغوطة");
    d->state = 1;
    for (i = 0; i < d->nfields; i++) {
        Field *f = &d->fields[i];
        uint64_t a;
        size_t j;
        for (j = 0; j < i; j++) {
            if (bytes_eq(d->fields[j].name, d->fields[j].len, f->name, f->len)) {
                return err_name(&f->tok, D_CC_FIELD_DUP, f->name, f->len);
            }
        }
        f->type = resolve_type(c, f->type, 1);
        if (f->type == NULL) return -1;
        a = d->packed ? 1 : ty_align_of(f->type);
        if (off % a != 0) {
            /* tell how many bytes are missing before this field */
            char msg[256];
            uint32_t digits[24];
            size_t n = 0, k, nd = num_format_dec(a - off % a, digits);
            memcpy(msg, f->name, f->len < 200 ? f->len : 200);
            n = f->len < 200 ? f->len : 200;
            memcpy(msg + n, " ← ", 5);
            n += 5;
            for (k = 0; k < nd; k++) n += utf8_encode(digits[k], (uint8_t *)msg + n);
            msg[n] = 0;
            return err(&f->tok, D_CC_PADDING, msg);
        }
        f->offset = off;
        off += ty_size_of(f->type);
        if (a > align) align = a;
    }
    if (off % align != 0) {
        char msg[64];
        uint32_t digits[24];
        size_t n = 0, k, nd = num_format_dec(align - off % align, digits);
        memcpy(msg, "{ ← ", 6);
        n = 6;
        for (k = 0; k < nd; k++) n += utf8_encode(digits[k], (uint8_t *)msg + n);
        msg[n] = 0;
        return err(&d->tok, D_CC_PADDING, msg);
    }
    d->size = off;
    d->align = align;
    d->state = 2;
    self_type(c, d);
    return 0;
}

static int resolve_enum(Ctx *c, Decl *d)
{
    size_t i, j;
    ConstVal next = cv_from_u64(0);
    if (d->state == 2) return 0;
    if (d->state == 1) return err_name(&d->tok, D_CC_CYCLE, d->name, d->len);
    d->state = 1;
    d->underlying = resolve_type(c, d->underlying, 1);
    if (d->underlying == NULL) return -1;
    if (!ty_is_int(d->underlying)) return err(&d->tok, D_CC_NEED_INT, NULL);
    for (i = 0; i < d->nmembers; i++) {
        EnumMember *m = &d->members[i];
        for (j = 0; j < i; j++) {
            if (bytes_eq(d->members[j].name, d->members[j].len, m->name, m->len)) {
                return err_name(&m->tok, D_CC_REDEFINED, m->name, m->len);
            }
        }
        if (m->value_expr != NULL) {
            if (check_expr(c, m->value_expr, d->underlying) != 0 ||
                coerce(c, m->value_expr, d->underlying) != 0) return -1;
            if (!m->value_expr->is_const) return err(&m->value_expr->tok, D_CC_NOT_CONST, NULL);
            m->value = m->value_expr->cval;
        } else {
            if (!cv_fits(next, d->underlying)) return err(&m->tok, D_CC_NOT_FIT, ty_name(d->underlying));
            m->value = next;
        }
        for (j = 0; j < i; j++) {
            if (d->members[j].value.neg == m->value.neg && d->members[j].value.mag == m->value.mag) {
                return err_name(&m->tok, D_CC_DUP_ENUM, m->name, m->len);
            }
        }
        if (cv_add(m->value, cv_from_u64(1), &next) != 0) next = cv_make(0, UINT64_MAX);
    }
    d->state = 2;
    self_type(c, d);
    return 0;
}

static int const_type_ok(const Type *t)
{
    return t->kind == T_INT || t->kind == T_BOOL || t->kind == T_CHAR || t->kind == T_ENUM ||
           (t->kind == T_SLICE && ty_equal(t, ty_string));
}

static int resolve_const(Ctx *c, Decl *d)
{
    Module *saved = c->mod;
    Decl *saved_fn = c->fn;
    Buf saved_scope = c->scope;
    int rc = 0;
    if (d->state == 2) return 0;
    if (d->state == 1) return err_name(&d->tok, D_CC_CYCLE, d->name, d->len);
    d->state = 1;
    if (d->module != NULL) {
        use_module(c, d->module);
        c->fn = NULL;
        buf_init(&c->scope);
    }
    d->type = resolve_type(c, d->type, 1);
    if (d->type == NULL) {
        rc = -1;
    } else if (!const_type_ok(d->type)) {
        rc = err(&d->tok, D_CC_NOT_SUPPORTED, "ثابت");
    } else if (check_expr(c, d->init, d->type) != 0 || coerce(c, d->init, d->type) != 0) {
        rc = -1;
    } else if (!d->init->is_const && d->init->kind != E_STRING) {
        rc = err(&d->init->tok, D_CC_NOT_CONST, NULL);
    }
    if (d->module != NULL) {
        buf_free(&c->scope);
        c->scope = saved_scope;
        c->fn = saved_fn;
        use_module(c, saved);
    }
    d->state = 2;
    return rc;
}

static int resolve_signature(Ctx *c, Decl *d)
{
    Module *saved = c->mod;
    size_t i;
    Type *ft;
    if (d->func_type != NULL) return 0;
    use_module(c, d->module);
    ft = ty_new(c->prog->arena, T_FUNC);
    ft->nparams = d->nparams;
    ft->params = arena_alloc(c->prog->arena, (d->nparams + 1) * sizeof(Type *));
    for (i = 0; i < d->nparams; i++) {
        d->params[i]->type = resolve_type(c, d->params[i]->type, 1);
        if (d->params[i]->type == NULL) { use_module(c, saved); return -1; }
        ft->params[i] = d->params[i]->type;
    }
    if (d->ret != NULL) {
        d->ret = resolve_type(c, d->ret, 1);
        if (d->ret == NULL) { use_module(c, saved); return -1; }
    }
    ft->elem = d->ret;
    d->func_type = ft;
    use_module(c, saved);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Coercion: the only implicit conversions are safe ones               */

static int assignable(const Type *from, const Type *to)
{
    if (ty_equal(from, to)) return 1;
    if (from->kind == T_PTR && to->kind == T_PTR) {
        return !from->readonly && to->readonly && ty_equal(from->elem, to->elem);
    }
    if (from->kind == T_SLICE && to->kind == T_SLICE) {
        return !from->readonly && to->readonly && ty_equal(from->elem, to->elem);
    }
    if (from->kind == T_PTR && to->kind == T_OPT) return assignable(from, to->elem);
    if (from->kind == T_OPT && to->kind == T_OPT) return assignable(from->elem, to->elem);
    if (from->kind == T_NULLLIT && to->kind == T_OPT) return 1;
    return 0;
}

static int coerce(Ctx *c, Expr *e, Type *t)
{
    (void)c;
    if (e->type == NULL) return -1;
    if (t == NULL || t->kind == T_VOID) return err(&e->tok, D_CC_VOID_VALUE, NULL);
    if (e->type->kind == T_VOID) return err(&e->tok, D_CC_VOID_VALUE, NULL);
    if (e->type->kind == T_UNTYPED) {
        if (t->kind != T_INT) return err(&e->tok, D_CC_TYPE_MISMATCH, ty_name(t));
        if (!cv_fits(e->cval, t)) return err(&e->tok, D_CC_NOT_FIT, ty_name(t));
        e->type = t;
        return 0;
    }
    if (assignable(e->type, t)) {
        e->type = t;
        return 0;
    }
    return err(&e->tok, D_CC_TYPE_MISMATCH, ty_name(t));
}

/* ------------------------------------------------------------------ */
/* Expressions                                                         */

static int is_lvalue(const Expr *e)
{
    switch (e->kind) {
    case E_NAME: return e->ref == REF_LOCAL || e->ref == REF_GLOBAL;
    case E_DEREF: return 1;
    case E_MEMBER: return e->ref == REF_FIELD && is_lvalue(e->a);
    case E_INDEX: return e->a->type->kind == T_SLICE || is_lvalue(e->a);
    case E_BUILTIN: return e->tok.kind == K_INDEX_CHECKED &&
                           (e->args[0]->type->kind == T_SLICE || is_lvalue(e->args[0]));
    default: return 0;
    }
}

/* can the location be written through? (after is_lvalue) */
static int is_writable(const Expr *e)
{
    switch (e->kind) {
    case E_NAME: return e->ref == REF_GLOBAL || !e->local->readonly;
    case E_DEREF: return !e->a->type->readonly;
    case E_MEMBER: return is_writable(e->a);
    case E_INDEX:
        return e->a->type->kind == T_SLICE ? !e->a->type->readonly : is_writable(e->a);
    case E_BUILTIN:
        return e->args[0]->type->kind == T_SLICE ? !e->args[0]->type->readonly
                                                 : is_writable(e->args[0]);
    default: return 0;
    }
}

static Type *slice_of(Ctx *c, Type *elem, int readonly)
{
    Type *t = ty_new(c->prog->arena, T_SLICE);
    t->elem = elem;
    t->readonly = readonly;
    return t;
}

static Type *ptr_to(Ctx *c, Type *elem, int readonly)
{
    Type *t = ty_new(c->prog->arena, T_PTR);
    t->elem = elem;
    t->readonly = readonly;
    return t;
}

static int ref_decl(Ctx *c, Expr *e, Decl *d)
{
    e->decl = d;
    switch (d->kind) {
    case D_CONST:
        if (resolve_const(c, d) != 0) return -1;
        e->ref = REF_CONST;
        e->type = d->type;
        if (d->init->is_const) set_const(e, d->init->cval);
        return 0;
    case D_VAR:
        e->ref = REF_GLOBAL;
        if (d->type->kind == T_NAMED || d->type->kind == T_ARRAY || d->type->kind == T_PTR ||
            d->type->kind == T_SLICE || d->type->kind == T_OPT) {
            Module *saved = c->mod;
            use_module(c, d->module);
            d->type = resolve_type(c, d->type, 1);
            use_module(c, saved);
            if (d->type == NULL) return -1;
        }
        e->type = d->type;
        return 0;
    case D_FUNC:
        if (resolve_signature(c, d) != 0) return -1;
        e->ref = REF_FUNC;
        e->type = d->func_type;
        return 0;
    default:
        return err_name(&e->tok, D_CC_UNKNOWN_NAME, d->name, d->len);
    }
}

static int check_name(Ctx *c, Expr *e)
{
    Local *l = find_local(c, e->name, e->name_len);
    Decl *d;
    if (l != NULL) {
        e->local = l;
        e->type = l->type;
        if (l->const_decl != NULL) {
            e->ref = REF_CONST;
            e->decl = l->const_decl;
            if (l->const_decl->init->is_const) set_const(e, l->const_decl->init->cval);
            return 0;
        }
        e->ref = REF_LOCAL;
        if (!c->lhs && l->uninit_index >= 0 && c->da && !c->da->b[l->uninit_index]) {
            return err_name(&e->tok, D_CC_UNINIT_READ, e->name, e->name_len);
        }
        return 0;
    }
    d = module_decl(c->mod, e->name, e->name_len);
    if (d != NULL) return ref_decl(c, e, d);
    return err_name(&e->tok, D_CC_UNKNOWN_NAME, e->name, e->name_len);
}

/* base.name where base names a module or an enumeration */
static int check_member_path(Ctx *c, Expr *e, int *handled)
{
    Expr *b = e->a;
    *handled = 0;
    if (b->kind == E_NAME && find_local(c, b->name, b->name_len) == NULL) {
        Decl *d = module_decl(c->mod, b->name, b->name_len);
        Import *im = d == NULL ? module_import(c->mod, b->name, b->name_len) : NULL;
        if (im != NULL) {
            Decl *m = module_decl(im->module, e->name, e->name_len);
            *handled = 1;
            if (m == NULL) return err_name(&e->tok, D_CC_UNKNOWN_NAME, e->name, e->name_len);
            if (!m->is_public) return err_name(&e->tok, D_CC_PRIVATE, e->name, e->name_len);
            if (m->kind == D_ENUM || m->kind == D_STRUCT) {
                return err_name(&e->tok, D_CC_UNEXPECTED, e->name, e->name_len);
            }
            return ref_decl(c, e, m);
        }
        if (d != NULL && d->kind == D_ENUM) {
            *handled = 1;
            return check_member_path_enum(c, e, d);
        }
    }
    /* module.enum.member */
    if (b->kind == E_MEMBER && b->a->kind == E_NAME && find_local(c, b->a->name, b->a->name_len) == NULL &&
        module_decl(c->mod, b->a->name, b->a->name_len) == NULL) {
        Import *im = module_import(c->mod, b->a->name, b->a->name_len);
        if (im != NULL) {
            Decl *d = module_decl(im->module, b->name, b->name_len);
            if (d != NULL && d->kind == D_ENUM) {
                *handled = 1;
                if (!d->is_public) return err_name(&b->tok, D_CC_PRIVATE, b->name, b->name_len);
                return check_member_path_enum(c, e, d);
            }
        }
    }
    return 0;
}

static int check_member_path_enum(Ctx *c, Expr *e, Decl *d)
{
    size_t i;
    Module *saved = c->mod;
    use_module(c, d->module);
    if (resolve_enum(c, d) != 0) { use_module(c, saved); return -1; }
    use_module(c, saved);
    for (i = 0; i < d->nmembers; i++) {
        if (bytes_eq(d->members[i].name, d->members[i].len, e->name, e->name_len)) {
            e->ref = REF_ENUM_MEMBER;
            e->member_index = (int)i;
            e->type = self_type(c, d);
            set_const(e, d->members[i].value);
            return 0;
        }
    }
    return err_name(&e->tok, D_CC_UNKNOWN_NAME, e->name, e->name_len);
}

static int check_member(Ctx *c, Expr *e)
{
    int handled;
    Type *bt;
    size_t i;
    int rc = check_member_path(c, e, &handled);
    if (handled) return rc;
    if (check_expr(c, e->a, NULL) != 0) return -1;
    bt = e->a->type;
    if (bytes_eq_str(e->name, e->name_len, "طول") &&
        (bt->kind == T_ARRAY || bt->kind == T_SLICE)) {
        e->ref = REF_LENGTH;
        e->type = ty_size;
        if (bt->kind == T_ARRAY) set_const(e, cv_from_u64(bt->count));
        return 0;
    }
    if (bt->kind == T_STRUCT) {
        Decl *d = bt->decl;
        for (i = 0; i < d->nfields; i++) {
            if (bytes_eq(d->fields[i].name, d->fields[i].len, e->name, e->name_len)) {
                e->ref = REF_FIELD;
                e->member_index = (int)i;
                e->type = d->fields[i].type;
                return 0;
            }
        }
        return err_name(&e->tok, D_CC_NO_FIELD, e->name, e->name_len);
    }
    if (bytes_eq_str(e->name, e->name_len, "طول")) return err(&e->tok, D_CC_NO_LENGTH, NULL);
    return err_name(&e->tok, D_CC_NO_FIELD, e->name, e->name_len);
}

static int arg_words(const Type *t)
{
    if (t->kind == T_SLICE) return 2;
    if (ty_is_aggregate(t)) {
        uint64_t s = ty_size_of(t);
        return s <= 8 ? 1 : s <= 16 ? 2 : 1;
    }
    return 1;
}

static int check_call(Ctx *c, Expr *e)
{
    Type *ft;
    size_t i;
    int words = 0;
    if (check_expr(c, e->a, NULL) != 0) return -1;
    ft = e->a->type;
    if (ft->kind != T_FUNC) return err(&e->tok, D_CC_NOT_CALLABLE, NULL);
    if (e->nargs != ft->nparams) return err(&e->tok, D_CC_ARGS, NULL);
    for (i = 0; i < e->nargs; i++) {
        if (check_expr(c, e->args[i], ft->params[i]) != 0 ||
            coerce(c, e->args[i], ft->params[i]) != 0) return -1;
        words += arg_words(ft->params[i]);
    }
    if (ft->elem != NULL && ty_is_aggregate(ft->elem) && ty_size_of(ft->elem) > 16) words++;
    if (words > 8) return err(&e->tok, D_CC_TOO_MANY_ARGS, NULL);
    e->type = ft->elem ? ft->elem : ty_void;
    return 0;
}

static int check_index_like(Ctx *c, Expr *e, Expr *base, Expr *idx)
{
    Type *bt;
    if (check_expr(c, base, NULL) != 0) return -1;
    bt = base->type;
    if (bt->kind != T_ARRAY && bt->kind != T_SLICE) return err(&base->tok, D_CC_NOT_INDEXABLE, NULL);
    if (check_expr(c, idx, ty_size) != 0 || coerce(c, idx, ty_size) != 0) return -1;
    if (bt->kind == T_ARRAY && idx->is_const && idx->cval.mag >= bt->count) {
        return err(&idx->tok, D_CC_INDEX_RANGE, NULL);
    }
    e->type = bt->elem;
    return 0;
}

static int check_slice_like(Ctx *c, Expr *e, Expr *base, Expr *lo, Expr *hi)
{
    Type *bt;
    if (check_expr(c, base, NULL) != 0) return -1;
    bt = base->type;
    if (bt->kind != T_ARRAY && bt->kind != T_SLICE) return err(&base->tok, D_CC_NOT_INDEXABLE, NULL);
    if (bt->kind == T_ARRAY && !is_lvalue(base)) return err(&base->tok, D_CC_NOT_LVALUE, NULL);
    if (check_expr(c, lo, ty_size) != 0 || coerce(c, lo, ty_size) != 0) return -1;
    if (check_expr(c, hi, ty_size) != 0 || coerce(c, hi, ty_size) != 0) return -1;
    if (bt->kind == T_ARRAY && hi->is_const && hi->cval.mag > bt->count) {
        return err(&hi->tok, D_CC_INDEX_RANGE, NULL);
    }
    if (lo->is_const && hi->is_const && lo->cval.mag > hi->cval.mag) {
        return err(&lo->tok, D_CC_INDEX_RANGE, NULL);
    }
    e->type = slice_of(c, bt->elem, bt->kind == T_SLICE ? bt->readonly : !is_writable(base));
    return 0;
}

/* integer range in bits for "does every value fit" (حجم: 32..64 bits) */
static int int_fits_all(const Type *from, const Type *to)
{
    int fbits = from->is_size ? 64 : from->bits;  /* largest possible */
    int tbits = to->is_size ? 32 : to->bits;      /* smallest possible */
    int fsigned = from->is_signed, tsigned = to->is_signed;
    if (fsigned && !tsigned) return 0;
    if (fsigned == tsigned) return fbits <= tbits;
    return fbits < tbits; /* unsigned into signed needs one more bit */
}

static int check_builtin(Ctx *c, Expr *e, Type *want)
{
    Kind k = e->tok.kind;
    Type *t = NULL, *ft;
    if (e->type_arg != NULL) {
        t = resolve_type(c, e->type_arg, 1);
        if (t == NULL) return -1;
        e->type_arg = t;
    }
    switch (k) {
    case K_SIZEOF:
    case K_ALIGNOF:
        e->type = ty_size;
        set_const(e, cv_from_u64(k == K_SIZEOF ? ty_size_of(t) : ty_align_of(t)));
        return 0;
    case K_CONVERT:
    case K_CONVERT_CHECKED:
    case K_TRUNCATE:
    case K_REINTERPRET: {
        int ok = 0;
        if (check_expr(c, e->a, NULL) != 0) return -1;
        ft = e->a->type;
        if (ft->kind == T_UNTYPED && t->kind == T_CHAR && k != K_REINTERPRET) {
            if (e->a->cval.neg || e->a->cval.mag > 0x10FFFF ||
                (e->a->cval.mag >= 0xD800 && e->a->cval.mag <= 0xDFFF)) {
                return err(&e->a->tok, D_CC_BAD_CHAR, NULL);
            }
            e->a->type = ty_int(32, 0);
            e->type = t;
            set_const(e, e->a->cval);
            return 0;
        }
        if (ft->kind == T_UNTYPED) {
            /* a literal: just check that it fits the target exactly */
            if (k == K_REINTERPRET || coerce(c, e->a, t) != 0) {
                if (k == K_REINTERPRET) err(&e->tok, D_CC_BAD_CONVERSION, NULL);
                return -1;
            }
            e->type = t;
            set_const(e, e->a->cval);
            return 0;
        }
        if (k == K_CONVERT) {
            if (ft->kind == T_INT && t->kind == T_INT) ok = int_fits_all(ft, t);
            else if (ft->kind == T_BOOL && t->kind == T_INT) ok = 1;
            else if (ft->kind == T_CHAR && t->kind == T_INT)
                ok = t->is_size || (t->bits >= 32 && !(t->is_signed && t->bits < 32));
            else if (ft->kind == T_ENUM && t->kind == T_INT) ok = int_fits_all(ft->decl->underlying, t);
        } else if (k == K_CONVERT_CHECKED) {
            ok = (ft->kind == T_INT || ft->kind == T_CHAR) &&
                 (t->kind == T_INT || t->kind == T_CHAR || t->kind == T_ENUM);
            if (ft->kind == T_CHAR && t->kind != T_INT) ok = 0;
        } else if (k == K_TRUNCATE) {
            ok = ft->kind == T_INT && t->kind == T_INT;
        } else {
            int fp = ft->kind == T_PTR || ft->kind == T_OPT || ft->kind == T_FUNC ||
                     (ft->kind == T_INT && ft->is_size);
            int tp = t->kind == T_PTR || t->kind == T_OPT || t->kind == T_FUNC ||
                     (t->kind == T_INT && t->is_size);
            ok = fp && tp;
        }
        if (!ok) return err(&e->tok, D_CC_BAD_CONVERSION, NULL);
        e->type = t;
        if (e->a->is_const && (k == K_CONVERT || k == K_TRUNCATE)) {
            set_const(e, cv_wrap(cv_bits(e->a->cval), t));
        }
        return 0;
    }
    case K_INDEX_CHECKED:
        return check_index_like(c, e, e->args[0], e->args[1]);
    case K_SLICE_CHECKED:
        return check_slice_like(c, e, e->args[0], e->args[1], e->args[2]);
    default: {
        /* checked / saturating arithmetic: two operands of one integer type */
        Expr *a = e->args[0], *b = e->args[1];
        if (check_expr(c, a, want) != 0 || check_expr(c, b, want) != 0) return -1;
        if (a->type->kind == T_UNTYPED && b->type->kind != T_UNTYPED) {
            if (coerce(c, a, b->type) != 0) return -1;
        } else if (b->type->kind == T_UNTYPED && a->type->kind != T_UNTYPED) {
            if (coerce(c, b, a->type) != 0) return -1;
        } else if (a->type->kind == T_UNTYPED) {
            if (want == NULL || coerce(c, a, want) != 0 || coerce(c, b, want) != 0) {
                return err(&e->tok, D_CC_NEED_INT, NULL);
            }
        }
        if (!ty_is_int(a->type) || !ty_equal(a->type, b->type)) {
            return err(&e->tok, D_CC_MIXED_TYPES, ty_name(a->type));
        }
        e->type = a->type;
        return 0;
    }
    }
}

static int fold_typed(Ctx *c, Expr *e)
{
    /* both operands are typed constants: evaluate with the run-time meaning */
    uint64_t x = cv_bits(e->a->cval), y = cv_bits(e->b->cval), r = 0;
    Type *t = e->a->type;
    int is_signed = t->kind == T_INT && t->is_signed;
    switch (e->tok.kind) {
    case K_PLUS: r = x + y; break;
    case K_MINUS: r = x - y; break;
    case K_STAR: r = x * y; break;
    case K_SLASH:
    case K_REM:
        if (y == 0) return err(&e->b->tok, D_DIV_ZERO, NULL);
        if (is_signed) {
            int64_t sx = (int64_t)x, sy = (int64_t)y;
            if (sx == INT64_MIN && sy == -1) r = e->tok.kind == K_SLASH ? x : 0;
            else r = (uint64_t)(e->tok.kind == K_SLASH ? sx / sy : sx % sy);
        } else {
            r = e->tok.kind == K_SLASH ? x / y : x % y;
        }
        break;
    case K_BAND: r = x & y; break;
    case K_BOR: r = x | y; break;
    case K_BXOR: r = x ^ y; break;
    default: return 0;
    }
    (void)c;
    set_const(e, cv_wrap(r, t));
    return 0;
}

static int check_binary(Ctx *c, Expr *e, Type *want)
{
    Kind k = e->tok.kind;
    Expr *a = e->a, *b = e->b;

    if (k == K_AND || k == K_OR) {
        if (check_expr(c, a, ty_bool) != 0 || coerce(c, a, ty_bool) != 0) return -1;
        if (check_expr(c, b, ty_bool) != 0 || coerce(c, b, ty_bool) != 0) return -1;
        e->type = ty_bool;
        if (a->is_const && b->is_const) {
            set_const(e, cv_from_u64(k == K_AND ? (a->cval.mag && b->cval.mag) : (a->cval.mag || b->cval.mag)));
        }
        return 0;
    }

    if (k == K_SHL || k == K_SHR) {
        Type *lt;
        if (check_expr(c, a, want) != 0) return -1;
        if (a->type->kind == T_UNTYPED) {
            if (want == NULL || !ty_is_int(want)) return err(&a->tok, D_CC_NEED_INT, NULL);
            if (coerce(c, a, want) != 0) return -1;
        }
        lt = a->type;
        if (!ty_is_int(lt)) return err(&a->tok, D_CC_NEED_INT, NULL);
        if (check_expr(c, b, NULL) != 0) return -1;
        if (b->type->kind == T_UNTYPED) {
            if (b->cval.neg || b->cval.mag >= (uint64_t)lt->bits) return err(&b->tok, D_CC_SHIFT_RANGE, NULL);
            b->type = ty_size;
        } else if (!ty_is_int(b->type) || b->type->is_signed) {
            return err(&b->tok, D_CC_NEED_INT, NULL);
        } else if (b->is_const && b->cval.mag >= (uint64_t)lt->bits) {
            return err(&b->tok, D_CC_SHIFT_RANGE, NULL);
        }
        e->type = lt;
        if (a->is_const && b->is_const) {
            uint64_t x = cv_bits(a->cval);
            unsigned n = (unsigned)b->cval.mag;
            uint64_t r;
            if (k == K_SHL) r = x << n;
            else if (lt->is_signed) {
                ConstVal v = cv_wrap(x, lt);
                r = (uint64_t)((int64_t)cv_bits(v) >> n);
            } else r = x >> n;
            set_const(e, cv_wrap(r, lt));
        }
        return 0;
    }

    {
        int cmp = k == K_EQ || k == K_NE || k == K_LT || k == K_GT || k == K_LE || k == K_GE;
        Type *operand_want = cmp ? NULL : want;
        Type *t;
        if (check_expr(c, a, operand_want) != 0 || check_expr(c, b, operand_want) != 0) return -1;
        /* untyped constants take the other side's type */
        if (a->type->kind == T_UNTYPED && b->type->kind == T_UNTYPED) {
            if (!cmp && want != NULL && ty_is_int(want) &&
                (k == K_BAND || k == K_BOR || k == K_BXOR)) {
                if (coerce(c, a, want) != 0 || coerce(c, b, want) != 0) return -1;
            } else {
                ConstVal r;
                int rc = 0;
                if (cmp) {
                    int less = (a->cval.neg && !b->cval.neg) ||
                               (a->cval.neg && b->cval.neg && a->cval.mag > b->cval.mag) ||
                               (!a->cval.neg && !b->cval.neg && a->cval.mag < b->cval.mag);
                    int eq = a->cval.neg == b->cval.neg && a->cval.mag == b->cval.mag;
                    int v = k == K_EQ ? eq : k == K_NE ? !eq : k == K_LT ? less :
                            k == K_GT ? (!less && !eq) : k == K_LE ? (less || eq) : !less;
                    e->type = ty_bool;
                    set_const(e, cv_from_u64((uint64_t)v));
                    return 0;
                }
                switch (k) {
                case K_PLUS: rc = cv_add(a->cval, b->cval, &r); break;
                case K_MINUS: rc = cv_add(a->cval, cv_make(!b->cval.neg, b->cval.mag), &r); break;
                case K_STAR: rc = cv_mul(a->cval, b->cval, &r); break;
                case K_SLASH:
                case K_REM:
                    if (b->cval.mag == 0) return err(&b->tok, D_DIV_ZERO, NULL);
                    if (k == K_SLASH) r = cv_make(a->cval.neg != b->cval.neg, a->cval.mag / b->cval.mag);
                    else r = cv_make(a->cval.neg, a->cval.mag % b->cval.mag);
                    break;
                default:
                    if (a->cval.neg || b->cval.neg) return err(&e->tok, D_CC_NEED_INT, NULL);
                    r = cv_from_u64(k == K_BAND ? a->cval.mag & b->cval.mag :
                                    k == K_BOR ? a->cval.mag | b->cval.mag : a->cval.mag ^ b->cval.mag);
                }
                if (rc != 0) return err(&e->tok, D_CC_CONST_TOO_BIG, NULL);
                e->type = ty_untyped;
                set_const(e, r);
                return 0;
            }
        } else if (a->type->kind == T_UNTYPED || a->type->kind == T_NULLLIT) {
            if (coerce(c, a, b->type) != 0) return -1;
        } else if (b->type->kind == T_UNTYPED || b->type->kind == T_NULLLIT) {
            if (coerce(c, b, a->type) != 0) return -1;
        }
        t = a->type;
        if (cmp) {
            int ordered = k != K_EQ && k != K_NE;
            int ok = ty_equal(a->type, b->type) ||
                     (a->type->kind == T_PTR && b->type->kind == T_PTR && ty_equal(a->type->elem, b->type->elem)) ||
                     (a->type->kind == T_OPT && b->type->kind == T_OPT && ty_equal(a->type->elem->elem, b->type->elem->elem));
            if (!ok) return err(&e->tok, D_CC_MIXED_TYPES, ty_name(a->type));
            if (ordered ? !(t->kind == T_INT || t->kind == T_CHAR)
                        : !(t->kind == T_INT || t->kind == T_CHAR || t->kind == T_BOOL ||
                            t->kind == T_ENUM || t->kind == T_PTR || t->kind == T_OPT)) {
                return err(&e->tok, D_CC_NO_EQUALITY, NULL);
            }
            e->type = ty_bool;
            if (a->is_const && b->is_const && t->kind != T_PTR && t->kind != T_OPT) {
                int64_t x = (int64_t)cv_bits(a->cval), y = (int64_t)cv_bits(b->cval);
                uint64_t ux = cv_bits(a->cval), uy = cv_bits(b->cval);
                int sg = t->kind == T_INT && t->is_signed;
                int less = sg ? x < y : ux < uy;
                int eq = ux == uy;
                int v = k == K_EQ ? eq : k == K_NE ? !eq : k == K_LT ? less :
                        k == K_GT ? (!less && !eq) : k == K_LE ? (less || eq) : !less;
                set_const(e, cv_from_u64((uint64_t)v));
            }
            return 0;
        }
        if (!ty_is_int(t) || !ty_equal(a->type, b->type)) {
            return err(&e->tok, D_CC_MIXED_TYPES, ty_name(t));
        }
        e->type = t;
        if (a->is_const && b->is_const) return fold_typed(c, e);
        return 0;
    }
}

static int check_unary(Ctx *c, Expr *e, Type *want)
{
    Expr *a = e->a;
    switch (e->tok.kind) {
    case K_NOT:
        if (check_expr(c, a, ty_bool) != 0 || coerce(c, a, ty_bool) != 0) return -1;
        e->type = ty_bool;
        if (a->is_const) set_const(e, cv_from_u64(!a->cval.mag));
        return 0;
    case K_MINUS:
        if (check_expr(c, a, want) != 0) return -1;
        if (a->type->kind == T_UNTYPED) {
            e->type = ty_untyped;
            set_const(e, cv_make(!a->cval.neg, a->cval.mag));
            return 0;
        }
        if (!ty_is_int(a->type) || !a->type->is_signed) return err(&e->tok, D_CC_NEED_INT, NULL);
        e->type = a->type;
        if (a->is_const) set_const(e, cv_wrap(0u - cv_bits(a->cval), a->type));
        return 0;
    default: /* نفي_ب */
        if (check_expr(c, a, want) != 0) return -1;
        if (a->type->kind == T_UNTYPED) {
            if (want == NULL || !ty_is_int(want) || coerce(c, a, want) != 0) {
                return err(&e->tok, D_CC_NEED_INT, NULL);
            }
        }
        if (!ty_is_int(a->type)) return err(&e->tok, D_CC_NEED_INT, NULL);
        e->type = a->type;
        if (a->is_const) set_const(e, cv_wrap(~cv_bits(a->cval), a->type));
        return 0;
    }
}

static int check_literal_aggregate(Ctx *c, Expr *e, Type *want)
{
    size_t i;
    if (e->kind == E_STRUCT_LIT) {
        Type *t = resolve_type(c, e->type_arg, 1);
        Decl *d;
        if (t == NULL) return -1;
        if (t->kind != T_STRUCT) return err(&e->tok, D_CC_UNKNOWN_TYPE, NULL);
        d = t->decl;
        for (i = 0; i < e->nargs; i++) {
            size_t f, j;
            for (j = 0; j < i; j++) {
                if (bytes_eq(e->names[j], e->name_lens[j], e->names[i], e->name_lens[i])) {
                    return err_name(&e->args[i]->tok, D_CC_FIELD_DUP, e->names[i], e->name_lens[i]);
                }
            }
            for (f = 0; f < d->nfields; f++) {
                if (bytes_eq(d->fields[f].name, d->fields[f].len, e->names[i], e->name_lens[i])) break;
            }
            if (f == d->nfields) return err_name(&e->args[i]->tok, D_CC_NO_FIELD, e->names[i], e->name_lens[i]);
            if (check_expr(c, e->args[i], d->fields[f].type) != 0 ||
                coerce(c, e->args[i], d->fields[f].type) != 0) return -1;
        }
        for (i = 0; i < d->nfields; i++) {
            size_t j;
            for (j = 0; j < e->nargs; j++) {
                if (bytes_eq(d->fields[i].name, d->fields[i].len, e->names[j], e->name_lens[j])) break;
            }
            if (j == e->nargs) return err_name(&e->tok, D_CC_FIELD_MISSING, d->fields[i].name, d->fields[i].len);
        }
        e->type = t;
        return 0;
    }
    if (want == NULL || want->kind != T_ARRAY) return err(&e->tok, D_CC_TYPE_MISMATCH, "]…[");
    if (e->kind == E_ARRAY_LIT && e->nargs != want->count) return err(&e->tok, D_CC_COUNT, NULL);
    for (i = 0; i < e->nargs; i++) {
        if (check_expr(c, e->args[i], want->elem) != 0 || coerce(c, e->args[i], want->elem) != 0) return -1;
    }
    e->type = want;
    return 0;
}

static int check_expr(Ctx *c, Expr *e, Type *want)
{
    switch (e->kind) {
    case E_INT:
        e->type = ty_untyped;
        e->is_const = 1;
        return 0;
    case E_CHAR:
        e->type = ty_char;
        set_const(e, cv_from_u64(e->tok.value));
        return 0;
    case E_BOOL:
        e->type = ty_bool;
        e->is_const = 1;
        return 0;
    case E_NULL:
        e->type = ty_null;
        return 0;
    case E_STRING:
        e->type = ty_string;
        if (e->string_id == 0) {
            buf_bytes(&c->prog->strings, &e, sizeof e);
            e->string_id = (int)(c->prog->strings.len / sizeof e);
        }
        return 0;
    case E_NAME:
        return check_name(c, e);
    case E_MEMBER:
        return check_member(c, e);
    case E_CALL:
        return check_call(c, e);
    case E_INDEX:
        return check_index_like(c, e, e->a, e->b);
    case E_SLICE:
        return check_slice_like(c, e, e->a, e->b, e->c);
    case E_DEREF:
        if (check_expr(c, e->a, NULL) != 0) return -1;
        if (e->a->type->kind == T_OPT) return err(&e->tok, D_CC_OPT_DEREF, NULL);
        if (e->a->type->kind != T_PTR) return err(&e->tok, D_CC_TYPE_MISMATCH, "^");
        e->type = e->a->type->elem;
        return 0;
    case E_ADDR:
        if (check_expr(c, e->a, NULL) != 0) return -1;
        if (!is_lvalue(e->a)) return err(&e->tok, D_CC_NOT_LVALUE, NULL);
        e->type = ptr_to(c, e->a->type, !is_writable(e->a));
        return 0;
    case E_UNARY:
        return check_unary(c, e, want);
    case E_BINARY:
        return check_binary(c, e, want);
    case E_STRUCT_LIT:
    case E_ARRAY_LIT:
    case E_ARRAY_REPEAT:
        return check_literal_aggregate(c, e, want);
    case E_BUILTIN:
        return check_builtin(c, e, want);
    default:
        return err(&e->tok, D_CC_UNEXPECTED, NULL);
    }
}

/* ------------------------------------------------------------------ */
/* Statements                                                          */

static int check_stmt(Ctx *c, Stmt *s, int *terminates);

static int check_cond(Ctx *c, Expr *e)
{
    if (check_expr(c, e, ty_bool) != 0) return -1;
    if (e->type->kind != T_BOOL) return err(&e->tok, D_CC_NEED_BOOL, NULL);
    return 0;
}

static void da_intersect(DaSet *a, const DaSet *b)
{
    int i;
    for (i = 0; i < MAX_UNINIT; i++) a->b[i] = a->b[i] && b->b[i];
}

static int check_block(Ctx *c, Stmt *b, int *terminates)
{
    size_t mark = scope_mark(c), i;
    int rc = 0;
    *terminates = 0;
    for (i = 0; i < b->nstmts; i++) {
        int t = 0;
        if (check_stmt(c, b->stmts[i], &t) != 0) rc = -1;
        if (t) *terminates = 1;
    }
    scope_pop(c, mark);
    return rc;
}

/* checks a branch on its own copy of the definite-assignment set */
static int check_branch(Ctx *c, Stmt *s, DaSet *out, int *terminates)
{
    DaSet *saved = c->da;
    int rc;
    *out = *saved;
    c->da = out;
    rc = s->kind == S_BLOCK ? check_block(c, s, terminates) : check_stmt(c, s, terminates);
    c->da = saved;
    return rc;
}

static int new_local_var(Ctx *c, Local *l)
{
    l->type = resolve_type(c, l->type, 1);
    if (l->type == NULL) return -1;
    return declare_local(c, l);
}

static int check_loop_body(Ctx *c, Stmt *body)
{
    DaSet copy;
    int t, rc;
    c->loop_depth++;
    rc = check_branch(c, body, &copy, &t);
    c->loop_depth--;
    return rc;
}

static int stmt_breaks(const Stmt *s)
{
    size_t i;
    if (s == NULL) return 0;
    switch (s->kind) {
    case S_BREAK: return 1;
    case S_BLOCK:
        for (i = 0; i < s->nstmts; i++) if (stmt_breaks(s->stmts[i])) return 1;
        return 0;
    case S_IF: case S_IF_FOUND: return stmt_breaks(s->then_s) || stmt_breaks(s->else_s);
    case S_SWITCH:
        for (i = 0; i < s->ncases; i++) if (stmt_breaks(s->cases[i].body)) return 1;
        return 0;
    default: return 0; /* nested loops own their breaks */
    }
}

static int check_switch(Ctx *c, Stmt *s, int *terminates)
{
    Type *t;
    size_t i, j, k;
    int has_default = 0, all_term = 1;
    DaSet acc;
    int first = 1;
    if (check_expr(c, s->e1, NULL) != 0) return -1;
    t = s->e1->type;
    if (t->kind != T_INT && t->kind != T_ENUM && t->kind != T_CHAR) {
        return err(&s->e1->tok, D_CC_NEED_INT, NULL);
    }
    for (i = 0; i < s->ncases; i++) {
        Case *cs = &s->cases[i];
        DaSet out;
        int term;
        if (cs->values == NULL) has_default = 1;
        for (j = 0; j < cs->nvalues; j++) {
            Expr *v = cs->values[j];
            if (check_expr(c, v, t) != 0 || coerce(c, v, t) != 0) return -1;
            if (!v->is_const) return err(&v->tok, D_CC_NOT_CONST, NULL);
            for (k = 0; k <= i; k++) {
                size_t m, lim = k == i ? j : s->cases[k].nvalues;
                for (m = 0; m < lim; m++) {
                    Expr *o = s->cases[k].values[m];
                    if (o->cval.neg == v->cval.neg && o->cval.mag == v->cval.mag) {
                        return err(&v->tok, D_CC_SWITCH_DUP, NULL);
                    }
                }
            }
        }
        if (check_branch(c, cs->body, &out, &term) != 0) return -1;
        if (!term) {
            if (first) { acc = out; first = 0; } else da_intersect(&acc, &out);
        }
        all_term = all_term && term;
    }
    if (!has_default) {
        if (t->kind != T_ENUM) return err(&s->tok, D_CC_SWITCH_DEFAULT, NULL);
        for (k = 0; k < t->decl->nmembers; k++) {
            int found = 0;
            for (i = 0; i < s->ncases && !found; i++) {
                for (j = 0; j < s->cases[i].nvalues; j++) {
                    Expr *v = s->cases[i].values[j];
                    if (v->cval.neg == t->decl->members[k].value.neg &&
                        v->cval.mag == t->decl->members[k].value.mag) { found = 1; break; }
                }
            }
            if (!found) {
                return err_name(&s->tok, D_CC_SWITCH_MISSING, t->decl->members[k].name, t->decl->members[k].len);
            }
        }
    }
    *terminates = all_term && s->ncases > 0;
    if (!first && !*terminates) {
        /* paths that fall out of the switch */
        *c->da = acc;
    }
    return 0;
}

static int check_stmt(Ctx *c, Stmt *s, int *terminates)
{
    *terminates = 0;
    switch (s->kind) {
    case S_BLOCK:
        return check_block(c, s, terminates);
    case S_VAR: {
        Local *l = s->local;
        l->type = resolve_type(c, l->type, 1);
        if (l->type == NULL) return -1;
        if (s->uninit) {
            if (c->nuninit >= MAX_UNINIT) return err(&s->tok, D_CC_NOT_SUPPORTED, "غير_مهيأ");
            l->uninit_index = c->nuninit++;
            c->da->b[l->uninit_index] = 0;
        } else if (check_expr(c, s->e1, l->type) != 0 || coerce(c, s->e1, l->type) != 0) {
            return -1;
        }
        return declare_local(c, l);
    }
    case S_CONST: {
        Local *l = s->local;
        Decl *d = arena_alloc(c->prog->arena, sizeof *d);
        d->kind = D_CONST;
        d->tok = l->tok;
        d->name = l->name;
        d->len = l->len;
        d->type = l->type;
        d->init = s->e1;
        if (resolve_const(c, d) != 0) return -1;
        l->type = d->type;
        l->const_decl = d;
        l->readonly = 1;
        s->const_decl = d;
        return declare_local(c, l);
    }
    case S_ASSIGN: {
        int rc;
        c->lhs = s->e1->kind == E_NAME;
        rc = check_expr(c, s->e1, NULL);
        c->lhs = 0;
        if (rc != 0) return -1;
        if (!is_lvalue(s->e1)) return err(&s->e1->tok, D_CC_NOT_LVALUE, NULL);
        if (!is_writable(s->e1)) {
            if (s->e1->kind == E_NAME && s->e1->local && s->e1->local->readonly) {
                return err_name(&s->e1->tok, D_CC_ASSIGN_LOOP, s->e1->name, s->e1->name_len);
            }
            return err(&s->e1->tok, D_CC_READONLY, NULL);
        }
        if (check_expr(c, s->e2, s->e1->type) != 0 || coerce(c, s->e2, s->e1->type) != 0) return -1;
        if (s->e1->kind == E_NAME && s->e1->local && s->e1->local->uninit_index >= 0) {
            c->da->b[s->e1->local->uninit_index] = 1;
        }
        return 0;
    }
    case S_EXPR:
        if (s->e1->kind != E_CALL) return err(&s->e1->tok, D_CC_UNEXPECTED, NULL);
        return check_expr(c, s->e1, NULL);
    case S_IF: {
        DaSet a, b;
        int ta = 0, tb = 0;
        if (check_cond(c, s->e1) != 0) return -1;
        if (check_branch(c, s->then_s, &a, &ta) != 0) return -1;
        if (s->else_s) {
            if (check_branch(c, s->else_s, &b, &tb) != 0) return -1;
            if (ta && !tb) *c->da = b;
            else if (!ta && tb) *c->da = a;
            else if (!ta && !tb) { da_intersect(&a, &b); *c->da = a; }
            *terminates = ta && tb;
        }
        return 0;
    }
    case S_IF_FOUND: {
        DaSet a, b;
        int ta = 0, tb = 0;
        size_t mark;
        Type *opt;
        if (check_expr(c, s->e1, NULL) != 0) return -1;
        opt = s->e1->type;
        s->local->type = resolve_type(c, s->local->type, 1);
        if (s->local->type == NULL) return -1;
        if (opt->kind != T_OPT) return err(&s->e1->tok, D_CC_TYPE_MISMATCH, "اختياري");
        if (s->local->type->kind != T_PTR || !assignable(opt->elem, s->local->type)) {
            return err(&s->local->tok, D_CC_TYPE_MISMATCH, ty_name(opt->elem));
        }
        mark = scope_mark(c);
        if (declare_local(c, s->local) != 0) return -1;
        if (check_branch(c, s->then_s, &a, &ta) != 0) return -1;
        scope_pop(c, mark);
        if (s->else_s) {
            if (check_branch(c, s->else_s, &b, &tb) != 0) return -1;
            if (ta && !tb) *c->da = b;
            else if (!ta && tb) *c->da = a;
            else if (!ta && !tb) { da_intersect(&a, &b); *c->da = a; }
            *terminates = ta && tb;
        }
        return 0;
    }
    case S_WHILE:
        if (check_cond(c, s->e1) != 0 || check_loop_body(c, s->then_s) != 0) return -1;
        *terminates = s->e1->is_const && s->e1->cval.mag && !stmt_breaks(s->then_s);
        return 0;
    case S_DO: {
        int t;
        c->loop_depth++;
        if (check_block(c, s->then_s, &t) != 0) { c->loop_depth--; return -1; }
        c->loop_depth--;
        if (check_cond(c, s->e1) != 0) return -1;
        *terminates = (t && !stmt_breaks(s->then_s)) ||
                      (s->e1->is_const && s->e1->cval.mag && !stmt_breaks(s->then_s));
        return 0;
    }
    case S_FOR_RANGE: {
        size_t mark = scope_mark(c);
        int rc;
        Local *l = s->local;
        l->type = resolve_type(c, l->type, 1);
        if (l->type == NULL) return -1;
        if (!ty_is_int(l->type)) return err(&l->tok, D_CC_NEED_INT, NULL);
        if (check_expr(c, s->e1, l->type) != 0 || coerce(c, s->e1, l->type) != 0) return -1;
        if (check_expr(c, s->e2, l->type) != 0 || coerce(c, s->e2, l->type) != 0) return -1;
        l->readonly = 1;
        if (declare_local(c, l) != 0) return -1;
        rc = check_loop_body(c, s->then_s);
        scope_pop(c, mark);
        return rc;
    }
    case S_FOR_EACH: {
        size_t mark = scope_mark(c);
        int rc;
        Type *seq;
        if (check_expr(c, s->e1, NULL) != 0) return -1;
        seq = s->e1->type;
        if (seq->kind != T_ARRAY && seq->kind != T_SLICE) return err(&s->e1->tok, D_CC_NOT_INDEXABLE, NULL);
        if (s->local2 != NULL) {
            if (new_local_var(c, s->local2) != 0) return -1;
            if (!ty_equal(s->local2->type, ty_size)) return err(&s->local2->tok, D_CC_TYPE_MISMATCH, "حجم");
            s->local2->readonly = 1;
        }
        s->local->type = resolve_type(c, s->local->type, 1);
        if (s->local->type == NULL) return -1;
        if (!ty_equal(s->local->type, seq->elem)) return err(&s->local->tok, D_CC_TYPE_MISMATCH, ty_name(seq->elem));
        s->local->readonly = 1;
        if (declare_local(c, s->local) != 0) return -1;
        rc = check_loop_body(c, s->then_s);
        scope_pop(c, mark);
        return rc;
    }
    case S_SWITCH:
        return check_switch(c, s, terminates);
    case S_BREAK:
    case S_CONTINUE:
        if (c->loop_depth == 0) return err(&s->tok, D_CC_BREAK_OUTSIDE, NULL);
        *terminates = 1;
        return 0;
    case S_RETURN:
        *terminates = 1;
        if (c->fn->ret == NULL) {
            if (s->e1 != NULL) return err(&s->tok, D_CC_RETURN, NULL);
            return 0;
        }
        if (s->e1 == NULL) return err(&s->tok, D_CC_RETURN, NULL);
        if (check_expr(c, s->e1, c->fn->ret) != 0 || coerce(c, s->e1, c->fn->ret) != 0) return -1;
        return 0;
    }
    return 0;
}

static int check_function(Ctx *c, Decl *d)
{
    DaSet da;
    size_t i;
    int term = 0, rc = 0;
    if (resolve_signature(c, d) != 0) return -1;
    if (d->is_extern) return 0;
    memset(&da, 1, sizeof da);
    c->fn = d;
    c->da = &da;
    c->nuninit = 0;
    c->loop_depth = 0;
    c->scope.len = 0;
    for (i = 0; i < d->nparams; i++) {
        if (declare_local(c, d->params[i]) != 0) rc = -1;
    }
    if (rc == 0 && check_block(c, d->body, &term) != 0) rc = -1;
    if (rc == 0 && d->ret != NULL && !term) {
        rc = err_name(&d->tok, D_CC_NO_RETURN, d->name, d->len);
    }
    d->nuninit = (size_t)c->nuninit;
    c->fn = NULL;
    c->da = NULL;
    return rc;
}

static int check_global_init(Ctx *c, Expr *e)
{
    size_t i;
    if (e->is_const || e->kind == E_STRING || e->kind == E_NULL) return 0;
    if (e->kind == E_STRUCT_LIT || e->kind == E_ARRAY_LIT || e->kind == E_ARRAY_REPEAT) {
        for (i = 0; i < e->nargs; i++) {
            if (check_global_init(c, e->args[i]) != 0) return -1;
        }
        return 0;
    }
    return err(&e->tok, D_CC_GLOBAL_INIT, NULL);
}

/* ------------------------------------------------------------------ */
/* Program                                                             */

int check_program(Program *p)
{
    Ctx c;
    size_t i, j, k;
    int rc = 0;
    Type *u8 = ty_int(8, 0);

    memset(&c, 0, sizeof c);
    c.prog = p;
    buf_init(&c.scope);
    ty_string = ty_new(p->arena, T_SLICE);
    ty_string->elem = u8;
    ty_string->readonly = 1;

    /* duplicates inside each module */
    for (i = 0; i < p->nmodules; i++) {
        Module *m = p->modules[i];
        use_module(&c, m);
        for (j = 0; j < m->ndecls; j++) {
            for (k = 0; k < j; k++) {
                if (bytes_eq(m->decls[j]->name, m->decls[j]->len, m->decls[k]->name, m->decls[k]->len)) {
                    rc = err_name(&m->decls[j]->tok, D_CC_REDEFINED, m->decls[j]->name, m->decls[j]->len);
                }
            }
            if (module_import(m, m->decls[j]->name, m->decls[j]->len)) {
                rc = err_name(&m->decls[j]->tok, D_CC_REDEFINED, m->decls[j]->name, m->decls[j]->len);
            }
        }
    }
    if (rc != 0) return rc;

    /* every declaration */
    for (i = 0; i < p->nmodules; i++) {
        Module *m = p->modules[i];
        for (j = 0; j < m->ndecls; j++) {
            Decl *d = m->decls[j];
            use_module(&c, m);
            switch (d->kind) {
            case D_STRUCT: if (layout_struct(&c, d) != 0) rc = -1; break;
            case D_ENUM: if (resolve_enum(&c, d) != 0) rc = -1; break;
            case D_CONST: if (resolve_const(&c, d) != 0) rc = -1; break;
            case D_FUNC: if (resolve_signature(&c, d) != 0) rc = -1; break;
            case D_VAR:
                d->type = resolve_type(&c, d->type, 1);
                if (d->type == NULL ||
                    check_expr(&c, d->init, d->type) != 0 || coerce(&c, d->init, d->type) != 0 ||
                    check_global_init(&c, d->init) != 0) rc = -1;
                break;
            }
        }
    }
    if (rc != 0) return rc;

    for (i = 0; i < p->nmodules; i++) {
        Module *m = p->modules[i];
        for (j = 0; j < m->ndecls; j++) {
            if (m->decls[j]->kind == D_FUNC) {
                use_module(&c, m);
                if (check_function(&c, m->decls[j]) != 0) rc = -1;
            }
        }
    }

    /* the entry point: عام دالة رئيسية)[وسائط: ][][ثابت طبيعي٨]( : طبيعي٨ */
    {
        Decl *mainf = module_decl(p->root, (const uint8_t *)"رئيسية", strlen("رئيسية"));
        int ok = mainf != NULL && mainf->kind == D_FUNC && mainf->is_public && !mainf->is_extern &&
                 mainf->ret != NULL && ty_equal(mainf->ret, u8) && mainf->nparams <= 1;
        if (ok && mainf->nparams == 1) {
            Type *t = mainf->params[0]->type;
            ok = t->kind == T_SLICE && !t->readonly && ty_equal(t->elem, ty_string);
        }
        if (!ok) {
            use_module(&c, p->root);
            diag_error(1, 1, D_CC_NO_MAIN, NULL, 0);
            rc = -1;
        }
        p->main_func = mainf;
    }
    buf_free(&c.scope);
    return rc;
}
