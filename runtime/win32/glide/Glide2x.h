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

uint32_t Glide2x_ProcLookup(const char *name);

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
uint32_t CCALL grAADrawPolygon_c(uint32_t count, void *vertices, void *ilist);
uint32_t CCALL grAADrawPolygonVertexList_c(uint32_t count, void *vertices);

/* the drawing primitives */
uint32_t CCALL grDrawTriangle_c(void *a, void *b, void *c);
uint32_t CCALL grDrawLine_c(void *v1, void *v2);
uint32_t CCALL grDrawPoint_c(void *p);

/* polygons */
uint32_t CCALL grDrawPolygon_c(uint32_t count, void *vertices, uint32_t flags);
uint32_t CCALL grDrawPlanarPolygon_c(uint32_t count, void *vertices, uint32_t flags);
uint32_t CCALL grDrawPolygonVertexList_c(uint32_t count, void *vertices);

/* colour and alpha combine */
uint32_t CCALL grColorCombine_c(uint32_t function, uint32_t factor, uint32_t local, uint32_t other, uint32_t invert);
uint32_t CCALL grAlphaCombine_c(uint32_t function, uint32_t factor, uint32_t local, uint32_t other, uint32_t invert);

/* fog */
uint32_t CCALL grFogMode_c(uint32_t mode);
uint32_t CCALL grFogColorValue_c(uint32_t value);
uint32_t CCALL grFogTable_c(void *table);
uint32_t CCALL guFogGenerateExp_c(void *table, uint32_t mode);

/* gamma */
uint32_t CCALL grGammaCorrectionValue_c(uint32_t value);

/* LFB constant alpha and depth */
uint32_t CCALL grLfbConstantAlpha_c(uint32_t value);
uint32_t CCALL grLfbConstantDepth_c(uint32_t value);

/* texture source and download */
uint32_t CCALL grTexSource_c(uint32_t tmu, uint32_t startAddress, uint32_t evenOdd, void *info);
uint32_t CCALL grTexDownloadMipMap_c(uint32_t tmu, uint32_t startAddress, uint32_t evenOdd, void *info);
uint32_t CCALL grTexCalcMemRequired_c(uint32_t smallest_lod, uint32_t largest_lod, uint32_t aspect, uint32_t format);

/* texture filter and clamp */
uint32_t CCALL grTexFilterMode_c(uint32_t tmu, uint32_t minFilter, uint32_t magFilter);
uint32_t CCALL grTexClampMode_c(uint32_t tmu, uint32_t sClamp, uint32_t tClamp);
uint32_t CCALL grTexMinAddress_c(uint32_t address);
uint32_t CCALL grTexMaxAddress_c(uint32_t address);

/* texture combine */
uint32_t CCALL grTexCombine_c(uint32_t tmu, uint32_t rgb_function, uint32_t rgb_factor, uint32_t alpha_function, uint32_t alpha_factor, uint32_t rgb_invert, uint32_t alpha_invert);
uint32_t CCALL grTexCombineFunction_c(uint32_t tmu, uint32_t function);

uint32_t CCALL guColorCombineFunction_c(uint32_t function);
uint32_t CCALL guAlphaSource_c(uint32_t mode);
uint32_t CCALL guTexCombineFunction_c(uint32_t tmu, uint32_t function);
uint32_t CCALL guTexMemReset_c(void);
uint32_t CCALL guTexAllocateMemory_c(uint32_t tmu, uint32_t odd_even_mask, uint32_t width, uint32_t height, uint32_t format, uint32_t mm_mode, uint32_t smallest_lod, uint32_t largest_lod, uint32_t aspect, uint32_t s_clamp_mode, uint32_t t_clamp_mode, uint32_t minfilter_mode, uint32_t magfilter_mode, uint32_t lod_bias, uint32_t trilinear);
uint32_t CCALL guTexDownloadMipMap_c(uint32_t mmid, void *src, void *nccTable);
uint32_t CCALL guTexSource_c(uint32_t mmid);
uint32_t CCALL guDrawTriangleWithClip_c(void *a, void *b, void *c);
uint32_t CCALL grAADrawTriangle_c(void *a, void *b, void *c, uint32_t ab_antialias, uint32_t bc_antialias, uint32_t ca_antialias);
uint32_t CCALL grAlphaTestFunction_c(uint32_t function);
uint32_t CCALL grAlphaTestReferenceValue_c(uint32_t value);
uint32_t CCALL grBufferNumPending_c(void);
uint32_t CCALL grDepthBiasLevel_c(uint32_t level);
uint32_t CCALL grDitherMode_c(uint32_t mode);
uint32_t CCALL grGlideGetVersion_c(void *version);
uint32_t CCALL grHints_c(uint32_t hintType, uint32_t hintMask);
uint32_t CCALL grLfbReadRegion_c(uint32_t src_buffer, uint32_t src_x, uint32_t src_y, uint32_t src_width, uint32_t src_height, uint32_t dst_stride, void *dst_data);
uint32_t CCALL grLfbWriteRegion_c(uint32_t dst_buffer, uint32_t dst_x, uint32_t dst_y, uint32_t src_format, uint32_t src_width, uint32_t src_height, uint32_t src_stride, void *src_data);
uint32_t CCALL grSstControl_c(uint32_t code);
uint32_t CCALL grSstIsBusy_c(void);
uint32_t CCALL grSstQueryBoards_c(void *hwconfig);
uint32_t CCALL grSstScreenHeight_c(void);
uint32_t CCALL grSstScreenWidth_c(void);
uint32_t CCALL grSstStatus_c(void);
uint32_t CCALL grTexLodBiasValue_c(uint32_t tmu, uint32_t bias);
uint32_t CCALL grTexMipMapMode_c(uint32_t tmu, uint32_t mode, uint32_t lodBlend);

/* the item "3D resolution" of the port's panel (GAME_GLIDE_3D_RESOLUTION_ITEM) */
void Glide_OsdItems(void);

#ifdef __cplusplus
}
#endif

#endif /* _GLIDE2X_H_INCLUDED_ */
