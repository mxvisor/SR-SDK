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

/* The modal dialogs of the game (DialogBoxParamA), drawn by the port over the
 * picture (Game_ui.h).  The template is the RT_DIALOG resource of the game's
 * EXE (Winapi_LoadResource); its controls are push buttons, group boxes, static
 * texts and drop-down lists (CBS_DROPDOWNLIST), enough for the dialogs of a
 * game's settings.  The dialog procedure is the guest's: it gets WM_INITDIALOG
 * and WM_COMMAND (BN_CLICKED, CBN_SELCHANGE), fills and reads the lists with
 * SendMessageA / SendDlgItemMessageA and ends with EndDialog.  While the dialog
 * is open DialogBoxParamA runs the message pump for it alone (Winapi_ModalLoop):
 * the game waits, as it does in Windows.  Keys: Tab and Shift+Tab, the arrows,
 * Enter (the focused or the default button), Escape (IDCANCEL), Space; the mouse
 * and touches as well.
 *
 * SendMessageA belongs to the game or to the stubs: it hands the messages to
 * the dialog's controls to Winapi_DialogMessage.  The dialogs open only in a
 * game with GAME_WINAPI_DIALOGS 1 (Game_hooks.h).
 *
 * The functions take the guest's raw 32-bit arguments; the game's import shim
 * bridges them (tools/gen_import_shim.py). */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_MSC_VER)
#include <strings.h>
#endif
#include <SDL.h>
#include "Game_memory.h"
#include "Game_hooks.h"
#include "Game_ui.h"
#include "touchgamepad.h"
#include "WinApi.h"
#include "WinApi-user32.h"
#include "WinApi-ui.h"

#define eprintf(...) fprintf(stderr,__VA_ARGS__)

/* GAME_WINAPI_DIALOGS 1 (Game_hooks.h): the game's dialogs open; 0: DialogBoxParamA
 * returns 0 at once, as its stub did */
#ifndef GAME_WINAPI_DIALOGS
#define GAME_WINAPI_DIALOGS 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

uint32_t CCALL Winapi_CallGuest(uint32_t func, unsigned int nargs, const uint32_t *args);

#define WM_SETTEXT_c        0x000c
#define WM_GETTEXT_c        0x000d
#define WM_GETTEXTLENGTH_c  0x000e
#define WM_INITDIALOG_c     0x0110
#define WM_COMMAND_c        0x0111

#define BM_GETCHECK_c       0x00f0
#define BM_SETCHECK_c       0x00f1

#define CB_ADDSTRING_c      0x0143
#define CB_DELETESTRING_c   0x0144
#define CB_GETCOUNT_c       0x0146
#define CB_GETCURSEL_c      0x0147
#define CB_GETLBTEXT_c      0x0148
#define CB_GETLBTEXTLEN_c   0x0149
#define CB_INSERTSTRING_c   0x014a
#define CB_RESETCONTENT_c   0x014b
#define CB_FINDSTRING_c     0x014c
#define CB_SELECTSTRING_c   0x014d
#define CB_SETCURSEL_c      0x014e
#define CB_SHOWDROPDOWN_c   0x014f
#define CB_GETITEMDATA_c    0x0150
#define CB_SETITEMDATA_c    0x0151
#define CB_GETDROPPEDSTATE_c 0x0157
#define CB_FINDSTRINGEXACT_c 0x0158
#define CB_ERR_c            0xffffffffu

#define BN_CLICKED_c        0
#define CBN_SELCHANGE_c     1
#define CBN_DROPDOWN_c      7
#define CBN_CLOSEUP_c       8

#define IDOK_c              1
#define IDCANCEL_c          2

#define WS_TABSTOP_c        0x00010000u
#define WS_DISABLED_c       0x08000000u
#define WS_VISIBLE_c        0x10000000u
#define DS_SETFONT_c        0x40

#define BS_TYPEMASK_c       0x0f
#define BS_DEFPUSHBUTTON_c  0x01
#define BS_GROUPBOX_c       0x07
#define SS_TYPEMASK_c       0x1f
#define SS_CENTER_c         0x01
#define SS_RIGHT_c          0x02

/* the dialog's handle; its controls are DIALOG_HANDLE + 1 + their index */
#define DIALOG_HANDLE 0x444c0000u
#define MAX_CONTROLS 64

enum { CTL_BUTTON, CTL_STATIC, CTL_COMBO, CTL_OTHER };

typedef struct {
    int type;
    uint32_t style;
    uint32_t id;
    int x, y, w, h;             /* dialog units */
    char *text;
    char **items;               /* a list: its strings, data and selection */
    uint32_t *data;
    int count, sel;
    uint32_t check;             /* a check box or radio button: BST_CHECKED 1 */
} control;

static struct {
    int open, ended;
    uint32_t result, proc;
    char *title;
    int cx, cy;                 /* dialog units */
    control ctl[MAX_CONTROLS];
    int n;
    int focus;                  /* the control with the focus, -1 none */
    int pressed;                /* the button held by the pointer, -1 none */
    int dropped;                /* the list that is open, -1 none */
    int top;                    /* the first row the open list shows */
    int pointer;                /* the pointer (mouse button, finger) is down */
    int drag_y, dragged;        /* a finger scrolling the open list */
} dlg;

/* the layout in pixels */
static struct {
    int W, H, th, x, y, w, h, title_h;
    float dux, duy;
} dl;

/* ---- the template ---- */

typedef struct {
    const uint8_t *data;
    uint32_t size, offset;
    int ok;
} reader;

static uint32_t rd16(reader *r)
{
    uint32_t v;

    if (r->offset + 2 > r->size) { r->ok = 0; return 0; }
    v = r->data[r->offset] | (r->data[r->offset + 1] << 8);
    r->offset += 2;
    return v;
}

static uint32_t rd32(reader *r)
{
    uint32_t lo = rd16(r);
    return lo | (rd16(r) << 16);
}

