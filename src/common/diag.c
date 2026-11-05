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
    [D_LINK_USAGE]          = "الاستخدام: رابط [--عاري] ملف_الخرج ملف_كائن...",
    [D_LINK_BAD_OBJECT]     = "ملف كائن غير صالح:",
    [D_OBJ_PCREL_LABEL]     = "في صيغة كائن يجب أن يكون وسم النقل النسبي في نفس القسم",
    [D_OBJ_USAGE]           = "الاستخدام: عارض ملف.كائن",
    [D_LINK_STALE]          = "كائن مبني على واجهة قديمة للوحدة:",
    [D_LINK_DUP_MODULE]     = "وحدة مربوطة أكثر من مرة:",
    [D_EXPECTED_CSR]        = "متوقع: سجل تحكم )اسم أو رقم من ٠ إلى ٥٩٠٤(",
    [D_LINK_BARE_ENTRY]     = "في الصورة العارية يجب أن يكون _بداية أول ما في الشفرة: اجعل ملف البدء أول الملفات",
    [D_LIB_USAGE]           = "الاستخدام: محزم مكتبة.مكتبة كائن.كائن...",
    [D_CC_IFACE]            = "لا يمكن كتابة واجهة الوحدة، فهي تستخدم نوعا من وحدة لم تستوردها:",
    [D_LINK_UNSUPPORTED_SECTION] = "قسم غير مدعوم:",
    [D_LINK_UNSUPPORTED_RELOC]   = "نوع نقل غير مدعوم في:",
    [D_LINK_UNDEFINED]      = "مرجع إلى رمز غير معرف:",
    [D_LINK_DUPLICATE]      = "رمز معرف أكثر من مرة:",
    [D_LINK_NO_ENTRY]       = "نقطة البداية غير معرفة: _بداية",
    [D_LINK_RANGE]          = "القيمة لا تتسع في التعليمة، الرمز:",
    [D_LINK_PCREL_LO]       = "أدنى_نسبي بلا عالي_نسبي مطابق في:",
    [D_LINK_ALIGN]          = "محاذاة كبيرة جدا في:",
    [D_CC_USAGE] = "الاستخدام: مترجم ملف.غرة [ملف_الخرج.تجميع] [مسار_بحث أو كائن أو مكتبة...]",
    [D_CC_EXPECTED] = "متوقع:",
    [D_CC_UNEXPECTED] = "رمز غير متوقع",
    [D_CC_UNKNOWN_NAME] = "اسم غير معرف:",
    [D_CC_REDEFINED] = "اسم معرف مسبقا:",
    [D_CC_UNKNOWN_TYPE] = "نوع غير معرف:",
    [D_CC_TYPE_MISMATCH] = "نوع غير متوافق، المتوقع:",
    [D_CC_NOT_FIT] = "القيمة لا تتسع في النوع:",
    [D_CC_MIXED_TYPES] = "نوعا المعاملين مختلفان:",
    [D_CC_NEED_BOOL] = "متوقع قيمة منطقية",
    [D_CC_NEED_INT] = "متوقع عدد صحيح",
    [D_CC_NOT_LVALUE] = "لا يمكن الإسناد إلى هذا التعبير",
    [D_CC_READONLY] = "لا يمكن الكتابة في قيمة للقراءة فقط",
    [D_CC_NOT_CONST] = "متوقع قيمة ثابتة وقت الترجمة",
    [D_CC_CONST_TOO_BIG] = "القيمة الثابتة كبيرة جدا",
    [D_CC_ARGS] = "عدد المعاملات غير صحيح",
    [D_CC_NOT_CALLABLE] = "ليست دالة",
    [D_CC_NO_RETURN] = "الدالة لا ترجع قيمة في كل المسارات:",
    [D_CC_RETURN] = "قيمة الرجوع غير متوافقة مع الدالة",
    [D_CC_BREAK_OUTSIDE] = "اقطع أو تجاوز خارج حلقة",
    [D_CC_PADDING] = "حشو مخفي في البنية:",
    [D_CC_RECURSIVE_TYPE] = "نوع يحتوي نفسه:",
    [D_CC_CYCLE] = "اعتماد دائري في:",
    [D_CC_IMPORT_CYCLE] = "استيراد دائري:",
    [D_CC_MODULE_NOT_FOUND] = "وحدة غير موجودة:",
    [D_CC_PRIVATE] = "اسم غير عام في وحدته:",
    [D_CC_NO_FIELD] = "لا يوجد حقل بهذا الاسم:",
    [D_CC_FIELD_MISSING] = "حقل مفقود في القيمة:",
    [D_CC_FIELD_DUP] = "حقل مكرر:",
    [D_CC_COUNT] = "عدد العناصر لا يساوي طول المصفوفة",
    [D_CC_INDEX_RANGE] = "فهرس ثابت خارج الحدود",
    [D_CC_NOT_INDEXABLE] = "لا يمكن الفهرسة في هذا النوع",
    [D_CC_NO_LENGTH] = "ليس لهذا النوع طول",
    [D_CC_UNINIT_READ] = "قراءة متغير قبل إسناد قيمة له:",
    [D_CC_SWITCH_MISSING] = "حالة مفقودة في حسب:",
    [D_CC_SWITCH_DUP] = "حالة مكررة في حسب",
    [D_CC_SWITCH_DEFAULT] = "حسب على عدد يحتاج وإلا:",
    [D_CC_BAD_CONVERSION] = "تحويل غير مسموح بين هذين النوعين",
    [D_CC_NO_EQUALITY] = "لا يمكن مقارنة هذا النوع",
    [D_CC_NEED_BRACKETS] = "استخدم الأقواس عند خلط هذه العمليات",
    [D_CC_COMPARE_CHAIN] = "لا يمكن تسلسل المقارنات",
    [D_CC_TOO_MANY_ARGS] = "معاملات أكثر من سجلات الوسائط الثمانية: غير مدعوم بعد",
    [D_CC_NOT_SUPPORTED] = "غير مدعوم بعد:",
    [D_CC_NO_MAIN] = "الدالة رئيسية غير موجودة أو نوعها غير صحيح",
    [D_CC_SHIFT_RANGE] = "مقدار الإزاحة خارج النطاق",
    [D_CC_OPT_DEREF] = "افتح المؤشر الاختياري باستعمال إذا وجد",
    [D_CC_DUP_ENUM] = "قيمة مكررة في التعداد:",
    [D_CC_ARRAY_SIZE] = "طول المصفوفة يجب أن يكون ثابتا موجبا",
    [D_CC_GLOBAL_INIT] = "القيمة الأولية للمتغير العام يجب أن تكون ثابتة",
    [D_CC_VOID_VALUE] = "الدالة لا ترجع قيمة",
    [D_CC_UNINIT_GLOBAL] = "غير_مهيأ غير مسموح للمتغيرات العامة",
    [D_CC_ASSIGN_LOOP] = "لا يمكن تغيير متغير الحلقة أو المعامل:",
    [D_CC_NOT_TYPE_HERE] = "متوقع نوع",
    [D_CC_STRUCT_LIT_HERE] = "ضع قيمة البنية بين قوسين في هذا الموضع",
    [D_CC_BAD_CHAR] = "قيمة حرف غير صالحة",
    [D_CC_BAD_MODULE_NAME] = "اسم الملف لا يصلح اسما لوحدة:",
    [D_CC_VOLATILE_TYPE] = "متطاير يكون لعدد أو منطقي أو حرف أو تعداد أو مؤشر أو دالة فقط، بحجم ١ أو ٢ أو ٤ أو ٨ بايت",
    [D_CC_VOLATILE_PLACE] = "متطاير يكون لمتغير عام أو لما يشير إليه مؤشر فقط",
    [D_CC_PACKED_ADDR] = "لا يمكن أخذ عنوان أو شريحة من حقل في بنية مضغوطة، فقد لا يكون محاذى",
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
