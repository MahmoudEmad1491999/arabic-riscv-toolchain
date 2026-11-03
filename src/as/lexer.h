/* lexer.h - tokens of the Arabic assembly language (docs/spec-assembler.md). */
#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>
#include <stdint.h>

#include "../common/rt.h"

typedef enum {
    T_EOF,
    T_NEWLINE,
    T_IDENT,      /* name or directive (directives start with '.') */
    T_NUMBER,     /* number or character literal: value */
    T_LOCAL_REF,  /* ١ق / ١م: value = label number, dir = +1 forward / -1 back */
    T_STRING,     /* str/str_len: UTF-8 bytes after escapes */
    T_COMMA,      /* ، */
    T_PLUS,
    T_MINUS,
    T_STAR,
    T_SLASH,
    T_COLON,
    T_OPEN,       /* ) or ] in file order: opens a group (assumption A1) */
    T_CLOSE       /* ( or [ in file order: closes a group */
} TokKind;

typedef struct {
    TokKind kind;
    uint32_t line;
    uint32_t col;
    const uint8_t *text;  /* T_IDENT: UTF-8 name */
    size_t text_len;
    uint64_t value;
    unsigned base;        /* T_NUMBER: 2, 10, 16, or 0 for a character literal */
    int dir;
    int square;           /* T_OPEN/T_CLOSE: 1 for ] [ , 0 for ) ( */
    const uint8_t *str;
    size_t str_len;
} Token;

typedef struct {
    Token *toks;
    size_t count;
} TokenList;

/* Tokenises a whole file. Errors are reported through diag; the rest of a
 * bad line is skipped. Always ends with T_NEWLINE, T_EOF. */
void lex(const uint32_t *cps, size_t count, Arena *a, TokenList *out);

#endif
