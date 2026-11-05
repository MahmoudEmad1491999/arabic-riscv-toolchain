/* C side of the ABI test, compiled by GCC: غرة calls these, and
 * استدع_غرة calls back into غرة. */
typedef struct { long a, b, c; } big;          /* 24 bytes: by reference */
typedef struct { int x, y; } pt;               /* 8 bytes: one register */
typedef struct { const unsigned char *ptr; unsigned long len; } slice;

long جمع_عشرة(long a, long b, long c, long d, long e, long f, long g, long h, long i, long j)
{
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i + 10 * j;
}

long مختلط(slice s, pt p, big q, unsigned int u, signed char sc, slice s2, long t1, long t2)
{
    return (long)s.len * 1000 + p.x * 100 + p.y * 10 + q.a + q.b + q.c + u + sc +
           (long)s2.len * 7 + t1 * 11 + t2 * 13;
}

big اصنع_كبيرة(long v)
{
    big r = {v, v * 2, v * 3};
    return r;
}

pt بدل(pt p)
{
    pt r = {p.y, p.x};
    return r;
}

unsigned int زد_ط(unsigned int v)
{
    return v + 1;
}

long مقسوم(long a, long b, long c, long d, long e, long f, long g, slice s)
{
    return a + b + c + d + e + f + g + (long)s.len * 100 + s.ptr[0];
}

extern long مجموع_غرة(long, long, long, long, long, long, long, long, long, long);

long استدع_غرة(void)
{
    return مجموع_غرة(10, 9, 8, 7, 6, 5, 4, 3, 2, 1);
}
