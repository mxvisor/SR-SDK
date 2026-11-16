/**
 *
 *  Copyright (C) 2016-2026 Roman Pauer
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

/* The port's settings of a DOS or a Win32 game, the Game_config.c of SR's ports
 * (SR/games/) with only this port's keys.  The file is the one --config-file= names (Game_ReadArgs), else
 * <program>.cfg of the game directory (GAME_PROGRAM_NAME of scons/game.py); a
 * game names another file with GAME_CONFIG_FILENAME (Game_hooks.h).  Without
 * the file everything stays as Init_Display/Init_Audio/Init_Input set it.  The
 * environment variables of the same meaning are read where they apply and win
 * over the file. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Game_defs.h"
#include "Game_vars.h"
#include "Game_config.h"
#ifdef GAME_WINAPI
#include "WinApi-d3d.h"
#endif
#include "main.h"
#include "display.h"
#include "audio.h"
#include "input.h"
#include "Game_vfs.h"
#include "Game_osd.h"
#include "Game_gyro.h"
#include "Game_hooks.h"

/* the program's name is the port's namespace in the game directory: the
 * settings file and the log are <program>.cfg and <program>.log there, so the
 * port's files never take a name of the game's (one file on Windows) */
#ifndef GAME_CONFIG_FILENAME
#define GAME_CONFIG_FILENAME GAME_PROGRAM_NAME ".cfg"
#endif

