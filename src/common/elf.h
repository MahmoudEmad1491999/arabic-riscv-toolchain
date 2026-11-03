/* elf.h - the ELF64 constants the toolchain uses (RISC-V, little-endian). */
#ifndef ELF_H
#define ELF_H

#define ELF_EHDR_SIZE   64
#define ELF_SHDR_SIZE   64
#define ELF_SYM_SIZE    24
#define ELF_RELA_SIZE   24

#define ET_REL          1
#define ET_EXEC         2
#define EM_RISCV        243

#define SHT_NULL        0
#define SHT_PROGBITS    1
#define SHT_SYMTAB      2
#define SHT_STRTAB      3
#define SHT_RELA        4
#define SHT_NOBITS      8

#define SHF_WRITE       0x1u
#define SHF_ALLOC       0x2u
#define SHF_EXECINSTR   0x4u
#define SHF_INFO_LINK   0x40u

#define SHN_UNDEF       0
#define SHN_ABS         0xFFF1u

#define STB_LOCAL       0
#define STB_GLOBAL      1
#define STT_NOTYPE      0
#define STT_SECTION     3

#define R_RISCV_32      1
#define R_RISCV_64      2
#define R_RISCV_BRANCH  16
#define R_RISCV_JAL     17
#define R_RISCV_CALL_PLT 19
#define R_RISCV_PCREL_HI20 23
#define R_RISCV_PCREL_LO12_I 24
#define R_RISCV_PCREL_LO12_S 25
#define R_RISCV_HI20    26
#define R_RISCV_LO12_I  27
#define R_RISCV_LO12_S  28

#endif
