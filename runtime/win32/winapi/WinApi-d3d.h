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

#if !defined(_WINAPI_D3D_H_INCLUDED_)
#define _WINAPI_D3D_H_INCLUDED_

#include <stdint.h>
#include "platform.h"

struct IDirectDraw_c;
struct IDirectDrawSurface_c;

#ifdef __cplusplus
extern "C" {
#endif

/* Direct3D 2 (DirectX 5) on top of the DirectDraw objects of WinApi-ddraw.c: a game that
 * asks for it (GAME_DIRECT3D in Game_hooks.h) gets IDirect3D2 from IDirectDraw::QueryInterface. */

/* does the port offer Direct3D to the game? */
int Winapi_Direct3DEnabled(void);

/* the config file's 3D=direct3d|software|off; 0 if the value is unknown */
int Winapi_Direct3DSetMode(const char *mode);

/* the OpenGL window failed: go on with the software rasteriser */
void Winapi_Direct3DNoGL(void);

struct SDL_Window;

/* Direct3D on OpenGL (a d3d_gl=yes build): the window is an OpenGL one
 * without an SDL renderer and the device is drawn with the shared GL layer
 * of runtime/core/gl.  The game env D3D_GL=0 forces the SDL backend. */
int Winapi_Direct3DGL(void);

/* the GL presentation of the primary surface: convert, upload, scaled quad */
void Winapi_D3dPresentSurface(struct IDirectDrawSurface_c *prim, struct SDL_Window *window);
/* the GL context and the present texture go away (the window is going away) */
void Winapi_D3dPresentDeinit(void);

/* IDirectDraw::QueryInterface for IID_IDirect3D2: 1 and *out = the object, else 0 */
int Winapi_D3dQuery(struct IDirectDraw_c *dd, const void *riid, uint32_t *out);

/* IDirectDrawSurface::QueryInterface for IID_IDirect3DTexture(2) */
int Winapi_D3dSurfaceQuery(struct IDirectDrawSurface_c *surface, const void *riid, uint32_t *out);

/* a surface goes away: its texture object and handle */
void Winapi_D3dSurfaceFree(struct IDirectDrawSurface_c *surface);

/* generated with llasm/WinApi-d3d-asm.llasm */

uint32_t CCALL IDirect3D2_QueryInterface_c(void *lpThis, void *a1, void *a2);
uint32_t CCALL IDirect3D2_AddRef_c(void *lpThis);
uint32_t CCALL IDirect3D2_Release_c(void *lpThis);
uint32_t CCALL IDirect3D2_EnumDevices_c(void *lpThis, uint32_t a1, uint32_t a2);
uint32_t CCALL IDirect3D2_CreateLight_c(void *lpThis, void *a1, void *a2);
uint32_t CCALL IDirect3D2_CreateMaterial_c(void *lpThis, void *a1, void *a2);
uint32_t CCALL IDirect3D2_CreateViewport_c(void *lpThis, void *a1, void *a2);
uint32_t CCALL IDirect3D2_FindDevice_c(void *lpThis, void *a1, void *a2);
uint32_t CCALL IDirect3D2_CreateDevice_c(void *lpThis, void *a1, void *a2, void *a3);

uint32_t CCALL IDirect3DDevice2_QueryInterface_c(void *lpThis, void *a1, void *a2);
uint32_t CCALL IDirect3DDevice2_AddRef_c(void *lpThis);
uint32_t CCALL IDirect3DDevice2_Release_c(void *lpThis);
uint32_t CCALL IDirect3DDevice2_GetCaps_c(void *lpThis, void *a1, void *a2);
uint32_t CCALL IDirect3DDevice2_SwapTextureHandles_c(void *lpThis, void *a1, void *a2);
uint32_t CCALL IDirect3DDevice2_GetStats_c(void *lpThis, void *a1);
uint32_t CCALL IDirect3DDevice2_AddViewport_c(void *lpThis, void *a1);
uint32_t CCALL IDirect3DDevice2_DeleteViewport_c(void *lpThis, void *a1);
uint32_t CCALL IDirect3DDevice2_NextViewport_c(void *lpThis, void *a1, void *a2, uint32_t a3);
uint32_t CCALL IDirect3DDevice2_EnumTextureFormats_c(void *lpThis, uint32_t a1, uint32_t a2);
uint32_t CCALL IDirect3DDevice2_BeginScene_c(void *lpThis);
uint32_t CCALL IDirect3DDevice2_EndScene_c(void *lpThis);
uint32_t CCALL IDirect3DDevice2_GetDirect3D_c(void *lpThis, void *a1);
uint32_t CCALL IDirect3DDevice2_SetCurrentViewport_c(void *lpThis, void *a1);
uint32_t CCALL IDirect3DDevice2_GetCurrentViewport_c(void *lpThis, void *a1);
uint32_t CCALL IDirect3DDevice2_SetRenderTarget_c(void *lpThis, void *a1, uint32_t a2);
uint32_t CCALL IDirect3DDevice2_GetRenderTarget_c(void *lpThis, void *a1);
uint32_t CCALL IDirect3DDevice2_Begin_c(void *lpThis, uint32_t a1, uint32_t a2, uint32_t a3);
uint32_t CCALL IDirect3DDevice2_BeginIndexed_c(void *lpThis, uint32_t a1, uint32_t a2, void *a3, uint32_t a4, uint32_t a5);
uint32_t CCALL IDirect3DDevice2_Vertex_c(void *lpThis, void *a1);
uint32_t CCALL IDirect3DDevice2_Index_c(void *lpThis, uint32_t a1);
uint32_t CCALL IDirect3DDevice2_End_c(void *lpThis, uint32_t a1);
uint32_t CCALL IDirect3DDevice2_GetRenderState_c(void *lpThis, uint32_t a1, void *a2);
uint32_t CCALL IDirect3DDevice2_SetRenderState_c(void *lpThis, uint32_t a1, uint32_t a2);
uint32_t CCALL IDirect3DDevice2_GetLightState_c(void *lpThis, uint32_t a1, void *a2);
uint32_t CCALL IDirect3DDevice2_SetLightState_c(void *lpThis, uint32_t a1, uint32_t a2);
uint32_t CCALL IDirect3DDevice2_SetTransform_c(void *lpThis, uint32_t a1, void *a2);
uint32_t CCALL IDirect3DDevice2_GetTransform_c(void *lpThis, uint32_t a1, void *a2);
uint32_t CCALL IDirect3DDevice2_MultiplyTransform_c(void *lpThis, uint32_t a1, void *a2);
uint32_t CCALL IDirect3DDevice2_DrawPrimitive_c(void *lpThis, uint32_t a1, uint32_t a2, void *a3, uint32_t a4, uint32_t a5);
uint32_t CCALL IDirect3DDevice2_DrawIndexedPrimitive_c(void *lpThis, uint32_t a1, uint32_t a2, void *a3, uint32_t a4, void *a5, uint32_t a6, uint32_t a7);
uint32_t CCALL IDirect3DDevice2_SetClipStatus_c(void *lpThis, void *a1);
uint32_t CCALL IDirect3DDevice2_GetClipStatus_c(void *lpThis, void *a1);

uint32_t CCALL IDirect3DViewport2_QueryInterface_c(void *lpThis, void *a1, void *a2);
uint32_t CCALL IDirect3DViewport2_AddRef_c(void *lpThis);
uint32_t CCALL IDirect3DViewport2_Release_c(void *lpThis);
uint32_t CCALL IDirect3DViewport2_Initialize_c(void *lpThis, void *a1);
uint32_t CCALL IDirect3DViewport2_GetViewport_c(void *lpThis, void *a1);
uint32_t CCALL IDirect3DViewport2_SetViewport_c(void *lpThis, void *a1);
uint32_t CCALL IDirect3DViewport2_TransformVertices_c(void *lpThis, uint32_t a1, void *a2, uint32_t a3, void *a4);
uint32_t CCALL IDirect3DViewport2_LightElements_c(void *lpThis, uint32_t a1, void *a2);
uint32_t CCALL IDirect3DViewport2_SetBackground_c(void *lpThis, uint32_t a1);
uint32_t CCALL IDirect3DViewport2_GetBackground_c(void *lpThis, void *a1, void *a2);
uint32_t CCALL IDirect3DViewport2_SetBackgroundDepth_c(void *lpThis, void *a1);
uint32_t CCALL IDirect3DViewport2_GetBackgroundDepth_c(void *lpThis, void *a1, void *a2);
uint32_t CCALL IDirect3DViewport2_Clear_c(void *lpThis, uint32_t a1, void *a2, uint32_t a3);
uint32_t CCALL IDirect3DViewport2_AddLight_c(void *lpThis, void *a1);
uint32_t CCALL IDirect3DViewport2_DeleteLight_c(void *lpThis, void *a1);
uint32_t CCALL IDirect3DViewport2_NextLight_c(void *lpThis, void *a1, void *a2, uint32_t a3);
uint32_t CCALL IDirect3DViewport2_GetViewport2_c(void *lpThis, void *a1);
uint32_t CCALL IDirect3DViewport2_SetViewport2_c(void *lpThis, void *a1);

uint32_t CCALL IDirect3DMaterial2_QueryInterface_c(void *lpThis, void *a1, void *a2);
uint32_t CCALL IDirect3DMaterial2_AddRef_c(void *lpThis);
uint32_t CCALL IDirect3DMaterial2_Release_c(void *lpThis);
uint32_t CCALL IDirect3DMaterial2_SetMaterial_c(void *lpThis, void *a1);
uint32_t CCALL IDirect3DMaterial2_GetMaterial_c(void *lpThis, void *a1);
uint32_t CCALL IDirect3DMaterial2_GetHandle_c(void *lpThis, void *a1, void *a2);

uint32_t CCALL IDirect3DTexture2_QueryInterface_c(void *lpThis, void *a1, void *a2);
uint32_t CCALL IDirect3DTexture2_AddRef_c(void *lpThis);
uint32_t CCALL IDirect3DTexture2_Release_c(void *lpThis);
uint32_t CCALL IDirect3DTexture2_GetHandle_c(void *lpThis, void *a1, void *a2);
uint32_t CCALL IDirect3DTexture2_PaletteChanged_c(void *lpThis, uint32_t a1, uint32_t a2);
uint32_t CCALL IDirect3DTexture2_Load_c(void *lpThis, void *a1);

#ifdef __cplusplus
}
#endif

#endif