static char *trim_string(char *buf)
{
    size_t len;

    while (*buf == ' ' || *buf == '\t') buf++;

    len = strlen(buf);

    while (len != 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\t'))
    {
        len--;
        buf[len] = 0;
    }

    return buf;
}

/* on/off; anything else leaves *value as it was */
static void read_on_off(const char *param, uint32_t *value)
{
    if ( strcasecmp(param, "on") == 0 ) // param equals "on"
    {
        *value = 1;
    }
    else if ( strcasecmp(param, "off") == 0 ) // param equals "off"
    {
        *value = 0;
    }
}

/* The port's own options, as in SR's ports: --config-file=FILE.  They are taken out
 * of argv, which goes on to the game's switch parser (/D, /T, ...). */
int Game_ReadArgs(int argc, char *argv[])
{
    int count = 1;

    Game_ConfigFilename[0] = 0;
    for (int index = 1; index < argc; index++)
    {
        if (strncmp(argv[index], "--config-file=", 14) == 0)
        {
            if (strlen(argv[index] + 14) < sizeof(Game_ConfigFilename)) strcpy(Game_ConfigFilename, argv[index] + 14);
            continue;
        }
        argv[count++] = argv[index];
    }
    argv[count] = NULL;
    return count;
}

/* the file the settings came from, where Game_ConfigSave writes */
static char config_path[4096];

static void read_config(void)
{
    char filename[4096];
    FILE *f;
    char buf[8192];
    char *str, *param;
    int items;

    if (Game_ConfigFilename[0] != 0)
    {
        // given with --config-file=, relative to the current directory
        snprintf(filename, sizeof(filename), "%s", Game_ConfigFilename);
    }
    else
    {
        const char *game_directory = Game_DataRoot();

        if (game_directory == NULL) return;
        if (snprintf(filename, sizeof(filename), "%s/%s", game_directory, GAME_CONFIG_FILENAME) >= (int)sizeof(filename)) return;
    }

    f = fopen(filename, "rt");
    if (f == NULL)
    {
        // only a file that was asked for is missed
        if (Game_ConfigFilename[0] != 0)
        {
            fprintf(stderr, GAME_NAME ": could not open config file: %s\n", filename);
            return;
        }
        // else the write root, where the port keeps what it writes
        {
            const char *write_root;

            /* the config is read before Game_FilesInit (the DOS runtime): the write root is not chosen yet */
            if (Game_SelectWriteRoot() != 0) return;
            write_root = Game_WriteRoot();
            if (write_root == NULL) return;
            if (snprintf(filename, sizeof(filename), "%s/%s", write_root, GAME_CONFIG_FILENAME) >= (int)sizeof(filename)) return;
            f = fopen(filename, "rt");
            if (f == NULL) return;
        }
    }

    fprintf(stderr, GAME_NAME ": using config file: %s\n", filename);
    snprintf(config_path, sizeof(config_path), "%s", filename);

    while (!feof(f))
    {
        // skip empty lines
        items = fscanf(f, "%8191[\n\r]", buf);

        // read line
        buf[0] = 0;
        items = fscanf(f, "%8191[^\n^\r]", buf);
        if (items <= 0) continue;

        // trim line
        str = trim_string(buf);

        if (str[0] == 0) continue;
        if (str[0] == '#') continue;

        // find parameter (after '=')
        param = strchr(str, '=');

        if (param == NULL) continue;

        // split string into two strings
        *param = 0;
        param++;

        // trim them
        str = trim_string(str);
        param = trim_string(param);

        if (Config_Display(str, param)) continue;
        if (Game_OsdConfigKey(str, param)) continue;
        if (Config_Audio(str, param)) continue;
        if (Config_Input(str, param)) continue;
        if (Game_GyroConfig(str, param)) continue;

#ifdef GAME_CONFIG_KEY
        // the game's own keys: nonzero if it took this one
        if (GAME_CONFIG_KEY(str, param)) continue;
#endif

        if ( strcasecmp(str, "Sound") == 0 ) // str equals "Sound"
        {
            // digital sound
            read_on_off(param, &Game_Sound);
        }
        else if ( strcasecmp(str, "Display_3D_Resolution") == 0 ) // str equals "Display_3D_Resolution"
        {
            // the 3D at WxH (Glide), off: the screen's; <GAME>_3D_RESOLUTION wins
            int w = 0, h = 0;
            if (strcasecmp(param, "off") == 0) Game_3DWidth = Game_3DHeight = 0;
            else if ((sscanf(param, "%ix%i", &w, &h) != 2) || (w < 1) || (h < 1)) fprintf(stderr, GAME_NAME ": unknown 3D resolution: %s\n", param);
            else { Game_3DWidth = w; Game_3DHeight = h; }
        }
        else if ( strcasecmp(str, "Display_Shader") == 0 ) // str equals "Display_Shader"
        {
            // the 2D filter of the picture, none or a name of Game_gl.h; <GAME>_SHADER wins
            snprintf(Game_Shader, sizeof(Game_Shader), "%s", param);
        }
        else if ( strcasecmp(str, "Display_Fullscreen") == 0 ) // str equals "Display_Fullscreen"
        {
            // the window on the whole screen (Game_eventloop.c), off: a window
            read_on_off(param, &Game_Fullscreen);
        }
        else if ( strcasecmp(str, "Display_Additional_Resolutions") == 0 ) // str equals "Display_Additional_Resolutions"
        {
            // the display modes of DirectDraw above 800x600 (WinApi-ddraw.c), off: the original's
            read_on_off(param, &Game_AdditionalResolutions);
        }
#ifdef GAME_WINAPI
        else if ( strcasecmp(str, "3D") == 0 ) // str equals "3D"
        {
            // direct3d (OpenGL), software (Direct3D, the software rasteriser) or off;
            // <GAME>_D3D and <GAME>_D3D_GL win (WinApi-d3d.c)
            if (!Winapi_Direct3DSetMode(param)) fprintf(stderr, GAME_NAME ": unknown 3D mode: %s\n", param);
        }
        else if ( strcasecmp(str, "Display_Antialiasing") == 0 ) // str equals "Display_Antialiasing"
        {
            // the multisampling of the Direct3D scene on OpenGL and of Glide; <GAME>_ANTIALIASING wins
            if (!Winapi_Direct3DSetAntialiasing(param)) fprintf(stderr, GAME_NAME ": unknown antialiasing: %s\n", param);
        }
#endif
        else if ( strcasecmp(str, "Music") == 0 ) // str equals "Music"
        {
            // the CD's music; <GAME>_NOMUSIC wins (Game_music.c)
            read_on_off(param, &Game_Music);
        }
    }

    fclose(f);
}

/* the line ending of a file: that of its first line, else the system's */
static const char *line_end(const char *data, size_t size)
{
    const char *nl = (const char *)memchr(data, '\n', size);

    if (nl != NULL) return (nl > data && nl[-1] == '\r') ? "\r\n" : "\n";
#ifdef _WIN32
    return "\r\n";
#else
    return "\n";
#endif
}

/* the file's bytes with key=value in place of the value of key's lines, or after them */
static char *config_with(const char *data, size_t size, const char *key, const char *value, size_t *out_size)
{
    size_t key_len = strlen(key), value_len = strlen(value), pos = 0, len = 0;
    const char *eol = line_end(data, size);
    int found = 0;
    char *out;

    /* at most every line replaced, and one more */
    out = (char *)malloc(size + (size / 2 + 2) * (key_len + value_len + 4) + 8);
    if (out == NULL) return NULL;
    while (pos < size)
    {
        size_t end = pos, text_end, k, eq;

        while (end < size && data[end] != '\n') end++;
        text_end = end;
        if (text_end > pos && data[text_end - 1] == '\r') text_end--;
        if (end < size) end++;   /* with its '\n' */

        k = pos;
        while (k < text_end && (data[k] == ' ' || data[k] == '\t')) k++;
        for (eq = k; eq < text_end && data[eq] != '='; eq++) {}
        if (k < text_end && data[k] != '#' && eq < text_end)
        {
            size_t key_end = eq;

            while (key_end > k && (data[key_end - 1] == ' ' || data[key_end - 1] == '\t')) key_end--;
            if (key_end - k == key_len && strncasecmp(data + k, key, key_len) == 0)
            {
                size_t v = eq + 1;

                /* the key, '=' and the blanks after it stay, then the value and the line's end */
                while (v < text_end && (data[v] == ' ' || data[v] == '\t')) v++;
                memcpy(out + len, data + pos, v - pos);
                len += v - pos;
                memcpy(out + len, value, value_len);
                len += value_len;
                memcpy(out + len, data + text_end, end - text_end);
                len += end - text_end;
                pos = end;
                found = 1;
                continue;
            }
        }
        memcpy(out + len, data + pos, end - pos);
        len += end - pos;
        pos = end;
    }
    if (!found)
    {
        if (len != 0 && out[len - 1] != '\n')
        {
            memcpy(out + len, eol, strlen(eol));
            len += strlen(eol);
        }
        memcpy(out + len, key, key_len);
        len += key_len;
        out[len++] = '=';
        memcpy(out + len, value, value_len);
        len += value_len;
        memcpy(out + len, eol, strlen(eol));
        len += strlen(eol);
    }
    *out_size = len;
    return out;
}

/* path's bytes replaced by data through a file beside it */
static int write_file(const char *path, const char *data, size_t size)
{
    char temp[4096 + 8];
    FILE *f;
    int ok;

    if (snprintf(temp, sizeof(temp), "%s.new", path) >= (int)sizeof(temp)) return 0;
    f = fopen(temp, "wb");
    if (f == NULL) return 0;
    ok = (fwrite(data, 1, size, f) == size);
    if (fclose(f) != 0) ok = 0;
    if (ok)
    {
#ifdef _WIN32
        /* rename does not replace a file there */
        remove(path);
#endif
        ok = (rename(temp, path) == 0);
    }
    if (!ok) remove(temp);
    return ok;
}

static int save_to(const char *path, const char *key, const char *value)
{
    FILE *f;
    char *data = NULL, *out;
    size_t size = 0, out_size;
    int ok;

    f = fopen(path, "rb");
    if (f != NULL)
    {
        long length;

        if (fseek(f, 0, SEEK_END) == 0 && (length = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0)
        {
            data = (char *)malloc((size_t)length + 1);
            if (data != NULL) size = fread(data, 1, (size_t)length, f);
        }
        fclose(f);
        if (data == NULL) return 0;
    }
    out = config_with((data != NULL) ? data : "", size, key, value, &out_size);
    free(data);
    if (out == NULL) return 0;
    ok = write_file(path, out, out_size);
    free(out);
    if (ok && config_path[0] == 0)
    {
        snprintf(config_path, sizeof(config_path), "%s", path);
        fprintf(stderr, GAME_NAME ": the settings are saved in %s\n", path);
    }
    return ok;
}

int Game_ConfigSave(const char *key, const char *value)
{
    char filename[4096];
    const char *root;

    if (config_path[0] != 0) return save_to(config_path, key, value);
    /* no file was read: the one asked for, else the game directory's, else the write root's
     * (the order read_config looks in) */
    if (Game_ConfigFilename[0] != 0) return save_to(Game_ConfigFilename, key, value);
    root = Game_DataRoot();
    if (root != NULL && snprintf(filename, sizeof(filename), "%s/%s", root, GAME_CONFIG_FILENAME) < (int)sizeof(filename)
        && save_to(filename, key, value)) return 1;
    if (Game_SelectWriteRoot() != 0) return 0;
    root = Game_WriteRoot();
    if (root == NULL || snprintf(filename, sizeof(filename), "%s/%s", root, GAME_CONFIG_FILENAME) >= (int)sizeof(filename)) return 0;
    return save_to(filename, key, value);
}

void Game_ReadConfig(void)
{
    read_config();
    /* the items of the port's panel, with what the settings gave */
    Game_OsdInit();
}
