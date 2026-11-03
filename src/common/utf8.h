/* utf8.h - strict UTF-8 decoding and encoding. */
#ifndef UTF8_H
#define UTF8_H

#include <stddef.h>
#include <stdint.h>

#include "rt.h"

/* Decodes one code point at s[*pos]. On success stores it in *cp, advances
 * *pos and returns 0. Returns -1 on invalid input (bad lead byte, truncated
 * sequence, overlong form, surrogate, value above U+10FFFF); *pos is left
 * unchanged. */
int utf8_decode(const uint8_t *s, size_t len, size_t *pos, uint32_t *cp);

/* Encodes cp into out (at least 4 bytes). Returns the number of bytes. */
size_t utf8_encode(uint32_t cp, uint8_t *out);

/* Decodes a whole buffer into an array of code points allocated in the
 * arena. Returns 0 on success. On failure returns -1 and stores the byte
 * offset of the bad sequence in *bad_offset. */
int utf8_decode_all(const uint8_t *s, size_t len, Arena *a,
                    uint32_t **cps, size_t *count, size_t *bad_offset);

#endif
