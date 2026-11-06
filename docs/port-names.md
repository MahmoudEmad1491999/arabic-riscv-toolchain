# Port glossary: C names and their غرة names (Phase 6)

The port is a faithful translation (decision 57): every C module has a غرة module, and every function, type and constant keeps its job under an Arabic name (decision 4). This table is how to find one from the other. The layout is assumption A82.

## Layout

| C | غرة | Module name |
|---|---|---|
| `src/common/rt.c` | `lib/غرة/منصة.غرة` | `غرة.منصة` (docs/spec-platform.md) |
| `src/common/utf8.c` | `مصدر/مشترك/ترميز.غرة` | `مشترك.ترميز` |
| `src/common/chars.c` | `مصدر/مشترك/حروف.غرة` | `مشترك.حروف` |
| `src/common/number.c` | `مصدر/مشترك/أعداد.غرة` | `مشترك.أعداد` |
| `src/common/buf.c` | `مصدر/مشترك/مخزن.غرة` | `مشترك.مخزن` |
| `src/common/diag.c` | `مصدر/مشترك/تشخيص.غرة` | `مشترك.تشخيص` |
| `src/common/elf.h` | `مصدر/مشترك/إلف.غرة` | `مشترك.إلف` |
| `src/common/obj.c` | `مصدر/مشترك/كائن.غرة` | `مشترك.كائن` |
| `src/obj/main.c` | `مصدر/عارض.غرة` | the program `عارض` |
| `src/lib/main.c` | `مصدر/محزم.غرة` | the program `محزم` |

A tool's main file sits in `مصدر/` and is named after the tool, so `مصدر/` is its root and `مشترك.…` resolves there: `مترجم مصدر/عارض.غرة lib` builds `مصدر/بناء/عارض`.

## Conventions

- **Status codes:** a C function returning `0` / `-1` returns `منطقي`: `صح` for success.
- **Pointer + length** pairs become slices; `const` data becomes `][ثابت …`.
- **`NULL` for "nothing extra"** becomes separate functions (e.g. `diag_plain(id, NULL)` → `اطبع_رسالة)رقم(`, `diag_plain(id, s)` → `اطبع_رسالة_مع)رقم، س(`).
- **Memory:** `rt_alloc` / `rt_realloc` / `rt_free` and `Buf` take their memory from an arena (decision 59); a `Buf` knows its arena, and `buf_free` only empties it.
- **Out-parameters** stay out-parameters (`^نوع`), so call sites read the same.
- **Comparisons** are written in file order like every غرة file: `أ > ب` is "less than".
- C `int` flags become `منطقي`; widths follow the C types (`uint32_t` → `طبيعي٢٣`, `size_t` → `حجم`).

## `src/common/utf8` → `مشترك.ترميز`

| C | غرة |
|---|---|
| `utf8_decode` | `فك` |
| `utf8_encode` | `اكتب` |
| `utf8_decode_all` | `فك_الكل` |

## `src/common/chars` → `مشترك.حروف`

| C | غرة |
|---|---|
| `CP_AR_ZERO`, `CP_AR_COMMA`, `CP_AR_SEMICOLON`, `CP_AR_THOUSANDS`, `CP_TATWEEL`, `CP_SEEN`, `CP_THEH`, `CP_QAF`, `CP_MEEM` | `صفر_عربي`، `فاصلة_عربية`، `فاصلة_منقوطة`، `فاصل_الآلاف`، `تطويل`، `سين`، `ثاء`، `قاف`، `ميم` |
| `ch_digit_value` | `قيمة_الرقم` |
| `ch_hex_letter_value` | `قيمة_حرف_ست_عشري` |
| `ch_is_letter` | `حرف_عربي` |
| `ch_is_forbidden` | `ممنوع` |
| `ch_is_ascii_digit` | `رقم_لاتيني` |
| `ch_is_persian_digit` | `رقم_فارسي` |

## `src/common/number` → `مشترك.أعداد`

| C | غرة |
|---|---|
| `NumError`: `NUM_OK`, `NUM_ERR_NOT_NUMBER`, `NUM_ERR_NO_DIGITS`, `NUM_ERR_BAD_DIGIT`, `NUM_ERR_SEPARATOR`, `NUM_ERR_OVERFLOW` | `خطأ_عدد`: `سليم`، `ليس_عددا`، `بلا_أرقام`، `رقم_كبير`، `فاصل_خاطئ`، `فيض` |
| `NumResult`: `value`, `base`, `end`, `err`, `err_pos` | `نتيجة`: `القيمة`، `الأساس`، `النهاية`، `الخطأ`، `موضع_الخطأ` |
| `num_parse` | `اقرأ` |
| `num_format_dec` / `num_format_hex` | `اكتب_عشريا` / `اكتب_ست_عشري` |

