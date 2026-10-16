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

/* The enhanced 3D rendering's CPU backend: the frame's command list
 * (Game_hires-list.h) drawn into H, 320k x 240k palette indices, the way the
 * game's own rasterizers draw L, with the colours, the tables and the
 * texture coordinates they use; with the filtering on, also in colours.
 *
 * H is drawn in bands of rows, on as many threads as the machine spares:
 * each band is the whole list drawn within the band's rows.  A pixel of H
 * depends only on the commands that cover it, in their order, so the bands
 * come out as one pass over the whole of H would draw them. */

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <SDL.h>

#include "Game_hires-list.h"
#include "Game_hooks.h"

/* floor(x) for the hot loops: floor() is a library call on x86-64
 * without SSE4.1, and a slow one in Windows' C library */
static inline ptrdiff_t hr_floor(double x)
{
    ptrdiff_t i = (ptrdiff_t)x;
    return i - (x < (double)i);
}

/* The filtered colours (hr_target's colour): H drawn a second time in the
 * palette's 32-bit colours, the frame's start, the ground and the textures
 * sampled bilinearly and the tables applied as the change they make to a
 * colour.  The indices
 * are drawn as they are without it: the tables read them. */

/* far away, a ground pixel averages at most this many samples each way */
#define HR_GROUND_TAPS 4

#define HR_LANES 0x00FF00FF00FF00FFull

/* a colour's 4 bytes in 16-bit lanes, and back */
static inline uint64_t hr_spread(uint32_t c)
{
    return (uint64_t)(c & 0xFF) | ((uint64_t)(c & 0xFF00) << 8) | ((uint64_t)(c & 0xFF0000) << 16) |
           ((uint64_t)(c & 0xFF000000u) << 24);
}

static inline uint32_t hr_pack(uint64_t s)
{
    return (uint32_t)((s & 0xFF) | ((s >> 8) & 0xFF00) | ((s >> 16) & 0xFF0000) | ((s >> 24) & 0xFF000000u));
}

static inline uint64_t hr_lerp(uint64_t a, uint64_t b, unsigned w)
{
    return ((a * (256 - w) + b * w) >> 8) & HR_LANES;
}

/* the frame's palette spread */
static uint64_t hr_pal64[256];

/* c00 c01 / c10 c11, fx across and fy down in 1/256 */
static inline uint64_t hr_bilerp64(uint64_t c00, uint64_t c01, uint64_t c10, uint64_t c11, unsigned fx, unsigned fy)
{
    return hr_lerp(hr_lerp(c00, c01, fx), hr_lerp(c10, c11, fx), fy);
}

/* dst + (now - was), byte by byte: a table's change of a colour applied to
 * the filtered colour under it */
static inline uint32_t hr_delta(uint32_t dst, uint32_t now, uint32_t was)
{
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        int c = (int)((dst >> shift) & 0xFF) + (int)((now >> shift) & 0xFF) - (int)((was >> shift) & 0xFF);
        if (c < 0) c = 0;
        if (c > 255) c = 255;
        out |= (uint32_t)c << shift;
    }
    return out;
}

static inline uint32_t *hr_colour_at(const hr_target *t, int hy)
{
    return t->colour + (size_t)hy * t->width;
}

/* H's column hx in L's pixels: ((hx + 0.5) / k, the column's centre */
#define HR_MAX_WIDTH (HR_L_WIDTH * 8)
static double hr_centre[HR_MAX_WIDTH];
static int hr_centre_k;
/* L sampled bilinearly at H's column hx: L's columns hr_left[hx] and the
 * one after, the latter by hr_right_w[hx] / 256 */
static short hr_left[HR_MAX_WIDTH];
static uint8_t hr_right_w[HR_MAX_WIDTH];

static void hr_centres(const hr_target *t)
{
    if (hr_centre_k == t->k) return;
    for (int hx = 0; hx < t->width && hx < HR_MAX_WIDTH; hx++) {
        hr_centre[hx] = (hx + 0.5) / t->k;
        int x;
        unsigned w;
        hr_bilinear_at(hx, t->k, HR_L_WIDTH, &x, &w);
        hr_left[hx] = (short)x;
        hr_right_w[hx] = (uint8_t)w;
    }
    hr_centre_k = t->k;
}

/* H's first row of L's row y, y clamped to [0, 200] */
static inline int hr_row(const hr_target *t, int y)
{
    return t->row0[y < 0 ? 0 : y > HR_L_HEIGHT ? HR_L_HEIGHT : y];
}

/* the band of H's rows [y0, y1) a thread draws, and its scratch */
typedef struct {
    int y0, y1;
    int column[HR_MAX_UNIT_SPRITES][HR_MAX_WIDTH];
} hr_band;

/* L's pixel (x, y) as a block of H */
static inline void hr_block(const hr_target *t, const hr_band *band, int x, int y, uint8_t value)
{
    int k = t->k;
    int row0 = t->row0[y], row1 = t->row0[y + 1];
    if (row0 < band->y0) row0 = band->y0;
    if (row1 > band->y1) row1 = band->y1;
    uint8_t *dst = t->pixel + (size_t)row0 * t->width + x * k;
    for (int row = row0; row < row1; row++, dst += t->width) memset(dst, value, k);
    if (t->colour != NULL) {
        uint32_t colour = t->palette[value];
        for (int row = row0; row < row1; row++) {
            uint32_t *out = hr_colour_at(t, row) + x * k;
            for (int i = 0; i < k; i++) out[i] = colour;
            memset(t->drawn + (size_t)row * t->width + x * k, HR_DRAWN_OVER, k);
        }
    }
}

