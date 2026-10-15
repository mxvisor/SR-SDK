/* A malloc'd-buffer wrapper around blast(): blast() has a streaming callback
 * interface; this makes it a call with (input, length, &output length). */
#include <stdlib.h>
#include <string.h>
#include "blast.h"

struct mem {
    unsigned char *p;
    size_t len;
    size_t cap;
    int oom;
};

static unsigned in_fn(void *how, unsigned char **buf)
{
    struct mem *m = (struct mem *)how;
    *buf = m->p;
    return (unsigned)m->len;
}

static int out_fn(void *how, unsigned char *buf, unsigned len)
{
    struct mem *m = (struct mem *)how;
    if (m->len + len > m->cap) {
        size_t cap = m->cap ? m->cap * 2 : 65536;
        while (cap < m->len + len) cap *= 2;
        unsigned char *np = (unsigned char *)realloc(m->p, cap);
        if (!np) { m->oom = 1; return -1; }
        m->p = np;
        m->cap = cap;
    }
    memcpy(m->p + m->len, buf, len);
    m->len += len;
    return 0;
}

unsigned char *is3_blast(const char *in, size_t inlen, size_t *outlen)
{
    struct mem m;
    m.len = 0;
    m.cap = 0;
    m.oom = 0;
    m.p = NULL;

    unsigned char *inp = (unsigned char *)(size_t)in;
    unsigned left = (unsigned)inlen;
    int err = blast(in_fn, &m, out_fn, &m, &left, &inp);

    if (err != 0 || m.oom) {
        free(m.p);
        return NULL;
    }
    *outlen = m.len;
    if (m.p == NULL) {
        m.p = (unsigned char *)malloc(1);
    }
    return m.p;
}

void is3_free(void *p)
{
    free(p);
}
