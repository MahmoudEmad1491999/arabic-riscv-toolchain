/* tables.c - registers, mnemonics and directives. */
#include "tables.h"

#include "../common/chars.h"
#include "../common/utf8.h"

#include <string.h>

#define OP_LOAD    0x03u
#define OP_MISC    0x0Fu
#define OP_IMM     0x13u
#define OP_AUIPC   0x17u
#define OP_IMM32   0x1Bu
#define OP_STORE   0x23u
#define OP_OP      0x33u
#define OP_LUI     0x37u
#define OP_OP32    0x3Bu
#define OP_BRANCH  0x63u
#define OP_JALR    0x67u
#define OP_JAL     0x6Fu

static const InsnDef insns[] = {
    {"جمع",                  FMT_R, OP_OP,   0, 0x00, 0},
    {"طرح",                  FMT_R, OP_OP,   0, 0x20, 0},
    {"رفع",                  FMT_R, OP_OP,   1, 0x00, 0},
    {"ضبط_إذا_أصغر",          FMT_R, OP_OP,   2, 0x00, 0},
    {"ضبط_إذا_أصغر_بلا_إشارة", FMT_R, OP_OP,   3, 0x00, 0},
    {"أو_حصري",              FMT_R, OP_OP,   4, 0x00, 0},
    {"خفض_منطقي",            FMT_R, OP_OP,   5, 0x00, 0},
    {"خفض_حسابي",            FMT_R, OP_OP,   5, 0x20, 0},
    {"أو_منطقي",             FMT_R, OP_OP,   6, 0x00, 0},
    {"و_منطقي",              FMT_R, OP_OP,   7, 0x00, 0},
    {"جمع_كلمة",             FMT_R, OP_OP32, 0, 0x00, 0},
    {"طرح_كلمة",             FMT_R, OP_OP32, 0, 0x20, 0},
    {"رفع_كلمة",             FMT_R, OP_OP32, 1, 0x00, 0},
    {"خفض_منطقي_كلمة",       FMT_R, OP_OP32, 5, 0x00, 0},
    {"خفض_حسابي_كلمة",       FMT_R, OP_OP32, 5, 0x20, 0},

    {"جمع_فوري",                   FMT_I, OP_IMM,   0, 0, 0},
    {"ضبط_إذا_أصغر_فوري",           FMT_I, OP_IMM,   2, 0, 0},
    {"ضبط_إذا_أصغر_فوري_بلا_إشارة",  FMT_I, OP_IMM,   3, 0, 0},
    {"أو_حصري_فوري",               FMT_I, OP_IMM,   4, 0, 0},
    {"أو_منطقي_فوري",              FMT_I, OP_IMM,   6, 0, 0},
    {"و_منطقي_فوري",               FMT_I, OP_IMM,   7, 0, 0},
    {"جمع_فوري_كلمة",              FMT_I, OP_IMM32, 0, 0, 0},

    {"رفع_فوري",               FMT_SHIFT, OP_IMM,   1, 0x000, 63},
    {"خفض_منطقي_فوري",         FMT_SHIFT, OP_IMM,   5, 0x000, 63},
    {"خفض_حسابي_فوري",         FMT_SHIFT, OP_IMM,   5, 0x400, 63},
    {"رفع_فوري_كلمة",          FMT_SHIFT, OP_IMM32, 1, 0x000, 31},
    {"خفض_منطقي_فوري_كلمة",    FMT_SHIFT, OP_IMM32, 5, 0x000, 31},
    {"خفض_حسابي_فوري_كلمة",    FMT_SHIFT, OP_IMM32, 5, 0x400, 31},

    {"حمل_بايت",                 FMT_LOAD, OP_LOAD, 0, 0, 0},
    {"حمل_نصف_كلمة",             FMT_LOAD, OP_LOAD, 1, 0, 0},
    {"حمل_كلمة",                 FMT_LOAD, OP_LOAD, 2, 0, 0},
    {"حمل_كلمة_مزدوجة",          FMT_LOAD, OP_LOAD, 3, 0, 0},
    {"حمل_بايت_بلا_إشارة",       FMT_LOAD, OP_LOAD, 4, 0, 0},
    {"حمل_نصف_كلمة_بلا_إشارة",   FMT_LOAD, OP_LOAD, 5, 0, 0},
    {"حمل_كلمة_بلا_إشارة",       FMT_LOAD, OP_LOAD, 6, 0, 0},

    {"خزن_بايت",          FMT_STORE, OP_STORE, 0, 0, 0},
    {"خزن_نصف_كلمة",      FMT_STORE, OP_STORE, 1, 0, 0},
    {"خزن_كلمة",          FMT_STORE, OP_STORE, 2, 0, 0},
    {"خزن_كلمة_مزدوجة",   FMT_STORE, OP_STORE, 3, 0, 0},

    {"تفرع_إذا_يساوي",                     FMT_BRANCH, OP_BRANCH, 0, 0, 0},
    {"تفرع_إذا_لا_يساوي",                  FMT_BRANCH, OP_BRANCH, 1, 0, 0},
    {"تفرع_إذا_أصغر",                      FMT_BRANCH, OP_BRANCH, 4, 0, 0},
    {"تفرع_إذا_أكبر_أو_يساوي",             FMT_BRANCH, OP_BRANCH, 5, 0, 0},
    {"تفرع_إذا_أصغر_بلا_إشارة",            FMT_BRANCH, OP_BRANCH, 6, 0, 0},
    {"تفرع_إذا_أكبر_أو_يساوي_بلا_إشارة",   FMT_BRANCH, OP_BRANCH, 7, 0, 0},

    {"قفز_وربط",         FMT_JAL,   OP_JAL,   0, 0, 0},
    {"قفز_وربط_بسجل",    FMT_JALR,  OP_JALR,  0, 0, 0},
    {"حمل_علوي_فوري",    FMT_LUI,   OP_LUI,   0, 0, 0},
    {"جمع_علوي_للعداد",  FMT_AUIPC, OP_AUIPC, 0, 0, 0},

    {"سياج",         FMT_FIXED, 0x0FF0000Fu, 0, 0, 0},
    {"نداء_البيئة",  FMT_FIXED, 0x00000073u, 0, 0, 0},
    {"نقطة_توقف",    FMT_FIXED, 0x00100073u, 0, 0, 0},
};

