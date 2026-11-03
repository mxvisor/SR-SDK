/**
 *
 *  Copyright (C) 2019-2026 Roman Pauer
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

#define _XOPEN_SOURCE 700
#define _SVID_SOURCE 1
#define _DEFAULT_SOURCE 1
#define _FILE_OFFSET_BITS 64
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2


#define _TIME_BITS 64
#ifdef DEBUG_KERNEL32
#include <inttypes.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#if !defined(_MSC_VER)
#include <strings.h>
#endif

#if (defined(__WIN32__) || defined(__WINDOWS__)) && !defined(_WIN32)
#define _WIN32
#endif

#ifdef _WIN32
#define WINAPI_NODEF_DEFINITIONS
#endif
#include "WinApi.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>
#else
#define USE_OLD_GETTIME 0

#if (USE_OLD_GETTIME)
#include <sys/time.h>
#endif

#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>
#include "CLIB.h"

#if (USE_OLD_GETTIME)
static struct timeval start_time;
#else
static struct timespec start_time;

#if defined(CLOCK_MONOTONIC_RAW)
static clockid_t monotonic_clock_id;

#define MONOTONIC_CLOCK_TYPE monotonic_clock_id
#else
#define MONOTONIC_CLOCK_TYPE CLOCK_MONOTONIC
#endif
#endif

#endif
#include "WinApi-kernel32.h"
#include "Game_memory.h"
#include "Game_cdrom.h"
#include "Game_hooks.h"
#include "Game_vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void CCALL Game_ExitMain_Asm(int32_t status);

#ifdef __cplusplus
}
#endif


#ifndef _WIN32
#define GENERIC_READ    0x80000000
#define GENERIC_WRITE   0x40000000

#define CREATE_NEW          1
#define CREATE_ALWAYS       2
#define OPEN_EXISTING       3
#define OPEN_ALWAYS         4
#define TRUNCATE_EXISTING   5


#define HIGH_PRIORITY_CLASS 0x00000080
#define REALTIME_PRIORITY_CLASS 0x00000100

#define THREAD_PRIORITY_TIME_CRITICAL 15


/* the guest's 260, not the host's PATH_MAX of <limits.h> (the sizes of the guest's structures) */
#undef MAX_PATH
#define MAX_PATH 260

#define FILE_ATTRIBUTE_READONLY             0x00000001
#define FILE_ATTRIBUTE_HIDDEN               0x00000002
#define FILE_ATTRIBUTE_SYSTEM               0x00000004
#define FILE_ATTRIBUTE_DIRECTORY            0x00000010
#define FILE_ATTRIBUTE_ARCHIVE              0x00000020
#define FILE_ATTRIBUTE_DEVICE               0x00000040
#define FILE_ATTRIBUTE_NORMAL               0x00000080
#define FILE_ATTRIBUTE_TEMPORARY            0x00000100
#define FILE_ATTRIBUTE_SPARSE_FILE          0x00000200
#define FILE_ATTRIBUTE_REPARSE_POINT        0x00000400
#define FILE_ATTRIBUTE_COMPRESSED           0x00000800
#define FILE_ATTRIBUTE_OFFLINE              0x00001000
#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED  0x00002000
#define FILE_ATTRIBUTE_ENCRYPTED            0x00004000
#define FILE_ATTRIBUTE_VALID_FLAGS          0x00007fb7
#define FILE_ATTRIBUTE_VALID_SET_FLAGS      0x000031a7

#define EPOCH_TIME INT64_C(116444736000000000)
#endif

#define eprintf(...) fprintf(stderr,__VA_ARGS__)

#define PSEUDO_HANDLE_CURRENT_PROCESS ((void *)(pointer_offset + 0xfffffffful))
#define PSEUDO_HANDLE_CURRENT_THREAD ((void *)(pointer_offset + 0xfffffffeul))


#if !defined(_WIN32)
typedef struct {
    DIR *dirinfo;
    char *directory;
    char *filename;
} find_file_state;

typedef struct _filetime_ {
    uint32_t dwLowDateTime;
    uint32_t dwHighDateTime;
} filetime;

typedef struct _large_integer {
    uint32_t LowPart;
    int32_t HighPart;
} large_integer;

#pragma pack(2)
typedef struct PACKED _systemtime {
    uint16_t wYear;
    uint16_t wMonth;
    uint16_t wDayOfWeek;
    uint16_t wDay;
    uint16_t wHour;
    uint16_t wMinute;
    uint16_t wSecond;
    uint16_t wMilliseconds;
} systemtime;
#pragma pack()

typedef struct _win32_find_data_ {
    uint32_t dwFileAttributes;
    filetime ftCreationTime;
    filetime ftLastAccessTime;
    filetime ftLastWriteTime;
    uint32_t nFileSizeHigh;
    uint32_t nFileSizeLow;
    uint32_t dwReserved0;
    uint32_t dwReserved1;
    char cFileName[MAX_PATH];
    char cAlternateFileName[14];
} win32_find_data;

#endif


/* The letter of the CD drive the game sees: D, unless the port itself is on D:
 * (a real Windows disk), where the game would take its own directory for the
 * CD ("cannot be run from the CD-ROM"): then E */
char Winapi_CdDriveLetter(void)
{
    static char letter = 0;

    if (letter == 0)
    {
        const char *root = NULL;

        letter = 'D';
#ifdef _WIN32
        root = Game_DataRoot();
#endif
        if (root != NULL && (root[0] == 'D' || root[0] == 'd') && root[1] == ':') letter = 'E';
    }
    return letter;
}

/* The drives the game sees: C: is the hard disk, the CD drive (Winapi_CdDriveLetter)
 * holds the port's disc when there is one. Games look for their disc on every
 * CD-ROM drive and by its volume label. */

/* the drive of a root path ("D:\\"), C for the current one; 0 for a drive that is not there */
static char drive_of_root(const char *root)
{
    char drive;

    if (root == NULL) return 'C';
    if (root[0] == 0 || root[1] != ':') return 0;
    drive = (char)toupper((unsigned char)root[0]);
    if (drive == 'C') return 'C';
    if (drive == Winapi_CdDriveLetter() && Game_CdPresent()) return drive;
    return 0;
}

/* the volume label of the disc: the volume identifier of the image's PVD; a copy
 * of the disc in a directory has GAME_CD_VOLUME_LABEL */
static void cd_volume_label(char *label, size_t size)
{
    uint8_t sector[GAME_CD_SECTOR_DATA];
    size_t length;

#ifdef GAME_CD_VOLUME_LABEL
    snprintf(label, size, "%s", GAME_CD_VOLUME_LABEL);
#else
    label[0] = 0;
#endif
    if (Game_CdDiscDir()[0] != 0 || Game_CdReadSector(16, sector) != 0 ||
        memcmp(sector + 1, "CD001", 5) != 0)
    {
        return;
    }
    length = 32;
    while (length != 0 && sector[40 + length - 1] == ' ') length--;
    if (length >= size) length = size - 1;
    memcpy(label, sector + 40, length);
    label[length] = 0;
}

uint32_t CCALL GetDriveTypeA_c(const char *lpRootPathName)
{
    switch (drive_of_root(lpRootPathName))
    {
        case 0: return 1;       /* DRIVE_NO_ROOT_DIR */
        case 'C': return 3;     /* DRIVE_FIXED */
        default: return 5;      /* DRIVE_CDROM */
    }
}

uint32_t CCALL GetLogicalDrives_c(void)
{
    uint32_t mask = 1u << ('C' - 'A');

    if (Game_CdPresent()) mask |= 1u << (Winapi_CdDriveLetter() - 'A');
    return mask;
}

uint32_t CCALL GetLogicalDriveStringsA_c(uint32_t nBufferLength, char *lpBuffer)
{
    char drives[] = "C:\\\0D:\\\0";
    uint32_t length = Game_CdPresent() ? 8 : 4;

    drives[4] = Winapi_CdDriveLetter();
    drives[length] = 0;
    /* the strings and the final empty one; without room: the size needed */
    if (lpBuffer == NULL || nBufferLength < length + 1) return length + 1;
    memcpy(lpBuffer, drives, length + 1);
    return length;
}

uint32_t CCALL GetVolumeInformationA_c(const char *lpRootPathName, char *lpVolumeNameBuffer, uint32_t nVolumeNameSize, uint32_t *lpVolumeSerialNumber, uint32_t *lpMaximumComponentLength, uint32_t *lpFileSystemFlags, char *lpFileSystemNameBuffer, uint32_t nFileSystemNameSize)
{
    char drive = drive_of_root(lpRootPathName);
    char label[33];

    if (drive == 0)
    {
        Winapi_SetLastError(ERROR_PATH_NOT_FOUND);
        return 0;
    }
    if (drive == 'C') label[0] = 0;
    else cd_volume_label(label, sizeof(label));

    if (lpVolumeNameBuffer != NULL && nVolumeNameSize != 0) snprintf(lpVolumeNameBuffer, nVolumeNameSize, "%s", label);
#ifndef GAME_CD_VOLUME_SERIAL
#define GAME_CD_VOLUME_SERIAL 0x0000CD01u
#endif
    if (lpVolumeSerialNumber != NULL) *lpVolumeSerialNumber = (drive != 'C') ? GAME_CD_VOLUME_SERIAL : 0x12345678u;
    if (lpMaximumComponentLength != NULL) *lpMaximumComponentLength = (drive != 'C') ? 110 : 255;
    /* CDFS: FILE_UNICODE_ON_DISK; FAT32: FILE_CASE_SENSITIVE_SEARCH | FILE_CASE_PRESERVED_NAMES */
    if (lpFileSystemFlags != NULL) *lpFileSystemFlags = (drive != 'C') ? 0x00000004u : 0x00000003u;
    if (lpFileSystemNameBuffer != NULL && nFileSystemNameSize != 0) snprintf(lpFileSystemNameBuffer, nFileSystemNameSize, "%s", (drive != 'C') ? "CDFS" : "FAT32");
    return 1;
}

/* a big, empty fixed disk */
uint32_t CCALL GetDiskFreeSpaceA_c(const char *lpRootPathName, uint32_t *lpSectorsPerCluster, uint32_t *lpBytesPerSector, uint32_t *lpNumberOfFreeClusters, uint32_t *lpTotalNumberOfClusters)
{
    (void)lpRootPathName;
    if (lpSectorsPerCluster != NULL) *lpSectorsPerCluster = 8;
    if (lpBytesPerSector != NULL) *lpBytesPerSector = 512;
    if (lpNumberOfFreeClusters != NULL) *lpNumberOfFreeClusters = 0x40000;
    if (lpTotalNumberOfClusters != NULL) *lpTotalNumberOfClusters = 0x80000;
    return 1;
}

/* The game asks for its files on the CD (D:\\...): they are in the directory
 * of the copy of the disc, when the port runs from one */
static int is_d_drive_path(const char *path)
{
    return (path[0] == Winapi_CdDriveLetter() || path[0] == (Winapi_CdDriveLetter() | 0x20)) &&
           path[1] == ':' && (path[2] == '\\' || path[2] == '/');
}

