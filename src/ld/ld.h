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
#include "../common/obj.h"
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
#define LD_BASE_BARE 0x80000000u  /* RAM of QEMU virt: --عاري (decision 39) */
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
    int has_iface;     /* our format with a module interface */
    ObjIface iface;    /* points into file */
} InObj;

struct GlobalSym {
    const char *name;
    int defined;
    int import;        /* program mode: a system import left for the loader */
    uint32_t import_index;
    int special;       /* defined by the linker (A67): 1 = _نهاية, 2 = _بداية_البيانات */
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
    uint64_t base;            /* LD_BASE, or LD_BASE_BARE for a bare-metal image */
    uint64_t seg_off;         /* file offset of the first segment (bare: one page) */
    int program;              /* --برنامج: our executable format (decision 53) */
    Buf imports;              /* GlobalSym* of the system imports, in first-use order */
    Buf out_relocs;           /* OutReloc: relocations left for the loader */
    Buf libs;                 /* LibMember: members of .مكتبة files not linked yet */
    Buf lib_files;            /* RtBuf of each library file */
} Linker;

typedef struct {
    int out;                  /* OUT_* */
    uint64_t offset;          /* inside the output section */
    uint8_t kind;             /* OBJ_REL_* */
    GlobalSym *sym;           /* NULL for the PC-relative low kinds */
    int64_t extra;            /* addend, or the offset of the paired high part */
} OutReloc;

typedef struct {
    const char *lib;          /* library path */
    ObjLibMember mem;
    int used;
} LibMember;

void ld_init(Linker *ld, Arena *a);
void ld_free(Linker *ld);

/* Each step returns 0 on success; errors are reported through diag. */
int ld_read_object(Linker *ld, const char *path);
int ld_link(Linker *ld);
int ld_write(Linker *ld, const char *path);
/* --برنامج: writes our executable format instead of ELF. */
int ld_write_program(Linker *ld, const char *path);

#endif
