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


/* The enhanced 3D rendering: the mission's 3D view drawn a second time, at
 * a higher resolution, beside the game's own 320x200 frame.
 *
 * The game keeps drawing its frame (L, the back buffer) exactly as before.
 * Hooks in its renderer (instruction_replacements.sci, all through
 * game_hr_hook) record the same ground rows, polygons and sprites in the
 * frame's command list (Game_hires-list.h), from inputs that carry more
 * precision than 320x200 uses: the ground's per-row tables, the points in
 * 1/16 pixel, and the sprites at their own resolution.  What the game draws
 * over the 3D view in 320x200 (the HUD, text, the arrows) is found by
 * comparing L with M, a copy of L as the list knows it, and goes into the
 * list as blocks.
 *
 * When the game copies L to the screen, the list is drawn into H by the CPU
 * backend (Game_hires-cpu.c): 320k x 240k palette indices, 4:3, the shape
 * mode 13h has on the monitor, so that H goes to the window without being
 * stretched again.  L's pixel is k pixels of H wide and 1.2k high; L's row y
 * is H's rows [hr_row0[y], hr_row0[y + 1]).  H is published as P, with S,
 * the L it belongs to.  The flip shows P wherever the screen still holds S,
 * and the screen's own pixels, as blocks, everywhere else (the HUD drawn
 * straight to the screen, the pause's panel).  A screen that has little of S
 * left (a movie, a picture) is shown as it is.
 *
 * With Display_3D_Filtering, H is drawn in colours as well, with the frame's
 * start, the ground and the textures filtered; P is then shown in colours,
 * the palette's of the flip before (the game cycles the palette's water
 * colours every frame or two, so a P drawn with the palette before is still
 * shown in colours once, a frame late; an older P only while the palette is
 * the same, as a fade over a still picture changes nothing but the
 * palette).  The screen's own pixels and the blocks over the 3D are then
 * shown filtered too, over P (hr_flip_filtered). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <SDL.h>

#include "Game_defs.h"
#include "Game_vars.h"
#include "Game_hires.h"
#include "Game_hires-list.h"
#include "Game_runtime.h"
#include "Game_hooks.h"

/* llasm's _cpu (llasm/llasm_cpu.h), the part read here */
typedef struct {
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi, eflags;
} hr_cpu;

enum {
    HR_FRAME_BEGIN = 1,     /* loc_12D10: the frame's back buffer in eax */
    HR_GROUND_ROW = 2,      /* loc_394F7: ground row edi drawn (or skipped) */
    HR_ENTRY_BEFORE = 3,    /* loc_3C5CB: list entry esi, its first dword in ecx */
    HR_ENTRY_AFTER = 4,     /* loc_3C604: the entry is drawn */
    HR_PUBLISH_VIEW = 5,    /* loc_3AA7A: 168 rows go to the screen */
    HR_PUBLISH_FULL = 6,    /* loc_3AA8F: 200 rows go to the screen */
    HR_PUBLISHED = 7,       /* loc_3AA8D, loc_3AAA2: the copy is done */
    HR_SPRITE_SCALED = 8,   /* loc_3F2AA: a sprite scaled to ebx x ecx at (eax, edx) */
    HR_SPRITE = 9,          /* loc_398C7: sprite ebx at (eax, edx) */
    HR_UNIT = 10,           /* loc_20314: a unit's buffer laid over the background */
    HR_SCREEN_BEGIN = 11,   /* loc_37390: a 3D screen's frame, in the cleared back buffer */
    HR_MENU_LIST = 12       /* loc_3A8CE: a menu's 3D list, over what the back buffer holds */
};

#define L_WIDTH HR_L_WIDTH
#define L_HEIGHT HR_L_HEIGHT
#define L_SIZE (L_WIDTH * L_HEIGHT)

/* the game's variables */
#define G_BACK_BUFFER 0x9862Cu      /* the rasterizers' target */
#define G_TEXTURE_PTR 0x98655u      /* [+4i]: texture page i, 256 texels wide */
#define G_TEXTURE_KIND 0x99655u     /* [+i]: 0 opaque, else texel 0 is not drawn */
#define G_TEXTURE_WIDE 0x971F8u     /* 1: textures are 256x32, else 64x64 */
#define G_BLEND_TABLE 0xEED7Cu      /* 256x256, [dst*256 + src] */
#define G_CLIP_Y0 0xF3D14u
#define G_CLIP_Y1 0xF3D18u
#define G_CLIP_X0 0xF3D1Cu
#define G_CLIP_X1 0xF3D20u
#define G_PITCH 0xF3D24u
#define G_SPRITE_BLEND 0xEED74u     /* the sprites' 256x256 blend table */
#define G_SPRITE_X0 0xF0310u        /* the sprites' clip rectangle */
#define G_SPRITE_X1 0xF0314u
#define G_SPRITE_Y0 0xF0318u
#define G_SPRITE_Y1 0xF031Cu
#define G_SPRITE_PITCH 0xF0374u
#define G_PLAIN_BUFFER 0x93CD4u     /* loc_398C7's target */
#define G_PLAIN_X1 0x97DA0u         /* loc_398C7's clip: [0, x1) x [0, y1) */
#define G_PLAIN_PITCH 0x97DA4u
#define G_PLAIN_Y1 0x97DA8u
#define G_GROUND_SPAN 0xEFEE0u      /* [+4r]: source span across the row, 16.16 */
#define G_GROUND_X 0xEF8A0u         /* [+4r]: source x of the row's start, 16.16 */
#define G_GROUND_Y 0xEFBC0u         /* [+4r]: source y, 20 per source row */
#define G_GROUND_COS 0xB25EDu       /* [+8a], [+8a+4]: the camera's angle a's c and s, 1 = 0x4000 */

static int hr_factor = -1;          /* k: H's pixels per L's pixel across */
static int hr_row0[L_HEIGHT + 1];   /* H's first row of L's row y, round(y * 1.2k) */
/* H's row hy shows L's rows hr_mix_y[hy] and, by hr_mix_w[hy] / 256 of its
 * height, the next one: the screen's pixels are blended over the rows where
 * two of L's meet, and keep their height (the flip) */
static int hr_mix_y[240 * GAME_HIRES_MAX_FACTOR], hr_mix_w[240 * GAME_HIRES_MAX_FACTOR];
static hr_target hr_h;              /* 320k x 240k: this frame */
static uint8_t *hr_p;               /* 320k x 240k: the published frame */
static uint8_t hr_m[L_SIZE];        /* L as the list knows it */
static uint8_t hr_s[L_SIZE];        /* L as P shows it */
static int hr_s_rows;               /* rows of S that went to the screen */
static SDL_mutex *hr_lock;
static int hr_locked;
static int hr_active;               /* between frame begin and publish */
static uint8_t *hr_l;               /* L */
/* the frame being recorded is one list; the frame published before it,
 * the other one, is drawn meanwhile on the drawing thread */
