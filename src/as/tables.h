/* tables.h - registers, mnemonics and directives (docs/spec-assembler.md). */
#ifndef TABLES_H
#define TABLES_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    FMT_R,        /* rd، rs1، rs2 */
    FMT_I,        /* rd، rs1، imm12 */
    FMT_SHIFT,    /* rd، rs1، shamt */
    FMT_LOAD,     /* rd، rs1 ± imm12 */
    FMT_STORE,    /* rs2، rs1 ± imm12 */
    FMT_BRANCH,   /* rs1، rs2، target */
    FMT_JAL,      /* rd، target */
    FMT_JALR,     /* rd، rs1 ± imm12 */
    FMT_LUI,      /* rd، imm20 */
    FMT_AUIPC,    /* rd، imm20 */
    FMT_FIXED,    /* no operands, fixed encoding */
    FMT_CSR,      /* rd، csr، rs1        (Zicsr, A64) */
    FMT_CSRI,     /* rd، csr، uimm5 */
    FMT_CSR_READ, /* rd، csr             csrr = csrrs rd، csr، صفر */
    FMT_CSR_WRITE,/* csr، rs1            csrw/csrs/csrc = csrrX صفر، csr، rs1 */
    FMT_CSRI_WRITE,/* csr، uimm5         csrwi/csrsi/csrci */
    FMT_SFENCE,   /* [rs1، rs2]          sfence.vma */
    FMT_FP,       /* F and D (decision 69): operands as in `ops` */
    FMT_FP4,      /* fused multiply-add: fd، fs1، fs2، fs3 [، rounding] */
    FMT_FLOAD,    /* fd، rs1 ± imm12 */
    FMT_FSTORE,   /* fs2، rs1 ± imm12 */
    FMT_PSEUDO    /* pseudo-instruction: see PseudoKind */
} InsnFormat;

typedef enum {
    P_NONE,
    P_LI,     /* حمل_قيمة      li   */
    P_LA,     /* حمل_عنوان     la (non-PIC: auipc + addi) */
    P_MV,     /* نسخ           mv   */
    P_NOT,    /* نفي           not  */
    P_NEG,    /* سالب          neg  */
    P_NOP,    /* لا_عملية      nop  */
    P_J,      /* قفز           j    */
    P_JR,     /* قفز_بسجل      jr   */
    P_CALL,   /* استدعاء       call */
    P_TAIL,   /* استدعاء_أخير  tail */
    P_RET,    /* عودة          ret  */
    P_BEQZ,   /* تفرع_إذا_صفر */
    P_BNEZ,   /* تفرع_إذا_ليس_صفرا */
    P_BGT,    /* تفرع_إذا_أكبر */
    P_BLE,    /* تفرع_إذا_أصغر_أو_يساوي */
    P_BGTU,   /* تفرع_إذا_أكبر_بلا_إشارة */
    P_BLEU    /* تفرع_إذا_أصغر_أو_يساوي_بلا_إشارة */
} PseudoKind;

typedef struct {
    const char *name;   /* Arabic mnemonic, UTF-8 */
    InsnFormat fmt;
    uint32_t opcode;    /* or the whole word for FMT_FIXED */
    uint32_t funct3;
    uint32_t funct7;    /* FMT_R: funct7; FMT_SHIFT: bits imm[11:...] */
    uint32_t shamt_max; /* FMT_SHIFT only */
    PseudoKind pseudo;  /* FMT_PSEUDO only */
    uint32_t rs2;       /* FMT_FP with one source: the fixed rs2 field */
    const char *ops;    /* FMT_FP: destination, then sources: F float register,
                           X integer register, D the previous source again;
                           a final R: optional rounding mode, funct3 its default */
} InsnDef;

const InsnDef *find_insn(const uint8_t *name, size_t len);

/* Register number 0..31, -1 if the name is not register-shaped,
 * -2 if register-shaped but out of range (e.g. وسيط٩). */
int find_register(const uint8_t *name, size_t len);

/* Floating-point register 0..31 (عشري، وسيط_عشري، محفوظ_عشري، مؤقت_عشري),
 * -1 / -2 as find_register. */
int find_fregister(const uint8_t *name, size_t len);

/* Rounding mode by name (A79), -1 if none. */
int find_rounding(const uint8_t *name, size_t len);

/* Number of a control and status register by its Arabic name, -1 if none. */
int find_csr(const uint8_t *name, size_t len);

typedef enum {
    DIR_NONE,
    DIR_SECTION,     /* .قسم */
    DIR_TEXT,        /* .شفرة */
    DIR_DATA,        /* .بيانات */
    DIR_RODATA,      /* .بيانات_للقراءة */
    DIR_BSS,         /* .بيانات_صفرية */
    DIR_GLOBAL,      /* .عام */
    DIR_ALIGN,       /* .محاذاة */
    DIR_BYTE,        /* .بايت */
    DIR_HALF,        /* .نصف_كلمة */
    DIR_WORD,        /* .كلمة */
    DIR_DWORD,       /* .كلمة_مزدوجة */
    DIR_STRING,      /* .سلسلة */
    DIR_ZERO,        /* .فراغ */
    DIR_EQU,         /* .ثابت */
    DIR_INCBIN       /* .تضمين "ملف": the file's bytes (A70) */
} DirKind;

DirKind find_directive(const uint8_t *name, size_t len);

/* Reserved words for relocation operators (used as عالي:رمز). */
#define RELOC_NONE 0
#define RELOC_HI   1
#define RELOC_LO   2
#define RELOC_PCREL_HI 3  /* عالي_نسبي: */
#define RELOC_PCREL_LO 4  /* أدنى_نسبي: */
int find_reloc_op(const uint8_t *name, size_t len);

int name_eq(const uint8_t *name, size_t len, const char *s);

#endif
