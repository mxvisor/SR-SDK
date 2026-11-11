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

#if !defined(_GAME_DEFS_H_INCLUDED_)
#define _GAME_DEFS_H_INCLUDED_

#include <SDL.h>
#include <limits.h>
#include "ptr32.h"

/* must be a power of 2 */
#define GAME_MQUEUE_LENGTH (256)

/* must be a power of 2 */
#define GAME_KQUEUE_LENGTH (256)

#if !defined(MAX_PATH) || MAX_PATH < 260
    #undef MAX_PATH
    #define MAX_PATH 260
#endif
#if defined(_MAX_PATH) && _MAX_PATH > MAX_PATH
    #undef MAX_PATH
    #define MAX_PATH _MAX_PATH
#endif
#if defined(_POSIX_PATH_MAX) && _POSIX_PATH_MAX > MAX_PATH
    #undef MAX_PATH
    #define MAX_PATH _POSIX_PATH_MAX
#endif
#if defined(PATH_MAX) && PATH_MAX > MAX_PATH
    #undef MAX_PATH
    #define MAX_PATH PATH_MAX
#endif

#define EC_DISPLAY_CREATE		(0)
#define EC_DISPLAY_DESTROY		(1)
#define EC_PROGRAM_QUIT			(4)
#define EC_MOUSE_MOVE			(5)
#define EC_MOUSE_SET 			(6)
#define EC_INPUT_KEY 			(7)
#define EC_SMK_FUNCTION			(8)
#define EC_WINAPI_TIMER			(9)
#define EC_RUN_ON_MAIN			(10)

#if defined(__GNUC__)
    #define INLINE __inline__
    #define NORETURN __attribute__ ((__noreturn__))
    #define PACKED __attribute__ ((__packed__))
    #if defined(__i386) || (defined(__x86_64) && defined(_WIN32))
        #define CCALL __attribute__ ((__cdecl__))
    #else
        #define CCALL
    #endif
#elif defined(_MSC_VER)
    #define INLINE __inline
    #define NORETURN __declspec(noreturn)
    #define PACKED
    #define CCALL __cdecl
    #define access _access
    #define chdir _chdir
    #define close _close
    #define getcwd _getcwd
    #define lseek _lseek
    #define mkdir _mkdir
    #define open _open
    #define read _read
    #define strcasecmp _stricmp
    #define strdup _strdup
    #define strncasecmp _strnicmp
    #define tzset _tzset
    #define unlink _unlink
    #define write _write
    #if _MSC_VER >= 1900
        #pragma comment(lib, "legacy_stdio_definitions.lib")
    #else
        #define snprintf _snprintf
    #endif
#else
    #define INLINE inline
    #define NORETURN
    #define PACKED
    #define CCALL
#endif

typedef void (*Game_Flip_Procedure)(void *src, void *dst);

#endif /* _GAME_DEFS_H_INCLUDED_ */