static hr_list hr_lists[2];
static hr_list *hr_frame = &hr_lists[0];
static SDL_cond *hr_drawn;          /* signalled when a published frame is drawn */
static SDL_sem *hr_draw;            /* posted when a frame is published */
static int hr_pending;              /* a published frame is being drawn (under hr_lock) */
static int hr_pending_rows;
static hr_list *hr_pending_list;
static uint8_t hr_pending_s[L_SIZE]; /* the L it belongs to */
/* the filtered colours: H's in hr_h.colour, drawn with hr_draw_palette */
static uint32_t *hr_p_colour;       /* P in colours */
static int hr_p_coloured;           /* P is drawn in colours (under hr_lock) */
static int hr_p_fresh;              /* and not yet shown */
static uint32_t hr_draw_palette[256];
static uint32_t hr_p_palette[256];  /* the palette P was drawn with */
static uint32_t hr_flip_palette[256]; /* the flip's latest palette */
static SDL_SpinLock hr_palette_lock;
static uint8_t *hr_p_drawn;         /* P's pixels the 3D drew (hr_target's drawn) */
/* With the filtering, the flip shows the screen's 2D filtered over P (the
 * frame's start, filtered, and the 3D): the screen as the filtering shows
 * it (its rows made from the screen's rows and the palette, kept until
 * those change), where the filtering samples it, and which of L's pixels
 * are 2D over P */
static uint32_t *hr_picture;
static uint8_t hr_picture_l[L_SIZE];
static uint32_t hr_picture_palette[256];
static uint8_t hr_picture_valid[240 * GAME_HIRES_MAX_FACTOR];
static int hr_pick_x[L_WIDTH * GAME_HIRES_MAX_FACTOR], hr_pick_y[240 * GAME_HIRES_MAX_FACTOR];
static unsigned hr_pick_wx[L_WIDTH * GAME_HIRES_MAX_FACTOR], hr_pick_wy[240 * GAME_HIRES_MAX_FACTOR];
static uint8_t hr_over[L_SIZE], hr_over_row[L_HEIGHT], hr_over_whole[L_HEIGHT];
static uint8_t hr_picture_make[240 * GAME_HIRES_MAX_FACTOR];

/* the list entry, as it was before its type routine changed it */
static struct {
    int valid;
    uint32_t type, count, data, shade;
    int32_t x[HR_MAX_VERTICES], y[HR_MAX_VERTICES];
    /* a sprite entry: up to four sprites at the point (x[0], y[0]), scaled
     * by q / 256 when they are scaled; what they drew into L */
    uint32_t sprite[4], q;
    int bx0, by0, bx1, by1;
} hr_entry;

/* the sprites loc_20094 scaled into a unit's buffer, in the list */
static size_t hr_unit_images[HR_MAX_UNIT_SPRITES];
static int hr_units;

static inline uint8_t *guest(uint32_t address)
{
#ifdef PTROFS_64BIT
    return (uint8_t *)(uintptr_t)((uint64_t)address + pointer_offset);
#else
    return (uint8_t *)(uintptr_t)address;
#endif
}

static inline uint32_t data32(uint32_t flat)
{
    uint32_t value;
    memcpy(&value, Game_Obj2Pointer(flat), 4);
    return value;
}

static inline uint8_t data8(uint32_t flat)
{
    return *(uint8_t *)Game_Obj2Pointer(flat);
}

static inline uint32_t guest32(uint32_t address)
{
    uint32_t value;
    memcpy(&value, guest(address), 4);
    return value;
}

extern "C" int Game_HiresFactor(void)
{
    if (hr_factor >= 0) return hr_factor;

    int factor = Game_UseEnhanced3DEngine ? Game_Enhanced3DFactor : 0;
    const char *value = getenv(GAME_ENV("ENHANCED_3D"));
    if (value != NULL && value[0] != 0) {
        if (strcasecmp(value, "off") == 0 || strcmp(value, "0") == 0) factor = 0;
        else if (strcasecmp(value, "on") == 0 || strcmp(value, "1") == 0) factor = Game_Enhanced3DFactor;
        else factor = atoi(value);
    }
    if (factor < 2) factor = 0;
    if (factor > GAME_HIRES_MAX_FACTOR) factor = GAME_HIRES_MAX_FACTOR;
    hr_factor = factor;
    return hr_factor;
}

extern "C" int Game_HiresHeight(void)
{
    return hr_h.height;
}

static int SDLCALL hr_drawing(void *data);

extern "C" int Game_HiresInit(void)
{
    int k = Game_HiresFactor();
    if (k == 0) return 0;
    hr_h.k = k;
    hr_h.ky = 1.2 * k;
    for (int y = 0; y <= L_HEIGHT; y++) hr_row0[y] = (int)floor(y * hr_h.ky + 0.5);
    hr_h.row0 = hr_row0;
    hr_h.width = L_WIDTH * k;
    hr_h.height = hr_row0[L_HEIGHT];
    for (int hy = 0; hy < hr_h.height; hy++) {
        int y = (int)floor(hy / hr_h.ky);
        double next = (y + 1) * hr_h.ky;   /* where L's next row starts, in H's rows */
        hr_mix_y[hy] = y;
        hr_mix_w[hy] = (next < hy + 1) ? (int)floor((hy + 1 - next) * 256 + 0.5) : 0;
    }
    /* the config file's Display_3D_Sprite_Smoothing, which GAME_HR_SMOOTH
     * (0 or 1) overrides */
    hr_h.smoothing = Game_SpriteSmoothing;
    const char *value = getenv(GAME_ENV("HR_SMOOTH"));
    if (value != NULL && value[0] != 0) hr_h.smoothing = strcasecmp(value, "on") == 0 || atoi(value) != 0;
    /* Display_3D_Filtering, which GAME_HR_FILTER (0 or 1) overrides */
    int filtering = Game_3DFiltering;
    value = getenv(GAME_ENV("HR_FILTER"));
    if (value != NULL && value[0] != 0) filtering = strcasecmp(value, "on") == 0 || atoi(value) != 0;

    hr_h.pixel = (uint8_t *)calloc((size_t)hr_h.width * hr_h.height, 1);
    hr_p = (uint8_t *)calloc((size_t)hr_h.width * hr_h.height, 1);
    if (filtering) {
        size_t size = (size_t)hr_h.width * hr_h.height;
        hr_h.colour = (uint32_t *)calloc(size, 4);
        hr_p_colour = (uint32_t *)calloc(size, 4);
        hr_picture = (uint32_t *)calloc(size, 4);
        hr_h.drawn = (uint8_t *)calloc(size, 1);
        hr_p_drawn = (uint8_t *)calloc(size, 1);
        if (hr_h.colour == NULL || hr_p_colour == NULL || hr_picture == NULL || hr_h.drawn == NULL || hr_p_drawn == NULL) {
            free(hr_h.colour);
            free(hr_p_colour);
            free(hr_picture);
            free(hr_h.drawn);
            free(hr_p_drawn);
            hr_h.colour = hr_p_colour = hr_picture = NULL;
            hr_h.drawn = hr_p_drawn = NULL;
        }
        hr_h.palette = hr_draw_palette;
        for (int hx = 0; hx < hr_h.width; hx++) hr_bilinear_at(hx, k, L_WIDTH, &hr_pick_x[hx], &hr_pick_wx[hx]);
        for (int hy = 0; hy < hr_h.height; hy++) hr_bilinear_at(hy, hr_h.ky, L_HEIGHT, &hr_pick_y[hy], &hr_pick_wy[hy]);
    }
    hr_cpu_init(&hr_h);
    hr_lock = SDL_CreateMutex();
    hr_drawn = SDL_CreateCond();
    hr_draw = SDL_CreateSemaphore(0);
    if (hr_h.pixel == NULL || hr_p == NULL || hr_lock == NULL || hr_drawn == NULL || hr_draw == NULL ||
        SDL_CreateThread(hr_drawing, "hires", NULL) == NULL) {
        free(hr_h.pixel);
        free(hr_p);
        free(hr_h.colour);
        free(hr_p_colour);
        hr_h.pixel = hr_p = NULL;
        hr_h.colour = hr_p_colour = NULL;
        hr_factor = 0;
        return -1;
    }
    fprintf(stderr, GAME_NAME ": enhanced 3D rendering, %d x %d%s\n", hr_h.width, hr_h.height,
            hr_h.colour != NULL ? ", filtered" : "");
    return 0;
}

