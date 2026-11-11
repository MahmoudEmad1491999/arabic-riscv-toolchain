/* cc.h - the passes of the غرة compiler and what they share. */
#ifndef CC_H
#define CC_H

#include "ast.h"

/* lex.c */
int cc_lex(const uint32_t *cps, size_t count, Arena *a, Tok **out, size_t *ntoks);
const char *kind_text(Kind k);

/* types.c */
extern Type *ty_void, *ty_bool, *ty_char, *ty_size, *ty_untyped, *ty_null;
extern Type *ty_real32, *ty_real64, *ty_untyped_real;
void types_init(Arena *a);
Type *ty_int(int bits, int is_signed);
Type *ty_scalar_by_name(const uint8_t *name, size_t len);
Type *ty_new(Arena *a, TypeKind kind);
int ty_equal(const Type *a, const Type *b);
uint64_t ty_size_of(const Type *t);
uint64_t ty_align_of(const Type *t);
int ty_is_int(const Type *t);       /* T_INT (حجم included) */
int ty_is_aggregate(const Type *t); /* lives in memory: array, struct */
int ty_is_real(const Type *t);      /* T_FLOAT */
int ty_has_real(const Type *t);     /* a real number anywhere inside (ABI, A84) */
const char *ty_name(const Type *t); /* for messages, file order */

/* parse.c */
typedef struct {
    Arena *arena;
    Module *module;
    Tok *toks;
    size_t ntoks;
    size_t pos;
    int errors;
    int no_struct_lit;    /* inside a condition: `name }` starts a block */
} Parser;

int parse_module(Arena *a, Module *m, Tok *toks, size_t ntoks);

/* check.c */
typedef struct Program {
    Arena *arena;
    Module **modules;
    size_t nmodules;
    Module *root;
    Buf strings;          /* Expr* of every string literal, for the data section */
    Decl *main_func;
    int no_main;          /* compiling a library module: no رئيسية needed */
    Module *only;         /* gen: emit this module only (NULL: every module with bodies) */
    int debug;            /* --تنقيح: write debug rows (decision 64, A89) */
} Program;

int check_program(Program *p);
int expr_is_volatile(const Expr *e); /* a متطاير location (decision 41) */

/* const helpers (check.c) */
ConstVal cv_from_u64(uint64_t v);
int cv_fits(ConstVal v, const Type *t);
uint64_t cv_bits(ConstVal v); /* two's complement bits */

/* iface.c: the module's interface as غرة text (decision 36) */
int iface_text(Module *m, Buf *out);

/* gen.c */
int gen_program(Program *p, Buf *out);

/* main.c: diagnostics carry the current module's file name */
void cc_set_file(const char *path);

#endif
