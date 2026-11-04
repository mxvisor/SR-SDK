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

/* The user32 functions around the port's one window that WinApi-user32.c does
 * not answer: the desktop, the minimized state, the frame (there is none) and the
 * menus.
 *
 * The menus are the game's own: LoadMenuA reads the RT_MENU resource of the
 * original EXE (GAME_MAIN_FILENAME in the game directory; the recompiled image
 * has no .rsrc) into a tree that the game can change (CheckMenuItem,
 * EnableMenuItem, InsertMenuA, ...).  The menu bar is drawn over the picture by
 * WinApi-menubar.c, which reads the window's menu through GetMenu_c,
 * GetMenuItemCount_c and Winapi_MenuItem; the dialogs (WinApi-dialog.c) read their
 * templates through Winapi_LoadResource.
 *
 * The functions take the guest's raw 32-bit arguments; the game's import shim
 * bridges them (tools/gen_import_shim.py). */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "platform.h"
#include "Game_memory.h"
#include "Game_hooks.h"
#include "WinApi.h"
#include "WinApi-user32.h"
#if !defined(_WIN32) && !defined(__WIN32__) && !defined(__WINDOWS__)
#include "CLIB.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ window */

/* the guest's handle of the desktop (the main window is 1, WinApi-user32.c) */
#define DESKTOP_WINDOW 2u

uint32_t CCALL GetDesktopWindow_c(void)
{
    return DESKTOP_WINDOW;
}

/* The port's window has no frame, title bar or menu bar: the rectangle of the
 * window is its client rectangle, whatever the style */
uint32_t CCALL AdjustWindowRect_c(uint32_t lpRect, uint32_t dwStyle, uint32_t bMenu)
{
    (void)dwStyle; (void)bMenu;
    return (lpRect != 0) ? 1 : 0;
}

/* minimized: the shown SDL window is (the port has one; a renderer may have
 * made it anew, so it is looked for) */
uint32_t CCALL IsIconic_c(uint32_t hWnd)
{
    uint32_t id;

    if (hWnd == 0 || hWnd == DESKTOP_WINDOW) return 0;
    for (id = 1; id <= 64; id++)
    {
        SDL_Window *window = SDL_GetWindowFromID(id);
        if (window != NULL)
        {
            uint32_t flags = SDL_GetWindowFlags(window);
            if ((flags & SDL_WINDOW_SHOWN) && (flags & SDL_WINDOW_MINIMIZED)) return 1;
        }
    }
    return 0;
}

/* no popup windows: the window itself */
uint32_t CCALL GetLastActivePopup_c(uint32_t hWnd)
{
    return hWnd;
}

/* ------------------------------------------------------------------- menus */

#define MF_GRAYED_c      0x0001
#define MF_DISABLED_c    0x0002
#define MF_BITMAP_c      0x0004
#define MF_CHECKED_c     0x0008
#define MF_POPUP_c       0x0010
#define MF_END_c         0x0080
#define MF_OWNERDRAW_c   0x0100
#define MF_BYPOSITION_c  0x0400
#define MF_SEPARATOR_c   0x0800

#define ERROR_RESOURCE_NAME_NOT_FOUND_c 1814
#define ERROR_INVALID_MENU_HANDLE_c     1401
#define ERROR_MENU_ITEM_NOT_FOUND_c     1456

/* a menu handle is MENU_HANDLE_BASE + the slot: no guest address, no other handle */
#define MENU_HANDLE_BASE 0x4d450000u
#define MENU_MAX 256
/* the depth of the popups, against a menu that holds itself */
#define MENU_DEPTH 8

typedef struct {
    uint32_t flags;     /* MF_* (type and state), without MF_BYPOSITION and MF_END */
    uint32_t id;        /* the command, or the handle of the popup (MF_POPUP) */
    char *text;         /* NULL for a separator, a bitmap and an owner-drawn item */
} menu_item;

typedef struct {
    int used;
    unsigned int count;
    menu_item *items;
} menu_t;

static menu_t menus[MENU_MAX];

/* the menus of the windows (SetMenu) */
static struct {
    uint32_t hwnd;
    uint32_t menu;
} window_menus[4];

static menu_t *menu_get(uint32_t hMenu)
{
    uint32_t slot = hMenu - MENU_HANDLE_BASE;

    if (slot < MENU_MAX && menus[slot].used) return &menus[slot];
    return NULL;
}

