/* diag.h - Arabic diagnostics.
 *
 * A message is built in FILE ORDER (the same convention as source files:
 * numbers least significant digit first). Before printing to the plain
 * left-to-right terminal the whole line is reversed, then runs of Latin
 * characters (paths, file names) are reversed back so they stay readable.
 * Set DIAG_ORDER=file to print in file order instead (used by the tests).
 * See docs/assumptions.md A2.
 *
 * All message texts live in one table in diag.c.
 */
#ifndef DIAG_H
#define DIAG_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    D_CANNOT_READ,
    D_CANNOT_WRITE,
    D_BAD_UTF8,
    D_USAGE,
    D_ASCII_DIGIT,
    D_PERSIAN_DIGIT,
    D_LATIN_LETTER,
    D_FORBIDDEN_CHAR,
    D_UNEXPECTED_CHAR,
    D_ASCII_COMMA,
    D_AR_SEMICOLON,
    D_NUM_NO_DIGITS,
    D_NUM_BAD_DIGIT,
    D_NUM_SEPARATOR,
    D_NUM_OVERFLOW,
    D_LETTER_AFTER_NUMBER,
    D_UNTERMINATED_STRING,
    D_BAD_ESCAPE,
    D_BAD_CHAR_LITERAL,
    D_UNKNOWN_INSN,
    D_EXPECTED_REGISTER,
    D_REGISTER_RANGE,
    D_EXPECTED_COMMA,
    D_EXTRA_TOKENS,
    D_EXPECTED_EXPR,
    D_UNCLOSED_BRACKET,
    D_BRACKET_MISMATCH,
    D_NOT_RELOCATABLE,
    D_NOT_CONSTANT,
    D_DIV_ZERO,
    D_VALUE_RANGE,
    D_NEEDS_LO,
    D_NEEDS_HI,
    D_RELOC_NOT_ALLOWED,
    D_UNDEFINED_SYMBOL,
    D_REDEFINED_SYMBOL,
    D_RESERVED_NAME,
    D_LOCAL_LABEL_MISSING,
    D_LOCAL_LABEL_BASE,
    D_BRANCH_TARGET,
    D_BRANCH_RANGE,
    D_MISALIGNED_TARGET,
    D_UNKNOWN_SECTION,
    D_NOT_IN_BSS,
    D_EXPECTED_STRING,
    D_EXPECTED_SYMBOL,
    D_CHAR_TOO_WIDE,
    D_EXPECTED_PLUS_MINUS,
    D_TOO_MANY_ERRORS,
    D_LINK_USAGE,
    D_LINK_BAD_OBJECT,
    D_OBJ_PCREL_LABEL,
    D_OBJ_USAGE,
    D_LINK_STALE,
    D_LINK_DUP_MODULE,
    D_CC_IFACE,
    D_LIB_USAGE,
    D_LINK_BARE_ENTRY,
    D_EXPECTED_CSR,
    D_INCLUDE_FAILED,
    D_FS_USAGE,
    D_FS_TOO_MANY,
    D_FS_BAD_NAME,
    D_FS_DUPLICATE,
    D_FS_FULL,
    D_LINK_UNSUPPORTED_SECTION,
    D_LINK_UNSUPPORTED_RELOC,
    D_LINK_UNDEFINED,
    D_LINK_DUPLICATE,
    D_LINK_NO_ENTRY,
    D_LINK_RANGE,
    D_LINK_PCREL_LO,
    D_LINK_ALIGN,
    D_CC_USAGE,
    D_CC_EXPECTED,
    D_CC_UNEXPECTED,
    D_CC_UNKNOWN_NAME,
    D_CC_REDEFINED,
    D_CC_UNKNOWN_TYPE,
    D_CC_TYPE_MISMATCH,
    D_CC_NOT_FIT,
    D_CC_MIXED_TYPES,
    D_CC_NEED_BOOL,
    D_CC_NEED_INT,
    D_CC_NOT_LVALUE,
    D_CC_READONLY,
    D_CC_NOT_CONST,
    D_CC_CONST_TOO_BIG,
    D_CC_ARGS,
    D_CC_NOT_CALLABLE,
    D_CC_NO_RETURN,
    D_CC_RETURN,
    D_CC_BREAK_OUTSIDE,
    D_CC_PADDING,
    D_CC_RECURSIVE_TYPE,
    D_CC_CYCLE,
    D_CC_IMPORT_CYCLE,
    D_CC_MODULE_NOT_FOUND,
    D_CC_PRIVATE,
    D_CC_NO_FIELD,
    D_CC_FIELD_MISSING,
    D_CC_FIELD_DUP,
    D_CC_COUNT,
    D_CC_INDEX_RANGE,
    D_CC_NOT_INDEXABLE,
    D_CC_NO_LENGTH,
    D_CC_UNINIT_READ,
    D_CC_SWITCH_MISSING,
    D_CC_SWITCH_DUP,
    D_CC_SWITCH_DEFAULT,
    D_CC_BAD_CONVERSION,
    D_CC_NO_EQUALITY,
    D_CC_NEED_BRACKETS,
    D_CC_COMPARE_CHAIN,
    D_CC_TOO_MANY_ARGS,
    D_CC_NOT_SUPPORTED,
    D_CC_NO_MAIN,
    D_CC_SHIFT_RANGE,
    D_CC_OPT_DEREF,
    D_CC_DUP_ENUM,
    D_CC_ARRAY_SIZE,
    D_CC_GLOBAL_INIT,
    D_CC_VOID_VALUE,
    D_CC_UNINIT_GLOBAL,
    D_CC_ASSIGN_LOOP,
    D_CC_NOT_TYPE_HERE,
    D_CC_STRUCT_LIT_HERE,
    D_CC_BAD_CHAR,
    D_CC_BAD_MODULE_NAME,
    D_CC_PACKED_ADDR,
    D_CC_VOLATILE_TYPE,
    D_CC_VOLATILE_PLACE,
    D_COUNT
} DiagId;

/* File name shown in front of every message (UTF-8). */
void diag_set_file(const char *path);

/* Reports an error at line:col (1-based). extra may be NULL; otherwise it
 * is appended after the message (e.g. the offending name). */
void diag_error(uint32_t line, uint32_t col, DiagId id,
                const uint8_t *extra, size_t extra_len);

/* Reports an error with no position (usage, I/O, linker). */
void diag_plain(DiagId id, const char *extra);

/* Same, with two extra parts: "<message> <extra>: <extra2>". */
void diag_plain2(DiagId id, const char *extra, const char *extra2);

int diag_count(void);

#endif