/* ------------------------------------------------------------------ */
/* The frame's list                                                     */

size_t hr_list_alloc(hr_list *list, size_t size)
{
    size = (size + 7) & ~(size_t)7;
    if (list->size + size > list->data_capacity) {
        size_t capacity = list->data_capacity ? list->data_capacity : (1u << 20);
        while (list->size + size > capacity) capacity *= 2;
        uint8_t *data = (uint8_t *)realloc(list->data, capacity);
        if (data == NULL) abort();
        list->data = data;
        list->data_capacity = capacity;
    }
    size_t offset = list->size;
    list->size += size;
    return offset;
}

static hr_command *hr_add(int type)
{
    hr_list *list = hr_frame;
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? 2 * list->capacity : 1024;
        hr_command *command = (hr_command *)realloc(list->command, capacity * sizeof(hr_command));
        if (command == NULL) abort();
        list->command = command;
        list->capacity = capacity;
    }
    hr_command *command = &list->command[list->count++];
    command->type = type;
    return command;
}

/* ------------------------------------------------------------------ */

/* Whatever L got from code the list does not follow goes into it as
 * blocks. */
static void hr_sync(void)
{
    size_t count = 0;
    for (int y = 0; y < L_HEIGHT; y++) {
        const uint8_t *l = hr_l + y * L_WIDTH, *m = hr_m + y * L_WIDTH;
        if (memcmp(l, m, L_WIDTH) == 0) continue;
        for (int x = 0; x < L_WIDTH; x++) count += l[x] != m[x];
    }
    if (count == 0) return;
    hr_command *command = hr_add(HR_CMD_BLOCKS);
    command->u.blocks.first = hr_list_alloc(hr_frame, count * sizeof(uint32_t));
    command->u.blocks.count = count;
    uint32_t *block = (uint32_t *)hr_list_at(hr_frame, command->u.blocks.first);
    for (int y = 0; y < L_HEIGHT; y++) {
        const uint8_t *l = hr_l + y * L_WIDTH;
        uint8_t *m = hr_m + y * L_WIDTH;
        if (memcmp(l, m, L_WIDTH) == 0) continue;
        for (int x = 0; x < L_WIDTH; x++) {
            if (l[x] != m[x]) {
                *block++ = (uint32_t)x | (uint32_t)y << 9 | (uint32_t)l[x] << 24;
                m[x] = l[x];
            }
        }
    }
}

/* M takes L's rectangle [x0, x1) x [y0, y1): the list drew the same there. */
static void hr_known(int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > L_WIDTH) x1 = L_WIDTH;
    if (y1 > L_HEIGHT) y1 = L_HEIGHT;
    for (int y = y0; y < y1 && x0 < x1; y++) {
        memcpy(hr_m + y * L_WIDTH + x0, hr_l + y * L_WIDTH + x0, x1 - x0);
    }
}

static void hr_frame_begin(uint32_t back_buffer)
{
    hr_l = guest(back_buffer);
    hr_entry.valid = 0;
    hr_units = 0;
    hr_frame->count = 0;
    hr_frame->size = 0;
    if (hr_l == NULL || data32(G_PITCH) != L_WIDTH) {
        hr_active = 0;
        return;
    }
    hr_active = 1;
    memcpy(hr_m, hr_l, L_SIZE);
    size_t l = hr_list_alloc(hr_frame, L_SIZE);
    memcpy(hr_list_at(hr_frame, l), hr_l, L_SIZE);
    hr_add(HR_CMD_BEGIN)->u.l = l;
}

/* ------------------------------------------------------------------ */
/* The ground (loc_39010): row r of the view is one row of the level's  */
/* ground bitmap, sampled by loc_3CC08 with the per-row tables, which   */
/* the backend interpolates between rows, or with the projection the    */
/* tables come from.                                                    */

