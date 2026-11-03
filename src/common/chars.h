/* chars.h - Arabic character classes used by every lexer in the toolchain.
 * See docs/spec-assembler.md and docs/assumptions.md (A3, A4, A6). */
#ifndef CHARS_H
#define CHARS_H

#include <stdint.h>

#define CP_AR_ZERO       0x0660u /* ٠ */
#define CP_AR_COMMA      0x060Cu /* ، operand separator */
#define CP_AR_SEMICOLON  0x061Bu /* ؛ reserved, error for now */
#define CP_AR_THOUSANDS  0x066Cu /* ٬ digit grouping */
#define CP_TATWEEL       0x0640u /* ـ forbidden */
#define CP_SEEN          0x0633u /* س hex marker, newline escape */
#define CP_THEH          0x062Bu /* ث binary marker */
#define CP_QAF           0x0642u /* ق local label: forward */
#define CP_MEEM          0x0645u /* م local label: backward */

/* Value 0..9 of an Arabic-Indic digit, or -1. */
int ch_digit_value(uint32_t cp);

/* Value 10..15 of a hex letter (أ ب ج د ه و), or -1. */
int ch_hex_letter_value(uint32_t cp);

/* Arabic letter allowed in identifiers (assumption A6). */
int ch_is_letter(uint32_t cp);

/* Characters that must never appear in identifiers: tatweel, tashkeel,
 * Arabic presentation forms. */
int ch_is_forbidden(uint32_t cp);

/* ASCII digit 0-9 (always an error in source outside strings/comments). */
int ch_is_ascii_digit(uint32_t cp);

/* Persian digit U+06F0..U+06F9 (rejected, assumption A4). */
int ch_is_persian_digit(uint32_t cp);

#endif
