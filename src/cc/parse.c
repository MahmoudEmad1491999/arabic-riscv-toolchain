/* parse.c - recursive-descent parser for غرة (decision 32).
 *
 * One function per grammar rule. Precedence, lowest first:
 *   logic      أ و ب و ج  |  أ أو ب      (mixing و and أو needs brackets)
 *   not        ليس …
 *   compare    == != > < >= <=          (no chains: أ > ب > ج is an error)
 *   bitwise    و_ب أو_ب أو_حصري_ب رفع_ب خفض_ب   (no mixing with arithmetic,
 *              comparison or another bitwise operator without brackets)
 *   additive   + -
 *   multiply   * / باقي
 *   unary      - نفي_ب @
 *   postfix    call ) (, index ] [, slice ] : [, member ., dereference ^
 * The parser stops a module at its first syntax error.
 */
#include "cc.h"

#include "../common/diag.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Token helpers                                                       */

static Tok *cur(Parser *p) { return &p->toks[p->pos]; }
static Tok *peek(Parser *p, size_t k)
{
    size_t i = p->pos + k;
    return &p->toks[i < p->ntoks ? i : p->ntoks - 1];
}
static int is(Parser *p, Kind k) { return cur(p)->kind == k; }
static void next(Parser *p) { if (cur(p)->kind != K_EOF) p->pos++; }

static int fail(Parser *p, const Tok *t, DiagId id, const char *extra)
{
    if (p->errors == 0) {
        diag_error(t->line, t->col, id, (const uint8_t *)extra, extra ? strlen(extra) : 0);
    }
    p->errors++;
    return -1;
}

static int expect(Parser *p, Kind k)
{
    if (!is(p, k)) {
        return fail(p, cur(p), D_CC_EXPECTED, kind_text(k));
    }
    next(p);
    return 0;
}

static void *alloc(Parser *p, size_t n) { return arena_alloc(p->arena, n); }

/* growable pointer list in the arena */
typedef struct {
    void **items;
    size_t n, cap;
} List;

static void list_add(Parser *p, List *l, void *x)
{
    if (l->n == l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 8;
        void **items = alloc(p, cap * sizeof *items);
        if (l->n) memcpy(items, l->items, l->n * sizeof *items);
        l->items = items;
        l->cap = cap;
    }
    l->items[l->n++] = x;
}

static Expr *new_expr(Parser *p, ExprKind k, const Tok *t)
{
    Expr *e = alloc(p, sizeof *e);
    e->kind = k;
    e->tok = *t;
    return e;
}

static Stmt *new_stmt(Parser *p, StmtKind k, const Tok *t)
{
    Stmt *s = alloc(p, sizeof *s);
    s->kind = k;
    s->tok = *t;
    return s;
}

static Local *new_local(Parser *p, const Tok *name)
{
    Local *l = alloc(p, sizeof *l);
    l->name = name->text;
    l->len = name->len;
    l->tok = *name;
    l->uninit_index = -1;
    return l;
}

/* ------------------------------------------------------------------ */
/* Types                                                               */

static Expr *parse_expr(Parser *p);