static uint32_t menu_new(void)
{
    unsigned int slot;

    for (slot = 0; slot < MENU_MAX; slot++)
    {
        if (!menus[slot].used)
        {
            menus[slot].used = 1;
            menus[slot].count = 0;
            menus[slot].items = NULL;
            return MENU_HANDLE_BASE + slot;
        }
    }
    return 0;
}

/* the item goes before pos (at the end when pos is past it); text is copied */
static int menu_insert(menu_t *menu, unsigned int pos, uint32_t flags, uint32_t id, const char *text)
{
    menu_item *items;

    items = (menu_item *)realloc(menu->items, (menu->count + 1) * sizeof(menu_item));
    if (items == NULL) return 0;
    menu->items = items;
    if (pos > menu->count) pos = menu->count;
    memmove(&items[pos + 1], &items[pos], (menu->count - pos) * sizeof(menu_item));
    items[pos].flags = flags & ~(MF_BYPOSITION_c | MF_END_c);
    items[pos].id = id;
    items[pos].text = (text != NULL) ? strdup(text) : NULL;
    menu->count++;
    return 1;
}

static void menu_destroy(uint32_t hMenu, int depth);

/* the item leaves the menu; its popup is destroyed with it (DeleteMenu) or kept (RemoveMenu) */
static void menu_remove(menu_t *menu, unsigned int pos, int destroy_popup, int depth)
{
    menu_item item = menu->items[pos];

    menu->count--;
    memmove(&menu->items[pos], &menu->items[pos + 1], (menu->count - pos) * sizeof(menu_item));
    free(item.text);
    if (destroy_popup && (item.flags & MF_POPUP_c)) menu_destroy(item.id, depth + 1);
}

static void menu_destroy(uint32_t hMenu, int depth)
{
    menu_t *menu = menu_get(hMenu);
    unsigned int index;

    if (menu == NULL || depth > MENU_DEPTH) return;
    while (menu->count != 0)
    {
        menu_remove(menu, menu->count - 1, 1, depth);
    }
    free(menu->items);
    menu->items = NULL;
    menu->used = 0;
    for (index = 0; index < sizeof(window_menus) / sizeof(window_menus[0]); index++)
    {
        if (window_menus[index].menu == hMenu) window_menus[index].menu = 0;
    }
}

/* The menu that holds the item, and its position there: by position in this
 * menu, or by command in this menu and its popups (the popup's handle is its
 * command) */
static menu_t *menu_find(uint32_t hMenu, uint32_t item, uint32_t flags, unsigned int *pos, int depth)
{
    menu_t *menu = menu_get(hMenu);
    unsigned int index;

    if (menu == NULL || depth > MENU_DEPTH) return NULL;
    if (flags & MF_BYPOSITION_c)
    {
        if (item >= menu->count) return NULL;
        *pos = item;
        return menu;
    }
    for (index = 0; index < menu->count; index++)
    {
        if (menu->items[index].id == item)
        {
            *pos = index;
            return menu;
        }
        if (menu->items[index].flags & MF_POPUP_c)
        {
            menu_t *found = menu_find(menu->items[index].id, item, flags, pos, depth + 1);
            if (found != NULL) return found;
        }
    }
    return NULL;
}

/* ---- the RT_MENU resource of the EXE ---- */

typedef struct {
    const uint8_t *data;
    uint32_t size;
} span;

static int rd16(span s, uint32_t offset, uint32_t *value)
{
    if (offset > s.size || s.size - offset < 2) return 0;
    *value = s.data[offset] | (s.data[offset + 1] << 8);
    return 1;
}

static int rd32(span s, uint32_t offset, uint32_t *value)
{
    if (offset > s.size || s.size - offset < 4) return 0;
    *value = s.data[offset] | (s.data[offset + 1] << 8) | (s.data[offset + 2] << 16) | ((uint32_t)s.data[offset + 3] << 24);
    return 1;
}

static uint8_t *exe_read(uint32_t *size)
{
    char path[8192];
    FILE *file;
    long length;
    uint8_t *data = NULL;

#if defined(_WIN32) || defined(__WIN32__) || defined(__WINDOWS__)
    snprintf(path, sizeof(path), "%s", GAME_MAIN_FILENAME);
#else
    if (!CLIB_FindFile(GAME_MAIN_FILENAME, path)) return NULL;
#endif
    file = fopen(path, "rb");
    if (file == NULL) return NULL;
    if (fseek(file, 0, SEEK_END) == 0 && (length = ftell(file)) > 0 && fseek(file, 0, SEEK_SET) == 0)
    {
        data = (uint8_t *)malloc(length);
        if (data != NULL && fread(data, 1, length, file) != (size_t)length)
        {
            free(data);
            data = NULL;
        }
        *size = (uint32_t)length;
    }
    fclose(file);
    return data;
}

