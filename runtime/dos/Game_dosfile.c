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

/* INT 21h file functions of a DOS4GW game.  Opens go through
 * Game_OpenGameFile (Game_vfs.c), as the game's fopen does: the disc, the
 * write root, the game directory.  FindFirst lists the write root, then the
 * game directory's virtual file system, or the disc. */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "Game_defs.h"
#include "Game_memory.h"
#include "Game_cdrom.h"
#include "Game_vfs.h"
#include "virtualfs.h"
#include "Game_dosfile.h"

#define MAX_FIND_RESULTS 256

typedef struct {
    char dos_name[13];
    char *host_path;        /* or "CD:<path>" */
    uint8_t attr;
    int64_t cd_size;        /* a CD entry's size, else -1 */
} find_result;

/* Disk Transfer Area for AH=4Eh/4Fh.  One active search (DOS's single-DTA
 * model); FindFirst collects the matches on the host, and the guest DTA only
 * ever carries the current result record. */
static uint8_t dta_default[0x80];
static uint32_t dta_addr;
static find_result find_results[MAX_FIND_RESULTS];
static unsigned int find_count;
static unsigned int find_next;
static char find_pattern[16];

static void *(*guest_pointer)(uint32_t address);

static uint16_t dos_error_from_errno(int value)
{
    switch (value) {
        case ENOENT: return 2;
        case ENOTDIR: return 3;
        case EMFILE: return 4;
        case EACCES:
        case EPERM:
        case EROFS: return 5;
        case ENOMEM: return 8;
        default: return 1;
    }
}

static void store16(uint8_t *pointer, uint16_t value)
{
    memcpy(pointer, &value, sizeof(value));
}

static void store32(uint8_t *pointer, uint32_t value)
{
    memcpy(pointer, &value, sizeof(value));
}

/* The device a path names, "CON" or "NUL" (a trailing ':' allowed), or 0. */
static int device_handle(const char *path)
{
    static const struct { const char *name; int handle; } devices[] = {
        { "CON", GAME_HANDLE_CON }, { "NUL", GAME_HANDLE_NUL }
    };
    if (path == NULL) return 0;
    for (unsigned int index = 0; index < sizeof(devices) / sizeof(devices[0]); index++) {
        const char *name = devices[index].name;
        size_t at = 0;
        while (name[at] != 0 && toupper((unsigned char)path[at]) == name[at]) at++;
        if (name[at] == 0 && (path[at] == 0 || (path[at] == ':' && path[at + 1] == 0))) return devices[index].handle;
    }
    return 0;
}

static void attach(Game_DosRegs *regs, game_file *file, const char *what, const char *dos_path)
{
    int handle = Game_AttachHandle(file);
    if (handle < 0) {
        game_fclose(file);
        Game_DosError(regs, 4);
        return;
    }
    if (Game_TraceFileEnabled()) fprintf(stderr, "FILE %s h=%d '%s'\n", what, handle, dos_path);
    Game_DosSetAX(regs, (uint16_t)handle);
    Game_DosSetCarry(regs, 0);
}

/* AH=3Dh: AL's low bits are the access, 0 reading, else reading and writing. */
static void dos_open(Game_DosRegs *regs)
{
    const char *dos_path = (const char *)guest_pointer(regs->edx);
    int device = device_handle(dos_path);
    if (device != 0) {
        Game_DosSetAX(regs, (uint16_t)device);
        Game_DosSetCarry(regs, 0);
        return;
    }
    errno = 0;
    game_file *file = Game_OpenGameFile(dos_path, 'r', (Game_DosAL(regs) & 3) != 0);
    if (file == NULL) {
        if (Game_TraceFileEnabled()) fprintf(stderr, "FILE OPENFAIL '%s'\n", dos_path);
        Game_DosError(regs, dos_error_from_errno(errno));
        return;
    }
    attach(regs, file, "OPEN", dos_path);
}

/* AH=3Ch: create a file, or cut an existing one to 0 bytes, and open it for
 * reading and writing, where the game's own fopen puts a new one: the write
 * root, or the game directory.  CX, the attributes, is not used. */
