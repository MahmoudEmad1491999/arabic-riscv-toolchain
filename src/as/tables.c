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
    {"جمع",                  FMT_R, OP_OP,   0, 0x00, 0, P_NONE, 0, NULL},
    {"طرح",                  FMT_R, OP_OP,   0, 0x20, 0, P_NONE, 0, NULL},
    {"رفع",                  FMT_R, OP_OP,   1, 0x00, 0, P_NONE, 0, NULL},
    {"ضبط_إذا_أصغر",          FMT_R, OP_OP,   2, 0x00, 0, P_NONE, 0, NULL},
    {"ضبط_إذا_أصغر_بلا_إشارة", FMT_R, OP_OP,   3, 0x00, 0, P_NONE, 0, NULL},
    {"أو_حصري",              FMT_R, OP_OP,   4, 0x00, 0, P_NONE, 0, NULL},
    {"خفض_منطقي",            FMT_R, OP_OP,   5, 0x00, 0, P_NONE, 0, NULL},
    {"خفض_حسابي",            FMT_R, OP_OP,   5, 0x20, 0, P_NONE, 0, NULL},
    {"أو_منطقي",             FMT_R, OP_OP,   6, 0x00, 0, P_NONE, 0, NULL},
    {"و_منطقي",              FMT_R, OP_OP,   7, 0x00, 0, P_NONE, 0, NULL},
    {"جمع_كلمة",             FMT_R, OP_OP32, 0, 0x00, 0, P_NONE, 0, NULL},
    {"طرح_كلمة",             FMT_R, OP_OP32, 0, 0x20, 0, P_NONE, 0, NULL},
    {"رفع_كلمة",             FMT_R, OP_OP32, 1, 0x00, 0, P_NONE, 0, NULL},
    {"خفض_منطقي_كلمة",       FMT_R, OP_OP32, 5, 0x00, 0, P_NONE, 0, NULL},
    {"خفض_حسابي_كلمة",       FMT_R, OP_OP32, 5, 0x20, 0, P_NONE, 0, NULL},

    /* M extension (decision 14) */
    {"ضرب",                          FMT_R, OP_OP,   0, 0x01, 0, P_NONE, 0, NULL},
    {"ضرب_عالي",                     FMT_R, OP_OP,   1, 0x01, 0, P_NONE, 0, NULL},
    {"ضرب_عالي_بإشارة_وبلا_إشارة",    FMT_R, OP_OP,   2, 0x01, 0, P_NONE, 0, NULL},
    {"ضرب_عالي_بلا_إشارة",           FMT_R, OP_OP,   3, 0x01, 0, P_NONE, 0, NULL},
    {"قسمة",                         FMT_R, OP_OP,   4, 0x01, 0, P_NONE, 0, NULL},
    {"قسمة_بلا_إشارة",               FMT_R, OP_OP,   5, 0x01, 0, P_NONE, 0, NULL},
    {"باقي",                         FMT_R, OP_OP,   6, 0x01, 0, P_NONE, 0, NULL},
    {"باقي_بلا_إشارة",               FMT_R, OP_OP,   7, 0x01, 0, P_NONE, 0, NULL},
    {"ضرب_كلمة",                     FMT_R, OP_OP32, 0, 0x01, 0, P_NONE, 0, NULL},
    {"قسمة_كلمة",                    FMT_R, OP_OP32, 4, 0x01, 0, P_NONE, 0, NULL},
    {"قسمة_بلا_إشارة_كلمة",          FMT_R, OP_OP32, 5, 0x01, 0, P_NONE, 0, NULL},
    {"باقي_كلمة",                    FMT_R, OP_OP32, 6, 0x01, 0, P_NONE, 0, NULL},
    {"باقي_بلا_إشارة_كلمة",          FMT_R, OP_OP32, 7, 0x01, 0, P_NONE, 0, NULL},

    {"جمع_فوري",                   FMT_I, OP_IMM,   0, 0, 0, P_NONE, 0, NULL},
    {"ضبط_إذا_أصغر_فوري",           FMT_I, OP_IMM,   2, 0, 0, P_NONE, 0, NULL},
    {"ضبط_إذا_أصغر_فوري_بلا_إشارة",  FMT_I, OP_IMM,   3, 0, 0, P_NONE, 0, NULL},
    {"أو_حصري_فوري",               FMT_I, OP_IMM,   4, 0, 0, P_NONE, 0, NULL},
    {"أو_منطقي_فوري",              FMT_I, OP_IMM,   6, 0, 0, P_NONE, 0, NULL},
    {"و_منطقي_فوري",               FMT_I, OP_IMM,   7, 0, 0, P_NONE, 0, NULL},
    {"جمع_فوري_كلمة",              FMT_I, OP_IMM32, 0, 0, 0, P_NONE, 0, NULL},

    {"رفع_فوري",               FMT_SHIFT, OP_IMM,   1, 0x000, 63, P_NONE, 0, NULL},
    {"خفض_منطقي_فوري",         FMT_SHIFT, OP_IMM,   5, 0x000, 63, P_NONE, 0, NULL},
    {"خفض_حسابي_فوري",         FMT_SHIFT, OP_IMM,   5, 0x400, 63, P_NONE, 0, NULL},
    {"رفع_فوري_كلمة",          FMT_SHIFT, OP_IMM32, 1, 0x000, 31, P_NONE, 0, NULL},
    {"خفض_منطقي_فوري_كلمة",    FMT_SHIFT, OP_IMM32, 5, 0x000, 31, P_NONE, 0, NULL},
    {"خفض_حسابي_فوري_كلمة",    FMT_SHIFT, OP_IMM32, 5, 0x400, 31, P_NONE, 0, NULL},

    {"حمل_بايت",                 FMT_LOAD, OP_LOAD, 0, 0, 0, P_NONE, 0, NULL},
    {"حمل_نصف_كلمة",             FMT_LOAD, OP_LOAD, 1, 0, 0, P_NONE, 0, NULL},
    {"حمل_كلمة",                 FMT_LOAD, OP_LOAD, 2, 0, 0, P_NONE, 0, NULL},
    {"حمل_كلمة_مزدوجة",          FMT_LOAD, OP_LOAD, 3, 0, 0, P_NONE, 0, NULL},
    {"حمل_بايت_بلا_إشارة",       FMT_LOAD, OP_LOAD, 4, 0, 0, P_NONE, 0, NULL},
    {"حمل_نصف_كلمة_بلا_إشارة",   FMT_LOAD, OP_LOAD, 5, 0, 0, P_NONE, 0, NULL},
    {"حمل_كلمة_بلا_إشارة",       FMT_LOAD, OP_LOAD, 6, 0, 0, P_NONE, 0, NULL},

    {"خزن_بايت",          FMT_STORE, OP_STORE, 0, 0, 0, P_NONE, 0, NULL},
    {"خزن_نصف_كلمة",      FMT_STORE, OP_STORE, 1, 0, 0, P_NONE, 0, NULL},
    {"خزن_كلمة",          FMT_STORE, OP_STORE, 2, 0, 0, P_NONE, 0, NULL},
    {"خزن_كلمة_مزدوجة",   FMT_STORE, OP_STORE, 3, 0, 0, P_NONE, 0, NULL},

    {"تفرع_إذا_يساوي",                     FMT_BRANCH, OP_BRANCH, 0, 0, 0, P_NONE, 0, NULL},
    {"تفرع_إذا_لا_يساوي",                  FMT_BRANCH, OP_BRANCH, 1, 0, 0, P_NONE, 0, NULL},
    {"تفرع_إذا_أصغر",                      FMT_BRANCH, OP_BRANCH, 4, 0, 0, P_NONE, 0, NULL},
    {"تفرع_إذا_أكبر_أو_يساوي",             FMT_BRANCH, OP_BRANCH, 5, 0, 0, P_NONE, 0, NULL},
    {"تفرع_إذا_أصغر_بلا_إشارة",            FMT_BRANCH, OP_BRANCH, 6, 0, 0, P_NONE, 0, NULL},
    {"تفرع_إذا_أكبر_أو_يساوي_بلا_إشارة",   FMT_BRANCH, OP_BRANCH, 7, 0, 0, P_NONE, 0, NULL},

    {"قفز_وربط",         FMT_JAL,   OP_JAL,   0, 0, 0, P_NONE, 0, NULL},
    {"قفز_وربط_بسجل",    FMT_JALR,  OP_JALR,  0, 0, 0, P_NONE, 0, NULL},
    {"حمل_علوي_فوري",    FMT_LUI,   OP_LUI,   0, 0, 0, P_NONE, 0, NULL},
    {"جمع_علوي_للعداد",  FMT_AUIPC, OP_AUIPC, 0, 0, 0, P_NONE, 0, NULL},

    {"سياج",         FMT_FIXED, 0x0FF0000Fu, 0, 0, 0, P_NONE, 0, NULL},
    {"نداء_البيئة",  FMT_FIXED, 0x00000073u, 0, 0, 0, P_NONE, 0, NULL},
    {"نقطة_توقف",    FMT_FIXED, 0x00100073u, 0, 0, 0, P_NONE, 0, NULL},

    /* Zicsr and the privileged instructions (A64) */
    {"قراءة_وكتابة_تحكم",        FMT_CSR,  0x73u, 1, 0, 0, P_NONE, 0, NULL},
    {"قراءة_وضبط_تحكم",          FMT_CSR,  0x73u, 2, 0, 0, P_NONE, 0, NULL},
    {"قراءة_ومسح_تحكم",          FMT_CSR,  0x73u, 3, 0, 0, P_NONE, 0, NULL},
    {"قراءة_وكتابة_تحكم_فوري",   FMT_CSRI, 0x73u, 5, 0, 0, P_NONE, 0, NULL},
    {"قراءة_وضبط_تحكم_فوري",     FMT_CSRI, 0x73u, 6, 0, 0, P_NONE, 0, NULL},
    {"قراءة_ومسح_تحكم_فوري",     FMT_CSRI, 0x73u, 7, 0, 0, P_NONE, 0, NULL},
    {"قراءة_تحكم",               FMT_CSR_READ,   0x73u, 2, 0, 0, P_NONE, 0, NULL},
    {"كتابة_تحكم",               FMT_CSR_WRITE,  0x73u, 1, 0, 0, P_NONE, 0, NULL},
    {"ضبط_تحكم",                 FMT_CSR_WRITE,  0x73u, 2, 0, 0, P_NONE, 0, NULL},
    {"مسح_تحكم",                 FMT_CSR_WRITE,  0x73u, 3, 0, 0, P_NONE, 0, NULL},
    {"كتابة_تحكم_فوري",          FMT_CSRI_WRITE, 0x73u, 5, 0, 0, P_NONE, 0, NULL},
    {"ضبط_تحكم_فوري",            FMT_CSRI_WRITE, 0x73u, 6, 0, 0, P_NONE, 0, NULL},
    {"مسح_تحكم_فوري",            FMT_CSRI_WRITE, 0x73u, 7, 0, 0, P_NONE, 0, NULL},
    {"عودة_من_الآلة",            FMT_FIXED, 0x30200073u, 0, 0, 0, P_NONE, 0, NULL},
    {"عودة_من_المشرف",           FMT_FIXED, 0x10200073u, 0, 0, 0, P_NONE, 0, NULL},
    {"انتظار_مقاطعة",            FMT_FIXED, 0x10500073u, 0, 0, 0, P_NONE, 0, NULL},
    {"سياج_الترجمة",             FMT_SFENCE, 0x12000073u, 0, 0, 0, P_NONE, 0, NULL},

    /* F and D extensions (decision 69): single _عشري, double _مزدوج */
    {"جمع_عشري", FMT_FP, 0x53u, 7, 0x00, 0, P_NONE, 0, "FFFR"},
    {"طرح_عشري", FMT_FP, 0x53u, 7, 0x04, 0, P_NONE, 0, "FFFR"},
    {"ضرب_عشري", FMT_FP, 0x53u, 7, 0x08, 0, P_NONE, 0, "FFFR"},
    {"قسمة_عشري", FMT_FP, 0x53u, 7, 0x0C, 0, P_NONE, 0, "FFFR"},
    {"جذر_عشري", FMT_FP, 0x53u, 7, 0x2C, 0, P_NONE, 0, "FFR"},
    {"نسخ_الإشارة_عشري", FMT_FP, 0x53u, 0, 0x10, 0, P_NONE, 0, "FFF"},
    {"نسخ_عكس_الإشارة_عشري", FMT_FP, 0x53u, 1, 0x10, 0, P_NONE, 0, "FFF"},
    {"ضرب_الإشارة_عشري", FMT_FP, 0x53u, 2, 0x10, 0, P_NONE, 0, "FFF"},
    {"أصغر_عشري", FMT_FP, 0x53u, 0, 0x14, 0, P_NONE, 0, "FFF"},
    {"أكبر_عشري", FMT_FP, 0x53u, 1, 0x14, 0, P_NONE, 0, "FFF"},
    {"ضبط_إذا_يساوي_عشري", FMT_FP, 0x53u, 2, 0x50, 0, P_NONE, 0, "XFF"},
    {"ضبط_إذا_أصغر_عشري", FMT_FP, 0x53u, 1, 0x50, 0, P_NONE, 0, "XFF"},
    {"ضبط_إذا_أصغر_أو_يساوي_عشري", FMT_FP, 0x53u, 0, 0x50, 0, P_NONE, 0, "XFF"},
    {"صنف_عشري", FMT_FP, 0x53u, 1, 0x70, 0, P_NONE, 0, "XF"},
    {"نسخ_بتات_من_عشري", FMT_FP, 0x53u, 0, 0x70, 0, P_NONE, 0, "XF"},
    {"نسخ_بتات_إلى_عشري", FMT_FP, 0x53u, 0, 0x78, 0, P_NONE, 0, "FX"},
    {"تحويل_كلمة_من_عشري", FMT_FP, 0x53u, 7, 0x60, 0, P_NONE, 0, "XFR"},
    {"تحويل_عشري_من_كلمة", FMT_FP, 0x53u, 7, 0x68, 0, P_NONE, 0, "FXR"},
    {"تحويل_كلمة_بلا_إشارة_من_عشري", FMT_FP, 0x53u, 7, 0x60, 0, P_NONE, 1, "XFR"},
    {"تحويل_عشري_من_كلمة_بلا_إشارة", FMT_FP, 0x53u, 7, 0x68, 0, P_NONE, 1, "FXR"},
    {"تحويل_كلمة_مزدوجة_من_عشري", FMT_FP, 0x53u, 7, 0x60, 0, P_NONE, 2, "XFR"},
    {"تحويل_عشري_من_كلمة_مزدوجة", FMT_FP, 0x53u, 7, 0x68, 0, P_NONE, 2, "FXR"},
    {"تحويل_كلمة_مزدوجة_بلا_إشارة_من_عشري", FMT_FP, 0x53u, 7, 0x60, 0, P_NONE, 3, "XFR"},
    {"تحويل_عشري_من_كلمة_مزدوجة_بلا_إشارة", FMT_FP, 0x53u, 7, 0x68, 0, P_NONE, 3, "FXR"},
    {"نسخ_عشري", FMT_FP, 0x53u, 0, 0x10, 0, P_NONE, 0, "FFD"},
    {"سالب_عشري", FMT_FP, 0x53u, 1, 0x10, 0, P_NONE, 0, "FFD"},
    {"مطلق_عشري", FMT_FP, 0x53u, 2, 0x10, 0, P_NONE, 0, "FFD"},
    {"ضرب_جمع_عشري", FMT_FP4, 0x43u, 7, 0, 0, P_NONE, 0, "FFFFR"},
    {"ضرب_طرح_عشري", FMT_FP4, 0x47u, 7, 0, 0, P_NONE, 0, "FFFFR"},
    {"سالب_ضرب_طرح_عشري", FMT_FP4, 0x4Bu, 7, 0, 0, P_NONE, 0, "FFFFR"},
    {"سالب_ضرب_جمع_عشري", FMT_FP4, 0x4Fu, 7, 0, 0, P_NONE, 0, "FFFFR"},
    {"حمل_عشري", FMT_FLOAD, 0x07u, 2, 0, 0, P_NONE, 0, NULL},
    {"خزن_عشري", FMT_FSTORE, 0x27u, 2, 0, 0, P_NONE, 0, NULL},

    {"جمع_مزدوج", FMT_FP, 0x53u, 7, 0x01, 0, P_NONE, 0, "FFFR"},
    {"طرح_مزدوج", FMT_FP, 0x53u, 7, 0x05, 0, P_NONE, 0, "FFFR"},
    {"ضرب_مزدوج", FMT_FP, 0x53u, 7, 0x09, 0, P_NONE, 0, "FFFR"},
    {"قسمة_مزدوج", FMT_FP, 0x53u, 7, 0x0D, 0, P_NONE, 0, "FFFR"},
    {"جذر_مزدوج", FMT_FP, 0x53u, 7, 0x2D, 0, P_NONE, 0, "FFR"},
    {"نسخ_الإشارة_مزدوج", FMT_FP, 0x53u, 0, 0x11, 0, P_NONE, 0, "FFF"},
    {"نسخ_عكس_الإشارة_مزدوج", FMT_FP, 0x53u, 1, 0x11, 0, P_NONE, 0, "FFF"},
    {"ضرب_الإشارة_مزدوج", FMT_FP, 0x53u, 2, 0x11, 0, P_NONE, 0, "FFF"},
    {"أصغر_مزدوج", FMT_FP, 0x53u, 0, 0x15, 0, P_NONE, 0, "FFF"},
    {"أكبر_مزدوج", FMT_FP, 0x53u, 1, 0x15, 0, P_NONE, 0, "FFF"},
    {"ضبط_إذا_يساوي_مزدوج", FMT_FP, 0x53u, 2, 0x51, 0, P_NONE, 0, "XFF"},
    {"ضبط_إذا_أصغر_مزدوج", FMT_FP, 0x53u, 1, 0x51, 0, P_NONE, 0, "XFF"},
    {"ضبط_إذا_أصغر_أو_يساوي_مزدوج", FMT_FP, 0x53u, 0, 0x51, 0, P_NONE, 0, "XFF"},
    {"صنف_مزدوج", FMT_FP, 0x53u, 1, 0x71, 0, P_NONE, 0, "XF"},
    {"نسخ_بتات_من_مزدوج", FMT_FP, 0x53u, 0, 0x71, 0, P_NONE, 0, "XF"},
    {"نسخ_بتات_إلى_مزدوج", FMT_FP, 0x53u, 0, 0x79, 0, P_NONE, 0, "FX"},
    {"تحويل_كلمة_من_مزدوج", FMT_FP, 0x53u, 7, 0x61, 0, P_NONE, 0, "XFR"},
    {"تحويل_مزدوج_من_كلمة", FMT_FP, 0x53u, 0, 0x69, 0, P_NONE, 0, "FX"},
    {"تحويل_كلمة_بلا_إشارة_من_مزدوج", FMT_FP, 0x53u, 7, 0x61, 0, P_NONE, 1, "XFR"},
    {"تحويل_مزدوج_من_كلمة_بلا_إشارة", FMT_FP, 0x53u, 0, 0x69, 0, P_NONE, 1, "FX"},
    {"تحويل_كلمة_مزدوجة_من_مزدوج", FMT_FP, 0x53u, 7, 0x61, 0, P_NONE, 2, "XFR"},
    {"تحويل_مزدوج_من_كلمة_مزدوجة", FMT_FP, 0x53u, 7, 0x69, 0, P_NONE, 2, "FXR"},
    {"تحويل_كلمة_مزدوجة_بلا_إشارة_من_مزدوج", FMT_FP, 0x53u, 7, 0x61, 0, P_NONE, 3, "XFR"},
    {"تحويل_مزدوج_من_كلمة_مزدوجة_بلا_إشارة", FMT_FP, 0x53u, 7, 0x69, 0, P_NONE, 3, "FXR"},
    {"نسخ_مزدوج", FMT_FP, 0x53u, 0, 0x11, 0, P_NONE, 0, "FFD"},
    {"سالب_مزدوج", FMT_FP, 0x53u, 1, 0x11, 0, P_NONE, 0, "FFD"},
    {"مطلق_مزدوج", FMT_FP, 0x53u, 2, 0x11, 0, P_NONE, 0, "FFD"},
    {"ضرب_جمع_مزدوج", FMT_FP4, 0x43u, 7, 1, 0, P_NONE, 0, "FFFFR"},
    {"ضرب_طرح_مزدوج", FMT_FP4, 0x47u, 7, 1, 0, P_NONE, 0, "FFFFR"},
    {"سالب_ضرب_طرح_مزدوج", FMT_FP4, 0x4Bu, 7, 1, 0, P_NONE, 0, "FFFFR"},
    {"سالب_ضرب_جمع_مزدوج", FMT_FP4, 0x4Fu, 7, 1, 0, P_NONE, 0, "FFFFR"},
    {"حمل_مزدوج", FMT_FLOAD, 0x07u, 3, 0, 0, P_NONE, 0, NULL},
    {"خزن_مزدوج", FMT_FSTORE, 0x27u, 3, 0, 0, P_NONE, 0, NULL},
    {"تحويل_عشري_من_مزدوج", FMT_FP, 0x53u, 7, 0x20, 0, P_NONE, 1, "FFR"},
    {"تحويل_مزدوج_من_عشري", FMT_FP, 0x53u, 0, 0x21, 0, P_NONE, 0, "FF"},

    /* pseudo-instructions (approved names) */
    {"حمل_قيمة",       FMT_PSEUDO, 0, 0, 0, 0, P_LI, 0, NULL},
    {"حمل_عنوان",      FMT_PSEUDO, 0, 0, 0, 0, P_LA, 0, NULL},
    {"نسخ",            FMT_PSEUDO, 0, 0, 0, 0, P_MV, 0, NULL},
    {"نفي",            FMT_PSEUDO, 0, 0, 0, 0, P_NOT, 0, NULL},
    {"سالب",           FMT_PSEUDO, 0, 0, 0, 0, P_NEG, 0, NULL},
    {"لا_عملية",       FMT_PSEUDO, 0, 0, 0, 0, P_NOP, 0, NULL},
    {"قفز",            FMT_PSEUDO, 0, 0, 0, 0, P_J, 0, NULL},
    {"قفز_بسجل",       FMT_PSEUDO, 0, 0, 0, 0, P_JR, 0, NULL},
    {"استدعاء",        FMT_PSEUDO, 0, 0, 0, 0, P_CALL, 0, NULL},
    {"استدعاء_أخير",   FMT_PSEUDO, 0, 0, 0, 0, P_TAIL, 0, NULL},
    {"عودة",           FMT_PSEUDO, 0, 0, 0, 0, P_RET, 0, NULL},
    {"تفرع_إذا_صفر",                     FMT_PSEUDO, 0, 0, 0, 0, P_BEQZ, 0, NULL},
    {"تفرع_إذا_ليس_صفرا",                FMT_PSEUDO, 0, 0, 0, 0, P_BNEZ, 0, NULL},
    {"تفرع_إذا_أكبر",                    FMT_PSEUDO, 0, 0, 0, 0, P_BGT, 0, NULL},
    {"تفرع_إذا_أصغر_أو_يساوي",           FMT_PSEUDO, 0, 0, 0, 0, P_BLE, 0, NULL},
    {"تفرع_إذا_أكبر_بلا_إشارة",          FMT_PSEUDO, 0, 0, 0, 0, P_BGTU, 0, NULL},
    {"تفرع_إذا_أصغر_أو_يساوي_بلا_إشارة", FMT_PSEUDO, 0, 0, 0, 0, P_BLEU, 0, NULL},
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

