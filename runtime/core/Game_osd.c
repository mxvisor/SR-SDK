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

/* The port's on-screen display, its panel of settings (Game_osd.h). */

#include <stdio.h>
#include <string.h>
#include "Game_osd.h"
#include "Game_ui.h"
#include "Game_config.h"
#include "Game_defs.h"
#include "Game_thread.h"
#include "touchgamepad.h"
#ifdef GAME_GL
#include "gl/Game_gl.h"
#endif
#include "Game_hooks.h"

#if defined(GAME_WINAPI_ADDITIONAL_RESOLUTIONS) && GAME_WINAPI_ADDITIONAL_RESOLUTIONS
/* WinApi-ddraw.c: the item of Display_Additional_Resolutions */
#ifdef __cplusplus
extern "C"
#endif
void Winapi_DirectDrawOsdItems(void);
#endif

#if defined(GAME_GLIDE_3D_RESOLUTION_ITEM) && GAME_GLIDE_3D_RESOLUTION_ITEM
/* Glide2x.c: the item of Display_3D_Resolution */
#ifdef __cplusplus
extern "C"
#endif
void Glide_OsdItems(void);
#endif

#ifndef GAME_NAME
#define GAME_NAME "game"
#endif

#define OSD_ITEMS 16
#define MESSAGE_MS 1500

static const Game_OsdItem *items[OSD_ITEMS];
static int item_count;

static SDL_Scancode osd_key = SDL_SCANCODE_F12;
static volatile int osd_shown;
static int osd_stamp;
/* the chosen row: an item, or item_count for "Close" */
static volatile int osd_sel;
/* the host's pointer before the panel showed its own */
static int osd_cursor = -1;
/* the keys pressed for the panel (its own key, every key pressed while it is
 * open, Esc that closes it): the game sees them only once they are released */
static volatile uint8_t osd_keys[SDL_NUM_SCANCODES];

/* written by the threads of the events, read by the presenter's */
static SDL_SpinLock message_lock;
static char message[64];
static Uint32 message_until;
static volatile int message_live;

/* where the presenter drew the panel, for the pointer (under message_lock):
 * the panel, the first row of the items, the height of a row, the size of the
 * screen interface */
static struct {
    int x, y, w, h;
    int rows_y, line_h;
    int W, H;
} drawn;

/* the presenter's thread */
static int frame_width, frame_height;
static char frame_output[64];
static Uint32 fps_start;
static int fps_frames, fps_tenths;

void Game_OsdAddItem(const Game_OsdItem *entry)
{
    if (entry == NULL || item_count == OSD_ITEMS) return;
    items[item_count++] = entry;
    osd_stamp++;
}

int Game_OsdOpen(void)
{
    return osd_shown;
}

static void move_selection(int step);

static void set_shown(int shown)
{
    if (shown == osd_shown) return;
    if (shown)
    {
        /* what is held on the touch gamepad is let go: the touches are the panel's */
        TouchGamepad_ReleaseAll();
        osd_cursor = SDL_ShowCursor(SDL_QUERY);
        SDL_ShowCursor(SDL_ENABLE);
        osd_shown = 1;
        /* the first item that can be switched, else "Close" */
        osd_sel = item_count;
        move_selection(1);
    }
    else if (osd_cursor >= 0)
    {
        SDL_ShowCursor(osd_cursor);
        osd_cursor = -1;
    }
    osd_shown = shown;
    osd_stamp++;
}

void Game_OsdToggle(void)
{
    set_shown(!osd_shown);
}

/* the panel's key: Display_OSD_Key, on Android also the system's Back (the
 * button or the gesture), so a touch screen needs nothing over the picture */
static int panel_key(SDL_Scancode scancode)
{
#if defined(__ANDROID__)
    if (scancode == SDL_SCANCODE_AC_BACK) return 1;
#endif
    return osd_key != SDL_SCANCODE_UNKNOWN && scancode == osd_key;
}

void Game_OsdInit(void)
{
#if defined(__ANDROID__)
    /* Back does not end the activity: it is the panel's */
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#endif
#ifdef GAME_GL
    Game_GlOsdItems();
#endif
    Game_WindowOsdItems();
#if defined(GAME_WINAPI_ADDITIONAL_RESOLUTIONS) && GAME_WINAPI_ADDITIONAL_RESOLUTIONS
    Winapi_DirectDrawOsdItems();
#endif
#if defined(GAME_GLIDE_3D_RESOLUTION_ITEM) && GAME_GLIDE_3D_RESOLUTION_ITEM
    Glide_OsdItems();
#endif
#ifdef GAME_OSD_ITEMS
    GAME_OSD_ITEMS();
#endif
}