static void dos_create(Game_DosRegs *regs)
{
    const char *dos_path = (const char *)guest_pointer(regs->edx);
    errno = 0;
    game_file *file = Game_OpenGameFile(dos_path, 'w', 1);
    if (file == NULL) {
        if (Game_TraceFileEnabled()) fprintf(stderr, "FILE CREATEFAIL '%s'\n", dos_path);
        Game_DosError(regs, errno == EACCES ? 5 : 3);
        return;
    }
    attach(regs, file, "CREATE", dos_path);
}

/* AH=43h: the attributes of a file (AL=0: into CX) or a change of them (AL=1,
 * CX; the port keeps none).  A file the game can open is a plain one, archive
 * (20h); a directory of the game directory is 10h.  WATCOM's open() asks after
 * it opens a file for writing, for the read-only bit. */
static void dos_attributes(Game_DosRegs *regs)
{
    const char *dos_path = (const char *)guest_pointer(regs->edx);
    uint16_t attributes;
    game_file *file;

    errno = 0;
    file = Game_OpenGameFile(dos_path, 'r', 0);
    if (file != NULL) {
        game_fclose(file);
        attributes = 0x20;
    } else {
        const char *path = dos_path;
        file_entry *entry = NULL;
        char rewritten[PATH_MAX];

        if (path[0] != 0 && path[1] == ':') {
            if (toupper((unsigned char)path[0]) != 'C' ||
                snprintf(rewritten, sizeof(rewritten), "C:%s", path + 2) >= (int)sizeof(rewritten)) {
                Game_DosError(regs, 0x02);
                return;
            }
            path = rewritten;
        }
        if (vfs_get_real_name(path, NULL, &entry) != 0 || entry == NULL || !(entry->attributes & 1)) {
            if (Game_TraceFileEnabled()) fprintf(stderr, "FILE ATTRFAIL '%s'\n", dos_path);
            Game_DosError(regs, 0x02);
            return;
        }
        attributes = 0x10;
    }
    if (Game_DosAL(regs) == 0) Game_DosSetCX(regs, attributes);
    Game_DosSetCarry(regs, 0);
}

static int is_device(uint16_t handle)
{
    return handle == GAME_HANDLE_CON || handle == GAME_HANDLE_NUL;
}

/* AH=3Fh.  This is a 32-bit DOS4GW program: a transfer count lives in the full
 * ECX, not just CX, and can exceed 65535 in one call, and the count read comes
 * back in the full EAX.  A count of 0 reads nothing, as in DOS.  The standard
 * handles and the devices give end of file. */
static void dos_read(Game_DosRegs *regs)
{
    uint16_t handle = Game_DosBX(regs);
    uint32_t count = regs->ecx;
    if (handle <= 2 || is_device(handle)) {
        regs->eax = 0;
        Game_DosSetCarry(regs, 0);
        return;
    }
    game_file *file = Game_HandleFile(handle);
    if (file == NULL) {
        Game_DosError(regs, 6);
        return;
    }
    if (Game_TraceFileEnabled()) {
        fprintf(stderr, "FILE READ h=%02x ofs=%08lx n=%04x dst=%08x\n", handle, game_ftell(file), count, regs->edx);
    }
    size_t length = game_fread(guest_pointer(regs->edx), count, file);
    if (game_ferror(file)) {
        game_clearerr(file);
        Game_DosError(regs, 5);
        return;
    }
    regs->eax = (uint32_t)length;
    Game_DosSetCarry(regs, 0);
}

/* AH=40h.  The count as in dos_read.  Standard output, standard error and CON
 * go to the host's; NUL takes everything. */
static void dos_write(Game_DosRegs *regs)
{
    uint16_t handle = Game_DosBX(regs);
    uint32_t length = regs->ecx;
    if (handle == 1 || handle == 2 || is_device(handle)) {
        if (handle != GAME_HANDLE_NUL && length != 0) {
            FILE *out = handle == 2 ? stderr : stdout;
            fwrite(guest_pointer(regs->edx), 1, length, out);
            fflush(out);
        }
        regs->eax = length;
        Game_DosSetCarry(regs, 0);
        return;
    }
    game_file *file = Game_HandleFile(handle);
    if (file == NULL) {
        Game_DosError(regs, 6);
        return;
    }
    /* A count of 0 cuts the file at the current position: WATCOM's open does
     * this for O_TRUNC, and so does its write(fd, buf, 0). */
    if (length == 0) {
        if (Game_TraceFileEnabled()) fprintf(stderr, "FILE TRUNC h=%02x at=%08lx\n", handle, game_ftell(file));
        if (game_ftruncate(file) != 0) {
            game_clearerr(file);
            Game_DosError(regs, 5);
            return;
        }
        regs->eax = 0;
        Game_DosSetCarry(regs, 0);
        return;
    }
    size_t written = game_fwrite(guest_pointer(regs->edx), length, file);
    if (written != length) {
        Game_DosError(regs, 5);
        return;
    }
    regs->eax = length;
    Game_DosSetCarry(regs, 0);
}