static void align4(reader *r)
{
    r->offset = (r->offset + 3) & ~3u;
}

/* a string or ordinal of the template: the string (malloc), or NULL with the ordinal */
static char *rd_sz(reader *r, uint32_t *ordinal)
{
    uint32_t first = rd16(r), length = 0, start, i;
    char *text;

    if (ordinal != NULL) *ordinal = 0;
    if (first == 0xffff)
    {
        uint32_t value = rd16(r);
        if (ordinal != NULL) *ordinal = value;
        return NULL;
    }
    start = r->offset - 2;
    r->offset = start;
    while (r->ok && rd16(r) != 0) length++;
    text = (char *)malloc(length + 1);
    if (text == NULL) { r->ok = 0; return NULL; }
    for (i = 0; i < length; i++)
    {
        text[i] = (char)Winapi_AnsiChar(r->data[start + 2 * i] | (r->data[start + 2 * i + 1] << 8));
    }
    text[length] = 0;
    return text;
}

static char *dup_text(const char *text)
{
    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1);

    if (copy != NULL) memcpy(copy, text, length + 1);
    return copy;
}

static void free_dialog(void)
{
    int i, j;

    for (i = 0; i < dlg.n; i++)
    {
        free(dlg.ctl[i].text);
        for (j = 0; j < dlg.ctl[i].count; j++) free(dlg.ctl[i].items[j]);
        free(dlg.ctl[i].items);
        free(dlg.ctl[i].data);
    }
    free(dlg.title);
    memset(&dlg, 0, sizeof(dlg));
}

/* DLGTEMPLATE or DLGTEMPLATEEX */
static int parse_template(const uint8_t *data, uint32_t size)
{
    reader r;
    uint32_t style, count, i, ordinal;
    int ex;

    r.data = data;
    r.size = size;
    r.offset = 0;
    r.ok = 1;
    ex = (size >= 4 && data[0] == 1 && data[1] == 0 && data[2] == 0xff && data[3] == 0xff);
    if (ex)
    {
        r.offset = 8;                       /* dlgVer, signature, helpID */
        rd32(&r);                           /* exStyle */
        style = rd32(&r);
    }
    else
    {
        style = rd32(&r);
        rd32(&r);                           /* dwExtendedStyle */
    }
    count = rd16(&r);
    rd16(&r); rd16(&r);                     /* x, y: the dialog is centred */
    dlg.cx = (int16_t)rd16(&r);
    dlg.cy = (int16_t)rd16(&r);
    free(rd_sz(&r, NULL));                  /* menu */
    free(rd_sz(&r, NULL));                  /* class */
    dlg.title = rd_sz(&r, NULL);
    if (style & DS_SETFONT_c)
    {
        rd16(&r);                           /* point size */
        if (ex) { rd16(&r); rd16(&r); }     /* weight, italic and charset */
        free(rd_sz(&r, NULL));              /* typeface */
    }
    if (!r.ok) return 0;

    for (i = 0; i < count && i < MAX_CONTROLS; i++)
    {
        control *c = &dlg.ctl[dlg.n];
        char *cls, *text;

        align4(&r);
        if (ex)
        {
            rd32(&r);                       /* helpID */
            rd32(&r);                       /* exStyle */
            c->style = rd32(&r);
        }
        else
        {
            c->style = rd32(&r);
            rd32(&r);                       /* dwExtendedStyle */
        }
        c->x = (int16_t)rd16(&r);
        c->y = (int16_t)rd16(&r);
        c->w = (int16_t)rd16(&r);
        c->h = (int16_t)rd16(&r);
        c->id = ex ? rd32(&r) : rd16(&r);
        if (!ex && c->id == 0xffff) c->id = 0xffffffffu;
        cls = rd_sz(&r, &ordinal);
        if (cls == NULL)
        {
            c->type = (ordinal == 0x80) ? CTL_BUTTON : (ordinal == 0x82) ? CTL_STATIC : (ordinal == 0x85) ? CTL_COMBO : CTL_OTHER;
        }
        else
        {
            c->type = !strcasecmp(cls, "BUTTON") ? CTL_BUTTON : !strcasecmp(cls, "STATIC") ? CTL_STATIC :
                      !strcasecmp(cls, "COMBOBOX") ? CTL_COMBO : CTL_OTHER;
            free(cls);
        }
        text = rd_sz(&r, NULL);
        c->text = (text != NULL) ? text : dup_text("");
        r.offset += rd16(&r);               /* the creation data */
        c->sel = -1;
        if (!r.ok) return 0;
        dlg.n++;
    }
    return 1;
}

/* ---- the controls ---- */

static control *by_handle(uint32_t hWnd)
{
    uint32_t index = hWnd - DIALOG_HANDLE - 1;

    if (!dlg.open || index >= (uint32_t)dlg.n) return NULL;
    return &dlg.ctl[index];
}

static int index_of_id(uint32_t id)
{
    int i;

    for (i = 0; i < dlg.n; i++)
    {
        if ((dlg.ctl[i].id & 0xffff) == (id & 0xffff)) return i;
    }
    return -1;
}

static int focusable(const control *c)
{
    if (c->style & WS_DISABLED_c) return 0;
    if (c->type == CTL_COMBO) return 1;
    return c->type == CTL_BUTTON && (c->style & BS_TYPEMASK_c) != BS_GROUPBOX_c;
}

uint32_t CCALL SendDlgItemMessageA_c(uint32_t hDlg, uint32_t nIDDlgItem, uint32_t Msg, uint32_t wParam, uint32_t lParam);

/* a modeless dialog (a "Loading" box over the window) is not shown: a game
 * goes on without its window */
uint32_t CCALL CreateDialogParamA_c(uint32_t hInstance, uint32_t lpTemplateName, uint32_t hWndParent, uint32_t lpDialogFunc, uint32_t dwInitParam)
{
    (void)hInstance; (void)lpTemplateName; (void)hWndParent; (void)lpDialogFunc; (void)dwInitParam;
    return 0;
}

