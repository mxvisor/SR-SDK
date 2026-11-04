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

/* What GetProcAddress hands out of glide2x.dll: a program (a driver DLL recompiled as a
 * module) that loads the library itself asks for "_grGlideInit@0"; the guest address
 * is that of the bridge of the function, which takes the stdcall arguments */

#include "Glide2x.h"
#include "Game_memory.h"

#include <stdint.h>
#include <string.h>

#define GLIDE_PROCS(X) \
    X(grAADrawLine) \
    X(grAADrawPoint) \
    X(grAADrawPolygon) \
    X(grAADrawPolygonVertexList) \
    X(grAADrawTriangle) \
    X(grAlphaBlendFunction) \
    X(grAlphaCombine) \
    X(grAlphaTestFunction) \
    X(grAlphaTestReferenceValue) \
    X(grBufferClear) \
    X(grBufferNumPending) \
    X(grBufferSwap) \
    X(grChromakeyMode) \
    X(grChromakeyValue) \
    X(grClipWindow) \
    X(grColorCombine) \
    X(grConstantColorValue) \
    X(grCullMode) \
    X(grDepthBiasLevel) \
    X(grDepthBufferFunction) \
    X(grDepthBufferMode) \
    X(grDepthMask) \
    X(grDitherMode) \
    X(grDrawLine) \
    X(grDrawPlanarPolygon) \
    X(grDrawPoint) \
    X(grDrawPolygon) \
    X(grDrawPolygonVertexList) \
    X(grDrawTriangle) \
    X(grFogColorValue) \
    X(grFogMode) \
    X(grFogTable) \
    X(grGammaCorrectionValue) \
    X(grGlideGetState) \
    X(grGlideGetVersion) \
    X(grGlideInit) \
    X(grGlideSetState) \
    X(grGlideShutdown) \
    X(grHints) \
    X(grLfbConstantAlpha) \
    X(grLfbConstantDepth) \
    X(grLfbLock) \
    X(grLfbReadRegion) \
    X(grLfbUnlock) \
    X(grLfbWriteRegion) \
    X(grRenderBuffer) \
    X(grSstControl) \
    X(grSstIdle) \
    X(grSstIsBusy) \
    X(grSstQueryBoards) \
    X(grSstQueryHardware) \
    X(grSstScreenHeight) \
    X(grSstScreenWidth) \
    X(grSstSelect) \
    X(grSstStatus) \
    X(grSstVRetraceOn) \
    X(grSstWinClose) \
    X(grSstWinOpen) \
    X(grTexCalcMemRequired) \
    X(grTexClampMode) \
    X(grTexCombine) \
    X(grTexCombineFunction) \
    X(grTexDownloadMipMap) \
    X(grTexDownloadTable) \
    X(grTexFilterMode) \
    X(grTexLodBiasValue) \
    X(grTexMaxAddress) \
    X(grTexMinAddress) \
    X(grTexMipMapMode) \
    X(grTexSource) \
    X(guAlphaSource) \
    X(guColorCombineFunction) \
    X(guDrawTriangleWithClip) \
    X(guFogGenerateExp) \
    X(guTexAllocateMemory) \
    X(guTexCombineFunction) \
    X(guTexDownloadMipMap) \
    X(guTexMemReset) \
    X(guTexSource)

#ifdef __cplusplus
extern "C" {
#endif

#define GLIDE_PROC_EXTERN(name) extern void name##_asm2c(void);
GLIDE_PROCS(GLIDE_PROC_EXTERN)

struct glide_proc { const char *name; void (*proc)(void); };
#define GLIDE_PROC_ENTRY(name) { #name, name##_asm2c },
static const struct glide_proc glide_procs[] = { GLIDE_PROCS(GLIDE_PROC_ENTRY) };

/* the guest address of a function of glide2x.dll by its name, decorated ("_grSstIdle@0") or
 * not, or 0 */
uint32_t Glide2x_ProcLookup(const char *name)
{
    size_t length;

    if (name[0] == '_') name++;
    length = strcspn(name, "@");
    for (unsigned int i = 0; i < sizeof(glide_procs) / sizeof(glide_procs[0]); i++)
    {
        if (strlen(glide_procs[i].name) == length && strncmp(name, glide_procs[i].name, length) == 0)
        {
            return GAME_PTR_TO_GUEST32((void *)glide_procs[i].proc);
        }
    }
    return 0;
}

#ifdef __cplusplus
}
#endif