## `src/common/buf` → `مشترك.مخزن`

| C | غرة |
|---|---|
| `Buf` (`data`, `len`, `cap`) | `مخزن` (`البيانات` with the capacity as its length, `الطول`, `الساحة`) |
| `buf_init` | `جديد)ساحة(` |
| `buf_free` | `حرر` |
| `b.data[0 .. b.len)` | `محتوى` |
| `buf_reserve` | `احجز` |
| `buf_bytes` | `أضف` |
| `buf_u8`, `buf_u16`, `buf_u32`, `buf_u64` | `أضف_بايت`، `أضف_نصف_كلمة`، `أضف_كلمة`، `أضف_كلمة_مزدوجة` |
| `buf_zeros` | `أضف_أصفارا` |
| `buf_align` | `حاذ` |
| `buf_put_u32_at` | `ضع_كلمة_في` |

## `src/common/diag` → `مشترك.تشخيص`

| C | غرة |
|---|---|
| `diag_set_file` | `اضبط_الملف` |
| `diag_count` | `عدد_الأخطاء` |
| `diag_error(…, NULL, 0)` / `diag_error(…, extra, len)` | `أبلغ` / `أبلغ_مع` |
| `diag_plain(id, NULL)` / `diag_plain(id, extra)` | `اطبع_رسالة` / `اطبع_رسالة_مع` |
| `diag_plain2(id, NULL, NULL)` / `(id, a, NULL)` / `(id, a, b)` | `أبلغ_بلا_موضع` / `أبلغ_بلا_موضع_مع` / `أبلغ_بلا_موضع_مع_تفصيل` |
| `DiagId` | `رسالة` |

Message ids:

| C | غرة |
|---|---|
| `D_CANNOT_READ` | `رسالة.تعذرت_القراءة` |
| `D_CANNOT_WRITE` | `رسالة.تعذرت_الكتابة` |
| `D_BAD_UTF8` | `رسالة.ترميز_غير_صالح` |
| `D_USAGE` | `رسالة.استخدام_المجمع` |
| `D_ASCII_DIGIT` | `رسالة.رقم_لاتيني` |
| `D_PERSIAN_DIGIT` | `رسالة.رقم_فارسي` |
| `D_LATIN_LETTER` | `رسالة.حرف_لاتيني` |
| `D_FORBIDDEN_CHAR` | `رسالة.حرف_ممنوع` |
| `D_UNEXPECTED_CHAR` | `رسالة.حرف_غير_متوقع` |
| `D_ASCII_COMMA` | `رسالة.فاصلة_لاتينية` |
| `D_AR_SEMICOLON` | `رسالة.فاصلة_منقوطة` |
| `D_NUM_NO_DIGITS` | `رسالة.أساس_بلا_أرقام` |
| `D_NUM_BAD_DIGIT` | `رسالة.رقم_أكبر_من_الأساس` |
| `D_NUM_SEPARATOR` | `رسالة.فاصل_في_غير_موضعه` |
| `D_NUM_OVERFLOW` | `رسالة.عدد_كبير_جدا` |
| `D_LETTER_AFTER_NUMBER` | `رسالة.حرف_ملتصق_بعدد` |
| `D_UNTERMINATED_STRING` | `رسالة.سلسلة_غير_منتهية` |
| `D_BAD_ESCAPE` | `رسالة.هروب_غير_معروف` |
| `D_BAD_CHAR_LITERAL` | `رسالة.حرف_مفرد_غير_صالح` |
| `D_UNKNOWN_INSN` | `رسالة.تعليمة_غير_معروفة` |
| `D_EXPECTED_REGISTER` | `رسالة.متوقع_سجل` |
| `D_REGISTER_RANGE` | `رسالة.سجل_غير_موجود` |
| `D_EXPECTED_COMMA` | `رسالة.متوقع_فاصلة` |
| `D_EXTRA_TOKENS` | `رسالة.رموز_زائدة` |
| `D_EXPECTED_EXPR` | `رسالة.متوقع_تعبير` |
| `D_UNCLOSED_BRACKET` | `رسالة.قوس_غير_مغلق` |
| `D_BRACKET_MISMATCH` | `رسالة.قوسان_غير_متطابقين` |
| `D_NOT_RELOCATABLE` | `رسالة.تعبير_لا_ينقل` |
| `D_NOT_CONSTANT` | `رسالة.متوقع_ثابت` |
| `D_DIV_ZERO` | `رسالة.قسمة_على_صفر` |
| `D_VALUE_RANGE` | `رسالة.قيمة_خارج_النطاق` |
| `D_NEEDS_LO` | `رسالة.يحتاج_أدنى` |
| `D_NEEDS_HI` | `رسالة.يحتاج_عاليا` |
| `D_RELOC_NOT_ALLOWED` | `رسالة.نقل_غير_مسموح` |
| `D_UNDEFINED_SYMBOL` | `رسالة.رمز_غير_معرف` |
| `D_REDEFINED_SYMBOL` | `رسالة.رمز_معرف_مسبقا` |
| `D_RESERVED_NAME` | `رسالة.اسم_محجوز` |
| `D_LOCAL_LABEL_MISSING` | `رسالة.تسمية_محلية_مفقودة` |
| `D_LOCAL_LABEL_BASE` | `رسالة.تسمية_محلية_غير_عشرية` |
| `D_BRANCH_TARGET` | `رسالة.هدف_تفرع_غير_صالح` |
| `D_BRANCH_RANGE` | `رسالة.هدف_بعيد` |
| `D_MISALIGNED_TARGET` | `رسالة.هدف_غير_محاذى` |
| `D_UNKNOWN_SECTION` | `رسالة.قسم_غير_معروف` |
| `D_NOT_IN_BSS` | `رسالة.غير_مسموح_في_الأصفار` |
| `D_EXPECTED_STRING` | `رسالة.متوقع_سلسلة` |
| `D_EXPECTED_SYMBOL` | `رسالة.متوقع_رمز` |
| `D_CHAR_TOO_WIDE` | `رسالة.حرف_أعرض_من_بايت` |
| `D_EXPECTED_PLUS_MINUS` | `رسالة.متوقع_زائد_أو_ناقص` |
| `D_TOO_MANY_ERRORS` | `رسالة.أخطاء_كثيرة` |
| `D_LINK_USAGE` | `رسالة.استخدام_الرابط` |
| `D_LINK_BAD_OBJECT` | `رسالة.كائن_غير_صالح` |
| `D_OBJ_PCREL_LABEL` | `رسالة.وسم_نسبي_في_قسم_آخر` |
| `D_OBJ_USAGE` | `رسالة.استخدام_العارض` |
| `D_LINK_STALE` | `رسالة.واجهة_قديمة` |
| `D_LINK_DUP_MODULE` | `رسالة.وحدة_مربوطة_مرتين` |
| `D_CC_IFACE` | `رسالة.واجهة_تتعذر_كتابتها` |
| `D_LIB_USAGE` | `رسالة.استخدام_المحزم` |
| `D_LINK_BARE_ENTRY` | `رسالة.البداية_ليست_أولا` |
| `D_LINK_IMPORT_RELOC` | `رسالة.نقل_استيراد_غير_مسموح` |
| `D_EXPECTED_CSR` | `رسالة.متوقع_سجل_تحكم` |
| `D_INCLUDE_FAILED` | `رسالة.تعذر_التضمين` |
| `D_FS_USAGE` | `رسالة.استخدام_المهيئ` |
| `D_FS_TOO_MANY` | `رسالة.دليل_ممتلئ` |
| `D_FS_BAD_NAME` | `رسالة.اسم_غير_صالح_في_المسار` |
| `D_FS_DUPLICATE` | `رسالة.اسم_ملف_مكرر` |
| `D_FS_FULL` | `رسالة.قرص_ممتلئ` |
| `D_FS_NOT_DIR` | `رسالة.ليس_دليلا` |
| `D_CC_SLICE_FROM` | `رسالة.شريحة_من_بلا_مؤشر` |
| `D_LINK_UNSUPPORTED_SECTION` | `رسالة.قسم_غير_مدعوم` |
| `D_LINK_UNSUPPORTED_RELOC` | `رسالة.نقل_غير_مدعوم` |
| `D_LINK_UNDEFINED` | `رسالة.مرجع_غير_معرف` |
| `D_LINK_DUPLICATE` | `رسالة.رمز_معرف_مرتين` |
| `D_LINK_NO_ENTRY` | `رسالة.لا_بداية` |
| `D_LINK_RANGE` | `رسالة.قيمة_لا_تتسع_في_التعليمة` |
| `D_LINK_PCREL_LO` | `رسالة.أدنى_نسبي_بلا_عال` |
| `D_LINK_ALIGN` | `رسالة.محاذاة_كبيرة` |
| `D_CC_USAGE` | `رسالة.استخدام_المترجم` |
| `D_CC_EXPECTED` | `رسالة.متوقع` |
| `D_CC_UNEXPECTED` | `رسالة.رمز_لغة_غير_متوقع` |
| `D_CC_UNKNOWN_NAME` | `رسالة.اسم_غير_معرف` |
| `D_CC_REDEFINED` | `رسالة.اسم_معرف_مسبقا` |
| `D_CC_UNKNOWN_TYPE` | `رسالة.نوع_غير_معرف` |
| `D_CC_TYPE_MISMATCH` | `رسالة.نوع_غير_متوافق` |
| `D_CC_NOT_FIT` | `رسالة.قيمة_لا_تتسع` |
| `D_CC_MIXED_TYPES` | `رسالة.نوعان_مختلفان` |
| `D_CC_NEED_BOOL` | `رسالة.متوقع_منطقي` |
| `D_CC_NEED_INT` | `رسالة.متوقع_عدد_صحيح` |
| `D_CC_NOT_LVALUE` | `رسالة.لا_يسند_إليه` |
| `D_CC_READONLY` | `رسالة.للقراءة_فقط` |
| `D_CC_NOT_CONST` | `رسالة.متوقع_ثابت_وقت_الترجمة` |
| `D_CC_CONST_TOO_BIG` | `رسالة.ثابت_كبير_جدا` |
| `D_CC_ARGS` | `رسالة.عدد_معاملات_خاطئ` |
| `D_CC_NOT_CALLABLE` | `رسالة.ليست_دالة` |
| `D_CC_NO_RETURN` | `رسالة.رجوع_مفقود` |
| `D_CC_RETURN` | `رسالة.رجوع_غير_متوافق` |
| `D_CC_BREAK_OUTSIDE` | `رسالة.قطع_خارج_حلقة` |
| `D_CC_PADDING` | `رسالة.حشو_مخفي` |
| `D_CC_RECURSIVE_TYPE` | `رسالة.نوع_يحتوي_نفسه` |
| `D_CC_CYCLE` | `رسالة.اعتماد_دائري` |
| `D_CC_IMPORT_CYCLE` | `رسالة.استيراد_دائري` |
| `D_CC_MODULE_NOT_FOUND` | `رسالة.وحدة_غير_موجودة` |
| `D_CC_PRIVATE` | `رسالة.اسم_غير_عام` |
| `D_CC_NO_FIELD` | `رسالة.حقل_غير_موجود` |
| `D_CC_FIELD_MISSING` | `رسالة.حقل_مفقود` |
| `D_CC_FIELD_DUP` | `رسالة.حقل_مكرر` |
| `D_CC_COUNT` | `رسالة.عدد_عناصر_خاطئ` |
| `D_CC_INDEX_RANGE` | `رسالة.فهرس_ثابت_خارج_الحدود` |
| `D_CC_NOT_INDEXABLE` | `رسالة.لا_يفهرس` |
| `D_CC_NO_LENGTH` | `رسالة.بلا_طول` |
| `D_CC_UNINIT_READ` | `رسالة.قراءة_قبل_إسناد` |
| `D_CC_SWITCH_MISSING` | `رسالة.حالة_مفقودة` |
| `D_CC_SWITCH_DUP` | `رسالة.حالة_مكررة` |
| `D_CC_SWITCH_DEFAULT` | `رسالة.حسب_بلا_وإلا` |
| `D_CC_BAD_CONVERSION` | `رسالة.تحويل_غير_مسموح` |
| `D_CC_NO_EQUALITY` | `رسالة.لا_يقارن` |
| `D_CC_NEED_BRACKETS` | `رسالة.أقواس_مطلوبة` |
| `D_CC_COMPARE_CHAIN` | `رسالة.مقارنات_متسلسلة` |
| `D_CC_TOO_MANY_ARGS` | `رسالة.معاملات_كثيرة` |
| `D_CC_NOT_SUPPORTED` | `رسالة.غير_مدعوم` |
| `D_CC_NO_MAIN` | `رسالة.رئيسية_مفقودة` |
| `D_CC_SHIFT_RANGE` | `رسالة.إزاحة_خارج_النطاق` |
| `D_CC_OPT_DEREF` | `رسالة.افتح_الاختياري` |
| `D_CC_DUP_ENUM` | `رسالة.قيمة_تعداد_مكررة` |
| `D_CC_ARRAY_SIZE` | `رسالة.طول_مصفوفة_غير_صالح` |
| `D_CC_GLOBAL_INIT` | `رسالة.قيمة_عامة_غير_ثابتة` |
| `D_CC_VOID_VALUE` | `رسالة.دالة_بلا_قيمة` |
| `D_CC_UNINIT_GLOBAL` | `رسالة.عام_غير_مهيأ` |
| `D_CC_ASSIGN_LOOP` | `رسالة.إسناد_إلى_متغير_الحلقة` |
| `D_CC_NOT_TYPE_HERE` | `رسالة.متوقع_نوع` |
| `D_CC_STRUCT_LIT_HERE` | `رسالة.بنية_بين_قوسين` |
| `D_CC_BAD_CHAR` | `رسالة.حرف_غير_صالح` |
| `D_CC_BAD_MODULE_NAME` | `رسالة.اسم_وحدة_غير_صالح` |
| `D_CC_PACKED_ADDR` | `رسالة.عنوان_في_مضغوطة` |
| `D_CC_VOLATILE_TYPE` | `رسالة.نوع_متطاير_غير_صالح` |
| `D_CC_VOLATILE_PLACE` | `رسالة.مكان_متطاير_غير_صالح` |