/* the file offset of an RVA, through the section table */
static int exe_offset(span exe, uint32_t rva, uint32_t *offset)
{
    uint32_t pe, sections, optional_size, table, index;

    if (!rd32(exe, 0x3c, &pe) || !rd16(exe, pe + 6, &sections) || !rd16(exe, pe + 20, &optional_size)) return 0;
    table = pe + 24 + optional_size;
    for (index = 0; index < sections; index++)
    {
        uint32_t virtual_address, raw_size, raw_pointer;
        uint32_t entry = table + 40 * index;

        if (!rd32(exe, entry + 12, &virtual_address) || !rd32(exe, entry + 16, &raw_size) ||
            !rd32(exe, entry + 20, &raw_pointer)) return 0;
        if (rva >= virtual_address && rva - virtual_address < raw_size)
        {
            *offset = raw_pointer + (rva - virtual_address);
            return 1;
        }
    }
    return 0;
}

/* The entry of a resource directory: by id, by name (case-insensitive, as
 * Windows compares them), or the first one (id 0xffffffff) */
static int resource_entry(span rsrc, uint32_t dir, uint32_t id, const char *name, uint32_t *target)
{
    uint32_t named, ids, index;

    if (!rd16(rsrc, dir + 12, &named) || !rd16(rsrc, dir + 14, &ids)) return 0;
    for (index = 0; index < named + ids; index++)
    {
        uint32_t entry_name, entry_target;

        if (!rd32(rsrc, dir + 16 + 8 * index, &entry_name) || !rd32(rsrc, dir + 20 + 8 * index, &entry_target)) return 0;
        if (name == NULL && id == 0xffffffffu)
        {
            *target = entry_target;
            return 1;
        }
        if (name != NULL && (entry_name & 0x80000000u))
        {
            uint32_t string = entry_name & 0x7fffffffu, length, ch, i;

            if (!rd16(rsrc, string, &length) || length != strlen(name)) continue;
            for (i = 0; i < length; i++)
            {
                uint32_t want = (unsigned char)name[i];
                if (!rd16(rsrc, string + 2 + 2 * i, &ch)) return 0;
                if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
                if (want >= 'a' && want <= 'z') want -= 'a' - 'A';
                if (ch != want) break;
            }
            if (i == length)
            {
                *target = entry_target;
                return 1;
            }
        }
        else if (name == NULL && !(entry_name & 0x80000000u) && entry_name == id)
        {
            *target = entry_target;
            return 1;
        }
    }
    return 0;
}

/* the data of a resource: type, then id or name, then the first language */
static int resource_find(span exe, uint32_t type, uint32_t id, const char *name, span *found)
{
    uint32_t pe, rva, size, offset, target, data_rva, data_size, data_offset;
    span rsrc;

    if (!rd32(exe, 0x3c, &pe) || !rd32(exe, pe + 24 + 96 + 2 * 8, &rva) || !rd32(exe, pe + 24 + 100 + 2 * 8, &size)) return 0;
    if (rva == 0 || !exe_offset(exe, rva, &offset) || offset > exe.size) return 0;
    rsrc.data = exe.data + offset;
    rsrc.size = (exe.size - offset < size) ? exe.size - offset : size;

    if (!resource_entry(rsrc, 0, type, NULL, &target) || !(target & 0x80000000u)) return 0;
    if (!resource_entry(rsrc, target & 0x7fffffffu, id, name, &target) || !(target & 0x80000000u)) return 0;
    if (!resource_entry(rsrc, target & 0x7fffffffu, 0xffffffffu, NULL, &target) || (target & 0x80000000u)) return 0;
    if (!rd32(rsrc, target, &data_rva) || !rd32(rsrc, target + 4, &data_size)) return 0;
    if (!exe_offset(exe, data_rva, &data_offset) || data_offset > exe.size || exe.size - data_offset < data_size) return 0;
    found->data = exe.data + data_offset;
    found->size = data_size;
    return 1;
}

