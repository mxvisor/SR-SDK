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


/* Link-correct stubs of Win32 imports that no runtime file implements.
 *
 * Each function is its own object of a static library (scons/game.py compiles
 * this file once per function, -DSTUB_<name>), and the linker takes a member
 * only for a symbol that is still undefined: a game that defines the real
 * <name>_c in its own source (game/WinApi-game-impl.c) wins.  (Weak symbols do
 * not do: the mingw linker leaves a reference to a weak COFF symbol
 * unresolved.)  A stub only returns 0; one that needs another result gets
 * implemented.  GAME_DEBUG_STUBS=1 reports the first calls of every function
 * that is still a stub.  tools/gen_import_shim.py writes a game's own stubs
 * only for the imports missing here.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "platform.h"

static void StubLog(const char *name, int *calls)
{
    if (*calls < 3 && getenv("GAME_DEBUG_STUBS") != NULL) {
        (*calls)++;
        fprintf(stderr, "[stub %s]\n", name);
    }
}

#define GAME_STUBLOG(name) \
    do { \
        static int calls_; \
        StubLog(name, &calls_); \
    } while (0)

#ifdef __cplusplus
extern "C" {
#endif

#ifdef STUB_AVIFileExit
uint32_t CCALL AVIFileExit_c(void)
{
    GAME_STUBLOG("AVIFileExit");
    return 0;
}
#endif

#ifdef STUB_AVIFileInit
uint32_t CCALL AVIFileInit_c(void)
{
    GAME_STUBLOG("AVIFileInit");
    return 0;
}
#endif

#ifdef STUB_AVIStreamInfoA
uint32_t CCALL AVIStreamInfoA_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("AVIStreamInfoA");
    return 0;
}
#endif

#ifdef STUB_AVIStreamLength
uint32_t CCALL AVIStreamLength_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("AVIStreamLength");
    return 0;
}
#endif

#ifdef STUB_AVIStreamOpenFromFileA
uint32_t CCALL AVIStreamOpenFromFileA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    GAME_STUBLOG("AVIStreamOpenFromFileA");
    return 0;
}
#endif

#ifdef STUB_AVIStreamRead
uint32_t CCALL AVIStreamRead_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    GAME_STUBLOG("AVIStreamRead");
    return 0;
}
#endif

#ifdef STUB_AVIStreamReadFormat
uint32_t CCALL AVIStreamReadFormat_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("AVIStreamReadFormat");
    return 0;
}
#endif

#ifdef STUB_AVIStreamRelease
uint32_t CCALL AVIStreamRelease_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("AVIStreamRelease");
    return 0;
}
#endif

#ifdef STUB_AllocConsole
uint32_t CCALL AllocConsole_c(void)
{
    GAME_STUBLOG("AllocConsole");
    return 0;
}
#endif

#ifdef STUB_BeginPaint
uint32_t CCALL BeginPaint_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("BeginPaint");
    return 0;
}
#endif

#ifdef STUB_CharToOemA
uint32_t CCALL CharToOemA_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("CharToOemA");
    return 0;
}
#endif

#ifdef STUB_ClientToScreen
uint32_t CCALL ClientToScreen_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("ClientToScreen");
    return 0;
}
#endif

#ifdef STUB_CloseHandle
uint32_t CCALL CloseHandle_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("CloseHandle");
    return 0;
}
#endif

#ifdef STUB_CompareStringA
uint32_t CCALL CompareStringA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    GAME_STUBLOG("CompareStringA");
    return 0;
}
#endif

#ifdef STUB_CompareStringW
uint32_t CCALL CompareStringW_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    GAME_STUBLOG("CompareStringW");
    return 0;
}
#endif

#ifdef STUB_CreateAcceleratorTableA
uint32_t CCALL CreateAcceleratorTableA_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("CreateAcceleratorTableA");
    return 0;
}
#endif

#ifdef STUB_CreateEventA
uint32_t CCALL CreateEventA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("CreateEventA");
    return 0;
}
#endif

#ifdef STUB_CreateFileA
uint32_t CCALL CreateFileA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    GAME_STUBLOG("CreateFileA");
    return 0;
}
#endif

#ifdef STUB_CreateFileMappingA
uint32_t CCALL CreateFileMappingA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    GAME_STUBLOG("CreateFileMappingA");
    return 0;
}
#endif

#ifdef STUB_CreateProcessA
uint32_t CCALL CreateProcessA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7; (void)a8; (void)a9;
    GAME_STUBLOG("CreateProcessA");
    return 0;
}
#endif

#ifdef STUB_CreateRectRgn
uint32_t CCALL CreateRectRgn_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("CreateRectRgn");
    return 0;
}
#endif

#ifdef STUB_CreateSemaphoreA
uint32_t CCALL CreateSemaphoreA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("CreateSemaphoreA");
    return 0;
}
#endif

#ifdef STUB_CreateThread
uint32_t CCALL CreateThread_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    GAME_STUBLOG("CreateThread");
    return 0;
}
#endif

#ifdef STUB_CreateWindowExA
uint32_t CCALL CreateWindowExA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10, uint32_t a11)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7; (void)a8; (void)a9; (void)a10; (void)a11;
    GAME_STUBLOG("CreateWindowExA");
    return 0;
}
#endif

#ifdef STUB_DefWindowProcA
uint32_t CCALL DefWindowProcA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("DefWindowProcA");
    return 0;
}
#endif

#ifdef STUB_DeleteFileA
uint32_t CCALL DeleteFileA_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("DeleteFileA");
    return 0;
}
#endif

#ifdef STUB_DeleteObject
uint32_t CCALL DeleteObject_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("DeleteObject");
    return 0;
}
#endif

#ifdef STUB_DestroyAcceleratorTable
uint32_t CCALL DestroyAcceleratorTable_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("DestroyAcceleratorTable");
    return 0;
}
#endif

#ifdef STUB_DialogBoxParamA
uint32_t CCALL DialogBoxParamA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    GAME_STUBLOG("DialogBoxParamA");
    return 0;
}
#endif

#ifdef STUB_DirectDrawCreate
uint32_t CCALL DirectDrawCreate_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("DirectDrawCreate");
    return 0;
}
#endif

#ifdef STUB_DirectDrawEnumerateA
uint32_t CCALL DirectDrawEnumerateA_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("DirectDrawEnumerateA");
    return 0;
}
#endif

#ifdef STUB_DirectInputCreateA
uint32_t CCALL DirectInputCreateA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("DirectInputCreateA");
    return 0;
}
#endif

#ifdef STUB_DirectPlayCreate
uint32_t CCALL DirectPlayCreate_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("DirectPlayCreate");
    return 0x80004001u;
}
#endif

#ifdef STUB_DirectPlayEnumerateA
uint32_t CCALL DirectPlayEnumerateA_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("DirectPlayEnumerateA");
    return 0x80004001u;
}
#endif

#ifdef STUB_DirectPlayLobbyCreateA
uint32_t CCALL DirectPlayLobbyCreateA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    GAME_STUBLOG("DirectPlayLobbyCreateA");
    return 0x80004001u;
}
#endif

#ifdef STUB_DirectSoundCreate
uint32_t CCALL DirectSoundCreate_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("DirectSoundCreate");
    return 0;
}
#endif

#ifdef STUB_DispatchMessageA
uint32_t CCALL DispatchMessageA_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("DispatchMessageA");
    return 0;
}
#endif

#ifdef STUB_EnableWindow
uint32_t CCALL EnableWindow_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("EnableWindow");
    return 0;
}
#endif

#ifdef STUB_EndDialog
uint32_t CCALL EndDialog_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("EndDialog");
    return 0;
}
#endif

#ifdef STUB_EndPaint
uint32_t CCALL EndPaint_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("EndPaint");
    return 0;
}
#endif

#ifdef STUB_EnumWindows
uint32_t CCALL EnumWindows_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("EnumWindows");
    return 0;
}
#endif

#ifdef STUB_ExitProcess
uint32_t CCALL ExitProcess_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("ExitProcess");
    return 0;
}
#endif

#ifdef STUB_FileTimeToLocalFileTime
uint32_t CCALL FileTimeToLocalFileTime_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("FileTimeToLocalFileTime");
    return 0;
}
#endif

#ifdef STUB_FileTimeToSystemTime
uint32_t CCALL FileTimeToSystemTime_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("FileTimeToSystemTime");
    return 0;
}
#endif

#ifdef STUB_FillRect
uint32_t CCALL FillRect_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("FillRect");
    return 0;
}
#endif

#ifdef STUB_FindClose
uint32_t CCALL FindClose_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("FindClose");
    return 0;
}
#endif

#ifdef STUB_FindFirstFileA
uint32_t CCALL FindFirstFileA_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("FindFirstFileA");
    return 0;
}
#endif

#ifdef STUB_FindResourceA
uint32_t CCALL FindResourceA_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("FindResourceA");
    return 0;
}
#endif

#ifdef STUB_FindWindowA
uint32_t CCALL FindWindowA_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("FindWindowA");
    return 0;
}
#endif

#ifdef STUB_FlushFileBuffers
uint32_t CCALL FlushFileBuffers_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("FlushFileBuffers");
    return 0;
}
#endif

#ifdef STUB_FormatMessageA
uint32_t CCALL FormatMessageA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    GAME_STUBLOG("FormatMessageA");
    return 0;
}
#endif

#ifdef STUB_FreeConsole
uint32_t CCALL FreeConsole_c(void)
{
    GAME_STUBLOG("FreeConsole");
    return 0;
}
#endif

#ifdef STUB_FreeEnvironmentStringsA
uint32_t CCALL FreeEnvironmentStringsA_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("FreeEnvironmentStringsA");
    return 0;
}
#endif

#ifdef STUB_FreeEnvironmentStringsW
uint32_t CCALL FreeEnvironmentStringsW_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("FreeEnvironmentStringsW");
    return 0;
}
#endif

#ifdef STUB_FreeLibrary
uint32_t CCALL FreeLibrary_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("FreeLibrary");
    return 0;
}
#endif

#ifdef STUB_FreeResource
uint32_t CCALL FreeResource_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("FreeResource");
    return 0;
}
#endif

#ifdef STUB_GetACP
uint32_t CCALL GetACP_c(void)
{
    GAME_STUBLOG("GetACP");
    return 0;
}
#endif

#ifdef STUB_GetAsyncKeyState
uint32_t CCALL GetAsyncKeyState_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("GetAsyncKeyState");
    return 0;
}
#endif

#ifdef STUB_GetCPInfo
uint32_t CCALL GetCPInfo_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("GetCPInfo");
    return 0;
}
#endif

#ifdef STUB_GetClassNameA
uint32_t CCALL GetClassNameA_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("GetClassNameA");
    return 0;
}
#endif

#ifdef STUB_GetClientRect
uint32_t CCALL GetClientRect_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("GetClientRect");
    return 0;
}
#endif

#ifdef STUB_GetCommandLineA
uint32_t CCALL GetCommandLineA_c(void)
{
    GAME_STUBLOG("GetCommandLineA");
    return 0;
}
#endif

#ifdef STUB_GetCurrentDirectoryA
uint32_t CCALL GetCurrentDirectoryA_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("GetCurrentDirectoryA");
    return 0;
}
#endif

#ifdef STUB_GetCurrentProcess
uint32_t CCALL GetCurrentProcess_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GetCurrentProcess");
    return 0;
}
#endif

#ifdef STUB_GetCurrentProcessId
uint32_t CCALL GetCurrentProcessId_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GetCurrentProcessId");
    return 0;
}
#endif

#ifdef STUB_GetCurrentThread
uint32_t CCALL GetCurrentThread_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GetCurrentThread");
    return 0;
}
#endif

#ifdef STUB_GetCursorPos
uint32_t CCALL GetCursorPos_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GetCursorPos");
    return 0;
}
#endif

#ifdef STUB_GetDC
uint32_t CCALL GetDC_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GetDC");
    return 0;
}
#endif

#ifdef STUB_GetDeviceCaps
uint32_t CCALL GetDeviceCaps_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("GetDeviceCaps");
    return 0;
}
#endif

#ifdef STUB_GetDlgItem
uint32_t CCALL GetDlgItem_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("GetDlgItem");
    return 0;
}
#endif

#ifdef STUB_GetDlgItemTextA
uint32_t CCALL GetDlgItemTextA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("GetDlgItemTextA");
    return 0;
}
#endif

#ifdef STUB_GetEnvironmentStrings
uint32_t CCALL GetEnvironmentStrings_c(void)
{
    GAME_STUBLOG("GetEnvironmentStrings");
    return 0;
}
#endif

#ifdef STUB_GetEnvironmentStringsW
uint32_t CCALL GetEnvironmentStringsW_c(void)
{
    GAME_STUBLOG("GetEnvironmentStringsW");
    return 0;
}
#endif

#ifdef STUB_GetExitCodeProcess
uint32_t CCALL GetExitCodeProcess_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("GetExitCodeProcess");
    return 0;
}
#endif

#ifdef STUB_GetFileAttributesA
uint32_t CCALL GetFileAttributesA_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GetFileAttributesA");
    return 0;
}
#endif

#ifdef STUB_GetFileInformationByHandle
uint32_t CCALL GetFileInformationByHandle_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("GetFileInformationByHandle");
    return 0;
}
#endif

#ifdef STUB_GetFileType
uint32_t CCALL GetFileType_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GetFileType");
    return 0;
}
#endif

#ifdef STUB_GetFullPathNameA
uint32_t CCALL GetFullPathNameA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("GetFullPathNameA");
    return 0;
}
#endif

#ifdef STUB_GetKeyboardState
uint32_t CCALL GetKeyboardState_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GetKeyboardState");
    return 0;
}
#endif

#ifdef STUB_GetLastError
uint32_t CCALL GetLastError_c(void)
{
    GAME_STUBLOG("GetLastError");
    return 0;
}
#endif

#ifdef STUB_GetLocalTime
uint32_t CCALL GetLocalTime_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GetLocalTime");
    return 0;
}
#endif

#ifdef STUB_GetLocaleInfoA
uint32_t CCALL GetLocaleInfoA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("GetLocaleInfoA");
    return 0;
}
#endif

#ifdef STUB_GetLocaleInfoW
uint32_t CCALL GetLocaleInfoW_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("GetLocaleInfoW");
    return 0;
}
#endif

#ifdef STUB_GetMenu
uint32_t CCALL GetMenu_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GetMenu");
    return 0;
}
#endif

#ifdef STUB_GetModuleFileNameA
uint32_t CCALL GetModuleFileNameA_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("GetModuleFileNameA");
    return 0;
}
#endif

#ifdef STUB_GetModuleHandleA
uint32_t CCALL GetModuleHandleA_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GetModuleHandleA");
    return 0;
}
#endif

#ifdef STUB_GetOEMCP
uint32_t CCALL GetOEMCP_c(void)
{
    GAME_STUBLOG("GetOEMCP");
    return 0;
}
#endif

#ifdef STUB_GetPriorityClass
uint32_t CCALL GetPriorityClass_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GetPriorityClass");
    return 0;
}
#endif

#ifdef STUB_GetProcAddress
uint32_t CCALL GetProcAddress_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("GetProcAddress");
    return 0;
}
#endif

#ifdef STUB_GetProfileStringA
uint32_t CCALL GetProfileStringA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    GAME_STUBLOG("GetProfileStringA");
    return 0;
}
#endif

#ifdef STUB_GetRegionData
uint32_t CCALL GetRegionData_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("GetRegionData");
    return 0;
}
#endif

#ifdef STUB_GetStartupInfoA
uint32_t CCALL GetStartupInfoA_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GetStartupInfoA");
    return 0;
}
#endif

#ifdef STUB_GetStdHandle
uint32_t CCALL GetStdHandle_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GetStdHandle");
    return 0;
}
#endif

#ifdef STUB_GetStringTypeA
uint32_t CCALL GetStringTypeA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    GAME_STUBLOG("GetStringTypeA");
    return 0;
}
#endif

#ifdef STUB_GetStringTypeW
uint32_t CCALL GetStringTypeW_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("GetStringTypeW");
    return 0;
}
#endif

#ifdef STUB_GetSystemDefaultLangID
uint32_t CCALL GetSystemDefaultLangID_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GetSystemDefaultLangID");
    return 0;
}
#endif

#ifdef STUB_GetSystemDirectoryA
uint32_t CCALL GetSystemDirectoryA_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("GetSystemDirectoryA");
    return 0;
}
#endif

#ifdef STUB_GetSystemMetrics
uint32_t CCALL GetSystemMetrics_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GetSystemMetrics");
    return 0;
}
#endif

#ifdef STUB_GetSystemPaletteEntries
uint32_t CCALL GetSystemPaletteEntries_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("GetSystemPaletteEntries");
    return 0;
}
#endif

#ifdef STUB_GetSystemTime
uint32_t CCALL GetSystemTime_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GetSystemTime");
    return 0;
}
#endif

#ifdef STUB_GetThreadPriority
uint32_t CCALL GetThreadPriority_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GetThreadPriority");
    return 0;
}
#endif

#ifdef STUB_GetTickCount
uint32_t CCALL GetTickCount_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GetTickCount");
    return 0;
}
#endif

#ifdef STUB_GetTimeZoneInformation
uint32_t CCALL GetTimeZoneInformation_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GetTimeZoneInformation");
    return 0;
}
#endif

#ifdef STUB_GetUpdateRect
uint32_t CCALL GetUpdateRect_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("GetUpdateRect");
    return 0;
}
#endif

#ifdef STUB_GetUpdateRgn
uint32_t CCALL GetUpdateRgn_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("GetUpdateRgn");
    return 0;
}
#endif

#ifdef STUB_GetUserDefaultLCID
uint32_t CCALL GetUserDefaultLCID_c(void)
{
    GAME_STUBLOG("GetUserDefaultLCID");
    return 0;
}
#endif

#ifdef STUB_GetVersion
uint32_t CCALL GetVersion_c(void)
{
    GAME_STUBLOG("GetVersion");
    return 0;
}
#endif

#ifdef STUB_GetVersionExA
uint32_t CCALL GetVersionExA_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GetVersionExA");
    return 0;
}
#endif

#ifdef STUB_GetWindowLongA
uint32_t CCALL GetWindowLongA_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("GetWindowLongA");
    return 0;
}
#endif

#ifdef STUB_GetWindowRect
uint32_t CCALL GetWindowRect_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("GetWindowRect");
    return 0;
}
#endif

#ifdef STUB_GetWindowTextA
uint32_t CCALL GetWindowTextA_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("GetWindowTextA");
    return 0;
}
#endif

#ifdef STUB_GlobalAlloc
uint32_t CCALL GlobalAlloc_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("GlobalAlloc");
    return 0;
}
#endif

#ifdef STUB_GlobalCompact
uint32_t CCALL GlobalCompact_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GlobalCompact");
    return 0;
}
#endif

#ifdef STUB_GlobalFlags
uint32_t CCALL GlobalFlags_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GlobalFlags");
    return 0;
}
#endif

#ifdef STUB_GlobalFree
uint32_t CCALL GlobalFree_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GlobalFree");
    return 0;
}
#endif

#ifdef STUB_GlobalLock
uint32_t CCALL GlobalLock_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GlobalLock");
    return 0;
}
#endif

#ifdef STUB_GlobalMemoryStatus
uint32_t CCALL GlobalMemoryStatus_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GlobalMemoryStatus");
    return 0;
}
#endif

#ifdef STUB_GlobalReAlloc
uint32_t CCALL GlobalReAlloc_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("GlobalReAlloc");
    return 0;
}
#endif

#ifdef STUB_GlobalSize
uint32_t CCALL GlobalSize_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GlobalSize");
    return 0;
}
#endif

#ifdef STUB_GlobalUnlock
uint32_t CCALL GlobalUnlock_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("GlobalUnlock");
    return 0;
}
#endif

#ifdef STUB_HeapAlloc
uint32_t CCALL HeapAlloc_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("HeapAlloc");
    return 0;
}
#endif

#ifdef STUB_HeapCreate
uint32_t CCALL HeapCreate_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("HeapCreate");
    return 0;
}
#endif

#ifdef STUB_HeapDestroy
uint32_t CCALL HeapDestroy_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("HeapDestroy");
    return 0;
}
#endif

#ifdef STUB_HeapFree
uint32_t CCALL HeapFree_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("HeapFree");
    return 0;
}
#endif

#ifdef STUB_HeapReAlloc
uint32_t CCALL HeapReAlloc_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("HeapReAlloc");
    return 0;
}
#endif

#ifdef STUB_HeapSize
uint32_t CCALL HeapSize_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("HeapSize");
    return 0;
}
#endif

#ifdef STUB_ICClose
uint32_t CCALL ICClose_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("ICClose");
    return 0;
}
#endif

#ifdef STUB_ICDecompress
uint32_t CCALL ICDecompress_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    GAME_STUBLOG("ICDecompress");
    return 0;
}
#endif

#ifdef STUB_ICLocate
uint32_t CCALL ICLocate_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    GAME_STUBLOG("ICLocate");
    return 0;
}
#endif

#ifdef STUB_ICSendMessage
uint32_t CCALL ICSendMessage_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("ICSendMessage");
    return 0;
}
#endif

#ifdef STUB_InvalidateRgn
uint32_t CCALL InvalidateRgn_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("InvalidateRgn");
    return 0;
}
#endif

#ifdef STUB_IsBadCodePtr
uint32_t CCALL IsBadCodePtr_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("IsBadCodePtr");
    return 0;
}
#endif

#ifdef STUB_IsBadReadPtr
uint32_t CCALL IsBadReadPtr_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("IsBadReadPtr");
    return 0;
}
#endif

#ifdef STUB_IsBadWritePtr
uint32_t CCALL IsBadWritePtr_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("IsBadWritePtr");
    return 0;
}
#endif

#ifdef STUB_IsValidCodePage
uint32_t CCALL IsValidCodePage_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("IsValidCodePage");
    return 0;
}
#endif

#ifdef STUB_IsValidLocale
uint32_t CCALL IsValidLocale_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("IsValidLocale");
    return 0;
}
#endif

#ifdef STUB_KillTimer
uint32_t CCALL KillTimer_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("KillTimer");
    return 0;
}
#endif

#ifdef STUB_LCMapStringA
uint32_t CCALL LCMapStringA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    GAME_STUBLOG("LCMapStringA");
    return 0;
}
#endif

#ifdef STUB_LCMapStringW
uint32_t CCALL LCMapStringW_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    GAME_STUBLOG("LCMapStringW");
    return 0;
}
#endif

#ifdef STUB_LoadCursorA
uint32_t CCALL LoadCursorA_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("LoadCursorA");
    return 0;
}
#endif

#ifdef STUB_LoadIconA
uint32_t CCALL LoadIconA_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("LoadIconA");
    return 0;
}
#endif

#ifdef STUB_LoadLibraryA
uint32_t CCALL LoadLibraryA_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("LoadLibraryA");
    return 0;
}
#endif

#ifdef STUB_LoadResource
uint32_t CCALL LoadResource_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    GAME_STUBLOG("LoadResource");
    return 0;
}
#endif

#ifdef STUB_LocalAlloc
uint32_t CCALL LocalAlloc_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("LocalAlloc");
    return 0;
}
#endif

#ifdef STUB_LocalFree
uint32_t CCALL LocalFree_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("LocalFree");
    return 0;
}
#endif

#ifdef STUB_LockResource
uint32_t CCALL LockResource_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("LockResource");
    return 0;
}
#endif

#ifdef STUB_MapViewOfFile
uint32_t CCALL MapViewOfFile_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    GAME_STUBLOG("MapViewOfFile");
    return 0;
}
#endif

#ifdef STUB_MessageBeep
uint32_t CCALL MessageBeep_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("MessageBeep");
    return 0;
}
#endif

#ifdef STUB_MessageBoxA
uint32_t CCALL MessageBoxA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("MessageBoxA");
    return 0;
}
#endif

#ifdef STUB_MoveFileA
uint32_t CCALL MoveFileA_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("MoveFileA");
    return 0;
}
#endif

#ifdef STUB_MultiByteToWideChar
uint32_t CCALL MultiByteToWideChar_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    GAME_STUBLOG("MultiByteToWideChar");
    return 0;
}
#endif

#ifdef STUB_OpenFileMappingA
uint32_t CCALL OpenFileMappingA_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("OpenFileMappingA");
    return 0;
}
#endif

#ifdef STUB_OutputDebugStringA
uint32_t CCALL OutputDebugStringA_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("OutputDebugStringA");
    return 0;
}
#endif

#ifdef STUB_PeekMessageA
uint32_t CCALL PeekMessageA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    GAME_STUBLOG("PeekMessageA");
    return 0;
}
#endif

#ifdef STUB_PeekNamedPipe
uint32_t CCALL PeekNamedPipe_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    GAME_STUBLOG("PeekNamedPipe");
    return 0;
}
#endif

#ifdef STUB_PostQuitMessage
uint32_t CCALL PostQuitMessage_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("PostQuitMessage");
    return 0;
}
#endif

#ifdef STUB_RaiseException
uint32_t CCALL RaiseException_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("RaiseException");
    return 0;
}
#endif

#ifdef STUB_ReadFile
uint32_t CCALL ReadFile_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    GAME_STUBLOG("ReadFile");
    return 0;
}
#endif

#ifdef STUB_ReadProcessMemory
uint32_t CCALL ReadProcessMemory_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    GAME_STUBLOG("ReadProcessMemory");
    return 0;
}
#endif

#ifdef STUB_RegCloseKey
uint32_t CCALL RegCloseKey_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("RegCloseKey");
    return 0;
}
#endif

#ifdef STUB_RegCreateKeyA
uint32_t CCALL RegCreateKeyA_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("RegCreateKeyA");
    return 0;
}
#endif

#ifdef STUB_RegDeleteKeyA
uint32_t CCALL RegDeleteKeyA_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("RegDeleteKeyA");
    return 0;
}
#endif

#ifdef STUB_RegEnumKeyA
uint32_t CCALL RegEnumKeyA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("RegEnumKeyA");
    return 0;
}
#endif

#ifdef STUB_RegFlushKey
uint32_t CCALL RegFlushKey_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("RegFlushKey");
    return 0;
}
#endif

#ifdef STUB_RegOpenKeyExA
uint32_t CCALL RegOpenKeyExA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    GAME_STUBLOG("RegOpenKeyExA");
    return 0;
}
#endif

#ifdef STUB_RegQueryValueExA
uint32_t CCALL RegQueryValueExA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    GAME_STUBLOG("RegQueryValueExA");
    return 0;
}
#endif

#ifdef STUB_RegSetValueExA
uint32_t CCALL RegSetValueExA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    GAME_STUBLOG("RegSetValueExA");
    return 0;
}
#endif

#ifdef STUB_RegisterClassA
uint32_t CCALL RegisterClassA_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("RegisterClassA");
    return 0;
}
#endif

#ifdef STUB_RegisterWindowMessageA
uint32_t CCALL RegisterWindowMessageA_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("RegisterWindowMessageA");
    return 0;
}
#endif

#ifdef STUB_ReleaseDC
uint32_t CCALL ReleaseDC_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("ReleaseDC");
    return 0;
}
#endif

#ifdef STUB_ReleaseSemaphore
uint32_t CCALL ReleaseSemaphore_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("ReleaseSemaphore");
    return 0;
}
#endif

#ifdef STUB_ReplyMessage
uint32_t CCALL ReplyMessage_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("ReplyMessage");
    return 0;
}
#endif

#ifdef STUB_ResetEvent
uint32_t CCALL ResetEvent_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("ResetEvent");
    return 0;
}
#endif

#ifdef STUB_ResumeThread
uint32_t CCALL ResumeThread_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("ResumeThread");
    return 0;
}
#endif

#ifdef STUB_RtlUnwind
uint32_t CCALL RtlUnwind_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("RtlUnwind");
    return 0;
}
#endif

#ifdef STUB_ScreenToClient
uint32_t CCALL ScreenToClient_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("ScreenToClient");
    return 0;
}
#endif

#ifdef STUB_SendDlgItemMessageA
uint32_t CCALL SendDlgItemMessageA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    GAME_STUBLOG("SendDlgItemMessageA");
    return 0;
}
#endif

#ifdef STUB_SendMessageA
uint32_t CCALL SendMessageA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("SendMessageA");
    return 0;
}
#endif

#ifdef STUB_SetConsoleCtrlHandler
uint32_t CCALL SetConsoleCtrlHandler_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("SetConsoleCtrlHandler");
    return 0;
}
#endif

#ifdef STUB_SetConsoleTitleA
uint32_t CCALL SetConsoleTitleA_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("SetConsoleTitleA");
    return 0;
}
#endif

#ifdef STUB_SetCurrentDirectoryA
uint32_t CCALL SetCurrentDirectoryA_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("SetCurrentDirectoryA");
    return 0;
}
#endif

#ifdef STUB_SetCursorPos
uint32_t CCALL SetCursorPos_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("SetCursorPos");
    return 0;
}
#endif

#ifdef STUB_SetEndOfFile
uint32_t CCALL SetEndOfFile_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("SetEndOfFile");
    return 0;
}
#endif

#ifdef STUB_SetEnvironmentVariableA
uint32_t CCALL SetEnvironmentVariableA_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("SetEnvironmentVariableA");
    return 0;
}
#endif

#ifdef STUB_SetEvent
uint32_t CCALL SetEvent_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("SetEvent");
    return 0;
}
#endif

#ifdef STUB_SetFilePointer
uint32_t CCALL SetFilePointer_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("SetFilePointer");
    return 0;
}
#endif

#ifdef STUB_SetForegroundWindow
uint32_t CCALL SetForegroundWindow_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("SetForegroundWindow");
    return 0;
}
#endif

#ifdef STUB_SetHandleCount
uint32_t CCALL SetHandleCount_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("SetHandleCount");
    return 0;
}
#endif

#ifdef STUB_SetPriorityClass
uint32_t CCALL SetPriorityClass_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("SetPriorityClass");
    return 0;
}
#endif

#ifdef STUB_SetRect
uint32_t CCALL SetRect_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    GAME_STUBLOG("SetRect");
    return 0;
}
#endif

#ifdef STUB_SetStdHandle
uint32_t CCALL SetStdHandle_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("SetStdHandle");
    return 0;
}
#endif

#ifdef STUB_SetThreadPriority
uint32_t CCALL SetThreadPriority_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("SetThreadPriority");
    return 0;
}
#endif

#ifdef STUB_SetTimer
uint32_t CCALL SetTimer_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("SetTimer");
    return 0;
}
#endif

#ifdef STUB_SetUnhandledExceptionFilter
uint32_t CCALL SetUnhandledExceptionFilter_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("SetUnhandledExceptionFilter");
    return 0;
}
#endif

#ifdef STUB_SetWindowLongA
uint32_t CCALL SetWindowLongA_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("SetWindowLongA");
    return 0;
}
#endif

#ifdef STUB_SetWindowPos
uint32_t CCALL SetWindowPos_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    GAME_STUBLOG("SetWindowPos");
    return 0;
}
#endif

#ifdef STUB_SetWindowTextA
uint32_t CCALL SetWindowTextA_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("SetWindowTextA");
    return 0;
}
#endif

#ifdef STUB_ShowCursor
uint32_t CCALL ShowCursor_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("ShowCursor");
    return 0;
}
#endif

#ifdef STUB_ShowWindow
uint32_t CCALL ShowWindow_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("ShowWindow");
    return 0;
}
#endif

#ifdef STUB_Sleep
uint32_t CCALL Sleep_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("Sleep");
    return 0;
}
#endif

#ifdef STUB_SuspendThread
uint32_t CCALL SuspendThread_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("SuspendThread");
    return 0;
}
#endif

#ifdef STUB_SystemParametersInfoA
uint32_t CCALL SystemParametersInfoA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("SystemParametersInfoA");
    return 0;
}
#endif

#ifdef STUB_TerminateProcess
uint32_t CCALL TerminateProcess_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("TerminateProcess");
    return 0;
}
#endif

#ifdef STUB_TranslateAcceleratorA
uint32_t CCALL TranslateAcceleratorA_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("TranslateAcceleratorA");
    return 0;
}
#endif

#ifdef STUB_TranslateMessage
uint32_t CCALL TranslateMessage_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("TranslateMessage");
    return 0;
}
#endif

#ifdef STUB_UnhandledExceptionFilter
uint32_t CCALL UnhandledExceptionFilter_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("UnhandledExceptionFilter");
    return 0;
}
#endif

#ifdef STUB_UnmapViewOfFile
uint32_t CCALL UnmapViewOfFile_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("UnmapViewOfFile");
    return 0;
}
#endif

#ifdef STUB_UpdateWindow
uint32_t CCALL UpdateWindow_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("UpdateWindow");
    return 0;
}
#endif

#ifdef STUB_ValidateRgn
uint32_t CCALL ValidateRgn_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("ValidateRgn");
    return 0;
}
#endif

#ifdef STUB_VirtualAlloc
uint32_t CCALL VirtualAlloc_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("VirtualAlloc");
    return 0;
}
#endif

#ifdef STUB_VirtualFree
uint32_t CCALL VirtualFree_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("VirtualFree");
    return 0;
}
#endif

#ifdef STUB_WaitForInputIdle
uint32_t CCALL WaitForInputIdle_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("WaitForInputIdle");
    return 0;
}
#endif

#ifdef STUB_WaitForSingleObject
uint32_t CCALL WaitForSingleObject_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("WaitForSingleObject");
    return 0;
}
#endif

#ifdef STUB_WideCharToMultiByte
uint32_t CCALL WideCharToMultiByte_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7;
    GAME_STUBLOG("WideCharToMultiByte");
    return 0;
}
#endif

#ifdef STUB_WinExec
uint32_t CCALL WinExec_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("WinExec");
    return 0;
}
#endif

#ifdef STUB_WriteConsoleA
uint32_t CCALL WriteConsoleA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    GAME_STUBLOG("WriteConsoleA");
    return 0;
}
#endif

#ifdef STUB_WriteFile
uint32_t CCALL WriteFile_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    GAME_STUBLOG("WriteFile");
    return 0;
}
#endif

#ifdef STUB_auxGetDevCapsA
uint32_t CCALL auxGetDevCapsA_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("auxGetDevCapsA");
    return 0;
}
#endif

#ifdef STUB_auxGetNumDevs
uint32_t CCALL auxGetNumDevs_c(void)
{
    GAME_STUBLOG("auxGetNumDevs");
    return 0;
}
#endif

#ifdef STUB_auxGetVolume
uint32_t CCALL auxGetVolume_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("auxGetVolume");
    return 0;
}
#endif

#ifdef STUB_auxSetVolume
uint32_t CCALL auxSetVolume_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("auxSetVolume");
    return 0;
}
#endif

#ifdef STUB_joyConfigChanged
uint32_t CCALL joyConfigChanged_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("joyConfigChanged");
    return 0;
}
#endif

#ifdef STUB_joyGetDevCapsA
uint32_t CCALL joyGetDevCapsA_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("joyGetDevCapsA");
    return 0;
}
#endif

#ifdef STUB_joyGetNumDevs
uint32_t CCALL joyGetNumDevs_c(void)
{
    GAME_STUBLOG("joyGetNumDevs");
    return 0;
}
#endif

#ifdef STUB_joyGetPosEx
uint32_t CCALL joyGetPosEx_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("joyGetPosEx");
    return 0;
}
#endif

#ifdef STUB_lstrcpyA
uint32_t CCALL lstrcpyA_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("lstrcpyA");
    return 0;
}
#endif

#ifdef STUB_lstrlenA
uint32_t CCALL lstrlenA_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("lstrlenA");
    return 0;
}
#endif

#ifdef STUB_mciGetErrorStringA
uint32_t CCALL mciGetErrorStringA_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("mciGetErrorStringA");
    return 0;
}
#endif

#ifdef STUB_mciSendCommandA
uint32_t CCALL mciSendCommandA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("mciSendCommandA");
    return 0;
}
#endif

#ifdef STUB_mciSendStringA
uint32_t CCALL mciSendStringA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    GAME_STUBLOG("mciSendStringA");
    return 0;
}
#endif

#ifdef STUB_midiOutPrepareHeader
uint32_t CCALL midiOutPrepareHeader_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("midiOutPrepareHeader");
    return 0;
}
#endif

#ifdef STUB_midiOutReset
uint32_t CCALL midiOutReset_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("midiOutReset");
    return 0;
}
#endif

#ifdef STUB_midiOutShortMsg
uint32_t CCALL midiOutShortMsg_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("midiOutShortMsg");
    return 0;
}
#endif

#ifdef STUB_midiOutUnprepareHeader
uint32_t CCALL midiOutUnprepareHeader_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("midiOutUnprepareHeader");
    return 0;
}
#endif

#ifdef STUB_midiStreamClose
uint32_t CCALL midiStreamClose_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("midiStreamClose");
    return 0;
}
#endif

#ifdef STUB_midiStreamOpen
uint32_t CCALL midiStreamOpen_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    GAME_STUBLOG("midiStreamOpen");
    return 0;
}
#endif

#ifdef STUB_midiStreamOut
uint32_t CCALL midiStreamOut_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("midiStreamOut");
    return 0;
}
#endif

#ifdef STUB_midiStreamPause
uint32_t CCALL midiStreamPause_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("midiStreamPause");
    return 0;
}
#endif

#ifdef STUB_midiStreamPosition
uint32_t CCALL midiStreamPosition_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("midiStreamPosition");
    return 0;
}
#endif

#ifdef STUB_midiStreamProperty
uint32_t CCALL midiStreamProperty_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("midiStreamProperty");
    return 0;
}
#endif

#ifdef STUB_midiStreamRestart
uint32_t CCALL midiStreamRestart_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("midiStreamRestart");
    return 0;
}
#endif

#ifdef STUB_midiStreamStop
uint32_t CCALL midiStreamStop_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("midiStreamStop");
    return 0;
}
#endif

#ifdef STUB_ws2_32_WSACleanup
uint32_t CCALL ws2_32_WSACleanup_c(void)
{
    GAME_STUBLOG("ws2_32_WSACleanup");
    return 0;
}
#endif

#ifdef STUB_ws2_32_WSAStartup
uint32_t CCALL ws2_32_WSAStartup_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("ws2_32_WSAStartup");
    return 0;
}
#endif

#ifdef STUB_ws2_32_bind
uint32_t CCALL ws2_32_bind_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("ws2_32_bind");
    return 0;
}
#endif

#ifdef STUB_ws2_32_closesocket
uint32_t CCALL ws2_32_closesocket_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("ws2_32_closesocket");
    return 0;
}
#endif

#ifdef STUB_ws2_32_gethostbyname
uint32_t CCALL ws2_32_gethostbyname_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("ws2_32_gethostbyname");
    return 0;
}
#endif

#ifdef STUB_ws2_32_gethostname
uint32_t CCALL ws2_32_gethostname_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    GAME_STUBLOG("ws2_32_gethostname");
    return 0;
}
#endif

#ifdef STUB_ws2_32_getsockname
uint32_t CCALL ws2_32_getsockname_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("ws2_32_getsockname");
    return 0;
}
#endif

#ifdef STUB_ws2_32_htonl
uint32_t CCALL ws2_32_htonl_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("ws2_32_htonl");
    return 0;
}
#endif

#ifdef STUB_ws2_32_htons
uint32_t CCALL ws2_32_htons_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("ws2_32_htons");
    return 0;
}
#endif

#ifdef STUB_ws2_32_inet_addr
uint32_t CCALL ws2_32_inet_addr_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("ws2_32_inet_addr");
    return 0;
}
#endif

#ifdef STUB_ws2_32_inet_ntoa
uint32_t CCALL ws2_32_inet_ntoa_c(uint32_t a0)
{
    (void)a0;
    GAME_STUBLOG("ws2_32_inet_ntoa");
    return 0;
}
#endif

#ifdef STUB_ws2_32_sendto
uint32_t CCALL ws2_32_sendto_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    GAME_STUBLOG("ws2_32_sendto");
    return 0;
}
#endif

#ifdef STUB_ws2_32_socket
uint32_t CCALL ws2_32_socket_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    GAME_STUBLOG("ws2_32_socket");
    return 0;
}
#endif

#ifdef __cplusplus
}
#endif
