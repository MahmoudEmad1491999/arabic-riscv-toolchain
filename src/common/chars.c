/* chars.c - Arabic character classes. */
#include "chars.h"

int ch_digit_value(uint32_t cp)
{
    if (cp >= 0x0660u && cp <= 0x0669u) {
        return (int)(cp - 0x0660u);
    }
    return -1;
}

int ch_hex_letter_value(uint32_t cp)
{
    switch (cp) {
    case 0x0623u: return 10; /* أ */
    case 0x0628u: return 11; /* ب */
    case 0x062Cu: return 12; /* ج */
    case 0x062Fu: return 13; /* د */
    case 0x0647u: return 14; /* ه */
    case 0x0648u: return 15; /* و */
    default: return -1;
    }
}

int ch_is_letter(uint32_t cp)
{
    return (cp >= 0x0621u && cp <= 0x063Au) || (cp >= 0x0641u && cp <= 0x064Au);
}

int ch_is_forbidden(uint32_t cp)
{
    if (cp == 0x0640u) {
        return 1; /* tatweel */
    }
    if ((cp >= 0x064Bu && cp <= 0x065Fu) || cp == 0x0670u) {
        return 1; /* tashkeel */
    }
    if ((cp >= 0xFB50u && cp <= 0xFDFFu) || (cp >= 0xFE70u && cp <= 0xFEFFu)) {
        return 1; /* presentation forms */
    }
    return 0;
}

int ch_is_ascii_digit(uint32_t cp)
{
    return cp >= '0' && cp <= '9';
}

int ch_is_persian_digit(uint32_t cp)
{
    return cp >= 0x06F0u && cp <= 0x06F9u;
}
