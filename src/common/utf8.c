/* utf8.c - strict UTF-8 decoding and encoding. */
#include "utf8.h"

int utf8_decode(const uint8_t *s, size_t len, size_t *pos, uint32_t *cp)
{
    size_t i = *pos;
    uint32_t c;
    uint32_t min;
    size_t need;
    size_t k;

    if (i >= len) {
        return -1;
    }
    c = s[i];
    if (c < 0x80) {
        *cp = c;
        *pos = i + 1;
        return 0;
    } else if ((c & 0xE0) == 0xC0) {
        c &= 0x1F;
        need = 1;
        min = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
        c &= 0x0F;
        need = 2;
        min = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
        c &= 0x07;
        need = 3;
        min = 0x10000;
    } else {
        return -1;
    }
    if (len - i - 1 < need) {
        return -1;
    }
    for (k = 1; k <= need; k++) {
        uint32_t b = s[i + k];
        if ((b & 0xC0) != 0x80) {
            return -1;
        }
        c = (c << 6) | (b & 0x3F);
    }
    if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
        return -1;
    }
    *cp = c;
    *pos = i + 1 + need;
    return 0;
}

size_t utf8_encode(uint32_t cp, uint8_t *out)
{
    if (cp < 0x80) {
        out[0] = (uint8_t)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (uint8_t)(0xC0 | (cp >> 6));
        out[1] = (uint8_t)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (uint8_t)(0xE0 | (cp >> 12));
        out[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (uint8_t)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (uint8_t)(0xF0 | (cp >> 18));
    out[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (uint8_t)(0x80 | (cp & 0x3F));
    return 4;
}

int utf8_decode_all(const uint8_t *s, size_t len, Arena *a,
                    uint32_t **cps, size_t *count, size_t *bad_offset)
{
    /* A code point takes at least one byte, so len is an upper bound. */
    uint32_t *out = arena_alloc(a, (len + 1) * sizeof *out);
    size_t pos = 0;
    size_t n = 0;

    while (pos < len) {
        if (utf8_decode(s, len, &pos, &out[n]) != 0) {
            *bad_offset = pos;
            return -1;
        }
        n++;
    }
    *cps = out;
    *count = n;
    return 0;
}