static void hr_ground_row(const hr_cpu *cpu)
{
    int r = (int)cpu->edi;
    if (r < 0 || r >= L_HEIGHT) return;
    if (data32(G_GROUND_SPAN + 4 * r) == 0) return;    /* the game skipped the row */

    /* loc_39010's frame: [esp+8] the x offset, [esp+0x10] its fraction,
     * [esp+0x14] = [ebp+0x1c] << 4, [esp+0x18] = [esp+8] / 20, [esp+0x30]
     * the y offset; [ebp+0x10] the bitmap's pitch, [ebp+0x14] its height,
     * [ebp+0x18] its address */
    uint32_t esp = cpu->esp, ebp = cpu->ebp;
    int32_t pitch = (int32_t)guest32(ebp + 0x10);
    int32_t height = (int32_t)guest32(ebp + 0x14);
    if (pitch <= 0 || height <= 0) return;

    hr_ground *g = &hr_add(HR_CMD_GROUND_ROW)->u.ground;
    g->row = r;
    for (int i = 0; i < 3; i++) {
        int row = r - 1 + i;
        int inside = row >= 0 && row < L_HEIGHT;
        g->span[i] = inside ? (int32_t)data32(G_GROUND_SPAN + 4 * row) : 0;
        g->x[i] = inside ? (int32_t)data32(G_GROUND_X + 4 * row) : 0;
        g->y[i] = inside ? (int32_t)data32(G_GROUND_Y + 4 * row) : 0;
    }
    g->fraction_x = (int32_t)guest32(esp + 0x10);
    g->wrap_test = (int32_t)guest32(esp + 0x14);
    g->offset_x = (int32_t)guest32(esp + 0x18);
    g->offset_y = (int32_t)guest32(esp + 0x30);
    g->pitch = pitch;
    g->height = height;
    /* [ebp+0x18] points into the bitmap, not at its start: rows above it
     * come out negative, as in the game */
    const uint8_t *bitmap = guest(guest32(ebp + 0x18));

    /* loc_39062's projection: [esp+0xc] the camera's angle, [esp+0x2c]
     * its height.  It is used when it puts the game's own row r within
     * two pixels and a half of where it is (the game's rows are off by up
     * to a pixel and a half: it rounds them to whole pixels toward 100) */
    int32_t angle = (int32_t)guest32(esp + 0xc);
    g->exact = 0;
    if (angle >= 0 && angle < 0x1000) {
        g->c = (int32_t)data32(G_GROUND_COS + 8 * angle);
        g->s = (int32_t)data32(G_GROUND_COS + 8 * angle + 4);
        g->h = (int32_t)guest32(esp + 0x2c);
        double length = sqrt((double)g->c * g->c + (double)g->s * g->s);
        if (g->h != 0 && fabs(length - 0x4000) < 0x100 && g->s + (L_HEIGHT - 100) / 256.0 * g->c > 0 &&
            g->s - 100 / 256.0 * g->c > 0) {
            double q = (g->y[1] * (double)g->s - (double)g->h * g->c) / (-g->y[1] * (double)g->c - (double)g->h * g->s);
            g->exact = fabs(100 + 256 * q - (r + 0.5)) < 2.5;
        }
    }

    /* What the row's samples can reach (Game_hires-cpu.c's hr_ground_draw):
     * a source y between the rows' y, and a source x from the rows' x,
     * less one pixel for loc_3CC08's lag, to 320.5 spans on; exact, from
     * half a row above to half a row below. */
    int32_t near_y[5], near_x[5], near_span[5];
    for (int i = 0; i < 3; i++) {
        near_y[i] = g->y[i];
        near_x[i] = g->x[i];
        near_span[i] = g->span[i];
    }
    for (int i = 3; i < 5; i++) {
        near_span[i] = 0;
        if (!g->exact) continue;
        double y = hr_ground_y(g, i == 3 ? r - 0.5 : r + 1.5);
        near_y[i] = (int32_t)floor(y + 0.5);
        near_span[i] = (int32_t)floor(hr_ground_span(g, y) + 0.5);
        near_x[i] = -(near_span[i] / 2);
    }
    double y_min = g->y[1], y_max = g->y[1], x_min = 0, x_max = 0;
    int first = 1;
    for (int i = 0; i < 5; i++) {
        if (near_span[i] == 0) continue;
        double x0 = (double)near_x[i] + g->fraction_x;
        double a = x0 + 0.5 * near_span[i] / L_WIDTH, b = x0 + 320.5 * near_span[i] / L_WIDTH;
        double lo = (a < b ? a : b) - 65536.0, hi = a < b ? b : a;
        if (first || lo < x_min) x_min = lo;
        if (first || hi > x_max) x_max = hi;
        if (near_y[i] < y_min) y_min = near_y[i];
        if (near_y[i] > y_max) y_max = near_y[i];
        first = 0;
    }
    g->first_row = (int32_t)floor((y_min + g->offset_y) / 20.0);
    g->rows = (int32_t)floor((y_max + g->offset_y) / 20.0) - g->first_row + 1;
    g->first_column = g->offset_x + (int32_t)floor(x_min / 65536.0);
    g->columns = g->offset_x + 1 + (int32_t)floor(x_max / 65536.0) - g->first_column + 1;
    if (g->rows < 1 || g->rows > 4096 || g->columns < 1 || g->columns > 65536) {
        hr_frame->count--;
        return;
    }
    g->bitmap = hr_list_alloc(hr_frame, (size_t)g->rows * g->columns);
    uint8_t *copy = (uint8_t *)hr_list_at(hr_frame, g->bitmap);
    for (int i = 0; i < g->rows; i++) {
        /* loc_3948D's wrap at the bitmap's height */
        int32_t row = g->first_row + i;
        if (row + g->wrap_test >= height) row -= height;
        memcpy(copy + (size_t)i * g->columns, bitmap + (ptrdiff_t)row * pitch + g->first_column, g->columns);
    }
    hr_known(0, r, L_WIDTH, r + 1);
}

/* ------------------------------------------------------------------ */
/* The sprites of the list's sprite entries.  A sprite is a word width  */
/* and height, then each row as runs of a skip byte and a count byte    */
/* with count pixels, ended by 0xFF.  loc_398C7 draws one as it is,     */
/* loc_3F2AA scales it to a rectangle (opaque, or through a table as    */
/* [dst*256 + src]), and loc_20094 scales a unit's sprites into a       */
/* buffer of its own that loc_20314 lays over the background.  The list */
/* takes each at its own resolution, in the rectangle the entry's point */
/* and scale give before the game rounds them.                          */

#define HR_MAX_SPRITE 512

/* the sprite at address, decoded into the list: an hr_image and its
 * values after it; (size_t)-1 when it is not a sprite */
static size_t hr_decode(uint32_t address)
{
    const uint8_t *sprite = guest(address);
    if (sprite == NULL) return (size_t)-1;
    int w = sprite[0] | (sprite[1] << 8), h = sprite[2] | (sprite[3] << 8);
    if (w < 1 || h < 1 || w > HR_MAX_SPRITE || h > HR_MAX_SPRITE) return (size_t)-1;
    size_t at = hr_list_alloc(hr_frame, sizeof(hr_image) + (size_t)w * h * sizeof(uint16_t));
    hr_image *image = (hr_image *)hr_list_at(hr_frame, at);
    uint16_t *pixel = (uint16_t *)(image + 1);
    image->w = w;
    image->h = h;
    image->pixel = NULL;
    memset(pixel, 0, (size_t)w * h * sizeof(uint16_t));
    const uint8_t *p = sprite + 4;
    for (int y = 0; y < h; y++) {
        uint16_t *row = pixel + y * w;
        int x = 0;
        for (;;) {
            uint8_t skip = *p++;
            if (skip == 0xFF) break;
            int count = *p++;
            x += skip;
            for (int i = 0; i < count; i++, x++) {
                if (x < w) row[x] = 0x100 | p[i];
            }
            p += count;
        }
    }
    uint32_t hash = 2166136261u ^ (uint32_t)(w << 16 | h);
    for (int i = 0; i < w * h; i++) hash = (hash ^ pixel[i]) * 16777619u;
    image->hash = hash;
    return at;
}

