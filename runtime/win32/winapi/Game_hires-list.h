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

/* The enhanced 3D rendering's frame as a list of commands.  Game_hires.c's
 * hooks record what the game draws, in the game's order and in L's
 * coordinates (320x200, the vertices in fractions of a pixel); a backend
 * draws the list at any size.  Game_hires-cpu.c draws it in palette indices,
 * at 320k x 240k, and with the filtering on in colours as well.
 *
 * A command keeps what it needs that the game may change before the list
 * is drawn (the frame's L, the blocks' values, a ground row's tables, the
 * shade table, the decoded sprites, the part of the ground's bitmap a row
 * reads, into which the game paints the explosions' glow) in the list's own
 * memory.  What stays as the game's pointer is what does not change during
 * a mission: the texture pages and the blend tables. */

#ifndef GAME_HIRES_LIST_H
#define GAME_HIRES_LIST_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define HR_L_WIDTH 320
#define HR_L_HEIGHT 200
#define HR_MAX_VERTICES 16
#define HR_MAX_UNIT_SPRITES 4

enum {
    HR_CMD_BEGIN,       /* the frame starts as L shows it, as blocks */
    HR_CMD_BLOCKS,      /* L's pixels drawn by code H does not follow, as blocks */
    HR_CMD_GROUND_ROW,  /* one row of the ground */
    HR_CMD_POLYGON,     /* a model polygon */
    HR_CMD_SPRITE       /* sprites stretched over a rectangle */
};

/* the polygons' modes */
enum { FILL_FLAT, FILL_FLAT_BLEND, FILL_TEXTURE, FILL_TEXTURE_KEY, FILL_TEXTURE_BLEND };

/* the sprites' modes: drawn as they are, through a table [dst*256 + src],
 * or as loc_20314 lays a unit's buffer over the background */
enum { SPRITE_OPAQUE, SPRITE_BLEND, SPRITE_UNIT };

/* L's rectangle [x0, x1) x [y0, y1) */
typedef struct { int x0, y0, x1, y1; } hr_clip;

/* a point in L's pixels, where pixel (x, y) has its centre at (x + 0.5,
 * y + 0.5), and its texel */
typedef struct { double x, y, u, v; } hr_vertex;

/* a sprite: w x h values, 0x100 | colour, 0 where nothing is drawn */
typedef struct {
    int w, h;
    uint32_t hash;          /* of w, h and the values */
    uint16_t *pixel;
} hr_image;

/* loc_39010's ground row r: L's rows r - 1, r and r + 1 as loc_3CC08 reads
 * them (span 0 where the game skips the row), the frame's offsets, and the
 * part of the ground's bitmap the row can read: the bitmap's rows first_row
 * to first_row + rows - 1 (before they wrap at its height), each from
 * column first_column, columns wide.  The tables are the projection's
 * values at rows rounded to whole pixels and linear in between; with
 * exact set, the camera's c, s and h give its exact values instead:
 * the source y at the view's row y is h (c - q s) / (s + q c), with
 * q = (y - 100) / 256, and its span (-y c - h s) / 4 */
typedef struct {
    int row;
    int exact;
    int32_t c, s, h;
    int32_t span[3], x[3], y[3];
    int32_t fraction_x, wrap_test, offset_x, offset_y, pitch, height;
    int32_t first_row, rows, first_column, columns;
    size_t bitmap;              /* the list's copy, rows x columns */
} hr_ground;

/* with g->exact: the source y at the view's row y (the top of row 0 at 0,
 * L's pixels), and the span at a source y */
static inline double hr_ground_y(const hr_ground *g, double y)
{
    double q = (y - 100) / 256;
    return (double)g->h * (g->c - q * g->s) / (g->s + q * g->c);
}

static inline double hr_ground_span(const hr_ground *g, double source_y)
{
    return (-source_y * g->c - (double)g->h * g->s) / 4;
}

typedef struct {
    int n, mode;
    uint8_t colour;
    hr_vertex p[HR_MAX_VERTICES];
    const uint8_t *texture;     /* 256 texels per row */
    size_t shade;               /* the list's copy of the 256-byte shade table */
    const uint8_t *blend;
    hr_clip clip;
} hr_polygon;

typedef struct {
    int count, mode;
    size_t image[HR_MAX_UNIT_SPRITES];  /* the list's hr_image, the later over the earlier */
    double left, top, w, h;             /* in L's pixels */
    const uint8_t *table;
    hr_clip clip;
} hr_sprite;

typedef struct {
    int type;
    union {
        size_t l;                                   /* HR_CMD_BEGIN: the list's copy of L */
        struct { size_t first, count; } blocks;     /* HR_CMD_BLOCKS: (x | y << 9 | value << 24) */
        hr_ground ground;
        hr_polygon polygon;
        hr_sprite sprite;
    } u;
} hr_command;

typedef struct {
    hr_command *command;
    size_t count, capacity;
    uint8_t *data;          /* the commands' own memory, by offset */
    size_t size, data_capacity;
} hr_list;

/* the commands' own memory: an offset, valid until the list is cleared
 * (a pointer into it is valid until the next allocation) */
size_t hr_list_alloc(hr_list *list, size_t size);
static inline void *hr_list_at(const hr_list *list, size_t offset)
{
    return list->data + offset;
}

/* H as the CPU backend draws it: k pixels across and 1.2k rows down per L
 * pixel; L's row y is H's rows [row0[y], row0[y + 1]) */
typedef struct {
    uint8_t *pixel;
    int k, width, height;
    double ky;
    const int *row0;
    int smoothing;          /* sprites shown larger than they are drawn are smoothed */
    uint32_t *colour;       /* NULL, or H in colours too, filtered */
    const uint32_t *palette;    /* the colours' palette */
    uint8_t *drawn;         /* with colour, what drew H's pixel: HR_DRAWN_* */
} hr_target;

/* the frame's start (L as it was), the 3D, L's blocks drawn over the 3D */
enum { HR_DRAWN_START, HR_DRAWN_3D, HR_DRAWN_OVER };

/* the filtering samples L bilinearly at H's pixels' centres: along an axis
 * of n of L's pixels, s of H's pixels each, H's pixel h takes L's pixel *i
 * and, by *w / 256, the next one */
static inline void hr_bilinear_at(int h, double s, int n, int *i, unsigned *w)
{
    double u = (h + 0.5) / s - 0.5;
    int x = (int)floor(u);
    unsigned f = (unsigned)((u - x) * 256);
    if (x < 0) x = 0, f = 0;
    if (x >= n - 1) x = n - 2, f = 255;
    *i = x;
    *w = f;
}

#ifdef __cplusplus
extern "C" {
#endif

/* Game_hires-cpu.c: draws the list into the target (one call at a time,
 * and not during hr_cpu_picture) */
void hr_cpu_init(const hr_target *target);
void hr_cpu_draw(const hr_list *list, const hr_target *target);
/* a 320x200 picture as the filtering shows it, in colours: H's rows hy
 * where rows[hy] is set, into out's */
void hr_cpu_picture(const hr_target *target, const uint8_t *picture, const uint32_t *palette, const uint8_t *rows,
                    uint32_t *out);

#ifdef __cplusplus
}
#endif

#endif
