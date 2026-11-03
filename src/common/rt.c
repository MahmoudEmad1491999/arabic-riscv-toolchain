/* rt.c - hosted implementation of the runtime layer (on top of libc). */
#define _POSIX_C_SOURCE 200809L
#include "rt.h"

#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rt_read_file(const char *path, RtBuf *out)
{
    FILE *f = fopen(path, "rb");
    long size;
    size_t got;

    if (f == NULL) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    out->data = rt_alloc((size_t)size + 1);
    got = fread(out->data, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) {
        return -1;
    }
    out->data[size] = 0;
    out->len = (size_t)size;
    return 0;
}

int rt_write_file(const char *path, const uint8_t *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    size_t put;

    if (f == NULL) {
        return -1;
    }
    put = fwrite(data, 1, len, f);
    if (fclose(f) != 0 || put != len) {
        return -1;
    }
    return 0;
}

int rt_make_executable(const char *path)
{
    return chmod(path, 0755) == 0 ? 0 : -1;
}

void rt_write(int fd, const uint8_t *data, size_t len)
{
    FILE *f = (fd == RT_STDERR) ? stderr : stdout;
    fwrite(data, 1, len, f);
    fflush(f);
}

void rt_exit(int code)
{
    exit(code);
}

void *rt_alloc(size_t size)
{
    void *p = calloc(1, size == 0 ? 1 : size);
    if (p == NULL) {
        static const char msg[] = "out of memory\n";
        rt_write(RT_STDERR, (const uint8_t *)msg, sizeof msg - 1);
        rt_exit(2);
    }
    return p;
}

void *rt_realloc(void *p, size_t size)
{
    void *q = realloc(p, size == 0 ? 1 : size);
    if (q == NULL) {
        static const char msg[] = "out of memory\n";
        rt_write(RT_STDERR, (const uint8_t *)msg, sizeof msg - 1);
        rt_exit(2);
    }
    return q;
}

void rt_free(void *p)
{
    free(p);
}

const char *rt_getenv(const char *name)
{
    return getenv(name);
}

#define ARENA_BLOCK_SIZE (1u << 20)

void arena_init(Arena *a)
{
    a->head = NULL;
}

void *arena_alloc(Arena *a, size_t size)
{
    ArenaBlock *b = a->head;
    void *p;

    size = (size + 7u) & ~(size_t)7u;
    if (b == NULL || b->cap - b->used < size) {
        size_t cap = size > ARENA_BLOCK_SIZE ? size : ARENA_BLOCK_SIZE;
        b = rt_alloc(sizeof *b);
        b->data = rt_alloc(cap);
        b->cap = cap;
        b->used = 0;
        b->next = a->head;
        a->head = b;
    }
    p = b->data + b->used;
    b->used += size;
    memset(p, 0, size);
    return p;
}

void arena_free(Arena *a)
{
    ArenaBlock *b = a->head;
    while (b != NULL) {
        ArenaBlock *next = b->next;
        free(b->data);
        free(b);
        b = next;
    }
    a->head = NULL;
}
