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

#if !defined(_WINAPI_KERNEL32_H_INCLUDED_)
#define _WINAPI_KERNEL32_H_INCLUDED_

#include "ptr32.h"
#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t CCALL GetCurrentDirectoryA_c(uint32_t nBufferLength, char *lpBuffer);
uint32_t CCALL SetCurrentDirectoryA_c(const char *lpPathName);
uint32_t CCALL Beep_c(uint32_t dwFreq, uint32_t dwDuration);
uint32_t CCALL CloseHandle_c(void *hObject);
uint32_t CCALL CreateDirectoryA_c(const char *lpPathName, void *lpSecurityAttributes);
void * CCALL CreateFileA_c(const char *lpFileName, uint32_t dwDesiredAccess, uint32_t dwShareMode, void *lpSecurityAttributes, uint32_t dwCreationDistribution, uint32_t dwFlagsAndAttributes, void *hTemplateFile);
void * CCALL CreateMutexA_c(void *lpMutexAttributes, uint32_t bInitialOwner, const char *lpName);
uint32_t CCALL CreatePipe_c(PTR32(void) *hReadPipe, PTR32(void) *hWritePipe, void *lpPipeAttributes, uint32_t nSize);
void CCALL DeleteCriticalSection_c(void *lpCriticalSection);
uint32_t CCALL DeleteFileA_c(const char *lpFileName);
void CCALL EnterCriticalSection_c(void *lpCriticalSection);
void CCALL ExitProcess_c(uint32_t uExitCode);
uint32_t CCALL FileTimeToLocalFileTime_c(const void *lpFileTime, void *lpLocalFileTime);
uint32_t CCALL FileTimeToSystemTime_c(const void *lpFileTime, void *lpSystemTime);
uint32_t CCALL FindClose_c(void *hFindFile);
void * CCALL FindFirstFileA_c(const char *lpFileName, void *lpFindFileData);
uint32_t CCALL FindNextFileA_c(void *hFindFile, void *lpFindFileData);
void * CCALL GetCurrentProcess_c(void);
void * CCALL GetCurrentThread_c(void);
uint32_t CCALL GetCurrentThreadId_c(void);
uint32_t CCALL GetFullPathNameA_c(const char *lpFileName, uint32_t nBufferLength, char *lpBuffer, PTR32(char) *lpFilePart);
uint32_t CCALL GetLastError_c(void);
uint32_t CCALL GetPrivateProfileStringA_c(const char *lpAppName, const char *lpKeyName, const char *lpDefault, char *lpReturnedString, uint32_t nSize, const char *lpFileName);
void CCALL InitializeCriticalSection_c(void *lpCriticalSection);
void CCALL LeaveCriticalSection_c(void *lpCriticalSection);
uint32_t CCALL QueryPerformanceCounter_c(void *lpPerformanceCount);
uint32_t CCALL QueryPerformanceFrequency_c(void *lpFrequency);
uint32_t CCALL ReadFile_c(void *hFile, void *lpBuffer, uint32_t nNumberOfBytesToRead, uint32_t *lpNumberOfBytesRead, void *lpOverlapped);
uint32_t CCALL SetErrorMode_c(uint32_t uMode);
uint32_t CCALL SetFilePointer_c(void *hFile, uint32_t lDistanceToMove, uint32_t *lpDistanceToMoveHigh, uint32_t dwMoveMethod);
uint32_t CCALL SetPriorityClass_c(void *hProcess, uint32_t fdwPriority);
uint32_t CCALL SetThreadPriority_c(void *hThread, int32_t nPriority);
void CCALL Sleep_c(uint32_t cMilliseconds);
uint32_t CCALL SleepEx_c(uint32_t dwMilliseconds, uint32_t bAlertable);
uint32_t CCALL TlsAlloc_c(void);
uint32_t CCALL TlsFree_c(uint32_t dwTlsIndex);
uint32_t CCALL TlsGetValue_c(uint32_t dwTlsIndex);
uint32_t CCALL TlsSetValue_c(uint32_t dwTlsIndex, uint32_t lpTlsValue);
uint32_t CCALL WriteFile_c(void *hFile, const void *lpBuffer, uint32_t nNumberOfBytesToWrite, uint32_t *lpNumberOfBytesWritten, void *lpOverlapped);
unsigned int CCALL Winapi_GetTicks(void);
/* the threads of the game that have not ended */
int Winapi_ThreadsRunning(void);
/* the letter of the CD drive (the disc, an image or a copy): D, or E when the port is on D: */
char Winapi_CdDriveLetter(void);
/* the drives: C: and the CD drive (GAME_CD_VOLUME_LABEL, GAME_CD_VOLUME_SERIAL) */
uint32_t CCALL GetDriveTypeA_c(const char *lpRootPathName);
uint32_t CCALL GetLogicalDrives_c(void);
uint32_t CCALL GetLogicalDriveStringsA_c(uint32_t nBufferLength, char *lpBuffer);
uint32_t CCALL GetVolumeInformationA_c(const char *lpRootPathName, char *lpVolumeNameBuffer, uint32_t nVolumeNameSize, uint32_t *lpVolumeSerialNumber, uint32_t *lpMaximumComponentLength, uint32_t *lpFileSystemFlags, char *lpFileSystemNameBuffer, uint32_t nFileSystemNameSize);
uint32_t CCALL GetDiskFreeSpaceA_c(const char *lpRootPathName, uint32_t *lpSectorsPerCluster, uint32_t *lpBytesPerSector, uint32_t *lpNumberOfFreeClusters, uint32_t *lpTotalNumberOfClusters);
/* the files: the size, the times, the mappings (a copy of the file in guest memory) and the
 * HFILE functions */
uint32_t CCALL GetFileSize_c(void *hFile, uint32_t *lpFileSizeHigh);
uint32_t CCALL GetFileTime_c(void *hFile, void *lpCreationTime, void *lpLastAccessTime, void *lpLastWriteTime);
void * CCALL CreateFileMappingA_c(void *hFile, void *lpFileMappingAttributes, uint32_t flProtect, uint32_t dwMaximumSizeHigh, uint32_t dwMaximumSizeLow, const char *lpName);
void * CCALL MapViewOfFile_c(void *hFileMappingObject, uint32_t dwDesiredAccess, uint32_t dwFileOffsetHigh, uint32_t dwFileOffsetLow, uint32_t dwNumberOfBytesToMap);
uint32_t CCALL UnmapViewOfFile_c(const void *lpBaseAddress);
void * CCALL _lopen_c(const char *lpPathName, uint32_t iReadWrite);
uint32_t CCALL _lclose_c(void *hFile);
/* the start of Winapi_GetTicks (Linux) */
void Winapi_InitTicks(void);

#ifdef __cplusplus
}
#endif

#endif

