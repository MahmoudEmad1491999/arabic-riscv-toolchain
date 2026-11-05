/* main.c - مهيئ: creates a disk image of our file system (docs/spec-fs.md).
 * Usage: مهيئ قرص.صورة [ملف...]
 * A 4 MiB image: superblock, 64 directory entries, then each file in
 * contiguous blocks, named after the last part of its path. */
#include <string.h>

#include "../common/buf.h"
#include "../common/diag.h"
#include "../common/rt.h"

#define BLOCK       512u
#define BLOCKS      8192u
#define DIR_BLOCKS  8u
#define ENTRY       64u
#define ENTRIES     (DIR_BLOCKS * BLOCK / ENTRY)
#define NAME_MAX    47u
#define FIRST_DATA  (1u + DIR_BLOCKS)

static const uint8_t MAGIC[8] = {0xD8, 0xAF, 0xD9, 0x84, 0xD9, 0x8A, 0xD9, 0x84}; /* دليل */

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int main(int argc, char **argv)
{
    uint8_t *img;
    uint32_t next = FIRST_DATA;
    int i, j, rc = 0;

    if (argc < 2) {
        diag_plain(D_FS_USAGE, NULL);
        return 2;
    }
    if (argc - 2 > (int)ENTRIES) {
        diag_plain(D_FS_TOO_MANY, NULL);
        return 1;
    }
    img = rt_alloc((size_t)BLOCKS * BLOCK);
    memset(img, 0, (size_t)BLOCKS * BLOCK);
    memcpy(img, MAGIC, 8);
    put32(img + 8, 1);
    put32(img + 12, BLOCK);
    put32(img + 16, BLOCKS);
    put32(img + 20, DIR_BLOCKS);

    for (i = 2; i < argc && rc == 0; i++) {
        const char *path = argv[i];
        const char *base = strrchr(path, '/');
        size_t name_len;
        uint8_t *e = img + BLOCK + (size_t)(i - 2) * ENTRY;
        RtBuf file;
        uint32_t blocks;

        base = base ? base + 1 : path;
        name_len = strlen(base);
        if (name_len == 0 || name_len > NAME_MAX) {
            diag_plain(D_FS_BAD_NAME, path);
            rc = 1;
            break;
        }
        for (j = 2; j < i; j++) {
            const char *other = strrchr(argv[j], '/');
            other = other ? other + 1 : argv[j];
            if (strcmp(other, base) == 0) {
                diag_plain(D_FS_DUPLICATE, base);
                rc = 1;
            }
        }
        if (rc != 0) {
            break;
        }
        if (rt_read_file(path, &file) != 0) {
            diag_plain(D_CANNOT_READ, path);
            rc = 2;
            break;
        }
        blocks = (uint32_t)((file.len + BLOCK - 1) / BLOCK);
        if (file.len > 0xFFFFFFFFu || (uint64_t)next + blocks > BLOCKS) {
            diag_plain(D_FS_FULL, path);
            rt_free(file.data);
            rc = 1;
            break;
        }
        e[0] = (uint8_t)name_len;
        memcpy(e + 1, base, name_len);
        put32(e + 48, next);
        put32(e + 52, (uint32_t)file.len);
        if (file.len > 0) {
            memcpy(img + (size_t)next * BLOCK, file.data, file.len);
        }
        next += blocks;
        rt_free(file.data);
    }
    put32(img + 24, next);
    if (rc == 0 && rt_write_file(argv[1], img, (size_t)BLOCKS * BLOCK) != 0) {
        diag_plain(D_CANNOT_WRITE, argv[1]);
        rc = 2;
    }
    rt_free(img);
    return rc;
}
