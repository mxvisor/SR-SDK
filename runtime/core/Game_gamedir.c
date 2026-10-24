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

/* The game directory as the DOS and the Win32 ports see it: where the game's
 * files are (GAME_DATA_MARKERS, GAME_ENV("DATA_DIR")), where it writes
 * (GAME_ENV("WRITE_ROOT")), the virtual file system over them (C:), the disc
 * (D:) and the DOS file handles shared with the C library layer (Game_vfs.c).
 * The game's runtime calls Game_FilesInit and Game_FilesDeinit. */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "Game_defs.h"
#include "Game_hooks.h"
#include "Game_cdrom.h"
#include "Game_vfs.h"
#include "virtualfs.h"

extern file_entry Game_CDir;

static game_file *dos_handles[GAME_MAX_HANDLES];
static char data_root[PATH_MAX];
/* Where files the game writes go (Game_SelectWriteRoot): GAME_WRITE_ROOT, else
 * the game directory; gamefiles/ under the start directory when there is no
 * game data.  Anywhere but data_root it is an overlay, see Game_vfs.c. */
static char write_root[PATH_MAX];
static int write_root_is_data_root;

/* GAME_ENV("TRACE_FILE"), read once. */
int Game_TraceFileEnabled(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = getenv(GAME_ENV("TRACE_FILE")) != NULL;
    return enabled;
}

/* An absolute host path: "/..." here, "C:\..." or "\\server\..." on Windows. */
static int host_path_is_absolute(const char *path)
{
#if defined(_WIN32)
    if (isalpha((unsigned char)path[0]) && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) return 1;
    return path[0] == '\\' || path[0] == '/';
#else
    return path[0] == '/';
#endif
}

/* The absolute form of a host path, into out (PATH_MAX bytes); 0 on success. */
static int host_full_path(const char *path, char *out)
{
#if defined(_WIN32)
    return _fullpath(out, path, PATH_MAX) != NULL ? 0 : -1;
#else
    return realpath(path, out) != NULL ? 0 : -1;
#endif
}

/* Whether two host paths are one existing directory, however spelled. */
static int same_directory(const char *first, const char *second)
{
#if defined(_WIN32)
    char full_first[PATH_MAX], full_second[PATH_MAX];
    size_t length_first, length_second;
    if (host_full_path(first, full_first) != 0 || host_full_path(second, full_second) != 0) return 0;
    length_first = strlen(full_first);
    length_second = strlen(full_second);
    while (length_first > 3 && (full_first[length_first - 1] == '\\' || full_first[length_first - 1] == '/')) length_first--;
    while (length_second > 3 && (full_second[length_second - 1] == '\\' || full_second[length_second - 1] == '/')) length_second--;
    if (length_first != length_second) return 0;
    for (size_t index = 0; index < length_first; index++) {
        char a = full_first[index] == '/' ? '\\' : (char)toupper((unsigned char)full_first[index]);
        char b = full_second[index] == '/' ? '\\' : (char)toupper((unsigned char)full_second[index]);
        if (a != b) return 0;
    }
    return 1;
#else
    struct stat status_first, status_second;
    return stat(first, &status_first) == 0 && stat(second, &status_second) == 0 &&
           S_ISDIR(status_first.st_mode) && status_first.st_dev == status_second.st_dev &&
           status_first.st_ino == status_second.st_ino;
#endif
}

/* A regular file `name` in `dir`, any case: copies of the game often change it. */
static int dir_has_file(const char *dir, const char *name)
{
    DIR *handle = opendir(dir);
    if (handle == NULL) return 0;
    int found = 0;
    for (struct dirent *entry = readdir(handle); entry != NULL && !found; entry = readdir(handle)) {
        char path[PATH_MAX];
        struct stat status;
        if (strcasecmp(entry->d_name, name) != 0) continue;
        int length = snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        found = length > 0 && length < (int)sizeof(path) && stat(path, &status) == 0 && S_ISREG(status.st_mode);
    }
    closedir(handle);
    return found;
}

/* DOS path -> host path, via the virtualfs file_entry tree.
 * vfs_get_real_name walks the tree vfs_visit_dir built at Game_FilesInit
 * (case-insensitive through the uppercase dos_name), the one FindFirst/FindNext
 * iterate too. Returns 0 with host_path filled on success (entry present in
 * the tree). */
int Game_ResolveDataPath(const char *dos_path, char *host_path, size_t host_path_size)
{
    char rewritten[PATH_MAX];
    const char *source = dos_path;
    file_entry *realdir;

    if (source == NULL || *source == 0) return -1;
    if (host_path_size < MAX_PATH) return -1;

    /* C: is the vfs tree (Game_CDir); the CD drive is Game_cdrom.c's, and no
     * other drive exists. */
    if (source[1] == ':') {
        if (toupper((unsigned char)source[0]) != 'C') return -1;
        if (snprintf(rewritten, sizeof(rewritten), "C:%s", source + 2) >= (int)sizeof(rewritten))
            return -1;
        source = rewritten;
    }

    /* 0 = found. 1 = final component missing (buf holds the would-be path
     * under the resolved parent); 2 = parent missing. Only an existing file
     * is resolved here; Game_GameDirWritePath places a new one. */
    return vfs_get_real_name(source, host_path, &realdir) == 0 ? 0 : -1;
}

