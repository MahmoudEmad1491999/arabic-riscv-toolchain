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
#define OBJ_FLAG_EXECUTABLE 1u   /* decision 53 */
#define OBJ_PROGRAM_BASE    0x10000u
/* A system import of an executable (decision 43): bound by the loader. */
#define OBJ_SYSTEM_PREFIX   "_نظام_"

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
    uint32_t flags;         /* OBJ_FLAG_* */
    const uint8_t *iface;   /* encoded interface region (obj_iface_encode), or NULL */
    size_t iface_len;
} ObjOut;

/* Module interface (docs/spec-object.md, decisions 35-36). */
typedef struct {
    const uint8_t *module;  /* dotted module name */
    uint32_t module_len;
    uint64_t hash;          /* FNV-1a of text */
    uint64_t source_hash;   /* FNV-1a of the module's source file */
    uint32_t nimports;
    const uint8_t *imports; /* encoded list, read with obj_iface_import */
    const uint8_t *text;    /* غرة source of the interface */
    uint32_t text_len;
} ObjIface;

uint64_t obj_fnv1a(const uint8_t *data, size_t len);

/* Encoding: the imports are given as parallel arrays. */
void obj_iface_encode(const uint8_t *module, size_t module_len, uint64_t source_hash,
                      size_t nimports, const uint8_t *const *names, const size_t *lens,
                      const uint64_t *hashes, const uint8_t *text, size_t text_len, Buf *out);
/* Decodes and checks a region; returns 0 if it is well formed. */
int obj_iface_decode(const uint8_t *data, size_t len, ObjIface *out);
void obj_iface_import(const ObjIface *f, uint32_t i, const uint8_t **name, uint32_t *len,
                      uint64_t *hash);

/* Serialises into out. */
void obj_serialize(const ObjOut *o, Buf *out);

/* A validated view into a file image; everything points into `file`. */
typedef struct {
    const uint8_t *file;
    size_t len;
    uint32_t flags;
    uint64_t size[OBJ_NSEC];
    uint32_t align[OBJ_NSEC];
    uint64_t content_off[OBJ_NSEC];
    uint32_t nsyms, nrelocs;
    uint64_t syms_off, relocs_off, names_off, names_size, iface_off, iface_size;
} ObjView;

int obj_is_object(const uint8_t *data, size_t len);   /* magic check */
/* Returns 0 if the file is well formed (every offset, index and kind checked). */
int obj_parse(const uint8_t *data, size_t len, ObjView *v);
/* The interface of a parsed object; returns -1 if it has none. */
int obj_iface(const ObjView *v, ObjIface *out);
void obj_sym(const ObjView *v, uint32_t i, ObjSym *out);
void obj_reloc(const ObjView *v, uint32_t i, ObjReloc *out);

/* Libraries (.مكتبة, decision 38): several objects in one file. */
typedef struct {
    const uint8_t *file;
    size_t len;
    uint32_t n;
} ObjLib;

typedef struct {
    const uint8_t *data;    /* the member's .كائن image */
    size_t len;
    const uint8_t *name;    /* file name it was added as */
    uint32_t name_len;
} ObjLibMember;

#define OBJ_LIB_HEADER 16u
#define OBJ_LIB_ENTRY  24u

int obj_is_lib(const uint8_t *data, size_t len);
int obj_lib_parse(const uint8_t *data, size_t len, ObjLib *lib);   /* 0 if well formed */
int obj_lib_member(const ObjLib *lib, uint32_t i, ObjLibMember *m); /* -1 past the end */
void obj_lib_serialize(size_t n, const uint8_t *const *names, const size_t *name_lens,
                       const uint8_t *const *datas, const size_t *sizes, Buf *out);

/* Text form for people (docs/spec-object.md). */
void obj_dump(const ObjView *v, Buf *out);

#endif
