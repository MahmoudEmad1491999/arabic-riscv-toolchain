/* main.c - عارض: prints the text form of a .كائن object for people.
 * Usage: عارض ملف.كائن   (read-only: no tool reads the text back) */
#include "../common/buf.h"
#include "../common/diag.h"
#include "../common/obj.h"
#include "../common/rt.h"

int main(int argc, char **argv)
{
    RtBuf file;
    ObjView v;
    Buf out;

    if (argc != 2) {
        diag_plain(D_OBJ_USAGE, NULL);
        return 2;
    }
    if (rt_read_file(argv[1], &file) != 0) {
        diag_plain(D_CANNOT_READ, argv[1]);
        return 2;
    }
    if (obj_parse(file.data, file.len, &v) != 0) {
        diag_plain2(D_LINK_BAD_OBJECT, argv[1], NULL);
        rt_free(file.data);
        return 1;
    }
    buf_init(&out);
    obj_dump(&v, &out);
    rt_write(1, out.data, out.len);
    buf_free(&out);
    rt_free(file.data);
    return 0;
}
