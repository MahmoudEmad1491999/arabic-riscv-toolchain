/* main.c - محزم: bundles .كائن objects into a library (.مكتبة, decision 38).
 * Usage: محزم مكتبة.مكتبة أ.كائن ب.كائن ...
 * Every member must be one of our objects; رابط later takes only the
 * members that define a needed name. */
#include <string.h>

#include "../common/buf.h"
#include "../common/diag.h"
#include "../common/obj.h"
#include "../common/rt.h"

int main(int argc, char **argv)
{
    size_t n, i;
    RtBuf *files;
    const uint8_t **names, **datas;
    size_t *name_lens, *sizes;
    Buf out;
    int rc = 0;

    if (argc < 3) {
        diag_plain(D_LIB_USAGE, NULL);
        return 2;
    }
    n = (size_t)argc - 2;
    files = rt_alloc(n * sizeof *files);
    names = rt_alloc(n * sizeof *names);
    datas = rt_alloc(n * sizeof *datas);
    name_lens = rt_alloc(n * sizeof *name_lens);
    sizes = rt_alloc(n * sizeof *sizes);
    for (i = 0; i < n; i++) {
        const char *path = argv[i + 2];
        const char *base = strrchr(path, '/');
        ObjView v;
        files[i].data = NULL;
        if (rt_read_file(path, &files[i]) != 0) {
            diag_plain(D_CANNOT_READ, path);
            rc = 2;
            continue;
        }
        if (obj_parse(files[i].data, files[i].len, &v) != 0) {
            diag_plain2(D_LINK_BAD_OBJECT, path, NULL);
            rc = 1;
            continue;
        }
        base = base ? base + 1 : path;
        names[i] = (const uint8_t *)base;
        name_lens[i] = strlen(base);
        datas[i] = files[i].data;
        sizes[i] = files[i].len;
    }
    if (rc == 0) {
        buf_init(&out);
        obj_lib_serialize(n, names, name_lens, datas, sizes, &out);
        if (rt_write_file(argv[1], out.data, out.len) != 0) {
            diag_plain(D_CANNOT_WRITE, argv[1]);
            rc = 2;
        }
        buf_free(&out);
    }
    for (i = 0; i < n; i++) {
        rt_free(files[i].data);
    }
    rt_free(files);
    rt_free((void *)names);
    rt_free((void *)datas);
    rt_free(name_lens);
    rt_free(sizes);
    return rc;
}
