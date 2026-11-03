/* ld.h - رابط: static linker for RV64 ELF relocatable objects.
 *
 * Layout (docs/assumptions.md, Phase 3):
 *   segment 1 (R+X) at 0x10000: ELF header, program headers, .text, .rodata
 *   segment 2 (R+W) on the next page: .data, then .bss
 * Input sections are placed in command-line order, each at its own
 * alignment, the same way GNU ld places `*(.text)` etc.
 */
#ifndef LD_H
#define LD_H

#include <stddef.h>
#include <stdint.h>

#include "../common/buf.h"
#include "../common/rt.h"

enum {
    OUT_TEXT,
    OUT_RODATA,
    OUT_DATA,
    OUT_BSS,
    OUT_COUNT,
    OUT_NONE = -1
};

#define LD_BASE      0x10000u
#define LD_PAGE      0x1000u

typedef struct GlobalSym GlobalSym;

typedef struct {
    const char *name;
    uint32_t name_off;
    uint32_t type;
    uint64_t flags;
    uint64_t offset;   /* in the file */
    uint64_t size;
    uint64_t align;
    uint32_t link;
    uint32_t info;
    int out;           /* OUT_* or OUT_NONE */
    uint64_t out_off;  /* offset inside the output section */
} InSec;

typedef struct {
    const char *name;
    uint8_t bind;
    uint8_t type;
    uint16_t shndx;
    uint64_t value;
    GlobalSym *global; /* for global/weak symbols */
} InSym;

typedef struct {
    const char *path;
    RtBuf file;
    uint16_t shnum;
    InSec *secs;
    InSym *syms;
    size_t nsyms;
} InObj;

struct GlobalSym {
    const char *name;
    int defined;
    int weak;
    InObj *obj;
    size_t sym;        /* index in obj->syms */
    GlobalSym *next;
};

#define LD_BUCKETS 4096

typedef struct {
    Arena *arena;
    InObj *objs;
    size_t nobjs;
    GlobalSym *buckets[LD_BUCKETS];
    Buf globals;              /* GlobalSym* in definition order */
    uint64_t out_size[OUT_COUNT];
    uint64_t out_align[OUT_COUNT];
    uint64_t out_addr[OUT_COUNT];
    uint64_t out_fileoff[OUT_COUNT];
    Buf out_data[OUT_COUNT];  /* contents (not for .bss) */
    uint64_t entry;
    int nphdr;
} Linker;

void ld_init(Linker *ld, Arena *a);
void ld_free(Linker *ld);

/* Each step returns 0 on success; errors are reported through diag. */
int ld_read_object(Linker *ld, const char *path);
int ld_link(Linker *ld);
int ld_write(Linker *ld, const char *path);

#endif