/* a UTF-16 character in the game's ANSI text: Latin-1 and the quotes and dashes
 * of Windows-1252; others are '?' */
unsigned char Winapi_AnsiChar(uint32_t ch)
{
    switch (ch)
    {
        case 0x2018: return 0x91;
        case 0x2019: return 0x92;
        case 0x201c: return 0x93;
        case 0x201d: return 0x94;
        case 0x2013: return 0x96;
        case 0x2014: return 0x97;
        case 0x2122: return 0x99;
    }
    return (ch < 0x100) ? (unsigned char)ch : '?';
}

/* a UTF-16 string of the template, to the game's ANSI text (Winapi_AnsiChar) */
static int read_text(span s, uint32_t *offset, char **text)
{
    uint32_t start = *offset, length = 0, ch, i;

    while (1)
    {
        if (!rd16(s, start + 2 * length, &ch)) return 0;
        if (ch == 0) break;
        length++;
    }
    *text = (char *)malloc(length + 1);
    if (*text == NULL) return 0;
    for (i = 0; i < length; i++)
    {
        rd16(s, start + 2 * i, &ch);
        (*text)[i] = (char)Winapi_AnsiChar(ch);
    }
    (*text)[length] = 0;
    *offset = start + 2 * (length + 1);
    return 1;
}

/* the items of one menu of a MENU (ex = 0) or MENUEX (ex = 1) template */
static int parse_items(span s, uint32_t *offset, uint32_t hMenu, int ex, int depth)
{
    if (depth > MENU_DEPTH) return 0;
    while (1)
    {
        uint32_t flags, id = 0, last, popup, hPopup = 0;
        char *text;
        int ok;

        if (ex)
        {
            uint32_t type, state, info;

            if (!rd32(s, *offset, &type) || !rd32(s, *offset + 4, &state) || !rd32(s, *offset + 8, &id) || !rd16(s, *offset + 12, &info)) return 0;
            *offset += 14;
            if (!read_text(s, offset, &text)) return 0;
            *offset = (*offset + 3) & ~3u;
            flags = (type & (MF_BITMAP_c | MF_OWNERDRAW_c | MF_SEPARATOR_c)) | (state & (MF_GRAYED_c | MF_DISABLED_c | MF_CHECKED_c));
            popup = info & 0x01;
            last = info & 0x80;
            if (popup) *offset += 4;                /* dwHelpId */
        }
        else
        {
            if (!rd16(s, *offset, &flags)) return 0;
            *offset += 2;
            popup = flags & MF_POPUP_c;
            last = flags & MF_END_c;
            if (!popup)
            {
                if (!rd16(s, *offset, &id)) return 0;
                *offset += 2;
            }
            if (!read_text(s, offset, &text)) return 0;
            /* MENUITEM SEPARATOR: no flags, no command, no text */
            if (!popup && (flags & ~MF_END_c) == 0 && id == 0 && text[0] == 0) flags |= MF_SEPARATOR_c;
        }

        if (popup)
        {
            hPopup = menu_new();
            if (hPopup == 0 || !parse_items(s, offset, hPopup, ex, depth + 1))
            {
                free(text);
                menu_destroy(hPopup, 0);
                return 0;
            }
            id = hPopup;
            flags |= MF_POPUP_c;
        }
        if (flags & MF_SEPARATOR_c)
        {
            free(text);
            text = NULL;
        }
        ok = menu_insert(menu_get(hMenu), 0xffffffffu, flags, id, text);
        free(text);
        if (!ok)
        {
            menu_destroy(hPopup, 0);
            return 0;
        }
        if (last) return 1;
    }
}

static uint32_t parse_menu(span s)
{
    uint32_t version, header, offset, hMenu;

    if (!rd16(s, 0, &version) || !rd16(s, 2, &header) || version > 1) return 0;
    offset = 4 + header;
    hMenu = menu_new();
    if (hMenu == 0) return 0;
    if (!parse_items(s, &offset, hMenu, (int)version, 0))
    {
        menu_destroy(hMenu, 0);
        return 0;
    }
    return hMenu;
}

