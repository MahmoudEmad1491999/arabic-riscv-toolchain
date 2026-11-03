/* Unit tests for src/common. Strings below are written in FILE ORDER,
 * i.e. least significant digit first: "٤٣٢١" is 1234. */
#include <stdio.h>
#include <string.h>

#include "../../src/common/chars.h"
#include "../../src/common/number.h"
#include "../../src/common/rt.h"
#include "../../src/common/utf8.h"

static int failures = 0;
static int checks = 0;
static Arena arena;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static uint32_t *decode(const char *s, size_t *n)
{
    uint32_t *cps = NULL;
    size_t bad = 0;
    if (utf8_decode_all((const uint8_t *)s, strlen(s), &arena, &cps, n, &bad) != 0) {
        printf("FAIL: test input is not valid UTF-8\n");
        failures++;
        *n = 0;
    }
    return cps;
}

static NumResult parse(const char *s)
{
    size_t n;
    uint32_t *cps = decode(s, &n);
    NumResult r;
    num_parse(cps, n, 0, &r);
    return r;
}

static void test_utf8(void)
{
    static const uint8_t overlong[] = {0xC0, 0xAF};
    static const uint8_t surrogate[] = {0xED, 0xA0, 0x80};
    static const uint8_t truncated[] = {0xD9};
    size_t pos;
    uint32_t cp;
    uint8_t buf[4];
    size_t n;
    uint32_t *cps = decode("جمع_فوري", &n);

    CHECK(n == 8);
    CHECK(cps[0] == 0x062C && cps[3] == '_');

    pos = 0;
    CHECK(utf8_decode(overlong, sizeof overlong, &pos, &cp) == -1);
    pos = 0;
    CHECK(utf8_decode(surrogate, sizeof surrogate, &pos, &cp) == -1);
    pos = 0;
    CHECK(utf8_decode(truncated, sizeof truncated, &pos, &cp) == -1 && pos == 0);

    CHECK(utf8_encode(0x0661, buf) == 2 && buf[0] == 0xD9 && buf[1] == 0xA1);
    CHECK(utf8_encode(0x1F600, buf) == 4);
}

static void test_chars(void)
{
    CHECK(ch_digit_value(0x0660) == 0 && ch_digit_value(0x0669) == 9);
    CHECK(ch_digit_value('5') == -1);
    CHECK(ch_hex_letter_value(0x0623) == 10 && ch_hex_letter_value(0x0648) == 15);
    CHECK(ch_hex_letter_value(0x0627) == -1); /* ا is not أ */
    CHECK(ch_is_letter(0x0628) && !ch_is_letter(0x0640));
    CHECK(ch_is_forbidden(0x0640) && ch_is_forbidden(0x064E) && ch_is_forbidden(0xFEDF));
    CHECK(!ch_is_forbidden(0x0628));
    CHECK(ch_is_ascii_digit('7') && ch_is_persian_digit(0x06F4));
}

static void test_numbers(void)
{
    NumResult r;

    r = parse("٤٣٢١");                 /* typed ٤ ٣ ٢ ١ */
    CHECK(r.err == NUM_OK && r.value == 1234 && r.base == 10 && r.end == 4);

    r = parse("٠");
    CHECK(r.err == NUM_OK && r.value == 0);

    r = parse("٠١");                   /* 10 */
    CHECK(r.err == NUM_OK && r.value == 10);

    r = parse("٧٠٠");                  /* on screen 007 */
    CHECK(r.err == NUM_OK && r.value == 7);

    r = parse("٠سوو");                 /* 0xFF */
    CHECK(r.err == NUM_OK && r.value == 0xFF && r.base == 16);

    r = parse("٠سأ١");                 /* 0x1A */
    CHECK(r.err == NUM_OK && r.value == 0x1A);

    r = parse("٠ث١٠١");                /* 0b101 */
    CHECK(r.err == NUM_OK && r.value == 5 && r.base == 2);

    r = parse("٠ث١٢");
    CHECK(r.err == NUM_ERR_BAD_DIGIT);

    r = parse("٠س");
    CHECK(r.err == NUM_ERR_NO_DIGITS);

    r = parse("٠٠٠٬٠٠٠٬١");             /* 1٬000٬000 */
    CHECK(r.err == NUM_OK && r.value == 1000000);

    r = parse("١٬٬٢");
    CHECK(r.err == NUM_ERR_SEPARATOR);

    r = parse("١٬");
    CHECK(r.err == NUM_ERR_SEPARATOR);

    /* 2^64 - 1 = 18446744073709551615, least significant digit first */
    r = parse("٥١٦١٥٥٩٠٧٣٧٠٤٤٧٦٤٤٨١");
    CHECK(r.err == NUM_OK && r.value == UINT64_MAX);

    /* 2^64 = 18446744073709551616 */
    r = parse("٦١٦١٥٥٩٠٧٣٧٠٤٤٧٦٤٤٨١");
    CHECK(r.err == NUM_ERR_OVERFLOW);

    /* leading zeros (trailing in file) beyond 64 bits are fine */
    r = parse("١٠٠٠٠٠٠٠٠٠٠٠٠٠٠٠٠٠٠٠٠٠٠٠٠");
    CHECK(r.err == NUM_OK && r.value == 1);

    r = parse("٠سوووووووووووووووو");      /* 0xFFFFFFFFFFFFFFFF */
    CHECK(r.err == NUM_OK && r.value == UINT64_MAX);

    r = parse("٠سووووووووووووووووو");     /* 17 F's */
    CHECK(r.err == NUM_ERR_OVERFLOW);

    /* the run stops at a non-digit; the lexer decides about what follows */
    r = parse("١ق");
    CHECK(r.err == NUM_OK && r.value == 1 && r.end == 1);

    r = parse("1");
    CHECK(r.err == NUM_ERR_NOT_NUMBER);
}

static void test_format(void)
{
    uint32_t out[24];
    size_t n;
    size_t want_n;
    uint32_t *want;
    NumResult r;
    uint64_t samples[] = {0, 7, 10, 1234, 0xFF, 0x1A, 1000000, UINT64_MAX};
    size_t i;

    n = num_format_dec(1234, out);
    want = decode("٤٣٢١", &want_n);
    CHECK(n == want_n && memcmp(out, want, n * sizeof *out) == 0);

    n = num_format_hex(0x1A, out);
    want = decode("٠سأ١", &want_n);
    CHECK(n == want_n && memcmp(out, want, n * sizeof *out) == 0);

    /* round trip */
    for (i = 0; i < sizeof samples / sizeof samples[0]; i++) {
        n = num_format_dec(samples[i], out);
        num_parse(out, n, 0, &r);
        CHECK(r.err == NUM_OK && r.value == samples[i] && r.end == n);
        n = num_format_hex(samples[i], out);
        num_parse(out, n, 0, &r);
        CHECK(r.err == NUM_OK && r.value == samples[i] && r.end == n);
    }
}

int main(void)
{
    arena_init(&arena);
    test_utf8();
    test_chars();
    test_numbers();
    test_format();
    arena_free(&arena);
    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
