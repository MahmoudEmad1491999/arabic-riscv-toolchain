/* number.h - reading and writing numbers in file order.
 *
 * File order = typing order. Number runs are stored LEAST significant digit
 * first: 1234 is stored as ٤٣٢١. A base marker comes first in file order:
 *   ٠س  hex     (digits ٠-٩ and أ ب ج د ه و = 10..15)
 *   ٠ث  binary
 * ٬ may separate digits and is ignored.
 *
 * Every tool that prints a number for a human or emits source code must use
 * num_format_* so the convention lives in exactly one place.
 */
#ifndef NUMBER_H
#define NUMBER_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    NUM_OK = 0,
    NUM_ERR_NOT_NUMBER,    /* does not start with an Arabic-Indic digit */
    NUM_ERR_NO_DIGITS,     /* base marker with no digits after it */
    NUM_ERR_BAD_DIGIT,     /* digit too large for the base (e.g. ٢ in binary) */
    NUM_ERR_SEPARATOR,     /* ٬ at the start/end of the run or doubled */
    NUM_ERR_OVERFLOW       /* does not fit in 64 bits */
} NumError;

typedef struct {
    uint64_t value;
    unsigned base;   /* 2, 10 or 16 */
    size_t end;      /* index just after the number run */
    NumError err;
    size_t err_pos;  /* index of the offending code point when err != NUM_OK */
} NumResult;

/* Parses the number run that starts at cps[pos]. Stops at the first code
 * point that cannot belong to the run; the caller decides whether what
 * follows is legal (assumption A7). */
void num_parse(const uint32_t *cps, size_t count, size_t pos, NumResult *r);

/* Writes v in file order into out and returns the number of code points.
 * out needs room for 22 (decimal) / 18 (hex) code points. */
size_t num_format_dec(uint64_t v, uint32_t *out);
size_t num_format_hex(uint64_t v, uint32_t *out);

#endif
