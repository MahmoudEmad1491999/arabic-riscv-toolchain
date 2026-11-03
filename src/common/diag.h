/* diag.h - Arabic diagnostics.
 *
 * A message is built in FILE ORDER (the same convention as source files:
 * numbers least significant digit first). Before printing to the plain
 * left-to-right terminal the whole line is reversed, then runs of Latin
 * characters (paths, file names) are reversed back so they stay readable.
 * Set DIAG_ORDER=file to print in file order instead (used by the tests).
 * See docs/assumptions.md A2.
 *
 * All message texts live in one table in diag.c.
 */
#ifndef DIAG_H
#define DIAG_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    D_CANNOT_READ,
    D_CANNOT_WRITE,
    D_BAD_UTF8,
    D_USAGE,
    D_ASCII_DIGIT,
    D_PERSIAN_DIGIT,
    D_LATIN_LETTER,
    D_FORBIDDEN_CHAR,
    D_UNEXPECTED_CHAR,
    D_ASCII_COMMA,
    D_AR_SEMICOLON,
    D_NUM_NO_DIGITS,
    D_NUM_BAD_DIGIT,
    D_NUM_SEPARATOR,
    D_NUM_OVERFLOW,
    D_LETTER_AFTER_NUMBER,
    D_UNTERMINATED_STRING,
    D_BAD_ESCAPE,
    D_BAD_CHAR_LITERAL,
    D_UNKNOWN_INSN,
    D_EXPECTED_REGISTER,
    D_REGISTER_RANGE,
    D_EXPECTED_COMMA,
    D_EXTRA_TOKENS,
    D_EXPECTED_EXPR,
    D_UNCLOSED_BRACKET,
    D_BRACKET_MISMATCH,
    D_NOT_RELOCATABLE,
    D_NOT_CONSTANT,
    D_DIV_ZERO,
    D_VALUE_RANGE,
    D_NEEDS_LO,
    D_NEEDS_HI,
    D_RELOC_NOT_ALLOWED,
    D_UNDEFINED_SYMBOL,
    D_REDEFINED_SYMBOL,
    D_RESERVED_NAME,
    D_LOCAL_LABEL_MISSING,
    D_LOCAL_LABEL_BASE,
    D_BRANCH_TARGET,
    D_BRANCH_RANGE,
    D_MISALIGNED_TARGET,
    D_UNKNOWN_SECTION,
    D_NOT_IN_BSS,
    D_EXPECTED_STRING,
    D_EXPECTED_SYMBOL,
    D_CHAR_TOO_WIDE,
    D_EXPECTED_PLUS_MINUS,
    D_TOO_MANY_ERRORS,
    D_COUNT
} DiagId;

/* File name shown in front of every message (UTF-8). */
void diag_set_file(const char *path);

/* Reports an error at line:col (1-based). extra may be NULL; otherwise it
 * is appended after the message (e.g. the offending name). */
void diag_error(uint32_t line, uint32_t col, DiagId id,
                const uint8_t *extra, size_t extra_len);

/* Reports an error with no position (usage, I/O). */
void diag_plain(DiagId id, const char *extra);

int diag_count(void);

#endif
