/* buf.c - growable byte buffer. */
#include "buf.h"
#include "rt.h"

#include <string.h>

void buf_init(Buf *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void buf_free(Buf *b)
{
    rt_free(b->data);
    buf_init(b);
}

void buf_reserve(Buf *b, size_t extra)
{
    size_t need = b->len + extra;
    if (need > b->cap) {
        size_t cap = b->cap == 0 ? 256 : b->cap;
        while (cap < need) {
            cap *= 2;
        }
        b->data = rt_realloc(b->data, cap);
        b->cap = cap;
    }
}

void buf_bytes(Buf *b, const void *data, size_t len)
{
    if (len == 0) {
        return;
    }
    buf_reserve(b, len);
    memcpy(b->data + b->len, data, len);
    b->len += len;
}

void buf_u8(Buf *b, uint8_t v)
{
    buf_bytes(b, &v, 1);
}

void buf_u16(Buf *b, uint16_t v)
{
    uint8_t t[2];
    t[0] = (uint8_t)v;
    t[1] = (uint8_t)(v >> 8);
    buf_bytes(b, t, 2);
}

void buf_u32(Buf *b, uint32_t v)
{
    uint8_t t[4];
    int i;
    for (i = 0; i < 4; i++) {
        t[i] = (uint8_t)(v >> (8 * i));
    }
    buf_bytes(b, t, 4);
}

void buf_u64(Buf *b, uint64_t v)
{
    uint8_t t[8];
    int i;
    for (i = 0; i < 8; i++) {
        t[i] = (uint8_t)(v >> (8 * i));
    }
    buf_bytes(b, t, 8);
}

void buf_zeros(Buf *b, size_t n)
{
    if (n == 0) {
        return;
    }
    buf_reserve(b, n);
    memset(b->data + b->len, 0, n);
    b->len += n;
}

void buf_align(Buf *b, size_t align)
{
    while (b->len % align != 0) {
        buf_u8(b, 0);
    }
}

void buf_put_u32_at(Buf *b, size_t off, uint32_t v)
{
    int i;
    for (i = 0; i < 4; i++) {
        b->data[off + (size_t)i] = (uint8_t)(v >> (8 * i));
    }
}