static const int ftemp_regs[] = {0, 1, 2, 3, 4, 5, 6, 7, 28, 29, 30, 31};
static const int fsaved_regs[] = {8, 9, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27};
static const int farg_regs[] = {10, 11, 12, 13, 14, 15, 16, 17};
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

/* floating-point registers (decision 69) */
static const RegFamily ffamilies[] = {
    {"عشري", NULL, 32},
    {"مؤقت_عشري", ftemp_regs, 12},
    {"محفوظ_عشري", fsaved_regs, 12},
    {"وسيط_عشري", farg_regs, 8},
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

/* Control and status registers by name (A64). Any other one can
 * be written as a number, ٠ to ٥٩٠٤ in file order. */
typedef struct {
    const char *name;
    int number;
} CsrName;

static const CsrName csrs[] = {
    {"أعلام_العشري", 0x001},              {"نمط_التقريب", 0x002},
    {"تحكم_العشري", 0x003},
    {"حالة_المشرف", 0x100},              {"تمكين_مقاطعات_المشرف", 0x104},
    {"متجه_فخ_المشرف", 0x105},           {"تمكين_عدادات_المشرف", 0x106},
    {"مسودة_المشرف", 0x140},             {"عنوان_استثناء_المشرف", 0x141},
    {"سبب_فخ_المشرف", 0x142},            {"قيمة_فخ_المشرف", 0x143},
    {"مقاطعات_المشرف_المعلقة", 0x144},   {"جدول_الصفحات", 0x180},
    {"حالة_الآلة", 0x300},               {"امتدادات_الآلة", 0x301},
    {"تفويض_استثناءات_الآلة", 0x302},    {"تفويض_مقاطعات_الآلة", 0x303},
    {"تمكين_مقاطعات_الآلة", 0x304},      {"متجه_فخ_الآلة", 0x305},
    {"تمكين_عدادات_الآلة", 0x306},       {"بيئة_الآلة", 0x30A},
    {"مسودة_الآلة", 0x340},              {"عنوان_استثناء_الآلة", 0x341},
    {"سبب_فخ_الآلة", 0x342},             {"قيمة_فخ_الآلة", 0x343},
    {"مقاطعات_الآلة_المعلقة", 0x344},
    {"إعداد_الحماية٠", 0x3A0},           {"عنوان_الحماية٠", 0x3B0},
    {"الدورات", 0xC00},                  {"الوقت", 0xC01},
    {"رقم_الخيط", 0xF14},
};

int find_csr(const uint8_t *name, size_t len)
{
    size_t i;
    for (i = 0; i < sizeof csrs / sizeof csrs[0]; i++) {
        if (name_eq(name, len, csrs[i].name)) {
            return csrs[i].number;
        }
    }
    return -1;
}

static int family_register(const RegFamily *fams, size_t nfams, const uint8_t *name, size_t len)
{
    size_t i;
    for (i = 0; i < nfams; i++) {
        size_t plen = strlen(fams[i].prefix);
        int n;
        if (len <= plen || memcmp(name, fams[i].prefix, plen) != 0) {
            continue;
        }
        n = suffix_number(name + plen, len - plen);
        if (n < 0) {
            return -1;
        }
        if (n >= fams[i].count) {
            return -2;
        }
        return fams[i].map != NULL ? fams[i].map[n] : n;
    }
    return -1;
}

int find_register(const uint8_t *name, size_t len)
{
    size_t i;
    for (i = 0; i < sizeof fixed_regs / sizeof fixed_regs[0]; i++) {
        if (name_eq(name, len, fixed_regs[i].name)) {
            return fixed_regs[i].number;
        }
    }
    return family_register(families, sizeof families / sizeof families[0], name, len);
}

int find_fregister(const uint8_t *name, size_t len)
{
    return family_register(ffamilies, sizeof ffamilies / sizeof ffamilies[0], name, len);
}

/* Rounding modes of the F and D instructions (A83) */
static const CsrName roundings[] = {
    {"أقرب_زوجي", 0}, {"نحو_الصفر", 1}, {"للأسفل", 2}, {"للأعلى", 3},
    {"أقرب_أبعد", 4}, {"حسب_النمط", 7},
};

int find_rounding(const uint8_t *name, size_t len)
{
    size_t i;
    for (i = 0; i < sizeof roundings / sizeof roundings[0]; i++) {
        if (name_eq(name, len, roundings[i].name)) {
            return roundings[i].number;
        }
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
    {".تضمين", DIR_INCBIN},
    {".ملف", DIR_DBG_FILE}, {".موضع", DIR_DBG_LINE}, {".دالة", DIR_DBG_FUNC},
    {".نطاق", DIR_DBG_SCOPE}, {".متغير", DIR_DBG_VAR}, {".نوع", DIR_DBG_TYPE},
    {".حقل", DIR_DBG_FIELD}, {".عضو", DIR_DBG_MEMBER}, {".معامل", DIR_DBG_PARAM},
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
    if (name_eq(name, len, "عالي_نسبي")) {
        return RELOC_PCREL_HI;
    }
    if (name_eq(name, len, "أدنى_نسبي")) {
        return RELOC_PCREL_LO;
    }
    return RELOC_NONE;
}
