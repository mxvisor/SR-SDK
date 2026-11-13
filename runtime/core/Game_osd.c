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
#include <stdlib.h>
#include <string.h>
#include "Game_osd.h"
#include "Game_ui.h"
#include "Game_config.h"
#include "Game_defs.h"
#include "Game_thread.h"
#include "touchgamepad.h"
#include "Game_pad.h"
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

#if defined(GAME_D3D_GL) || defined(GAME_GLIDE)
/* WinApi-d3d.c: the item of Display_Antialiasing */
#ifdef __cplusplus
extern "C"
#endif
void Winapi_Direct3DOsdItems(void);
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
/* the values of a list that are shown */
#define OSD_LIST_MAX 32
#define MESSAGE_MS 1500

static const Game_OsdItem *items[OSD_ITEMS];
static int item_count;

static SDL_Scancode osd_key = SDL_SCANCODE_F12;
static volatile int osd_shown;
static int osd_stamp;
/* the chosen row: an item, or item_count for "Close" */
static volatile int osd_sel;
/* the item whose list of values is open under it (-1: none), the value chosen in it */
static volatile int list_item = -1;
static volatile int list_sel;
/* the first row shown when they do not all fit; the presenter brings the chosen
 * row into the view when osd_follow is set */
static volatile int osd_top;
static volatile int osd_follow;
/* a finger on the panel: where it went down, whether it drags the rows */
static struct {
    SDL_FingerID id;
    int down, y, top, row, drag;
} finger;
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
 * the panel, the first row of the items, the height of a row, the first row
 * shown, how many are shown, how many there are, the size of the screen
 * interface */
static struct {
    int x, y, w, h;
    int rows_y, row_h, top, shown, total;
    int W, H;
} drawn;

/* the presenter's thread */
static int frame_width, frame_height;
static char frame_output[64];
static Uint32 fps_start;
static int fps_frames, fps_tenths;
/* the frames per second in the upper right corner (the item FPS, Display_FPS) */
static volatile int fps_shown;

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
        osd_cursor = Game_ShowCursor(SDL_QUERY);
        Game_ShowCursor(SDL_ENABLE);
        list_item = -1;
        finger.down = 0;
        osd_top = 0;
        osd_shown = 1;
        /* the first item that can be switched, else "Close" */
        osd_sel = item_count;
        move_selection(1);
    }
    else
    {
        list_item = -1;
        finger.down = 0;
    }
    if (!shown && osd_cursor >= 0)
    {
        Game_ShowCursor(osd_cursor);
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

static void fps_item_press(void)
{
    fps_shown = !fps_shown;
    osd_stamp++;
}

static const char *fps_item_state(void)
{
    return fps_shown ? "on" : "off";
}

static const Game_OsdItem fps_item = { "FPS", NULL, fps_item_press, fps_item_state, "Display_FPS", NULL, NULL, NULL, NULL };

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
    Game_OsdAddItem(&fps_item);
#if defined(GAME_WINAPI_ADDITIONAL_RESOLUTIONS) && GAME_WINAPI_ADDITIONAL_RESOLUTIONS
    Winapi_DirectDrawOsdItems();
#endif
#if defined(GAME_GLIDE_3D_RESOLUTION_ITEM) && GAME_GLIDE_3D_RESOLUTION_ITEM
    Glide_OsdItems();
#endif
#if defined(GAME_D3D_GL) || defined(GAME_GLIDE)
    Winapi_Direct3DOsdItems();
#endif
#ifdef GAME_OSD_ITEMS
    GAME_OSD_ITEMS();
#endif
}