static Type *parse_type(Parser *p)
{
    Tok *t = cur(p);
    Type *ty;

    switch (t->kind) {
    case K_CARET:
        next(p);
        ty = ty_new(p->arena, T_PTR);
        ty->tok = *t;
        if (is(p, K_CONST)) {
            ty->readonly = 1;
            next(p);
        }
        ty->elem = parse_type(p);
        return ty->elem ? ty : NULL;
    case K_OPTIONAL:
        next(p);
        if (!is(p, K_CARET)) {
            fail(p, cur(p), D_CC_EXPECTED, "^");
            return NULL;
        }
        ty = ty_new(p->arena, T_OPT);
        ty->tok = *t;
        ty->elem = parse_type(p);
        return ty->elem ? ty : NULL;
    case K_OPEN_SQUARE:
        next(p);
        if (is(p, K_CLOSE_SQUARE)) {
            next(p);
            ty = ty_new(p->arena, T_SLICE);
            ty->tok = *t;
            if (is(p, K_CONST)) {
                ty->readonly = 1;
                next(p);
            }
        } else {
            ty = ty_new(p->arena, T_ARRAY);
            ty->tok = *t;
            ty->count_expr = parse_expr(p);
            if (ty->count_expr == NULL || expect(p, K_CLOSE_SQUARE) != 0) return NULL;
        }
        ty->elem = parse_type(p);
        return ty->elem ? ty : NULL;
    case K_FUNC: {
        List params = {0};
        next(p);
        ty = ty_new(p->arena, T_FUNC);
        ty->tok = *t;
        if (expect(p, K_OPEN_PAREN) != 0) return NULL;
        while (!is(p, K_CLOSE_PAREN)) {
            Type *pt = parse_type(p);
            if (pt == NULL) return NULL;
            list_add(p, &params, pt);
            if (is(p, K_COMMA)) next(p); else break;
        }
        if (expect(p, K_CLOSE_PAREN) != 0) return NULL;
        ty->params = (Type **)params.items;
        ty->nparams = params.n;
        if (is(p, K_COLON)) {
            next(p);
            ty->elem = parse_type(p);
            if (ty->elem == NULL) return NULL;
        }
        return ty;
    }
    case K_IDENT: {
        Type *s = ty_scalar_by_name(t->text, t->len);
        next(p);
        if (s != NULL) return s;
        ty = ty_new(p->arena, T_NAMED);
        ty->tok = *t;
        if (is(p, K_DOT) && peek(p, 1)->kind == K_IDENT) {
            ty->qual = t->text;
            ty->qual_len = t->len;
            next(p);
            ty->tname = cur(p)->text;
            ty->tname_len = cur(p)->len;
            next(p);
        } else {
            ty->tname = t->text;
            ty->tname_len = t->len;
        }
        return ty;
    }
    default:
        fail(p, t, D_CC_NOT_TYPE_HERE, NULL);
        return NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Expressions                                                         */

static int is_arith_binary(const Expr *e)
{
    if (e == NULL || e->bracketed || e->kind != E_BINARY) return 0;
    switch (e->tok.kind) {
    case K_PLUS: case K_MINUS: case K_STAR: case K_SLASH: case K_REM: return 1;
    default: return 0;
    }
}

static int is_bit_kind(Kind k)
{
    return k == K_BAND || k == K_BOR || k == K_BXOR || k == K_SHL || k == K_SHR;
}

static int is_bit_binary(const Expr *e)
{
    return e != NULL && !e->bracketed && e->kind == E_BINARY && is_bit_kind(e->tok.kind);
}

static int is_cmp_kind(Kind k)
{
    return k == K_EQ || k == K_NE || k == K_LT || k == K_GT || k == K_LE || k == K_GE;
}

static Expr *binary(Parser *p, Tok *op, Expr *a, Expr *b)
{
    Expr *e = new_expr(p, E_BINARY, op);
    e->a = a;
    e->b = b;
    return e;
}

static Expr *parse_args_into(Parser *p, Expr *e, int count)
{
    /* ) a، b، … ( with exactly `count` expressions, or any number if count < 0 */
    List args = {0};
    int saved = p->no_struct_lit;
    if (expect(p, K_OPEN_PAREN) != 0) return NULL;
    p->no_struct_lit = 0; /* inside brackets a struct literal is unambiguous */
    while (!is(p, K_CLOSE_PAREN)) {
        Expr *a = parse_expr(p);
        if (a == NULL) return NULL;
        list_add(p, &args, a);
        if (is(p, K_COMMA)) next(p); else break;
    }
    p->no_struct_lit = saved;
    if (expect(p, K_CLOSE_PAREN) != 0) return NULL;
    e->args = (Expr **)args.items;
    e->nargs = args.n;
    if (count >= 0 && args.n != (size_t)count) {
        fail(p, &e->tok, D_CC_ARGS, NULL);
        return NULL;
    }
    return e;
}

static Expr *parse_struct_lit(Parser *p, Expr *type_name)
{
    Expr *e = new_expr(p, E_STRUCT_LIT, &type_name->tok);
    List vals = {0}, names = {0}, lens = {0};
    Type *t = ty_new(p->arena, T_NAMED);
    t->tok = type_name->tok;
    if (type_name->kind == E_NAME) {
        t->tname = type_name->name;
        t->tname_len = type_name->name_len;
    } else {
        t->qual = type_name->a->name;
        t->qual_len = type_name->a->name_len;
        t->tname = type_name->name;
        t->tname_len = type_name->name_len;
    }
    e->type_arg = t;
    next(p); /* } */
    while (!is(p, K_CLOSE_BRACE)) {
        Tok *n = cur(p);
        size_t *len;
        Expr *v;
        if (expect(p, K_IDENT) != 0 || expect(p, K_ASSIGN) != 0) return NULL;
        v = parse_expr(p);
        if (v == NULL) return NULL;
        len = alloc(p, sizeof *len);
        *len = n->len;
        list_add(p, &names, (void *)n->text);
        list_add(p, &lens, len);
        list_add(p, &vals, v);
        if (is(p, K_COMMA)) next(p); else break;
    }
    if (expect(p, K_CLOSE_BRACE) != 0) return NULL;
    e->args = (Expr **)vals.items;
    e->nargs = vals.n;
    e->names = (const uint8_t **)names.items;
    e->name_lens = alloc(p, (lens.n + 1) * sizeof(size_t));
    {
        size_t i;
        for (i = 0; i < lens.n; i++) e->name_lens[i] = *(size_t *)lens.items[i];
    }
    return e;
}

static Expr *parse_primary(Parser *p)
{
    Tok *t = cur(p);
    Expr *e;

    switch (t->kind) {
    case K_INT:
        e = new_expr(p, E_INT, t);
        e->cval = cv_from_u64(t->value);
        next(p);
        return e;
    case K_CHAR:
        e = new_expr(p, E_CHAR, t);
        next(p);
        return e;
    case K_STRING:
        e = new_expr(p, E_STRING, t);
        next(p);
        return e;
    case K_TRUE:
    case K_FALSE:
        e = new_expr(p, E_BOOL, t);
        e->cval.mag = t->kind == K_TRUE;
        next(p);
        return e;
    case K_NULL:
        e = new_expr(p, E_NULL, t);
        next(p);
        return e;
    case K_IDENT:
        e = new_expr(p, E_NAME, t);
        e->name = t->text;
        e->name_len = t->len;
        next(p);
        return e;
    case K_OPEN_PAREN: {
        int saved = p->no_struct_lit;
        next(p);
        p->no_struct_lit = 0;
        e = parse_expr(p);
        p->no_struct_lit = saved;
        if (e == NULL || expect(p, K_CLOSE_PAREN) != 0) return NULL;
        e->bracketed = 1;
        return e;
    }
    case K_OPEN_SQUARE: {
        List items = {0};
        int saved = p->no_struct_lit;
        next(p);
        p->no_struct_lit = 0;
        e = new_expr(p, E_ARRAY_LIT, t);
        while (!is(p, K_CLOSE_SQUARE)) {
            Expr *x = parse_expr(p);
            if (x == NULL) return NULL;
            list_add(p, &items, x);
            if (items.n == 1 && is(p, K_REPEAT)) {
                next(p);
                e->kind = E_ARRAY_REPEAT;
                break;
            }
            if (is(p, K_COMMA)) next(p); else break;
        }
        p->no_struct_lit = saved;
        if (expect(p, K_CLOSE_SQUARE) != 0) return NULL;
        e->args = (Expr **)items.items;
        e->nargs = items.n;
        return e;
    }
    case K_CONVERT: case K_CONVERT_CHECKED: case K_TRUNCATE: case K_REINTERPRET:
        e = new_expr(p, E_BUILTIN, t);
        next(p);
        if (expect(p, K_OPEN_PAREN) != 0) return NULL;
        e->type_arg = parse_type(p);
        if (e->type_arg == NULL || expect(p, K_COMMA) != 0) return NULL;
        e->a = parse_expr(p);
        if (e->a == NULL || expect(p, K_CLOSE_PAREN) != 0) return NULL;
        return e;
    case K_SIZEOF: case K_ALIGNOF:
        e = new_expr(p, E_BUILTIN, t);
        next(p);
        if (expect(p, K_OPEN_PAREN) != 0) return NULL;
        e->type_arg = parse_type(p);
        if (e->type_arg == NULL || expect(p, K_CLOSE_PAREN) != 0) return NULL;
        return e;
    case K_INDEX_CHECKED: case K_ADD_CHECKED: case K_SUB_CHECKED: case K_MUL_CHECKED:
    case K_ADD_SAT: case K_SUB_SAT: case K_MUL_SAT:
        e = new_expr(p, E_BUILTIN, t);
        next(p);
        return parse_args_into(p, e, 2);
    case K_SLICE_CHECKED:
        e = new_expr(p, E_BUILTIN, t);
        next(p);
        return parse_args_into(p, e, 3);
    default:
        fail(p, t, D_EXPECTED_EXPR, NULL);
        return NULL;
    }
}

static Expr *parse_postfix(Parser *p)
{
    Expr *e = parse_primary(p);
    while (e != NULL) {
        Tok *t = cur(p);
        if (t->kind == K_OPEN_PAREN) {
            Expr *c = new_expr(p, E_CALL, t);
            c->a = e;
            e = parse_args_into(p, c, -1);
        } else if (t->kind == K_OPEN_SQUARE) {
            Expr *lo;
            int saved = p->no_struct_lit;
            next(p);
            p->no_struct_lit = 0;
            lo = parse_expr(p);
            if (lo == NULL) return NULL;
            if (is(p, K_COLON)) {
                Expr *s = new_expr(p, E_SLICE, t);
                next(p);
                s->a = e;
                s->b = lo;
                s->c = parse_expr(p);
                if (s->c == NULL) return NULL;
                e = s;
            } else {
                Expr *x = new_expr(p, E_INDEX, t);
                x->a = e;
                x->b = lo;
                e = x;
            }
            p->no_struct_lit = saved;
            if (expect(p, K_CLOSE_SQUARE) != 0) return NULL;
        } else if (t->kind == K_DOT) {
            Expr *m;
            next(p);
            if (!is(p, K_IDENT)) {
                fail(p, cur(p), D_CC_EXPECTED, kind_text(K_IDENT));
                return NULL;
            }
            m = new_expr(p, E_MEMBER, cur(p));
            m->a = e;
            m->name = cur(p)->text;
            m->name_len = cur(p)->len;
            next(p);
            e = m;
        } else if (t->kind == K_CARET) {
            Expr *d = new_expr(p, E_DEREF, t);
            next(p);
            d->a = e;
            e = d;
        } else if (t->kind == K_OPEN_BRACE && !p->no_struct_lit && !e->bracketed &&
                   (e->kind == E_NAME || (e->kind == E_MEMBER && e->a->kind == E_NAME))) {
            e = parse_struct_lit(p, e);
        } else {
            break;
        }
    }
    return e;
}

static Expr *parse_unary(Parser *p)
{
    Tok *t = cur(p);
    if (t->kind == K_MINUS || t->kind == K_BNOT || t->kind == K_AT) {
        Expr *e = new_expr(p, t->kind == K_AT ? E_ADDR : E_UNARY, t);
        next(p);
        e->a = parse_unary(p);
        return e->a ? e : NULL;
    }
    return parse_postfix(p);
}

static Expr *parse_mul(Parser *p)
{
    Expr *e = parse_unary(p);
    while (e && (is(p, K_STAR) || is(p, K_SLASH) || is(p, K_REM))) {
        Tok *op = cur(p);
        Expr *b;
        next(p);
        b = parse_unary(p);
        e = b ? binary(p, op, e, b) : NULL;
    }
    return e;
}

static Expr *parse_add(Parser *p)
{
    Expr *e = parse_mul(p);
    while (e && (is(p, K_PLUS) || is(p, K_MINUS))) {
        Tok *op = cur(p);
        Expr *b;
        next(p);
        b = parse_mul(p);
        e = b ? binary(p, op, e, b) : NULL;
    }
    return e;
}

static Expr *parse_bit(Parser *p)
{
    Expr *e = parse_add(p);
    Kind first;
    if (e == NULL || !is_bit_kind(cur(p)->kind)) return e;
    first = cur(p)->kind;
    if (is_arith_binary(e)) {
        fail(p, cur(p), D_CC_NEED_BRACKETS, NULL);
        return NULL;
    }
    while (is_bit_kind(cur(p)->kind)) {
        Tok *op = cur(p);
        Expr *b;
        if (op->kind != first) {
            fail(p, op, D_CC_NEED_BRACKETS, NULL);
            return NULL;
        }
        next(p);
        b = parse_add(p);
        if (b == NULL) return NULL;
        if (is_arith_binary(b)) {
            fail(p, op, D_CC_NEED_BRACKETS, NULL);
            return NULL;
        }
        e = binary(p, op, e, b);
    }
    return e;
}

static Expr *parse_cmp(Parser *p)
{
    Expr *e = parse_bit(p);
    Tok *op;
    Expr *b;
    if (e == NULL || !is_cmp_kind(cur(p)->kind)) return e;
    op = cur(p);
    if (is_bit_binary(e)) {
        fail(p, op, D_CC_NEED_BRACKETS, NULL);
        return NULL;
    }
    next(p);
    b = parse_bit(p);
    if (b == NULL) return NULL;
    if (is_bit_binary(b)) {
        fail(p, op, D_CC_NEED_BRACKETS, NULL);
        return NULL;
    }
    if (is_cmp_kind(cur(p)->kind)) {
        fail(p, cur(p), D_CC_COMPARE_CHAIN, NULL);
        return NULL;
    }
    return binary(p, op, e, b);
}

static Expr *parse_not(Parser *p)
{
    if (is(p, K_NOT)) {
        Expr *e = new_expr(p, E_UNARY, cur(p));
        next(p);
        e->a = parse_not(p);
        return e->a ? e : NULL;
    }
    return parse_cmp(p);
}

static Expr *parse_expr(Parser *p)
{
    Expr *e = parse_not(p);
    Kind first;
    if (e == NULL || (!is(p, K_AND) && !is(p, K_OR))) return e;
    first = cur(p)->kind;
    while (is(p, K_AND) || is(p, K_OR)) {
        Tok *op = cur(p);
        Expr *b;
        if (op->kind != first) {
            fail(p, op, D_CC_NEED_BRACKETS, NULL);
            return NULL;
        }
        next(p);
        b = parse_not(p);
        if (b == NULL) return NULL;
        e = binary(p, op, e, b);
    }
    return e;
}

/* a condition: no struct literals, so `إذا أ == ب }` opens the block */
static Expr *parse_cond(Parser *p)
{
    int saved = p->no_struct_lit;
    Expr *e;
    p->no_struct_lit = 1;
    e = parse_expr(p);
    p->no_struct_lit = saved;
    return e;
}

/* ------------------------------------------------------------------ */
/* Statements                                                          */

static Stmt *parse_stmt(Parser *p);

static Stmt *parse_block(Parser *p)
{
    Stmt *b = new_stmt(p, S_BLOCK, cur(p));
    List items = {0};
    if (expect(p, K_OPEN_BRACE) != 0) return NULL;
    while (!is(p, K_CLOSE_BRACE)) {
        Stmt *s;
        if (is(p, K_EOF)) {
            fail(p, cur(p), D_CC_EXPECTED, kind_text(K_CLOSE_BRACE));
            return NULL;
        }
        s = parse_stmt(p);
        if (s == NULL) return NULL;
        list_add(p, &items, s);
    }
    next(p);
    b->stmts = (Stmt **)items.items;
    b->nstmts = items.n;
    return b;
}

/* name: type */
static Local *parse_binding(Parser *p)
{
    Tok *n = cur(p);
    Local *l;
    if (expect(p, K_IDENT) != 0 || expect(p, K_COLON) != 0) return NULL;
    l = new_local(p, n);
    l->type = parse_type(p);
    return l->type ? l : NULL;
}

static Stmt *parse_var(Parser *p, int is_const)
{
    Stmt *s = new_stmt(p, is_const ? S_CONST : S_VAR, cur(p));
    next(p);
    s->local = parse_binding(p);
    if (s->local == NULL || expect(p, K_ASSIGN) != 0) return NULL;
    if (!is_const && is(p, K_UNINIT)) {
        s->uninit = 1;
        next(p);
    } else {
        s->e1 = parse_expr(p);
        if (s->e1 == NULL) return NULL;
    }
    return expect(p, K_SEMI) == 0 ? s : NULL;
}

static Stmt *parse_if(Parser *p)
{
    Stmt *s = new_stmt(p, S_IF, cur(p));
    next(p); /* إذا */
    if (is(p, K_FOUND)) {
        next(p);
        s->kind = S_IF_FOUND;
        s->local = parse_binding(p);
        if (s->local == NULL || expect(p, K_ASSIGN) != 0) return NULL;
    }
    s->e1 = parse_cond(p);
    if (s->e1 == NULL) return NULL;
    s->then_s = parse_block(p);
    if (s->then_s == NULL) return NULL;
    if (is(p, K_ELSE) && peek(p, 1)->kind != K_COLON) {
        next(p);
        s->else_s = is(p, K_IF) ? parse_if(p) : parse_block(p);
        if (s->else_s == NULL) return NULL;
    }
    return s;
}

static Stmt *parse_switch(Parser *p)
{
    Stmt *s = new_stmt(p, S_SWITCH, cur(p));
    Buf cases;
    buf_init(&cases);
    next(p);
    s->e1 = parse_cond(p);
    if (s->e1 == NULL || expect(p, K_OPEN_BRACE) != 0) { buf_free(&cases); return NULL; }
    while (!is(p, K_CLOSE_BRACE)) {
        Case c;
        List vals = {0};
        memset(&c, 0, sizeof c);
        if (is(p, K_ELSE)) {
            next(p);
        } else if (is(p, K_CASE)) {
            next(p);
            for (;;) {
                Expr *v = parse_cond(p);
                if (v == NULL) { buf_free(&cases); return NULL; }
                list_add(p, &vals, v);
                if (is(p, K_COMMA)) next(p); else break;
            }
            c.values = (Expr **)vals.items;
            c.nvalues = vals.n;
        } else {
            fail(p, cur(p), D_CC_EXPECTED, kind_text(K_CASE));
            buf_free(&cases);
            return NULL;
        }
        if (expect(p, K_COLON) != 0) { buf_free(&cases); return NULL; }
        c.body = parse_block(p);
        if (c.body == NULL) { buf_free(&cases); return NULL; }
        buf_bytes(&cases, &c, sizeof c);
    }
    next(p);
    s->ncases = cases.len / sizeof(Case);
    s->cases = alloc(p, cases.len + sizeof(Case));
    if (cases.len) memcpy(s->cases, cases.data, cases.len);
    buf_free(&cases);
    return s;
}

static Stmt *parse_for(Parser *p)
{
    Stmt *s = new_stmt(p, S_FOR_RANGE, cur(p));
    next(p);
    s->local = parse_binding(p);
    if (s->local == NULL) return NULL;
    if (is(p, K_FROM)) {
        next(p);
        s->e1 = parse_cond(p);
        if (s->e1 == NULL || expect(p, K_BEFORE) != 0) return NULL;
        s->e2 = parse_cond(p);
        if (s->e2 == NULL) return NULL;
    } else {
        s->kind = S_FOR_EACH;
        if (is(p, K_COMMA)) {
            /* لكل ف: حجم، ع: نوع في ش */
            next(p);
            s->local2 = s->local;
            s->local = parse_binding(p);
            if (s->local == NULL) return NULL;
        }
        if (expect(p, K_IN) != 0) return NULL;
        s->e1 = parse_cond(p);
        if (s->e1 == NULL) return NULL;
    }
    s->then_s = parse_block(p);
    return s->then_s ? s : NULL;
}

static Stmt *parse_stmt(Parser *p)
{
    Tok *t = cur(p);
    Stmt *s;

    switch (t->kind) {
    case K_VAR: return parse_var(p, 0);
    case K_CONST: return parse_var(p, 1);
    case K_IF: return parse_if(p);
    case K_SWITCH: return parse_switch(p);
    case K_FOR: return parse_for(p);
    case K_OPEN_BRACE: return parse_block(p);
    case K_WHILE:
        s = new_stmt(p, S_WHILE, t);
        next(p);
        s->e1 = parse_cond(p);
        if (s->e1 == NULL) return NULL;
        s->then_s = parse_block(p);
        return s->then_s ? s : NULL;
    case K_DO:
        s = new_stmt(p, S_DO, t);
        next(p);
        s->then_s = parse_block(p);
        if (s->then_s == NULL || expect(p, K_WHILE) != 0) return NULL;
        s->e1 = parse_expr(p);
        if (s->e1 == NULL) return NULL;
        return expect(p, K_SEMI) == 0 ? s : NULL;
    case K_BREAK:
    case K_CONTINUE:
        s = new_stmt(p, t->kind == K_BREAK ? S_BREAK : S_CONTINUE, t);
        next(p);
        return expect(p, K_SEMI) == 0 ? s : NULL;
    case K_RETURN:
        s = new_stmt(p, S_RETURN, t);
        next(p);
        if (!is(p, K_SEMI)) {
            s->e1 = parse_expr(p);
            if (s->e1 == NULL) return NULL;
        }
        return expect(p, K_SEMI) == 0 ? s : NULL;
    default:
        s = new_stmt(p, S_EXPR, t);
        s->e1 = parse_expr(p);
        if (s->e1 == NULL) return NULL;
        if (is(p, K_ASSIGN)) {
            s->kind = S_ASSIGN;
            s->tok = *cur(p);
            next(p);
            s->e2 = parse_expr(p);
            if (s->e2 == NULL) return NULL;
        }
        return expect(p, K_SEMI) == 0 ? s : NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Declarations                                                        */

static Decl *new_decl(Parser *p, DeclKind k, const Tok *name)
{
    Decl *d = alloc(p, sizeof *d);
    d->kind = k;
    d->tok = *name;
    d->name = name->text;
    d->len = name->len;
    d->module = p->module;
    return d;
}

static Decl *parse_func(Parser *p, int pub, int ext)
{
    Tok *n;
    Decl *d;
    List params = {0};
    next(p); /* دالة */
    n = cur(p);
    if (expect(p, K_IDENT) != 0) return NULL;
    d = new_decl(p, D_FUNC, n);
    d->is_public = pub;
    if (expect(p, K_OPEN_PAREN) != 0) return NULL;
    while (!is(p, K_CLOSE_PAREN)) {
        Local *l = parse_binding(p);
        if (l == NULL) return NULL;
        l->is_param = 1;
        l->readonly = 1;
        list_add(p, &params, l);
        if (is(p, K_COMMA)) next(p); else break;
    }
    if (expect(p, K_CLOSE_PAREN) != 0) return NULL;
    d->params = (Local **)params.items;
    d->nparams = params.n;
    if (is(p, K_COLON)) {
        next(p);
        d->ret = parse_type(p);
        if (d->ret == NULL) return NULL;
    }
    if (ext && !pub) {
        d->is_extern = 1;
        return expect(p, K_SEMI) == 0 ? d : NULL;
    }
    d->raw_export = ext;
    d->body = parse_block(p);
    return d->body ? d : NULL;
}

static Decl *parse_struct(Parser *p, int pub)
{
    Tok *n;
    Decl *d;
    Buf fields;
    int packed = 0;
    next(p);
    if (is(p, K_PACKED)) {
        packed = 1;
        next(p);
    }
    n = cur(p);
    if (expect(p, K_IDENT) != 0) return NULL;
    d = new_decl(p, D_STRUCT, n);
    d->is_public = pub;
    d->packed = packed;
    if (expect(p, K_OPEN_BRACE) != 0) return NULL;
    buf_init(&fields);
    while (!is(p, K_CLOSE_BRACE)) {
        Field f;
        Local *b = parse_binding(p);
        if (b == NULL || expect(p, K_SEMI) != 0) { buf_free(&fields); return NULL; }
        memset(&f, 0, sizeof f);
        f.name = b->name;
        f.len = b->len;
        f.type = b->type;
        f.tok = b->tok;
        buf_bytes(&fields, &f, sizeof f);
    }
    next(p);
    d->nfields = fields.len / sizeof(Field);
    d->fields = alloc(p, fields.len + sizeof(Field));
    if (fields.len) memcpy(d->fields, fields.data, fields.len);
    buf_free(&fields);
    return d;
}

static Decl *parse_enum(Parser *p, int pub)
{
    Tok *n;
    Decl *d;
    Buf members;
    next(p);
    n = cur(p);
    if (expect(p, K_IDENT) != 0 || expect(p, K_COLON) != 0) return NULL;
    d = new_decl(p, D_ENUM, n);
    d->is_public = pub;
    d->underlying = parse_type(p);
    if (d->underlying == NULL || expect(p, K_OPEN_BRACE) != 0) return NULL;
    buf_init(&members);
    while (!is(p, K_CLOSE_BRACE)) {
        EnumMember m;
        memset(&m, 0, sizeof m);
        m.tok = *cur(p);
        m.name = cur(p)->text;
        m.len = cur(p)->len;
        if (expect(p, K_IDENT) != 0) { buf_free(&members); return NULL; }
        if (is(p, K_ASSIGN)) {
            next(p);
            m.value_expr = parse_expr(p);
            if (m.value_expr == NULL) { buf_free(&members); return NULL; }
        }
        if (expect(p, K_SEMI) != 0) { buf_free(&members); return NULL; }
        buf_bytes(&members, &m, sizeof m);
    }
    next(p);
    d->nmembers = members.len / sizeof(EnumMember);
    d->members = alloc(p, members.len + sizeof(EnumMember));
    if (members.len) memcpy(d->members, members.data, members.len);
    buf_free(&members);
    return d;
}

static Decl *parse_global(Parser *p, int pub, int is_const)
{
    Stmt *s = parse_var(p, is_const);
    Decl *d;
    if (s == NULL) return NULL;
    d = new_decl(p, is_const ? D_CONST : D_VAR, &s->local->tok);
    d->is_public = pub;
    d->type = s->local->type;
    d->init = s->e1;
    if (s->uninit) {
        fail(p, &s->tok, D_CC_UNINIT_GLOBAL, NULL);
        return NULL;
    }
    return d;
}

static int parse_import(Parser *p, Buf *imports)
{
    Import im;
    Buf path;
    Tok *last;
    memset(&im, 0, sizeof im);
    im.tok = *cur(p);
    next(p);
    buf_init(&path);
    for (;;) {
        last = cur(p);
        if (expect(p, K_IDENT) != 0) { buf_free(&path); return -1; }
        buf_bytes(&path, last->text, last->len);
        if (!is(p, K_DOT)) break;
        next(p);
        buf_u8(&path, '.');
    }
    im.path = alloc(p, path.len + 1);
    memcpy((uint8_t *)im.path, path.data, path.len);
    im.path_len = path.len;
    buf_free(&path);
    im.alias = last->text;
    im.alias_len = last->len;
    if (is(p, K_AS)) {
        next(p);
        if (!is(p, K_IDENT)) return fail(p, cur(p), D_CC_EXPECTED, kind_text(K_IDENT));
        im.alias = cur(p)->text;
        im.alias_len = cur(p)->len;
        next(p);
    }
    if (expect(p, K_SEMI) != 0) return -1;
    buf_bytes(imports, &im, sizeof im);
    return 0;
}

int parse_module(Arena *a, Module *m, Tok *toks, size_t ntoks)
{
    Parser p;
    Buf imports;
    List decls = {0};

    memset(&p, 0, sizeof p);
    p.arena = a;
    p.module = m;
    p.toks = toks;
    p.ntoks = ntoks;
    buf_init(&imports);

    while (is(&p, K_IMPORT)) {
        if (parse_import(&p, &imports) != 0) break;
    }
    while (p.errors == 0 && !is(&p, K_EOF)) {
        int pub = 0, ext = 0;
        Decl *d = NULL;
        if (is(&p, K_PUBLIC)) { pub = 1; next(&p); }
        if (is(&p, K_EXTERN)) { ext = 1; next(&p); }
        switch (cur(&p)->kind) {
        case K_FUNC: d = parse_func(&p, pub, ext); break;
        case K_VAR: if (!ext) d = parse_global(&p, pub, 0); break;
        case K_CONST: if (!ext) d = parse_global(&p, pub, 1); break;
        case K_STRUCT: if (!ext) d = parse_struct(&p, pub); break;
        case K_ENUM: if (!ext) d = parse_enum(&p, pub); break;
        default: break;
        }
        if (d == NULL) {
            fail(&p, cur(&p), D_CC_UNEXPECTED, NULL);
            break;
        }
        list_add(&p, &decls, d);
    }
    m->nimports = imports.len / sizeof(Import);
    m->imports = arena_alloc(a, imports.len + sizeof(Import));
    if (imports.len) memcpy(m->imports, imports.data, imports.len);
    buf_free(&imports);
    m->decls = (Decl **)decls.items;
    m->ndecls = decls.n;
    return p.errors ? -1 : 0;
}
