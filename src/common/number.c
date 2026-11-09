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

/* ------------------------------------------------------------------ */
/* Real literals: exact decimal -> binary with round to nearest even.  */

#define BIG_LIMBS 160   /* 5120 bits: enough for 800 digits shifted by 1077 */

typedef struct {
    uint32_t w[BIG_LIMBS];   /* least significant limb first */
} Big;

static void big_zero(Big *a)
{
    size_t i;
    for (i = 0; i < BIG_LIMBS; i++) a->w[i] = 0;
}

static void big_mul_add(Big *a, uint32_t m, uint32_t add)
{
    uint64_t carry = add;
    size_t i;
    for (i = 0; i < BIG_LIMBS; i++) {
        uint64_t v = (uint64_t)a->w[i] * m + carry;
        a->w[i] = (uint32_t)v;
        carry = v >> 32;
    }
}

static int big_bitlen(const Big *a)
{
    int i = BIG_LIMBS - 1, n = 0;
    uint32_t v;
    while (i >= 0 && a->w[i] == 0) i--;
    if (i < 0) return 0;
    v = a->w[i];
    while (v != 0) { n++; v >>= 1; }
    return i * 32 + n;
}

static void big_shl(Big *a, int n)
{
    int limbs = n / 32, bits = n % 32, i;
    for (i = BIG_LIMBS - 1; i >= 0; i--) {
        uint32_t hi = i - limbs >= 0 ? a->w[i - limbs] : 0;
        uint32_t lo = i - limbs - 1 >= 0 ? a->w[i - limbs - 1] : 0;
        a->w[i] = bits == 0 ? hi : (hi << bits) | (lo >> (32 - bits));
    }
}

static void big_shr1(Big *a)
{
    size_t i;
    for (i = 0; i < BIG_LIMBS; i++) {
        uint32_t next = i + 1 < BIG_LIMBS ? a->w[i + 1] : 0;
        a->w[i] = (a->w[i] >> 1) | (next << 31);
    }
}

static int big_cmp(const Big *a, const Big *b)
{
    int i;
    for (i = BIG_LIMBS - 1; i >= 0; i--) {
        if (a->w[i] != b->w[i]) return a->w[i] < b->w[i] ? -1 : 1;
    }
    return 0;
}

static void big_sub(Big *a, const Big *b)
{
    uint64_t borrow = 0;
    size_t i;
    for (i = 0; i < BIG_LIMBS; i++) {
        uint64_t s = (uint64_t)b->w[i] + borrow;
        borrow = (uint64_t)a->w[i] < s ? 1 : 0;
        a->w[i] = (uint32_t)((uint64_t)a->w[i] - s);
    }
}

static int big_is_zero(const Big *a)
{
    return big_bitlen(a) == 0;
}

int num_real_ahead(const uint32_t *cps, size_t count, size_t pos)
{
    size_t i = pos;
    while (i < count && (ch_digit_value(cps[i]) >= 0 || cps[i] == CP_AR_THOUSANDS)) i++;
    return i > pos && i + 1 < count && cps[i] == CP_AR_DECIMAL && ch_digit_value(cps[i + 1]) >= 0;
}

/* one part of the literal (a digit run with ٬); appends the digits to
 * digs (file order) and returns the index after the run, or sets r->err */
static size_t real_part(const uint32_t *cps, size_t count, size_t i, uint8_t *digs, int *nd,
                        NumResult *r)
{
    int prev_sep = 0, any = 0;
    while (i < count) {
        int d = ch_digit_value(cps[i]);
        if (cps[i] == CP_AR_THOUSANDS) {
            if (!any || prev_sep) { r->err = NUM_ERR_SEPARATOR; r->err_pos = i; return i; }
            prev_sep = 1;
        } else if (d >= 0) {
            if (*nd >= NUM_REAL_MAX_DIGITS) { r->err = NUM_ERR_OVERFLOW; r->err_pos = i; return i; }
            digs[(*nd)++] = (uint8_t)d;
            any = 1;
            prev_sep = 0;
        } else {
            break;
        }
        i++;
    }
    if (prev_sep) { r->err = NUM_ERR_SEPARATOR; r->err_pos = i - 1; }
    return i;
}

void num_parse_real(const uint32_t *cps, size_t count, size_t pos, int bits,
                    NumResult *r, uint64_t *out)
{
    static uint8_t digs[NUM_REAL_MAX_DIGITS];
    static Big n, d, t;
    int nd = 0, nfrac, i, e, s, p, emin, emax, sticky;
    uint64_t q2 = 0, q;
    size_t j;

    r->value = 0;
    r->base = 10;
    r->end = pos;
    r->err = NUM_OK;
    r->err_pos = pos;
    *out = 0;
    j = real_part(cps, count, pos, digs, &nd, r);
    if (r->err != NUM_OK) return;
    nfrac = nd;
    j = real_part(cps, count, j + 1, digs, &nd, r);   /* skip ٫ */
    if (r->err != NUM_OK) return;
    r->end = j;

    /* n = all digits (the last one in file order is the most significant),
     * d = 10^nfrac */
    big_zero(&n);
    for (i = nd - 1; i >= 0; i--) big_mul_add(&n, 10, digs[i]);
    big_zero(&d);
    d.w[0] = 1;
    for (i = 0; i < nfrac; i++) big_mul_add(&d, 10, 0);
    if (big_is_zero(&n)) return;

    p = bits == 32 ? 24 : 53;
    emin = bits == 32 ? -126 : -1022;
    emax = bits == 32 ? 127 : 1023;

    /* e = floor(log2(n / d)) */
    e = big_bitlen(&n) - big_bitlen(&d);
    t = d;
    if (e >= 0) big_shl(&t, e);
    if (e < 0) {
        Big m = n;
        big_shl(&m, -e);
        if (big_cmp(&m, &t) < 0) e--;
    } else if (big_cmp(&n, &t) < 0) {
        e--;
    }

    /* q2 = floor(n / d * 2^(s+1)): the kept bits plus one rounding bit */
    s = e >= emin ? p - 1 - e : p - 1 - emin;
    if (s + 1 >= 0) big_shl(&n, s + 1); else big_shl(&d, -(s + 1));
    t = d;
    big_shl(&t, p);
    for (i = p; i >= 0; i--) {
        if (big_cmp(&n, &t) >= 0) {
            big_sub(&n, &t);
            q2 |= (uint64_t)1 << i;
        }
        big_shr1(&t);
    }
    sticky = !big_is_zero(&n);
    q = q2 >> 1;
    if ((q2 & 1) != 0 && (sticky || (q & 1) != 0)) q++;

    if (e >= emin) {
        if (q == (uint64_t)1 << p) { q >>= 1; e++; }
        if (e > emax) { r->err = NUM_ERR_TOO_BIG; return; }
        *out = ((uint64_t)(e + (bits == 32 ? 127 : 1023)) << (p - 1)) | (q - ((uint64_t)1 << (p - 1)));
    } else {
        if (q == 0) { r->err = NUM_ERR_TOO_SMALL; return; }
        *out = q;   /* subnormal; q == 2^(p-1) is the smallest normal number */
    }
}
