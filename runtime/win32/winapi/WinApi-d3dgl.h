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

/* The OpenGL backend of IDirect3DDevice2 (a d3d_gl=yes build, runtime/win32/gl):
 * the pre-transformed primitives of WinApi-d3d.c drawn into an FBO the size of
 * the target surface, its colour the guest's surface memory.  The context, the
 * FBO and the texture objects live here; WinApi-d3d.c only routes the calls. */

#if !defined(_WINAPI_D3DGL_H_INCLUDED_)
#define _WINAPI_D3DGL_H_INCLUDED_

#include <stdint.h>

struct IDirectDrawSurface_c;
struct SDL_Window;

#ifdef __cplusplus
extern "C" {
#endif

/* a transformed vertex: the Direct3D screen space the game sends, y down; f is the
 * alpha of the specular colour, the vertex fog (255 no fog) */
typedef struct {
    float x, y, z, rhw, u, v;
    int r, g, b, a, sr, sg, sb, f;
} d3dgl_vert;

/* the context on the DirectDraw window; 0 and a message if it cannot be made */
int Winapi_D3dGLContext(struct SDL_Window *window);

/* BeginScene: the FBO of the target, its colour the target's memory */
int Winapi_D3dGLBeginScene(struct IDirectDrawSurface_c *target);
/* EndScene: the FBO's colour back into the target's memory */
void Winapi_D3dGLEndScene(struct IDirectDrawSurface_c *target);
/* the guest touches the target's pixels mid-scene: read the FBO back first */
void Winapi_D3dGLSync(struct IDirectDrawSurface_c *target);
/* the guest wrote the target mid-scene (Unlock, Blt): the new pixels under
 * the 3D drawn so far */
void Winapi_D3dGLUpload(struct IDirectDrawSurface_c *target);
/* a texture surface goes away: drop both of its cached GL textures */
void Winapi_D3dGLTextureFree(const struct IDirectDrawSurface_c *surface);
/* the entries of a palette changed: the cached textures that use it want
 * their pixels read again (the software rasteriser reads the palette live) */
void Winapi_D3dGLPaletteChanged(const struct IDirectDrawPalette_c *palette);

/* Clear inside a scene: the rect of the FBO, colour 0xAARRGGBB and/or depth */
void Winapi_D3dGLClear(struct IDirectDrawSurface_c *target,
                       int x1, int y1, int x2, int y2,
                       int do_color, uint32_t color, int do_z);

/* D3DRENDERSTATE: the backend mirrors the values it understands */
void Winapi_D3dGLSetState(uint32_t state, uint32_t value);

/* type: 2 line list, 3 line strip, 4 triangle list, 5 strip, 6 fan;
 * verts: nverts of them, index may be NULL (draw in order, count = nverts);
 * texture may be NULL; the clip rect is the viewport's (w == 0: the target) */
void Winapi_D3dGLDraw(uint32_t type, const d3dgl_vert *verts, uint32_t nverts,
                      const uint16_t *index, uint32_t count,
                      const struct IDirectDrawSurface_c *texture,
                      int clip_x, int clip_y, int clip_w, int clip_h);

/* the scaled image of the target's memory on the window (WinApi-d3d.c) */
void Winapi_D3dGLPresent(struct IDirectDrawSurface_c *target, struct SDL_Window *window);

/* the context and every object go away (the window is going away) */
void Winapi_D3dGLShutdown(void);

#ifdef __cplusplus
}
#endif

#endif
