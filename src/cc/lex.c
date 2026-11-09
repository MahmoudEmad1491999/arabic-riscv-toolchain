/* lex.c - tokens of غرة. Shares the character and number rules with the
 * assembler (chars.c, number.c). */
#include "cc.h"

#include "../common/chars.h"
#include "../common/diag.h"
#include "../common/number.h"
#include "../common/utf8.h"

#include <string.h>

typedef struct {
    const char *word;
    Kind kind;
} Keyword;

static const Keyword keywords[] = {
    {"استورد", K_IMPORT}, {"باسم", K_AS}, {"عام", K_PUBLIC}, {"خارجي", K_EXTERN},
    {"دالة", K_FUNC}, {"متغير", K_VAR}, {"ثابت", K_CONST}, {"بنية", K_STRUCT},
    {"مضغوطة", K_PACKED}, {"متطاير", K_VOLATILE}, {"تعداد", K_ENUM}, {"إذا", K_IF}, {"وإلا", K_ELSE},
    {"وجد", K_FOUND}, {"طالما", K_WHILE}, {"كلما", K_DO}, {"لكل", K_FOR},
    {"من", K_FROM}, {"قبل", K_BEFORE}, {"في", K_IN}, {"حسب", K_SWITCH},
    {"حالة", K_CASE}, {"اقطع", K_BREAK}, {"تجاوز", K_CONTINUE}, {"ارجع", K_RETURN},
    {"صح", K_TRUE}, {"خطأ", K_FALSE}, {"لاشيء", K_NULL}, {"غير_مهيأ", K_UNINIT},
    {"اختياري", K_OPTIONAL}, {"و", K_AND}, {"أو", K_OR}, {"ليس", K_NOT},
    {"و_ب", K_BAND}, {"أو_ب", K_BOR}, {"أو_حصري_ب", K_BXOR}, {"نفي_ب", K_BNOT},
    {"رفع_ب", K_SHL}, {"خفض_ب", K_SHR}, {"باقي", K_REM}, {"مكرر", K_REPEAT},
    {"حول", K_CONVERT}, {"حول_بفحص", K_CONVERT_CHECKED}, {"اقتطع", K_TRUNCATE},
    {"اعتبر", K_REINTERPRET}, {"مقاس", K_SIZEOF}, {"محاذاة", K_ALIGNOF},
    {"عنصر_بفحص", K_INDEX_CHECKED}, {"شريحة_بفحص", K_SLICE_CHECKED}, {"شريحة_من", K_SLICE_FROM},
    {"جمع_بفحص", K_ADD_CHECKED}, {"طرح_بفحص", K_SUB_CHECKED}, {"ضرب_بفحص", K_MUL_CHECKED},
    {"جمع_مشبع", K_ADD_SAT}, {"طرح_مشبع", K_SUB_SAT}, {"ضرب_مشبع", K_MUL_SAT},
    {"قرب", K_ROUND}, {"قرب_لأقرب", K_ROUND_NEAREST}, {"أرضية", K_FLOOR}, {"سقف", K_CEIL},
    {"ليس_عددا", K_IS_NAN}, {"ضرب_جمع", K_FMA},
};

const char *kind_text(Kind k)
{
    size_t i;
    switch (k) {
    case K_EOF: return "نهاية الملف";
    case K_IDENT: return "اسم";
    case K_INT: return "عدد";
    case K_REAL: return "عدد حقيقي";
    case K_CHAR: return "حرف";
    case K_STRING: return "سلسلة";
    case K_COMMA: return "،";
    case K_SEMI: return "؛";
    case K_COLON: return ":";
    case K_DOT: return ".";
    case K_CARET: return "^";
    case K_AT: return "@";
    case K_PLUS: return "+";
    case K_MINUS: return "-";
    case K_STAR: return "*";
    case K_SLASH: return "/";
    case K_ASSIGN: return "=";
    case K_EQ: return "==";
    case K_NE: return "!=";
    case K_LT: return ">";   /* file-order glyphs (decision 21) */
    case K_GT: return "<";
    case K_LE: return ">=";
    case K_GE: return "<=";
    case K_OPEN_PAREN: return ")";
    case K_CLOSE_PAREN: return "(";
    case K_OPEN_BRACE: return "}";
    case K_CLOSE_BRACE: return "{";
    case K_OPEN_SQUARE: return "]";
    case K_CLOSE_SQUARE: return "[";
    default:
        break;
    }
    for (i = 0; i < sizeof keywords / sizeof keywords[0]; i++) {
        if (keywords[i].kind == k) {
            return keywords[i].word;
        }
    }
    return "?";
}

