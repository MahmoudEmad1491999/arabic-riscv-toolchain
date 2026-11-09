/* ast.h - tokens, syntax tree and types of the غرة compiler (مترجم). */
#ifndef AST_H
#define AST_H

#include <stddef.h>
#include <stdint.h>

#include "../common/buf.h"
#include "../common/rt.h"

/* ------------------------------------------------------------------ */
/* Tokens                                                              */

typedef enum {
    K_EOF,
    K_IDENT,
    K_INT,        /* integer literal: value */
    K_REAL,       /* real literal (decision 68): value = 64-bit pattern, value32 = 32-bit */
    K_CHAR,       /* character literal: value = code point */
    K_STRING,     /* str / str_len: UTF-8 bytes */
    /* punctuation */
    K_COMMA,      /* ، */
    K_SEMI,       /* ؛ */
    K_COLON,
    K_DOT,
    K_CARET,      /* ^ */
    K_AT,         /* @ */
    K_PLUS, K_MINUS, K_STAR, K_SLASH,
    K_ASSIGN,     /* = */
    K_EQ, K_NE,   /* == != */
    K_LT, K_GT, K_LE, K_GE, /* by MEANING: less, greater, less-or-equal, greater-or-equal */
    K_OPEN_PAREN, K_CLOSE_PAREN,     /* ) (  in file order */
    K_OPEN_BRACE, K_CLOSE_BRACE,     /* } {  */
    K_OPEN_SQUARE, K_CLOSE_SQUARE,   /* ] [  */
    /* keywords */
    K_IMPORT, K_AS, K_PUBLIC, K_EXTERN, K_FUNC, K_VAR, K_CONST, K_STRUCT, K_PACKED, K_VOLATILE,
    K_ENUM, K_IF, K_ELSE, K_FOUND, K_WHILE, K_DO, K_FOR, K_FROM, K_BEFORE, K_IN,
    K_SWITCH, K_CASE, K_BREAK, K_CONTINUE, K_RETURN, K_TRUE, K_FALSE, K_NULL,
    K_UNINIT, K_OPTIONAL, K_AND, K_OR, K_NOT, K_BAND, K_BOR, K_BXOR, K_BNOT,
    K_SHL, K_SHR, K_REM, K_REPEAT,
    /* built-in operations written like calls */
    K_CONVERT, K_CONVERT_CHECKED, K_TRUNCATE, K_REINTERPRET, K_SIZEOF, K_ALIGNOF,
    K_INDEX_CHECKED, K_SLICE_CHECKED, K_SLICE_FROM,
    K_ADD_CHECKED, K_SUB_CHECKED, K_MUL_CHECKED, K_ADD_SAT, K_SUB_SAT, K_MUL_SAT,
    /* real numbers (decisions 66-68) */
    K_ROUND, K_ROUND_NEAREST, K_FLOOR, K_CEIL, K_IS_NAN, K_FMA
} Kind;

typedef struct {
    Kind kind;
    uint32_t line, col;
    const uint8_t *text;  /* identifiers (UTF-8) */
    size_t len;
    uint64_t value;
    uint64_t value32;     /* K_REAL: the literal rounded to 32 bits */
    int real_err;         /* K_REAL: REAL_ERR_* bits */
    const uint8_t *str;
    size_t str_len;
} Tok;

/* Tok.real_err: the literal does not fit a format (decision 68) */
#define REAL_ERR_BIG64 1
#define REAL_ERR_SMALL64 2
#define REAL_ERR_BIG32 4
#define REAL_ERR_SMALL32 8

/* ------------------------------------------------------------------ */
/* Types                                                               */