static void redirect_d_drive(const char *src, char *dst)
{
    const char *disc_dir = Game_CdDiscDir();

    if (!is_d_drive_path(src) || disc_dir[0] == 0)
    {
        snprintf(dst, MAX_PATH, "%s", src);
        return;
    }

#ifdef _WIN32
    /* Wine: a Unix path is a path of the Z: drive */
    if (disc_dir[0] == '/')
    {
        char *p;

        snprintf(dst, MAX_PATH, "Z:%s\\%s", disc_dir, src + 3);
        for (p = dst; *p; p++)
        {
            if (*p == '/') *p = '\\';
        }
        return;
    }
#endif
    snprintf(dst, MAX_PATH, "%s/%s", disc_dir, src + 3);
}

/* D: when the disc is an image, not a directory: its files are inside the
 * image, so there is no host path for them */
static int is_d_drive_image_path(const char *path)
{
    return is_d_drive_path(path) && Game_CdDiscDir()[0] == 0 && Game_CdPresent();
}

/* A file of the image, read-only, as a host FILE: a temporary copy, which
 * ReadFile, SetFilePointer and the file mappings take like any other file */
static FILE *open_d_drive_image_file(const char *path)
{
    game_file *source;
    FILE *copy;
    char buffer[16384];
    size_t length;

    source = Game_CdOpen(path + 3);
    if (source == NULL) return NULL;

#ifdef _WIN32
    {
        /* msvcrt's tmpfile() writes into the root of the drive; 'D' deletes
         * the file when it is closed */
        char directory[MAX_PATH], name[MAX_PATH];

        copy = NULL;
        if (GetTempPathA(sizeof(directory), directory) != 0 &&
            GetTempFileNameA(directory, "cd", 0, name) != 0)
        {
            copy = fopen(name, "w+bD");
        }
    }
#else
    copy = tmpfile();
#endif
    if (copy == NULL)
    {
        game_fclose(source);
        return NULL;
    }

    while ((length = game_fread(buffer, sizeof(buffer), source)) != 0)
    {
        if (fwrite(buffer, 1, length, copy) != length)
        {
            game_fclose(source);
            fclose(copy);
            return NULL;
        }
    }
    game_fclose(source);
    rewind(copy);
    return copy;
}

/* the directory the game thinks it is in, "" for the process's own */
#ifndef GAME_WINAPI_START_DIRECTORY
#define GAME_WINAPI_START_DIRECTORY ""
#endif
static char current_directory[MAX_PATH] = GAME_WINAPI_START_DIRECTORY;

/* src as the host file system sees it: a path on D: or relative to the
 * current directory when that is on D: */
static void resolve_path(const char *src, char *dst)
{
    char temp[MAX_PATH];
    char rooted[MAX_PATH];

    /* "D:name": a path relative to the root of the disc */
    if ((src[0] == Winapi_CdDriveLetter() || src[0] == (Winapi_CdDriveLetter() | 0x20)) &&
        src[1] == ':' && src[2] != 0 && src[2] != '\\' && src[2] != '/')
    {
        snprintf(rooted, MAX_PATH, "%c:\\%s", src[0], src + 2);
        src = rooted;
    }

    if (is_d_drive_path(src))
    {
        redirect_d_drive(src, dst);
        return;
    }

    if (is_d_drive_path(current_directory))
    {
#ifdef _WIN32
        /* the process does not change its directory: a full path */
        char *p;

        redirect_d_drive(current_directory, temp);
        if (src[0] == '\\' || src[0] == '/')
            snprintf(dst, MAX_PATH, "%s%s", temp, src);
        else
            snprintf(dst, MAX_PATH, "%s\\%s", temp, src);
        for (p = dst; *p; p++)
        {
            if (*p == '/') *p = '\\';
        }
#else
        /* the process is in the disc directory: a relative path */
        (void)temp;
        snprintf(dst, MAX_PATH, "%s", (src[0] == '\\' || src[0] == '/') ? src + 1 : src);
#endif
        return;
    }

    snprintf(dst, MAX_PATH, "%s", src);
}

/* the path as the host sees it: D: (the disc) redirected */
#ifdef __cplusplus
extern "C"
#endif
void Winapi_ResolveGamePath(const char *src, char *dst)
{
    resolve_path(src, dst);
}

#if defined(_WIN32)
void Winapi_InitTicks(void)
{
}
#endif

#if !defined(_WIN32)
void Winapi_InitTicks(void)
{
    // initialize start time

#if (USE_OLD_GETTIME)
    gettimeofday(&start_time, NULL);
#else
#if defined(CLOCK_MONOTONIC_RAW)
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &start_time))
    {
        monotonic_clock_id = CLOCK_MONOTONIC;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
    }
    else
    {
        monotonic_clock_id = CLOCK_MONOTONIC_RAW;
    }
#else
    clock_gettime(CLOCK_MONOTONIC, &start_time);
#endif
#endif
}

unsigned int CCALL Winapi_GetTicks(void)
{
#ifdef _WIN32
    return GetTickCount();
#else
#if (USE_OLD_GETTIME)
    struct timeval _tv;

    gettimeofday(&_tv, NULL);

    return ((_tv.tv_sec - start_time.tv_sec) * 1000) + ((_tv.tv_usec - start_time.tv_usec) / 1000);
#else
    struct timespec _tp;

    clock_gettime(MONOTONIC_CLOCK_TYPE, &_tp);

    return (unsigned int)((((_tp.tv_sec - start_time.tv_sec) * 1000000000ULL) + (_tp.tv_nsec - start_time.tv_nsec)) / 1000000ULL);
#endif
#endif
}


static int file_pattern_match(const char *filename, const char *pattern)
{
    int i, asterisk;

    if ((pattern[0] == '*') && (pattern[1] == '.') && (pattern[2] == '*') && (pattern[3] == 0) )
    {
        return 1;
    }

new_segment:
    asterisk = 0;

    if (*pattern == '*')
    {
        asterisk = 1;
        do
        {
            pattern++;
        } while (*pattern == '*');
    }

test_match:

    for (i = 0; (pattern[i] != 0) && (pattern[i] != '*'); i++)
    {
        if (toupper(filename[i]) != pattern[i])
        {
            if (filename[i] == 0) return 0;
            if ( (pattern[i] == '?') && (filename[i] != '.') ) continue;
            if (!asterisk) return 0;
            filename++;
            goto test_match;
        }
    }

    if (pattern[i] == '*')
    {
        filename += i;
        pattern += i;
        goto new_segment;
    }
    if (filename[i] == 0) return 1;
    if ( (i != 0) && (pattern[i - 1] == '*') ) return 1;
    if (!asterisk) return 0;
    filename++;
    goto test_match;

    return 1;
}

/*

wildcard matching:
http://xoomer.alice.it/acantato/dev/wildcard/wildmatch.html

*/

/* a time of the host's file system as a FILETIME, in UTC (the least significant bit is zero) */
static void Conv_filetime(filetime *ft, time_t seconds, long nanoseconds)
{
    int64_t dt_result;

    dt_result = ((int64_t) seconds) * 10000000;
    dt_result += (nanoseconds / 100) & ~1; // set least significant bit to zero to indicate system time
    dt_result += EPOCH_TIME;

    ft->dwLowDateTime = (uint32_t) dt_result;
    ft->dwHighDateTime = (uint32_t) (dt_result >> 32);
}

#define STAT_NSEC(filestat, t) ((filestat)->st_##t##tim.tv_nsec)

static void Conv_find(win32_find_data *buffer, struct stat *filestat, const char *filename)
{
    // file attributes
    buffer->dwFileAttributes = (S_ISDIR(filestat->st_mode))?FILE_ATTRIBUTE_DIRECTORY:FILE_ATTRIBUTE_NORMAL;

    // file's creation, last access and modification time and date
    Conv_filetime(&buffer->ftCreationTime, filestat->st_ctime, STAT_NSEC(filestat, c));
    Conv_filetime(&buffer->ftLastAccessTime, filestat->st_atime, STAT_NSEC(filestat, a));
    Conv_filetime(&buffer->ftLastWriteTime, filestat->st_mtime, STAT_NSEC(filestat, m));

    // file size
    buffer->nFileSizeHigh = (uint32_t) (filestat->st_size >> 32);
    buffer->nFileSizeLow = (uint32_t) filestat->st_size;


    buffer->dwReserved0 = 0;
    buffer->dwReserved1 = 0;

    // file name
    strncpy(buffer->cFileName, filename, MAX_PATH - 1);
    buffer->cFileName[MAX_PATH - 1] = 0;

    buffer->cAlternateFileName[0] = 0;
}

#endif

uint32_t CCALL Beep_c(uint32_t dwFreq, uint32_t dwDuration)
{
#ifdef DEBUG_KERNEL32
    eprintf("Beep: %i, %i\n", dwFreq, dwDuration);
#endif

#ifdef _WIN32
    return Beep(dwFreq, dwDuration);
#else
    struct timespec _tp, rem;
    int ret;

    (void)dwFreq;
    _tp.tv_sec = dwDuration / 1000;
    _tp.tv_nsec = (dwDuration % 1000) * 1000000;

    ret = nanosleep(&_tp, &rem);

    while (1)
    {
        if (ret == 0) break;
        else if (errno != EINTR) break;
        else if ((rem.tv_sec == 0) && (rem.tv_nsec < 500000)) break;

        _tp.tv_sec = rem.tv_sec;
        _tp.tv_nsec = rem.tv_nsec;

        ret = nanosleep(&_tp, &rem);
    }

    return 1;
#endif
}

/* the host file of a file handle, NULL for any other handle */
static FILE *file_of_handle(void *hFile)
{
    if (hFile == NULL || hFile == INVALID_HANDLE32_VALUE || ((handle)hFile)->handle_type != HT_FILE) return NULL;
    return (FILE *)((handle)hFile)->fh.f;
}

/* the size of an open file, -1 on an error; the position stays */
static int64_t file_size(FILE *f)
{
    off_t pos, size;

    pos = ftello(f);
    if (pos < 0 || fseeko(f, 0, SEEK_END) != 0) return -1;
    size = ftello(f);
    fseeko(f, pos, SEEK_SET);
    return (int64_t)size;
}

uint32_t CCALL GetFileSize_c(void *hFile, uint32_t *lpFileSizeHigh)
{
    FILE *f;
    int64_t size;

    f = file_of_handle(hFile);
    size = (f != NULL) ? file_size(f) : -1;
    if (size < 0)
    {
        Winapi_SetLastError(ERROR_INVALID_HANDLE);
        return 0xffffffffu;
    }

    if (lpFileSizeHigh != NULL) *lpFileSizeHigh = (uint32_t)((uint64_t)size >> 32);
    /* a size with all the low bits set: the caller asks GetLastError */
    if ((uint32_t)size == 0xffffffffu) Winapi_SetLastError(0);
    return (uint32_t)size;
}

