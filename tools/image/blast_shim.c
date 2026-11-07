/**
 *
 *  Copyright (C) 2026 mxvisor
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is furnished to do
 *  so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 */

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