/* the property sheet of the game's settings is not shown: it is cancelled
 * (the settings stay as they are) */
uint32_t CCALL PropertySheetA_c(uint32_t lppsph)
{
    (void)lppsph;
    eprintf("dialog: a property sheet is not shown\n");
    return 0;
}

uint32_t CCALL GetDlgCtrlID_c(uint32_t hWnd)
{
    control *c = by_handle(hWnd);

    return (c != NULL) ? (c->id & 0xffff) : 0;
}

uint32_t CCALL CheckDlgButton_c(uint32_t hDlg, uint32_t nIDButton, uint32_t uCheck)
{
    SendDlgItemMessageA_c(hDlg, nIDButton, BM_SETCHECK_c, uCheck, 0);
    return 1;
}

uint32_t CCALL IsDlgButtonChecked_c(uint32_t hDlg, uint32_t nIDButton)
{
    return SendDlgItemMessageA_c(hDlg, nIDButton, BM_GETCHECK_c, 0, 0);
}

/* the buttons nIDFirstButton..nIDLastButton: only nIDCheckButton checked */
uint32_t CCALL CheckRadioButton_c(uint32_t hDlg, uint32_t nIDFirstButton, uint32_t nIDLastButton, uint32_t nIDCheckButton)
{
    uint32_t id;

    for (id = nIDFirstButton; id <= nIDLastButton; id++)
    {
        SendDlgItemMessageA_c(hDlg, id, BM_SETCHECK_c, (id == nIDCheckButton) ? 1 : 0, 0);
    }
    return 1;
}

uint32_t CCALL EndDialog_c(uint32_t hDlg, uint32_t nResult);

static uint32_t call_proc(uint32_t msg, uint32_t wParam, uint32_t lParam)
{
    uint32_t args[4];

    args[0] = DIALOG_HANDLE;
    args[1] = msg;
    args[2] = wParam;
    args[3] = lParam;
    return Winapi_CallGuest(dlg.proc, 4, args);
}

static void notify(int index, uint32_t code)
{
    if (dlg.proc == 0)
    {
        /* a message box: its buttons end it */
        if (code == BN_CLICKED_c && dlg.ctl[index].type == CTL_BUTTON) EndDialog_c(DIALOG_HANDLE, dlg.ctl[index].id);
        return;
    }
    call_proc(WM_COMMAND_c, (dlg.ctl[index].id & 0xffff) | (code << 16), DIALOG_HANDLE + 1 + index);
}

static void press(int index)
{
    notify(index, BN_CLICKED_c);
}

static uint32_t list_insert(control *c, int pos, const char *text)
{
    char **items;
    uint32_t *data;
    char *copy = dup_text(text != NULL ? text : "");

    if (copy == NULL) return CB_ERR_c;
    items = (char **)realloc(c->items, (c->count + 1) * sizeof(char *));
    if (items == NULL) { free(copy); return CB_ERR_c; }
    c->items = items;
    data = (uint32_t *)realloc(c->data, (c->count + 1) * sizeof(uint32_t));
    if (data == NULL) { free(copy); return CB_ERR_c; }
    c->data = data;
    if (pos < 0 || pos > c->count) pos = c->count;
    memmove(&c->items[pos + 1], &c->items[pos], (c->count - pos) * sizeof(char *));
    memmove(&c->data[pos + 1], &c->data[pos], (c->count - pos) * sizeof(uint32_t));
    c->items[pos] = copy;
    c->data[pos] = 0;
    c->count++;
    if (c->sel >= pos) c->sel++;
    return (uint32_t)pos;
}

static uint32_t list_find(const control *c, uint32_t start, const char *text, int exact)
{
    int i, n;
    size_t length = strlen(text);

    for (n = 0; n < c->count; n++)
    {
        i = (int)((start == CB_ERR_c ? 0 : start + 1 + n) % (c->count ? c->count : 1));
        if (start == CB_ERR_c) i = n;
        if (exact ? !strcasecmp(c->items[i], text) : !strncasecmp(c->items[i], text, length)) return (uint32_t)i;
    }
    return CB_ERR_c;
}