uint8_t *Winapi_LoadResource(uint32_t type, uint32_t lpName, uint32_t *size)
{
    const char *name = NULL;
    uint32_t id = lpName, exe_size = 0;
    uint8_t *data, *copy = NULL;
    span exe, resource;

    if (lpName >= 0x10000)
    {
        name = (const char *)GUEST_MEM(lpName);
        if (name[0] == '#')
        {
            id = (uint32_t)strtoul(name + 1, NULL, 10);
            name = NULL;
        }
    }

    data = exe_read(&exe_size);
    if (data == NULL) return NULL;
    exe.data = data;
    exe.size = exe_size;
    if (resource_find(exe, type, id, name, &resource) && (copy = (uint8_t *)malloc(resource.size ? resource.size : 1)) != NULL)
    {
        memcpy(copy, resource.data, resource.size);
        *size = resource.size;
    }
    free(data);
    return copy;
}

uint32_t CCALL LoadMenuA_c(uint32_t hInstance, uint32_t lpMenuName)
{
    uint32_t size = 0, hMenu = 0;
    uint8_t *data;

    (void)hInstance;                                /* the EXE's own resources */
    data = Winapi_LoadResource(4 /* RT_MENU */, lpMenuName, &size);
    if (data != NULL)
    {
        span resource;

        resource.data = data;
        resource.size = size;
        hMenu = parse_menu(resource);
        free(data);
    }
    if (hMenu == 0) Winapi_SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND_c);
    return hMenu;
}

uint32_t CCALL DestroyMenu_c(uint32_t hMenu)
{
    if (menu_get(hMenu) == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_MENU_HANDLE_c);
        return 0;
    }
    menu_destroy(hMenu, 0);
    return 1;
}

uint32_t CCALL SetMenu_c(uint32_t hWnd, uint32_t hMenu)
{
    unsigned int index, free_index = sizeof(window_menus) / sizeof(window_menus[0]);

    if (hWnd == 0 || (hMenu != 0 && menu_get(hMenu) == NULL))
    {
        Winapi_SetLastError(ERROR_INVALID_MENU_HANDLE_c);
        return 0;
    }
    for (index = 0; index < sizeof(window_menus) / sizeof(window_menus[0]); index++)
    {
        if (window_menus[index].hwnd == hWnd)
        {
            window_menus[index].menu = hMenu;
            return 1;
        }
        if (window_menus[index].hwnd == 0 && free_index > index) free_index = index;
    }
    if (free_index == sizeof(window_menus) / sizeof(window_menus[0])) return 0;
    window_menus[free_index].hwnd = hWnd;
    window_menus[free_index].menu = hMenu;
    return 1;
}

uint32_t CCALL GetMenu_c(uint32_t hWnd)
{
    unsigned int index;

    for (index = 0; index < sizeof(window_menus) / sizeof(window_menus[0]); index++)
    {
        if (hWnd != 0 && window_menus[index].hwnd == hWnd) return window_menus[index].menu;
    }
    return 0;
}

/* the port draws no menu bar (WinApi-menubar.c reads the menu when it shows it) */
uint32_t CCALL DrawMenuBar_c(uint32_t hWnd)
{
    (void)hWnd;
    return 1;
}

uint32_t CCALL GetSubMenu_c(uint32_t hMenu, uint32_t nPos)
{
    menu_t *menu = menu_get(hMenu);

    if (menu == NULL || nPos >= menu->count || !(menu->items[nPos].flags & MF_POPUP_c)) return 0;
    return menu->items[nPos].id;
}

uint32_t CCALL GetMenuItemCount_c(uint32_t hMenu)
{
    menu_t *menu = menu_get(hMenu);

    if (menu == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_MENU_HANDLE_c);
        return 0xffffffffu;
    }
    return menu->count;
}

uint32_t CCALL GetMenuItemID_c(uint32_t hMenu, uint32_t nPos)
{
    menu_t *menu = menu_get(hMenu);

    if (menu == NULL || nPos >= menu->count || (menu->items[nPos].flags & MF_POPUP_c)) return 0xffffffffu;
    return menu->items[nPos].id;
}

/* the flags of the item; a popup's has the count of its items in the second byte */
uint32_t CCALL GetMenuState_c(uint32_t hMenu, uint32_t uId, uint32_t uFlags)
{
    unsigned int pos;
    menu_t *menu = menu_find(hMenu, uId, uFlags, &pos, 0);
    menu_t *popup;

    if (menu == NULL) return 0xffffffffu;
    if ((menu->items[pos].flags & MF_POPUP_c) && (popup = menu_get(menu->items[pos].id)) != NULL)
    {
        return (popup->count << 8) | (menu->items[pos].flags & 0xff);
    }
    return menu->items[pos].flags;
}