/* AH=42h: CX:DX the offset, AL the origin; the position comes back in DX:AX
 * (and whole in EAX). */
static void dos_seek(Game_DosRegs *regs)
{
    uint16_t handle = Game_DosBX(regs);
    game_file *file = Game_HandleFile(handle);
    int origin;
    int32_t offset = (int32_t)(((uint32_t)Game_DosCX(regs) << 16) | Game_DosDX(regs));
    switch (Game_DosAL(regs)) {
        case 0: origin = SEEK_SET; break;
        case 1: origin = SEEK_CUR; break;
        case 2: origin = SEEK_END; break;
        default:
            Game_DosError(regs, 1);
            return;
    }
    if (Game_TraceFileEnabled()) {
        fprintf(stderr, "FILE SEEK h=%02x al=%02x off=%08x at=%08lx\n",
                handle, Game_DosAL(regs), (uint32_t)offset, file != NULL ? game_ftell(file) : -1L);
    }
    if (file == NULL) {
        Game_DosError(regs, 6);
        return;
    }
    if (game_fseek(file, offset, origin) != 0) {
        Game_DosError(regs, 1);
        return;
    }
    long position = game_ftell(file);
    if (position < 0) {
        Game_DosError(regs, 1);
        return;
    }
    regs->eax = (uint32_t)position;
    regs->edx = (uint32_t)position >> 16;
    Game_DosSetCarry(regs, 0);
}

/* AH=3Eh.  Closing a standard handle or a device does nothing. */
static void dos_close(Game_DosRegs *regs)
{
    uint16_t handle = Game_DosBX(regs);
    if (handle <= 2 || is_device(handle)) {
        Game_DosSetCarry(regs, 0);
        return;
    }
    game_file *file = Game_HandleFile(handle);
    if (file == NULL) {
        Game_DosError(regs, 6);
        return;
    }
    Game_ReleaseHandle(handle);
    if (game_fclose(file) != 0) {
        Game_DosError(regs, 6);
        return;
    }
    Game_DosSetCarry(regs, 0);
}

/* DOS 8.3 wildcard match against an uppercased name (file_pattern_match
 * of SR's DOS ports, same algorithm). */
static int dos_pattern_match(const char *filename, const char *pattern)
{
    int i, asterisk;

    if (pattern[0] == '*' && pattern[1] == '.' && pattern[2] == '*' && pattern[3] == 0) return 1;

new_segment:
    asterisk = 0;
    if (*pattern == '*') {
        asterisk = 1;
        do {
            pattern++;
        } while (*pattern == '*');
    }

test_match:
    for (i = 0; (pattern[i] != 0) && (pattern[i] != '*'); i++) {
        if (filename[i] != pattern[i]) {
            if (filename[i] == 0) return 0;
            if ((pattern[i] == '?') && (filename[i] != '.')) continue;
            if (!asterisk) return 0;
            filename++;
            goto test_match;
        }
    }

    if (pattern[i] == '*') {
        filename += i;
        pattern += i;
        goto new_segment;
    }

    if (pattern[i] == 0) return filename[i] == 0;
    /* pattern ended mid-name with a pending asterisk segment already consumed */
    if (asterisk) {
        if (*filename == 0) return 1;
        filename++;
        goto test_match;
    }
    return 0;
}

