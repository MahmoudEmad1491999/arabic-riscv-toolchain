/* lexer.c - tokens of the Arabic assembly language. */
#include "lexer.h"

#include "../common/buf.h"
#include "../common/chars.h"
#include "../common/diag.h"
#include "../common/number.h"
#include "../common/utf8.h"

#include <string.h>

typedef struct {
    const uint32_t *cps;
    size_t count;
    size_t pos;
    uint32_t line;
    size_t line_start;
    Arena *arena;
    Buf toks; /* array of Token */
} Lexer;

static uint32_t peek(const Lexer *lx, size_t ahead)
{
    size_t i = lx->pos + ahead;
    return i < lx->count ? lx->cps[i] : 0;
}

static uint32_t col_of(const Lexer *lx, size_t pos)
{
    return (uint32_t)(pos - lx->line_start + 1);
}

static int is_ascii_letter(uint32_t cp)
{
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
}

static int is_name_char(uint32_t cp)
{
    return ch_is_letter(cp) || cp == '_' || ch_digit_value(cp) >= 0;
}

static Token *push(Lexer *lx, TokKind kind, size_t start)
{
    Token t;
    memset(&t, 0, sizeof t);
    t.kind = kind;
    t.line = lx->line;
    t.col = col_of(lx, start);
    buf_bytes(&lx->toks, &t, sizeof t);
    return (Token *)(lx->toks.data + lx->toks.len - sizeof t);
}

static void error_at(Lexer *lx, size_t pos, DiagId id)
{
    diag_error(lx->line, col_of(lx, pos), id, NULL, 0);
}

/* Skips to the end of the current line (the newline itself is kept). */
static void skip_line(Lexer *lx)
{
    while (lx->pos < lx->count && lx->cps[lx->pos] != '\n') {
        lx->pos++;
    }
}

static const uint8_t *encode_range(Lexer *lx, size_t from, size_t to, size_t *len)
{
    uint8_t *out = arena_alloc(lx->arena, (to - from) * 4 + 1);
    size_t n = 0;
    size_t i;
    for (i = from; i < to; i++) {
        n += utf8_encode(lx->cps[i], out + n);
    }
    *len = n;
    return out;
}

/* Returns 0 if the character after a number or name is acceptable. */
static int check_after_word(Lexer *lx)
{
    uint32_t cp = peek(lx, 0);
    if (ch_is_forbidden(cp)) {
        error_at(lx, lx->pos, D_FORBIDDEN_CHAR);
        return -1;
    }
    if (is_ascii_letter(cp)) {
        error_at(lx, lx->pos, D_LATIN_LETTER);
        return -1;
    }
    if (ch_is_ascii_digit(cp)) {
        error_at(lx, lx->pos, D_ASCII_DIGIT);
        return -1;
    }
    if (ch_is_persian_digit(cp)) {
        error_at(lx, lx->pos, D_PERSIAN_DIGIT);
        return -1;
    }
    return 0;
}

static int lex_number(Lexer *lx)
{
    size_t start = lx->pos;
    NumResult r;
    Token *t;

    num_parse(lx->cps, lx->count, lx->pos, &r);
    if (r.err != NUM_OK) {
        static const DiagId map[] = {
            D_UNEXPECTED_CHAR, D_UNEXPECTED_CHAR, D_NUM_NO_DIGITS,
            D_NUM_BAD_DIGIT, D_NUM_SEPARATOR, D_NUM_OVERFLOW
        };
        error_at(lx, r.err_pos, map[r.err]);
        return -1;
    }
    lx->pos = r.end;
    if (r.base == 10 && (peek(lx, 0) == CP_QAF || peek(lx, 0) == CP_MEEM)) {
        t = push(lx, T_LOCAL_REF, start);
        t->value = r.value;
        t->dir = peek(lx, 0) == CP_QAF ? 1 : -1;
        lx->pos++;
    } else {
        t = push(lx, T_NUMBER, start);
        t->value = r.value;
        t->base = r.base;
    }
    if (is_name_char(peek(lx, 0))) {
        error_at(lx, lx->pos, D_LETTER_AFTER_NUMBER);
        return -1;
    }
    return check_after_word(lx);
}

static int lex_name(Lexer *lx)
{
    size_t start = lx->pos;
    Token *t;
    const uint8_t *text;
    size_t len;

    lx->pos++; /* first char already checked: letter, '_' or '.' */
    while (lx->pos < lx->count && is_name_char(lx->cps[lx->pos])) {
        lx->pos++;
    }
    if (check_after_word(lx) != 0) {
        return -1;
    }
    text = encode_range(lx, start, lx->pos, &len);
    t = push(lx, T_IDENT, start);
    t->text = text;
    t->text_len = len;
    return 0;
}

/* Reads one character of a string or character literal, handling escapes.
 * Returns -1 on error. */
static int string_char(Lexer *lx, uint32_t quote, uint32_t *out)
{
    uint32_t cp = peek(lx, 0);
    if (cp != '\\') {
        lx->pos++;
        *out = cp;
        return 0;
    }
    cp = peek(lx, 1);
    switch (cp) {
    case CP_SEEN: *out = '\n'; break;   /* \س */
    case 0x0631u: *out = '\r'; break;   /* \ر */
    case 0x062Cu: *out = '\t'; break;   /* \ج */
    case CP_AR_ZERO: *out = 0; break;   /* \٠ */
    case '\\': *out = '\\'; break;
    default:
        if (cp == quote) {
            *out = cp;
            break;
        }
        error_at(lx, lx->pos, D_BAD_ESCAPE);
        return -1;
    }
    lx->pos += 2;
    return 0;
}

