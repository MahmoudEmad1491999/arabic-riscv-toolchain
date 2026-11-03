/* diag.c - Arabic diagnostics. */
#include "diag.h"
#include "number.h"
#include "rt.h"
#include "utf8.h"

#include <string.h>

#define DIAG_MAX_ERRORS 50
#define DIAG_LINE_MAX   1024

static const char *const diag_text[D_COUNT] = {
    [D_CANNOT_READ]         = "لا يمكن قراءة الملف",
    [D_CANNOT_WRITE]        = "لا يمكن كتابة الملف",
    [D_BAD_UTF8]            = "ترميز غير صالح في الملف",
    [D_USAGE]               = "الاستخدام: مجمع ملف_الدخل ملف_الخرج",
    [D_ASCII_DIGIT]         = "الأرقام اللاتينية غير مسموحة",
    [D_PERSIAN_DIGIT]       = "الأرقام الفارسية غير مسموحة",
    [D_LATIN_LETTER]        = "الحروف اللاتينية غير مسموحة في الأسماء",
    [D_FORBIDDEN_CHAR]      = "حرف ممنوع: التطويل والتشكيل وأشكال العرض غير مسموحة",
    [D_UNEXPECTED_CHAR]     = "حرف غير متوقع",
    [D_ASCII_COMMA]         = "استخدم الفاصلة العربية بين المعاملات",
    [D_AR_SEMICOLON]        = "الفاصلة المنقوطة العربية محجوزة",
    [D_NUM_NO_DIGITS]       = "علامة الأساس بلا أرقام",
    [D_NUM_BAD_DIGIT]       = "رقم أكبر من الأساس",
    [D_NUM_SEPARATOR]       = "فاصل الآلاف في غير موضعه",
    [D_NUM_OVERFLOW]        = "الرقم كبير جدا",
    [D_LETTER_AFTER_NUMBER] = "حرف ملتصق بالرقم",
    [D_UNTERMINATED_STRING] = "سلسلة غير منتهية",
    [D_BAD_ESCAPE]          = "تسلسل هروب غير معروف",
    [D_BAD_CHAR_LITERAL]    = "الحرف المفرد يجب أن يكون حرفا واحدا",
    [D_UNKNOWN_INSN]        = "تعليمة أو توجيه غير معروف:",
    [D_EXPECTED_REGISTER]   = "متوقع سجل",
    [D_REGISTER_RANGE]      = "سجل غير موجود:",
    [D_EXPECTED_COMMA]      = "متوقع فاصلة",
    [D_EXTRA_TOKENS]        = "رموز زائدة في نهاية السطر",
    [D_EXPECTED_EXPR]       = "متوقع تعبير",
    [D_UNCLOSED_BRACKET]    = "قوس غير مغلق",
    [D_BRACKET_MISMATCH]    = "قوسان غير متطابقين",
    [D_NOT_RELOCATABLE]     = "تعبير لا يمكن نقله",
    [D_NOT_CONSTANT]        = "متوقع قيمة ثابتة",
    [D_DIV_ZERO]            = "قسمة على صفر",
    [D_VALUE_RANGE]         = "القيمة خارج النطاق",
    [D_NEEDS_LO]            = "الرمز يحتاج إلى أدنى: هنا",
    [D_NEEDS_HI]            = "الرمز يحتاج إلى عالي: هنا",
    [D_RELOC_NOT_ALLOWED]   = "عامل النقل غير مسموح هنا",
    [D_UNDEFINED_SYMBOL]    = "رمز غير معرف:",
    [D_REDEFINED_SYMBOL]    = "رمز معرف مسبقا:",
    [D_RESERVED_NAME]       = "اسم محجوز:",
    [D_LOCAL_LABEL_MISSING] = "تسمية محلية غير موجودة",
    [D_LOCAL_LABEL_BASE]    = "التسمية المحلية يجب أن تكون رقما عشريا",
    [D_BRANCH_TARGET]       = "هدف التفرع يجب أن يكون تسمية في القسم نفسه",
    [D_BRANCH_RANGE]        = "الهدف بعيد جدا",
    [D_MISALIGNED_TARGET]   = "الهدف غير محاذى",
    [D_UNKNOWN_SECTION]     = "قسم غير معروف",
    [D_NOT_IN_BSS]          = "غير مسموح في قسم البيانات الصفرية",
    [D_EXPECTED_STRING]     = "متوقع سلسلة",
    [D_EXPECTED_SYMBOL]     = "متوقع اسم رمز",
    [D_CHAR_TOO_WIDE]       = "الحرف لا يتسع في بايت",
    [D_EXPECTED_PLUS_MINUS] = "متوقع + أو - بعد السجل",
    [D_TOO_MANY_ERRORS]     = "أخطاء كثيرة، توقف",
    [D_LINK_USAGE]          = "الاستخدام: رابط ملف_الخرج ملف_كائن...",
    [D_LINK_BAD_OBJECT]     = "ملف كائن غير صالح:",
    [D_LINK_UNSUPPORTED_SECTION] = "قسم غير مدعوم:",
    [D_LINK_UNSUPPORTED_RELOC]   = "نوع نقل غير مدعوم في:",
    [D_LINK_UNDEFINED]      = "مرجع إلى رمز غير معرف:",
    [D_LINK_DUPLICATE]      = "رمز معرف أكثر من مرة:",
    [D_LINK_NO_ENTRY]       = "نقطة البداية غير معرفة: _بداية",
    [D_LINK_RANGE]          = "القيمة لا تتسع في التعليمة، الرمز:",
    [D_LINK_PCREL_LO]       = "أدنى_نسبي بلا عالي_نسبي مطابق في:",
    [D_LINK_ALIGN]          = "محاذاة كبيرة جدا في:",
};