/* H's row hy of a picture, bilinear, with a spread palette */
static void hr_picture_row(const hr_target *t, const uint8_t *picture, const uint64_t *pal64, int hy, uint32_t *out)
{
    int ya;
    unsigned fy;
    hr_bilinear_at(hy, t->ky, HR_L_HEIGHT, &ya, &fy);
    const uint8_t *a = picture + ya * HR_L_WIDTH, *b = a + HR_L_WIDTH;
    for (int hx = 0; hx < t->width; hx++) {
        int x = hr_left[hx];
        out[hx] = hr_pack(hr_bilerp64(pal64[a[x]], pal64[a[x + 1]], pal64[b[x]], pal64[b[x + 1]], hr_right_w[hx], fy));
    }
}


/* L's rows the frame's ground covers: their colours need no filtering */
static uint8_t hr_ground_covers[HR_L_HEIGHT];

/* The frame starts as L: in indices as blocks, and in colours as the
 * filtering shows L (what the 3D blends over is filtered too), except
 * under the ground */
static void hr_begin(const hr_target *t, const hr_band *band, const uint8_t *l)
{
    int k = t->k;
    for (int y = 0; y < HR_L_HEIGHT; y++) {
        int row0 = t->row0[y], row1 = t->row0[y + 1];
        if (row0 < band->y0) row0 = band->y0;
        if (row1 > band->y1) row1 = band->y1;
        if (row0 >= row1) continue;
        uint8_t *dst = t->pixel + (size_t)row0 * t->width;
        const uint8_t *src = l + y * HR_L_WIDTH;
        for (int x = 0; x < HR_L_WIDTH; x++) memset(dst + x * k, src[x], k);
        for (int row = 1; row < row1 - row0; row++) {
            memcpy(dst + (size_t)row * t->width, dst, (size_t)t->width);
        }
        if (t->colour == NULL) continue;
        memset(t->drawn + (size_t)row0 * t->width, HR_DRAWN_START, (size_t)(row1 - row0) * t->width);
        for (int row = row0; row < row1; row++) {
            uint32_t *out = hr_colour_at(t, row);
            if (hr_ground_covers[y]) {
                for (int x = 0; x < HR_L_WIDTH; x++) {
                    uint32_t colour = t->palette[src[x]];
                    for (int i = 0; i < k; i++) out[x * k + i] = colour;
                }
                continue;
            }
            hr_picture_row(t, l, hr_pal64, row, out);
        }
    }
}

/* ------------------------------------------------------------------ */
/* The ground (loc_39010): row r of the view is one row of the level's  */
/* ground bitmap, sampled by loc_3CC08 with the per-row tables.  H's    */
/* rows take the projection's exact values, or without it the tables   */
/* interpolated between L's rows.                                      */

