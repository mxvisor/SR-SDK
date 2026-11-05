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

/* The port's hot keys and its on-screen display (Game_osd.h). */

#include <stdio.h>
#include <string.h>
#include "Game_osd.h"
#include "Game_ui.h"
#include "Game_hooks.h"

#ifndef GAME_NAME
#define GAME_NAME "game"
#endif

#define OSD_KEYS 16
#define MESSAGE_MS 1500

static struct {
    SDL_Scancode key;
    const Game_OsdKey *entry;
    int held;   /* its press was taken: so is its release */
} keys[OSD_KEYS];
static int key_count;

static SDL_Scancode osd_key = SDL_SCANCODE_F12;
static volatile int osd_shown;
static int osd_stamp;

/* written by the threads of the keys, read by the presenter's */
static SDL_SpinLock message_lock;
static char message[64];
static Uint32 message_until;
static volatile int message_live;

/* the presenter's thread */
static int frame_width, frame_height;
static char frame_output[64];
static Uint32 fps_start;
static int fps_frames, fps_tenths;

int Game_OsdParseKey(const char *value, SDL_Scancode *key)
{
    SDL_Scancode scancode;

    if (strcasecmp(value, "off") == 0)
    {
        *key = SDL_SCANCODE_UNKNOWN;
        return 1;
    }
    scancode = SDL_GetScancodeFromName(value);
    if (scancode == SDL_SCANCODE_UNKNOWN) return 0;
    *key = scancode;
    return 1;
}

void Game_OsdAddKey(SDL_Scancode key, const Game_OsdKey *entry)
{
    int i;

    if (entry == NULL) return;
    for (i = 0; i < key_count; i++)
    {
        if (key != SDL_SCANCODE_UNKNOWN && keys[i].key == key)
        {
            /* the one place where two keys of the port meet */
            fprintf(stderr, GAME_NAME ": the key %s of \"%s\" is \"%s\"'s already\n",
                    SDL_GetScancodeName(key), entry->label, keys[i].entry->label);
            key = SDL_SCANCODE_UNKNOWN;
        }
    }
    if (key_count == OSD_KEYS) return;
    keys[key_count].key = key;
    keys[key_count].entry = entry;
    keys[key_count].held = 0;
    key_count++;
    osd_stamp++;
}

void Game_OsdToggle(void)
{
    osd_shown = !osd_shown;
    osd_stamp++;
}

static const Game_OsdKey osd_entry = { "This panel", NULL, Game_OsdToggle, NULL };

void Game_OsdInit(void)
{
    Game_OsdAddKey(osd_key, &osd_entry);
#ifdef GAME_OSD_KEYS
    GAME_OSD_KEYS();
#endif
}

int Game_OsdConfigKey(const char *key, const char *value)
{
    if (strcasecmp(key, "Display_OSD_Key") != 0) return 0;
    if (!Game_OsdParseKey(value, &osd_key)) fprintf(stderr, GAME_NAME ": unknown Display_OSD_Key: %s\n", value);
    return 1;
}

int Game_OsdKeyEvent(const SDL_KeyboardEvent *key)
{
    int i;

    if (key->type != SDL_KEYDOWN && key->type != SDL_KEYUP) return 0;
    for (i = 0; i < key_count; i++)
    {
        const Game_OsdKey *entry = keys[i].entry;

        if (keys[i].key == SDL_SCANCODE_UNKNOWN || key->keysym.scancode != keys[i].key || entry->press == NULL) continue;
        if (key->type == SDL_KEYUP)
        {
            int held = keys[i].held;

            keys[i].held = 0;
            return held;
        }
        if (!keys[i].held && entry->active != NULL && !entry->active()) return 0;
        keys[i].held = 1;
        if (!key->repeat)
        {
            entry->press();
            if (entry->state != NULL)
            {
                char text[sizeof(message)];

                snprintf(text, sizeof(text), "%s: %s", entry->label, entry->state());
                Game_OsdMessage(text);
            }
            osd_stamp++;
        }
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

#define PANEL_LINES (6 + OSD_KEYS)

void Game_OsdDraw(SDL_Renderer *renderer, int width, int height)
{
    char lines[PANEL_LINES][128];
    int count = 0, line_h, pad, panel_w, i, y;

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
        for (i = 0; i < key_count && count < PANEL_LINES; i++)
        {
            const Game_OsdKey *entry = keys[i].entry;
            const char *name = (keys[i].key != SDL_SCANCODE_UNKNOWN) ? SDL_GetScancodeName(keys[i].key) : "-";

            if (entry->state != NULL && (entry->active == NULL || entry->active()))
                snprintf(lines[count++], sizeof(lines[0]), "%-4s %s: %s", name, entry->label, entry->state());
            else if (entry->active != NULL && !entry->active())
                snprintf(lines[count++], sizeof(lines[0]), "%-4s %s (off)", name, entry->label);
            else
                snprintf(lines[count++], sizeof(lines[0]), "%-4s %s", name, entry->label);
        }
    }
    if (message_live && count < PANEL_LINES)
    {
        SDL_AtomicLock(&message_lock);
        snprintf(lines[count++], sizeof(lines[0]), "%s", message);
        SDL_AtomicUnlock(&message_lock);
    }
    if (count == 0) return;

    panel_w = 0;
    for (i = 0; i < count; i++)
    {
        int w = Game_UiTextWidth(lines[i], line_h);

        if (w > panel_w) panel_w = w;
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
    Game_UiFillRect(renderer, pad, pad, panel_w + 2 * pad, count * line_h + 2 * pad);
    for (i = 0, y = 2 * pad; i < count; i++, y += line_h)
    {
        /* the message under the panel in yellow */
        if (message_live && i == count - 1) SDL_SetRenderDrawColor(renderer, 255, 230, 96, 255);
        else SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        Game_UiText(renderer, lines[i], 2 * pad, y, line_h);
    }
}