## `src/common/elf.h` → `مشترك.إلف`

| C | غرة |
|---|---|
| `ELF_EHDR_SIZE`, `ELF_SHDR_SIZE`, `ELF_SYM_SIZE`, `ELF_RELA_SIZE` | `حجم_رأس_الملف`، `حجم_رأس_القسم`، `حجم_الرمز`، `حجم_النقل` |
| `ET_REL`, `ET_EXEC`, `EM_RISCV` | `نوع_قابل_للنقل`، `نوع_تنفيذي`، `آلة_ريسك_في` |
| `SHT_NULL`, `SHT_PROGBITS`, `SHT_SYMTAB`, `SHT_STRTAB`, `SHT_RELA`, `SHT_NOBITS` | `قسم_فارغ`، `قسم_بيانات_البرنامج`، `قسم_جدول_الرموز`، `قسم_جدول_النصوص`، `قسم_النقل`، `قسم_بلا_بيانات` |
| `SHF_WRITE`, `SHF_ALLOC`, `SHF_EXECINSTR`, `SHF_INFO_LINK` | `علم_كتابة`، `علم_تحميل`، `علم_تنفيذ`، `علم_ربط_المعلومات` |
| `SHN_UNDEF`, `SHN_ABS` | `فهرس_غير_معرف`، `فهرس_مطلق` |
| `STB_LOCAL`, `STB_GLOBAL`, `STT_NOTYPE`, `STT_SECTION` | `ربط_محلي`، `ربط_عام`، `نوع_بلا_نوع`، `نوع_قسم` |
| `R_RISCV_32`, `R_RISCV_64`, `R_RISCV_BRANCH`, `R_RISCV_JAL`, `R_RISCV_CALL_PLT` | `نقل_كلمة`، `نقل_كلمة_مزدوجة`، `نقل_فرع`، `نقل_قفز`، `نقل_نداء` |
| `R_RISCV_PCREL_HI20`, `R_RISCV_PCREL_LO12_I`, `R_RISCV_PCREL_LO12_S` | `نقل_نسبي_عال`، `نقل_نسبي_منخفض_ي`، `نقل_نسبي_منخفض_خ` |
| `R_RISCV_HI20`, `R_RISCV_LO12_I`, `R_RISCV_LO12_S` | `نقل_عال`، `نقل_منخفض_ي`، `نقل_منخفض_خ` |