static void hr_ground_draw(const hr_target *t, const hr_band *band, const hr_list *list, const hr_ground *g)
{
    int r = g->row;
    int hy0 = t->row0[r], hy1 = t->row0[r + 1];
    if (hy0 < band->y0) hy0 = band->y0;
    if (hy1 > band->y1) hy1 = band->y1;
    const uint8_t *bitmap = (const uint8_t *)hr_list_at(list, g->bitmap);
    for (int hy = hy0; hy < hy1; hy++) {
        /* the row's centre in L's rows, where L's row r has its centre at r */
        double yf = (hy + 0.5) / t->ky - 0.5;
        int ra = (yf < r) ? r - 1 : r;
        double tb = yf - ra;
        /* g's rows r - 1, r, r + 1 are its [0], [1], [2] */
        int a = ra - r + 1, b = a + 1;
        double gy, gx, span, foot_y = 0;
        if (g->exact) {
            /* the row's centre, its top and its bottom */
            double y = (hy + 0.5) / t->ky;
            gy = hr_ground_y(g, y);
            span = hr_ground_span(g, gy);
            gx = -span / 2;
            foot_y = fabs(hr_ground_y(g, y + 0.5 / t->ky) - hr_ground_y(g, y - 0.5 / t->ky)) / 20.0;
        } else if (g->span[a] != 0 && g->span[b] != 0) {
            gy = (1 - tb) * g->y[a] + tb * g->y[b];
            gx = (1 - tb) * g->x[a] + tb * g->x[b];
            span = (1 - tb) * g->span[a] + tb * g->span[b];
            foot_y = fabs((double)g->y[b] - g->y[a]) / 20.0 / t->ky;
        } else {
            gy = g->y[1];
            gx = g->x[1];
            span = g->span[1];
        }

        /* loc_3948D's address: the source row (the copy's rows are before
         * the wrap at the bitmap's height) and the row's start */
        int32_t row = (int32_t)floor((gy + g->offset_y) / 20.0);
        const uint8_t *base = bitmap + (size_t)(row - g->first_row) * g->columns + (g->offset_x + 1 - g->first_column);
        if (span >= (double)0x1400000) base--;
        double x0 = gx + g->fraction_x;
        double step = span / HR_L_WIDTH;
        /* loc_3CC08 steps with the integer in the low word and the fraction
         * in the high word, through adc: the fraction's carry reaches the
         * integer one pixel late, so L's pixel i takes the sample at
         * x0 + (i + 1) * step less step's fraction */
        double lag = step - floor(step / 65536.0) * 65536.0;

        uint8_t *dst = t->pixel + (size_t)hy * t->width;
        for (int hx = 0; hx < t->width; hx++) {
            double x = x0 + (hr_centre[hx] + 0.5) * step - lag;
            dst[hx] = base[hr_floor(x * (1.0 / 65536.0))];
        }
        if (t->drawn != NULL) memset(t->drawn + (size_t)hy * t->width, HR_DRAWN_3D, t->width);

        if (t->colour == NULL || g->rows <= 0 || g->columns <= 0) continue;
        /* the same samples, bilinear, in texels: column coloff + c of the
         * copy's rows; far away a few taps across the pixel's footprint */
        ptrdiff_t coloff = (g->offset_x + 1 - g->first_column) - (span >= (double)0x1400000 ? 1 : 0);
        double sy = (gy + g->offset_y) / 20.0;
        double foot_x = fabs(step) / 65536.0 / t->k;
        int nx = (int)ceil(foot_x), ny = (int)ceil(foot_y);
        int taps = HR_GROUND_TAPS;
        if (nx < 1) nx = 1;
        if (ny < 1) ny = 1;
        if (nx > taps) nx = taps;
        if (ny > taps) ny = taps;
        const uint8_t *rows[2 * 8];
        unsigned fys[8];
        for (int j = 0; j < ny; j++) {
            double ys = sy + ((j + 0.5) / ny - 0.5) * foot_y - 0.5;
            ptrdiff_t r0 = hr_floor(ys);
            fys[j] = (unsigned)((ys - r0) * 256);
            for (int e = 0; e < 2; e++) {
                ptrdiff_t r = r0 + e;
                if (r < g->first_row) r = g->first_row;
                if (r > g->first_row + g->rows - 1) r = g->first_row + g->rows - 1;
                rows[2 * j + e] = bitmap + (size_t)(r - g->first_row) * g->columns;
            }
        }
        ptrdiff_t last = g->columns - 1;
        uint32_t *out = hr_colour_at(t, hy);
        if (nx == 1 && ny == 1) {
            /* one tap: x stepped in 32.32 texels */
            const uint8_t *ra = rows[0], *rb = rows[1];
            unsigned fy = fys[0];
            double xa = (x0 + (0.5 / t->k + 0.5) * step - lag) * (1.0 / 65536.0) - 0.5;
            int64_t X = (int64_t)floor(xa * 4294967296.0) + ((int64_t)coloff << 32);
            int64_t DX = (int64_t)floor(step / t->k * 65536.0);
            for (int hx = 0; hx < t->width; hx++, X += DX) {
                ptrdiff_t ca = (ptrdiff_t)(X >> 32), cb = ca + 1;
                unsigned fx = (unsigned)(X >> 24) & 0xFF;
                if ((size_t)ca >= (size_t)last) {
                    if (ca < 0) ca = 0;
                    if (ca > last) ca = last;
                    if (cb < 0) cb = 0;
                    if (cb > last) cb = last;
                }
                out[hx] = hr_pack(hr_bilerp64(hr_pal64[ra[ca]], hr_pal64[ra[cb]], hr_pal64[rb[ca]], hr_pal64[rb[cb]], fx, fy));
            }
            continue;
        }
        for (int hx = 0; hx < t->width; hx++) {
            double x = (x0 + (hr_centre[hx] + 0.5) * step - lag) * (1.0 / 65536.0);
            uint64_t sum = 0;
            for (int i = 0; i < nx; i++) {
                double xs = x + ((i + 0.5) / nx - 0.5) * foot_x - 0.5;
                ptrdiff_t c0 = hr_floor(xs);
                unsigned fx = (unsigned)((xs - c0) * 256);
                ptrdiff_t ca = coloff + c0, cb = ca + 1;
                if (ca < 0) ca = 0;
                if (ca > last) ca = last;
                if (cb < 0) cb = 0;
                if (cb > last) cb = last;
                for (int j = 0; j < ny; j++) {
                    const uint8_t *ra = rows[2 * j], *rb = rows[2 * j + 1];
                    sum += hr_bilerp64(hr_pal64[ra[ca]], hr_pal64[ra[cb]], hr_pal64[rb[ca]], hr_pal64[rb[cb]], fx, fys[j]);
                }
            }
            int n = nx * ny;
            if (n == 1) {
                out[hx] = hr_pack(sum);
            } else {
                uint32_t colour = 0;
                for (int shift = 0; shift < 64; shift += 16) {
                    colour |= (uint32_t)(((sum >> shift) & 0xFFFF) / n) << (shift / 2);
                }
                out[hx] = colour;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* The model polygons (loc_3C5C3's list): scanline polygons with the    */
/* corners' texture coordinates interpolated along the edges and across */
/* each span, as the game's affine rasterizers do.                     */

typedef struct { double x, u, v; } hr_cross;

/* a span's colours, the texture bilinear within its tile, before the
 * indices are written (row holds the indices under the span) */
static void hr_fill_colour(const hr_target *t, const hr_polygon *polygon, const uint8_t *shade, const uint8_t *row,
                           uint32_t *out, int hx0, int hx1, int32_t u, int32_t v, int32_t step_u, int32_t step_v, int tw, int th)
{
    const uint8_t *texture = polygon->texture;
    for (int hx = hx0; hx < hx1; hx++, u += step_u, v += step_v) {
        int32_t us = u - 0x8000, vs = v - 0x8000;
        int32_t c0 = us >> 16, r0 = vs >> 16;
        unsigned fx = ((uint32_t)us >> 8) & 0xFF, fy = ((uint32_t)vs >> 8) & 0xFF;
        int32_t c1 = c0 + 1, r1 = r0 + 1;
        if (c0 < 0) c0 = 0;
        if (c1 < 0) c1 = 0;
        if (c0 > tw - 1) c0 = tw - 1;
        if (c1 > tw - 1) c1 = tw - 1;
        if (r0 < 0) r0 = 0;
        if (r1 < 0) r1 = 0;
        if (r0 > th - 1) r0 = th - 1;
        if (r1 > th - 1) r1 = th - 1;
        uint8_t t00 = texture[(r0 << 8) | c0], t01 = texture[(r0 << 8) | c1];
        uint8_t t10 = texture[(r1 << 8) | c0], t11 = texture[(r1 << 8) | c1];
        switch (polygon->mode) {
        case FILL_TEXTURE:
            out[hx] = hr_pack(hr_bilerp64(hr_pal64[shade[t00]], hr_pal64[shade[t01]], hr_pal64[shade[t10]],
                                          hr_pal64[shade[t11]], fx, fy));
            break;
        case FILL_TEXTURE_KEY: {
            /* texel 0 is not drawn: it weighs as transparent */
            unsigned w[4] = { (256 - fx) * (256 - fy) >> 8, fx * (256 - fy) >> 8, (256 - fx) * fy >> 8, fx * fy >> 8 };
            uint8_t texel[4] = { t00, t01, t10, t11 };
            uint64_t sum = 0;
            unsigned alpha = 0;
            for (int i = 0; i < 4; i++) {
                if (texel[i] == 0) continue;
                sum += hr_pal64[shade[texel[i]]] * w[i];
                alpha += w[i];
            }
            if (alpha == 0) break;
            if (alpha > 256) alpha = 256;
            sum += hr_spread(out[hx]) * (256 - alpha);
            out[hx] = hr_pack((sum >> 8) & HR_LANES);
            break;
        }
        default: {
            uint8_t was = row[hx];
            const uint8_t *table = polygon->blend + (was << 8);
            uint32_t now = hr_pack(hr_bilerp64(hr_pal64[table[shade[t00]]], hr_pal64[table[shade[t01]]],
                                               hr_pal64[table[shade[t10]]], hr_pal64[table[shade[t11]]], fx, fy));
            out[hx] = hr_delta(out[hx], now, t->palette[was]);
            break;
        }
        }
    }
}

static void hr_fill(const hr_target *t, const hr_band *band, const hr_polygon *polygon, const uint8_t *shade)
{
    int k = t->k;
    int width = t->width;
    const hr_vertex *p = polygon->p;
    int n = polygon->n;
    int cx0 = polygon->clip.x0 * k, cx1 = polygon->clip.x1 * k;
    int cy0 = hr_row(t, polygon->clip.y0), cy1 = hr_row(t, polygon->clip.y1);
    if (cx0 < 0) cx0 = 0;
    if (cx1 > width) cx1 = width;
    if (cy0 < band->y0) cy0 = band->y0;
    if (cy1 > band->y1) cy1 = band->y1;
    if (cy0 >= cy1) return;

    double ymin = p[0].y, ymax = p[0].y;
    for (int i = 1; i < n; i++) {
        if (p[i].y < ymin) ymin = p[i].y;
        if (p[i].y > ymax) ymax = p[i].y;
    }
    int hy0 = (int)ceil(ymin * t->ky - 0.5), hy1 = (int)ceil(ymax * t->ky - 0.5);
    if (hy0 < cy0) hy0 = cy0;
    if (hy1 > cy1) hy1 = cy1;

    /* the texture's tile: 64x64, or 256x32 (hr_entry_after's corners) */
    int tw = 64, th = 64;
    for (int i = 0; i < n; i++) {
        if (p[i].u > 64) {
            tw = 256;
            th = 32;
        }
    }

    hr_cross cross[HR_MAX_VERTICES];
    for (int hy = hy0; hy < hy1; hy++) {
        double yc = (hy + 0.5) / t->ky;
        int count = 0;
        for (int i = 0; i < n; i++) {
            const hr_vertex *a = &p[i], *b = &p[(i + 1) % n];
            if ((a->y <= yc && yc < b->y) || (b->y <= yc && yc < a->y)) {
                double f = (yc - a->y) / (b->y - a->y);
                hr_cross c = { a->x + f * (b->x - a->x), a->u + f * (b->u - a->u), a->v + f * (b->v - a->v) };
                int at = count++;
                while (at > 0 && cross[at - 1].x > c.x) {
                    cross[at] = cross[at - 1];
                    at--;
                }
                cross[at] = c;
            }
        }
        uint8_t *row = t->pixel + (size_t)hy * width;
        for (int i = 0; i + 1 < count; i += 2) {
            const hr_cross *l = &cross[i], *r = &cross[i + 1];
            int hx0 = (int)ceil(l->x * k - 0.5), hx1 = (int)ceil(r->x * k - 0.5);
            if (hx0 < cx0) hx0 = cx0;
            if (hx1 > cx1) hx1 = cx1;
            if (hx0 >= hx1) continue;
            uint32_t *out = t->colour != NULL ? hr_colour_at(t, hy) : NULL;
            if (out != NULL) memset(t->drawn + (size_t)hy * width + hx0, HR_DRAWN_3D, hx1 - hx0);
            if (polygon->mode == FILL_FLAT) {
                memset(row + hx0, polygon->colour, hx1 - hx0);
                if (out != NULL) for (int hx = hx0; hx < hx1; hx++) out[hx] = t->palette[polygon->colour];
                continue;
            }
            if (polygon->mode == FILL_FLAT_BLEND) {
                for (int hx = hx0; hx < hx1; hx++) {
                    uint8_t was = row[hx];
                    row[hx] = polygon->blend[(was << 8) | polygon->colour];
                    if (out != NULL) out[hx] = hr_delta(out[hx], t->palette[row[hx]], t->palette[was]);
                }
                continue;
            }
            double dx = r->x - l->x;
            double du = (dx > 0) ? (r->u - l->u) / dx : 0, dv = (dx > 0) ? (r->v - l->v) / dx : 0;
            double start = (hx0 + 0.5) / k - l->x;
            int32_t u = (int32_t)floor((l->u + start * du) * 65536.0);
            int32_t v = (int32_t)floor((l->v + start * dv) * 65536.0);
            int32_t step_u = (int32_t)floor(du / k * 65536.0), step_v = (int32_t)floor(dv / k * 65536.0);
            if (out != NULL) hr_fill_colour(t, polygon, shade, row, out, hx0, hx1, u, v, step_u, step_v, tw, th);
            for (int hx = hx0; hx < hx1; hx++, u += step_u, v += step_v) {
                uint8_t texel = polygon->texture[(((v >> 16) & 0xFF) << 8) | ((u >> 16) & 0xFF)];
                switch (polygon->mode) {
                case FILL_TEXTURE:
                    row[hx] = shade[texel];
                    break;
                case FILL_TEXTURE_KEY:
                    if (texel != 0) row[hx] = shade[texel];
                    break;
                default:
                    row[hx] = polygon->blend[(row[hx] << 8) | shade[texel]];
                    break;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* The sprites.  One shown larger than it is drawn is smoothed first.   */

/* Scale2x and Scale3x (AdvMAME2x/3x) on the sprite's values, "nothing
 * drawn" among them: edges and outlines come out in steps of a target
 * pixel instead of n x n blocks, and every value is one of the source's. */
static void hr_scale2x(const hr_image *src, hr_image *dst)
{
    int w = src->w, h = src->h;
    dst->w = 2 * w;
    dst->h = 2 * h;
    for (int y = 0; y < h; y++) {
        const uint16_t *row = src->pixel + y * w;
        const uint16_t *up = (y > 0) ? row - w : row, *down = (y + 1 < h) ? row + w : row;
        uint16_t *out0 = dst->pixel + (size_t)(2 * y) * dst->w, *out1 = out0 + dst->w;
        for (int x = 0; x < w; x++) {
            uint16_t B = up[x], D = row[x > 0 ? x - 1 : x], E = row[x], F = row[x + 1 < w ? x + 1 : x], H = down[x];
            uint16_t e0 = E, e1 = E, e2 = E, e3 = E;
            if (B != H && D != F) {
                if (D == B) e0 = D;
                if (B == F) e1 = F;
                if (D == H) e2 = D;
                if (H == F) e3 = F;
            }
            out0[2 * x] = e0;
            out0[2 * x + 1] = e1;
            out1[2 * x] = e2;
            out1[2 * x + 1] = e3;
        }
    }
}

static void hr_scale3x(const hr_image *src, hr_image *dst)
{
    int w = src->w, h = src->h;
    dst->w = 3 * w;
    dst->h = 3 * h;
    for (int y = 0; y < h; y++) {
        const uint16_t *row = src->pixel + y * w;
        const uint16_t *up = (y > 0) ? row - w : row, *down = (y + 1 < h) ? row + w : row;
        uint16_t *out0 = dst->pixel + (size_t)(3 * y) * dst->w, *out1 = out0 + dst->w, *out2 = out1 + dst->w;
        for (int x = 0; x < w; x++) {
            int l = x > 0 ? x - 1 : x, r = x + 1 < w ? x + 1 : x;
            uint16_t A = up[l], B = up[x], C = up[r], D = row[l], E = row[x], F = row[r], G = down[l], H = down[x], I = down[r];
            uint16_t e[9] = { E, E, E, E, E, E, E, E, E };
            if (B != H && D != F) {
                if (D == B) e[0] = D;
                if ((D == B && E != C) || (B == F && E != A)) e[1] = B;
                if (B == F) e[2] = F;
                if ((D == B && E != G) || (D == H && E != A)) e[3] = D;
                if ((B == F && E != I) || (H == F && E != C)) e[5] = F;
                if (D == H) e[6] = D;
                if ((D == H && E != I) || (H == F && E != G)) e[7] = H;
                if (H == F) e[8] = F;
            }
            for (int i = 0; i < 3; i++) {
                out0[3 * x + i] = e[i];
                out1[3 * x + i] = e[3 + i];
                out2[3 * x + i] = e[6 + i];
            }
        }
    }
}

/* The smoothed sprites, by the sprite's hash and the scale; a sprite is
 * smoothed once, not every frame.  The cache is emptied when it grows
 * past HR_CACHE_BYTES. */
#define HR_MAX_SPRITE 512
#define HR_CACHE_SLOTS 1024
#define HR_CACHE_BYTES (96u << 20)

typedef struct {
    uint32_t hash;
    int w, h, n;
    hr_image image;
} hr_cached;

static hr_cached hr_cache[HR_CACHE_SLOTS];
static size_t hr_cache_bytes;

/* what the cache lets go of during a frame, freed when the frame is drawn:
 * a sprite earlier in the frame may still be using it */
static uint16_t **hr_released;
static size_t hr_released_count, hr_released_capacity;

static void hr_release(uint16_t *pixel)
{
    if (pixel == NULL) return;
    if (hr_released_count == hr_released_capacity) {
        size_t capacity = hr_released_capacity ? 2 * hr_released_capacity : 64;
        uint16_t **grown = (uint16_t **)realloc(hr_released, capacity * sizeof(uint16_t *));
        if (grown == NULL) {
            /* nothing is drawn yet when the frame's sprites are smoothed,
             * but a sprite smoothed earlier in it may use this one: keep it */
            return;
        }
        hr_released = grown;
        hr_released_capacity = capacity;
    }
    hr_released[hr_released_count++] = pixel;
}

static void hr_release_all(void)
{
    for (size_t i = 0; i < hr_released_count; i++) free(hr_released[i]);
    hr_released_count = 0;
}

static void hr_cache_clear(void)
{
    for (int i = 0; i < HR_CACHE_SLOTS; i++) {
        hr_release(hr_cache[i].image.pixel);
        memset(&hr_cache[i], 0, sizeof(hr_cache[i]));
    }
    hr_cache_bytes = 0;
}

/* image, or image smoothed to n times its size, for a sprite shown scale
 * times larger than it is drawn (n is scale rounded, 2, 3 or 4) */
static const hr_image *hr_smooth(const hr_image *image, double scale)
{
    if (scale < 1.5) return image;
    int n = (scale < 2.5) ? 2 : (scale < 3.5) ? 3 : 4;
    while (n > 1 && (image->w * n > 4 * HR_MAX_SPRITE || image->h * n > 4 * HR_MAX_SPRITE)) n--;
    if (n == 1) return image;

    hr_cached *slot = &hr_cache[(image->hash ^ (uint32_t)n * 0x9E3779B9u) % HR_CACHE_SLOTS];
    if (slot->image.pixel != NULL && slot->hash == image->hash && slot->w == image->w && slot->h == image->h && slot->n == n) {
        return &slot->image;
    }
    size_t size = (size_t)image->w * n * image->h * n * sizeof(uint16_t);
    if (hr_cache_bytes + size > HR_CACHE_BYTES) hr_cache_clear();
    if (slot->image.pixel != NULL) {
        hr_cache_bytes -= (size_t)slot->image.w * slot->image.h * sizeof(uint16_t);
        hr_release(slot->image.pixel);
        slot->image.pixel = NULL;
    }
    uint16_t *pixels = (uint16_t *)malloc(size);
    if (pixels == NULL) return image;
    slot->image.pixel = pixels;
    if (n == 3) {
        hr_scale3x(image, &slot->image);
    } else if (n == 2) {
        hr_scale2x(image, &slot->image);
    } else {
        /* 4x: Scale2x twice */
        hr_image half;
        half.pixel = (uint16_t *)malloc(size / 4);
        if (half.pixel == NULL) {
            free(pixels);
            slot->image.pixel = NULL;
            return image;
        }
        hr_scale2x(image, &half);
        hr_scale2x(&half, &slot->image);
        free(half.pixel);
    }
    slot->hash = image->hash;
    slot->w = image->w;
    slot->h = image->h;
    slot->n = n;
    hr_cache_bytes += size;
    return &slot->image;
}

/* the images a sprite command draws: the list's (it keeps an hr_image and
 * its values after it), or those smoothed */
static void hr_images(const hr_target *t, const hr_list *list, const hr_sprite *sprite, hr_image *images)
{
    for (int i = 0; i < sprite->count; i++) {
        hr_image source;
        memcpy(&source, hr_list_at(list, sprite->image[i]), sizeof(hr_image));
        source.pixel = (uint16_t *)hr_list_at(list, sprite->image[i] + sizeof(hr_image));
        double scale_x = sprite->w * t->k / source.w, scale_y = sprite->h * t->ky / source.h;
        images[i] = t->smoothing ? *hr_smooth(&source, scale_x > scale_y ? scale_x : scale_y) : source;
    }
}

/* the sprites stretched over the rectangle, the later ones over the
 * earlier ones, within the clip rectangle */
static void hr_draw(const hr_target *t, hr_band *band, const hr_sprite *sprite, const hr_image *images)
{
    int k = t->k;
    int width = t->width;
    double left = sprite->left, top = sprite->top, fw = sprite->w, fh = sprite->h;
    int count = sprite->count;

    int cx0 = sprite->clip.x0 * k, cy0 = hr_row(t, sprite->clip.y0);
    int cx1 = sprite->clip.x1 * k, cy1 = hr_row(t, sprite->clip.y1);
    if (cx0 < 0) cx0 = 0;
    if (cx1 > width) cx1 = width;
    int hx0 = (int)ceil(left * k - 0.5), hx1 = (int)ceil((left + fw) * k - 0.5);
    int hy0 = (int)ceil(top * t->ky - 0.5), hy1 = (int)ceil((top + fh) * t->ky - 0.5);
    if (hx0 < cx0) hx0 = cx0;
    if (hy0 < cy0) hy0 = cy0;
    if (hx1 > cx1) hx1 = cx1;
    if (hy1 > cy1) hy1 = cy1;
    if (hy0 < band->y0) hy0 = band->y0;
    if (hy1 > band->y1) hy1 = band->y1;
    if (hy0 >= hy1 || hx0 >= hx1) return;
    const uint8_t *table = sprite->table;
    /* each image's source column for each of H's columns, and source row
     * for each of H's rows, as ((centre - left) / w) * the image's width */
    int line[HR_MAX_UNIT_SPRITES];
    if (hx1 - hx0 > HR_MAX_WIDTH) hx1 = hx0 + HR_MAX_WIDTH;
    for (int hx = hx0; hx < hx1; hx++) {
        double fx = (hr_centre[hx] - left) / fw;
        for (int i = 0; i < count; i++) {
            int sx = (int)(fx * images[i].w);
            if (sx < 0) sx = 0;
            if (sx >= images[i].w) sx = images[i].w - 1;
            band->column[i][hx - hx0] = sx;
        }
    }
    for (int hy = hy0; hy < hy1; hy++) {
        double fy = ((hy + 0.5) / t->ky - top) / fh;
        for (int i = 0; i < count; i++) {
            int sy = (int)(fy * images[i].h);
            if (sy < 0) sy = 0;
            if (sy >= images[i].h) sy = images[i].h - 1;
            line[i] = sy * images[i].w;
        }
        uint8_t *row = t->pixel + (size_t)hy * width;
        uint32_t *out = t->colour != NULL ? hr_colour_at(t, hy) : NULL;
        uint8_t *drawn = t->colour != NULL ? t->drawn + (size_t)hy * width : NULL;
        for (int hx = hx0; hx < hx1; hx++) {
            uint16_t pixel = 0;
            for (int i = 0; i < count; i++) {
                uint16_t value = images[i].pixel[line[i] + band->column[i][hx - hx0]];
                if (value != 0) pixel = value;
            }
            if (pixel == 0) continue;
            uint8_t src = (uint8_t)pixel, dst = row[hx];
            if (drawn != NULL && (sprite->mode != SPRITE_UNIT || src != 0)) drawn[hx] = HR_DRAWN_3D;
            switch (sprite->mode) {
            case SPRITE_OPAQUE:
                row[hx] = src;
                if (out != NULL) out[hx] = t->palette[src];
                break;
            case SPRITE_BLEND:
                row[hx] = table[(dst << 8) | src];
                if (out != NULL) out[hx] = hr_delta(out[hx], t->palette[row[hx]], t->palette[dst]);
                break;
            default:
                /* loc_20314: the buffer's 0 is no pixel; the background's
                 * colours 0xF4..0xFB are seen through the unit */
                if (src == 0) break;
                if (dst >= 0xF4 && dst <= 0xFB) {
                    row[hx] = table[(src << 8) | dst];
                    if (out != NULL) out[hx] = hr_delta(out[hx], t->palette[row[hx]], t->palette[dst]);
                } else {
                    row[hx] = src;
                    if (out != NULL) out[hx] = t->palette[src];
                }
                break;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* The frame, band by band.                                             */

#define HR_MAX_THREADS 8
#define HR_BANDS_PER_THREAD 4

/* the frame being drawn: its list, the target, each sprite command's
 * images (smoothed before the bands start, HR_MAX_UNIT_SPRITES per
 * command), and the next band to take; or, without a list, a picture's
 * rows (hr_cpu_picture) */
static const hr_list *hr_job_list;
static const hr_target *hr_job_target;
static hr_image *hr_job_images;
static size_t hr_job_images_capacity;
static int hr_job_bands;
static SDL_atomic_t hr_job_next;
static const uint8_t *hr_job_picture, *hr_job_rows;
static uint64_t hr_job_pal64[256];
static uint32_t *hr_job_out;

static int hr_threads;
static hr_band hr_bands[HR_MAX_THREADS];
static SDL_sem *hr_start[HR_MAX_THREADS], *hr_finish[HR_MAX_THREADS];

static void hr_draw_band(hr_band *band)
{
    const hr_list *list = hr_job_list;
    const hr_target *target = hr_job_target;
    if (list == NULL) {
        for (int hy = band->y0; hy < band->y1; hy++) {
            if (hr_job_rows[hy]) {
                hr_picture_row(target, hr_job_picture, hr_job_pal64, hy, hr_job_out + (size_t)hy * target->width);
            }
        }
        return;
    }
    for (size_t i = 0; i < list->count; i++) {
        const hr_command *command = &list->command[i];
        switch (command->type) {
        case HR_CMD_BEGIN:
            hr_begin(target, band, (const uint8_t *)hr_list_at(list, command->u.l));
            break;
        case HR_CMD_BLOCKS: {
            const uint32_t *block = (const uint32_t *)hr_list_at(list, command->u.blocks.first);
            for (size_t j = 0; j < command->u.blocks.count; j++) {
                hr_block(target, band, block[j] & 0x1FF, (block[j] >> 9) & 0xFF, (uint8_t)(block[j] >> 24));
            }
            break;
        }
        case HR_CMD_GROUND_ROW:
            hr_ground_draw(target, band, list, &command->u.ground);
            break;
        case HR_CMD_POLYGON:
            hr_fill(target, band, &command->u.polygon, (const uint8_t *)hr_list_at(list, command->u.polygon.shade));
            break;
        case HR_CMD_SPRITE:
            hr_draw(target, band, &command->u.sprite, hr_job_images + i * HR_MAX_UNIT_SPRITES);
            break;
        }
    }
}

/* takes the frame's bands until none is left */
static void hr_work(hr_band *band)
{
    int height = hr_job_target->height;
    for (;;) {
        int b = SDL_AtomicAdd(&hr_job_next, 1);
        if (b >= hr_job_bands) break;
        band->y0 = (int)((long long)height * b / hr_job_bands);
        band->y1 = (int)((long long)height * (b + 1) / hr_job_bands);
        hr_draw_band(band);
    }
}

static int SDLCALL hr_thread(void *data)
{
    int index = (int)(intptr_t)data;
    for (;;) {
        SDL_SemWait(hr_start[index]);
        hr_work(&hr_bands[index]);
        SDL_SemPost(hr_finish[index]);
    }
    return 0;
}

/* the threads that draw: the calling one and, unless GAME_HR_THREADS says
 * how many in all, one for each processor beyond the game's main and
 * display threads, at most HR_DEFAULT_THREADS in all */
#define HR_DEFAULT_THREADS 4

static void hr_threads_start(void)
{
    int count;
    const char *value = getenv(GAME_ENV("HR_THREADS"));
    if (value != NULL && *value != 0) {
        count = atoi(value);
    } else {
        count = SDL_GetCPUCount() - 2;
        if (count > HR_DEFAULT_THREADS) count = HR_DEFAULT_THREADS;
    }
    if (count < 1) count = 1;
    if (count > HR_MAX_THREADS) count = HR_MAX_THREADS;
    hr_threads = 1;
    for (int i = 1; i < count; i++) {
        hr_start[i] = SDL_CreateSemaphore(0);
        hr_finish[i] = SDL_CreateSemaphore(0);
        if (hr_start[i] == NULL || hr_finish[i] == NULL ||
            SDL_CreateThread(hr_thread, "hires", (void *)(intptr_t)i) == NULL) {
            break;
        }
        hr_threads++;
    }
}

void hr_cpu_init(const hr_target *target)
{
    hr_centres(target);
    hr_threads_start();
}

/* the job's bands, on all the threads */
static void hr_run(const hr_list *list, const hr_target *target)
{
    hr_job_list = list;
    hr_job_target = target;
    hr_job_bands = (hr_threads > 1) ? hr_threads * HR_BANDS_PER_THREAD : 1;
    SDL_AtomicSet(&hr_job_next, 0);
    for (int i = 1; i < hr_threads; i++) SDL_SemPost(hr_start[i]);
    hr_work(&hr_bands[0]);
    for (int i = 1; i < hr_threads; i++) SDL_SemWait(hr_finish[i]);
}

void hr_cpu_picture(const hr_target *target, const uint8_t *picture, const uint32_t *palette, const uint8_t *rows,
                    uint32_t *out)
{
    for (int i = 0; i < 256; i++) hr_job_pal64[i] = hr_spread(palette[i]);
    hr_job_picture = picture;
    hr_job_rows = rows;
    hr_job_out = out;
    hr_run(NULL, target);
}

void hr_cpu_draw(const hr_list *list, const hr_target *target)
{
    if (target->colour != NULL) {
        for (int i = 0; i < 256; i++) hr_pal64[i] = hr_spread(target->palette[i]);
        memset(hr_ground_covers, 0, sizeof(hr_ground_covers));
        for (size_t i = 0; i < list->count; i++) {
            const hr_command *command = &list->command[i];
            if (command->type == HR_CMD_GROUND_ROW && command->u.ground.row >= 0 && command->u.ground.row < HR_L_HEIGHT) {
                hr_ground_covers[command->u.ground.row] = 1;
            }
        }
    }

    /* the sprites are smoothed here, on one thread: the cache is not
     * shared by the bands */
    if (hr_job_images_capacity < list->count * HR_MAX_UNIT_SPRITES) {
        size_t capacity = list->count * HR_MAX_UNIT_SPRITES * 2;
        hr_image *grown = (hr_image *)realloc(hr_job_images, capacity * sizeof(hr_image));
        if (grown == NULL) return;
        hr_job_images = grown;
        hr_job_images_capacity = capacity;
    }
    for (size_t i = 0; i < list->count; i++) {
        if (list->command[i].type == HR_CMD_SPRITE) {
            hr_images(target, list, &list->command[i].u.sprite, hr_job_images + i * HR_MAX_UNIT_SPRITES);
        }
    }

    hr_run(list, target);

    hr_release_all();
}
