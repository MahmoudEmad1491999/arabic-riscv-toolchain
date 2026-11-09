/* iface.c - a module's interface as غرة source text (decision 36).
 *
 * The text holds what an importer needs: the module's imports (so names of
 * other modules' types resolve), every structure and enumeration (private
 * ones without عام, so exported signatures can use them but importers
 * cannot name them), and the exported constants (already evaluated),
 * variables and functions (no bodies). Its FNV-1a hash is the module's
 * interface hash. The text is in file order, like every source file. */
#include "cc.h"

#include <string.h>

#include "../common/diag.h"
#include "../common/number.h"
#include "../common/utf8.h"

typedef struct {
    Module *m;
    Buf *out;
    int err;
} Pr;

static void s(Pr *p, const char *t) { buf_bytes(p->out, t, strlen(t)); }
static void b(Pr *p, const uint8_t *t, size_t n) { buf_bytes(p->out, t, n); }

static void cp(Pr *p, uint32_t c)
{
    uint8_t e[4];
    b(p, e, utf8_encode(c, e));
}

static void num(Pr *p, uint64_t v)
{
    uint32_t cps[24];
    size_t n = num_format_dec(v, cps), i;
    for (i = 0; i < n; i++) cp(p, cps[i]);
}

static void cval(Pr *p, ConstVal v)
{
    if (v.neg && v.mag != 0) s(p, "-");
    num(p, v.mag);
}

/* the alias under which this module imports the module of d */
static void type_name(Pr *p, const Decl *d)
{
    size_t i;
    if (d->module != p->m) {
        for (i = 0; i < p->m->nimports; i++) {
            if (p->m->imports[i].module == d->module) break;
        }
        if (i == p->m->nimports) {
            p->err = 1; /* cannot be named from this module */
            return;
        }
        b(p, p->m->imports[i].alias, p->m->imports[i].alias_len);
        s(p, ".");
    }
    b(p, d->name, d->len);
}

static void type(Pr *p, const Type *t)
{
    static const char *const digits[] = {"٨", "٦١", "٢٣", "٤٦"};
    size_t i;
    switch (t->kind) {
    case T_INT:
        if (t->is_size) {
            s(p, "حجم");
        } else {
            s(p, t->is_signed ? "صحيح" : "طبيعي");
            s(p, digits[t->bits == 8 ? 0 : t->bits == 16 ? 1 : t->bits == 32 ? 2 : 3]);
        }
        break;
    case T_FLOAT: s(p, t->bits == 32 ? "حقيقي٢٣" : "حقيقي٤٦"); break;
    case T_BOOL: s(p, "منطقي"); break;
    case T_CHAR: s(p, "حرف"); break;
    case T_PTR:
        s(p, "^");
        if (t->readonly) s(p, "ثابت ");
        if (t->vol) s(p, "متطاير ");
        type(p, t->elem);
        break;
    case T_OPT: s(p, "اختياري "); type(p, t->elem); break;
    case T_ARRAY: s(p, "]"); num(p, t->count); s(p, "["); type(p, t->elem); break;
    case T_SLICE: s(p, "]["); if (t->readonly) s(p, "ثابت "); type(p, t->elem); break;
    case T_STRUCT: case T_ENUM: type_name(p, t->decl); break;
    case T_FUNC:
        s(p, "دالة)");
        for (i = 0; i < t->nparams; i++) {
            if (i) s(p, "، ");
            type(p, t->params[i]);
        }
        s(p, "(");
        if (t->elem != NULL && t->elem->kind != T_VOID) {
            s(p, ": ");
            type(p, t->elem);
        }
        break;
    default:
        p->err = 1;
        break;
    }
}

/* string and character contents: quote, backslash and control characters
 * become \}n{ (code point in file order); everything else is written as is */
static void lit_cp(Pr *p, uint32_t c, uint32_t quote)
{
    if (c == quote || c == '\\' || c < 0x20 || c == 0x7F) {
        s(p, "\\}");
        num(p, c);
        s(p, "{");
    } else {
        cp(p, c);
    }
}

