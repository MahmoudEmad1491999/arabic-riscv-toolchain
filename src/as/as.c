/* as.c - assembler core.
 *
 * Both passes parse the same token list with the same code. Pass 1 defines
 * labels and emits placeholder bytes so that every offset is final; pass 2
 * evaluates operands, encodes instructions and records relocations.
 * Encodings and relocation field contents follow GNU as with -mno-relax so
 * the output can be compared byte for byte (see docs/assumptions.md).
 */
#include "as.h"

#include "../common/diag.h"
#include "../common/elf.h"
#include "../common/obj.h"
#include "tables.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Symbols                                                             */

static uint32_t hash_name(const uint8_t *s, size_t len)
{
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < len; i++) {
        h = (h ^ s[i]) * 16777619u;
    }
    return h;
}

static Symbol *sym_get(Assembler *as, const uint8_t *name, size_t len)
{
    uint32_t b = hash_name(name, len) % SYM_BUCKETS;
    Symbol *s;
    uint8_t *copy;

    for (s = as->buckets[b]; s != NULL; s = s->next) {
        if (s->len == len && memcmp(s->name, name, len) == 0) {
            return s;
        }
    }
    s = arena_alloc(as->arena, sizeof *s);
    copy = arena_alloc(as->arena, len + 1);
    memcpy(copy, name, len);
    s->name = copy;
    s->len = len;
    s->section = SEC_UNDEF;
    s->next = as->buckets[b];
    as->buckets[b] = s;
    buf_bytes(&as->symbols, &s, sizeof s);
    return s;
}

/* Numeric local labels get GNU-compatible names: .L<n>^B<instance>. */
static size_t ascii_dec(uint64_t v, uint8_t *out)
{
    uint8_t tmp[24];
    size_t n = 0;
    size_t i;
    do {
        tmp[n++] = (uint8_t)('0' + v % 10u);
        v /= 10u;
    } while (v != 0);
    for (i = 0; i < n; i++) {
        out[i] = tmp[n - 1 - i];
    }
    return n;
}

static LocalCount *local_count(Assembler *as, uint64_t number)
{
    LocalCount *lc = (LocalCount *)as->locals.data;
    size_t n = as->locals.len / sizeof *lc;
    size_t i;
    LocalCount fresh;

    for (i = 0; i < n; i++) {
        if (lc[i].number == number) {
            return &lc[i];
        }
    }
    fresh.number = number;
    fresh.count = 0;
    buf_bytes(&as->locals, &fresh, sizeof fresh);
    return (LocalCount *)(as->locals.data + as->locals.len - sizeof fresh);
}

static Symbol *local_symbol(Assembler *as, uint64_t number, uint32_t instance)
{
    uint8_t name[64];
    size_t n = 0;
    name[n++] = '.';
    name[n++] = 'L';
    n += ascii_dec(number, name + n);
    name[n++] = 2;
    n += ascii_dec(instance, name + n);
    return sym_get(as, name, n);
}

/* ------------------------------------------------------------------ */
/* Tokens                                                              */

static Token *tok(Assembler *as)
{
    return &as->tl.toks[as->pos];
}

static Token *tok_at(Assembler *as, size_t ahead)
{
    size_t i = as->pos + ahead;
    if (i >= as->tl.count) {
        i = as->tl.count - 1;
    }
    return &as->tl.toks[i];
}

static void advance(Assembler *as)
{
    if (as->tl.toks[as->pos].kind != T_EOF) {
        as->pos++;
    }
}

static int error_tok(const Token *t, DiagId id)
{
    diag_error(t->line, t->col, id, NULL, 0);
    return -1;
}

static int error_name(const Token *t, DiagId id)
{
    diag_error(t->line, t->col, id, t->text, t->text_len);
    return -1;
}