static uint32_t control_message(control *c, uint32_t msg, uint32_t wParam, uint32_t lParam)
{
    uint32_t result = 0;

    switch (msg)
    {
        case WM_SETTEXT_c:
        {
            char *text = dup_text(lParam ? (const char *)GUEST_MEM(lParam) : "");
            if (text == NULL) return 0;
            free(c->text);
            c->text = text;
            Game_UiChanged();
            return 1;
        }
        case WM_GETTEXT_c:
            if (wParam == 0 || lParam == 0) return 0;
            strncpy((char *)GUEST_MEM(lParam), c->text, wParam - 1);
            ((char *)GUEST_MEM(lParam))[wParam - 1] = 0;
            return (uint32_t)strlen((char *)GUEST_MEM(lParam));
        case WM_GETTEXTLENGTH_c:
            return (uint32_t)strlen(c->text);
        case BM_GETCHECK_c:
            return c->check;
        case BM_SETCHECK_c:
            c->check = wParam;
            Game_UiChanged();
            return 0;
    }
    if (c->type != CTL_COMBO) return 0;

    switch (msg)
    {
        case CB_ADDSTRING_c:
            result = list_insert(c, -1, lParam ? (const char *)GUEST_MEM(lParam) : "");
            break;
        case CB_INSERTSTRING_c:
            result = list_insert(c, (int)wParam, lParam ? (const char *)GUEST_MEM(lParam) : "");
            break;
        case CB_DELETESTRING_c:
            if (wParam >= (uint32_t)c->count) return CB_ERR_c;
            free(c->items[wParam]);
            c->count--;
            memmove(&c->items[wParam], &c->items[wParam + 1], (c->count - wParam) * sizeof(char *));
            memmove(&c->data[wParam], &c->data[wParam + 1], (c->count - wParam) * sizeof(uint32_t));
            if (c->sel == (int)wParam) c->sel = -1;
            else if (c->sel > (int)wParam) c->sel--;
            result = (uint32_t)c->count;
            break;
        case CB_RESETCONTENT_c:
            while (c->count > 0) free(c->items[--c->count]);
            c->sel = -1;
            result = 1;
            break;
        case CB_GETCOUNT_c:
            return (uint32_t)c->count;
        case CB_GETCURSEL_c:
            return (c->sel < 0) ? CB_ERR_c : (uint32_t)c->sel;
        case CB_SETCURSEL_c:
            if (wParam >= (uint32_t)c->count)
            {
                c->sel = -1;
                result = CB_ERR_c;
            }
            else result = (uint32_t)(c->sel = (int)wParam);
            break;
        case CB_GETLBTEXT_c:
            if (wParam >= (uint32_t)c->count) return CB_ERR_c;
            if (lParam != 0) strcpy((char *)GUEST_MEM(lParam), c->items[wParam]);
            return (uint32_t)strlen(c->items[wParam]);
        case CB_GETLBTEXTLEN_c:
            if (wParam >= (uint32_t)c->count) return CB_ERR_c;
            return (uint32_t)strlen(c->items[wParam]);
        case CB_GETITEMDATA_c:
            if (wParam >= (uint32_t)c->count) return CB_ERR_c;
            return c->data[wParam];
        case CB_SETITEMDATA_c:
            if (wParam >= (uint32_t)c->count) return CB_ERR_c;
            c->data[wParam] = lParam;
            return 1;
        case CB_FINDSTRING_c:
        case CB_FINDSTRINGEXACT_c:
            return list_find(c, wParam, lParam ? (const char *)GUEST_MEM(lParam) : "", msg == CB_FINDSTRINGEXACT_c);
        case CB_SELECTSTRING_c:
            result = list_find(c, wParam, lParam ? (const char *)GUEST_MEM(lParam) : "", 0);
            if (result != CB_ERR_c) c->sel = (int)result;
            break;
        case CB_SHOWDROPDOWN_c:
            dlg.dropped = wParam ? (int)(c - dlg.ctl) : -1;
            result = 1;
            break;
        case CB_GETDROPPEDSTATE_c:
            return (dlg.dropped == (int)(c - dlg.ctl)) ? 1 : 0;
        default:
            eprintf("dialog: message 0x%x to a list is not done\n", msg);
            return 0;
    }
    Game_UiChanged();
    return result;
}

int Winapi_DialogMessage(uint32_t hWnd, uint32_t Msg, uint32_t wParam, uint32_t lParam, uint32_t *result)
{
    control *c = by_handle(hWnd);

    if (c == NULL) return 0;
    *result = control_message(c, Msg, wParam, lParam);
    return 1;
}

uint32_t CCALL SendDlgItemMessageA_c(uint32_t hDlg, uint32_t nIDDlgItem, uint32_t Msg, uint32_t wParam, uint32_t lParam)
{
    int index;

    if (!dlg.open || hDlg != DIALOG_HANDLE || (index = index_of_id(nIDDlgItem)) < 0) return 0;
    return control_message(&dlg.ctl[index], Msg, wParam, lParam);
}

uint32_t CCALL GetDlgItem_c(uint32_t hDlg, uint32_t nIDDlgItem)
{
    int index;

    if (!dlg.open || hDlg != DIALOG_HANDLE || (index = index_of_id(nIDDlgItem)) < 0) return 0;
    return DIALOG_HANDLE + 1 + index;
}

uint32_t CCALL EndDialog_c(uint32_t hDlg, uint32_t nResult)
{
    if (!dlg.open || hDlg != DIALOG_HANDLE) return 0;
    dlg.ended = 1;
    dlg.result = nResult;
    return 1;
}

/* ---- layout ---- */

/* a closed list is a field of 12 dialog units; open, its rows go down to its height */
#define COMBO_FIELD 12

static void layout(int W, int H)
{
    int th = H / 28;

    dl.W = W;
    dl.H = H;
    /* dialog units: 4 across and 8 down are about a letter of the font of Windows;
     * this font is wider, so its letter is a little narrower than 4 */
    if (dlg.cx > 0 && th * 0.155f * dlg.cx > W * 0.96f) th = (int)(W * 0.96f / (0.155f * dlg.cx));
    if (dlg.cy > 0 && th * (dlg.cy / 6.4f + 1.6f) > H * 0.96f) th = (int)(H * 0.96f / (dlg.cy / 6.4f + 1.6f));
    if (th > 40) th = 40;
    if (th < 8) th = 8;
    dl.th = th;
    dl.dux = th * 0.155f;
    dl.duy = th / 6.4f;
    dl.title_h = th * 16 / 10;
    dl.w = (int)(dlg.cx * dl.dux);
    dl.h = (int)(dlg.cy * dl.duy) + dl.title_h;
    dl.x = (W - dl.w) / 2;
    dl.y = (H - dl.h) / 2;
}

static void control_box(const control *c, int *x, int *y, int *w, int *h)
{
    *x = dl.x + (int)(c->x * dl.dux);
    *y = dl.y + dl.title_h + (int)(c->y * dl.duy);
    *w = (int)(c->w * dl.dux);
    *h = (int)(((c->type == CTL_COMBO) ? COMBO_FIELD : c->h) * dl.duy);
}

static int list_row_h(void)
{
    return dl.th * 14 / 10;
}

/* the rows of the open list that fit: its template height, at most to the bottom of the screen */
static int list_rows(const control *c, int *list_y)
{
    int x, y, w, h, rows, row_h = list_row_h();

    control_box(c, &x, &y, &w, &h);
    rows = (int)((c->h - COMBO_FIELD) * dl.duy) / row_h;
    if (rows < 3) rows = 3;
    if (rows > c->count) rows = c->count;
    *list_y = y + h;
    if (*list_y + rows * row_h > dl.H)
    {
        /* no room below: above the field */
        if (y - rows * row_h >= 0) *list_y = y - rows * row_h;
        else rows = (dl.H - *list_y) / row_h;
    }
    return (rows < 1) ? 1 : rows;
}

