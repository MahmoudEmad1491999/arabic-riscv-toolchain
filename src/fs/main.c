/* main.c - مهيئ: creates a disk image of our file system (docs/spec-fs.md).
 * Usage: مهيئ قرص.صورة [ملف | مسار=ملف | دليل/]...
 *   ملف        a host file, put in the root under the last part of its path
 *   مسار=ملف   a host file, put at that path on the disk
 *   دليل/      an empty directory
 * Missing directories on the way are created. A 32 MiB image (version 2):
 * superblock, the root directory (8 blocks, 64 entries), then every file in
 * contiguous blocks and every other directory in 8 blocks of its own. */
#include <string.h>

#include "../common/buf.h"
#include "../common/diag.h"
#include "../common/rt.h"

#define BLOCK       512u
#define BLOCKS      65536u
#define DIR_BLOCKS  8u
#define ENTRY       64u
#define ENTRIES     (DIR_BLOCKS * BLOCK / ENTRY)
#define NAME_MAX    47u
#define ROOT        1u
#define FIRST_DATA  (ROOT + DIR_BLOCKS)
#define E_FIRST     48u
#define E_SIZE      52u
#define E_KIND      56u
#define KIND_FILE   0u
#define KIND_DIR    1u

static const uint8_t MAGIC[8] = {0xD8, 0xAF, 0xD9, 0x84, 0xD9, 0x8A, 0xD9, 0x84}; /* دليل */

static uint8_t *img;
static uint32_t next = FIRST_DATA;

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint8_t *entry(uint32_t dir, unsigned i)
{
    return img + (size_t)dir * BLOCK + (size_t)i * ENTRY;
}

/* The entry named name[0..len) in the directory starting at block dir, or -1. */
static int find(uint32_t dir, const char *name, size_t len)
{
    unsigned i;
    for (i = 0; i < ENTRIES; i++) {
        uint8_t *e = entry(dir, i);
        if (e[0] == len && memcmp(e + 1, name, len) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int valid_name(const char *name, size_t len)
{
    size_t i;
    if (len == 0 || len > NAME_MAX) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (name[i] == 0 || name[i] == '/') {
            return 0;
        }
    }
    return 1;
}

/* Adds an entry; returns 0, or 1 after reporting an error about path. */
static int add(uint32_t dir, const char *name, size_t len, uint32_t first, uint32_t size,
               unsigned kind, const char *path)
{
    unsigned i;
    for (i = 0; i < ENTRIES; i++) {
        uint8_t *e = entry(dir, i);
        if (e[0] == 0) {
            e[0] = (uint8_t)len;
            memcpy(e + 1, name, len);
            put32(e + E_FIRST, first);
            put32(e + E_SIZE, size);
            e[E_KIND] = (uint8_t)kind;
            return 0;
        }
    }
    diag_plain(D_FS_TOO_MANY, path);
    return 1;
}

/* The directory path[0..len) (its first block), created with its parents as
 * needed; 0 after reporting an error about path. */
static uint32_t directory(const char *path, size_t len, const char *whole)
{
    size_t start = len, plen;
    uint32_t parent, first;
    int i;

    if (len == 0) {
        return ROOT;
    }
    while (start > 0 && path[start - 1] != '/') {
        start--;
    }
    plen = start > 0 ? start - 1 : 0;
    parent = directory(path, plen, whole);
    if (parent == 0) {
        return 0;
    }
    if (!valid_name(path + start, len - start)) {
        diag_plain(D_FS_BAD_NAME, whole);
        return 0;
    }
    i = find(parent, path + start, len - start);
    if (i >= 0) {
        uint8_t *e = entry(parent, (unsigned)i);
        if (e[E_KIND] != KIND_DIR) {
            diag_plain(D_FS_NOT_DIR, whole);
            return 0;
        }
        return get32(e + E_FIRST);
    }
    if ((uint64_t)next + DIR_BLOCKS > BLOCKS) {
        diag_plain(D_FS_FULL, whole);
        return 0;
    }
    first = next;
    if (add(parent, path + start, len - start, first, DIR_BLOCKS * BLOCK, KIND_DIR, whole) != 0) {
        return 0;
    }
    next += DIR_BLOCKS;
    return first;
}

/* One argument; returns 0, 1 (bad input) or 2 (cannot read). */
static int put(const char *arg)
{
    const char *eq = strchr(arg, '=');
    const char *disk, *host;
    size_t disk_len, start;
    uint32_t dir, blocks;
    RtBuf file;

    if (eq != NULL) {
        disk = arg;
        disk_len = (size_t)(eq - arg);
        host = eq + 1;
    } else {
        size_t n = strlen(arg);
        if (n > 0 && arg[n - 1] == '/') {
            return directory(arg, n - 1, arg) == 0 ? 1 : 0;
        }
        host = arg;
        disk = strrchr(arg, '/');
        disk = disk ? disk + 1 : arg;
        disk_len = strlen(disk);
    }
    if (disk_len > 0 && disk[0] == '/') {
        disk++;
        disk_len--;
    }
    start = disk_len;
    while (start > 0 && disk[start - 1] != '/') {
        start--;
    }
    dir = directory(disk, start > 0 ? start - 1 : 0, arg);
    if (dir == 0) {
        return 1;
    }
    if (!valid_name(disk + start, disk_len - start)) {
        diag_plain(D_FS_BAD_NAME, arg);
        return 1;
    }
    if (find(dir, disk + start, disk_len - start) >= 0) {
        diag_plain(D_FS_DUPLICATE, arg);
        return 1;
    }
    if (rt_read_file(host, &file) != 0) {
        diag_plain(D_CANNOT_READ, host);
        return 2;
    }
    blocks = (uint32_t)((file.len + BLOCK - 1) / BLOCK);
    if (file.len > 0xFFFFFFFFu || (uint64_t)next + blocks > BLOCKS) {
        diag_plain(D_FS_FULL, arg);
        rt_free(file.data);
        return 1;
    }
    if (add(dir, disk + start, disk_len - start, next, (uint32_t)file.len, KIND_FILE, arg) != 0) {
        rt_free(file.data);
        return 1;
    }
    if (file.len > 0) {
        memcpy(img + (size_t)next * BLOCK, file.data, file.len);
    }
    next += blocks;
    rt_free(file.data);
    return 0;
}

int main(int argc, char **argv)
{
    int i, rc = 0;

    if (argc < 2) {
        diag_plain(D_FS_USAGE, NULL);
        return 2;
    }
    img = rt_alloc((size_t)BLOCKS * BLOCK);
    memset(img, 0, (size_t)BLOCKS * BLOCK);
    memcpy(img, MAGIC, 8);
    put32(img + 8, 2);
    put32(img + 12, BLOCK);
    put32(img + 16, BLOCKS);
    put32(img + 20, DIR_BLOCKS);

    for (i = 2; i < argc && rc == 0; i++) {
        rc = put(argv[i]);
    }
    put32(img + 24, next);
    if (rc == 0 && rt_write_file(argv[1], img, (size_t)BLOCKS * BLOCK) != 0) {
        diag_plain(D_CANNOT_WRITE, argv[1]);
        rc = 2;
    }
    rt_free(img);
    return rc;
}
