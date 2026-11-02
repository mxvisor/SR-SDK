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

#if !defined(_WINAPI_H_INCLUDED_)
#define _WINAPI_H_INCLUDED_

#include <stdint.h>

#ifdef WINAPI_DEFINE_VARIABLES
#define WINAPI_STORAGE
#else
#define WINAPI_STORAGE extern
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
// C11
WINAPI_STORAGE _Thread_local unsigned int Winapi_LastError;
#elif defined(__cplusplus) && __cplusplus >= 201103L
// C++11
WINAPI_STORAGE thread_local unsigned int Winapi_LastError;
#elif defined(__SUNPRO_C) || defined(__SUNPRO_CC) || defined(__IBMC__) || defined(__IBMCPP__) || defined(__ibmxl__) || defined(__GNUC__) || defined(__llvm__) || (defined(__INTEL_COMPILER) && defined(__linux__))
WINAPI_STORAGE __thread unsigned int Winapi_LastError;
#elif defined(_MSC_VER) || (defined(__INTEL_COMPILER) && defined(_WIN32)) || defined(__BORLANDC__) || defined(__DMC__)
WINAPI_STORAGE __declspec(thread) unsigned int Winapi_LastError;
#else
#error thread local variables are not supported
#endif

#define Winapi_GetLastError() (Winapi_LastError)
#define Winapi_SetLastError(n) Winapi_LastError = (n)


#ifndef WINAPI_NODEF_DEFINITIONS
#ifndef ERROR_FILE_NOT_FOUND
#define ERROR_FILE_NOT_FOUND 2
#endif
#ifndef ERROR_PATH_NOT_FOUND
#define ERROR_PATH_NOT_FOUND 3
#endif
#ifndef ERROR_ACCESS_DENIED
#define ERROR_ACCESS_DENIED 5
#endif
#ifndef ERROR_INVALID_HANDLE
#define ERROR_INVALID_HANDLE 6
#endif
#ifndef ERROR_NOT_ENOUGH_MEMORY
#define ERROR_NOT_ENOUGH_MEMORY 8
#endif
#ifndef ERROR_NO_MORE_FILES
#define ERROR_NO_MORE_FILES 18
#endif
#ifndef ERROR_WRITE_FAULT
#define ERROR_WRITE_FAULT 29
#endif
#ifndef ERROR_READ_FAULT
#define ERROR_READ_FAULT 30
#endif
#ifndef ERROR_HANDLE_EOF
#define ERROR_HANDLE_EOF 38
#endif
#ifndef ERROR_NOT_SUPPORTED
#define ERROR_NOT_SUPPORTED 50
#endif
#ifndef ERROR_FILE_EXISTS
#define ERROR_FILE_EXISTS 80
#endif
#ifndef ERROR_INVALID_PARAMETER
#define ERROR_INVALID_PARAMETER 87
#endif
#ifndef ERROR_NEGATIVE_SEEK
#define ERROR_NEGATIVE_SEEK 131
#endif
#ifndef ERROR_ALREADY_EXISTS
#define ERROR_ALREADY_EXISTS 183
#endif
#endif


#ifdef PTROFS_64BIT
#define INVALID_HANDLE32_VALUE (handle)(pointer_offset + 0xfffffffful)
#else
#define INVALID_HANDLE32_VALUE (handle)(0xfffffffful)
#endif


typedef enum _handle_types_ {
    HT_NONE,
    HT_FILE,
    HT_SEARCH,
    HT_PIPE,
    HT_EVENT,
    HT_THREAD,
    HT_SEMAPHORE,
    HT_MAPPING
} handle_types;


typedef struct {
    handle_types handle_type;
    void *f;
} T_file_handle, *file_handle;

typedef struct {
    handle_types handle_type;
    void *d;
} T_search_handle, *search_handle;

typedef struct {
    handle_types handle_type;
    void *pp;
} T_pipe_handle, *pipe_handle;

typedef struct {
    handle_types handle_type;
    int manual_reset;
    volatile int signaled;
} T_event_handle, *event_handle;

/* a thread or a semaphore (WinApi-thread.c) */
typedef struct {
    handle_types handle_type;
    void *obj;
} T_object_handle, *object_handle;

/* a memory-mapped file (created by the game's CreateFileMappingA): the whole file in a guest buffer */
typedef struct {
    handle_types handle_type;
    void *data;
    uint32_t size;
} T_mapping_handle, *mapping_handle;

typedef union {
    handle_types handle_type;
    T_file_handle fh;
    T_search_handle sh;
    T_pipe_handle pph;
    T_event_handle eh;
    T_object_handle oh;
    T_mapping_handle mh;
} T_handle, *handle;


handle Winapi_AllocHandle(void);

/* WinApi-thread.c: guest threads and semaphores */
#ifdef __cplusplus
extern "C" {
#endif
/* WinApi-process.c: the port's arguments, which GetCommandLineA hands to the guest */
void Winapi_SetCommandLine(int argc, char *argv[]);
/* WinApi-user32.c: calls the window procedure the guest registered */
uint32_t Winapi_CallWndProc(uint32_t hwnd, uint32_t uMsg, uint32_t wParam, uint32_t lParam);
/* CloseHandle of a thread or a semaphore */
uint32_t Winapi_CloseObject(handle hObject);
/* a suspended guest thread stops here: every wait and Sleep pass through it */
void Winapi_ThreadCheckpoint(void);
/* SetThreadPriority of a thread handle or of the current thread (pseudo handle: NULL) */
uint32_t Winapi_SetThreadPriority(handle hThread, int32_t nPriority);
#ifdef __cplusplus
}
#endif


#endif

