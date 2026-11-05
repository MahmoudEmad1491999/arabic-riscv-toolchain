/* as.h - assembler core: two passes over the token list, then ELF output. */
#ifndef AS_H
#define AS_H

#include <stddef.h>
#include <stdint.h>

#include "../common/buf.h"
#include "../common/rt.h"
#include "lexer.h"

enum {
    SEC_TEXT,
    SEC_DATA,
    SEC_RODATA,
    SEC_BSS,
    SEC_COUNT,
    SEC_ABS = 100,
    SEC_UNDEF = 101
};

typedef struct Symbol Symbol;
struct Symbol {
    const uint8_t *name;
    size_t len;
    int defined;
    int section;      /* SEC_* */
    uint64_t value;   /* offset in its section, or the constant */
    int global;
    int is_const;     /* defined by .ثابت */
    int referenced;
    uint32_t elf_index;
    Symbol *next;     /* hash chain */
};

typedef struct {
    uint64_t offset;
    uint32_t type;
    Symbol *sym;
    int64_t addend;
} Reloc;

typedef struct {
    Buf data;         /* contents (unused for .bss) */
    uint64_t size;    /* .bss size */
    Buf relocs;       /* array of Reloc */
    uint64_t align;
} Section;

typedef struct {
    uint64_t number;
    uint32_t count;
} LocalCount;

#define SYM_BUCKETS 4096

typedef struct {
    Arena *arena;
    TokenList tl;
    size_t pos;
    int pass;                 /* 1 or 2 */
    int cur;                  /* current section */
    Section sec[SEC_COUNT];
    Symbol *buckets[SYM_BUCKETS];
    Buf symbols;              /* array of Symbol*, creation order */
    Buf locals;               /* array of LocalCount */
} Assembler;

void as_init(Assembler *as, Arena *a, const TokenList *tl);

/* Runs both passes. Returns 0 if there were no errors. */
int as_assemble(Assembler *as);

/* Releases the buffers owned by the assembler. */
void as_free(Assembler *as);

/* Writes the ELF64 relocatable object. Returns 0 on success. */
int as_write_elf(Assembler *as, const char *path);

/* Writes our own object format (.كائن). Returns 0 on success. */
int as_write_obj(Assembler *as, const char *path, const uint8_t *iface, size_t iface_len);

#endif