int Game_OsdConfigKey(const char *key, const char *value)
{
    SDL_Scancode scancode;

    if (strcasecmp(key, "Display_FPS") == 0)
    {
        if (strcasecmp(value, "on") == 0) fps_shown = 1;
        else if (strcasecmp(value, "off") == 0) fps_shown = 0;
        else fprintf(stderr, GAME_NAME ": unknown Display_FPS: %s\n", value);
        return 1;
    }
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

static int item_has_list(int i)
{
    return items[i]->count != NULL && items[i]->value != NULL && items[i]->choose != NULL;
}

/* the values of an item's list, at most OSD_LIST_MAX */
static int item_values(int i)
{
    int n = items[i]->count();

    return (n < 0) ? 0 : (n > OSD_LIST_MAX) ? OSD_LIST_MAX : n;
}

/* the value of an item's list that it has now (its state), else the first */
static int item_current(int i)
{
    char now[64];
    int n = item_values(i), v;

    if (items[i]->state == NULL) return 0;
    /* state and value may both answer in one static buffer */
    snprintf(now, sizeof(now), "%s", items[i]->state());
    for (v = 0; v < n; v++)
    {
        if (strcasecmp(items[i]->value(v), now) == 0) return v;
    }
    return 0;
}

/* the rows below the information: the items, the values of the open list
 * under its item, "Close" */
static int list_values(void)
{
    int open = list_item;

    return (open >= 0 && open < item_count) ? item_values(open) : 0;
}

/* a row's item (item_count: "Close"); *value: the value of the open list, else -1 */
static int row_item(int row, int *value)
{
    int open = list_item, n = list_values();

    *value = -1;
    if (open >= 0 && row > open)
    {
        if (row <= open + n)
        {
            *value = row - open - 1;
            return open;
        }
        row -= n;
    }
    return row;
}

static void save_item(const Game_OsdItem *entry)
{
    if (entry->config != NULL && entry->state != NULL && !Game_ConfigSave(entry->config, entry->state()))
    {
        Game_OsdMessage("Settings: not saved");
    }
}

static void open_list(int i)
{
    list_sel = item_current(i);
    list_item = i;
    osd_follow = 1;
    osd_stamp++;
}

/* the list closes, its item stays chosen */
static void close_list(void)
{
    int open = list_item;

    if (open < 0) return;
    list_item = -1;
    osd_sel = open;
    osd_follow = 1;
    osd_stamp++;
}

/* a value of the open list chosen: the item to it, saved */
static void choose_value(int value)
{
    int open = list_item;

    close_list();
    if (open < 0 || open >= item_count || !item_active(open) || value < 0 || value >= item_values(open)) return;
    items[open]->choose(value);
    save_item(items[open]);
    osd_stamp++;
}

/* a row of the panel chosen: the item to its next value, saved, or its list
 * opened or closed; "Close" closes */
/* dir: 0 press (Enter, a click, a touch), -1/+1 Left/Right */
static void activate(int row, int dir)
{
    const Game_OsdItem *entry;

    if (row >= item_count)
    {
        set_shown(0);
        return;
    }
    if (row < 0 || !item_active(row)) return;
    entry = items[row];
    if (dir == 0 && item_has_list(row))
    {
        if (list_item == row) close_list();
        else open_list(row);
        return;
    }
    if (dir != 0 && entry->step != NULL) entry->step(dir);
    else entry->press();
    save_item(entry);
    osd_stamp++;
}

static void select_row(int row)
{
    if (row < 0 || row > item_count || row == osd_sel) return;
    osd_sel = row;
    osd_follow = 1;
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

/* Up and Down in the open list: round, as over the items */
static void move_list(int step)
{
    int n = list_values();

    if (n <= 0) return;
    list_sel = (list_sel + step + n) % n;
    osd_follow = 1;
    osd_stamp++;
}

/* the pointer over a row: it is chosen (in the open list, only its values) */
static void point_row(int row)
{
    int value, item = row_item(row, &value);

    if (value >= 0)
    {
        if (value != list_sel)
        {
            list_sel = value;
            osd_stamp++;
        }
    }
    else if (list_item < 0) select_row(item);
}

/* a click or a tap on a row: a value of the open list is chosen; another row
 * closes the list first */
static void click_row(int row)
{
    int value, item = row_item(row, &value), open = list_item;

    if (value >= 0)
    {
        choose_value(value);
        return;
    }
    if (open >= 0)
    {
        close_list();
        if (item == open) return;
    }
    select_row(item);
    activate(item, 0);
}

/* the row at a point of the screen interface: a row (row_item), -1 in the
 * panel elsewhere, -2 outside it */
static int row_at(int x, int y)
{
    int row = -1;

    SDL_AtomicLock(&message_lock);
    if (drawn.row_h <= 0 || x < drawn.x || x >= drawn.x + drawn.w || y < drawn.y || y >= drawn.y + drawn.h) row = -2;
    else if (y >= drawn.rows_y)
    {
        row = (y - drawn.rows_y) / drawn.row_h;
        row = (row < drawn.shown) ? drawn.top + row : -1;
        if (row >= drawn.total) row = -1;
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

/* a finger: a tap chooses a row when it is lifted, a finger drawn up or down
 * scrolls the rows; a touch outside the panel closes it */
static void finger_event(const SDL_Event *event)
{
    int x, y, row, row_h;

    finger_point(event, &x, &y);
    if (event->type == SDL_FINGERDOWN)
    {
        if (finger.down) return;
        row = row_at(x, y);
        if (row == -2)
        {
            set_shown(0);
            return;
        }
        finger.id = event->tfinger.fingerId;
        finger.down = 1;
        finger.y = y;
        finger.top = osd_top;
        finger.row = row;
        finger.drag = 0;
        if (row >= 0) point_row(row);
        return;
    }
    if (!finger.down || event->tfinger.fingerId != finger.id) return;
    SDL_AtomicLock(&message_lock);
    row_h = drawn.row_h;
    SDL_AtomicUnlock(&message_lock);
    if (row_h <= 0) row_h = 1;
    if (event->type == SDL_FINGERMOTION)
    {
        int dy = y - finger.y;

        if (!finger.drag && (dy >= row_h / 2 || -dy >= row_h / 2)) finger.drag = 1;
        if (finger.drag)
        {
            int top = finger.top - (dy >= 0 ? dy + row_h / 2 : dy - row_h / 2) / row_h;

            if (top < 0) top = 0;
            if (top != osd_top)
            {
                osd_top = top;
                osd_stamp++;
            }
        }
        return;
    }
    /* SDL_FINGERUP */
    finger.down = 0;
    if (!finger.drag && finger.row >= 0) click_row(finger.row);
}

static void note_key(const SDL_Event *event)
{
    SDL_Scancode sc = event->key.keysym.scancode;

    /* no scancode: the pad's key (Game_pad.c), which is never held */
    if (sc == SDL_SCANCODE_UNKNOWN || (unsigned)sc >= SDL_NUM_SCANCODES) return;
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
    /* the repeats of a key that closed the panel, still held; a key of the pad
     * that came after the panel closed */
    if (!osd_shown) return event->type == SDL_KEYDOWN && (Game_OsdKeyHidden(event->key.keysym.scancode)
                                                          || event->key.keysym.scancode == SDL_SCANCODE_UNKNOWN);

    switch (event->type)
    {
        case SDL_KEYDOWN:
            if (list_item >= 0)
            {
                switch (event->key.keysym.sym)
                {
                    case SDLK_UP:
                        move_list(-1);
                        break;
                    case SDLK_DOWN:
                    case SDLK_TAB:
                        move_list(1);
                        break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                    case SDLK_SPACE:
                        choose_value(list_sel);
                        break;
                    case SDLK_ESCAPE:
                        close_list();
                        break;
                }
                return 1;
            }
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
                    activate(osd_sel, -1);
                    break;
                case SDLK_RIGHT:
                    activate(osd_sel, 1);
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                    activate(osd_sel, 0);
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
            if (row >= 0) point_row(row);
            return 1;
        case SDL_MOUSEBUTTONDOWN:
            if (event->button.which == SDL_TOUCH_MOUSEID || event->button.button != SDL_BUTTON_LEFT) return 1;
            x = event->button.x;
            y = event->button.y;
            Game_UiWindowPoint(event->button.windowID, &x, &y);
            row = row_at(x, y);
            if (row >= 0) click_row(row);
            else if (row == -2) set_shown(0);
            return 1;
        case SDL_MOUSEBUTTONUP:
            return 1;
        case SDL_MOUSEWHEEL:
            if (event->wheel.y == 0) return 1;
            if (list_item >= 0) move_list((event->wheel.y > 0) ? -1 : 1);
            else move_selection((event->wheel.y > 0) ? -1 : 1);
            return 1;

        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
        case SDL_FINGERUP:
            finger_event(event);
            return 1;

        /* the pad moves the panel with keys (Game_pad.c, a key without a
         * scancode); where SDL still sends its events, they are not for the game */
        case SDL_CONTROLLERBUTTONDOWN:
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

        if (tenths != fps_tenths && fps_shown) osd_stamp++;
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
    return osd_shown || message_live || fps_shown;
}

int Game_OsdStamp(void)
{
    Game_OsdVisible();
    return osd_stamp;
}

/* ---- the panel ---- */

/* The panel's font has ASCII only: the words of a pad's name with other characters
 * (UTF-8; Steam names its pad in the user's language: "Контроллер Steam Deck") are
 * left out; "gamepad" when none is left. */
static void ascii_words(char *out, size_t size, const char *name)
{
    size_t length = 0;

    if (size == 0) return;
    while (*name != 0)
    {
        const char *word = name;
        size_t word_length = 0;
        int ascii = 1;

        while (*name == ' ') name++;
        word = name;
        while (*name != 0 && *name != ' ')
        {
            if ((unsigned char)*name >= 0x80 || (unsigned char)*name < 0x20) ascii = 0;
            name++;
        }
        word_length = (size_t)(name - word);
        if (!ascii || word_length == 0) continue;
        if (length + (length != 0) + word_length + 1 > size) break;
        if (length != 0) out[length++] = ' ';
        memcpy(out + length, word, word_length);
        length += word_length;
    }
    out[length] = 0;
    if (length == 0) snprintf(out, size, "gamepad");
}

static void gamepad_text(char *text, size_t size)
{
    char name[64];
    unsigned int slots = Game_PadSlots();
    int pad, first = -1, count = 0;

    /* the first pad by its name, the others counted (Game_pad.h) */
    for (pad = 0; pad < GAME_PAD_MAX; pad++)
    {
        if (!(slots & (1u << pad))) continue;
        if (first < 0) first = pad;
        count++;
    }
    if (count == 0)
    {
        snprintf(text, size, "Gamepad: none");
        return;
    }
    ascii_words(name, sizeof(name), Game_PadName(first));
    snprintf(text, size, "Gamepad: %s", name);
    if (count > 1) snprintf(text + strlen(text), size - strlen(text), " (+%d)", count - 1);
}

#define INFO_LINES 4
#define PANEL_ROWS (OSD_ITEMS + 1 + OSD_LIST_MAX)

enum { LINE_ITEM, LINE_GREY, LINE_VALUE };

/* a screen a finger works on: Android, a touch screen, the Steam Deck (its
 * Game Mode may not show the touch screen to SDL) */
static int touch_screen(void)
{
#if defined(__ANDROID__)
    return 1;
#else
    return SDL_GetNumTouchDevices() > 0 || getenv("SteamDeck") != NULL;
#endif
}

/* the height of the text and of a row: about 30 lines in the height of the
 * window, never smaller than 10 pixels; on a touch screen a row is about 7 mm
 * (by the display's DPI; else 1/14 of the height, and never more than 1/10 of
 * it) and the text a little more than half of it */
static void panel_sizes(SDL_Renderer *renderer, int height, int *text_h, int *row_h)
{
    int text = height / 30, row;

    if (text < 10) text = 10;
    row = text;
    if (touch_screen())
    {
        SDL_Window *window = SDL_RenderGetWindow(renderer);
        int display = (window != NULL) ? SDL_GetWindowDisplayIndex(window) : -1;
        float vdpi = 0;

        row = height / 14;
        if (display >= 0 && SDL_GetDisplayDPI(display, NULL, NULL, &vdpi) == 0 && vdpi >= 100 && vdpi <= 1000)
        {
            int wanted = (int)(vdpi * 7 / 25.4f);

            if (wanted > row) row = wanted;
        }
        if (row > height / 10) row = height / 10;
        if (row < text) row = text;
        if (text < row * 11 / 20) text = row * 11 / 20;
    }
    *text_h = text;
    *row_h = row;
}

void Game_OsdDraw(SDL_Renderer *renderer, int width, int height)
{
    char info_text[INFO_LINES][128], rows[PANEL_ROWS][128], note[64];
    uint8_t kind[PANEL_ROWS];
    int info = 0, total = 0, has_note = 0, text_h, row_h, pad, panel_w = 0, panel_h, rows_y, shown = 0, top = 0;
    int sel = -1, open = list_item, open_n = 0, current = -1, bar_w, i, y;

    panel_sizes(renderer, height, &text_h, &row_h);
    pad = text_h / 2;
    bar_w = (pad / 2 < 3) ? 3 : pad / 2;

    if (osd_shown)
    {
        snprintf(info_text[info++], sizeof(info_text[0]), "%s", GAME_NAME);
        if (frame_width > 0)
        {
            snprintf(info_text[info++], sizeof(info_text[0]), "Picture %dx%d, %s", frame_width, frame_height, frame_output);
            snprintf(info_text[info++], sizeof(info_text[0]), "Window %dx%d (x%.2f)", width, height,
                     (width * frame_height < height * frame_width) ? (double)width / frame_width : (double)height / frame_height);
        }
        else snprintf(info_text[info++], sizeof(info_text[0]), "Window %dx%d", width, height);
        gamepad_text(info_text[info++], sizeof(info_text[0]));

        if (open >= 0 && open < item_count && item_has_list(open))
        {
            open_n = item_values(open);
            current = item_current(open);
        }
        else open = -1;
        for (i = 0; i < item_count; i++)
        {
            const Game_OsdItem *entry = items[i];
            int v;

            if (entry->state != NULL) snprintf(rows[total], sizeof(rows[0]), "%s: %s", entry->label, entry->state());
            else snprintf(rows[total], sizeof(rows[0]), "%s", entry->label);
            kind[total++] = item_active(i) ? LINE_ITEM : LINE_GREY;
            if (i != open) continue;
            /* the list under its item, the value it has now marked */
            for (v = 0; v < open_n; v++)
            {
                snprintf(rows[total], sizeof(rows[0]), "%s %s", (v == current) ? "*" : " ", entry->value(v));
                kind[total++] = LINE_VALUE;
            }
        }
#if defined(__ANDROID__)
        snprintf(rows[total], sizeof(rows[0]), "Close (Back)");
#else
        if (osd_key != SDL_SCANCODE_UNKNOWN) snprintf(rows[total], sizeof(rows[0]), "Close (%s)", SDL_GetScancodeName(osd_key));
        else snprintf(rows[total], sizeof(rows[0]), "Close");
#endif
        kind[total++] = LINE_ITEM;
        sel = (open >= 0) ? open + 1 + list_sel : osd_sel;
        if (sel >= total) sel = total - 1;
    }
    if (message_live)
    {
        SDL_AtomicLock(&message_lock);
        snprintf(note, sizeof(note), "%s", message);
        SDL_AtomicUnlock(&message_lock);
        has_note = 1;
    }

    for (i = 0; i < info; i++)
    {
        int w = Game_UiTextWidth(info_text[i], text_h);

        if (w > panel_w) panel_w = w;
    }
    for (i = 0; i < total; i++)
    {
        int w = Game_UiTextWidth(rows[i], text_h) + ((kind[i] == LINE_VALUE) ? 4 : 2) * pad + bar_w;

        if (w > panel_w) panel_w = w;
    }
    if (has_note && Game_UiTextWidth(note, text_h) > panel_w) panel_w = Game_UiTextWidth(note, text_h);

    /* the information, a gap, the rows that fit in the window, the message */
    rows_y = 2 * pad + info * text_h + (osd_shown ? pad : 0);
    if (osd_shown)
    {
        shown = (height - 2 * pad - rows_y - (has_note ? text_h : 0)) / row_h;
        if (shown < 1) shown = 1;
        if (shown > total) shown = total;
        top = osd_top;
        if (osd_follow)
        {
            /* an open list wholly in the view if it can be, the chosen row always */
            if (open >= 0)
            {
                if (open + open_n >= top + shown) top = open + open_n - shown + 1;
                if (open < top) top = open;
            }
            if (sel < top) top = sel;
            if (sel >= top + shown) top = sel - shown + 1;
            osd_follow = 0;
        }
        if (top > total - shown) top = total - shown;
        if (top < 0) top = 0;
        osd_top = top;
    }
    /* from pad down: the rows' end, a pad under it */
    panel_h = rows_y + shown * row_h + (has_note ? text_h : 0);

    SDL_AtomicLock(&message_lock);
    drawn.x = pad;
    drawn.y = pad;
    drawn.w = osd_shown ? panel_w + 2 * pad : 0;
    drawn.h = osd_shown ? panel_h : 0;
    drawn.rows_y = rows_y;
    drawn.row_h = osd_shown ? row_h : 0;
    drawn.top = top;
    drawn.shown = shown;
    drawn.total = total;
    drawn.W = width;
    drawn.H = height;
    SDL_AtomicUnlock(&message_lock);

    if (fps_shown)
    {
        char fps[32];
        int fps_w;

        snprintf(fps, sizeof(fps), "FPS %d.%d", fps_tenths / 10, fps_tenths % 10);
        fps_w = Game_UiTextWidth(fps, text_h) + 2 * pad;
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 176);
        Game_UiFillRect(renderer, width - pad - fps_w, pad, fps_w, text_h + 2 * pad);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        Game_UiText(renderer, fps, width - fps_w, 2 * pad, text_h);
    }
    if (!osd_shown && !has_note) return;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 176);
    Game_UiFillRect(renderer, pad, pad, panel_w + 2 * pad, panel_h);
    SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
    for (i = 0, y = 2 * pad; i < info; i++, y += text_h) Game_UiText(renderer, info_text[i], 2 * pad, y, text_h);

    for (i = top, y = rows_y; i < top + shown; i++, y += row_h)
    {
        int x = 3 * pad;

        if (kind[i] == LINE_VALUE)
        {
            /* the list a little lighter than the panel */
            x += 2 * pad;
            SDL_SetRenderDrawColor(renderer, 40, 40, 56, 200);
            Game_UiFillRect(renderer, 2 * pad, y, panel_w - bar_w, row_h);
        }
        if (i == sel)
        {
            SDL_SetRenderDrawColor(renderer, 70, 110, 190, 220);
            Game_UiFillRect(renderer, pad + pad / 2, y, panel_w + pad - bar_w, row_h);
        }
        else if (row_h > text_h && i > top)
        {
            /* the rows for a finger, apart */
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 40);
            Game_UiFillRect(renderer, 2 * pad, y, panel_w - bar_w, 1);
        }
        switch (kind[i])
        {
            case LINE_GREY: SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255); break;
            case LINE_VALUE: SDL_SetRenderDrawColor(renderer, 220, 230, 255, 255); break;
            default: SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); break;
        }
        Game_UiText(renderer, rows[i], x, y + (row_h - text_h) / 2, text_h);
    }
    if (shown < total)
    {
        /* the rows scroll: where the view is in them */
        int track = shown * row_h, thumb = track * shown / total, at = track * top / total;

        if (thumb < bar_w) thumb = bar_w;
        if (at > track - thumb) at = track - thumb;
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 48);
        Game_UiFillRect(renderer, pad + panel_w + pad - bar_w, rows_y, bar_w, track);
        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 200);
        Game_UiFillRect(renderer, pad + panel_w + pad - bar_w, rows_y + at, bar_w, thumb);
    }
    if (has_note)
    {
        /* the message under the panel in yellow */
        SDL_SetRenderDrawColor(renderer, 255, 230, 96, 255);
        Game_UiText(renderer, note, 2 * pad, rows_y + shown * row_h, text_h);
    }
}