typedef enum {
    T_VOID,
    T_INT,        /* signed/unsigned 8..64, and حجم (is_size) */
    T_BOOL,
    T_CHAR,
    T_PTR,        /* ^T, may be readonly */
    T_OPT,        /* اختياري ^T: elem is the pointer type */
    T_ARRAY,
    T_SLICE,
    T_STRUCT,
    T_ENUM,
    T_FUNC,
    T_NAMED,      /* a struct/enum name not resolved yet (parser output) */
    T_UNTYPED,    /* integer constant without a type yet */
    T_NULLLIT,    /* the literal لاشيء before it meets an optional type */
    T_FLOAT,      /* حقيقي٢٣ / حقيقي٤٦: bits */
    T_UNTYPED_REAL /* real literal without a type yet */
} TypeKind;

typedef struct Type Type;
typedef struct Decl Decl;

struct Type {
    TypeKind kind;
    int bits;             /* T_INT */
    int is_signed;        /* T_INT */
    int is_size;          /* T_INT: حجم */
    int readonly;         /* T_PTR, T_SLICE: elements cannot be written */
    int vol;              /* T_PTR: points to متطاير (decision 41) */
    Type *elem;           /* T_PTR, T_OPT, T_ARRAY, T_SLICE; T_FUNC: result */
    uint64_t count;       /* T_ARRAY */
    Decl *decl;           /* T_STRUCT, T_ENUM */
    Type **params;        /* T_FUNC */
    size_t nparams;
    /* parser output, resolved by the checker */
    struct Expr *count_expr;  /* T_ARRAY */
    const uint8_t *qual;      /* T_NAMED: module alias or NULL */
    size_t qual_len;
    const uint8_t *tname;     /* T_NAMED */
    size_t tname_len;
    Tok tok;
};

/* ------------------------------------------------------------------ */
/* Expressions                                                         */

typedef enum {
    E_INT, E_CHAR, E_STRING, E_BOOL, E_NULL,
    E_REAL,       /* real literal: value in tok (decision 68) */
    E_NAME,       /* resolved to a local, global, constant, function or enum member */
    E_MEMBER,     /* base.name (module member, enum member, struct field, .طول) */
    E_CALL,
    E_INDEX,      /* a]i[ */
    E_SLICE,      /* a]lo : hi[ */
    E_DEREF,      /* p^ */
    E_ADDR,       /* @x */
    E_UNARY,      /* op: K_MINUS, K_BNOT, K_NOT */
    E_BINARY,
    E_STRUCT_LIT,
    E_ARRAY_LIT,
    E_ARRAY_REPEAT,
    E_BUILTIN,    /* conversions, مقاس, checked operations */
    E_PAREN       /* kept so the parser can tell bracketed operands apart */
} ExprKind;

typedef struct Expr Expr;
typedef struct Local Local;

/* compile-time integer value: exact sign + magnitude. A real constant
 * keeps its IEEE 754 bit pattern in mag (neg = 0). */
typedef struct {
    int neg;
    uint64_t mag;
} ConstVal;

typedef enum {
    REF_NONE, REF_LOCAL, REF_GLOBAL, REF_CONST, REF_FUNC, REF_ENUM_MEMBER, REF_MODULE,
    REF_LENGTH, REF_FIELD
} RefKind;

struct Expr {
    ExprKind kind;
    Tok tok;              /* position (and operator for unary/binary) */
    Type *type;
    int is_const;         /* value known at compile time (cval / bool in cval.mag) */
    ConstVal cval;
    Expr *a, *b, *c;      /* operands */
    Expr **args;          /* calls, literals */
    size_t nargs;
    const uint8_t **names;/* struct literal field names */
    size_t *name_lens;
    Type *type_arg;       /* type written in a built-in or literal */
    const uint8_t *name;  /* E_NAME / E_MEMBER */
    size_t name_len;
    RefKind ref;
    Local *local;
    Decl *decl;
    int member_index;     /* field index / enum member index */
    int bracketed;        /* the expression was written in brackets */
    int string_id;        /* E_STRING: label number */
    struct Module *str_module; /* E_STRING: module whose object holds it */
};

/* ------------------------------------------------------------------ */
/* Statements                                                          */

