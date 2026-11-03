/* number.c - reading and writing numbers in file order. */
#include "number.h"
#include "chars.h"

static int digit_in_run(uint32_t cp, unsigned base)
{
    int v = ch_digit_value(cp);
    if (v < 0 && base == 16) {
        v = ch_hex_letter_value(cp);
    }
    return v;
}

void num_parse(const uint32_t *cps, size_t count, size_t pos, NumResult *r)
{
    size_t i = pos;
    uint64_t mult = 1;      /* base^k for the k-th digit (least significant first) */
    int mult_overflow = 0;
    int ndigits = 0;
    int prev_was_sep = 0;

    r->value = 0;
    r->base = 10;
    r->end = pos;
    r->err = NUM_OK;
    r->err_pos = pos;

    if (pos >= count || ch_digit_value(cps[pos]) < 0) {
        r->err = NUM_ERR_NOT_NUMBER;
        return;
    }

    if (cps[pos] == CP_AR_ZERO && pos + 1 < count &&
        (cps[pos + 1] == CP_SEEN || cps[pos + 1] == CP_THEH)) {
        r->base = (cps[pos + 1] == CP_SEEN) ? 16 : 2;
        i = pos + 2;
    }

    while (i < count) {
        uint32_t cp = cps[i];
        int d;

        if (cp == CP_AR_THOUSANDS) {
            if (ndigits == 0 || prev_was_sep) {
                r->err = NUM_ERR_SEPARATOR;
                r->err_pos = i;
                return;
            }
            prev_was_sep = 1;
            i++;
            continue;
        }
        d = digit_in_run(cp, r->base);
        if (d < 0) {
            break;
        }
        if ((unsigned)d >= r->base) {
            r->err = NUM_ERR_BAD_DIGIT;
            r->err_pos = i;
            return;
        }
        if (d != 0) {
            uint64_t add;
            if (mult_overflow || mult > UINT64_MAX / (uint64_t)d) {
                r->err = NUM_ERR_OVERFLOW;
                r->err_pos = i;
                return;
            }
            add = mult * (uint64_t)d;
            if (r->value > UINT64_MAX - add) {
                r->err = NUM_ERR_OVERFLOW;
                r->err_pos = i;
                return;
            }
            r->value += add;
        }
        if (mult > UINT64_MAX / r->base) {
            mult_overflow = 1;
        } else {
            mult *= r->base;
        }
        ndigits++;
        prev_was_sep = 0;
        i++;
    }

    if (prev_was_sep) {
        r->err = NUM_ERR_SEPARATOR;
        r->err_pos = i - 1;
        return;
    }
    if (ndigits == 0) {
        r->err = NUM_ERR_NO_DIGITS;
        r->err_pos = i;
        return;
    }
    r->end = i;
}

size_t num_format_dec(uint64_t v, uint32_t *out)
{
    size_t n = 0;
    do {
        out[n++] = CP_AR_ZERO + (uint32_t)(v % 10u);
        v /= 10u;
    } while (v != 0);
    return n;
}

static const uint32_t hex_cp[16] = {
    0x0660u, 0x0661u, 0x0662u, 0x0663u, 0x0664u,
    0x0665u, 0x0666u, 0x0667u, 0x0668u, 0x0669u,
    0x0623u, 0x0628u, 0x062Cu, 0x062Fu, 0x0647u, 0x0648u
};

size_t num_format_hex(uint64_t v, uint32_t *out)
{
    size_t n = 0;
    out[n++] = CP_AR_ZERO;
    out[n++] = CP_SEEN;
    do {
        out[n++] = hex_cp[v & 0xFu];
        v >>= 4;
    } while (v != 0);
    return n;
}