typedef struct {
    const uint32_t *cps;
    size_t count, pos, line_start;
    uint32_t line;
    Arena *arena;
    Buf toks;
} Lx;

static uint32_t at(const Lx *l, size_t k)
{
    return l->pos + k < l->count ? l->cps[l->pos + k] : 0;
}

static Tok *emit(Lx *l, Kind kind, size_t start)
{
    Tok t;
    memset(&t, 0, sizeof t);
    t.kind = kind;
    t.line = l->line;
    t.col = (uint32_t)(start - l->line_start + 1);
    buf_bytes(&l->toks, &t, sizeof t);
    return (Tok *)(l->toks.data + l->toks.len - sizeof t);
}

static int err(Lx *l, size_t pos, DiagId id)
{
    diag_error(l->line, (uint32_t)(pos - l->line_start + 1), id, NULL, 0);
    return -1;
}

static int name_char(uint32_t c)
{
    return ch_is_letter(c) || c == '_' || ch_digit_value(c) >= 0;
}

static int ascii_letter(uint32_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int after_word(Lx *l)
{
    uint32_t c = at(l, 0);
    if (ch_is_forbidden(c)) return err(l, l->pos, D_FORBIDDEN_CHAR);
    if (ascii_letter(c)) return err(l, l->pos, D_LATIN_LETTER);
    if (ch_is_ascii_digit(c)) return err(l, l->pos, D_ASCII_DIGIT);
    if (ch_is_persian_digit(c)) return err(l, l->pos, D_PERSIAN_DIGIT);
    return 0;
}

static int lex_number(Lx *l)
{
    size_t start = l->pos;
    NumResult r;
    Tok *t;
    if (num_real_ahead(l->cps, l->count, l->pos)) {
        uint64_t b64, b32;
        int flags = 0;
        num_parse_real(l->cps, l->count, l->pos, 32, &r, &b32);
        if (r.err == NUM_ERR_TOO_BIG) flags |= REAL_ERR_BIG32;
        if (r.err == NUM_ERR_TOO_SMALL) flags |= REAL_ERR_SMALL32;
        if (r.err == NUM_OK || r.err == NUM_ERR_TOO_BIG || r.err == NUM_ERR_TOO_SMALL) {
            num_parse_real(l->cps, l->count, l->pos, 64, &r, &b64);
            if (r.err == NUM_ERR_TOO_BIG) flags |= REAL_ERR_BIG64;
            if (r.err == NUM_ERR_TOO_SMALL) flags |= REAL_ERR_SMALL64;
        }
        if (r.err == NUM_ERR_SEPARATOR) return err(l, r.err_pos, D_NUM_SEPARATOR);
        if (r.err == NUM_ERR_OVERFLOW) return err(l, r.err_pos, D_NUM_REAL_DIGITS);
        l->pos = r.end;
        if (name_char(at(l, 0))) return err(l, l->pos, D_LETTER_AFTER_NUMBER);
        t = emit(l, K_REAL, start);
        t->value = b64;
        t->value32 = b32;
        t->real_err = flags;
        return after_word(l);
    }
    num_parse(l->cps, l->count, l->pos, &r);
    if (r.err != NUM_OK) {
        static const DiagId map[] = {
            D_UNEXPECTED_CHAR, D_UNEXPECTED_CHAR, D_NUM_NO_DIGITS,
            D_NUM_BAD_DIGIT, D_NUM_SEPARATOR, D_NUM_OVERFLOW
        };
        return err(l, r.err_pos, map[r.err]);
    }
    l->pos = r.end;
    if (name_char(at(l, 0))) {
        return err(l, l->pos, D_LETTER_AFTER_NUMBER);
    }
    t = emit(l, K_INT, start);
    t->value = r.value;
    return after_word(l);
}

static int lex_name(Lx *l)
{
    size_t start = l->pos;
    uint8_t *text;
    size_t n = 0, i;
    Tok *t;
    Kind kind = K_IDENT;

    while (l->pos < l->count && name_char(l->cps[l->pos])) {
        l->pos++;
    }
    if (after_word(l) != 0) {
        return -1;
    }
    text = arena_alloc(l->arena, (l->pos - start) * 4 + 1);
    for (i = start; i < l->pos; i++) {
        n += utf8_encode(l->cps[i], text + n);
    }
    for (i = 0; i < sizeof keywords / sizeof keywords[0]; i++) {
        if (bytes_eq_str(text, n, keywords[i].word)) {
            kind = keywords[i].kind;
            break;
        }
    }
    t = emit(l, kind, start);
    t->text = text;
    t->len = n;
    return 0;
}

/* one character of a string or character literal, escapes included */
static int lit_char(Lx *l, uint32_t quote, uint32_t *out)
{
    uint32_t c = at(l, 0);
    if (c != '\\') {
        l->pos++;
        *out = c;
        return 0;
    }
    c = at(l, 1);
    l->pos += 2;
    switch (c) {
    case CP_SEEN: *out = '\n'; return 0;
    case 0x0631u: *out = '\r'; return 0;
    case 0x062Cu: *out = '\t'; return 0;
    case CP_AR_ZERO: *out = 0; return 0;
    case '\\': *out = '\\'; return 0;
    case '}': {
        /* \}number{ : a code point, number in file order */
        NumResult r;
        num_parse(l->cps, l->count, l->pos, &r);
        if (r.err != NUM_OK || r.end >= l->count || l->cps[r.end] != '{' ||
            r.value > 0x10FFFF || (r.value >= 0xD800 && r.value <= 0xDFFF)) {
            return err(l, l->pos - 2, D_BAD_ESCAPE);
        }
        l->pos = r.end + 1;
        *out = (uint32_t)r.value;
        return 0;
    }
    default:
        if (c == quote) {
            *out = c;
            return 0;
        }
        return err(l, l->pos - 2, D_BAD_ESCAPE);
    }
}

static int lex_string(Lx *l)
{
    size_t start = l->pos;
    Buf b;
    Tok *t;
    uint8_t *copy;

    buf_init(&b);
    l->pos++;
    for (;;) {
        uint32_t c;
        uint8_t enc[4];
        if (l->pos >= l->count || at(l, 0) == '\n') {
            buf_free(&b);
            return err(l, start, D_UNTERMINATED_STRING);
        }
        if (at(l, 0) == '"') {
            l->pos++;
            break;
        }
        if (lit_char(l, '"', &c) != 0) {
            buf_free(&b);
            return -1;
        }
        buf_bytes(&b, enc, utf8_encode(c, enc));
    }
    copy = arena_alloc(l->arena, b.len + 1);
    if (b.len) {
        memcpy(copy, b.data, b.len);
    }
    t = emit(l, K_STRING, start);
    t->str = copy;
    t->str_len = b.len;
    buf_free(&b);
    return 0;
}

static int lex_char(Lx *l)
{
    size_t start = l->pos;
    uint32_t c;
    Tok *t;
    l->pos++;
    if (at(l, 0) == '\'' || at(l, 0) == '\n' || l->pos >= l->count) {
        return err(l, start, D_BAD_CHAR_LITERAL);
    }
    if (lit_char(l, '\'', &c) != 0) {
        return -1;
    }
    if (at(l, 0) != '\'') {
        return err(l, start, D_BAD_CHAR_LITERAL);
    }
    l->pos++;
    t = emit(l, K_CHAR, start);
    t->value = c;
    return 0;
}

static int lex_one(Lx *l)
{
    uint32_t c = at(l, 0), d = at(l, 1);
    size_t s = l->pos;

    if (ch_digit_value(c) >= 0) return lex_number(l);
    if (ch_is_letter(c) || c == '_') return lex_name(l);
    if (c == '"') return lex_string(l);
    if (c == '\'') return lex_char(l);

    l->pos++;
    switch (c) {
    case CP_AR_COMMA: emit(l, K_COMMA, s); return 0;
    case CP_AR_SEMICOLON: emit(l, K_SEMI, s); return 0;
    case ':': emit(l, K_COLON, s); return 0;
    case '.': emit(l, K_DOT, s); return 0;
    case '^': emit(l, K_CARET, s); return 0;
    case '@': emit(l, K_AT, s); return 0;
    case '+': emit(l, K_PLUS, s); return 0;
    case '-': emit(l, K_MINUS, s); return 0;
    case '*': emit(l, K_STAR, s); return 0;
    case '/': emit(l, K_SLASH, s); return 0;
    case ')': emit(l, K_OPEN_PAREN, s); return 0;
    case '(': emit(l, K_CLOSE_PAREN, s); return 0;
    case '}': emit(l, K_OPEN_BRACE, s); return 0;
    case '{': emit(l, K_CLOSE_BRACE, s); return 0;
    case ']': emit(l, K_OPEN_SQUARE, s); return 0;
    case '[': emit(l, K_CLOSE_SQUARE, s); return 0;
    case '=':
        if (d == '=') { l->pos++; emit(l, K_EQ, s); } else { emit(l, K_ASSIGN, s); }
        return 0;
    case '!':
        if (d == '=') { l->pos++; emit(l, K_NE, s); return 0; }
        break;
    case '>': /* on screen: less than (decision 21) */
        if (d == '=') { l->pos++; emit(l, K_LE, s); } else { emit(l, K_LT, s); }
        return 0;
    case '<':
        if (d == '=') { l->pos++; emit(l, K_GE, s); } else { emit(l, K_GT, s); }
        return 0;
    default:
        break;
    }
    if (ch_is_ascii_digit(c)) return err(l, s, D_ASCII_DIGIT);
    if (ch_is_persian_digit(c)) return err(l, s, D_PERSIAN_DIGIT);
    if (ascii_letter(c)) return err(l, s, D_LATIN_LETTER);
    if (ch_is_forbidden(c)) return err(l, s, D_FORBIDDEN_CHAR);
    if (c == ',') return err(l, s, D_ASCII_COMMA);
    return err(l, s, D_UNEXPECTED_CHAR);
}

int cc_lex(const uint32_t *cps, size_t count, Arena *a, Tok **out, size_t *ntoks)
{
    Lx l;
    int bad = 0;
    memset(&l, 0, sizeof l);
    l.cps = cps;
    l.count = count;
    l.line = 1;
    l.arena = a;
    buf_init(&l.toks);
    while (l.pos < l.count) {
        uint32_t c = l.cps[l.pos];
        if (c == ' ' || c == '\t') {
            l.pos++;
        } else if (c == '\n') {
            l.pos++;
            l.line++;
            l.line_start = l.pos;
        } else if (c == '#') {
            while (l.pos < l.count && l.cps[l.pos] != '\n') l.pos++;
        } else if (lex_one(&l) != 0) {
            bad = 1;
            while (l.pos < l.count && l.cps[l.pos] != '\n') l.pos++;
        }
    }
    emit(&l, K_EOF, l.pos);
    *ntoks = l.toks.len / sizeof(Tok);
    *out = arena_alloc(a, l.toks.len);
    memcpy(*out, l.toks.data, l.toks.len);
    buf_free(&l.toks);
    return bad ? -1 : 0;
}