/* A path on the CD drive: its part after "D:", else NULL.  With
 * GAME_CD_CURRENT_DRIVE (Game_hooks.h) the game runs from the disc, and a path
 * without a drive is on it too. */
const char *Game_CdRelativePath(const char *dos_path)
{
    if (dos_path == NULL || dos_path[0] == 0) return NULL;
    if (dos_path[1] != ':') {
#ifdef GAME_CD_CURRENT_DRIVE
        return dos_path;
#else
        return NULL;
#endif
    }
    if (toupper((unsigned char)dos_path[0]) - 'A' != GAME_CD_DRIVE_INDEX) return NULL;
    return dos_path + 2;
}

/* Opens a file on the CD, read-only as the medium is. NULL, errno set, when
 * it is not there (ENOENT) or a write is asked for (EACCES). */
static game_file *open_cd_file(const char *relative, int write)
{
    game_cd_entry entry;
    if (Game_CdFind(relative, &entry) != 0 || entry.directory) {
        errno = ENOENT;
        return NULL;
    }
    if (write) {
        errno = EACCES;
        return NULL;
    }
    game_file *file = Game_CdOpen(relative);
    if (file == NULL) errno = ENOENT;
    return file;
}

const char *Game_WriteRoot(void)
{
    return write_root;
}

int Game_WriteRootIsDataRoot(void)
{
    return write_root_is_data_root;
}

/* With the write root in the game directory: where a C: path is written. A
 * file that is there keeps its own name, whatever its case; a new one goes
 * into its directory under the name in upper case, as DOS makes it, and
 * *new_in is that directory, for Game_GameDirAdded once the file exists. */
int Game_GameDirWritePath(const char *dos_path, char *host_path, size_t host_path_size, void **new_in)
{
    char rewritten[PATH_MAX];
    char found[MAX_PATH];
    const char *source = dos_path;
    const char *leaf;
    file_entry *realdir;
    size_t length;

    *new_in = NULL;
    if (source == NULL || *source == 0) return -1;
    if (source[1] == ':') {
        if (toupper((unsigned char)source[0]) != 'C') return -1;
        if (snprintf(rewritten, sizeof(rewritten), "C:%s", source + 2) >= (int)sizeof(rewritten)) return -1;
        source = rewritten;
    }
    switch (vfs_get_real_name(source, found, &realdir)) {
    case 0:
        return snprintf(host_path, host_path_size, "%s", found) < (int)host_path_size ? 0 : -1;
    case 1:
        /* realdir is the directory, found its would-be path in lower case */
        leaf = source + strlen(source);
        while (leaf > source && leaf[-1] != '\\' && leaf[-1] != '/' && leaf[-1] != ':') leaf--;
        if (*leaf == 0) return -1;
        length = (size_t)snprintf(host_path, host_path_size, "%s/", realdir->real_fullname);
        for (; *leaf != 0; leaf++) {
            if (length + 1 >= host_path_size) return -1;
            host_path[length++] = (char)toupper((unsigned char)*leaf);
        }
        host_path[length] = 0;
        *new_in = realdir;
        return 0;
    default:
        return -1;          /* a directory on the way is missing */
    }
}

/* A file Game_GameDirWritePath placed, now created: into the VFS, so opens by
 * Game_ResolveDataPath and FindFirst see it, as Game_fopen of SR's DOS ports does. */
void Game_GameDirAdded(void *new_in, const char *host_path)
{
    if (new_in != NULL) vfs_add_file((file_entry *)new_in, host_path, 0);
}

/* 0-4 are DOS's standard handles, GAME_HANDLE_CON and GAME_HANDLE_NUL the
 * devices; a file gets one of those between. */
int Game_AttachHandle(game_file *file)
{
    if (file == NULL) return -1;
    for (int handle = 5; handle < GAME_HANDLE_CON; handle++) {
        if (dos_handles[handle] == NULL) {
            dos_handles[handle] = file;
            return handle;
        }
    }
    return -1;
}

/* A path on the CD drive: *on_cd = 1 and the file opened, or NULL with errno
 * set.  A path without a drive (GAME_CD_CURRENT_DRIVE) that the disc does not
 * have, or that is opened for writing, is left to the game directory:
 * *on_cd = 0. */
game_file *Game_OpenCdFile(const char *dos_path, int write, int *on_cd)
{
    const char *relative = Game_CdRelativePath(dos_path);
    game_file *file;
    *on_cd = relative != NULL;
    if (relative == NULL) return NULL;
    file = open_cd_file(relative, write);
#ifdef GAME_CD_CURRENT_DRIVE
    if (file == NULL && relative == dos_path) *on_cd = 0;
#endif
    return file;
}

