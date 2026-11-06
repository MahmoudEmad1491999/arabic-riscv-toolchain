/* The C side of the diagnostics comparison (tools/run_port_tests.sh): the
 * same calls as اختبار_التشخيص.غرة, whose standard error must be identical. */
#include <string.h>

#include "../../src/common/diag.h"

int main(void)
{
    int i;
    const char *name = "اسم_build/x.o";
    diag_set_file("tests/ملف.تجميع");
    for (i = 0; i < D_COUNT; i++) {
        if (i % 2 == 0) diag_plain((DiagId)i, NULL);
        else diag_plain((DiagId)i, name);
    }
    for (i = 0; i < 30; i++) {
        if (i % 3 == 0) diag_plain2((DiagId)i, NULL, NULL);
        else if (i % 3 == 1) diag_plain2((DiagId)i, name, NULL);
        else diag_plain2((DiagId)i, name, "path/ملف.كائن");
    }
    for (i = 0; i < D_COUNT; i++) {
        if (i % 2 == 0) diag_error((uint32_t)i + 1, (uint32_t)i * 7 + 3, (DiagId)i, NULL, 0);
        else diag_error((uint32_t)i + 1, 12345, (DiagId)i, (const uint8_t *)name, strlen(name));
    }
    return diag_count() == 30 + D_COUNT ? 0 : 1;
}
