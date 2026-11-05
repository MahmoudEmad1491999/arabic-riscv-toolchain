/* C side of the packed-structure test: the same layout with GCC's packed attribute. */
struct __attribute__((packed)) hdr {
    unsigned char magic;
    unsigned int len;     /* offset 1 */
    unsigned short flag;  /* offset 5 */
    long last;            /* offset 7, total 15 bytes */
};

long قيم_الرأس(struct hdr h)
{
    return h.magic + (long)h.len * 10 + h.flag * 100 + h.last * 1000;
}

long اقرأ_الرأس(const struct hdr *h)
{
    return h->magic + (long)h->len * 10 + h->flag * 100 + h->last * 1000;
}

struct hdr اصنع_رأسا(long v)
{
    struct hdr h = {1, (unsigned int)v, 2, -v};
    return h;
}
