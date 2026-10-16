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

/* Glide 2.x (glide2x.dll) of a Windows original: a software renderer into a
 * 16-bit frame buffer, shown in the window of the DirectDraw layer. Only what
 * the ports need so far; see Glide2x.c for what is left out. */

#if !defined(_GLIDE2X_H_INCLUDED_)
#define _GLIDE2X_H_INCLUDED_

#include "platform.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t CCALL grGlideInit_c(void);
uint32_t CCALL grGlideShutdown_c(void);
uint32_t CCALL grGlideGetState_c(void *state);
uint32_t CCALL grGlideSetState_c(void *state);
uint32_t CCALL grSstQueryHardware_c(void *hwconfig);
uint32_t CCALL grSstSelect_c(uint32_t which_sst);
uint32_t CCALL grSstWinOpen_c(uint32_t hWnd, uint32_t resolution, uint32_t refresh, uint32_t color_format, uint32_t origin, uint32_t nColBuffers, uint32_t nAuxBuffers);
uint32_t CCALL grSstWinClose_c(void);
uint32_t CCALL grSstIdle_c(void);
uint32_t CCALL grSstVRetraceOn_c(void);
uint32_t CCALL grBufferClear_c(uint32_t color, uint32_t alpha, uint32_t depth);
uint32_t CCALL grBufferSwap_c(uint32_t swap_interval);
uint32_t CCALL grRenderBuffer_c(uint32_t buffer);
uint32_t CCALL grClipWindow_c(uint32_t minx, uint32_t miny, uint32_t maxx, uint32_t maxy);
uint32_t CCALL grCullMode_c(uint32_t mode);
uint32_t CCALL grDepthBufferMode_c(uint32_t mode);
uint32_t CCALL grDepthBufferFunction_c(uint32_t function);
uint32_t CCALL grDepthMask_c(uint32_t mask);
uint32_t CCALL grConstantColorValue_c(uint32_t value);
uint32_t CCALL grChromakeyMode_c(uint32_t mode);
uint32_t CCALL grChromakeyValue_c(uint32_t value);
uint32_t CCALL grAlphaBlendFunction_c(uint32_t rgb_sf, uint32_t rgb_df, uint32_t alpha_sf, uint32_t alpha_df);
uint32_t CCALL grLfbLock_c(uint32_t type, uint32_t buffer, uint32_t writeMode, uint32_t origin, uint32_t pixelPipeline, void *info);
uint32_t CCALL grLfbUnlock_c(uint32_t type, uint32_t buffer);
uint32_t CCALL grTexDownloadTable_c(uint32_t tmu, uint32_t type, void *data);
uint32_t CCALL grAADrawPoint_c(void *p);
uint32_t CCALL grAADrawLine_c(void *v1, void *v2);

uint32_t CCALL guColorCombineFunction_c(uint32_t function);
uint32_t CCALL guAlphaSource_c(uint32_t mode);
uint32_t CCALL guTexCombineFunction_c(uint32_t tmu, uint32_t function);
uint32_t CCALL guTexMemReset_c(void);
uint32_t CCALL guTexAllocateMemory_c(uint32_t tmu, uint32_t odd_even_mask, uint32_t width, uint32_t height, uint32_t format, uint32_t mm_mode, uint32_t smallest_lod, uint32_t largest_lod, uint32_t aspect, uint32_t s_clamp_mode, uint32_t t_clamp_mode, uint32_t minfilter_mode, uint32_t magfilter_mode, uint32_t lod_bias, uint32_t trilinear);
uint32_t CCALL guTexDownloadMipMap_c(uint32_t mmid, void *src, void *nccTable);
uint32_t CCALL guTexSource_c(uint32_t mmid);
uint32_t CCALL guDrawTriangleWithClip_c(void *a, void *b, void *c);

#ifdef __cplusplus
}
#endif

#endif /* _GLIDE2X_H_INCLUDED_ */