/* the previous state: MF_CHECKED or MF_UNCHECKED, -1 for no item */
uint32_t CCALL CheckMenuItem_c(uint32_t hMenu, uint32_t uIDCheckItem, uint32_t uCheck)
{
    unsigned int pos;
    menu_t *menu = menu_find(hMenu, uIDCheckItem, uCheck, &pos, 0);
    uint32_t previous;

    if (menu == NULL) return 0xffffffffu;
    previous = menu->items[pos].flags & MF_CHECKED_c;
    menu->items[pos].flags = (menu->items[pos].flags & ~MF_CHECKED_c) | (uCheck & MF_CHECKED_c);
    return previous;
}

/* the previous state: MF_ENABLED, MF_GRAYED, MF_DISABLED; -1 for no item */
uint32_t CCALL EnableMenuItem_c(uint32_t hMenu, uint32_t uIDEnableItem, uint32_t uEnable)
{
    unsigned int pos;
    menu_t *menu = menu_find(hMenu, uIDEnableItem, uEnable, &pos, 0);
    uint32_t previous;

    if (menu == NULL) return 0xffffffffu;
    previous = menu->items[pos].flags & (MF_GRAYED_c | MF_DISABLED_c);
    menu->items[pos].flags = (menu->items[pos].flags & ~(MF_GRAYED_c | MF_DISABLED_c)) | (uEnable & (MF_GRAYED_c | MF_DISABLED_c));
    return previous;
}

static uint32_t remove_item(uint32_t hMenu, uint32_t uPosition, uint32_t uFlags, int destroy_popup)
{
    unsigned int pos;
    menu_t *menu = menu_find(hMenu, uPosition, uFlags, &pos, 0);

    if (menu == NULL)
    {
        Winapi_SetLastError(ERROR_MENU_ITEM_NOT_FOUND_c);
        return 0;
    }
    menu_remove(menu, pos, destroy_popup, 0);
    return 1;
}

/* the popup of the item stays, for the game to insert again */
uint32_t CCALL RemoveMenu_c(uint32_t hMenu, uint32_t uPosition, uint32_t uFlags)
{
    return remove_item(hMenu, uPosition, uFlags, 0);
}

uint32_t CCALL DeleteMenu_c(uint32_t hMenu, uint32_t uPosition, uint32_t uFlags)
{
    return remove_item(hMenu, uPosition, uFlags, 1);
}

/* before the item uPosition (by position: -1 or past the end appends); the new
 * item is a string, a separator, or (MF_BITMAP, MF_OWNERDRAW) a value without text */
uint32_t CCALL InsertMenuA_c(uint32_t hMenu, uint32_t uPosition, uint32_t uFlags, uint32_t uIDNewItem, uint32_t lpNewItem)
{
    menu_t *menu = menu_get(hMenu);
    unsigned int pos;
    const char *text = NULL;

    if (menu == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_MENU_HANDLE_c);
        return 0;
    }
    if (uFlags & MF_BYPOSITION_c)
    {
        pos = (uPosition > menu->count) ? menu->count : uPosition;
    }
    else
    {
        menu = menu_find(hMenu, uPosition, uFlags, &pos, 0);
        if (menu == NULL)
        {
            /* not found: Windows appends it to the menu */
            menu = menu_get(hMenu);
            pos = menu->count;
        }
    }
    if ((uFlags & MF_POPUP_c) && menu_get(uIDNewItem) == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_MENU_HANDLE_c);
        return 0;
    }
    if (!(uFlags & (MF_BITMAP_c | MF_OWNERDRAW_c | MF_SEPARATOR_c)) && lpNewItem != 0)
    {
        text = (const char *)GUEST_MEM(lpNewItem);
    }
    return menu_insert(menu, pos, uFlags, uIDNewItem, text);
}

int Winapi_MenuItem(uint32_t hMenu, unsigned int pos, uint32_t *flags, uint32_t *id, const char **text)
{
    menu_t *menu = menu_get(hMenu);

    if (menu == NULL || pos >= menu->count) return 0;
    if (flags != NULL) *flags = menu->items[pos].flags;
    if (id != NULL) *id = menu->items[pos].id;
    if (text != NULL) *text = menu->items[pos].text;
    return 1;
}

#ifdef __cplusplus
}
#endif