static int lex_string(Lexer *lx)
{
    size_t start = lx->pos;
    Buf bytes;
    Token *t;
    uint8_t *copy;

    buf_init(&bytes);
    lx->pos++;
    for (;;) {
        uint32_t cp;
        uint8_t enc[4];
        if (lx->pos >= lx->count || peek(lx, 0) == '\n') {
            error_at(lx, start, D_UNTERMINATED_STRING);
            buf_free(&bytes);
            return -1;
        }
        if (peek(lx, 0) == '"') {
            lx->pos++;
            break;
        }
        if (string_char(lx, '"', &cp) != 0) {
            buf_free(&bytes);
            return -1;
        }
        buf_bytes(&bytes, enc, utf8_encode(cp, enc));
    }
    copy = arena_alloc(lx->arena, bytes.len + 1);
    if (bytes.len > 0) {
        memcpy(copy, bytes.data, bytes.len);
    }
    t = push(lx, T_STRING, start);
    t->str = copy;
    t->str_len = bytes.len;
    buf_free(&bytes);
    return 0;
}

static int lex_char(Lexer *lx)
{
    size_t start = lx->pos;
    uint32_t cp;
    Token *t;

    lx->pos++;
    if (peek(lx, 0) == '\'' || peek(lx, 0) == '\n' || lx->pos >= lx->count) {
        error_at(lx, start, D_BAD_CHAR_LITERAL);
        return -1;
    }
    if (string_char(lx, '\'', &cp) != 0) {
        return -1;
    }
    if (peek(lx, 0) != '\'') {
        error_at(lx, start, D_BAD_CHAR_LITERAL);
        return -1;
    }
    lx->pos++;
    t = push(lx, T_NUMBER, start);
    t->value = cp;
    t->base = 0;
    return 0;
}

static int lex_one(Lexer *lx)
{
    uint32_t cp = peek(lx, 0);
    size_t start = lx->pos;
    Token *t;

    if (ch_digit_value(cp) >= 0) {
        return lex_number(lx);
    }
    if (ch_is_letter(cp) || cp == '_' || (cp == '.' && ch_is_letter(peek(lx, 1)))) {
        return lex_name(lx);
    }
    if (cp == '"') {
        return lex_string(lx);
    }
    if (cp == '\'') {
        return lex_char(lx);
    }
    lx->pos++;
    switch (cp) {
    case CP_AR_COMMA: push(lx, T_COMMA, start); return 0;
    case '+': push(lx, T_PLUS, start); return 0;
    case '-': push(lx, T_MINUS, start); return 0;
    case '*': push(lx, T_STAR, start); return 0;
    case '/': push(lx, T_SLASH, start); return 0;
    case ':': push(lx, T_COLON, start); return 0;
    case ')': case ']':
        t = push(lx, T_OPEN, start);
        t->square = cp == ']';
        return 0;
    case '(': case '[':
        t = push(lx, T_CLOSE, start);
        t->square = cp == '[';
        return 0;
    default:
        break;
    }
    if (ch_is_ascii_digit(cp)) {
        error_at(lx, start, D_ASCII_DIGIT);
    } else if (ch_is_persian_digit(cp)) {
        error_at(lx, start, D_PERSIAN_DIGIT);
    } else if (is_ascii_letter(cp)) {
        error_at(lx, start, D_LATIN_LETTER);
    } else if (ch_is_forbidden(cp)) {
        error_at(lx, start, D_FORBIDDEN_CHAR);
    } else if (cp == ',') {
        error_at(lx, start, D_ASCII_COMMA);
    } else if (cp == CP_AR_SEMICOLON) {
        error_at(lx, start, D_AR_SEMICOLON);
    } else {
        error_at(lx, start, D_UNEXPECTED_CHAR);
    }
    return -1;
}

void lex(const uint32_t *cps, size_t count, Arena *a, TokenList *out)
{
    Lexer lx;

    memset(&lx, 0, sizeof lx);
    lx.cps = cps;
    lx.count = count;
    lx.line = 1;
    lx.arena = a;
    buf_init(&lx.toks);

    while (lx.pos < lx.count) {
        uint32_t cp = lx.cps[lx.pos];
        if (cp == ' ' || cp == '\t') {
            lx.pos++;
        } else if (cp == '\n') {
            push(&lx, T_NEWLINE, lx.pos);
            lx.pos++;
            lx.line++;
            lx.line_start = lx.pos;
        } else if (cp == '#') {
            skip_line(&lx);
        } else if (lex_one(&lx) != 0) {
            skip_line(&lx);
        }
    }
    push(&lx, T_NEWLINE, lx.pos);
    push(&lx, T_EOF, lx.pos);

    out->count = lx.toks.len / sizeof(Token);
    out->toks = arena_alloc(a, lx.toks.len);
    memcpy(out->toks, lx.toks.data, lx.toks.len);
    buf_free(&lx.toks);
}