game_file *Game_HandleFile(int handle)
{
    return (handle >= 0 && handle < GAME_MAX_HANDLES) ? dos_handles[handle] : NULL;
}

void Game_ReleaseHandle(int handle)
{
    if (handle >= 0 && handle < GAME_MAX_HANDLES) dos_handles[handle] = NULL;
}

/* a game that names no data files is recognised by its main file */
#ifndef GAME_DATA_MARKERS
#define GAME_DATA_MARKERS GAME_MAIN_FILENAME
#endif

static int game_dir_has_marker(const char *dir)
{
    static const char *const markers[] = { GAME_DATA_MARKERS };
    for (unsigned int index = 0; index < sizeof(markers) / sizeof(markers[0]); index++)
        if (dir_has_file(dir, markers[index])) return 1;
    return 0;
}

/* The directory of the port's executable, into dir (PATH_MAX bytes); NULL
 * when it is not known.  A portable copy has the program beside the game's
 * files, so it is started from anywhere, without a launcher that cd's there. */
static const char *executable_directory(char *dir)
{
    size_t length;
#if defined(_WIN32)
    DWORD written = GetModuleFileNameA(NULL, dir, PATH_MAX);
    if (written == 0 || written >= PATH_MAX) return NULL;
    length = written;
    while (length > 0 && dir[length - 1] != '\\' && dir[length - 1] != '/') length--;
#else
    ssize_t written = readlink("/proc/self/exe", dir, PATH_MAX - 1);
    if (written <= 0) return NULL;
    length = (size_t)written;
    while (length > 0 && dir[length - 1] != '/') length--;
#endif
    if (length == 0) return NULL;
    dir[length - 1] = 0;
    return dir[0] != 0 ? dir : "/";
}

/* The game directory the runtime chdir()s into: one that holds one of
 * GAME_DATA_MARKERS (Game_hooks.h): GAME_ENV("DATA_DIR"), else the current
 * directory, else the executable's. Before the chdir into data_root, which
 * keeps a relative GAME_ENV("DATA_DIR") valid; once found, kept. */
static int select_data_root(void)
{
    char program_dir[PATH_MAX];
    const char *candidates[3];
    char dir[PATH_MAX];
    if (data_root[0] != 0) return 0;
    candidates[0] = getenv(GAME_ENV("DATA_DIR"));
    candidates[1] = ".";
    candidates[2] = executable_directory(program_dir);
    for (unsigned int index = 0; index < sizeof(candidates) / sizeof(candidates[0]); index++) {
        if (candidates[index] == NULL) continue;
        snprintf(dir, sizeof(dir), "%s", candidates[index]);
        if (game_dir_has_marker(dir)) {
            if (host_full_path(dir, data_root) != 0) {
                data_root[0] = 0;
                return -1;
            }
            return 0;
        }
    }
    return -1;
}

const char *Game_DataRoot(void)
{
    return (select_data_root() == 0) ? data_root : NULL;
}

int Game_SelectWriteRoot(void)
{
    const char *configured_write_root = getenv(GAME_ENV("WRITE_ROOT"));
    char start_dir[PATH_MAX];
    int length;
    if (write_root[0] != 0) return 0;
    if (configured_write_root != NULL && *configured_write_root != 0) {
        length = snprintf(write_root, sizeof(write_root), "%s", configured_write_root);
    } else if (select_data_root() == 0) {
        length = snprintf(write_root, sizeof(write_root), "%s", data_root);
    } else if (getcwd(start_dir, sizeof(start_dir)) != NULL) {
        length = snprintf(write_root, sizeof(write_root), "%s/gamefiles", start_dir);
    } else {
        length = -1;
    }
    if (length < 0 || length >= (int)sizeof(write_root) || !host_path_is_absolute(write_root)) {
        write_root[0] = 0;
        return -1;
    }
    return 0;
}

int Game_FilesInit(void)
{
    if (select_data_root() != 0) {
        fprintf(stderr, GAME_NAME ": game data not found; set " GAME_ENV("DATA_DIR") " to the game directory\n");
        return -1;
    }
    if (Game_SelectWriteRoot() != 0) {
        fprintf(stderr, GAME_NAME ": no usable write directory; set " GAME_ENV("WRITE_ROOT") " to an absolute path\n");
        return -1;
    }
    write_root_is_data_root = same_directory(write_root, data_root);
    if (chdir(data_root) != 0) {
        fprintf(stderr, GAME_NAME ": cannot chdir to data root: %s\n", data_root);
        return -1;
    }
    if (vfs_init(0) != 0) {
        fprintf(stderr, GAME_NAME ": virtualfs init failed\n");
        return -1;
    }
    vfs_visit_dir(&Game_CDir);
    return 0;
}

void Game_FilesDeinit(void)
{
    for (int handle = 5; handle < GAME_MAX_HANDLES; handle++) {
        if (dos_handles[handle] != NULL) game_fclose(dos_handles[handle]);
        dos_handles[handle] = NULL;
    }
}