## `src/common/obj` → `مشترك.كائن`

| C | غرة |
|---|---|
| `OBJ_VERSION`, `OBJ_HEADER_SIZE`, `OBJ_SYM_SIZE`, `OBJ_RELOC_SIZE`, `OBJ_NSEC`, `OBJ_MAX_ALIGN`, `OBJ_NO_SYMBOL` | `الإصدار`، `حجم_الرأس`، `حجم_الرمز`، `حجم_النقل`، `عدد_الأقسام`، `أقصى_محاذاة`، `بلا_رمز` |
| `OBJ_FLAG_EXECUTABLE`, `OBJ_PROGRAM_BASE`, `OBJ_SYSTEM_PREFIX` | `علم_تنفيذي`، `أساس_البرنامج`، `بادئة_النظام` |
| `OBJ_SEC_TEXT`, `_DATA`, `_RODATA`, `_BSS`, `_ABS`, `_UNDEF` | `قسم_النص`، `قسم_البيانات`، `قسم_الثوابت`، `قسم_الأصفار`، `قسم_مطلق`، `قسم_غير_معرف` |
| `OBJ_PRIVATE`, `OBJ_EXPORTED`, `OBJ_IMPORTED` | `رمز_خاص`، `رمز_مصدر`، `رمز_مستورد` |
| `OBJ_SHAPE_UNKNOWN`, `_FUNCTION`, `_DATA` | `شكل_غير_معروف`، `شكل_دالة`، `شكل_بيانات` |
| `OBJ_REL_BRANCH` … `OBJ_REL_DWORD`, `OBJ_REL_LAST` | `نقل_فرع`، `نقل_قفزة`، `نقل_نداء`، `نقل_عال`، `نقل_منخفض_ي`، `نقل_منخفض_خ`، `نقل_نسبي_عال`، `نقل_نسبي_منخفض_ي`، `نقل_نسبي_منخفض_خ`، `نقل_كلمة`، `نقل_كلمة_مزدوجة`، `آخر_نقل` |
| `ObjSym` (`name`+`len`, `kind`, `shape`, `section`, `size`, `value`) | `رمز` (`الاسم`، `النوع`، `الشكل`، `القسم`، `الحجم`، `القيمة`) |
| `ObjReloc` (`offset`, `section`, `kind`, `sym`, `extra`) | `نقل` (`الموضع`، `القسم`، `النوع`، `الرمز`، `الإضافة`) |
| `ObjOut` (`content`, `size`, `align`, `syms`, `relocs`, `flags`, `iface`) | `كائن_للكتابة` (`المحتويات`، `الأحجام`، `المحاذاة`، `الرموز`، `النقول`، `الأعلام`، `الواجهة`) |
| `ObjIface` (`module`, `hash`, `source_hash`, `nimports`, `imports`, `text`) | `واجهة` (`الوحدة`، `البصمة`، `بصمة_المصدر`، `عدد_المستوردات`، `المستوردات`، `النص`) |
| `ObjView` | `منظر` (`الملف`، `الأعلام`، `الأحجام`، `المحاذاة`، `إزاحات_المحتوى`، `عدد_الرموز`، `عدد_النقول`، `إزاحة_الرموز`، `إزاحة_النقول`، `إزاحة_الأسماء`، `حجم_الأسماء`، `إزاحة_الواجهة`، `حجم_الواجهة`) |
| `ObjLib`, `ObjLibMember` | `مكتبة` (`الملف`، `العدد`)، `عضو` (`البيانات`، `الاسم`) |
| `OBJ_LIB_HEADER`, `OBJ_LIB_ENTRY` | `حجم_رأس_المكتبة`، `حجم_مدخل_المكتبة` |
| `obj_fnv1a` | `بصمة` |
| `obj_iface_encode` / `obj_iface_decode` / `obj_iface_import` / `obj_iface` | `رمز_الواجهة` / `فك_الواجهة` / `مستورد_الواجهة` / `واجهة_الكائن` |
| `obj_serialize` | `اكتب_الكائن` |
| `obj_is_object` / `obj_parse` | `كائن_هو` / `افحص` |
| `obj_sym` / `obj_reloc` | `رمز_في` / `نقل_في` |
| `obj_is_lib` / `obj_lib_parse` / `obj_lib_member` / `obj_lib_serialize` | `مكتبة_هي` / `افحص_المكتبة` / `عضو_في` / `اكتب_المكتبة` |
| `obj_dump` | `اعرض` |
| (new, for a zeroed value) | `منظر_فارغ`، `واجهة_فارغة` |