static void keep_visible(const control *c)
{
    int list_y, rows = list_rows(c, &list_y);

    if (c->sel >= 0 && c->sel < dlg.top) dlg.top = c->sel;
    if (c->sel >= dlg.top + rows) dlg.top = c->sel - rows + 1;
    if (dlg.top > c->count - rows) dlg.top = c->count - rows;
    if (dlg.top < 0) dlg.top = 0;
}

/* ---- events ---- */

static void changed(void)
{
    Game_UiChanged();
    Game_UiRepaint();
}

static void set_sel(int index, int sel)
{
    control *c = &dlg.ctl[index];

    if (sel < 0 || sel >= c->count || sel == c->sel) return;
    c->sel = sel;
    notify(index, CBN_SELCHANGE_c);
}

static void drop(int index, int open)
{
    if (open && dlg.dropped != index)
    {
        notify(index, CBN_DROPDOWN_c);
        dlg.dropped = index;
        dlg.top = 0;
        keep_visible(&dlg.ctl[index]);
    }
    else if (!open && dlg.dropped == index)
    {
        dlg.dropped = -1;
        notify(index, CBN_CLOSEUP_c);
    }
}

static void move_focus(int step)
{
    int i, n = dlg.focus;

    for (i = 0; i < dlg.n; i++)
    {
        n = (n < 0) ? (step > 0 ? 0 : dlg.n - 1) : (n + step + dlg.n) % dlg.n;
        if (focusable(&dlg.ctl[n]) && (dlg.ctl[n].style & WS_TABSTOP_c))
        {
            dlg.focus = n;
            return;
        }
    }
}

static int default_button(void)
{
    int i;

    for (i = 0; i < dlg.n; i++)
    {
        if (dlg.ctl[i].type == CTL_BUTTON && (dlg.ctl[i].style & BS_TYPEMASK_c) == BS_DEFPUSHBUTTON_c) return i;
    }
    return -1;
}

static void command(uint32_t id)
{
    int index = index_of_id(id);

    if (index >= 0) press(index);
    else if (dlg.proc != 0) call_proc(WM_COMMAND_c, id, 0);
}

static void key(SDL_Keycode sym, Uint16 mod)
{
    control *f = (dlg.focus >= 0) ? &dlg.ctl[dlg.focus] : NULL;

    if (dlg.dropped >= 0)
    {
        control *c = &dlg.ctl[dlg.dropped];
        int list_y, rows = list_rows(c, &list_y), index = dlg.dropped;

        switch (sym)
        {
            case SDLK_UP: set_sel(index, c->sel - 1); break;
            case SDLK_DOWN: set_sel(index, c->sel < 0 ? 0 : c->sel + 1); break;
            case SDLK_PAGEUP: set_sel(index, c->sel - rows < 0 ? 0 : c->sel - rows); break;
            case SDLK_PAGEDOWN: set_sel(index, c->sel + rows >= c->count ? c->count - 1 : c->sel + rows); break;
            case SDLK_HOME: set_sel(index, 0); break;
            case SDLK_END: set_sel(index, c->count - 1); break;
            case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_ESCAPE: case SDLK_TAB: case SDLK_F4: case SDLK_SPACE:
                drop(index, 0);
                break;
        }
        if (dlg.dropped >= 0) keep_visible(c);
        return;
    }

    switch (sym)
    {
        case SDLK_TAB:
            move_focus((mod & KMOD_SHIFT) ? -1 : 1);
            break;
        case SDLK_ESCAPE:
            command(IDCANCEL_c);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (f != NULL && f->type == CTL_BUTTON) press(dlg.focus);
            else if (default_button() >= 0) press(default_button());
            else command(IDOK_c);
            break;
        case SDLK_SPACE:
            if (f != NULL && f->type == CTL_BUTTON) press(dlg.focus);
            else if (f != NULL && f->type == CTL_COMBO) drop(dlg.focus, 1);
            break;
        case SDLK_F4:
            if (f != NULL && f->type == CTL_COMBO) drop(dlg.focus, 1);
            break;
        case SDLK_UP:
        case SDLK_DOWN:
            if (f != NULL && f->type == CTL_COMBO)
            {
                if (mod & KMOD_ALT) drop(dlg.focus, 1);
                else set_sel(dlg.focus, (sym == SDLK_UP) ? f->sel - 1 : (f->sel < 0 ? 0 : f->sel + 1));
            }
            else move_focus(sym == SDLK_UP ? -1 : 1);
            break;
        case SDLK_LEFT:
        case SDLK_RIGHT:
            move_focus(sym == SDLK_LEFT ? -1 : 1);
            break;
    }
}

/* the control under a point, -1 none; the row of the open list in *row */
static int hit(int x, int y, int *row)
{
    int i;

    *row = -1;
    if (dlg.dropped >= 0)
    {
        const control *c = &dlg.ctl[dlg.dropped];
        int cx, cy, cw, ch, list_y, rows = list_rows(c, &list_y);

        control_box(c, &cx, &cy, &cw, &ch);
        if (x >= cx && x < cx + cw && y >= list_y && y < list_y + rows * list_row_h())
        {
            *row = dlg.top + (y - list_y) / list_row_h();
            if (*row >= c->count) *row = c->count - 1;
            return dlg.dropped;
        }
    }
    for (i = dlg.n - 1; i >= 0; i--)
    {
        int cx, cy, cw, ch;

        if (!focusable(&dlg.ctl[i])) continue;
        control_box(&dlg.ctl[i], &cx, &cy, &cw, &ch);
        if (x >= cx && x < cx + cw && y >= cy && y < cy + ch) return i;
    }
    return -1;
}

