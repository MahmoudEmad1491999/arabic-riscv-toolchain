/* buf.h - growable byte buffer with little-endian writers. */
#ifndef BUF_H
#define BUF_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} Buf;

void buf_init(Buf *b);
void buf_free(Buf *b);
void buf_reserve(Buf *b, size_t extra);
void buf_bytes(Buf *b, const void *data, size_t len);
void buf_u8(Buf *b, uint8_t v);
void buf_u16(Buf *b, uint16_t v);
void buf_u32(Buf *b, uint32_t v);
void buf_u64(Buf *b, uint64_t v);
void buf_zeros(Buf *b, size_t n);
void buf_align(Buf *b, size_t align); /* pad with zeros */

/* Overwrite 4 bytes at an existing offset. */
void buf_put_u32_at(Buf *b, size_t off, uint32_t v);

#endif