int Game_OsdConfigKey(const char *key, const char *value)
{
    SDL_Scancode scancode;

    if (strcasecmp(key, "Display_OSD_Key") != 0) return 0;
    if (strcasecmp(value, "off") == 0) osd_key = SDL_SCANCODE_UNKNOWN;
    else if ((scancode = SDL_GetScancodeFromName(value)) != SDL_SCANCODE_UNKNOWN) osd_key = scancode;
    else fprintf(stderr, GAME_NAME ": unknown Display_OSD_Key: %s\n", value);
    return 1;
}

static int item_active(int i)
{
    return items[i]->press != NULL && (items[i]->active == NULL || items[i]->active());
}

/* a row of the panel chosen: the item to its next value, saved; "Close" closes */
static void activate(int row)
{
    const Game_OsdItem *entry;

    if (row >= item_count)
    {
        set_shown(0);
        return;
    }
    if (row < 0 || !item_active(row)) return;
    entry = items[row];
    entry->press();
    if (entry->config != NULL && entry->state != NULL && !Game_ConfigSave(entry->config, entry->state()))
    {
        Game_OsdMessage("Settings: not saved");
    }
    osd_stamp++;
}

static void select_row(int row)
{
    if (row < 0 || row > item_count || row == osd_sel) return;
    osd_sel = row;
    osd_stamp++;
}

static void move_selection(int step)
{
    int row = osd_sel;
    int n;

    /* over the rows that cannot be switched now */
    for (n = 0; n <= item_count; n++)
    {
        row = (row + step + item_count + 1) % (item_count + 1);
        if (row == item_count || item_active(row)) break;
    }
    select_row(row);
}

/* the row at a point of the screen interface: an item, item_count ("Close"), -1
 * in the panel elsewhere, -2 outside it */
static int row_at(int x, int y)
{
    int row = -1;

    SDL_AtomicLock(&message_lock);
    if (drawn.line_h <= 0 || x < drawn.x || x >= drawn.x + drawn.w || y < drawn.y || y >= drawn.y + drawn.h) row = -2;
    else if (y >= drawn.rows_y)
    {
        row = (y - drawn.rows_y) / drawn.line_h;
        if (row > item_count) row = -1;
    }
    SDL_AtomicUnlock(&message_lock);
    return row;
}

static void finger_point(const SDL_Event *event, int *x, int *y)
{
    int W, H;

    SDL_AtomicLock(&message_lock);
    W = drawn.W;
    H = drawn.H;
    SDL_AtomicUnlock(&message_lock);
    *x = (int)(event->tfinger.x * W);
    *y = (int)(event->tfinger.y * H);
}

static void note_key(const SDL_Event *event)
{
    SDL_Scancode sc = event->key.keysym.scancode;

    if ((unsigned)sc >= SDL_NUM_SCANCODES) return;
    if (event->type == SDL_KEYUP) osd_keys[sc] = 0;
    else if (osd_shown || panel_key(sc)) osd_keys[sc] = 1;
}

int Game_OsdKeyHidden(SDL_Scancode scancode)
{
    return osd_shown || ((unsigned)scancode < SDL_NUM_SCANCODES && osd_keys[scancode]);
}

int Game_OsdHides(const SDL_Event *event)
{
    switch (event->type)
    {
        case SDL_KEYDOWN:
            note_key(event);
            return Game_OsdKeyHidden(event->key.keysym.scancode);
        case SDL_KEYUP:
            note_key(event);
            return panel_key(event->key.keysym.scancode);
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEWHEEL:
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
            return osd_shown;
    }
    /* a release goes through: what was held before the panel opened is let go */
    return 0;
}