static void pointer_down(int x, int y)
{
    int row, i = hit(x, y, &row);

    dlg.pointer = 1;
    dlg.drag_y = y;
    dlg.dragged = 0;
    if (dlg.dropped >= 0 && !(i == dlg.dropped && row >= 0))
    {
        /* outside the open list: it closes (a click on its own field only closes it) */
        int was = dlg.dropped;
        drop(was, 0);
        if (i == was) return;
    }
    if (i < 0) return;
    if (row >= 0) return;               /* a row: chosen when the pointer is released */
    dlg.focus = i;
    if (dlg.ctl[i].type == CTL_COMBO) drop(i, 1);
    else dlg.pressed = i;
}

static void pointer_move(int x, int y)
{
    int row, i;

    if (dlg.dropped >= 0 && dlg.pointer)
    {
        control *c = &dlg.ctl[dlg.dropped];
        int step = list_row_h();

        /* a drag in the list scrolls it */
        if (y - dlg.drag_y >= step || dlg.drag_y - y >= step)
        {
            dlg.top -= (y - dlg.drag_y) / step;
            dlg.drag_y += ((y - dlg.drag_y) / step) * step;
            dlg.dragged = 1;
            {
                int list_y, rows = list_rows(c, &list_y);
                if (dlg.top > c->count - rows) dlg.top = c->count - rows;
                if (dlg.top < 0) dlg.top = 0;
            }
        }
        return;
    }
    i = hit(x, y, &row);
    (void)i;
}

static void pointer_up(int x, int y)
{
    int row, i = hit(x, y, &row), pressed = dlg.pressed;

    dlg.pointer = 0;
    dlg.pressed = -1;
    if (dlg.dropped >= 0 && i == dlg.dropped && row >= 0 && !dlg.dragged)
    {
        int index = dlg.dropped;
        drop(index, 0);
        set_sel(index, row);
        return;
    }
    if (pressed >= 0 && i == pressed) press(pressed);
}

static void wheel(int dy)
{
    if (dlg.dropped >= 0)
    {
        control *c = &dlg.ctl[dlg.dropped];
        int list_y, rows = list_rows(c, &list_y);

        dlg.top -= dy;
        if (dlg.top > c->count - rows) dlg.top = c->count - rows;
        if (dlg.top < 0) dlg.top = 0;
    }
    else if (dlg.focus >= 0 && dlg.ctl[dlg.focus].type == CTL_COMBO)
    {
        set_sel(dlg.focus, dlg.ctl[dlg.focus].sel - dy);
    }
}

static void to_ui(Uint32 window_id, int *x, int *y)
{
    SDL_Window *window = SDL_GetWindowFromID(window_id);
    int ww, wh;

    if (window == NULL) return;
    SDL_GetWindowSize(window, &ww, &wh);
    if (ww > 0 && wh > 0 && dl.W > 0 && dl.H > 0)
    {
        *x = (int)((int64_t)*x * dl.W / ww);
        *y = (int)((int64_t)*y * dl.H / wh);
    }
}

/* open and not yet ended (EndDialog): the pump hands it the events */
int Winapi_DialogOpen(void)
{
    return dlg.open && !dlg.ended;
}

int Winapi_DialogEvent(const SDL_Event *event)
{
    int x, y;

    if (!Winapi_DialogOpen()) return 0;
    switch (event->type)
    {
        case SDL_KEYDOWN:
            key(event->key.keysym.sym, event->key.keysym.mod);
            break;
        case SDL_KEYUP:
        case SDL_TEXTINPUT:
        case SDL_TEXTEDITING:
            break;
        case SDL_MOUSEWHEEL:
            wheel(event->wheel.y);
            break;
        case SDL_MOUSEMOTION:
            if (event->motion.which == SDL_TOUCH_MOUSEID) return 1;
            x = event->motion.x;
            y = event->motion.y;
            to_ui(event->motion.windowID, &x, &y);
            pointer_move(x, y);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (event->button.which == SDL_TOUCH_MOUSEID || event->button.button != SDL_BUTTON_LEFT) return 1;
            x = event->button.x;
            y = event->button.y;
            to_ui(event->button.windowID, &x, &y);
            if (event->type == SDL_MOUSEBUTTONDOWN) pointer_down(x, y);
            else pointer_up(x, y);
            break;
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
        case SDL_FINGERUP:
            x = (int)(event->tfinger.x * dl.W);
            y = (int)(event->tfinger.y * dl.H);
            if (event->type == SDL_FINGERDOWN) pointer_down(x, y);
            else if (event->type == SDL_FINGERMOTION) pointer_move(x, y);
            else pointer_up(x, y);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
        case SDL_CONTROLLERAXISMOTION:
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
        case SDL_JOYAXISMOTION:
        case SDL_JOYHATMOTION:
            break;
        default:
            return 0;
    }
    changed();
    return 1;
}

/* ---- drawing ---- */

static void color(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
}

static void frame(SDL_Renderer *renderer, int x, int y, int w, int h)
{
    SDL_Rect rect;

    rect.x = x; rect.y = y; rect.w = w; rect.h = h;
    SDL_RenderDrawRect(renderer, &rect);
}

/* text cut to the width, aligned left (0), centred (1) or right (2) */
static void text_in(SDL_Renderer *renderer, const char *text, int x, int y, int w, int align)
{
    char line[256];
    size_t n = 0;
    int tw;

    for (; *text != 0 && n + 1 < sizeof(line); text++)
    {
        if (*text == '&' && text[1] != '&') continue;
        if (*text == '&') text++;
        line[n++] = *text;
    }
    line[n] = 0;
    while (n > 0 && Game_UiTextWidth(line, dl.th) > w)
    {
        /* a whole character of UTF-8 */
        while (n > 1 && ((unsigned char)line[n - 1] & 0xC0) == 0x80) line[--n] = 0;
        line[--n] = 0;
    }
    tw = Game_UiTextWidth(line, dl.th);
    if (align == 1) x += (w - tw) / 2;
    else if (align == 2) x += w - tw;
    Game_UiText(renderer, line, x, y, dl.th);
}