static const char *diag_file = "";
static int diag_errors = 0;

void diag_set_file(const char *path)
{
    diag_file = path;
}

int diag_count(void)
{
    return diag_errors;
}

typedef struct {
    uint32_t cp[DIAG_LINE_MAX];
    size_t n;
} Line;

static void line_cp(Line *l, uint32_t cp)
{
    if (l->n < DIAG_LINE_MAX) {
        l->cp[l->n++] = cp;
    }
}

static void line_utf8(Line *l, const uint8_t *s, size_t len)
{
    size_t pos = 0;
    while (pos < len) {
        uint32_t cp;
        if (utf8_decode(s, len, &pos, &cp) != 0) {
            cp = '?';
            pos++;
        }
        line_cp(l, cp);
    }
}

static void line_str(Line *l, const char *s)
{
    line_utf8(l, (const uint8_t *)s, strlen(s));
}

static void line_num(Line *l, uint64_t v)
{
    uint32_t digits[24];
    size_t n = num_format_dec(v, digits);
    size_t i;
    for (i = 0; i < n; i++) {
        line_cp(l, digits[i]);
    }
}

static int is_latin_run_char(uint32_t cp)
{
    return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
           (cp >= '0' && cp <= '9') || cp == '.' || cp == '/' ||
           cp == '_' || cp == '-';
}

static void reverse(uint32_t *a, size_t n)
{
    size_t i;
    for (i = 0; i < n / 2; i++) {
        uint32_t t = a[i];
        a[i] = a[n - 1 - i];
        a[n - 1 - i] = t;
    }
}

static void line_emit(Line *l)
{
    const char *order = rt_getenv("DIAG_ORDER");
    uint8_t out[DIAG_LINE_MAX * 4 + 1];
    size_t len = 0;
    size_t i;

    if (order == NULL || strcmp(order, "file") != 0) {
        reverse(l->cp, l->n);
        i = 0;
        while (i < l->n) {
            size_t j = i;
            while (j < l->n && is_latin_run_char(l->cp[j])) {
                j++;
            }
            if (j > i) {
                reverse(l->cp + i, j - i);
                i = j;
            } else {
                i++;
            }
        }
    }
    for (i = 0; i < l->n; i++) {
        len += utf8_encode(l->cp[i], out + len);
    }
    out[len++] = '\n';
    rt_write(RT_STDERR, out, len);
}

void diag_error(uint32_t line, uint32_t col, DiagId id,
                const uint8_t *extra, size_t extra_len)
{
    static Line l;

    diag_errors++;
    if (diag_errors > DIAG_MAX_ERRORS) {
        if (diag_errors == DIAG_MAX_ERRORS + 1) {
            diag_plain(D_TOO_MANY_ERRORS, NULL);
        }
        return;
    }
    l.n = 0;
    line_str(&l, diag_file);
    line_cp(&l, ':');
    line_num(&l, line);
    line_cp(&l, ':');
    line_num(&l, col);
    line_str(&l, ": خطأ: ");
    line_str(&l, diag_text[id]);
    if (extra != NULL) {
        line_cp(&l, ' ');
        line_utf8(&l, extra, extra_len);
    }
    line_emit(&l);
}

void diag_plain2(DiagId id, const char *extra, const char *extra2)
{
    static Line l;

    diag_errors++;
    if (diag_errors > DIAG_MAX_ERRORS) {
        return;
    }
    l.n = 0;
    line_str(&l, "خطأ: ");
    line_str(&l, diag_text[id]);
    if (extra != NULL) {
        line_cp(&l, ' ');
        line_str(&l, extra);
    }
    if (extra2 != NULL) {
        line_str(&l, ": ");
        line_str(&l, extra2);
    }
    line_emit(&l);
}

void diag_plain(DiagId id, const char *extra)
{
    static Line l;

    l.n = 0;
    line_str(&l, "خطأ: ");
    line_str(&l, diag_text[id]);
    if (extra != NULL) {
        line_cp(&l, ' ');
        line_str(&l, extra);
    }
    line_emit(&l);
}