static inline int hr_image_w(size_t image)
{
    return ((const hr_image *)hr_list_at(hr_frame, image))->w;
}

static inline int hr_image_h(size_t image)
{
    return ((const hr_image *)hr_list_at(hr_frame, image))->h;
}

/* The rectangle (x, y, w, h) a sprite routine got, in L, as the list
 * draws it: the size before rounding when it is one of the entry's
 * sprites scaled, and the position from the entry's point in 1/16 pixel
 * when the game centred the rectangle on it (or stood it on it). */
static void hr_rectangle(hr_sprite *s, int x, int y, int w, int h)
{
    s->w = w;
    s->h = h;
    uint32_t q = hr_entry.q;
    for (int i = 0; i < 4 && q != 0; i++) {
        const uint8_t *sprite = guest(hr_entry.sprite[i]);
        if (sprite == NULL) continue;
        int sw = sprite[0] | (sprite[1] << 8), sh = sprite[2] | (sprite[3] << 8);
        if ((int)((sw * q) >> 8) == w && (int)((sh * q) >> 8) == h) {
            s->w = sw * (q / 256.0);
            s->h = sh * (q / 256.0);
            break;
        }
    }
    int32_t px = hr_entry.x[0], py = hr_entry.y[0];
    if (x == (px >> 4) - (w >> 1)) s->left = px / 16.0 - s->w / 2;
    else s->left = x;
    if (y == (py >> 4) - (h >> 1)) s->top = py / 16.0 - s->h / 2;
    else if (y == (py >> 4) - h) s->top = py / 16.0 - s->h;
    else s->top = y;
}

static hr_sprite *hr_add_sprite(int mode, const uint8_t *table, int x0, int y0, int x1, int y1)
{
    hr_sprite *s = &hr_add(HR_CMD_SPRITE)->u.sprite;
    s->count = 0;
    s->mode = mode;
    s->table = table;
    s->clip.x0 = x0;
    s->clip.y0 = y0;
    s->clip.x1 = x1;
    s->clip.y1 = y1;
    return s;
}

static void hr_entry_drew(int x, int y, int w, int h)
{
    if (x < hr_entry.bx0) hr_entry.bx0 = x;
    if (y < hr_entry.by0) hr_entry.by0 = y;
    if (x + w > hr_entry.bx1) hr_entry.bx1 = x + w;
    if (y + h > hr_entry.by1) hr_entry.by1 = y + h;
}

static int hr_in_sprite_entry(void)
{
    return hr_entry.valid && hr_entry.type == 0x80;
}

/* loc_3F2AA's entry: [esp+4] the sprite, [esp+8] the target, [esp+0xc]
 * the table (0: opaque), [esp+0x10] the target the table reads */
static void hr_sprite_scaled(const hr_cpu *cpu)
{
    if (!hr_in_sprite_entry()) return;
    int x = (int32_t)cpu->eax, y = (int32_t)cpu->edx, w = (int32_t)cpu->ebx, h = (int32_t)cpu->ecx;
    uint32_t sprite = guest32(cpu->esp + 4), target = guest32(cpu->esp + 8);
    uint32_t table = guest32(cpu->esp + 0xc), source = guest32(cpu->esp + 0x10);
    if (w < 2 || h < 2) return;

    if (guest(target) != hr_l) {
        /* loc_20094 scales a unit's sprites into its own buffer */
        if (x != 0 || y != 0 || table != 0 || hr_units >= HR_MAX_UNIT_SPRITES) return;
        size_t image = hr_decode(sprite);
        if (image != (size_t)-1) hr_unit_images[hr_units++] = image;
        return;
    }
    if (source != target || data32(G_SPRITE_PITCH) != L_WIDTH) return;
    size_t image = hr_decode(sprite);
    if (image == (size_t)-1) return;
    hr_sprite *s = hr_add_sprite(table != 0 ? SPRITE_BLEND : SPRITE_OPAQUE, guest(table),
                                 (int32_t)data32(G_SPRITE_X0), (int32_t)data32(G_SPRITE_Y0),
                                 (int32_t)data32(G_SPRITE_X1) + 1, (int32_t)data32(G_SPRITE_Y1) + 1);
    s->image[s->count++] = image;
    hr_rectangle(s, x, y, w, h);
    hr_entry_drew(x, y, w, h);
}

/* loc_398C7's entry: sprite ebx at (eax, edx), as it is */
static void hr_sprite_plain(const hr_cpu *cpu)
{
    if (!hr_in_sprite_entry()) return;
    if (guest(data32(G_PLAIN_BUFFER)) != hr_l || data32(G_PLAIN_PITCH) != L_WIDTH) return;
    size_t image = hr_decode(cpu->ebx);
    if (image == (size_t)-1) return;
    int x = (int32_t)cpu->eax, y = (int32_t)cpu->edx, w = hr_image_w(image), h = hr_image_h(image);
    hr_sprite *s = hr_add_sprite(SPRITE_OPAQUE, NULL, 0, 0, (int32_t)data32(G_PLAIN_X1), (int32_t)data32(G_PLAIN_Y1));
    s->image[s->count++] = image;
    hr_rectangle(s, x, y, w, h);
    hr_entry_drew(x, y, w, h);
}

/* loc_20314's entry: the unit's buffer [esp+4], ebx x ecx, one more than
 * the sprites in it, goes to (eax, edx), within the rectangle at [esp+0xc]
 * ([x0, x1) x [y0, y1)) */
static void hr_unit(const hr_cpu *cpu)
{
    if (!hr_in_sprite_entry() || hr_units == 0) return;
    int count = hr_units;
    hr_units = 0;
    const uint8_t *table = guest(data32(G_SPRITE_BLEND));
    if (table == NULL) return;
    int x = (int32_t)cpu->eax, y = (int32_t)cpu->edx, w = (int32_t)cpu->ebx - 1, h = (int32_t)cpu->ecx - 1;
    uint32_t clip = guest32(cpu->esp + 0xc);
    hr_sprite *s = hr_add_sprite(SPRITE_UNIT, table, (int32_t)guest32(clip), (int32_t)guest32(clip + 8),
                                 (int32_t)guest32(clip + 4), (int32_t)guest32(clip + 0xc));
    for (int i = 0; i < count; i++) s->image[s->count++] = hr_unit_images[i];
    hr_rectangle(s, x, y, w, h);
    hr_entry_drew(x, y, w + 1, h + 1);
}

