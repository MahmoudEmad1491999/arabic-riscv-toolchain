/* rt.h - the only place that touches the operating system.
 *
 * Everything else in the toolchain goes through these functions, so the
 * self-hosted version only has to reimplement this file on top of Linux
 * RISC-V system calls.
 */
#ifndef RT_H
#define RT_H

#include <stddef.h>
#include <stdint.h>

#define RT_STDOUT 1
#define RT_STDERR 2

typedef struct {
    uint8_t *data;
    size_t len;
} RtBuf;

/* Reads a whole file. Returns 0 on success, -1 on failure. */
int rt_read_file(const char *path, RtBuf *out);

/* Writes a whole file (create/truncate). Returns 0 on success, -1 on failure. */
int rt_write_file(const char *path, const uint8_t *data, size_t len);

/* Writes bytes to a file descriptor (RT_STDOUT / RT_STDERR). */
void rt_write(int fd, const uint8_t *data, size_t len);

void rt_exit(int code);

/* Raw allocation. Never returns NULL: exits on failure. */
void *rt_alloc(size_t size);
void *rt_realloc(void *p, size_t size);
void rt_free(void *p);

/* Environment variable, or NULL if unset. */
const char *rt_getenv(const char *name);

/* Bump allocator. Memory is released all at once with arena_free. */
typedef struct ArenaBlock ArenaBlock;
struct ArenaBlock {
    ArenaBlock *next;
    size_t used;
    size_t cap;
    uint8_t *data;
};

typedef struct {
    ArenaBlock *head;
} Arena;

void arena_init(Arena *a);
void *arena_alloc(Arena *a, size_t size); /* zeroed, 8-byte aligned */
void arena_free(Arena *a);

#endif