static int expect_comma(Assembler *as)
{
    if (tok(as)->kind != T_COMMA) {
        return error_tok(tok(as), D_EXPECTED_COMMA);
    }
    advance(as);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Expressions                                                         */

typedef struct {
    int64_t v;
    Symbol *sym;      /* NULL: absolute value */
    int reloc;        /* RELOC_NONE / RELOC_HI / RELOC_LO */
    int provisional;  /* pass 1 guess, not usable where a constant is needed */
    int from_char;    /* a character literal */
} Value;

static int parse_expr(Assembler *as, Value *out);

static void value_abs(Value *v, int64_t x)
{
    memset(v, 0, sizeof *v);
    v->v = x;
}

static int parse_primary(Assembler *as, Value *out)
{
    Token *t = tok(as);

    switch (t->kind) {
    case T_NUMBER:
        value_abs(out, (int64_t)t->value);
        out->from_char = t->base == 0;
        advance(as);
        return 0;
    case T_LOCAL_REF: {
        LocalCount *lc = local_count(as, t->value);
        uint32_t instance = t->dir < 0 ? lc->count : lc->count + 1;
        value_abs(out, 0);
        out->sym = local_symbol(as, t->value, instance);
        if (as->pass == 2 && !out->sym->defined) {
            return error_tok(t, D_LOCAL_LABEL_MISSING);
        }
        out->sym->referenced = 1;
        advance(as);
        return 0;
    }
    case T_IDENT: {
        Symbol *s;
        if (t->text[0] == '.' || find_register(t->text, t->text_len) != -1) {
            return error_tok(t, D_EXPECTED_EXPR);
        }
        if (find_reloc_op(t->text, t->text_len) != RELOC_NONE) {
            return error_name(t, D_RESERVED_NAME);
        }
        s = sym_get(as, t->text, t->text_len);
        value_abs(out, 0);
        if (s->is_const) {
            out->v = (int64_t)s->value;
        } else {
            s->referenced = 1;
            out->sym = s;
        }
        advance(as);
        return 0;
    }
    case T_OPEN: {
        Token *open = t;
        advance(as);
        if (parse_expr(as, out) != 0) {
            return -1;
        }
        if (tok(as)->kind != T_CLOSE) {
            return error_tok(open, D_UNCLOSED_BRACKET);
        }
        if (tok(as)->square != open->square) {
            return error_tok(tok(as), D_BRACKET_MISMATCH);
        }
        advance(as);
        return 0;
    }
    default:
        return error_tok(t, D_EXPECTED_EXPR);
    }
}

static int parse_unary(Assembler *as, Value *out)
{
    if (tok(as)->kind == T_MINUS) {
        Token *t = tok(as);
        advance(as);
        if (parse_unary(as, out) != 0) {
            return -1;
        }
        if (out->sym != NULL || out->reloc != RELOC_NONE) {
            return error_tok(t, D_NOT_RELOCATABLE);
        }
        out->v = (int64_t)(0u - (uint64_t)out->v);
        out->from_char = 0;
        return 0;
    }
    return parse_primary(as, out);
}

static int parse_term(Assembler *as, Value *out)
{
    if (parse_unary(as, out) != 0) {
        return -1;
    }
    while (tok(as)->kind == T_STAR || tok(as)->kind == T_SLASH) {
        Token *op = tok(as);
        Value rhs;
        advance(as);
        if (parse_unary(as, &rhs) != 0) {
            return -1;
        }
        if (out->sym != NULL || rhs.sym != NULL) {
            return error_tok(op, D_NOT_RELOCATABLE);
        }
        if (op->kind == T_STAR) {
            out->v = (int64_t)((uint64_t)out->v * (uint64_t)rhs.v);
        } else {
            if (rhs.v == 0) {
                return error_tok(op, D_DIV_ZERO);
            }
            if (!(out->v == INT64_MIN && rhs.v == -1)) {
                out->v = out->v / rhs.v;
            }
        }
        out->provisional |= rhs.provisional;
        out->from_char = 0;
    }
    return 0;
}

static int parse_expr(Assembler *as, Value *out)
{
    if (parse_term(as, out) != 0) {
        return -1;
    }
    while (tok(as)->kind == T_PLUS || tok(as)->kind == T_MINUS) {
        Token *op = tok(as);
        Value rhs;
        advance(as);
        if (parse_term(as, &rhs) != 0) {
            return -1;
        }
        out->provisional |= rhs.provisional;
        out->from_char = 0;
        if (op->kind == T_PLUS) {
            if (out->sym != NULL && rhs.sym != NULL) {
                return error_tok(op, D_NOT_RELOCATABLE);
            }
            if (out->sym == NULL) {
                out->sym = rhs.sym;
            }
            out->v = (int64_t)((uint64_t)out->v + (uint64_t)rhs.v);
        } else {
            if (rhs.sym != NULL) {
                if (out->sym == NULL) {
                    return error_tok(op, D_NOT_RELOCATABLE);
                }
                if (out->sym->defined && rhs.sym->defined) {
                    if (out->sym->section != rhs.sym->section) {
                        return error_tok(op, D_NOT_RELOCATABLE);
                    }
                    out->v = (int64_t)((uint64_t)out->v + out->sym->value -
                                       rhs.sym->value - (uint64_t)rhs.v);
                } else if (as->pass == 2) {
                    return error_tok(op, D_NOT_RELOCATABLE);
                } else {
                    out->provisional = 1;
                    out->v = 0;
                }
                out->sym = NULL;
                continue;
            }
            out->v = (int64_t)((uint64_t)out->v - (uint64_t)rhs.v);
        }
    }
    return 0;
}

/* An operand: an expression, or عالي:primary / أدنى:primary. */
static int parse_operand(Assembler *as, Value *out)
{
    Token *t = tok(as);
    int op = RELOC_NONE;

    if (t->kind == T_IDENT && tok_at(as, 1)->kind == T_COLON) {
        op = find_reloc_op(t->text, t->text_len);
    }
    if (op == RELOC_NONE) {
        return parse_expr(as, out);
    }
    advance(as);
    advance(as);
    if (parse_primary(as, out) != 0) {
        return -1;
    }
    out->reloc = op;
    return 0;
}

/* A value that must be known in pass 1 (sizes, alignment, constants). */
static int parse_constant(Assembler *as, int64_t *out)
{
    Token *t = tok(as);
    Value v;
    if (parse_expr(as, &v) != 0) {
        return -1;
    }
    if (v.sym != NULL || v.provisional) {
        return error_tok(t, D_NOT_CONSTANT);
    }
    *out = v.v;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Emission                                                            */

static Section *cur_sec(Assembler *as)
{
    return &as->sec[as->cur];
}

static uint64_t cur_offset(Assembler *as)
{
    if (as->cur == SEC_BSS) {
        return as->sec[SEC_BSS].size;
    }
    return cur_sec(as)->data.len;
}

static void add_reloc(Assembler *as, uint64_t offset, uint32_t type,
                      Symbol *sym, int64_t addend)
{
    Reloc r;
    if (as->pass != 2) {
        return;
    }
    r.offset = offset;
    r.type = type;
    r.sym = sym;
    r.addend = addend;
    buf_bytes(&cur_sec(as)->relocs, &r, sizeof r);
}

static int in_bss_error(Assembler *as, const Token *t)
{
    if (as->cur == SEC_BSS) {
        return error_tok(t, D_NOT_IN_BSS);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Instructions                                                        */

static int parse_reg(Assembler *as, uint32_t *r)
{
    Token *t = tok(as);
    int n;
    if (t->kind != T_IDENT) {
        return error_tok(t, D_EXPECTED_REGISTER);
    }
    n = find_register(t->text, t->text_len);
    if (n == -2) {
        return error_name(t, D_REGISTER_RANGE);
    }
    if (n < 0) {
        return error_tok(t, D_EXPECTED_REGISTER);
    }
    *r = (uint32_t)n;
    advance(as);
    return 0;
}

/* A control and status register: its name, or a constant 0..4095. */
static int parse_csr(Assembler *as, uint32_t *csr)
{
    Token *t = tok(as);
    Value v;
    int n;
    if (t->kind == T_IDENT && (n = find_csr(t->text, t->text_len)) >= 0) {
        *csr = (uint32_t)n;
        advance(as);
        return 0;
    }
    if (parse_operand(as, &v) != 0) {
        return -1;
    }
    *csr = 0;
    if (as->pass == 2) {
        if (v.sym != NULL || v.reloc != RELOC_NONE) {
            return error_tok(t, D_EXPECTED_CSR);
        }
        if (v.v < 0 || v.v > 4095) {
            return error_tok(t, D_VALUE_RANGE);
        }
        *csr = (uint32_t)v.v;
    }
    return 0;
}

/* A 5-bit unsigned constant (the immediate of the csr*i instructions). */
static int parse_uimm5(Assembler *as, uint32_t *out)
{
    Token *t = tok(as);
    Value v;
    if (parse_operand(as, &v) != 0) {
        return -1;
    }
    *out = 0;
    if (as->pass == 2) {
        if (v.sym != NULL || v.reloc != RELOC_NONE) {
            return error_tok(t, D_NOT_CONSTANT);
        }
        if (v.v < 0 || v.v > 31) {
            return error_tok(t, D_VALUE_RANGE);
        }
        *out = (uint32_t)v.v;
    }
    return 0;
}

static uint32_t enc_csr(const InsnDef *d, uint32_t rd, uint32_t csr, uint32_t rs1)
{
    return (csr << 20) | (rs1 << 15) | (d->funct3 << 12) | (rd << 7) | d->opcode;
}

/* base ± offset */
static int parse_mem(Assembler *as, uint32_t *base, Value *off)
{
    Token *sign;
    if (parse_reg(as, base) != 0) {
        return -1;
    }
    sign = tok(as);
    if (sign->kind != T_PLUS && sign->kind != T_MINUS) {
        if (sign->kind == T_NEWLINE) {
            value_abs(off, 0);
            return 0;
        }
        return error_tok(sign, D_EXPECTED_PLUS_MINUS);
    }
    advance(as);
    if (parse_operand(as, off) != 0) {
        return -1;
    }
    if (sign->kind == T_MINUS) {
        if (off->sym != NULL || off->reloc != RELOC_NONE) {
            return error_tok(sign, D_NOT_RELOCATABLE);
        }
        off->v = (int64_t)(0u - (uint64_t)off->v);
    }
    return 0;
}

static int64_t lo12(int64_t x)
{
    return (int64_t)(((uint64_t)x & 0xFFFu) ^ 0x800u) - 0x800;
}

static uint32_t hi20(int64_t x)
{
    return (uint32_t)((((uint64_t)x + 0x800u) >> 12) & 0xFFFFFu);
}

/* Resolves a 12-bit immediate (I or S form). Records LO12 relocations. */
static int imm12(Assembler *as, const Token *t, const Value *v, int store,
                 uint64_t at, int64_t *field)
{
    *field = 0;
    if (as->pass != 2) {
        return 0;
    }
    if (v->reloc == RELOC_HI || v->reloc == RELOC_PCREL_HI) {
        return error_tok(t, D_RELOC_NOT_ALLOWED);
    }
    if (v->reloc == RELOC_PCREL_LO) {
        /* GNU leaves the field zero; the linker fills it in */
        if (v->sym == NULL) {
            return error_tok(t, D_EXPECTED_SYMBOL);
        }
        add_reloc(as, at, store ? R_RISCV_PCREL_LO12_S : R_RISCV_PCREL_LO12_I, v->sym, v->v);
        return 0;
    }
    if (v->reloc == RELOC_LO) {
        *field = lo12(v->v);
        if (v->sym != NULL) {
            add_reloc(as, at, store ? R_RISCV_LO12_S : R_RISCV_LO12_I, v->sym, v->v);
        }
        return 0;
    }
    if (v->sym != NULL) {
        return error_tok(t, D_NEEDS_LO);
    }
    if (v->v < -2048 || v->v > 2047) {
        return error_tok(t, D_VALUE_RANGE);
    }
    *field = v->v;
    return 0;
}

static uint32_t enc_r(const InsnDef *d, uint32_t rd, uint32_t rs1, uint32_t rs2)
{
    return (d->funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (d->funct3 << 12) |
           (rd << 7) | d->opcode;
}

static uint32_t enc_i(const InsnDef *d, uint32_t rd, uint32_t rs1, int64_t imm)
{
    return (((uint32_t)imm & 0xFFFu) << 20) | (rs1 << 15) | (d->funct3 << 12) |
           (rd << 7) | d->opcode;
}

static uint32_t enc_s(const InsnDef *d, uint32_t rs1, uint32_t rs2, int64_t imm)
{
    uint32_t u = (uint32_t)imm;
    return (((u >> 5) & 0x7Fu) << 25) | (rs2 << 20) | (rs1 << 15) |
           (d->funct3 << 12) | ((u & 0x1Fu) << 7) | d->opcode;
}

static uint32_t enc_b(uint32_t funct3, uint32_t rs1, uint32_t rs2, int64_t off)
{
    uint32_t u = (uint32_t)off;
    return (((u >> 12) & 1u) << 31) | (((u >> 5) & 0x3Fu) << 25) | (rs2 << 20) |
           (rs1 << 15) | (funct3 << 12) | (((u >> 1) & 0xFu) << 8) |
           (((u >> 11) & 1u) << 7) | 0x63u;
}

static uint32_t enc_j(uint32_t rd, int64_t off)
{
    uint32_t u = (uint32_t)off;
    return (((u >> 20) & 1u) << 31) | (((u >> 1) & 0x3FFu) << 21) |
           (((u >> 11) & 1u) << 20) | (((u >> 12) & 0xFFu) << 12) |
           (rd << 7) | 0x6Fu;
}

/* Target of a branch or jump: a symbol plus constant. */
static int check_target_symbol(Assembler *as, const Token *t, const Value *v)
{
    (void)as;
    if (v->sym == NULL || v->reloc != RELOC_NONE) {
        return error_tok(t, D_BRANCH_TARGET);
    }
    return 0;
}

/* Conditional branch to a label in the current section (assumption A13). */
static int branch_word(Assembler *as, uint32_t funct3, uint32_t rs1, uint32_t rs2,
                       const Token *vt, const Value *v, uint64_t at, uint32_t *word)
{
    int64_t field = 0;
    if (check_target_symbol(as, vt, v) != 0) {
        return -1;
    }
    if (as->pass == 2) {
        if (!v->sym->defined || v->sym->section != as->cur) {
            return error_tok(vt, D_BRANCH_TARGET);
        }
        field = (int64_t)(v->sym->value + (uint64_t)v->v - at);
        if (field & 1) {
            return error_tok(vt, D_MISALIGNED_TARGET);
        }
        if (field < -4096 || field > 4094) {
            return error_tok(vt, D_BRANCH_RANGE);
        }
        add_reloc(as, at, R_RISCV_BRANCH, v->sym, v->v);
    }
    *word = enc_b(funct3, rs1, rs2, field);
    return 0;
}

/* jal rd، target. The field holds S + A - P like GNU, S = 0 when undefined. */
static int jal_word(Assembler *as, uint32_t rd, const Token *vt, const Value *v,
                    uint64_t at, uint32_t *word)
{
    int64_t field = 0;
    if (check_target_symbol(as, vt, v) != 0) {
        return -1;
    }
    if (as->pass == 2) {
        uint64_t s = v->sym->defined ? v->sym->value : 0;
        field = (int64_t)(s + (uint64_t)v->v - at);
        if (v->sym->defined && v->sym->section == as->cur) {
            if (field & 1) {
                return error_tok(vt, D_MISALIGNED_TARGET);
            }
            if (field < -(1 << 20) || field > (1 << 20) - 2) {
                return error_tok(vt, D_BRANCH_RANGE);
            }
        }
        add_reloc(as, at, R_RISCV_JAL, v->sym, v->v);
    }
    *word = enc_j(rd, field);
    return 0;
}

static int pseudo(Assembler *as, const InsnDef *d);

static int insn(Assembler *as, const InsnDef *d)
{
    Token *head = tok(as);
    uint32_t rd = 0, rs1 = 0, rs2 = 0;
    uint32_t word = 0;
    uint64_t at = cur_offset(as);
    Token *vt;
    Value v;
    int64_t field;

    advance(as);
    if (in_bss_error(as, head) != 0) {
        return -1;
    }

    switch (d->fmt) {
    case FMT_R:
        if (parse_reg(as, &rd) || expect_comma(as) || parse_reg(as, &rs1) ||
            expect_comma(as) || parse_reg(as, &rs2)) {
            return -1;
        }
        word = enc_r(d, rd, rs1, rs2);
        break;

    case FMT_I:
        if (parse_reg(as, &rd) || expect_comma(as) || parse_reg(as, &rs1) ||
            expect_comma(as)) {
            return -1;
        }
        vt = tok(as);
        if (parse_operand(as, &v) || imm12(as, vt, &v, 0, at, &field)) {
            return -1;
        }
        word = enc_i(d, rd, rs1, field);
        break;

    case FMT_SHIFT:
        if (parse_reg(as, &rd) || expect_comma(as) || parse_reg(as, &rs1) ||
            expect_comma(as)) {
            return -1;
        }
        vt = tok(as);
        if (parse_operand(as, &v) != 0) {
            return -1;
        }
        if (as->pass == 2) {
            if (v.sym != NULL || v.reloc != RELOC_NONE) {
                return error_tok(vt, D_NOT_CONSTANT);
            }
            if (v.v < 0 || (uint64_t)v.v > d->shamt_max) {
                return error_tok(vt, D_VALUE_RANGE);
            }
        }
        word = enc_i(d, rd, rs1, (int64_t)(d->funct7 | ((uint32_t)v.v & 0x3Fu)));
        break;

    case FMT_LOAD:
    case FMT_JALR:
        if (parse_reg(as, &rd) || expect_comma(as)) {
            return -1;
        }
        vt = tok(as);
        if (parse_mem(as, &rs1, &v) || imm12(as, vt, &v, 0, at, &field)) {
            return -1;
        }
        word = enc_i(d, rd, rs1, field);
        break;

    case FMT_STORE:
        if (parse_reg(as, &rs2) || expect_comma(as)) {
            return -1;
        }
        vt = tok(as);
        if (parse_mem(as, &rs1, &v) || imm12(as, vt, &v, 1, at, &field)) {
            return -1;
        }
        word = enc_s(d, rs1, rs2, field);
        break;

    case FMT_BRANCH:
        if (parse_reg(as, &rs1) || expect_comma(as) || parse_reg(as, &rs2) ||
            expect_comma(as)) {
            return -1;
        }
        vt = tok(as);
        if (parse_operand(as, &v) ||
            branch_word(as, d->funct3, rs1, rs2, vt, &v, at, &word)) {
            return -1;
        }
        break;

    case FMT_JAL:
        if (parse_reg(as, &rd) || expect_comma(as)) {
            return -1;
        }
        vt = tok(as);
        if (parse_operand(as, &v) || jal_word(as, rd, vt, &v, at, &word)) {
            return -1;
        }
        break;

    case FMT_LUI:
    case FMT_AUIPC: {
        uint32_t imm = 0;
        if (parse_reg(as, &rd) || expect_comma(as)) {
            return -1;
        }
        vt = tok(as);
        if (parse_operand(as, &v) != 0) {
            return -1;
        }
        if (as->pass == 2) {
            if (v.reloc == RELOC_LO || v.reloc == RELOC_PCREL_LO ||
                (v.reloc == RELOC_HI && d->fmt == FMT_AUIPC) ||
                (v.reloc == RELOC_PCREL_HI && d->fmt == FMT_LUI)) {
                return error_tok(vt, D_RELOC_NOT_ALLOWED);
            }
            if (v.reloc == RELOC_PCREL_HI) {
                if (v.sym == NULL) {
                    return error_tok(vt, D_EXPECTED_SYMBOL);
                }
                add_reloc(as, at, R_RISCV_PCREL_HI20, v.sym, v.v);
            } else if (v.reloc == RELOC_HI) {
                imm = hi20(v.v);
                if (v.sym != NULL) {
                    add_reloc(as, at, R_RISCV_HI20, v.sym, v.v);
                }
            } else if (v.sym != NULL) {
                return error_tok(vt, D_NEEDS_HI);
            } else if (v.v < 0 || v.v > 0xFFFFF) {
                return error_tok(vt, D_VALUE_RANGE);
            } else {
                imm = (uint32_t)v.v;
            }
        }
        word = (imm << 12) | (rd << 7) | d->opcode;
        break;
    }

    case FMT_FIXED:
        word = d->opcode;
        break;

    case FMT_CSR: {
        uint32_t csr;
        if (parse_reg(as, &rd) || expect_comma(as) || parse_csr(as, &csr) ||
            expect_comma(as) || parse_reg(as, &rs1)) {
            return -1;
        }
        word = enc_csr(d, rd, csr, rs1);
        break;
    }
    case FMT_CSRI: {
        uint32_t csr, imm;
        if (parse_reg(as, &rd) || expect_comma(as) || parse_csr(as, &csr) ||
            expect_comma(as) || parse_uimm5(as, &imm)) {
            return -1;
        }
        word = enc_csr(d, rd, csr, imm);
        break;
    }
    case FMT_CSR_READ: {
        uint32_t csr;
        if (parse_reg(as, &rd) || expect_comma(as) || parse_csr(as, &csr)) {
            return -1;
        }
        word = enc_csr(d, rd, csr, 0);
        break;
    }
    case FMT_CSR_WRITE: {
        uint32_t csr;
        if (parse_csr(as, &csr) || expect_comma(as) || parse_reg(as, &rs1)) {
            return -1;
        }
        word = enc_csr(d, 0, csr, rs1);
        break;
    }
    case FMT_CSRI_WRITE: {
        uint32_t csr, imm;
        if (parse_csr(as, &csr) || expect_comma(as) || parse_uimm5(as, &imm)) {
            return -1;
        }
        word = enc_csr(d, 0, csr, imm);
        break;
    }
    case FMT_SFENCE:
        /* no operands: every address, every address space */
        if (tok(as)->kind != T_NEWLINE &&
            (parse_reg(as, &rs1) || expect_comma(as) || parse_reg(as, &rs2))) {
            return -1;
        }
        word = d->opcode | (rs2 << 20) | (rs1 << 15);
        break;

    case FMT_PSEUDO:
        as->pos--; /* pseudo() reads the mnemonic itself */
        return pseudo(as, d);
    }

    buf_u32(&cur_sec(as)->data, word);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Pseudo-instructions (expanded exactly like GNU as)                  */

#define X_ZERO 0u
#define X_RA   1u
#define X_T1   6u

static uint32_t enc_imm(uint32_t opcode, uint32_t funct3, uint32_t rd, uint32_t rs1, int64_t imm)
{
    return (((uint32_t)imm & 0xFFFu) << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode;
}

typedef struct {
    uint32_t w[16];
    int n;
} Words;

static int is_sext32(int64_t v)
{
    return v == (int64_t)(int32_t)v;
}

/* GNU as load_const(): lui/addiw for 32-bit values, otherwise build the
 * upper part recursively, shift it into place and add the low 12 bits. */
static void load_const(Words *out, uint32_t rd, int64_t v)
{
    int64_t lower = lo12(v);
    uint64_t upper = (uint64_t)v - (uint64_t)lower;

    if (!is_sext32(v)) {
        int shift = 12;
        while (((upper >> shift) & 1u) == 0) {
            shift++;
        }
        load_const(out, rd, (int64_t)upper >> shift);
        out->w[out->n++] = enc_imm(0x13u, 1, rd, rd, shift);          /* slli */
        if (lower != 0) {
            out->w[out->n++] = enc_imm(0x13u, 0, rd, rd, lower);      /* addi */
        }
    } else {
        uint32_t hi_reg = X_ZERO;
        if (upper != 0) {
            out->w[out->n++] = ((((uint32_t)upper) >> 12) << 12) | (rd << 7) | 0x37u; /* lui */
            hi_reg = rd;
        }
        if (lower != 0 || hi_reg == X_ZERO) {
            out->w[out->n++] = enc_imm(0x1Bu, 0, rd, hi_reg, lower);  /* addiw */
        }
    }
}

static void li_words(Words *out, uint32_t rd, int64_t v)
{
    out->n = 0;
    if (v >= -2048 && v <= 2047) {
        out->w[out->n++] = enc_imm(0x13u, 0, rd, X_ZERO, v);          /* addi */
    } else {
        load_const(out, rd, v);
    }
}

/* Unnamed local label GNU creates for la: ".L0 " at the auipc. */
static Symbol *fake_label(Assembler *as, uint64_t value)
{
    static const uint8_t name[] = ".L0 ";
    Symbol *s = arena_alloc(as->arena, sizeof *s);
    s->name = name;
    s->len = 4;
    s->defined = 1;
    s->section = as->cur;
    s->value = value;
    s->referenced = 1;
    buf_bytes(&as->symbols, &s, sizeof s);
    return s;
}

static int expect_symbol_value(const Token *t, const Value *v)
{
    if (v->sym == NULL || v->reloc != RELOC_NONE) {
        return error_tok(t, D_EXPECTED_SYMBOL);
    }
    return 0;
}

static int pseudo(Assembler *as, const InsnDef *d)
{
    Token *head = tok(as);
    uint64_t at = cur_offset(as);
    uint32_t rd = 0, rs = 0, rs2 = 0;
    Words w;
    Token *vt;
    Value v;
    int i;

    advance(as);
    if (in_bss_error(as, head) != 0) {
        return -1;
    }
    w.n = 0;

    switch (d->pseudo) {
    case P_LI: {
        int64_t value;
        if (parse_reg(as, &rd) || expect_comma(as) || parse_constant(as, &value)) {
            return -1;
        }
        li_words(&w, rd, value);
        break;
    }
    case P_LA:
        if (parse_reg(as, &rd) || expect_comma(as)) {
            return -1;
        }
        vt = tok(as);
        if (parse_operand(as, &v) || expect_symbol_value(vt, &v)) {
            return -1;
        }
        if (as->pass == 2) {
            add_reloc(as, at, R_RISCV_PCREL_HI20, v.sym, v.v);
            add_reloc(as, at + 4, R_RISCV_PCREL_LO12_I, fake_label(as, at), 0);
        }
        w.w[w.n++] = (rd << 7) | 0x17u;                     /* auipc rd، 0 */
        w.w[w.n++] = enc_imm(0x13u, 0, rd, rd, 0);          /* addi rd، rd، 0 */
        break;
    case P_MV:
    case P_NOT:
    case P_NEG:
        if (parse_reg(as, &rd) || expect_comma(as) || parse_reg(as, &rs)) {
            return -1;
        }
        if (d->pseudo == P_MV) {
            w.w[w.n++] = enc_imm(0x13u, 0, rd, rs, 0);      /* addi rd، rs، 0 */
        } else if (d->pseudo == P_NOT) {
            w.w[w.n++] = enc_imm(0x13u, 4, rd, rs, -1);     /* xori rd، rs، -1 */
        } else {
            w.w[w.n++] = 0x40000033u | (rs << 20) | (rd << 7); /* sub rd، zero، rs */
        }
        break;
    case P_NOP:
        w.w[w.n++] = 0x00000013u;
        break;
    case P_RET:
        w.w[w.n++] = 0x00008067u;                           /* jalr zero، ra + 0 */
        break;
    case P_JR:
        if (parse_reg(as, &rs) != 0) {
            return -1;
        }
        w.w[w.n++] = (rs << 15) | 0x67u;                    /* jalr zero، rs + 0 */
        break;
    case P_J:
        vt = tok(as);
        if (parse_operand(as, &v) || jal_word(as, X_ZERO, vt, &v, at, &w.w[0])) {
            return -1;
        }
        w.n = 1;
        break;
    case P_CALL:
    case P_TAIL: {
        uint32_t tmp = d->pseudo == P_CALL ? X_RA : X_T1;
        uint32_t link = d->pseudo == P_CALL ? X_RA : X_ZERO;
        vt = tok(as);
        if (parse_operand(as, &v) || expect_symbol_value(vt, &v)) {
            return -1;
        }
        add_reloc(as, at, R_RISCV_CALL_PLT, v.sym, v.v);
        w.w[w.n++] = (tmp << 7) | 0x17u;                     /* auipc tmp، 0 */
        w.w[w.n++] = (tmp << 15) | (link << 7) | 0x67u;      /* jalr link، tmp + 0 */
        break;
    }
    case P_BEQZ:
    case P_BNEZ:
        if (parse_reg(as, &rs) || expect_comma(as)) {
            return -1;
        }
        vt = tok(as);
        if (parse_operand(as, &v) ||
            branch_word(as, d->pseudo == P_BEQZ ? 0 : 1, rs, X_ZERO, vt, &v, at, &w.w[0])) {
            return -1;
        }
        w.n = 1;
        break;
    case P_BGT:
    case P_BLE:
    case P_BGTU:
    case P_BLEU: {
        static const uint32_t f3[] = {4, 5, 6, 7}; /* blt bge bltu bgeu, operands swapped */
        if (parse_reg(as, &rs) || expect_comma(as) || parse_reg(as, &rs2) ||
            expect_comma(as)) {
            return -1;
        }
        vt = tok(as);
        if (parse_operand(as, &v) ||
            branch_word(as, f3[d->pseudo - P_BGT], rs2, rs, vt, &v, at, &w.w[0])) {
            return -1;
        }
        w.n = 1;
        break;
    }
    case P_NONE:
        break;
    }

    for (i = 0; i < w.n; i++) {
        buf_u32(&cur_sec(as)->data, w.w[i]);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Directives                                                          */

static int section_of(DirKind k)
{
    switch (k) {
    case DIR_TEXT: return SEC_TEXT;
    case DIR_DATA: return SEC_DATA;
    case DIR_RODATA: return SEC_RODATA;
    case DIR_BSS: return SEC_BSS;
    default: return -1;
    }
}

static int dir_data(Assembler *as, DirKind k, const Token *head)
{
    unsigned size = k == DIR_BYTE ? 1 : k == DIR_HALF ? 2 : k == DIR_WORD ? 4 : 8;

    if (in_bss_error(as, head) != 0) {
        return -1;
    }
    for (;;) {
        Token *vt = tok(as);
        Value v;
        uint64_t at = cur_offset(as);
        uint64_t x = 0;
        unsigned i;

        if (parse_operand(as, &v) != 0) {
            return -1;
        }
        if (as->pass == 2) {
            if (v.reloc != RELOC_NONE) {
                return error_tok(vt, D_RELOC_NOT_ALLOWED);
            }
            if (v.sym != NULL) {
                if (size < 4) {
                    return error_tok(vt, D_NOT_RELOCATABLE);
                }
                add_reloc(as, at, size == 4 ? R_RISCV_32 : R_RISCV_64, v.sym, v.v);
            } else {
                if (v.from_char && size == 1 && v.v > 127) {
                    return error_tok(vt, D_CHAR_TOO_WIDE);
                }
                if (size < 8) {
                    int64_t lo = -((int64_t)1 << (size * 8 - 1));
                    int64_t hi = ((int64_t)1 << (size * 8)) - 1;
                    if (v.v < lo || v.v > hi) {
                        return error_tok(vt, D_VALUE_RANGE);
                    }
                }
                x = (uint64_t)v.v;
            }
        }
        for (i = 0; i < size; i++) {
            buf_u8(&cur_sec(as)->data, (uint8_t)(x >> (8 * i)));
        }
        if (tok(as)->kind != T_COMMA) {
            return 0;
        }
        advance(as);
    }
}

static int dir_string(Assembler *as, const Token *head)
{
    if (in_bss_error(as, head) != 0) {
        return -1;
    }
    for (;;) {
        Token *t = tok(as);
        if (t->kind != T_STRING) {
            return error_tok(t, D_EXPECTED_STRING);
        }
        buf_bytes(&cur_sec(as)->data, t->str, t->str_len);
        buf_u8(&cur_sec(as)->data, 0);
        advance(as);
        if (tok(as)->kind != T_COMMA) {
            return 0;
        }
        advance(as);
    }
}

/* Code padding exactly as GNU as does it: a zero byte up to an even
 * offset, then one 2-byte c.nop (0x0001) if needed, then 4-byte nops. */
static void pad_text(Section *s, uint64_t pad)
{
    if (pad > 0 && s->data.len % 2 != 0) {
        buf_u8(&s->data, 0);
        pad--;
    }
    if (pad % 4 >= 2) {
        buf_u16(&s->data, 0x0001u);
        pad -= 2;
    }
    while (pad >= 4) {
        buf_u32(&s->data, 0x00000013u); /* nop */
        pad -= 4;
    }
    buf_zeros(&s->data, pad);
}

static int dir_align(Assembler *as)
{
    Token *t = tok(as);
    int64_t n;
    uint64_t align, off, pad;
    Section *s = cur_sec(as);

    if (parse_constant(as, &n) != 0) {
        return -1;
    }
    if (n < 0 || n > 16) {
        return error_tok(t, D_VALUE_RANGE);
    }
    align = (uint64_t)1 << n;
    if (align > s->align) {
        s->align = align;
    }
    off = cur_offset(as);
    pad = (align - off % align) % align;
    if (as->cur == SEC_BSS) {
        s->size += pad;
    } else if (as->cur == SEC_TEXT) {
        pad_text(s, pad);
    } else {
        buf_zeros(&s->data, pad);
    }
    return 0;
}

static int dir_zero(Assembler *as, const Token *head)
{
    Token *t = tok(as);
    int64_t n;
    (void)head;
    if (parse_constant(as, &n) != 0) {
        return -1;
    }
    if (n < 0 || n > ((int64_t)1 << 32)) {
        return error_tok(t, D_VALUE_RANGE);
    }
    if (as->cur == SEC_BSS) {
        as->sec[SEC_BSS].size += (uint64_t)n;
    } else {
        buf_zeros(&cur_sec(as)->data, (size_t)n);
    }
    return 0;
}

static int check_new_name(const Token *t)
{
    if (find_register(t->text, t->text_len) != -1 ||
        find_reloc_op(t->text, t->text_len) != RELOC_NONE) {
        return error_name(t, D_RESERVED_NAME);
    }
    return 0;
}

static int dir_equ(Assembler *as)
{
    Token *name = tok(as);
    Symbol *s;
    int64_t v;

    if (name->kind != T_IDENT || name->text[0] == '.') {
        return error_tok(name, D_EXPECTED_SYMBOL);
    }
    if (check_new_name(name) != 0) {
        return -1;
    }
    advance(as);
    if (expect_comma(as) != 0) {
        return -1;
    }
    if (as->pass == 2) {
        /* already defined in pass 1; just consume the expression */
        Value skip;
        return parse_expr(as, &skip);
    }
    if (parse_constant(as, &v) != 0) {
        return -1;
    }
    s = sym_get(as, name->text, name->text_len);
    if (s->defined) {
        return error_name(name, D_REDEFINED_SYMBOL);
    }
    s->defined = 1;
    s->is_const = 1;
    s->section = SEC_ABS;
    s->value = (uint64_t)v;
    return 0;
}

static int dir_global(Assembler *as)
{
    for (;;) {
        Token *t = tok(as);
        Symbol *s;
        if (t->kind != T_IDENT || t->text[0] == '.') {
            return error_tok(t, D_EXPECTED_SYMBOL);
        }
        if (check_new_name(t) != 0) {
            return -1;
        }
        s = sym_get(as, t->text, t->text_len);
        s->global = 1;
        advance(as);
        if (tok(as)->kind != T_COMMA) {
            return 0;
        }
        advance(as);
    }
}

static int directive(Assembler *as)
{
    Token *head = tok(as);
    DirKind k = find_directive(head->text, head->text_len);
    int sec;

    advance(as);
    switch (k) {
    case DIR_NONE:
        return error_name(head, D_UNKNOWN_INSN);
    case DIR_TEXT:
    case DIR_DATA:
    case DIR_RODATA:
    case DIR_BSS:
        as->cur = section_of(k);
        return 0;
    case DIR_SECTION:
        if (tok(as)->kind != T_IDENT ||
            (sec = section_of(find_directive(tok(as)->text, tok(as)->text_len))) < 0) {
            return error_tok(tok(as), D_UNKNOWN_SECTION);
        }
        as->cur = sec;
        advance(as);
        return 0;
    case DIR_GLOBAL:
        return dir_global(as);
    case DIR_ALIGN:
        return dir_align(as);
    case DIR_BYTE:
    case DIR_HALF:
    case DIR_WORD:
    case DIR_DWORD:
        return dir_data(as, k, head);
    case DIR_STRING:
        return dir_string(as, head);
    case DIR_ZERO:
        return dir_zero(as, head);
    case DIR_EQU:
        return dir_equ(as);
    }
    return error_name(head, D_UNKNOWN_INSN);
}

/* ------------------------------------------------------------------ */
/* Lines                                                               */

static int define_label(Assembler *as)
{
    Token *t = tok(as);
    Symbol *s;

    if (t->kind == T_NUMBER) {
        LocalCount *lc;
        if (t->base != 10) {
            return error_tok(t, D_LOCAL_LABEL_BASE);
        }
        lc = local_count(as, t->value);
        lc->count++;
        s = local_symbol(as, t->value, lc->count);
    } else {
        if (check_new_name(t) != 0) {
            return -1;
        }
        s = sym_get(as, t->text, t->text_len);
    }
    advance(as);
    advance(as); /* ':' */
    if (as->pass == 1) {
        if (s->defined) {
            return error_name(t, D_REDEFINED_SYMBOL);
        }
        s->defined = 1;
        s->section = as->cur;
        s->value = cur_offset(as);
    }
    return 0;
}

static int is_label_start(Assembler *as)
{
    Token *t = tok(as);
    if (tok_at(as, 1)->kind != T_COLON) {
        return 0;
    }
    return (t->kind == T_IDENT && t->text[0] != '.') || t->kind == T_NUMBER;
}

static int statement(Assembler *as)
{
    Token *t;
    const InsnDef *d;

    while (is_label_start(as)) {
        if (define_label(as) != 0) {
            return -1;
        }
    }
    t = tok(as);
    if (t->kind == T_NEWLINE) {
        return 0;
    }
    if (t->kind != T_IDENT) {
        return error_tok(t, D_UNKNOWN_INSN);
    }
    if (t->text[0] == '.') {
        return directive(as);
    }
    d = find_insn(t->text, t->text_len);
    if (d == NULL) {
        return error_name(t, D_UNKNOWN_INSN);
    }
    return insn(as, d);
}

static void run_pass(Assembler *as, int pass)
{
    int i;

    as->pass = pass;
    as->pos = 0;
    as->cur = SEC_TEXT;
    as->locals.len = 0;
    for (i = 0; i < SEC_COUNT; i++) {
        as->sec[i].data.len = 0;
        as->sec[i].size = 0;
        as->sec[i].relocs.len = 0;
    }
    while (tok(as)->kind != T_EOF) {
        if (statement(as) != 0) {
            while (tok(as)->kind != T_NEWLINE && tok(as)->kind != T_EOF) {
                advance(as);
            }
        } else if (tok(as)->kind != T_NEWLINE) {
            error_tok(tok(as), D_EXTRA_TOKENS);
            while (tok(as)->kind != T_NEWLINE && tok(as)->kind != T_EOF) {
                advance(as);
            }
        }
        advance(as);
    }
    /* GNU as pads the end of .text to the section alignment. */
    {
        Section *t = &as->sec[SEC_TEXT];
        uint64_t len = t->data.len;
        pad_text(t, (t->align - len % t->align) % t->align);
    }
}

void as_init(Assembler *as, Arena *a, const TokenList *tl)
{
    int i;
    memset(as, 0, sizeof *as);
    as->arena = a;
    as->tl = *tl;
    buf_init(&as->symbols);
    buf_init(&as->locals);
    for (i = 0; i < SEC_COUNT; i++) {
        buf_init(&as->sec[i].data);
        buf_init(&as->sec[i].relocs);
        as->sec[i].align = 1;
    }
    as->sec[SEC_TEXT].align = 4;
}

void as_free(Assembler *as)
{
    int i;
    buf_free(&as->symbols);
    buf_free(&as->locals);
    for (i = 0; i < SEC_COUNT; i++) {
        buf_free(&as->sec[i].data);
        buf_free(&as->sec[i].relocs);
    }
}

int as_assemble(Assembler *as)
{
    run_pass(as, 1);
    if (diag_count() != 0) {
        return -1;
    }
    run_pass(as, 2);
    return diag_count() != 0 ? -1 : 0;
}

/* ------------------------------------------------------------------ */
/* ELF output                                                          */

static const char *const sec_names[SEC_COUNT] = {".text", ".data", ".rodata", ".bss"};
static const char *const rela_names[SEC_COUNT] = {
    ".rela.text", ".rela.data", ".rela.rodata", ".rela.bss"
};

static uint32_t strtab_add(Buf *b, const uint8_t *s, size_t len)
{
    uint32_t off = (uint32_t)b->len;
    buf_bytes(b, s, len);
    buf_u8(b, 0);
    return off;
}

static void sym_entry(Buf *b, uint32_t name, uint8_t info, uint16_t shndx, uint64_t value)
{
    buf_u32(b, name);
    buf_u8(b, info);
    buf_u8(b, 0);
    buf_u16(b, shndx);
    buf_u64(b, value);
    buf_u64(b, 0);
}

static void shdr(Buf *b, uint32_t name, uint32_t type, uint64_t flags,
                 uint64_t offset, uint64_t size, uint32_t link, uint32_t info,
                 uint64_t align, uint64_t entsize)
{
    buf_u32(b, name);
    buf_u32(b, type);
    buf_u64(b, flags);
    buf_u64(b, 0); /* addr */
    buf_u64(b, offset);
    buf_u64(b, size);
    buf_u32(b, link);
    buf_u32(b, info);
    buf_u64(b, align);
    buf_u64(b, entsize);
}

/* ELF section header indices: 1..4 = .text .data .rodata .bss,
 * 5 = .symtab, 6 = .strtab, 7 = .shstrtab, then one .rela.* per section
 * that has relocations. */
#define SHN_SYMTAB   5
#define SHN_STRTAB   6
#define SHN_SHSTRTAB 7

static int emit_symbol(const Symbol *s)
{
    int numeric_local = s->len > 2 && s->name[0] == '.' && s->name[1] == 'L';
    if (numeric_local) {
        return s->referenced; /* GNU as drops unused numeric labels */
    }
    return s->defined || s->referenced || s->global;
}

static int is_global_entry(const Symbol *s)
{
    return s->global || !s->defined;
}

int as_write_elf(Assembler *as, const char *path)
{
    Symbol **syms = (Symbol **)as->symbols.data;
    size_t nsyms = as->symbols.len / sizeof *syms;
    Buf out, symtab, strtab, shstr, shdrs;
    uint64_t sec_off[SEC_COUNT], rela_off[SEC_COUNT];
    uint64_t symtab_off, strtab_off, shstr_off, shdr_off;
    uint32_t sec_name[SEC_COUNT], rela_name[SEC_COUNT];
    uint32_t symtab_name, strtab_name, shstr_name;
    uint32_t index = 0, first_global;
    uint16_t nsec;
    size_t i;
    int k;
    int pass_global;
    int rc;

    buf_init(&out);
    buf_init(&symtab);
    buf_init(&strtab);
    buf_init(&shstr);
    buf_init(&shdrs);

    /* symbol table: null, section symbols, locals, then globals */
    buf_u8(&strtab, 0);
    sym_entry(&symtab, 0, 0, 0, 0);
    index = 1;
    for (k = 0; k < SEC_COUNT; k++) {
        sym_entry(&symtab, 0, (STB_LOCAL << 4) | STT_SECTION, (uint16_t)(k + 1), 0);
        index++;
    }
    first_global = 0;
    for (pass_global = 0; pass_global < 2; pass_global++) {
        if (pass_global == 1) {
            first_global = index;
        }
        for (i = 0; i < nsyms; i++) {
            Symbol *s = syms[i];
            uint16_t shndx;
            if (!emit_symbol(s) || is_global_entry(s) != pass_global) {
                continue;
            }
            if (!s->defined) {
                shndx = SHN_UNDEF;
            } else if (s->section == SEC_ABS) {
                shndx = (uint16_t)SHN_ABS;
            } else {
                shndx = (uint16_t)(s->section + 1);
            }
            s->elf_index = index++;
            sym_entry(&symtab, strtab_add(&strtab, s->name, s->len),
                      (uint8_t)(((pass_global ? STB_GLOBAL : STB_LOCAL) << 4) | STT_NOTYPE),
                      shndx, s->value);
        }
    }

    /* section names */
    buf_u8(&shstr, 0);
    for (k = 0; k < SEC_COUNT; k++) {
        sec_name[k] = strtab_add(&shstr, (const uint8_t *)sec_names[k], strlen(sec_names[k]));
        rela_name[k] = strtab_add(&shstr, (const uint8_t *)rela_names[k], strlen(rela_names[k]));
    }
    symtab_name = strtab_add(&shstr, (const uint8_t *)".symtab", 7);
    strtab_name = strtab_add(&shstr, (const uint8_t *)".strtab", 7);
    shstr_name = strtab_add(&shstr, (const uint8_t *)".shstrtab", 9);

    /* file contents */
    buf_zeros(&out, ELF_EHDR_SIZE);
    for (k = 0; k < SEC_COUNT; k++) {
        buf_align(&out, 8);
        sec_off[k] = out.len;
        if (k != SEC_BSS) {
            buf_bytes(&out, as->sec[k].data.data, as->sec[k].data.len);
        }
    }
    buf_align(&out, 8);
    symtab_off = out.len;
    buf_bytes(&out, symtab.data, symtab.len);
    strtab_off = out.len;
    buf_bytes(&out, strtab.data, strtab.len);
    shstr_off = out.len;
    buf_bytes(&out, shstr.data, shstr.len);
    for (k = 0; k < SEC_COUNT; k++) {
        Reloc *r = (Reloc *)as->sec[k].relocs.data;
        size_t n = as->sec[k].relocs.len / sizeof *r;
        buf_align(&out, 8);
        rela_off[k] = out.len;
        for (i = 0; i < n; i++) {
            buf_u64(&out, r[i].offset);
            buf_u64(&out, ((uint64_t)r[i].sym->elf_index << 32) | r[i].type);
            buf_u64(&out, (uint64_t)r[i].addend);
        }
    }
    buf_align(&out, 8);
    shdr_off = out.len;

    /* section headers */
    shdr(&shdrs, 0, SHT_NULL, 0, 0, 0, 0, 0, 0, 0);
    shdr(&shdrs, sec_name[SEC_TEXT], SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
         sec_off[SEC_TEXT], as->sec[SEC_TEXT].data.len, 0, 0, as->sec[SEC_TEXT].align, 0);
    shdr(&shdrs, sec_name[SEC_DATA], SHT_PROGBITS, SHF_ALLOC | SHF_WRITE,
         sec_off[SEC_DATA], as->sec[SEC_DATA].data.len, 0, 0, as->sec[SEC_DATA].align, 0);
    shdr(&shdrs, sec_name[SEC_RODATA], SHT_PROGBITS, SHF_ALLOC,
         sec_off[SEC_RODATA], as->sec[SEC_RODATA].data.len, 0, 0, as->sec[SEC_RODATA].align, 0);
    shdr(&shdrs, sec_name[SEC_BSS], SHT_NOBITS, SHF_ALLOC | SHF_WRITE,
         sec_off[SEC_BSS], as->sec[SEC_BSS].size, 0, 0, as->sec[SEC_BSS].align, 0);
    shdr(&shdrs, symtab_name, SHT_SYMTAB, 0, symtab_off, symtab.len,
         SHN_STRTAB, first_global, 8, ELF_SYM_SIZE);
    shdr(&shdrs, strtab_name, SHT_STRTAB, 0, strtab_off, strtab.len, 0, 0, 1, 0);
    shdr(&shdrs, shstr_name, SHT_STRTAB, 0, shstr_off, shstr.len, 0, 0, 1, 0);
    nsec = 8;
    for (k = 0; k < SEC_COUNT; k++) {
        size_t n = as->sec[k].relocs.len / sizeof(Reloc);
        if (n == 0) {
            continue;
        }
        shdr(&shdrs, rela_name[k], SHT_RELA, SHF_INFO_LINK, rela_off[k],
             n * ELF_RELA_SIZE, SHN_SYMTAB, (uint32_t)(k + 1), 8, ELF_RELA_SIZE);
        nsec++;
    }
    buf_bytes(&out, shdrs.data, shdrs.len);

    /* ELF header */
    {
        Buf h;
        static const uint8_t ident[16] = {0x7F, 'E', 'L', 'F', 2, 1, 1, 0};
        buf_init(&h);
        buf_bytes(&h, ident, 16);
        buf_u16(&h, ET_REL);
        buf_u16(&h, EM_RISCV);
        buf_u32(&h, 1);           /* version */
        buf_u64(&h, 0);           /* entry */
        buf_u64(&h, 0);           /* phoff */
        buf_u64(&h, shdr_off);
        buf_u32(&h, 0);           /* flags: RV64I, soft-float ABI */
        buf_u16(&h, ELF_EHDR_SIZE);
        buf_u16(&h, 0);           /* phentsize */
        buf_u16(&h, 0);           /* phnum */
        buf_u16(&h, ELF_SHDR_SIZE);
        buf_u16(&h, nsec);
        buf_u16(&h, SHN_SHSTRTAB);
        memcpy(out.data, h.data, ELF_EHDR_SIZE);
        buf_free(&h);
    }

    rc = rt_write_file(path, out.data, out.len);
    buf_free(&out);
    buf_free(&symtab);
    buf_free(&strtab);
    buf_free(&shstr);
    buf_free(&shdrs);
    return rc;
}


/* ------------------------------------------------------------------ */
/* Our own object format (.كائن), docs/spec-object.md                   */

static int is_fake_label(const Symbol *s)
{
    return s->len == 4 && memcmp(s->name, ".L0 ", 4) == 0;
}

static int obj_reloc_kind(uint32_t type)
{
    switch (type) {
    case R_RISCV_32: return OBJ_REL_WORD;
    case R_RISCV_64: return OBJ_REL_DWORD;
    case R_RISCV_BRANCH: return OBJ_REL_BRANCH;
    case R_RISCV_JAL: return OBJ_REL_JAL;
    case R_RISCV_CALL_PLT: return OBJ_REL_CALL;
    case R_RISCV_HI20: return OBJ_REL_HI;
    case R_RISCV_LO12_I: return OBJ_REL_LO_I;
    case R_RISCV_LO12_S: return OBJ_REL_LO_S;
    case R_RISCV_PCREL_HI20: return OBJ_REL_PCREL_HI;
    case R_RISCV_PCREL_LO12_I: return OBJ_REL_PCREL_LO_I;
    case R_RISCV_PCREL_LO12_S: return OBJ_REL_PCREL_LO_S;
    default: return 0;
    }
}

int as_write_obj(Assembler *as, const char *path, const uint8_t *iface, size_t iface_len)
{
    Symbol **syms = (Symbol **)as->symbols.data;
    size_t nsyms = as->symbols.len / sizeof *syms;
    Buf osyms, orels, out;
    ObjOut o;
    uint32_t index = 0;
    size_t i;
    int k, pass, rc;

    buf_init(&osyms);
    buf_init(&orels);
    buf_init(&out);
    for (pass = 0; pass < 2; pass++) { /* private first, then exported/imported */
        for (i = 0; i < nsyms; i++) {
            Symbol *s = syms[i];
            ObjSym y;
            if (is_fake_label(s) || !emit_symbol(s) || is_global_entry(s) != pass) {
                continue;
            }
            y.name = s->name;
            y.len = (uint32_t)s->len;
            y.shape = OBJ_SHAPE_UNKNOWN;
            y.size = 0;
            y.value = s->value;
            if (!s->defined) {
                y.kind = OBJ_IMPORTED;
                y.section = OBJ_SEC_UNDEF;
                y.value = 0;
            } else {
                y.kind = s->global ? OBJ_EXPORTED : OBJ_PRIVATE;
                y.section = s->section == SEC_ABS ? OBJ_SEC_ABS : (uint8_t)s->section;
            }
            s->elf_index = index++;
            buf_bytes(&osyms, &y, sizeof y);
        }
    }
    rc = 0;
    for (k = 0; k < SEC_COUNT && rc == 0; k++) {
        Reloc *r = (Reloc *)as->sec[k].relocs.data;
        size_t n = as->sec[k].relocs.len / sizeof *r;
        for (i = 0; i < n; i++) {
            ObjReloc x;
            x.offset = r[i].offset;
            x.section = (uint8_t)k;
            x.kind = (uint8_t)obj_reloc_kind(r[i].type);
            if (x.kind == OBJ_REL_PCREL_LO_I || x.kind == OBJ_REL_PCREL_LO_S) {
                const Symbol *l = r[i].sym;
                if (!l->defined || l->section != k || l->section == SEC_ABS) {
                    diag_plain(D_OBJ_PCREL_LABEL, NULL);
                    rc = -1;
                    break;
                }
                x.sym = OBJ_NO_SYMBOL;
                x.extra = (int64_t)l->value;
            } else {
                x.sym = r[i].sym->elf_index;
                x.extra = r[i].addend;
            }
            buf_bytes(&orels, &x, sizeof x);
        }
    }
    if (rc == 0) {
        memset(&o, 0, sizeof o);
        for (k = 0; k < SEC_COUNT; k++) {
            o.content[k] = k == SEC_BSS ? NULL : as->sec[k].data.data;
            o.size[k] = k == SEC_BSS ? as->sec[k].size : as->sec[k].data.len;
            o.align[k] = (uint32_t)as->sec[k].align;
        }
        o.syms = (const ObjSym *)osyms.data;
        o.nsyms = osyms.len / sizeof(ObjSym);
        o.relocs = (const ObjReloc *)orels.data;
        o.nrelocs = orels.len / sizeof(ObjReloc);
        o.iface = iface;
        o.iface_len = iface_len;
        obj_serialize(&o, &out);
        rc = rt_write_file(path, out.data, out.len);
    }
    buf_free(&osyms);
    buf_free(&orels);
    buf_free(&out);
    return rc;
}