/* ------------------------------------------------------------------ */
/* The model polygons (loc_3C5C3's list)                                */

static void hr_entry_before(const hr_cpu *cpu)
{
    hr_entry.valid = 0;
    if (guest(data32(G_BACK_BUFFER)) != hr_l) return;
    hr_sync();

    const uint8_t *record = guest(cpu->esi);
    uint32_t type = (cpu->ecx >> 8) & 0xFF;
    uint32_t count = cpu->ecx & 0xFF;
    if (type == 0x80) {
        /* loc_3C61C: [record+4] holds four sprites; by the kind byte of
         * each, loc_398C7, loc_3F2AA or loc_20094 draws it at the point,
         * and those hooks put it in the list.  The scale is loc_3C993's. */
        uint32_t object, depth;
        memcpy(&object, record + 4, 4);
        memcpy(hr_entry.sprite, guest(object), 16);
        memcpy(&depth, guest(cpu->edi), 4);
        hr_entry.q = (depth >> 12) != 0 ? 0x3FFFFFu / (depth >> 12) : 0;
        memcpy(&hr_entry.x[0], record + 0x10, 4);
        memcpy(&hr_entry.y[0], record + 0x14, 4);
        hr_entry.bx0 = L_WIDTH;
        hr_entry.by0 = L_HEIGHT;
        hr_entry.bx1 = hr_entry.by1 = 0;
        hr_units = 0;
        hr_entry.type = type;
        hr_entry.valid = 1;
        return;
    }
    if (type == 2 || type == 0x42) count = 4;
    if (count < 3 || count > HR_MAX_VERTICES) return;
    hr_entry.type = type;
    hr_entry.count = count;
    memcpy(&hr_entry.data, record + 4, 4);
    for (uint32_t i = 0; i < count; i++) {
        memcpy(&hr_entry.x[i], record + 8 + 8 * i, 4);
        memcpy(&hr_entry.y[i], record + 12 + 8 * i, 4);
    }
    if (count == 4) memcpy(&hr_entry.shade, record + 0x28, 4);
    hr_entry.valid = 1;
}

static void hr_entry_after(void)
{
    if (!hr_entry.valid) return;
    hr_entry.valid = 0;

    if (hr_entry.type == 0x80) {
        hr_units = 0;
        hr_known(hr_entry.bx0, hr_entry.by0, hr_entry.bx1, hr_entry.by1);
        return;
    }

    hr_polygon polygon;
    memset(&polygon, 0, sizeof(polygon));
    int n = (int)hr_entry.count;
    polygon.n = n;
    const uint8_t *shade = NULL;
    uint32_t type = hr_entry.type;
    /* the dispatch of loc_3C5C3 and the type routines */
    if (type == 2 || type == 0x42) {
        uint32_t index = hr_entry.data & 0x3FF;
        int wide = data32(G_TEXTURE_WIDE) == 1;
        int keyed = data8(G_TEXTURE_KIND + index) != 0;
        polygon.texture = guest(data32(G_TEXTURE_PTR + 4 * index));
        shade = guest(hr_entry.shade);
        polygon.blend = guest(data32(G_BLEND_TABLE));
        if (type == 2) polygon.mode = keyed ? FILL_TEXTURE_KEY : FILL_TEXTURE;
        else polygon.mode = (wide && keyed) ? FILL_TEXTURE_KEY : FILL_TEXTURE_BLEND;
        /* loc_3B338: the record's vertices 1, 2, 3, 0 get the corners
         * (max, min), (max, max), (min, max), (min, min) of the texture */
        double umax = wide ? 255.5 : 63.5, vmax = wide ? 31.5 : 63.5;
        static const int order[4] = { 1, 2, 3, 0 };
        const double corner_u[4] = { umax, umax, 0.5, 0.5 };
        const double corner_v[4] = { 0.5, vmax, vmax, 0.5 };
        for (int i = 0; i < 4; i++) {
            polygon.p[i].x = hr_entry.x[order[i]] / 16.0;
            polygon.p[i].y = hr_entry.y[order[i]] / 16.0;
            polygon.p[i].u = corner_u[i];
            polygon.p[i].v = corner_v[i];
        }
        if (polygon.texture == NULL || shade == NULL) return;
        if (polygon.mode == FILL_TEXTURE_BLEND && polygon.blend == NULL) return;
    } else {
        polygon.colour = (uint8_t)hr_entry.data;
        polygon.mode = (type & 0x40) ? FILL_FLAT_BLEND : FILL_FLAT;
        polygon.blend = guest(data32(G_BLEND_TABLE));
        if (polygon.mode == FILL_FLAT_BLEND && polygon.blend == NULL) return;
        for (int i = 0; i < n; i++) {
            polygon.p[i].x = hr_entry.x[i] / 16.0;
            polygon.p[i].y = hr_entry.y[i] / 16.0;
            polygon.p[i].u = polygon.p[i].v = 0;
        }
    }
    polygon.clip.x0 = (int32_t)data32(G_CLIP_X0);
    polygon.clip.y0 = (int32_t)data32(G_CLIP_Y0);
    polygon.clip.x1 = (int32_t)data32(G_CLIP_X1) + 1;
    polygon.clip.y1 = (int32_t)data32(G_CLIP_Y1) + 1;
    polygon.shade = hr_list_alloc(hr_frame, 256);
    if (shade != NULL) memcpy(hr_list_at(hr_frame, polygon.shade), shade, 256);
    hr_add(HR_CMD_POLYGON)->u.polygon = polygon;

    /* L's polygon lies in the bounding box of the vertices it rounded */
    int x0 = hr_entry.x[0] >> 4, x1 = x0, y0 = hr_entry.y[0] >> 4, y1 = y0;
    for (int i = 1; i < n; i++) {
        int x = hr_entry.x[i] >> 4, y = hr_entry.y[i] >> 4;
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
    }
    hr_known(x0, y0, x1 + 2, y1 + 2);
}

/* ------------------------------------------------------------------ */

/* The drawing thread: a published frame's list drawn into H, then H and
 * the frame's L made P and S.  The game goes on with the next frame
 * meanwhile; the flip waits for it (Game_HiresFlip). */
