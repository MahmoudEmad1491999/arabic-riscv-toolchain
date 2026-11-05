/* obj.h - our own object format (.كائن), see docs/spec-object.md.
 * One place knows the layout: the assembler writes through obj_write, the
 * linker and the viewer read through obj_parse. */
#ifndef OBJ_H
#define OBJ_H

#include <stddef.h>
#include <stdint.h>

#include "buf.h"

#define OBJ_VERSION     1u
#define OBJ_HEADER_SIZE 80u
#define OBJ_SYM_SIZE    24u
#define OBJ_RELOC_SIZE  24u
#define OBJ_NSEC        4
#define OBJ_MAX_ALIGN   4096u
#define OBJ_NO_SYMBOL   0xFFFFFFFFu

enum { OBJ_SEC_TEXT, OBJ_SEC_DATA, OBJ_SEC_RODATA, OBJ_SEC_BSS,
       OBJ_SEC_ABS = 4, OBJ_SEC_UNDEF = 255 };
enum { OBJ_PRIVATE, OBJ_EXPORTED, OBJ_IMPORTED };
enum { OBJ_SHAPE_UNKNOWN, OBJ_SHAPE_FUNCTION, OBJ_SHAPE_DATA };
enum { OBJ_REL_BRANCH = 1, OBJ_REL_JAL, OBJ_REL_CALL, OBJ_REL_HI, OBJ_REL_LO_I,
       OBJ_REL_LO_S, OBJ_REL_PCREL_HI, OBJ_REL_PCREL_LO_I, OBJ_REL_PCREL_LO_S,
       OBJ_REL_WORD, OBJ_REL_DWORD, OBJ_REL_LAST = OBJ_REL_DWORD };

typedef struct {
    const uint8_t *name;
    uint32_t len;
    uint8_t kind, shape, section;
    uint32_t size;
    uint64_t value;
} ObjSym;

typedef struct {
    uint64_t offset;
    uint8_t section, kind;
    uint32_t sym;       /* OBJ_NO_SYMBOL for the PC-relative low kinds */
    int64_t extra;      /* addend, or the paired high offset */
} ObjReloc;

/* What the writer needs. */
typedef struct {
    const uint8_t *content[OBJ_NSEC];   /* bss: NULL */
    uint64_t size[OBJ_NSEC];
    uint32_t align[OBJ_NSEC];
    const ObjSym *syms;
    size_t nsyms;
    const ObjReloc *relocs;
    size_t nrelocs;
} ObjOut;

/* Serialises into out. */
void obj_serialize(const ObjOut *o, Buf *out);

/* A validated view into a file image; everything points into `file`. */
typedef struct {
    const uint8_t *file;
    size_t len;
    uint64_t size[OBJ_NSEC];
    uint32_t align[OBJ_NSEC];
    uint64_t content_off[OBJ_NSEC];
    uint32_t nsyms, nrelocs;
    uint64_t syms_off, relocs_off, names_off, names_size, iface_off, iface_size;
} ObjView;

int obj_is_object(const uint8_t *data, size_t len);   /* magic check */
/* Returns 0 if the file is well formed (every offset, index and kind checked). */
int obj_parse(const uint8_t *data, size_t len, ObjView *v);
void obj_sym(const ObjView *v, uint32_t i, ObjSym *out);
void obj_reloc(const ObjView *v, uint32_t i, ObjReloc *out);

/* Text form for people (docs/spec-object.md). */
void obj_dump(const ObjView *v, Buf *out);

#endif