int Game_OsdEvent(const SDL_Event *event)
{
    int x, y, row;

    if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) note_key(event);
    if ((event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) && panel_key(event->key.keysym.scancode))
    {
        if (event->type == SDL_KEYDOWN && !event->key.repeat) Game_OsdToggle();
        return 1;
    }
    /* the repeats of a key that closed the panel, still held */
    if (!osd_shown) return event->type == SDL_KEYDOWN && Game_OsdKeyHidden(event->key.keysym.scancode);

    switch (event->type)
    {
        case SDL_KEYDOWN:
            switch (event->key.keysym.sym)
            {
                case SDLK_UP:
                    move_selection(-1);
                    break;
                case SDLK_DOWN:
                case SDLK_TAB:
                    move_selection(1);
                    break;
                case SDLK_LEFT:
                case SDLK_RIGHT:
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                    activate(osd_sel);
                    break;
                case SDLK_ESCAPE:
                    set_shown(0);
                    break;
            }
            return 1;
        case SDL_KEYUP:
        case SDL_TEXTINPUT:
        case SDL_TEXTEDITING:
            return 1;

        case SDL_MOUSEMOTION:
            if (event->motion.which == SDL_TOUCH_MOUSEID) return 1;
            x = event->motion.x;
            y = event->motion.y;
            Game_UiWindowPoint(event->motion.windowID, &x, &y);
            row = row_at(x, y);
            if (row >= 0) select_row(row);
            return 1;
        case SDL_MOUSEBUTTONDOWN:
            if (event->button.which == SDL_TOUCH_MOUSEID || event->button.button != SDL_BUTTON_LEFT) return 1;
            x = event->button.x;
            y = event->button.y;
            Game_UiWindowPoint(event->button.windowID, &x, &y);
            row = row_at(x, y);
            if (row >= 0)
            {
                select_row(row);
                activate(row);
            }
            else if (row == -2) set_shown(0);
            return 1;
        case SDL_MOUSEBUTTONUP:
            return 1;
        case SDL_MOUSEWHEEL:
            if (event->wheel.y != 0) move_selection((event->wheel.y > 0) ? -1 : 1);
            return 1;

        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
        case SDL_FINGERUP:
            if (event->type != SDL_FINGERDOWN) return 1;
            finger_point(event, &x, &y);
            row = row_at(x, y);
            if (row >= 0)
            {
                select_row(row);
                activate(row);
            }
            else if (row == -2) set_shown(0);
            return 1;

        case SDL_CONTROLLERBUTTONDOWN:
            switch (event->cbutton.button)
            {
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    move_selection(-1);
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    move_selection(1);
                    break;
                case SDL_CONTROLLER_BUTTON_A:
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    activate(osd_sel);
                    break;
                case SDL_CONTROLLER_BUTTON_B:
                case SDL_CONTROLLER_BUTTON_BACK:
                    set_shown(0);
                    break;
            }
            return 1;
        case SDL_CONTROLLERBUTTONUP:
        case SDL_CONTROLLERAXISMOTION:
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
        case SDL_JOYAXISMOTION:
        case SDL_JOYHATMOTION:
            return 1;
    }
    return 0;
}

void Game_OsdMessage(const char *text)
{
    SDL_AtomicLock(&message_lock);
    snprintf(message, sizeof(message), "%s", text);
    message_until = SDL_GetTicks() + MESSAGE_MS;
    message_live = 1;
    osd_stamp++;
    SDL_AtomicUnlock(&message_lock);
}

void Game_OsdFrame(int width, int height, const char *output)
{
    Uint32 now = SDL_GetTicks();

    if (output == NULL) output = "?";
    if (width != frame_width || height != frame_height || strcmp(output, frame_output) != 0)
    {
        frame_width = width;
        frame_height = height;
        snprintf(frame_output, sizeof(frame_output), "%s", output);
        if (osd_shown) osd_stamp++;
    }
    if (fps_frames == 0) fps_start = now;
    fps_frames++;
    if (now - fps_start >= 1000)
    {
        int tenths = (int)((fps_frames - 1) * 10000.0 / (now - fps_start) + 0.5);

        if (tenths != fps_tenths && osd_shown) osd_stamp++;
        fps_tenths = tenths;
        fps_frames = 0;
    }
}

int Game_OsdVisible(void)
{
    if (message_live && SDL_TICKS_PASSED(SDL_GetTicks(), message_until))
    {
        message_live = 0;
        osd_stamp++;
    }
    return osd_shown || message_live;
}

int Game_OsdStamp(void)
{
    Game_OsdVisible();
    return osd_stamp;
}

/* ---- the panel ---- */

static void gamepad_text(char *text, size_t size)
{
    const char *name;

    /* the port has no gamepad layer of its own yet: what SDL has opened */
    if (!SDL_WasInit(SDL_INIT_JOYSTICK) || SDL_NumJoysticks() <= 0)
    {
        snprintf(text, size, "Gamepad: none");
        return;
    }
    name = SDL_JoystickNameForIndex(0);
    snprintf(text, size, "Gamepad: %s", (name != NULL) ? name : "?");
    if (SDL_NumJoysticks() > 1) snprintf(text + strlen(text), size - strlen(text), " (+%d)", SDL_NumJoysticks() - 1);
}