int name_eq(const uint8_t *name, size_t len, const char *s)
{
    return strlen(s) == len && memcmp(name, s, len) == 0;
}

const InsnDef *find_insn(const uint8_t *name, size_t len)
{
    size_t i;
    for (i = 0; i < sizeof insns / sizeof insns[0]; i++) {
        if (name_eq(name, len, insns[i].name)) {
            return &insns[i];
        }
    }
    return NULL;
}

typedef struct {
    const char *name;
    int number;
} FixedReg;

static const FixedReg fixed_regs[] = {
    {"صفر", 0},
    {"عنوان_العودة", 1},
    {"مؤشر_المكدس", 2},
    {"المؤشر_العام", 3},
    {"مؤشر_الخيط", 4},
};

static const int temp_regs[] = {5, 6, 7, 28, 29, 30, 31};
static const int saved_regs[] = {8, 9, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27};
static const int arg_regs[] = {10, 11, 12, 13, 14, 15, 16, 17};

typedef struct {
    const char *prefix;
    const int *map;   /* NULL: identity (سجل) */
    int count;
} RegFamily;

static const RegFamily families[] = {
    {"سجل", NULL, 32},
    {"مؤقت", temp_regs, 7},
    {"محفوظ", saved_regs, 12},
    {"وسيط", arg_regs, 8},
};

/* Reads the digit run that forms the rest of a register name, least
 * significant digit first. Returns -1 if it is not a clean digit run
 * (empty, non-digits, or a redundant leading zero such as ٠٠). */
static int suffix_number(const uint8_t *s, size_t len)
{
    size_t pos = 0;
    int value = 0;
    int mult = 1;
    int ndigits = 0;
    int last = 0;

    while (pos < len) {
        uint32_t cp;
        int d;
        if (utf8_decode(s, len, &pos, &cp) != 0) {
            return -1;
        }
        d = ch_digit_value(cp);
        if (d < 0 || ndigits >= 3) {
            return -1;
        }
        value += d * mult;
        mult *= 10;
        ndigits++;
        last = d;
    }
    if (ndigits == 0 || (ndigits > 1 && last == 0)) {
        return -1;
    }
    return value;
}

int find_register(const uint8_t *name, size_t len)
{
    size_t i;
    for (i = 0; i < sizeof fixed_regs / sizeof fixed_regs[0]; i++) {
        if (name_eq(name, len, fixed_regs[i].name)) {
            return fixed_regs[i].number;
        }
    }
    for (i = 0; i < sizeof families / sizeof families[0]; i++) {
        size_t plen = strlen(families[i].prefix);
        int n;
        if (len <= plen || memcmp(name, families[i].prefix, plen) != 0) {
            continue;
        }
        n = suffix_number(name + plen, len - plen);
        if (n < 0) {
            return -1;
        }
        if (n >= families[i].count) {
            return -2;
        }
        return families[i].map != NULL ? families[i].map[n] : n;
    }
    return -1;
}

typedef struct {
    const char *name;
    DirKind kind;
} DirDef;

static const DirDef dirs[] = {
    {".قسم", DIR_SECTION},
    {".شفرة", DIR_TEXT},
    {".بيانات", DIR_DATA},
    {".بيانات_للقراءة", DIR_RODATA},
    {".بيانات_صفرية", DIR_BSS},
    {".عام", DIR_GLOBAL},
    {".محاذاة", DIR_ALIGN},
    {".بايت", DIR_BYTE},
    {".نصف_كلمة", DIR_HALF},
    {".كلمة", DIR_WORD},
    {".كلمة_مزدوجة", DIR_DWORD},
    {".سلسلة", DIR_STRING},
    {".فراغ", DIR_ZERO},
    {".ثابت", DIR_EQU},
};

DirKind find_directive(const uint8_t *name, size_t len)
{
    size_t i;
    for (i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        if (name_eq(name, len, dirs[i].name)) {
            return dirs[i].kind;
        }
    }
    return DIR_NONE;
}

int find_reloc_op(const uint8_t *name, size_t len)
{
    if (name_eq(name, len, "عالي")) {
        return RELOC_HI;
    }
    if (name_eq(name, len, "أدنى")) {
        return RELOC_LO;
    }
    return RELOC_NONE;
}