typedef enum {
    S_BLOCK, S_VAR, S_CONST, S_ASSIGN, S_EXPR, S_IF, S_IF_FOUND, S_WHILE, S_DO,
    S_FOR_RANGE, S_FOR_EACH, S_SWITCH, S_BREAK, S_CONTINUE, S_RETURN
} StmtKind;

typedef struct Stmt Stmt;

struct Local {
    const uint8_t *name;
    size_t len;
    Type *type;
    int32_t offset;       /* from the frame pointer */
    int is_param;
    int readonly;         /* parameters, loop variables */
    int uninit_index;     /* >= 0 if declared غير_مهيأ (definite assignment) */
    Decl *const_decl;     /* local ثابت */
    Tok tok;
};

typedef struct {
    Expr **values;
    size_t nvalues;
    Stmt *body;           /* NULL values = وإلا */
} Case;

struct Stmt {
    StmtKind kind;
    Tok tok;
    Stmt **stmts;         /* block */
    size_t nstmts;
    Local *local;         /* S_VAR, S_IF_FOUND, loops */
    Local *local2;        /* S_FOR_EACH index variable */
    Local *hidden1, *hidden2; /* compiler temporaries */
    Expr *e1, *e2;        /* condition / value / target */
    Type *decl_type;
    int uninit;
    int vol;              /* متغير متطاير */
    Stmt *then_s, *else_s;
    Case *cases;
    size_t ncases;
    Decl *const_decl;
};

/* ------------------------------------------------------------------ */
/* Declarations and modules                                            */

typedef enum { D_FUNC, D_VAR, D_CONST, D_STRUCT, D_ENUM } DeclKind;

typedef struct Module Module;

typedef struct {
    const uint8_t *name;
    size_t len;
    Type *type;
    uint64_t offset;
    Tok tok;
} Field;

typedef struct {
    const uint8_t *name;
    size_t len;
    Expr *value_expr;
    ConstVal value;
    Tok tok;
} EnumMember;

struct Decl {
    DeclKind kind;
    Tok tok;
    const uint8_t *name;
    size_t len;
    Module *module;
    int is_public;
    int is_extern;        /* خارجي: no body, raw symbol */
    int raw_export;       /* عام خارجي دالة: body, raw symbol */
    int is_volatile;      /* متغير متطاير (decision 41) */
    /* functions */
    Local **params;
    size_t nparams;
    Type *ret;            /* NULL: no result */
    Stmt *body;
    Type *func_type;
    /* variables and constants */
    Type *type;
    Expr *init;
    int state;            /* 0 = unresolved, 1 = resolving, 2 = done (cycle detection) */
    /* structures */
    Field *fields;
    size_t nfields;
    int packed;
    uint64_t size, align;
    Type *self_type;
    /* enumerations */
    EnumMember *members;
    size_t nmembers;
    Type *underlying;
    /* code generation */
    const uint8_t *asm_name;
    size_t asm_len;
    int32_t frame_size;
    size_t nuninit;
};

typedef struct {
    const uint8_t *alias;
    size_t alias_len;
    const uint8_t *path;  /* dotted module name */
    size_t path_len;
    Module *module;
    Tok tok;
} Import;

struct Module {
    const uint8_t *name;  /* dotted name, e.g. غرة.نصوص */
    size_t name_len;
    const char *path;     /* file */
    Import *imports;
    size_t nimports;
    Decl **decls;
    size_t ndecls;
    int interface_only;   /* read from an object: declarations without bodies */
    uint64_t source_hash; /* FNV-1a of the source file */
    uint64_t iface_hash;  /* FNV-1a of the interface text */
    const char *obj_path; /* interface_only: the object (or archive) that has it */
    int visiting;         /* import cycle detection */
    int loaded;
};

/* ------------------------------------------------------------------ */
/* Helpers shared by the passes                                        */

int bytes_eq(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen);
int bytes_eq_str(const uint8_t *a, size_t alen, const char *s);

#endif