static int SDLCALL hr_drawing(void *data)
{
    (void)data;
    for (;;) {
        /* hr_publish set the frame before it posted (it still holds hr_lock
         * until the game's copy is done) */
        SDL_SemWait(hr_draw);
        if (hr_h.colour != NULL) {
            SDL_AtomicLock(&hr_palette_lock);
            memcpy(hr_draw_palette, hr_flip_palette, sizeof(hr_draw_palette));
            SDL_AtomicUnlock(&hr_palette_lock);
        }
        hr_cpu_draw(hr_pending_list, &hr_h);
        SDL_LockMutex(hr_lock);
        memcpy(hr_p, hr_h.pixel, (size_t)hr_row0[hr_pending_rows] * hr_h.width);
        if (hr_h.colour != NULL) {
            memcpy(hr_p_colour, hr_h.colour, (size_t)hr_row0[hr_pending_rows] * hr_h.width * 4);
            memcpy(hr_p_drawn, hr_h.drawn, (size_t)hr_row0[hr_pending_rows] * hr_h.width);
            memcpy(hr_p_palette, hr_draw_palette, sizeof(hr_p_palette));
            hr_p_coloured = 1;
            hr_p_fresh = 1;
        }
        memcpy(hr_s, hr_pending_s, (size_t)hr_pending_rows * L_WIDTH);
        hr_s_rows = hr_pending_rows;
        hr_pending = 0;
        SDL_CondBroadcast(hr_drawn);
        SDL_UnlockMutex(hr_lock);
    }
    return 0;
}

/* The game copies the frame's rows of L to the screen: the frame goes to
 * the drawing thread, and the flip waits until it is drawn and the copy is
 * done (HR_PUBLISHED), so that it never shows S and the screen apart. */
static void hr_publish(int rows)
{
    /* a frame the list did not follow keeps the last P: the flip shows it
     * where the screen still holds S */
    if (!hr_active) return;
    hr_active = 0;
    hr_entry.valid = 0;
    if (guest(data32(G_BACK_BUFFER)) != hr_l) return;
    hr_sync();
    SDL_LockMutex(hr_lock);
    hr_locked = 1;
    /* the frame before is drawn by now, unless the game is faster than
     * the drawing: then it waits for it */
    while (hr_pending) SDL_CondWait(hr_drawn, hr_lock);
    memcpy(hr_pending_s, hr_l, (size_t)rows * L_WIDTH);
    hr_pending_rows = rows;
    hr_pending = 1;
    hr_pending_list = hr_frame;
    /* the next frame is recorded into the other list */
    hr_frame = (hr_frame == &hr_lists[0]) ? &hr_lists[1] : &hr_lists[0];
    SDL_SemPost(hr_draw);
}

extern "C" void game_hr_hook(hr_cpu *cpu, uint32_t which)
{
    if (hr_factor <= 0 || hr_h.pixel == NULL) return;
    switch (which) {
    case HR_FRAME_BEGIN:
        hr_frame_begin(cpu->eax);
        break;
    case HR_SCREEN_BEGIN:
        hr_frame_begin(data32(G_BACK_BUFFER));
        break;
    case HR_MENU_LIST:
        /* the menus' plates; loc_37374's lists are in its frame already */
        if (!hr_active || guest(data32(G_BACK_BUFFER)) != hr_l) hr_frame_begin(data32(G_BACK_BUFFER));
        break;
    case HR_GROUND_ROW:
        if (hr_active) hr_ground_row(cpu);
        break;
    case HR_ENTRY_BEFORE:
        if (hr_active) hr_entry_before(cpu);
        break;
    case HR_ENTRY_AFTER:
        if (hr_active) hr_entry_after();
        break;
    case HR_PUBLISH_VIEW:
        hr_publish(168);
        break;
    case HR_PUBLISH_FULL:
        hr_publish(L_HEIGHT);
        break;
    case HR_SPRITE_SCALED:
        if (hr_active) hr_sprite_scaled(cpu);
        break;
    case HR_SPRITE:
        if (hr_active) hr_sprite_plain(cpu);
        break;
    case HR_UNIT:
        if (hr_active) hr_unit(cpu);
        break;
    case HR_PUBLISHED:
        if (hr_locked) {
            hr_locked = 0;
            SDL_UnlockMutex(hr_lock);
        }
        break;
    }
}

/* c[0] * w[0] + ... + c[3] * w[3], each channel, over total */
static inline uint32_t hr_weigh(const uint32_t *c, const unsigned *w, unsigned total)
{
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        unsigned sum = 0;
        for (int i = 0; i < 4; i++) sum += ((c[i] >> shift) & 0xFF) * w[i];
        out |= (uint32_t)((sum + total / 2) / total) << shift;
    }
    return out;
}

/* The filtered flip.  L's pixels are 2D over P where the screen no longer
 * shows S (or P has no such row), or where the frame drew L's blocks over
 * its 3D (the menus' captions, the text); the rest is P.  Each pixel of H
 * takes the four of L's pixels the filtering samples: the 2D among them,
 * filtered, over P by their weight, so the 2D's edges are filtered on both
 * sides; a pixel of the 2D shows the 2D alone. */