#define INFO_LINES 5
#define PANEL_LINES (INFO_LINES + OSD_ITEMS + 2)

enum { LINE_INFO, LINE_ITEM, LINE_GREY, LINE_MESSAGE };

void Game_OsdDraw(SDL_Renderer *renderer, int width, int height)
{
    char lines[PANEL_LINES][128];
    int kind[PANEL_LINES];
    int count = 0, info = 0, line_h, pad, panel_w, panel_h, i, y, sel = osd_sel;

    /* about 30 lines in the height of the window, and never smaller than 10 pixels */
    line_h = height / 30;
    if (line_h < 10) line_h = 10;
    pad = line_h / 2;

    if (osd_shown)
    {
        snprintf(lines[count++], sizeof(lines[0]), "%s", GAME_NAME);
        if (frame_width > 0)
        {
            snprintf(lines[count++], sizeof(lines[0]), "Picture %dx%d, %s", frame_width, frame_height, frame_output);
            snprintf(lines[count++], sizeof(lines[0]), "Window %dx%d (x%.2f)", width, height,
                     (width * frame_height < height * frame_width) ? (double)width / frame_width : (double)height / frame_height);
        }
        else snprintf(lines[count++], sizeof(lines[0]), "Window %dx%d", width, height);
        snprintf(lines[count++], sizeof(lines[0]), "FPS %d.%d", fps_tenths / 10, fps_tenths % 10);
        gamepad_text(lines[count++], sizeof(lines[0]));
        for (i = 0; i < count; i++) kind[i] = LINE_INFO;
        info = count;
        for (i = 0; i < item_count; i++)
        {
            const Game_OsdItem *entry = items[i];

            if (entry->state != NULL) snprintf(lines[count], sizeof(lines[0]), "%s: %s", entry->label, entry->state());
            else snprintf(lines[count], sizeof(lines[0]), "%s", entry->label);
            kind[count++] = item_active(i) ? LINE_ITEM : LINE_GREY;
        }
#if defined(__ANDROID__)
        snprintf(lines[count], sizeof(lines[0]), "Close (Back)");
#else
        if (osd_key != SDL_SCANCODE_UNKNOWN) snprintf(lines[count], sizeof(lines[0]), "Close (%s)", SDL_GetScancodeName(osd_key));
        else snprintf(lines[count], sizeof(lines[0]), "Close");
#endif
        kind[count++] = LINE_ITEM;
    }
    if (message_live)
    {
        SDL_AtomicLock(&message_lock);
        snprintf(lines[count], sizeof(lines[0]), "%s", message);
        SDL_AtomicUnlock(&message_lock);
        kind[count++] = LINE_MESSAGE;
    }

    panel_w = 0;
    for (i = 0; i < count; i++)
    {
        int w = Game_UiTextWidth(lines[i], line_h) + ((kind[i] == LINE_ITEM || kind[i] == LINE_GREY) ? 2 * pad : 0);

        if (w > panel_w) panel_w = w;
    }
    /* a gap between the information and the items */
    panel_h = count * line_h + 2 * pad + (osd_shown ? pad : 0);

    SDL_AtomicLock(&message_lock);
    drawn.x = pad;
    drawn.y = pad;
    drawn.w = osd_shown ? panel_w + 2 * pad : 0;
    drawn.h = osd_shown ? panel_h : 0;
    drawn.rows_y = 2 * pad + info * line_h + pad;
    drawn.line_h = osd_shown ? line_h : 0;
    drawn.W = width;
    drawn.H = height;
    SDL_AtomicUnlock(&message_lock);
    if (count == 0) return;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 176);
    Game_UiFillRect(renderer, pad, pad, panel_w + 2 * pad, panel_h);
    for (i = 0, y = 2 * pad; i < count; i++, y += line_h)
    {
        int x = 2 * pad;

        if (i == info && osd_shown) y += pad;
        if (kind[i] == LINE_ITEM || kind[i] == LINE_GREY)
        {
            x += pad;
            if (i - info == sel)
            {
                SDL_SetRenderDrawColor(renderer, 70, 110, 190, 220);
                Game_UiFillRect(renderer, pad + pad / 2, y, panel_w + pad, line_h);
            }
        }
        switch (kind[i])
        {
            case LINE_INFO: SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255); break;
            case LINE_GREY: SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255); break;
            /* the message under the panel in yellow */
            case LINE_MESSAGE: SDL_SetRenderDrawColor(renderer, 255, 230, 96, 255); break;
            default: SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); break;
        }
        Game_UiText(renderer, lines[i], x, y, line_h);
    }
}