/* Split "DIR\FILE.EXT" -> dir + pattern.  Empty dir means CWD. */
static int split_find_path(const char *path, char *dir_out, size_t dir_sz, char *pat_out, size_t pat_sz)
{
    const char *slash = NULL;
    const char *p;

    for (p = path; *p != 0; p++) {
        if (*p == '\\' || *p == '/') slash = p;
    }
    if (slash != NULL) {
        size_t length = (size_t)(slash - path);
        if (length >= dir_sz) return -1;
        memcpy(dir_out, path, length);
        dir_out[length] = 0;
        strncpy(pat_out, slash + 1, pat_sz - 1);
        pat_out[pat_sz - 1] = 0;
    } else {
        dir_out[0] = 0;
        strncpy(pat_out, path, pat_sz - 1);
        pat_out[pat_sz - 1] = 0;
    }
    if (pat_out[0] == 0) {
        if (pat_sz < 5) return -1;
        strcpy(pat_out, "*.*");
    }
    return 0;
}

static void fill_dta_from_result(const find_result *result)
{
    uint8_t *dta = (uint8_t *)guest_pointer(dta_addr);
    struct stat file_stat;
    uint16_t dos_time = 0, dos_date = 0;
    uint32_t size = 0;

    memset(dta, 0, 0x2b);
    if (result->cd_size >= 0) {
        size = (result->attr & 0x10) ? 0 : (uint32_t)result->cd_size;
    } else if (stat(result->host_path, &file_stat) == 0) {
        struct tm *parts = localtime(&file_stat.st_mtime);
        if (parts != NULL) {
            dos_time = (uint16_t)(((parts->tm_hour & 31) << 11) |
                                  ((parts->tm_min & 63) << 5) |
                                  ((parts->tm_sec / 2) & 31));
            dos_date = (uint16_t)((((parts->tm_year - 80) & 127) << 9) |
                                  (((parts->tm_mon + 1) & 15) << 5) |
                                  (parts->tm_mday & 31));
        }
        if (S_ISREG(file_stat.st_mode)) size = (uint32_t)file_stat.st_size;
    }
    dta[0x15] = result->attr;
    store16(dta + 0x16, dos_time);
    store16(dta + 0x18, dos_date);
    store32(dta + 0x1a, size);
    memcpy(dta + 0x1e, result->dos_name, 12);
    dta[0x1e + 12] = 0;
}

/* Write the next collected match into the DTA.  Shared by FindFirst (after
 * collecting) and FindNext. */
static void find_advance(Game_DosRegs *regs)
{
    if (find_next < find_count) {
        const find_result *result = &find_results[find_next++];
        fill_dta_from_result(result);
        if (Game_TraceFileEnabled()) {
            fprintf(stderr, "FILE FIND '%s' -> '%s' attr=%02x (%s)\n",
                    find_pattern, result->dos_name, result->attr, result->host_path);
        }
        Game_DosSetCarry(regs, 0);
        return;
    }
    if (Game_TraceFileEnabled()) fprintf(stderr, "FILE FIND '%s' -> done\n", find_pattern);
    Game_DosError(regs, 0x12); /* no more files */
}

static void clear_find_results(void)
{
    for (unsigned int index = 0; index < find_count; index++) free(find_results[index].host_path);
    find_count = 0;
    find_next = 0;
}

/* Adds a match unless one with the same DOS name is already listed; the
 * write root is collected first, so its copy wins, as it does for reads.
 * Only HIDDEN/SYSTEM/SUBDIR are filtered against CX, the rule
 * Game_dos_findfirst of SR's DOS ports uses; archive on a regular file always passes. */
static void add_find_result(const char *dos_name, const char *host_path, uint8_t attr, uint16_t attr_mask,
                            int64_t cd_size)
{
    if (!dos_pattern_match(dos_name, find_pattern)) return;
    if ((attr & (uint8_t)~attr_mask & (0x02 | 0x04 | 0x10)) != 0) return;
    for (unsigned int index = 0; index < find_count; index++) {
        if (strcmp(find_results[index].dos_name, dos_name) == 0) return;
    }
    if (find_count >= MAX_FIND_RESULTS) return;
    char *copy = strdup(host_path);
    if (copy == NULL) return;
    find_result *result = &find_results[find_count++];
    snprintf(result->dos_name, sizeof(result->dos_name), "%s", dos_name);
    result->host_path = copy;
    result->attr = attr;
    result->cd_size = cd_size;
}

/* A host name as DOS sees it: 8.3 and upper case, as virtualfs lists them;
 * anything longer is skipped there too. */