static void hr_flip_filtered(const uint8_t *src, uint32_t *dst, const uint32_t *palette, const uint32_t *colour, int rows)
{
    int k = hr_h.k;
    int width = hr_h.width;

    /* the rows made from the screen's rows that changed, or with another
     * palette, are made again when they are needed */
    if (memcmp(palette, hr_picture_palette, sizeof(hr_picture_palette)) != 0) {
        memcpy(hr_picture_palette, palette, sizeof(hr_picture_palette));
        memset(hr_picture_valid, 0, sizeof(hr_picture_valid));
    }
    for (int y = 0; y < L_HEIGHT; y++) {
        if (memcmp(src + y * L_WIDTH, hr_picture_l + y * L_WIDTH, L_WIDTH) == 0) continue;
        memcpy(hr_picture_l + y * L_WIDTH, src + y * L_WIDTH, L_WIDTH);
        /* H's rows sample L's rows around their centres, a row apart */
        int hy0 = hr_row0[y > 0 ? y - 1 : 0], hy1 = hr_row0[y + 2 < L_HEIGHT ? y + 2 : L_HEIGHT];
        memset(hr_picture_valid + hy0, 0, hy1 - hy0);
    }

    for (int y = 0; y < L_HEIGHT; y++) {
        uint8_t *over = hr_over + y * L_WIDTH;
        if (y >= rows) {
            memset(over, 1, L_WIDTH);
            hr_over_row[y] = hr_over_whole[y] = 1;
            continue;
        }
        const uint8_t *screen = src + y * L_WIDTH, *s = hr_s + y * L_WIDTH;
        /* the middle of L's pixel in H */
        const uint8_t *drawn = hr_p_drawn + (size_t)((hr_row0[y] + hr_row0[y + 1]) / 2) * width + k / 2;
        int any = 0, all = 1;
        for (int x = 0; x < L_WIDTH; x++) {
            over[x] = screen[x] != s[x] || drawn[x * k] == HR_DRAWN_OVER;
            any |= over[x];
            all &= over[x];
        }
        hr_over_row[y] = (uint8_t)any;
        hr_over_whole[y] = (uint8_t)all;
    }

    /* the picture's rows the 2D needs, made on the drawing's threads */
    int make = 0;
    for (int hy = 0; hy < hr_h.height; hy++) {
        int y = hr_mix_y[hy] + (hr_mix_w[hy] >= 128), ya = hr_pick_y[hy];
        hr_picture_make[hy] = !hr_picture_valid[hy] && (hr_over_row[y] || hr_over_row[ya] || hr_over_row[ya + 1]);
        make |= hr_picture_make[hy];
    }
    if (make) {
        hr_cpu_picture(&hr_h, src, palette, hr_picture_make, hr_picture);
        for (int hy = 0; hy < hr_h.height; hy++) hr_picture_valid[hy] |= hr_picture_make[hy];
    }

    for (int hy = 0; hy < hr_h.height; hy++) {
        int y = hr_mix_y[hy] + (hr_mix_w[hy] >= 128);    /* the L row the row is in, as in hr_row0 */
        int ya = hr_pick_y[hy];
        unsigned fy = hr_pick_wy[hy];
        uint32_t *out = dst + (size_t)hy * width;
        const uint8_t *p = hr_p + (size_t)hy * width;
        const uint32_t *c = (colour != NULL) ? colour + (size_t)hy * width : NULL;
        if (!hr_over_row[y] && !hr_over_row[ya] && !hr_over_row[ya + 1]) {
            if (c != NULL) memcpy(out, c, (size_t)width * 4);
            else for (int hx = 0; hx < width; hx++) out[hx] = palette[p[hx]];
            continue;
        }
        uint32_t *picture = hr_picture + (size_t)hy * width;
        if (hr_over_whole[y] && hr_over_whole[ya] && hr_over_whole[ya + 1]) {
            memcpy(out, picture, (size_t)width * 4);
            continue;
        }
        const uint8_t *own = hr_over + y * L_WIDTH;
        const uint8_t *oa = hr_over + ya * L_WIDTH, *ob = oa + L_WIDTH;
        const uint8_t *la = src + ya * L_WIDTH, *lb = la + L_WIDTH;
        for (int hx = 0; hx < width; hx++) {
            int xa = hr_pick_x[hx];
            unsigned fx = hr_pick_wx[hx];
            unsigned w[4] = { (256 - fx) * (256 - fy), fx * (256 - fy), (256 - fx) * fy, fx * fy };
            uint8_t o[4] = { oa[xa], oa[xa + 1], ob[xa], ob[xa + 1] };
            unsigned at = 0;
            for (int i = 0; i < 4; i++) if (!o[i]) w[i] = 0; else at += w[i];
            uint32_t under = (c != NULL) ? c[hx] : palette[p[hx]];
            if (own[hx / k]) {
                if (at == 65536 || at == 0) out[hx] = picture[hx];
                else {
                    uint32_t l[4] = { palette[la[xa]], palette[la[xa + 1]], palette[lb[xa]], palette[lb[xa + 1]] };
                    out[hx] = hr_weigh(l, w, at);
                }
            } else if (at == 0) {
                out[hx] = under;
            } else {
                uint32_t l[4] = { palette[la[xa]], palette[la[xa + 1]], palette[lb[xa]], palette[lb[xa + 1]] };
                /* the 2D's share of the pixel, and P under the rest */
                uint32_t mix[4] = { hr_weigh(l, w, at), under, 0, 0 };
                unsigned share[4] = { at, 65536 - at, 0, 0 };
                out[hx] = hr_weigh(mix, share, 65536);
            }
        }
    }
}

extern "C" void Game_HiresFlip(const uint8_t *src, uint32_t *dst, const uint32_t *palette)
{
    int k = hr_h.k;
    int width = hr_h.width;

    if (hr_h.colour != NULL) {
        SDL_AtomicLock(&hr_palette_lock);
        memcpy(hr_flip_palette, palette, sizeof(hr_flip_palette));
        SDL_AtomicUnlock(&hr_palette_lock);
    }

    SDL_LockMutex(hr_lock);
    while (hr_pending) SDL_CondWait(hr_drawn, hr_lock);
    /* P in colours: one not shown yet, or one drawn with this palette */
    const uint32_t *colour = NULL;
    if (hr_p_coloured && (hr_p_fresh || memcmp(palette, hr_p_palette, sizeof(hr_p_palette)) == 0)) {
        colour = hr_p_colour;
    }
    hr_p_fresh = 0;
    /* the screen changed without the game's copy (a movie, a picture): P is
     * kept only where at least a quarter of S is still on the screen */
    int rows = hr_s_rows, same = 0;
    for (int i = 0; i < rows * L_WIDTH; i++) same += src[i] == hr_s[i];
    if (same * 4 < rows * L_WIDTH) rows = 0;
    if (hr_h.colour != NULL) {
        hr_flip_filtered(src, dst, palette, colour, rows);
        SDL_UnlockMutex(hr_lock);
        return;
    }
    for (int hy = 0; hy < hr_h.height; hy++) {
        /* the row belongs to the L row that has most of it, as in hr_row0 */
        int ya = hr_mix_y[hy], w = hr_mix_w[hy];
        int y = (w >= 128) ? ya + 1 : ya;
        const uint8_t *screen = src + y * L_WIDTH;
        const uint8_t *s = hr_s + y * L_WIDTH;
        const uint8_t *above = src + ya * L_WIDTH, *below = (w != 0) ? above + L_WIDTH : above;
        int published = y < rows;
        uint32_t *out = dst + (size_t)hy * width;
        const uint8_t *p = hr_p + (size_t)hy * width;
        if (published && memcmp(screen, s, L_WIDTH) == 0) {
            for (int hx = 0; hx < width; hx++) out[hx] = palette[p[hx]];
            continue;
        }
        for (int x = 0; x < L_WIDTH; x++) {
            if (published && screen[x] == s[x]) {
                for (int i = 0; i < k; i++) out[x * k + i] = palette[p[x * k + i]];
                continue;
            }
            uint32_t pixel = palette[above[x]];
            if (w != 0 && below[x] != above[x]) {
                uint32_t a = pixel, b = palette[below[x]];
                pixel = 0;
                for (int shift = 0; shift < 32; shift += 8) {
                    uint32_t c = ((((a >> shift) & 0xFF) * (256 - w) + ((b >> shift) & 0xFF) * w) >> 8) & 0xFF;
                    pixel |= c << shift;
                }
            }
            for (int i = 0; i < k; i++) out[x * k + i] = pixel;
        }
    }
    SDL_UnlockMutex(hr_lock);
}