uint32_t CCALL GetFileTime_c(void *hFile, void *lpCreationTime, void *lpLastAccessTime, void *lpLastWriteTime)
{
    FILE *f;

    f = file_of_handle(hFile);
    if (f == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    fflush(f);

#ifdef _WIN32
    return GetFileTime((HANDLE)_get_osfhandle(_fileno(f)), (LPFILETIME)lpCreationTime, (LPFILETIME)lpLastAccessTime, (LPFILETIME)lpLastWriteTime) ? 1 : 0;
#else
    struct stat filestat;

    if (fstat(fileno(f), &filestat) != 0)
    {
        Winapi_SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (lpCreationTime != NULL) Conv_filetime((filetime *)lpCreationTime, filestat.st_ctime, STAT_NSEC(&filestat, c));
    if (lpLastAccessTime != NULL) Conv_filetime((filetime *)lpLastAccessTime, filestat.st_atime, STAT_NSEC(&filestat, a));
    if (lpLastWriteTime != NULL) Conv_filetime((filetime *)lpLastWriteTime, filestat.st_mtime, STAT_NSEC(&filestat, m));
    return 1;
#endif
}

/* The file mappings: CreateFileMappingA reads the file (or as much of it as the size asked)
 * into a guest buffer, the views point into the buffer. Writes into a view do not reach the
 * file. The buffer lives while the handle is open or a view of it is mapped. */
static mapping_handle mappings;

static void release_mapping(mapping_handle m)
{
    mapping_handle *link;

    if (!m->closed || m->views != 0) return;

    for (link = &mappings; *link != NULL; link = (mapping_handle *)&(*link)->next)
    {
        if (*link == m)
        {
            *link = (mapping_handle)m->next;
            break;
        }
    }
    if (m->data != NULL) x86_free(m->data);
    x86_free(m);
}

void * CCALL CreateFileMappingA_c(void *hFile, void */*lpFileMappingAttributes*/, uint32_t /*flProtect*/, uint32_t dwMaximumSizeHigh, uint32_t dwMaximumSizeLow, const char */*lpName*/)
{
    FILE *f;
    int64_t length;
    uint32_t size;
    void *data;
    handle ret;

    /* INVALID_HANDLE_VALUE: memory of the paging file, zeroed */
    f = NULL;
    length = 0;
    if (hFile != INVALID_HANDLE32_VALUE)
    {
        f = file_of_handle(hFile);
        length = (f != NULL) ? file_size(f) : -1;
        if (length < 0)
        {
            Winapi_SetLastError(ERROR_INVALID_HANDLE);
            return NULL;
        }
    }

    if (dwMaximumSizeHigh != 0 || (dwMaximumSizeLow == 0 && length > 0xffffffffll))
    {
        Winapi_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    size = (dwMaximumSizeLow != 0) ? dwMaximumSizeLow : (uint32_t)length;
    if (size == 0)
    {
        Winapi_SetLastError((f != NULL) ? ERROR_FILE_INVALID : ERROR_INVALID_PARAMETER);
        return NULL;
    }

    data = x86_malloc(size);
    if (data == NULL)
    {
        Winapi_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    memset(data, 0, size);
    if (f != NULL)
    {
        off_t pos = ftello(f);

        fseeko(f, 0, SEEK_SET);
        if (fread(data, 1, ((int64_t)size < length) ? size : (size_t)length, f) == 0 && ferror(f))
        {
            clearerr(f);
        }
        fseeko(f, pos, SEEK_SET);
    }

    ret = Winapi_AllocHandle();
    if (ret == NULL)
    {
        x86_free(data);
        Winapi_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    ret->mh.handle_type = HT_MAPPING;
    ret->mh.data = data;
    ret->mh.size = size;
    ret->mh.views = 0;
    ret->mh.closed = 0;
    ret->mh.next = mappings;
    mappings = &ret->mh;
    return ret;
}

void * CCALL MapViewOfFile_c(void *hFileMappingObject, uint32_t /*dwDesiredAccess*/, uint32_t dwFileOffsetHigh, uint32_t dwFileOffsetLow, uint32_t dwNumberOfBytesToMap)
{
    mapping_handle m;

    m = (mapping_handle)hFileMappingObject;
    if (m == NULL || m == (mapping_handle)INVALID_HANDLE32_VALUE || m->handle_type != HT_MAPPING || m->closed)
    {
        Winapi_SetLastError(ERROR_INVALID_HANDLE);
        return NULL;
    }
    if (dwFileOffsetHigh != 0 || dwFileOffsetLow >= m->size ||
        dwNumberOfBytesToMap > m->size - dwFileOffsetLow)
    {
        Winapi_SetLastError(ERROR_ACCESS_DENIED);
        return NULL;
    }

    m->views++;
    return (uint8_t *)m->data + dwFileOffsetLow;
}

uint32_t CCALL UnmapViewOfFile_c(const void *lpBaseAddress)
{
    mapping_handle m;

    for (m = mappings; m != NULL; m = (mapping_handle)m->next)
    {
        if (m->views != 0 && (const uint8_t *)lpBaseAddress >= (const uint8_t *)m->data &&
            (const uint8_t *)lpBaseAddress < (const uint8_t *)m->data + m->size)
        {
            m->views--;
            release_mapping(m);
            return 1;
        }
    }

    Winapi_SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
}

/* the files of 16-bit Windows: the HFILE is the handle of CreateFileA, HFILE_ERROR is
 * INVALID_HANDLE_VALUE */
void * CCALL _lopen_c(const char *lpPathName, uint32_t iReadWrite)
{
    static const uint32_t access[3] = { GENERIC_READ, GENERIC_WRITE, GENERIC_READ | GENERIC_WRITE };

    if ((iReadWrite & 3) == 3)
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE32_VALUE;
    }
    return CreateFileA_c(lpPathName, access[iReadWrite & 3], 0, NULL, OPEN_EXISTING, 0, NULL);
}

uint32_t CCALL _lclose_c(void *hFile)
{
    return CloseHandle_c(hFile) ? 0 : 0xffffffffu;
}

uint32_t CCALL CloseHandle_c(void *hObject)
{
#ifdef DEBUG_KERNEL32
    eprintf("CloseHandle: 0x%" PRIxPTR "\n", (uintptr_t) hObject);
#endif

    if (hObject == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    // pseudo handles
    if ((hObject == PSEUDO_HANDLE_CURRENT_PROCESS) || (hObject == PSEUDO_HANDLE_CURRENT_THREAD))
    {
        return 1;
    }

    switch (((handle)hObject)->handle_type)
    {
        case HT_FILE:
#define hFile ((file_handle)(void *)hObject)

            if (hFile->f != NULL)
            {
                fclose((FILE *)hFile->f);
            }

            x86_free(hFile);

            return 1;
#undef hFile
        case HT_EVENT:
            x86_free(hObject);

            return 1;
        case HT_MAPPING:
            ((mapping_handle)hObject)->closed = 1;
            release_mapping((mapping_handle)hObject);

            return 1;
        case HT_THREAD:
        case HT_SEMAPHORE:
            return Winapi_CloseObject((handle)hObject);
        case HT_PIPE:
#define hPipe ((pipe_handle)hObject)

            if (hPipe->pp != NULL)
            {
#ifdef _WIN32
                CloseHandle((HANDLE)hPipe->pp);
#else
                close((intptr_t)hPipe->pp);
#endif
            }

            x86_free(hPipe);

            return 1;
#undef hPipe
        default:
            break;
    }

    eprintf("Unimplemented: %s\n", "CloseHandle");
    exit(1);
//    return CloseHandle((HANDLE)hObject);
}

uint32_t CCALL CreateDirectoryA_c(const char *lpPathName, void */*lpSecurityAttributes*/)
{
#ifdef DEBUG_KERNEL32
    eprintf("CreateDirectoryA: %s, 0x%" PRIxPTR "\n", lpPathName, (uintptr_t)lpSecurityAttributes);
#endif

    if (lpPathName == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

#ifdef _WIN32
    if (0 != mkdir(lpPathName))
#else
    char buf[8192];

    if (CLIB_FindFile(lpPathName, buf))
    {
        Winapi_SetLastError(ERROR_FILE_EXISTS);
        return 0;
    }

    if (0 != mkdir(buf, 0777))
#endif
    {
        if (errno == EEXIST)
        {
            Winapi_SetLastError(ERROR_FILE_EXISTS);
        }
        else
        {
            Winapi_SetLastError(ERROR_ACCESS_DENIED);
        }
        return 0;
    }

    return 1;
}

uint32_t CCALL SetCurrentDirectoryA_c(const char *lpPathName)
{
    if (lpPathName == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    /* D: is the CD: the files there are found by resolve_path(); another
     * directory is the process's own */
    if (!is_d_drive_path(lpPathName))
    {
#ifdef _WIN32
        if (!SetCurrentDirectoryA(lpPathName)) return 0;
#else
        if (chdir(lpPathName) != 0) return 0;
#endif
    }

    strncpy(current_directory, lpPathName, MAX_PATH - 1);
    current_directory[MAX_PATH - 1] = 0;

    return 1;
}

uint32_t CCALL GetCurrentDirectoryA_c(uint32_t nBufferLength, char *lpBuffer)
{
    char buffer[MAX_PATH];
    size_t length;

    if (lpBuffer == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    if (current_directory[0] != 0)
    {
        snprintf(buffer, sizeof(buffer), "%s", current_directory);
    }
    else
    {
#ifdef _WIN32
        if (GetCurrentDirectoryA(sizeof(buffer), buffer) == 0) return 0;
#else
        if (getcwd(buffer, sizeof(buffer)) == NULL) return 0;
#endif
    }

    length = strlen(buffer);
    if (nBufferLength > length)
    {
        memcpy(lpBuffer, buffer, length + 1);
        return (uint32_t)length;
    }
    return (uint32_t)(length + 1);
}

void * CCALL CreateFileA_c(const char *lpFileName, uint32_t dwDesiredAccess, uint32_t /*dwShareMode*/, void */*lpSecurityAttributes*/, uint32_t dwCreationDistribution, uint32_t /*dwFlagsAndAttributes*/, void *hTemplateFile)
{
    int file_exists;
    char mode[4];
    int SeekToStart;
    handle ret;
    char resolved_path[MAX_PATH];
#ifndef _WIN32
    char buf[8192];
#endif

#ifdef DEBUG_KERNEL32
    eprintf("CreateFileA: %s, 0x%x, %i, %i, 0x%x - ", lpFileName, dwDesiredAccess, dwShareMode, dwCreationDistribution, dwFlagsAndAttributes);
#endif

    if (lpFileName == NULL)
    {
#ifdef DEBUG_KERNEL32
        eprintf("error\n");
#endif
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE32_VALUE;
    }

    if (dwDesiredAccess == 0 || hTemplateFile != 0)
    {
#ifdef DEBUG_KERNEL32
        eprintf("error\n");
#endif
        Winapi_SetLastError(ERROR_NOT_SUPPORTED);
        return INVALID_HANDLE32_VALUE;
    }

    if (is_d_drive_image_path(lpFileName))
    {
        game_cd_entry entry;

        if (Game_CdFind(lpFileName + 3, &entry) != 0 || entry.directory)
        {
            Winapi_SetLastError((dwDesiredAccess & GENERIC_WRITE) ? ERROR_ACCESS_DENIED : ERROR_FILE_NOT_FOUND);
            return INVALID_HANDLE32_VALUE;
        }
        if ((dwDesiredAccess & GENERIC_WRITE) || dwCreationDistribution == CREATE_NEW ||
            dwCreationDistribution == CREATE_ALWAYS || dwCreationDistribution == TRUNCATE_EXISTING)
        {
            Winapi_SetLastError(ERROR_ACCESS_DENIED);
            return INVALID_HANDLE32_VALUE;
        }

        ret = Winapi_AllocHandle();
        if (ret == NULL) return NULL;
        ret->handle_type = HT_FILE;
        ret->fh.f = open_d_drive_image_file(lpFileName);
        if (ret->fh.f == NULL)
        {
            x86_free(ret);
            Winapi_SetLastError(ERROR_ACCESS_DENIED);
            return INVALID_HANDLE32_VALUE;
        }
        return ret;
    }

    resolve_path(lpFileName, resolved_path);
    lpFileName = resolved_path;

#ifdef _WIN32
    file_exists = (INVALID_FILE_ATTRIBUTES != GetFileAttributes(lpFileName));
#else
    file_exists = CLIB_FindFile(lpFileName, buf);
#endif

    /* the NUL device */
    if (strcasecmp(lpFileName, "nul") == 0)
    {
        file_exists = 1;
#ifdef _WIN32
        lpFileName = "NUL";
#else
        snprintf(buf, sizeof(buf), "/dev/null");
#endif
    }

    SeekToStart = 0;
    mode[0] = mode[1] = mode[2] = mode[3] = 0;

    switch (dwCreationDistribution)
    {
        case CREATE_NEW:
            if (file_exists)
            {
#ifdef DEBUG_KERNEL32
                eprintf("error\n");
#endif
                Winapi_SetLastError(ERROR_FILE_EXISTS);
                return INVALID_HANDLE32_VALUE;
            }

            mode[0] = 'w';
            mode[1] = 'b';
            if (dwDesiredAccess & GENERIC_READ)
            {
                mode[2] = '+';
            }
            break;
        case CREATE_ALWAYS:
            mode[0] = 'w';
            mode[1] = 'b';
            if (dwDesiredAccess & GENERIC_READ)
            {
                mode[2] = '+';
            }
            break;
        case OPEN_EXISTING:
            if (!file_exists)
            {
#ifdef DEBUG_KERNEL32
                eprintf("error\n");
#endif
                Winapi_SetLastError(ERROR_FILE_NOT_FOUND);
                return INVALID_HANDLE32_VALUE;
            }

            if (dwDesiredAccess & GENERIC_READ)
            {
                mode[0] = 'r';
                mode[1] = 'b';
                if (dwDesiredAccess & GENERIC_WRITE)
                {
                    mode[2] = '+';
                }
            }
            else
            {
                mode[0] = 'a';
                mode[1] = 'b';
                SeekToStart = 1;
            }
            break;
        case OPEN_ALWAYS:
            if (file_exists)
            {
                if (dwDesiredAccess & GENERIC_READ)
                {
                    mode[0] = 'r';
                    mode[1] = 'b';
                    if (dwDesiredAccess & GENERIC_WRITE)
                    {
                        mode[2] = '+';
                    }
                }
                else
                {
                    mode[0] = 'a';
                    mode[1] = 'b';
                    SeekToStart = 1;
                }
            }
            else
            {
                mode[0] = 'w';
                mode[1] = 'b';
                if (dwDesiredAccess & GENERIC_READ)
                {
                    mode[2] = '+';
                }
            }
            break;
        case TRUNCATE_EXISTING:
            if (dwDesiredAccess & GENERIC_WRITE)
            {
                if (!file_exists)
                {
#ifdef DEBUG_KERNEL32
                    eprintf("error\n");
#endif
                    Winapi_SetLastError(ERROR_FILE_NOT_FOUND);
                    return INVALID_HANDLE32_VALUE;
                }

                mode[0] = 'w';
                mode[1] = 'b';
                if (dwDesiredAccess & GENERIC_READ)
                {
                    mode[2] = '+';
                }
            }
            break;
    }

    if (mode[0] == 0)
    {
#ifdef DEBUG_KERNEL32
        eprintf("error\n");
#endif
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE32_VALUE;
    }

    ret = Winapi_AllocHandle();
    if (ret == NULL)
    {
#ifdef DEBUG_KERNEL32
        eprintf("error\n");
#endif
        return NULL;
    }

    ret->handle_type = HT_FILE;

#ifdef _WIN32
    ret->fh.f = fopen(lpFileName, mode);
#else
    ret->fh.f = fopen(buf, mode);
#endif
    if (ret->fh.f == NULL)
    {
        x86_free(ret);
#ifdef DEBUG_KERNEL32
        eprintf("error\n");
#endif
        Winapi_SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE32_VALUE;
    }

    if (SeekToStart)
    {
        fseek((FILE *)ret->fh.f, 0, SEEK_SET);
    }

#ifdef DEBUG_KERNEL32
    eprintf("0x%" PRIxPTR "\n", (uintptr_t)ret);
#endif
    return ret;
}

void * CCALL CreateMutexA_c(void */*lpMutexAttributes*/, uint32_t /*bInitialOwner*/, const char */*lpName*/)
{
    // the original does not use the mutex - it only checks last error for ERROR_ALREADY_EXISTS, to prevent two instances of the application to run
    Winapi_SetLastError(ERROR_ACCESS_DENIED);
    return NULL;
}

uint32_t CCALL CreatePipe_c(PTR32(void) *hReadPipe, PTR32(void) *hWritePipe, void *lpPipeAttributes, uint32_t nSize)
{
    handle hread, hwrite;
    int ret;

#ifdef DEBUG_KERNEL32
    eprintf("CreatePipe: 0x%" PRIxPTR ", 0x%" PRIxPTR ", 0x%" PRIxPTR ", %i - ", (uintptr_t)hReadPipe, (uintptr_t)hWritePipe, (uintptr_t)lpPipeAttributes, nSize);
#endif

    if ((hReadPipe == NULL) || (hWritePipe == NULL))
    {
#ifdef DEBUG_KERNEL32
        eprintf("error\n");
#endif
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    if ((lpPipeAttributes == NULL) && (nSize == 0))
    {
        hread = Winapi_AllocHandle();
        if (hread == NULL)
        {
#ifdef DEBUG_KERNEL32
        eprintf("error\n");
#endif
            Winapi_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }

        hread->handle_type = HT_PIPE;

        hwrite = Winapi_AllocHandle();
        if (hwrite == NULL)
        {
            x86_free(hread);
#ifdef DEBUG_KERNEL32
        eprintf("error\n");
#endif
            Winapi_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }

        hwrite->handle_type = HT_PIPE;

#ifdef _WIN32
        ret = CreatePipe((PHANDLE)&(hread->pph.pp), (PHANDLE)&(hwrite->pph.pp), (LPSECURITY_ATTRIBUTES)lpPipeAttributes, nSize);
#else
        int pipefd[2];

        if (pipe(pipefd))
        {
            ret = 0;
        }
        else
        {
            hread->pph.pp = (void *)(intptr_t)pipefd[0];
            hwrite->pph.pp = (void *)(intptr_t)pipefd[1];
            ret = 1;
        }
#endif

        if (ret == 0)
        {
            x86_free(hwrite);
            x86_free(hread);
#ifdef DEBUG_KERNEL32
            eprintf("error\n");
#endif
            Winapi_SetLastError(ERROR_ACCESS_DENIED);
            return 0;
        }

        *hReadPipe = hread;
        *hWritePipe = hwrite;

#ifdef DEBUG_KERNEL32
        eprintf("OK: 0x%" PRIxPTR ", 0x%" PRIxPTR "\n", (uintptr_t)hread, (uintptr_t)hwrite);
#endif

        return 1;
    }

    eprintf("Unsupported method: %s\n", "CreatePipe");
    exit(1);
}

void CCALL DeleteCriticalSection_c(void *lpCriticalSection)
{
#ifdef DEBUG_KERNEL32
    eprintf("DeleteCriticalSection: 0x%" PRIxPTR "\n", (uintptr_t)lpCriticalSection);
#endif

#ifdef _WIN32
    if (sizeof(void *) == 4)
    {
        DeleteCriticalSection((LPCRITICAL_SECTION)lpCriticalSection);
    }
    else
    {
        if (*(LPCRITICAL_SECTION *)lpCriticalSection != NULL)
        {
            DeleteCriticalSection(*(LPCRITICAL_SECTION *)lpCriticalSection);
            free(*(LPCRITICAL_SECTION *)lpCriticalSection);
            *(LPCRITICAL_SECTION *)lpCriticalSection = NULL;
        }
    }
#else
    if (*(pthread_mutex_t **)lpCriticalSection != NULL)
    {
        pthread_mutex_destroy(*(pthread_mutex_t **)lpCriticalSection);
        free(*(pthread_mutex_t **)lpCriticalSection);
        *(pthread_mutex_t **)lpCriticalSection = NULL;
    }
#endif
}

uint32_t CCALL DeleteFileA_c(const char *lpFileName)
{
#ifdef _WIN32
    return DeleteFileA(lpFileName);
#else
    char buf[8192];

    if (lpFileName == NULL || !CLIB_FindFile(lpFileName, buf) || unlink(buf) != 0)
    {
        Winapi_SetLastError(ERROR_FILE_NOT_FOUND);
        return 0;
    }
    return 1;
#endif
}

void CCALL EnterCriticalSection_c(void *lpCriticalSection)
{
#ifdef DEBUG_KERNEL32
    eprintf("EnterCriticalSection: 0x%" PRIxPTR "\n", (uintptr_t)lpCriticalSection);
#endif

#ifdef _WIN32
    if (sizeof(void *) == 4)
    {
        EnterCriticalSection((LPCRITICAL_SECTION)lpCriticalSection);
    }
    else
    {
        EnterCriticalSection(*(LPCRITICAL_SECTION *)lpCriticalSection);
    }
#else
    pthread_mutex_lock(*(pthread_mutex_t **)lpCriticalSection);
#endif
}

void CCALL ExitProcess_c(uint32_t uExitCode)
{
    eprintf("ExitProcess: %u\n", uExitCode);
    Game_ExitMain_Asm((int32_t)uExitCode);
}



uint32_t CCALL FileTimeToLocalFileTime_c(const void *lpFileTime, void *lpLocalFileTime)
{
#ifdef DEBUG_KERNEL32
    eprintf("FileTimeToLocalFileTime\n");
#endif

    if (lpFileTime == NULL || lpLocalFileTime == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

#ifdef _WIN32
    return FileTimeToLocalFileTime((const FILETIME *)lpFileTime, (LPFILETIME)lpLocalFileTime);
#else
    ((filetime *)lpLocalFileTime)->dwLowDateTime = ((const filetime *)lpFileTime)->dwLowDateTime | 1;  // set least significant bit to 1 to indicate local time
    ((filetime *)lpLocalFileTime)->dwHighDateTime = ((const filetime *)lpFileTime)->dwHighDateTime;

    return 1;
#endif
}

uint32_t CCALL FileTimeToSystemTime_c(const void *lpFileTime, void *lpSystemTime)
{
#ifdef DEBUG_KERNEL32
    eprintf("FileTimeToSystemTime\n");
#endif

    if (lpFileTime == NULL || lpSystemTime == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

#ifdef _WIN32
    return FileTimeToSystemTime((const FILETIME *)lpFileTime, (LPSYSTEMTIME)lpSystemTime);
#else
    int64_t dt_time;
    struct tm timem, *ptimem;
    time_t timet;

    dt_time = ( ( ( (uint32_t) (((const filetime *)lpFileTime)->dwLowDateTime) ) | ( ( (uint64_t) ( (uint32_t) (((const filetime *)lpFileTime)->dwHighDateTime) ) ) << 32 ) ) - EPOCH_TIME );
    timet = dt_time / 10000000;

    if (dt_time & 1)
    {
        // least significant bit indicates local time
        ptimem = localtime_r(&timet, &timem);
    }
    else
    {
        // least significant bit indicates system time
        ptimem = gmtime_r(&timet, &timem);
    }

    if (ptimem == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    ((systemtime *)lpSystemTime)->wYear = timem.tm_year + 1900;
    ((systemtime *)lpSystemTime)->wMonth = timem.tm_mon + 1;
    ((systemtime *)lpSystemTime)->wDayOfWeek = timem.tm_wday;
    ((systemtime *)lpSystemTime)->wDay = timem.tm_mday;
    ((systemtime *)lpSystemTime)->wHour = timem.tm_hour;
    ((systemtime *)lpSystemTime)->wMinute = timem.tm_min;
    ((systemtime *)lpSystemTime)->wSecond = timem.tm_sec;
    ((systemtime *)lpSystemTime)->wMilliseconds = (dt_time / 10000) % 1000;

    return 1;
#endif
}

uint32_t CCALL FindClose_c(void *hFindFile)
{
#ifdef DEBUG_KERNEL32
    eprintf("FindClose: 0x%" PRIxPTR "\n", (uintptr_t) hFindFile);
#endif

    if (hFindFile == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    /* FindFirstFile failure returns INVALID_HANDLE_VALUE; the game passes it
     * straight to FindClose, which must fail gracefully, not dereference it. */
    if (hFindFile == INVALID_HANDLE32_VALUE)
    {
        Winapi_SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }

    if (((handle)hFindFile)->handle_type != HT_SEARCH)
    {
        Winapi_SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }

    if (((handle)hFindFile)->sh.d != NULL)
    {
#ifdef _WIN32
        FindClose((HANDLE)((handle)hFindFile)->sh.d);
#else
        find_file_state *state;

        state = (find_file_state *) ((handle)hFindFile)->sh.d;

        if (state->dirinfo != NULL)
        {
            closedir(state->dirinfo);
            state->dirinfo = NULL;
        }
        if (state->directory != NULL)
        {
            free(state->directory);
            state->directory = NULL;
        }
        if (state->filename != NULL)
        {
            free(state->filename);
            state->filename = NULL;
        }

        free(state);
#endif
        ((handle)hFindFile)->sh.d = NULL;
    }

    x86_free(hFindFile);

    return 1;
}

void * CCALL FindFirstFileA_c(const char *lpFileName, void *lpFindFileData)
{
    handle ret;
    char resolved_path[MAX_PATH];

#ifdef DEBUG_KERNEL32
    eprintf("FindFirstFileA: %s, 0x%" PRIxPTR "\n", lpFileName, (uintptr_t) lpFindFileData);
#endif

    if ((lpFileName == NULL) || (lpFindFileData == NULL))
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE32_VALUE;
    }

    resolve_path(lpFileName, resolved_path);
    lpFileName = resolved_path;

#ifdef _WIN32
    ret = Winapi_AllocHandle();
    if (ret == NULL)
    {
        return INVALID_HANDLE32_VALUE;
    }

    ret->handle_type = HT_SEARCH;
    ret->sh.d = FindFirstFileA(lpFileName, (LPWIN32_FIND_DATAA)lpFindFileData);

    if (ret->sh.d == INVALID_HANDLE_VALUE)
    {
        x86_free(ret);
        return INVALID_HANDLE32_VALUE;
    }

    return ret;
#else
    int readdirtype;
    struct stat filestat;
    DIR *dirinfo;
    struct dirent *direntry;
    find_file_state *state;
    const char *pattern, *slash;
    char *replace;
    char orig_directory[MAX_PATH];
    char buf[8192];

    // check for wildcards
    if (strchr(lpFileName, '*') != NULL)
    {
        readdirtype = 1;
    }
    else if (strchr(lpFileName, '?') != NULL)
    {
        readdirtype = 1;
    }
    else
    {
        readdirtype = 0;
    }

    if (readdirtype == 0)
    {
        // no wildcards

        if (!CLIB_FindFile(lpFileName, buf))
        {
            Winapi_SetLastError(ERROR_FILE_NOT_FOUND);
            return INVALID_HANDLE32_VALUE;
        }

        if (0 != stat(buf, &filestat))
        {
            Winapi_SetLastError(ERROR_ACCESS_DENIED);
            return INVALID_HANDLE32_VALUE;
        }

        if (S_ISDIR(filestat.st_mode))
        {
            // directory
            dirinfo = opendir(buf);
            if (dirinfo == NULL)
            {
                Winapi_SetLastError(ERROR_ACCESS_DENIED);
                return INVALID_HANDLE32_VALUE;
            }

            direntry = readdir(dirinfo);
            if (direntry == NULL)
            {
                closedir(dirinfo);
                Winapi_SetLastError(ERROR_FILE_NOT_FOUND);
                return INVALID_HANDLE32_VALUE;
            }
        }
        else
        {
            // regular file / other
            dirinfo = NULL;
            direntry = NULL;
        }
    }
    else
    {
        // wildcards

        // find last slash or backslash in lpFileName
        slash = strrchr(lpFileName, '/');
        pattern = strrchr(lpFileName, '\\');

        if (pattern == NULL)
        {
            pattern = slash;
        }
        else
        {
            if (slash > pattern)
            {
                pattern = slash;
            }
        }

        // copy lpFileName without last entry (delimited by '\' or '/') to orig_directory
        if ((pattern == NULL) || (pattern == lpFileName))
        {
            orig_directory[1] = '.';
            orig_directory[0] = 0;

            pattern = lpFileName;
        }
        else
        {
            strncpy(orig_directory, lpFileName, MAX_PATH - 1);
            orig_directory[MAX_PATH - 1] = 0;

            if (pattern - lpFileName < MAX_PATH)
            {
                orig_directory[pattern - lpFileName] = 0;
            }

            pattern++;
        }

        if (!CLIB_FindFile(orig_directory, buf))
        {
            Winapi_SetLastError(ERROR_FILE_NOT_FOUND);
            return INVALID_HANDLE32_VALUE;
        }

        dirinfo = opendir(buf);
        if (dirinfo == NULL)
        {
            Winapi_SetLastError(ERROR_ACCESS_DENIED);
            return INVALID_HANDLE32_VALUE;
        }

        // copy pattern to orig_directory
        strncpy(orig_directory, pattern, MAX_PATH - 1);
        orig_directory[MAX_PATH - 1] = 0;

        // convert pattern to uppercase
        for (replace = orig_directory; *replace != 0; replace++)
        {
            *replace = toupper(*replace);
        }

        direntry = readdir(dirinfo);

        // find first name matching pattern
        while (direntry != NULL && !file_pattern_match(direntry->d_name, orig_directory))
        {
            direntry = readdir(dirinfo);
        }

        if (direntry == NULL)
        {
            closedir(dirinfo);
            Winapi_SetLastError(ERROR_FILE_NOT_FOUND);
            return INVALID_HANDLE32_VALUE;
        }
    }


    ret = Winapi_AllocHandle();
    if (ret == NULL)
    {
        if (dirinfo != NULL) closedir(dirinfo);
        Winapi_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return INVALID_HANDLE32_VALUE;
    }

    state = (find_file_state *) malloc(sizeof(find_file_state));
    if (state == NULL)
    {
        x86_free(ret);
        if (dirinfo != NULL) closedir(dirinfo);
        Winapi_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return INVALID_HANDLE32_VALUE;
    }

    ret->handle_type = HT_SEARCH;
    ret->sh.d = state;

    state->dirinfo = dirinfo;
    state->directory = NULL;
    state->filename = NULL;

    if (dirinfo != NULL)
    {
        state->directory = strdup(buf);

        if (state->directory == NULL)
        {
            free(state);
            x86_free(ret);
            if (dirinfo != NULL) closedir(dirinfo);
            Winapi_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return INVALID_HANDLE32_VALUE;
        }

        if (readdirtype)
        {
            state->filename = strdup(orig_directory);

            if (state->filename == NULL)
            {
                free(state->directory);
                free(state);
                x86_free(ret);
                if (dirinfo != NULL) closedir(dirinfo);
                Winapi_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return INVALID_HANDLE32_VALUE;
            }
        }

        snprintf(buf, sizeof(buf), "%s/%s", state->directory, direntry->d_name);

        if (0 != stat(buf, &filestat))
        {
            if (state->filename != NULL) free(state->filename);
            free(state->directory);
            free(state);
            x86_free(ret);
            if (dirinfo != NULL) closedir(dirinfo);
            Winapi_SetLastError(ERROR_ACCESS_DENIED);
            return INVALID_HANDLE32_VALUE;
        }
    }

    // fill find structure
    {
        /* cFileName is the name without its directory, as on Windows */
        const char *name = (direntry != NULL) ? direntry->d_name : buf;
        const char *sep = strrchr(name, '/');

        if (sep != NULL) name = sep + 1;
        Conv_find((win32_find_data *)lpFindFileData, &filestat, name);
    }

#ifdef DEBUG_KERNEL32
    eprintf("found file: %s\n", (direntry != NULL)?direntry->d_name:buf);
#endif

    return ret;
#endif
}

uint32_t CCALL FindNextFileA_c(void *hFindFile, void *lpFindFileData)
{
#ifdef DEBUG_KERNEL32
    eprintf("FindNextFileA: 0x%" PRIxPTR ", 0x%" PRIxPTR "\n", (uintptr_t) hFindFile, (uintptr_t) lpFindFileData);
#endif

    if (hFindFile == NULL || lpFindFileData == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    /* FindFirstFile failure returns INVALID_HANDLE_VALUE; the game passes it
     * straight to FindClose, which must fail gracefully, not dereference it. */
    if (hFindFile == INVALID_HANDLE32_VALUE)
    {
        Winapi_SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }

    if (((handle)hFindFile)->handle_type != HT_SEARCH)
    {
        Winapi_SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }

#ifdef _WIN32
    return FindNextFileA((HANDLE)((handle)hFindFile)->sh.d, (LPWIN32_FIND_DATAA)lpFindFileData);
#else
    struct dirent *direntry;
    find_file_state *state;
    struct stat filestat;
    char buf[8192];

    state = (find_file_state *) ((handle)hFindFile)->sh.d;

    if (state->dirinfo == NULL)
    {
        direntry = NULL;
    }
    else if (state->filename == NULL)
    {
        direntry = readdir(state->dirinfo);
    }
    else
    {
        direntry = readdir(state->dirinfo);

        // find first name matching pattern
        while (direntry != NULL && !file_pattern_match(direntry->d_name, state->filename))
        {
            direntry = readdir(state->dirinfo);
        }
    }

    if (direntry == NULL)
    {
        if (state->dirinfo != NULL)
        {
            closedir(state->dirinfo);
            state->dirinfo = NULL;
        }
        if (state->directory != NULL)
        {
            free(state->directory);
            state->directory = NULL;
        }
        if (state->filename != NULL)
        {
            free(state->filename);
            state->filename = NULL;
        }

        Winapi_SetLastError(ERROR_NO_MORE_FILES);
        return 0;
    }

    snprintf(buf, sizeof(buf), "%s/%s", state->directory, direntry->d_name);

    stat(buf, &filestat);

    // fill find structure
    Conv_find((win32_find_data *)lpFindFileData, &filestat, direntry->d_name);

#ifdef DEBUG_KERNEL32
    eprintf("found file: %s\n", direntry->d_name);
#endif

    return 1;
#endif
}

void * CCALL GetCurrentProcess_c(void)
{
#ifdef DEBUG_KERNEL32
    eprintf("GetCurrentProcess\n");
#endif

    // this is a pseudo handle to the current process
    return PSEUDO_HANDLE_CURRENT_PROCESS;
}

void * CCALL GetCurrentThread_c(void)
{
#ifdef DEBUG_KERNEL32
    eprintf("GetCurrentThread\n");
#endif

    // this is a pseudo handle to the current thread
    return PSEUDO_HANDLE_CURRENT_THREAD;
}

uint32_t CCALL GetFullPathNameA_c(const char *lpFileName, uint32_t nBufferLength, char *lpBuffer, PTR32(char) *lpFilePart)
{
    uint32_t len;
    char *p1, *p2;

#ifdef DEBUG_KERNEL32
    eprintf("GetFullPathNameA: %s, %i, 0x%" PRIxPTR ", 0x%" PRIxPTR "\n", lpFileName, nBufferLength, (uintptr_t)lpBuffer, (uintptr_t)lpFilePart);
#endif

    if (lpFileName == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    len = (uint32_t)strlen(lpFileName);

    if (len + 1 > nBufferLength)
    {
        return (uint32_t)(len + 1);
    }

    if (lpBuffer == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    // the original only uses the function to get file path for movie playing, so it doesn't need to be full path
    strcpy(lpBuffer, lpFileName);

    if (lpFilePart != NULL)
    {
        p1 = strrchr(lpBuffer, '\\');
        p2 = strrchr(lpBuffer, '/');

        if (p1 == NULL)
        {
            p1 = p2;
        }
        else if (p2 != NULL)
        {
            if ((uintptr_t)p2 > (uintptr_t)p1)
            {
                p1 = p2;
            }
        }

        if (p1 == NULL)
        {
            p1 = lpBuffer;
        }

        *lpFilePart = p1;
    }

    return (uint32_t)len;
}

uint32_t CCALL GetLastError_c(void)
{
    return Winapi_GetLastError();
}

/* The profiles (NAME.INI).  A game asks for ".\\NAME.INI" or for a path in its
 * directory: the file is the name without the directory.  What
 * WritePrivateProfileStringA writes is in the write root, and that copy is read
 * before the game directory's own. */
static const char *profile_name(const char *lpFileName)
{
    const char *name;

    name = strrchr(lpFileName, '\\');
    if (name == NULL) name = strrchr(lpFileName, '/');
    return (name != NULL) ? (name + 1) : lpFileName;
}

/* the profile's path in the write root; 1 when the file is there */
static int profile_write_path(const char *name, char *path, size_t size)
{
#ifdef _WIN32
    snprintf(path, size, "%s\\%s", Game_WriteRoot(), name);
    return _access(path, 0) == 0;
#else
    char found[MAX_PATH * 2];

    snprintf(path, size, "%s/%s", Game_WriteRoot(), name);
    if (strlen(path) >= sizeof(found) || !CLIB_FindFile(path, found)) return 0;
    snprintf(path, size, "%s", found);
    return 1;
#endif
}

static FILE *open_profile(const char *name)
{
    char path[MAX_PATH * 2];

    if (profile_write_path(name, path, sizeof(path)))
    {
        return fopen(path, "rb");
    }
#ifdef _WIN32
    return fopen(name, "rb");
#else
    if (strlen(name) < sizeof(path) && CLIB_FindFile(name, path))
    {
        return fopen(path, "rb");
    }
    return NULL;
#endif
}

uint32_t CCALL GetPrivateProfileStringA_c(const char *lpAppName, const char *lpKeyName, const char *lpDefault, char *lpReturnedString, uint32_t nSize, const char *lpFileName)
{
    FILE *file;
    unsigned int remaining_size, correct_appname;
    size_t default_length, length;
    int items;
    char buf[8192];

    if (lpFileName != NULL)
    {
        lpFileName = profile_name(lpFileName);
    }

#ifdef DEBUG_KERNEL32
    eprintf("GetPrivateProfileStringA: %s, %s, %s, 0x%" PRIxPTR ", %i, %s\n", lpAppName, lpKeyName, lpDefault, (uintptr_t)lpReturnedString, nSize, lpFileName);
#endif

    if ((lpReturnedString == NULL) || (nSize == 0))
    {
        return 0;
    }

    if (nSize == 1)
    {
        lpReturnedString[0] = 0;
        return 0;
    }

    if ((nSize == 2) && ((lpAppName == NULL) || (lpKeyName == NULL)))
    {
        lpReturnedString[0] = 0;
        lpReturnedString[1] = 0;
        return 0;
    }

    file = (lpFileName != NULL) ? open_profile(lpFileName) : NULL;

    if (file == NULL)
    {
#ifdef DEBUG_KERNEL32
        eprintf("default KeyValue (%i): %s\n", 1, lpDefault);
#endif

        if ((lpAppName == NULL) || (lpKeyName == NULL) || (lpDefault == NULL))
        {
            lpReturnedString[0] = 0;
            lpReturnedString[1] = 0;
            return 0;
        }

        default_length = strlen(lpDefault);

        if (default_length < nSize)
        {
            strcpy(lpReturnedString, lpDefault);
            return (uint32_t)default_length;
        }
        else
        {
            strncpy(lpReturnedString, lpDefault, nSize - 1);
            lpReturnedString[nSize - 1] = 0;
            return nSize - 1;
        }
    }

    correct_appname = 0;
    remaining_size = nSize;

    while (!feof(file))
    {
        items = fscanf(file, "%8191[\r\n]", buf);
        buf[0] = 0;
        items = fscanf(file, "%8191[^\r\n]", buf);
        if (items <= 0) continue;
        length = strlen(buf);

        if (length == 0) continue;

        if (buf[0] == '[')
        {
            // section name

            char *endstr;

            endstr = strchr((char *) &buf, ']');
            if (endstr != NULL)
            {
                *endstr = 0;
                length = endstr - (char *) &(buf[1]);
            }
            else
            {
                length--;
            }

            if (lpAppName == NULL)
            {
                if (length < remaining_size)
                {
                    strcpy(lpReturnedString, (char *) &(buf[1]));
                    lpReturnedString += length + 1;
                    remaining_size -= (unsigned int)(length + 1);
                }
                else
                {
                    strncpy(lpReturnedString, (char *) &(buf[1]), remaining_size);
                    lpReturnedString[remaining_size - 1] = 0;
                    lpReturnedString[remaining_size - 2] = 0;

                    fclose(file);

                    return nSize - 2;
                }
            }
            else
            {
                if (strcasecmp(lpAppName, (char *) &(buf[1])) == 0)
                {
                    correct_appname = 1;
                }
                else
                {
                    correct_appname = 0;
                }
            }
        }
        else
        {
            char *keyvalue, *endstr;

            if ((lpAppName != NULL) && correct_appname)
            {
                keyvalue = strchr((char *) &buf, '=');
                if (keyvalue != NULL)
                {
                    *keyvalue = 0;
                    endstr = keyvalue - 1;
                    keyvalue++;
                }
                else
                {
                    endstr = (char *) &(buf[length - 1]);
                    keyvalue = (char *) &(buf[length]);
                }

                while ((endstr >= (char *) &buf) && (*endstr == ' '))
                {
                    *endstr = 0;
                    endstr--;
                }

                if (buf[0] == 0) continue;

                if (lpKeyName == NULL)
                {
                    length = (endstr + 1) - (char *) &buf;

                    if (length < remaining_size)
                    {
                        strcpy(lpReturnedString, (char *) &buf);
                        lpReturnedString += length + 1;
                        remaining_size -= (unsigned int)(length + 1);
                    }
                    else
                    {
                        strncpy(lpReturnedString, (char *) &buf, remaining_size);
                        lpReturnedString[remaining_size - 1] = 0;
                        lpReturnedString[remaining_size - 2] = 0;

                        fclose(file);

                        return nSize - 2;
                    }
                }
                else
                {
                    if (strcasecmp(lpKeyName, (char *) &buf) != 0) continue;

                    fclose(file);

                    while (*keyvalue == ' ') keyvalue++;

                    endstr = (char *) &(buf[length - 1]);

                    while ((endstr >= keyvalue) && (*endstr == ' '))
                    {
                        *endstr = 0;
                        endstr--;
                    }

                    length = (endstr + 1) - keyvalue;

                    if ((keyvalue[0] == '\"') ||(keyvalue[0] == '\''))
                    {
                        if ((keyvalue[length - 1] == keyvalue[0]) && (length != 1))
                        {
                            keyvalue[length - 1] = 0;
                            length--;
                        }
                        keyvalue++;
                        length--;
                    }

#ifdef DEBUG_KERNEL32
                    eprintf("KeyValue: %s\n", keyvalue);
#endif

                    if (length < nSize)
                    {
                        strcpy(lpReturnedString, keyvalue);
                        return (uint32_t)length;
                    }
                    else
                    {
                        strncpy(lpReturnedString, keyvalue, nSize - 1);
                        lpReturnedString[nSize - 1] = 0;
                        return nSize - 1;
                    }
                }
            }
        }
    }

    fclose(file);

    if ((lpAppName == NULL) || (lpKeyName == NULL))
    {
        if (remaining_size == nSize)
        {
            lpReturnedString[0] = 0;
            lpReturnedString[1] = 0;
            return 0;
        }
        if (remaining_size == 0)
        {
            lpReturnedString[-1] = 0;
            lpReturnedString[-2] = 0;
            return nSize - 2;
        }

        lpReturnedString[0] = 0;
        return nSize - remaining_size;
    }

#ifdef DEBUG_KERNEL32
        eprintf("default KeyValue (%i): %s\n", 2, lpDefault);
#endif

    if (lpDefault == NULL)
    {
        lpReturnedString[0] = 0;
        return 0;
    }
    else
    {
        default_length = strlen(lpDefault);

        if (default_length < nSize)
        {
            strcpy(lpReturnedString, lpDefault);
            return (uint32_t)default_length;
        }
        else
        {
            strncpy(lpReturnedString, lpDefault, nSize - 1);
            lpReturnedString[nSize - 1] = 0;
            return nSize - 1;
        }
    }
}

uint32_t CCALL GetPrivateProfileIntA_c(const char *lpAppName, const char *lpKeyName, int32_t nDefault, const char *lpFileName)
{
    char value[64];
    const char *p;
    uint32_t number, base, digit;
    int negative;

#ifdef DEBUG_KERNEL32
    eprintf("GetPrivateProfileIntA: %s, %s, %i, %s\n", lpAppName, lpKeyName, nDefault, lpFileName);
#endif

    /* no key, or an empty value: the default */
    if (GetPrivateProfileStringA_c(lpAppName, lpKeyName, "", value, sizeof(value), lpFileName) == 0)
    {
        return (uint32_t)nDefault;
    }

    /* as Windows: spaces, a sign, 0x, 0o or 0b, and the digits up to the first other character */
    p = value;
    while (*p == ' ' || *p == '\t') p++;
    negative = (*p == '-');
    if (*p == '-' || *p == '+') p++;
    base = 10;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) base = 16;
    else if (p[0] == '0' && (p[1] == 'o' || p[1] == 'O')) base = 8;
    else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) base = 2;
    if (base != 10) p += 2;

    number = 0;
    for (;; p++)
    {
        if (*p >= '0' && *p <= '9') digit = (uint32_t)(*p - '0');
        else if (*p >= 'a' && *p <= 'f') digit = (uint32_t)(*p - 'a' + 10);
        else if (*p >= 'A' && *p <= 'F') digit = (uint32_t)(*p - 'A' + 10);
        else break;
        if (digit >= base) break;
        number = number * base + digit;
    }

    return negative ? (0u - number) : number;
}

/* a line of a profile, without its end */
typedef struct {
    const char *text;
    size_t length;
} profile_line;

static int profile_line_is_blank(const profile_line *line)
{
    size_t index;

    for (index = 0; index < line->length; index++)
    {
        if (line->text[index] != ' ' && line->text[index] != '\t') return 0;
    }
    return 1;
}

/* "[name]"; any section with name NULL */
static int profile_line_is_section(const profile_line *line, const char *name)
{
    size_t length;

    if (line->length == 0 || line->text[0] != '[') return 0;
    if (name == NULL) return 1;
    length = strlen(name);
    return line->length > length + 1 && strncasecmp(line->text + 1, name, length) == 0 && line->text[length + 1] == ']';
}

/* "key=value", spaces around the key */
static int profile_line_is_key(const profile_line *line, const char *key)
{
    const char *text, *equals;
    size_t length;

    text = line->text;
    equals = (const char *)memchr(text, '=', line->length);
    length = (equals != NULL) ? (size_t)(equals - text) : line->length;
    while (length != 0 && (text[length - 1] == ' ' || text[length - 1] == '\t')) length--;
    while (length != 0 && (text[0] == ' ' || text[0] == '\t'))
    {
        text++;
        length--;
    }
    return length != 0 && length == strlen(key) && strncasecmp(text, key, length) == 0;
}

/* The profile is read (the write root's copy, else the game's), changed and written
 * whole into the write root, with CRLF as Windows writes it.  No key: the section
 * goes; no string: the key goes; a new key ends its section, a new section the file. */
uint32_t CCALL WritePrivateProfileStringA_c(const char *lpAppName, const char *lpKeyName, const char *lpString, const char *lpFileName)
{
    const char *name;
    char path[MAX_PATH * 2];
    FILE *file;
    char *text, *added, *p;
    long text_size;
    profile_line *lines;
    size_t count, index, section, end, key;
    int written;

#ifdef DEBUG_KERNEL32
    eprintf("WritePrivateProfileStringA: %s, %s, %s, %s\n", lpAppName, lpKeyName, lpString, lpFileName);
#endif

    if (lpAppName == NULL || lpFileName == NULL)
    {
        /* (NULL, NULL, NULL, file) writes the cached file: there is no cache */
        if (lpAppName == NULL && lpKeyName == NULL && lpString == NULL) return 1;
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    name = profile_name(lpFileName);

    text = NULL;
    text_size = 0;
    file = open_profile(name);
    if (file != NULL)
    {
        if (fseek(file, 0, SEEK_END) == 0) text_size = ftell(file);
        if (text_size < 0) text_size = 0;
        rewind(file);
        text = (char *)malloc((size_t)text_size + 1);
        if (text != NULL) text_size = (long)fread(text, 1, (size_t)text_size, file);
        fclose(file);
    }

    count = 1;
    for (index = 0; index < (size_t)text_size; index++)
    {
        if (text[index] == '\n') count++;
    }

    /* room for a blank line, a section and a key */
    lines = (profile_line *)malloc((count + 3) * sizeof(profile_line));
    added = (char *)malloc(strlen(lpAppName) + ((lpKeyName != NULL) ? strlen(lpKeyName) : 0) + ((lpString != NULL) ? strlen(lpString) : 0) + 8);
    if (lines == NULL || added == NULL || (file != NULL && text == NULL))
    {
        free(text);
        free(lines);
        free(added);
        Winapi_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }

    count = 0;
    p = text;
    for (index = 0; text != NULL && index <= (size_t)text_size; index++)
    {
        if (index == (size_t)text_size || text[index] == '\n')
        {
            lines[count].text = p;
            lines[count].length = (size_t)(text + index - p);
            if (lines[count].length != 0 && p[lines[count].length - 1] == '\r') lines[count].length--;
            if (index != (size_t)text_size || lines[count].length != 0) count++;
            p = text + index + 1;
        }
    }

    for (section = 0; section < count && !profile_line_is_section(&lines[section], lpAppName); section++);
    for (end = section + 1; end < count && !profile_line_is_section(&lines[end], NULL); end++);

    if (section == count)
    {
        if (lpKeyName == NULL || lpString == NULL)
        {
            /* nothing to remove */
            free(text);
            free(lines);
            free(added);
            return 1;
        }

        if (count != 0 && !profile_line_is_blank(&lines[count - 1]))
        {
            lines[count].text = "";
            lines[count].length = 0;
            count++;
        }
        lines[count].text = added;
        lines[count].length = (size_t)sprintf(added, "[%s]", lpAppName);
        count++;
        lines[count].text = added + lines[count - 1].length + 1;
        lines[count].length = (size_t)sprintf(added + lines[count - 1].length + 1, "%s=%s", lpKeyName, lpString);
        count++;
    }
    else if (lpKeyName == NULL)
    {
        memmove(&lines[section], &lines[end], (count - end) * sizeof(profile_line));
        count -= end - section;
    }
    else
    {
        for (key = section + 1; key < end && !profile_line_is_key(&lines[key], lpKeyName); key++);

        if (lpString == NULL)
        {
            if (key < end)
            {
                memmove(&lines[key], &lines[key + 1], (count - key - 1) * sizeof(profile_line));
                count--;
            }
        }
        else
        {
            if (key == end)
            {
                /* after the last line of the section that is not blank */
                while (key > section + 1 && profile_line_is_blank(&lines[key - 1])) key--;
                memmove(&lines[key + 1], &lines[key], (count - key) * sizeof(profile_line));
                count++;
            }
            lines[key].text = added;
            lines[key].length = (size_t)sprintf(added, "%s=%s", lpKeyName, lpString);
        }
    }

#ifdef _WIN32
    CreateDirectoryA(Game_WriteRoot(), NULL);
#else
    mkdir(Game_WriteRoot(), 0777);
#endif
    profile_write_path(name, path, sizeof(path));
    file = fopen(path, "wb");
    written = (file != NULL);
    for (index = 0; written && index < count; index++)
    {
        written = fwrite(lines[index].text, 1, lines[index].length, file) == lines[index].length &&
                  fwrite("\r\n", 1, 2, file) == 2;
    }
    if (file != NULL && fclose(file) != 0) written = 0;

    free(text);
    free(lines);
    free(added);

    if (!written)
    {
        Winapi_SetLastError(ERROR_ACCESS_DENIED);
        return 0;
    }
    return 1;
}

void CCALL InitializeCriticalSection_c(void *lpCriticalSection)
{
#ifdef DEBUG_KERNEL32
    eprintf("InitializeCriticalSection: 0x%" PRIxPTR "\n", (uintptr_t)lpCriticalSection);
#endif

#ifdef _WIN32
    if (sizeof(void *) == 4)
    {
        InitializeCriticalSection((LPCRITICAL_SECTION)lpCriticalSection);
    }
    else
    {
        LPCRITICAL_SECTION critical_section;

        critical_section = (LPCRITICAL_SECTION)malloc(sizeof(CRITICAL_SECTION));
        if (critical_section == NULL)
        {
            eprintf("InitializeCriticalSection: critical section not created\n");
            exit(6);
        }

        InitializeCriticalSection(critical_section);

        *(LPCRITICAL_SECTION *)lpCriticalSection = critical_section;
    }
#else
    pthread_mutexattr_t attr;
    pthread_mutex_t *mutex;

    mutex = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
    if (mutex == NULL) goto InitializeCriticalSection_error;

    if (0 != pthread_mutexattr_init(&attr)) goto InitializeCriticalSection_error;
    if (0 != pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE)) goto InitializeCriticalSection_error;
    if (0 != pthread_mutex_init(mutex, &attr)) goto InitializeCriticalSection_error;
    pthread_mutexattr_destroy(&attr);

    *(pthread_mutex_t **)lpCriticalSection = mutex;

    return;

InitializeCriticalSection_error:
    eprintf("InitializeCriticalSection: mutex not created\n");
    exit(6);
#endif
}

void CCALL LeaveCriticalSection_c(void *lpCriticalSection)
{
#ifdef DEBUG_KERNEL32
    eprintf("LeaveCriticalSection: 0x%" PRIxPTR "\n", (uintptr_t)lpCriticalSection);
#endif

#ifdef _WIN32
    if (sizeof(void *) == 4)
    {
        LeaveCriticalSection((LPCRITICAL_SECTION)lpCriticalSection);
    }
    else
    {
        LeaveCriticalSection(*(LPCRITICAL_SECTION *)lpCriticalSection);
    }
#else
    pthread_mutex_unlock(*(pthread_mutex_t **)lpCriticalSection);
#endif
}

uint32_t CCALL QueryPerformanceCounter_c(void *lpPerformanceCount)
{
#ifdef _WIN32
    return QueryPerformanceCounter((LARGE_INTEGER *)lpPerformanceCount);
#else
    ((large_integer *)lpPerformanceCount)->LowPart = Winapi_GetTicks();
    ((large_integer *)lpPerformanceCount)->HighPart = 0;
    return 1;
#endif
}

uint32_t CCALL QueryPerformanceFrequency_c(void *lpFrequency)
{
#ifdef _WIN32
    return QueryPerformanceFrequency((LARGE_INTEGER *)lpFrequency);
#else
    // 1000 is enough for the original
    ((large_integer *)lpFrequency)->LowPart = 1000;
    ((large_integer *)lpFrequency)->HighPart = 0;

    return 1;
#endif
}

uint32_t CCALL ReadFile_c(void *hFile, void *lpBuffer, uint32_t nNumberOfBytesToRead, uint32_t *lpNumberOfBytesRead, void *lpOverlapped)
{
#ifdef DEBUG_KERNEL32
    eprintf("ReadFile: 0x%" PRIxPTR ", %i\n", (uintptr_t) hFile, nNumberOfBytesToRead);
#endif

    if (lpNumberOfBytesRead != NULL)
    {
        *lpNumberOfBytesRead = 0;
    }

    if (hFile == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    if (lpOverlapped != NULL)
    {
        Winapi_SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }

    if (lpBuffer == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    if (((handle)hFile)->handle_type == HT_PIPE)
    {
        if (((handle)hFile)->pph.pp == NULL)
        {
            Winapi_SetLastError(ERROR_INVALID_HANDLE);
            return 0;
        }

#ifdef _WIN32
        return ReadFile((HANDLE)((handle)hFile)->pph.pp, lpBuffer, nNumberOfBytesToRead, (LPDWORD)lpNumberOfBytesRead, (LPOVERLAPPED)lpOverlapped);
#else
        ssize_t ret;

        ret = read((intptr_t)((handle)hFile)->pph.pp, lpBuffer, nNumberOfBytesToRead);

        if (ret == -1)
        {
            return 0;
        }

        if (lpNumberOfBytesRead != NULL)
        {
            *lpNumberOfBytesRead = ret;
        }

        return 1;
#endif
    }

    if (((handle)hFile)->handle_type == HT_FILE)
    {
        FILE *f;
        ssize_t bytesreadtotal;
        uint32_t bytes_to_read;
        int iseof;

        f = (FILE *)((handle)hFile)->fh.f;
        if (f == NULL)
        {
            Winapi_SetLastError(ERROR_INVALID_HANDLE);
            return 0;
        }

        bytesreadtotal = 0;
        iseof = 0;
        bytes_to_read = nNumberOfBytesToRead;
        while (bytes_to_read)
        {
            size_t bytesread;

            bytesread = fread(lpBuffer, 1, bytes_to_read, f);
            if (bytesread == 0)
            {
                bytes_to_read = 0;

                if (feof(f))
                {
                    iseof = 1;
                }
                clearerr(f);
            }
            else
            {
                if (bytesread != bytes_to_read)
                {
                    clearerr(f);
                }
                bytesreadtotal += bytesread;
                bytes_to_read -= (uint32_t)bytesread;
                lpBuffer = (void *) (((uintptr_t) lpBuffer) + bytesread);
            }
        }

        if ((bytesreadtotal != 0) || (nNumberOfBytesToRead == 0))
        {
            if (lpNumberOfBytesRead != NULL)
            {
                *lpNumberOfBytesRead = (uint32_t)bytesreadtotal;
            }

            return 1;
        }
        else
        {
            if (iseof)
            {
                Winapi_SetLastError(ERROR_HANDLE_EOF);
                return 1;
            }
            else
            {
                Winapi_SetLastError(ERROR_READ_FAULT);
                return 0;
            }
        }
    }

    eprintf("Unsupported method: %s\n", "ReadFile");
    exit(1);
}

uint32_t CCALL SetErrorMode_c(uint32_t /*uMode*/)
{
#ifdef DEBUG_KERNEL32
    eprintf("SetErrorMode: 0x%x\n", uMode);
#endif

    return 0;
}

uint32_t CCALL SetFilePointer_c(void *hFile, uint32_t lDistanceToMove, uint32_t *lpDistanceToMoveHigh, uint32_t dwMoveMethod)
{
    FILE *f;
    int64_t pos;
    int whence;

    if (hFile == NULL || hFile == INVALID_HANDLE32_VALUE || ((handle)hFile)->handle_type != HT_FILE || (f = (FILE *)((handle)hFile)->fh.f) == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_HANDLE);
        return 0xffffffffu;
    }

    pos = (lpDistanceToMoveHigh != NULL) ? (int64_t)(((uint64_t)(uint32_t)*lpDistanceToMoveHigh << 32) | lDistanceToMove) : (int64_t)(int32_t)lDistanceToMove;
    whence = (dwMoveMethod == FILE_CURRENT) ? SEEK_CUR : (dwMoveMethod == FILE_END) ? SEEK_END : SEEK_SET;

    if (dwMoveMethod > FILE_END)
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return 0xffffffffu;
    }

    if (fseeko(f, (off_t)pos, whence) != 0)
    {
        /* the method is valid, so EINVAL is a position before the start: the
         * MSVC CRT's fopen("...+") seeks to -1 from the end to look for a
         * trailing Ctrl-Z and treats any other error on an empty file as fatal */
        Winapi_SetLastError((errno == EINVAL) ? ERROR_NEGATIVE_SEEK : ERROR_INVALID_PARAMETER);
        return 0xffffffffu;
    }

    pos = (int64_t)ftello(f);
    if (lpDistanceToMoveHigh != NULL) *lpDistanceToMoveHigh = (uint32_t)((uint64_t)pos >> 32);
    return (uint32_t)pos;
}

uint32_t CCALL SetPriorityClass_c(void *hProcess, uint32_t fdwPriority)
{
#ifdef DEBUG_KERNEL32
    eprintf("SetPriorityClass: 0x%" PRIxPTR ", %i\n", (uintptr_t) hProcess, fdwPriority);
#endif

    if (hProcess == PSEUDO_HANDLE_CURRENT_PROCESS)
    {
        // pseudo handle
        if ((fdwPriority == HIGH_PRIORITY_CLASS) || (fdwPriority == REALTIME_PRIORITY_CLASS))
        {
            // function is failing
            return 0;
        }
        // the priority is left as it is
        return 1;
    }

    eprintf("Unsupported method: %s\n", "SetPriorityClass");
    exit(1);
}

uint32_t CCALL SetThreadPriority_c(void *hThread, int32_t nPriority)
{
#ifdef DEBUG_KERNEL32
    eprintf("SetThreadPriority: 0x%" PRIxPTR ", %i\n", (uintptr_t) hThread, nPriority);
#endif

    if ((hThread != NULL) && (hThread != PSEUDO_HANDLE_CURRENT_THREAD) && (((handle)hThread)->handle_type == HT_THREAD))
    {
        return Winapi_SetThreadPriority((handle)hThread, nPriority);
    }

    if (hThread == PSEUDO_HANDLE_CURRENT_THREAD)
    {
        // pseudo handle

#ifdef _WIN32
        return SetThreadPriority(GetCurrentThread(), nPriority);
#else
        if (nPriority == THREAD_PRIORITY_TIME_CRITICAL)
        {
            // function is failing
            return 0;
        }
        return Winapi_SetThreadPriority(NULL, nPriority);
#endif
    }

    eprintf("Unsupported method: %s\n", "SetThreadPriority");
    exit(1);
}

void CCALL Sleep_c(uint32_t cMilliseconds)
{
#ifdef DEBUG_KERNEL32
    eprintf("Sleep: %i\n", cMilliseconds);
#endif

    Winapi_ThreadCheckpoint();

#ifdef _WIN32
    Sleep(cMilliseconds);
#else
    struct timespec _tp, rem;
    int ret;

    _tp.tv_sec = cMilliseconds / 1000;
    _tp.tv_nsec = (cMilliseconds % 1000) * 1000000;

    ret = nanosleep(&_tp, &rem);

    while (1)
    {
        if (ret == 0) break;
        else if (errno != EINTR) break;
        else if ((rem.tv_sec == 0) && (rem.tv_nsec < 500000)) break;

        _tp.tv_sec = rem.tv_sec;
        _tp.tv_nsec = rem.tv_nsec;

        ret = nanosleep(&_tp, &rem);
    }
#endif
}

uint32_t CCALL WriteFile_c(void *hFile, const void *lpBuffer, uint32_t nNumberOfBytesToWrite, uint32_t *lpNumberOfBytesWritten, void *lpOverlapped)
{
#ifdef DEBUG_KERNEL32
    eprintf("WriteFile: 0x%" PRIxPTR ", %i\n", (uintptr_t) hFile, nNumberOfBytesToWrite);
#endif

    if (lpNumberOfBytesWritten != NULL)
    {
        *lpNumberOfBytesWritten = 0;
    }

    if (hFile == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    if (lpOverlapped != NULL)
    {
        Winapi_SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }

    if (lpBuffer == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    if (((handle)hFile)->handle_type == HT_PIPE)
    {
        if (((handle)hFile)->pph.pp == NULL)
        {
            Winapi_SetLastError(ERROR_INVALID_HANDLE);
            return 0;
        }

#ifdef _WIN32
        return WriteFile((HANDLE)((handle)hFile)->pph.pp, lpBuffer, nNumberOfBytesToWrite, (LPDWORD)lpNumberOfBytesWritten, (LPOVERLAPPED)lpOverlapped);
#else
        ssize_t ret;

        ret = write((intptr_t)((handle)hFile)->pph.pp, lpBuffer, nNumberOfBytesToWrite);

        if (ret == -1)
        {
            return 0;
        }

        if (lpNumberOfBytesWritten != NULL)
        {
            *lpNumberOfBytesWritten = ret;
        }

        return 1;
#endif
    }

    if (((handle)hFile)->handle_type == HT_FILE)
    {
        FILE *f;
        ssize_t byteswrittentotal;
        uint32_t bytes_to_write;

        f = (FILE *)((handle)hFile)->fh.f;
        if (f == NULL)
        {
            Winapi_SetLastError(ERROR_INVALID_HANDLE);
            return 0;
        }

        byteswrittentotal = 0;
        bytes_to_write = nNumberOfBytesToWrite;

        while (bytes_to_write)
        {
            size_t byteswritten;

            byteswritten = fwrite(lpBuffer, 1, bytes_to_write, f);
            if (byteswritten == 0)
            {
                bytes_to_write = 0;
                clearerr(f);
            }
            else
            {
                if (byteswritten != bytes_to_write)
                {
                    clearerr(f);
                }
                byteswrittentotal += byteswritten;
                bytes_to_write -= (uint32_t)byteswritten;
                lpBuffer = (void *) (((uintptr_t) lpBuffer) + byteswritten);
            }
        }

        if ((byteswrittentotal != 0) || (nNumberOfBytesToWrite == 0))
        {
            if (lpNumberOfBytesWritten != NULL)
            {
                *lpNumberOfBytesWritten = (uint32_t)byteswrittentotal;
            }

            return 1;
        }
        else
        {
            Winapi_SetLastError(ERROR_WRITE_FAULT);
            return 0;
        }
    }

    eprintf("Unsupported method: %s\n", "WriteFile");
    exit(1);
}