static int dos_name_of(const char *host_name, char *dos_name)
{
    size_t length = strlen(host_name);
    const char *dot = strchr(host_name, '.');
    if (length == 0 || length > 12 || host_name[0] == '.') return -1;
    if (dot == NULL ? length > 8 : (dot - host_name > 8 || strchr(dot + 1, '.') != NULL || strlen(dot + 1) > 3)) return -1;
    for (size_t index = 0; index <= length; index++) dos_name[index] = (char)toupper((unsigned char)host_name[index]);
    return 0;
}

/* The write root mirrors C:\ (Game_vfs.c's write_root_path): "C:\DIR" or
 * "DIR" -> <write root>/DIR, upper case; nothing changes the DOS current
 * directory from the root. */
static void collect_write_root_matches(const char *dos_dir, uint16_t attr_mask)
{
    char host_dir[PATH_MAX];
    const char *source = dos_dir;
    size_t length;

    if (source[0] != 0 && source[1] == ':') source += 2;
    while (*source == '\\' || *source == '/') source++;
    length = (size_t)snprintf(host_dir, sizeof(host_dir), "%s", Game_WriteRoot());
    if (*source != 0 && length + 1 < sizeof(host_dir)) host_dir[length++] = '/';
    for (; *source != 0 && length + 1 < sizeof(host_dir); source++) {
        host_dir[length++] = (*source == '\\') ? '/' : (char)toupper((unsigned char)*source);
    }
    host_dir[length] = 0;

    DIR *dir = opendir(host_dir);
    if (dir == NULL) return;
    for (struct dirent *entry = readdir(dir); entry != NULL; entry = readdir(dir)) {
        char dos_name[13];
        char host_path[PATH_MAX];
        struct stat file_stat;
        if (dos_name_of(entry->d_name, dos_name) != 0) continue;
        int written = snprintf(host_path, sizeof(host_path), "%s/%s", host_dir, entry->d_name);
        if (written < 0 || written >= (int)sizeof(host_path) || stat(host_path, &file_stat) != 0) continue;
        if (!S_ISREG(file_stat.st_mode) && !S_ISDIR(file_stat.st_mode)) continue;
        add_find_result(dos_name, host_path, S_ISDIR(file_stat.st_mode) ? 0x10 : 0x20, attr_mask, -1);
    }
    closedir(dir);
}

typedef struct {
    uint16_t attr_mask;
    const char *dir;
} cd_find;

static void add_cd_match(const game_cd_entry *entry, void *arg)
{
    const cd_find *find = (const cd_find *)arg;
    char where[PATH_MAX];
    snprintf(where, sizeof(where), "CD:%s\\%s", find->dir, entry->dos_name);
    add_find_result(entry->dos_name, where, entry->directory ? 0x11 : 0x21, find->attr_mask, entry->size);
}

static void set_find_pattern(const char *pattern)
{
    clear_find_results();
    strncpy(find_pattern, pattern, sizeof(find_pattern) - 1);
    find_pattern[sizeof(find_pattern) - 1] = 0;
}

/* AH=4Eh.  A path with the CD's drive letter lists the disc.  Any other lists
 * the write root's matches first, then the game directory's: a file the game
 * wrote is found even when the game directory has none, and it shadows the
 * game directory's copy, as it does for opens. */