static void value(Pr *p, const Decl *d)
{
    const Type *t = d->type;
    const Expr *e = d->init;
    size_t i;
    switch (t->kind) {
    case T_BOOL: s(p, e->cval.mag ? "صح" : "خطأ"); break;
    case T_CHAR: s(p, "'"); lit_cp(p, (uint32_t)e->cval.mag, '\''); s(p, "'"); break;
    case T_INT: cval(p, e->cval); break;
    case T_FLOAT: /* the exact bits: اعتبر)حقيقي٤٦، حول)طبيعي٤٦، ن(( */
        s(p, t->bits == 32 ? "اعتبر)حقيقي٢٣، حول)طبيعي٢٣، " : "اعتبر)حقيقي٤٦، حول)طبيعي٤٦، ");
        num(p, e->cval.mag);
        s(p, "((");
        break;
    case T_ENUM: {
        const Decl *en = t->decl;
        for (i = 0; i < en->nmembers; i++) {
            if (en->members[i].value.neg == e->cval.neg && en->members[i].value.mag == e->cval.mag) break;
        }
        if (i < en->nmembers) {
            type_name(p, en);
            s(p, ".");
            b(p, en->members[i].name, en->members[i].len);
        } else {
            s(p, "اعتبر)");
            type_name(p, en);
            s(p, "، ");
            cval(p, e->cval);
            s(p, "(");
        }
        break;
    }
    case T_SLICE: {
        const Expr *str = e;
        size_t pos = 0;
        while (str->kind == E_NAME && str->ref == REF_CONST) str = str->decl->init;
        if (str->kind != E_STRING) { p->err = 1; break; }
        s(p, "\"");
        while (pos < str->tok.str_len) {
            uint32_t c;
            if (utf8_decode(str->tok.str, str->tok.str_len, &pos, &c) != 0) {
                c = str->tok.str[pos++];
            }
            lit_cp(p, c, '"');
        }
        s(p, "\"");
        break;
    }
    default:
        p->err = 1;
        break;
    }
}

static void signature(Pr *p, const Decl *d)
{
    size_t i;
    s(p, ")");
    for (i = 0; i < d->nparams; i++) {
        if (i) s(p, "، ");
        b(p, d->params[i]->name, d->params[i]->len);
        s(p, ": ");
        type(p, d->params[i]->type);
    }
    s(p, "(");
    if (d->ret != NULL) {
        s(p, ": ");
        type(p, d->ret);
    }
}

int iface_text(Module *m, Buf *out)
{
    Pr p;
    size_t i, j;
    memset(&p, 0, sizeof p);
    p.m = m;
    p.out = out;
    for (i = 0; i < m->nimports; i++) {
        s(&p, "استورد ");
        b(&p, m->imports[i].path, m->imports[i].path_len);
        s(&p, " باسم ");
        b(&p, m->imports[i].alias, m->imports[i].alias_len);
        s(&p, "؛\n");
    }
    for (i = 0; i < m->ndecls; i++) {
        Decl *d = m->decls[i];
        if (d->kind == D_STRUCT) {
            if (d->is_public) s(&p, "عام ");
            s(&p, d->packed ? "بنية مضغوطة " : "بنية ");
            b(&p, d->name, d->len);
            s(&p, " }\n");
            for (j = 0; j < d->nfields; j++) {
                s(&p, "    ");
                b(&p, d->fields[j].name, d->fields[j].len);
                s(&p, ": ");
                type(&p, d->fields[j].type);
                s(&p, "؛\n");
            }
            s(&p, "{\n");
        } else if (d->kind == D_ENUM) {
            if (d->is_public) s(&p, "عام ");
            s(&p, "تعداد ");
            b(&p, d->name, d->len);
            s(&p, ": ");
            type(&p, d->underlying);
            s(&p, " }\n");
            for (j = 0; j < d->nmembers; j++) {
                s(&p, "    ");
                b(&p, d->members[j].name, d->members[j].len);
                s(&p, " = ");
                cval(&p, d->members[j].value);
                s(&p, "؛\n");
            }
            s(&p, "{\n");
        } else if (!d->is_public) {
            continue;
        } else if (d->kind == D_CONST) {
            s(&p, "عام ثابت ");
            b(&p, d->name, d->len);
            s(&p, ": ");
            type(&p, d->type);
            s(&p, " = ");
            value(&p, d);
            s(&p, "؛\n");
        } else if (d->kind == D_VAR) {
            s(&p, d->is_volatile ? "عام متغير متطاير " : "عام متغير ");
            b(&p, d->name, d->len);
            s(&p, ": ");
            type(&p, d->type);
            s(&p, " = غير_مهيأ؛\n");
        } else if (d->kind == D_FUNC) {
            s(&p, d->raw_export ? "عام خارجي دالة " : "عام دالة ");
            b(&p, d->name, d->len);
            signature(&p, d);
            s(&p, "؛\n");
        }
    }
    return p.err ? -1 : 0;
}
