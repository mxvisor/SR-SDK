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

/* The port's settings of a DOS or a Win32 game, the Game_config.c of SR's ports with only
 * this port's keys.  The file is the one --config-file= names (Game_ReadArgs), else
 * <program>.cfg of the game directory (GAME_PROGRAM_NAME of scons/game.py); a
 * game names another file with GAME_CONFIG_FILENAME (Game_hooks.h).  Without
 * the file everything stays as Init_Display/Init_Audio/Init_Input set it.  The
 * environment variables of the same meaning are read where they apply and win
 * over the file. */

#include <stdio.h>
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

void Game_ReadConfig(void)
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
            const char *write_root = Game_WriteRoot();

            if (write_root == NULL) return;
            if (snprintf(filename, sizeof(filename), "%s/%s", write_root, GAME_CONFIG_FILENAME) >= (int)sizeof(filename)) return;
            f = fopen(filename, "rt");
            if (f == NULL) return;
        }
    }

    fprintf(stderr, GAME_NAME ": using config file: %s\n", filename);

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
        if (Config_Audio(str, param)) continue;
        if (Config_Input(str, param)) continue;

#ifdef GAME_CONFIG_KEY
        // the game's own keys: nonzero if it took this one
        if (GAME_CONFIG_KEY(str, param)) continue;
#endif

        if ( strcasecmp(str, "Sound") == 0 ) // str equals "Sound"
        {
            // digital sound
            read_on_off(param, &Game_Sound);
        }
#ifdef GAME_WINAPI
        else if ( strcasecmp(str, "3D") == 0 ) // str equals "3D"
        {
            // direct3d (OpenGL), software (Direct3D, the software rasteriser) or off;
            // <GAME>_D3D and <GAME>_D3D_GL win (WinApi-d3d.c)
            if (!Winapi_Direct3DSetMode(param)) fprintf(stderr, GAME_NAME ": unknown 3D mode: %s\n", param);
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