static void draw_arrow_down(SDL_Renderer *renderer, int x, int y, int s)
{
    int row;

    for (row = 0; row <= s / 3; row++)
    {
        SDL_RenderDrawLine(renderer, x + s / 6 + row, y + s / 3 + row, x + s * 5 / 6 - row, y + s / 3 + row);
    }
}

static void draw_control(SDL_Renderer *renderer, int i)
{
    const control *c = &dlg.ctl[i];
    int x, y, w, h, disabled = (c->style & WS_DISABLED_c) != 0, focused = (i == dlg.focus);

    if (!(c->style & WS_VISIBLE_c)) return;
    control_box(c, &x, &y, &w, &h);
    switch (c->type)
    {
        case CTL_STATIC:
        {
            const char *line = c->text;
            int lines = 1, k;

            if ((c->style & SS_TYPEMASK_c) > SS_RIGHT_c) return;        /* icons, frames */
            color(renderer, 230, 230, 235, 255);
            for (k = 0; line[k] != 0; k++) if (line[k] == '\n') lines++;
            if (lines == 1)
            {
                text_in(renderer, c->text, x, y + (h - dl.th) / 2, w + dl.th, c->style & SS_TYPEMASK_c);
                break;
            }
            for (k = 0; k < lines; k++)
            {
                char part[256];
                size_t n = 0;

                while (*line != 0 && *line != '\n' && n + 1 < sizeof(part)) part[n++] = *line++;
                part[n] = 0;
                if (*line == '\n') line++;
                text_in(renderer, part, x, y + k * (int)(8 * dl.duy), w + dl.th, c->style & SS_TYPEMASK_c);
            }
            break;
        }
        case CTL_BUTTON:
            if ((c->style & BS_TYPEMASK_c) == BS_GROUPBOX_c)
            {
                int tw = Game_UiTextWidth(c->text, dl.th);
                color(renderer, 110, 110, 130, 255);
                frame(renderer, x, y + dl.th / 2, w, h - dl.th / 2);
                color(renderer, 36, 36, 46, 255);
                Game_UiFillRect(renderer, x + dl.th / 2, y, tw + dl.th / 2, dl.th);
                color(renderer, 200, 200, 210, 255);
                Game_UiText(renderer, c->text, x + dl.th * 3 / 4, y, dl.th);
                break;
            }
            if (i == dlg.pressed) color(renderer, 40, 70, 130, 255);
            else if (focused) color(renderer, 70, 110, 190, 255);
            else color(renderer, 62, 62, 78, 255);
            Game_UiFillRect(renderer, x, y, w, h);
            color(renderer, 140, 140, 160, 255);
            frame(renderer, x, y, w, h);
            if ((c->style & BS_TYPEMASK_c) == BS_DEFPUSHBUTTON_c) frame(renderer, x + 1, y + 1, w - 2, h - 2);
            if (disabled) color(renderer, 125, 125, 135, 255);
            else color(renderer, 255, 255, 255, 255);
            text_in(renderer, c->text, x, y + (h - dl.th) / 2, w, 1);
            break;
        case CTL_COMBO:
            color(renderer, 22, 22, 30, 255);
            Game_UiFillRect(renderer, x, y, w, h);
            color(renderer, focused ? 120 : 110, focused ? 160 : 110, focused ? 230 : 130, 255);
            frame(renderer, x, y, w, h);
            if (focused) frame(renderer, x + 1, y + 1, w - 2, h - 2);
            color(renderer, 255, 255, 255, 255);
            if (c->sel >= 0) text_in(renderer, c->items[c->sel], x + dl.th / 3, y + (h - dl.th) / 2, w - h - dl.th / 3, 0);
            draw_arrow_down(renderer, x + w - h, y, h);
            break;
    }
}

static void draw_list(SDL_Renderer *renderer)
{
    const control *c = &dlg.ctl[dlg.dropped];
    int x, y, w, h, list_y, rows = list_rows(c, &list_y), r, row_h = list_row_h();

    control_box(c, &x, &y, &w, &h);
    color(renderer, 28, 28, 38, 250);
    Game_UiFillRect(renderer, x, list_y, w, rows * row_h);
    for (r = 0; r < rows && dlg.top + r < c->count; r++)
    {
        int item = dlg.top + r, ry = list_y + r * row_h;

        if (item == c->sel)
        {
            color(renderer, 70, 110, 190, 255);
            Game_UiFillRect(renderer, x + 1, ry, w - 2, row_h);
        }
        color(renderer, 255, 255, 255, 255);
        text_in(renderer, c->items[item], x + dl.th / 3, ry + (row_h - dl.th) / 2, w - dl.th, 0);
    }
    color(renderer, 140, 140, 160, 255);
    frame(renderer, x, list_y, w, rows * row_h);
    if (c->count > rows)
    {
        /* where the rows are in the list */
        int bar_h = rows * row_h * rows / c->count, bar_y = list_y + rows * row_h * dlg.top / c->count;
        color(renderer, 140, 140, 160, 255);
        Game_UiFillRect(renderer, x + w - dl.th / 3, bar_y, dl.th / 4, bar_h > 4 ? bar_h : 4);
    }
}

static void draw(SDL_Renderer *renderer, int W, int H)
{
    int i;

    layout(W, H);
    color(renderer, 0, 0, 0, 110);
    Game_UiFillRect(renderer, 0, 0, W, H);
    color(renderer, 36, 36, 46, 245);
    Game_UiFillRect(renderer, dl.x, dl.y, dl.w, dl.h);
    color(renderer, 70, 110, 190, 255);
    Game_UiFillRect(renderer, dl.x, dl.y, dl.w, dl.title_h);
    color(renderer, 140, 140, 160, 255);
    frame(renderer, dl.x, dl.y, dl.w, dl.h);
    color(renderer, 255, 255, 255, 255);
    if (dlg.title != NULL) text_in(renderer, dlg.title, dl.x + dl.th / 2, dl.y + (dl.title_h - dl.th) / 2, dl.w - dl.th, 0);
    for (i = 0; i < dlg.n; i++) draw_control(renderer, i);
    if (dlg.dropped >= 0) draw_list(renderer);
}