static void dos_findfirst(Game_DosRegs *regs)
{
    const char *path = (const char *)guest_pointer(regs->edx);
    char dir_path[PATH_MAX];
    char pattern[16];
    char rewritten[PATH_MAX];
    const char *dir_source;
    file_entry *dir_entry = NULL;
    file_entry *realdir = NULL;
    int vfs_err = 0;
    unsigned int index;
    uint16_t attr_mask = Game_DosCX(regs);

    if (split_find_path(path, dir_path, sizeof(dir_path), pattern, sizeof(pattern)) != 0) {
        Game_DosError(regs, 0x03);
        return;
    }
    for (index = 0; pattern[index] != 0; index++) pattern[index] = (char)toupper((unsigned char)pattern[index]);

    /* The CD drive: Game_cdrom.c's directory, read-only, nothing from the
     * write root. */
    const char *cd_dir = dir_path[0] != 0 && dir_path[1] == ':' ? Game_CdRelativePath(dir_path) : NULL;
    if (cd_dir != NULL) {
        set_find_pattern(pattern);
        cd_find find = { attr_mask, cd_dir };
        if (Game_CdList(cd_dir, add_cd_match, &find) != 0) {
            if (Game_TraceFileEnabled()) fprintf(stderr, "FILE FINDFAIL '%s' (CD dir '%s')\n", path, cd_dir);
            Game_DosError(regs, 0x03);
            return;
        }
        if (Game_TraceFileEnabled()) fprintf(stderr, "FILE FIND1 '%s' CD dir='%s' cx=%04x: %u match(es)\n", path, cd_dir, attr_mask, find_count);
        find_advance(regs);
        return;
    }

    /* C:, the only other drive (Game_ResolveDataPath). */
    dir_source = dir_path;
    if (dir_source[0] != 0 && dir_source[1] == ':') {
        if (toupper((unsigned char)dir_source[0]) != 'C' ||
            snprintf(rewritten, sizeof(rewritten), "C:%s", dir_source + 2) >= (int)sizeof(rewritten)) {
            Game_DosError(regs, 0x03);
            return;
        }
        dir_source = rewritten;
    }

    if (dir_source[0] == 0) {
        dir_entry = vfs_get_current_dir();
    } else {
        vfs_err = vfs_get_real_name(dir_source, NULL, &realdir);
        if (vfs_err == 0 && realdir != NULL && (realdir->attributes & 1)) dir_entry = realdir;
    }

    set_find_pattern(pattern);
    /* a write root in the game directory is the data tree itself */
    if (!Game_WriteRootIsDataRoot()) collect_write_root_matches(dir_source, attr_mask);
    if (dir_entry != NULL) {
        if (dir_entry->dir_visited == 0) vfs_visit_dir(dir_entry);
        for (file_entry *entry = dir_entry->first_child; entry != NULL; entry = entry->next) {
            add_find_result(entry->dos_name, entry->real_fullname, (entry->attributes & 1) ? 0x10 : 0x20, attr_mask, -1);
        }
    }

    if (Game_TraceFileEnabled()) {
        fprintf(stderr, "FILE FIND1 '%s' dir='%s' cx=%04x: %u match(es)%s\n", path,
                dir_entry != NULL ? dir_entry->dos_fullname : dir_path, attr_mask, find_count,
                dir_entry == NULL ? " (not in the data tree)" : "");
    }
    if (dir_entry == NULL && find_count == 0) {
        if (Game_TraceFileEnabled()) fprintf(stderr, "FILE FINDFAIL '%s' (dir '%s' err=%d)\n", path, dir_path, vfs_err);
        Game_DosError(regs, 0x03);
        return;
    }
    find_advance(regs);
}

static void dos_findnext(Game_DosRegs *regs)
{
    if (find_pattern[0] == 0) {
        Game_DosError(regs, 0x12);
        return;
    }
    find_advance(regs);
}

void Game_DosFileInit(void)
{
    dta_addr = GAME_PTR_TO_GUEST32(dta_default);
    clear_find_results();
    find_pattern[0] = 0;
}

void Game_DosFileDeinit(void)
{
    clear_find_results();
    find_pattern[0] = 0;
}

int Game_DosFileCall(Game_DosRegs *regs, void *(*pointer)(uint32_t address))
{
    guest_pointer = pointer;
    switch (Game_DosAH(regs)) {
    case 0x1a:
        dta_addr = regs->edx;
        if (Game_TraceFileEnabled()) fprintf(stderr, "FILE SETDTA %08x\n", dta_addr);
        Game_DosSetCarry(regs, 0);
        return 1;
    case 0x2f:
        regs->ebx = dta_addr;
        Game_DosSetCarry(regs, 0);
        return 1;
    case 0x3c: dos_create(regs); return 1;
    case 0x3d: dos_open(regs); return 1;
    case 0x3e: dos_close(regs); return 1;
    case 0x3f: dos_read(regs); return 1;
    case 0x40: dos_write(regs); return 1;
    case 0x42: dos_seek(regs); return 1;
    case 0x43: dos_attributes(regs); return 1;
    case 0x4e: dos_findfirst(regs); return 1;
    case 0x4f: dos_findnext(regs); return 1;
    default: return 0;
    }
}