static int visible(void)
{
    return dlg.open;
}

static const Game_UiClient dialog_client = { draw, visible };

static int dialog_done(void)
{
    return dlg.ended;
}

/* the dialog in dlg, its controls made: shown until EndDialog */
static uint32_t run_dialog(uint32_t dwInitParam)
{
    uint32_t result;
    int W, H, i;

    dlg.focus = -1;
    dlg.pressed = -1;
    dlg.dropped = -1;
    dlg.open = 1;
    Game_UiSize(&W, &H);
    layout(W, H);
    Game_UiSetClient(&dialog_client);
    TouchGamepad_ReleaseAll();
    /* the focus: the default button, else the first control with a tab stop */
    for (i = 0; i < dlg.n; i++)
    {
        if (dlg.ctl[i].type == CTL_BUTTON && (dlg.ctl[i].style & BS_TYPEMASK_c) == BS_DEFPUSHBUTTON_c) dlg.focus = i;
    }
    if (dlg.focus < 0) move_focus(1);

    if (dlg.proc != 0) call_proc(WM_INITDIALOG_c, (dlg.focus >= 0) ? DIALOG_HANDLE + 1 + dlg.focus : 0, dwInitParam);
    changed();
    if (!dlg.ended) Winapi_ModalLoop(dialog_done);

    result = dlg.result;
    free_dialog();
    changed();
    return result;
}

uint32_t CCALL DialogBoxParamA_c(uint32_t hInstance, uint32_t lpTemplateName, uint32_t hWndParent, uint32_t lpDialogFunc, uint32_t dwInitParam)
{
    uint32_t size = 0;
    uint8_t *data;
    int i;

    (void)hInstance; (void)hWndParent;
    if (!GAME_WINAPI_DIALOGS)
    {
        eprintf("[stub DialogBoxParamA]\n");
        return 0;
    }
    if (dlg.open)
    {
        eprintf("dialog: a dialog inside a dialog is not done\n");
        return 0xffffffffu;
    }
    data = Winapi_LoadResource(5 /* RT_DIALOG */, lpTemplateName, &size);
    if (data == NULL)
    {
        eprintf("dialog: no template %s\n", (lpTemplateName >= 0x10000) ? (const char *)GUEST_MEM(lpTemplateName) : "(id)");
        return 0xffffffffu;
    }
    memset(&dlg, 0, sizeof(dlg));
    i = parse_template(data, size);
    free(data);
    if (!i)
    {
        eprintf("dialog: a template that does not parse\n");
        free_dialog();
        return 0xffffffffu;
    }

    dlg.proc = lpDialogFunc;
    return run_dialog(dwInitParam);
}

/* a control of the message box, in dialog units */
static void add_control(int type, uint32_t style, uint32_t id, int x, int y, int w, int h, const char *text)
{
    control *c = &dlg.ctl[dlg.n++];

    c->type = type;
    c->style = WS_VISIBLE_c | style;
    c->id = id;
    c->x = x; c->y = y; c->w = w; c->h = h;
    c->text = dup_text(text != NULL ? text : "");
    c->sel = -1;
}

/* MessageBoxA with the window of the game, as a dialog of the same kind: the text
 * and the buttons of its type (MB_OK ... MB_RETRYCANCEL); the default button is
 * the first one unless MB_DEFBUTTON2/3 says otherwise */
uint32_t Winapi_MessageBoxDialog(const char *text, const char *caption, uint32_t type)
{
    static const struct { int count; uint32_t id[3]; const char *label[3]; } kinds[6] = {
        { 1, { 1 }, { "OK" } },
        { 2, { 1, 2 }, { "OK", "Cancel" } },
        { 3, { 3, 4, 5 }, { "Abort", "Retry", "Ignore" } },
        { 3, { 6, 7, 2 }, { "Yes", "No", "Cancel" } },
        { 2, { 6, 7 }, { "Yes", "No" } },
        { 2, { 4, 2 }, { "Retry", "Cancel" } },
    };
    int kind = (int)(type & 0x0f), lines = 1, longest = 0, run = 0, i, cx, bx, by, def;
    const char *p;

    if (kind > 5) kind = 0;
    if (text == NULL) text = "";
    for (p = text; ; p++)
    {
        if (*p == '\n' || *p == 0)
        {
            if (run > longest) longest = run;
            run = 0;
            if (*p == 0) break;
            lines++;
        }
        else run++;
    }
    if (dlg.open) return 0;
    memset(&dlg, 0, sizeof(dlg));
    /* a letter of this font is about 3.4 dialog units across */
    cx = longest * 34 / 10 + 14;
    if (cx < kinds[kind].count * 56 + 8) cx = kinds[kind].count * 56 + 8;
    by = 7 + lines * 8 + 8;
    dlg.cx = cx;
    dlg.cy = by + 14 + 7;
    dlg.title = dup_text(caption != NULL ? caption : "");
    add_control(CTL_STATIC, 0, 0xffffffffu, 7, 7, cx - 14, lines * 8, text);
    def = (int)((type >> 8) & 0x0f);
    if (def >= kinds[kind].count) def = 0;
    bx = (cx - (kinds[kind].count * 56 - 6)) / 2;
    for (i = 0; i < kinds[kind].count; i++)
    {
        add_control(CTL_BUTTON, WS_TABSTOP_c | (i == def ? BS_DEFPUSHBUTTON_c : 0), kinds[kind].id[i],
                    bx + i * 56, by, 50, 14, kinds[kind].label[i]);
    }
    dlg.proc = 0;
    return run_dialog(0);
}

#ifdef __cplusplus
}
#endif
