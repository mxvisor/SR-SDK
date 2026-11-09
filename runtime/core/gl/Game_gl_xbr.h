/*

  xBR-lv2 and Super-xBR, the 2D filters "xbr" and "super-xbr" of Game_gl.c:
  Hyllian's shaders of libretro's glsl-shaders (xbr/shaders/xbr-lv2.glsl,
  xbr/shaders/super-xbr/super-xbr-pass0..2.glsl), as fragment shaders of
  Game_gl.c (GLSL 1.20, GLSL ES 1.00): the texel offsets are made in the
  fragment shader, the matrices of four colours are sums, only the default
  settings (xBR-lv2: corner C, no small details; Super-xBR: normal mode).
  Changes Copyright (C) 2026 mxvisor, under the same license.

  Copyright (C) 2011-2016 Hyllian - sergiogdb@gmail.com

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.

*/

#if !defined(_GAME_GL_XBR_H_INCLUDED_)
#define _GAME_GL_XBR_H_INCLUDED_

/* xBR-lv2 in one pass from the picture (u_tex, u_size texels, sampled nearest)
 * to the window: the edges are rounded off at 30, 45 and 60 degrees, the flat
 * areas stay sharp */
static const char filter_xbr_fs[] =
    "#define XBR_SCALE 3.0\n"
    "#define XBR_EQ_THRESHOLD 15.0\n"
    "#define lv2_cf 2.0\n"
    "const vec3 rgbw = vec3(14.352, 28.176, 5.472);\n"
    "const vec4 delta = vec4(1.0/XBR_SCALE, 1.0/XBR_SCALE, 1.0/XBR_SCALE, 1.0/XBR_SCALE);\n"
    "const vec4 delta_l = vec4(0.5/XBR_SCALE, 1.0/XBR_SCALE, 0.5/XBR_SCALE, 1.0/XBR_SCALE);\n"
    "const vec4 delta_u = vec4(1.0/XBR_SCALE, 0.5/XBR_SCALE, 1.0/XBR_SCALE, 0.5/XBR_SCALE);\n"
    "const vec4 Ao = vec4( 1.0, -1.0, -1.0, 1.0 );\n"
    "const vec4 Bo = vec4( 1.0,  1.0, -1.0,-1.0 );\n"
    "const vec4 Co = vec4( 1.5,  0.5, -0.5, 0.5 );\n"
    "const vec4 Ax = vec4( 1.0, -1.0, -1.0, 1.0 );\n"
    "const vec4 Bx = vec4( 0.5,  2.0, -0.5,-2.0 );\n"
    "const vec4 Cx = vec4( 1.0,  1.0, -0.5, 0.0 );\n"
    "const vec4 Ay = vec4( 1.0, -1.0, -1.0, 1.0 );\n"
    "const vec4 By = vec4( 2.0,  0.5, -2.0,-0.5 );\n"
    "const vec4 Cy = vec4( 2.0,  0.0, -1.0, 0.5 );\n"
    "const vec4 Ci = vec4(0.25, 0.25, 0.25, 0.25);\n"
    "vec4 df(vec4 A, vec4 B) { return abs(A - B); }\n"
    "vec4 diff(vec4 A, vec4 B) { return vec4(notEqual(A, B)); }\n"
    "vec4 eq(vec4 A, vec4 B) { return step(df(A, B), vec4(XBR_EQ_THRESHOLD)); }\n"
    "vec4 neq(vec4 A, vec4 B) { return vec4(1.0) - eq(A, B); }\n"
    "vec4 wd(vec4 a, vec4 b, vec4 c, vec4 d, vec4 e, vec4 f, vec4 g, vec4 h) {\n"
    "    return df(a,b) + df(a,c) + df(d,e) + df(d,f) + 4.0*df(g,h);\n"
    "}\n"
    "float c_df(vec3 c1, vec3 c2) { vec3 d = abs(c1 - c2); return d.r + d.g + d.b; }\n"
    "vec3 tx(vec2 c) { return texture2D(u_tex, c).rgb; }\n"
    "void main() {\n"
    "    float dx = 1.0 / u_size.x;\n"
    "    float dy = 1.0 / u_size.y;\n"
    "    vec2 tc = v_uv;\n"
    "    vec4 t1 = tc.xxxy + vec4( -dx, 0.0, dx,-2.0*dy);\n"
    "    vec4 t2 = tc.xxxy + vec4( -dx, 0.0, dx,    -dy);\n"
    "    vec4 t3 = tc.xxxy + vec4( -dx, 0.0, dx,    0.0);\n"
    "    vec4 t4 = tc.xxxy + vec4( -dx, 0.0, dx,     dy);\n"
    "    vec4 t5 = tc.xxxy + vec4( -dx, 0.0, dx, 2.0*dy);\n"
    "    vec4 t6 = tc.xyyy + vec4(-2.0*dx,-dy, 0.0,  dy);\n"
    "    vec4 t7 = tc.xyyy + vec4( 2.0*dx,-dy, 0.0,  dy);\n"
    "    vec2 fp = fract(vec2(tc.x * 1.00000001, tc.y) * u_size);\n"
    "    vec3 A1 = tx(t1.xw); vec3 B1 = tx(t1.yw); vec3 C1 = tx(t1.zw);\n"
    "    vec3 A  = tx(t2.xw); vec3 B  = tx(t2.yw); vec3 C  = tx(t2.zw);\n"
    "    vec3 D  = tx(t3.xw); vec3 E  = tx(t3.yw); vec3 F  = tx(t3.zw);\n"
    "    vec3 G  = tx(t4.xw); vec3 H  = tx(t4.yw); vec3 I  = tx(t4.zw);\n"
    "    vec3 G5 = tx(t5.xw); vec3 H5 = tx(t5.yw); vec3 I5 = tx(t5.zw);\n"
    "    vec3 A0 = tx(t6.xy); vec3 D0 = tx(t6.xz); vec3 G0 = tx(t6.xw);\n"
    "    vec3 C4 = tx(t7.xy); vec3 F4 = tx(t7.xz); vec3 I4 = tx(t7.xw);\n"
    "    vec4 b = vec4(dot(B,rgbw), dot(D,rgbw), dot(H,rgbw), dot(F,rgbw));\n"
    "    vec4 c = vec4(dot(C,rgbw), dot(A,rgbw), dot(G,rgbw), dot(I,rgbw));\n"
    "    vec4 d = b.yzwx;\n"
    "    vec4 e = vec4(dot(E,rgbw));\n"
    "    vec4 f = b.wxyz;\n"
    "    vec4 g = c.zwxy;\n"
    "    vec4 h = b.zwxy;\n"
    "    vec4 i = c.wxyz;\n"
    "    vec4 i4 = vec4(dot(I4,rgbw), dot(C1,rgbw), dot(A0,rgbw), dot(G5,rgbw));\n"
    "    vec4 i5 = vec4(dot(I5,rgbw), dot(C4,rgbw), dot(A1,rgbw), dot(G0,rgbw));\n"
    "    vec4 h5 = vec4(dot(H5,rgbw), dot(F4,rgbw), dot(B1,rgbw), dot(D0,rgbw));\n"
    "    vec4 f4 = h5.yzwx;\n"
    "    vec4 fx   = Ao*fp.y + Bo*fp.x;\n"
    "    vec4 fx_l = Ax*fp.y + Bx*fp.x;\n"
    "    vec4 fx_u = Ay*fp.y + By*fp.x;\n"
    "    vec4 irlv0 = diff(e,f) * diff(e,h);\n"
    "    vec4 irlv1 = irlv0 * (neq(f,b) * neq(f,c) + neq(h,d) * neq(h,g) + eq(e,i) * (neq(f,f4) * neq(f,i4) + neq(h,h5) * neq(h,i5)) + eq(e,g) + eq(e,c));\n"
    "    vec4 irlv2l = diff(e,g) * diff(d,g);\n"
    "    vec4 irlv2u = diff(e,c) * diff(b,c);\n"
    "    vec4 fx45i = clamp((fx   + delta   - Co - Ci) / (2.0*delta  ), 0.0, 1.0);\n"
    "    vec4 fx45  = clamp((fx   + delta   - Co     ) / (2.0*delta  ), 0.0, 1.0);\n"
    "    vec4 fx30  = clamp((fx_l + delta_l - Cx     ) / (2.0*delta_l), 0.0, 1.0);\n"
    "    vec4 fx60  = clamp((fx_u + delta_u - Cy     ) / (2.0*delta_u), 0.0, 1.0);\n"
    "    vec4 wd1 = wd(e, c,  g, i, h5, f4, h, f);\n"
    "    vec4 wd2 = wd(h, d, i5, f, i4,  b, e, i);\n"
    "    vec4 edri  = step(wd1, wd2) * irlv0;\n"
    "    vec4 edr   = step(wd1 + vec4(0.1), wd2) * step(vec4(0.5), irlv1);\n"
    "    vec4 edr_l = step(lv2_cf*df(f,g), df(h,c)) * irlv2l * edr;\n"
    "    vec4 edr_u = step(lv2_cf*df(h,c), df(f,g)) * irlv2u * edr;\n"
    "    fx45  = edr   * fx45;\n"
    "    fx30  = edr_l * fx30;\n"
    "    fx60  = edr_u * fx60;\n"
    "    fx45i = edri  * fx45i;\n"
    "    vec4 px = step(df(e,f), df(e,h));\n"
    "    vec4 maximos = max(max(fx30, fx60), max(fx45, fx45i));\n"
    "    vec3 res1 = E;\n"
    "    res1 = mix(res1, mix(H, F, px.x), maximos.x);\n"
    "    res1 = mix(res1, mix(B, D, px.z), maximos.z);\n"
    "    vec3 res2 = E;\n"
    "    res2 = mix(res2, mix(F, B, px.y), maximos.y);\n"
    "    res2 = mix(res2, mix(D, H, px.w), maximos.w);\n"
    "    gl_FragColor = vec4(mix(res1, res2, step(c_df(E, res1), c_df(E, res2))), 1.0);\n"
    "}\n";

/* Super-xBR: the part of the three passes; a pass defines WP1..WP6, WEIGHT1,
 * WEIGHT2 and fetches the 16 texels */
#define SUPER_XBR_CORE \
    "const vec3 Y = vec3(0.2126, 0.7152, 0.0722);\n" \
    "float df(float A, float B) { return abs(A - B); }\n" \
    "float d_wd(float b0, float b1, float c0, float c1, float c2, float d0, float d1, float d2, float d3, float e1, float e2, float e3, float f2, float f3) {\n" \
    "    return WP1*(df(c1,c2) + df(c1,c0) + df(e2,e1) + df(e2,e3)) + WP2*(df(d2,d3) + df(d0,d1)) + WP3*(df(d1,d3) + df(d0,d2)) + WP4*df(d1,d2) + WP5*(df(c0,c2) + df(e1,e3)) + WP6*(df(b0,b1) + df(f2,f3));\n" \
    "}\n" \
    "float hv_wd(float i1, float i2, float i3, float i4, float e1, float e2, float e3, float e4) {\n" \
    "    return WP4*(df(i1,i2) + df(i3,i4)) + WP1*(df(i1,e1) + df(i2,e2) + df(i3,e3) + df(i4,e4)) + WP3*(df(i1,e2) + df(i3,e4) + df(e1,i2) + df(e3,i4));\n" \
    "}\n" \
    "vec3 w4(vec4 w, vec3 a, vec3 b, vec3 c, vec3 d) { return w.x*a + w.y*b + w.z*c + w.w*d; }\n" \
    "vec3 super_xbr(vec3 P0, vec3 P1, vec3 P2, vec3 P3, vec3 B, vec3 C, vec3 D, vec3 E, vec3 F, vec3 G, vec3 H, vec3 I, vec3 F4, vec3 I4, vec3 H5, vec3 I5) {\n" \
    "    float b = dot(B, Y); float c = dot(C, Y); float d = dot(D, Y); float e = dot(E, Y);\n" \
    "    float f = dot(F, Y); float g = dot(G, Y); float h = dot(H, Y); float i = dot(I, Y);\n" \
    "    float i4 = dot(I4, Y); float p0 = dot(P0, Y); float i5 = dot(I5, Y); float p1 = dot(P1, Y);\n" \
    "    float h5 = dot(H5, Y); float p2 = dot(P2, Y); float f4 = dot(F4, Y); float p3 = dot(P3, Y);\n" \
    "    float d_edge = d_wd(d, b, g, e, c, p2, h, f, p1, h5, i, f4, i5, i4) - d_wd(c, f4, b, f, i4, p0, e, i, p3, d, h, i5, g, h5);\n" \
    "    float hv_edge = hv_wd(f, i, e, h, c, i5, b, h5) - hv_wd(e, f, h, i, d, f4, g, i4);\n" \
    "    float edge_strength = smoothstep(0.0, 0.6 + 0.000001, abs(d_edge));\n" \
    "    vec4 w1 = vec4(-WEIGHT1, WEIGHT1 + 0.5, WEIGHT1 + 0.5, -WEIGHT1);\n" \
    "    vec4 w2 = vec4(-WEIGHT2, WEIGHT2 + 0.25, WEIGHT2 + 0.25, -WEIGHT2);\n" \
    "    vec3 c3 = w4(w2, D+G, E+H, F+I, F4+I4);\n" \
    "    vec3 c4 = w4(w2, C+B, F+E, I+H, I5+H5);\n" \
    "    vec3 c1 = w4(w1, P2, H, F, P1);\n" \
    "    vec3 c2 = w4(w1, P0, E, I, P3);\n" \
    "    vec3 color = mix(mix(c1, c2, step(0.0, d_edge)), mix(c3, c4, step(0.0, hv_edge)), 1.0 - edge_strength);\n" \
    "    return clamp(color, min(E, min(F, min(H, I))), max(E, max(F, max(H, I))));\n" \
    "}\n" \
    "vec3 tx(vec2 c) { return texture2D(u_tex, c).rgb; }\n"

/* pass 0, at the picture's size: the colour between four texels (a diagonal) */
static const char filter_super_xbr0_fs[] =
    "#define WP1 2.0\n#define WP2 1.0\n#define WP3 -1.0\n#define WP4 4.0\n#define WP5 -1.0\n#define WP6 1.0\n"
    "#define WEIGHT1 (1.29633/10.0)\n#define WEIGHT2 (1.75068/10.0/2.0)\n"
    SUPER_XBR_CORE
    "void main() {\n"
    "    vec2 tc = v_uv * 1.0001;\n"
    "    float dx = 1.0 / u_size.x;\n"
    "    float dy = 1.0 / u_size.y;\n"
    "    vec4 t1 = tc.xyxy + vec4(-dx, -dy, 2.0*dx, 2.0*dy);\n"
    "    vec4 t2 = tc.xyxy + vec4(0.0, -dy, dx, 2.0*dy);\n"
    "    vec4 t3 = tc.xyxy + vec4(-dx, 0.0, 2.0*dx, dy);\n"
    "    vec4 t4 = tc.xyxy + vec4(0.0, 0.0, dx, dy);\n"
    "    gl_FragColor = vec4(super_xbr(tx(t1.xy), tx(t1.zy), tx(t1.xw), tx(t1.zw), tx(t2.xy), tx(t2.zy), tx(t3.xy), tx(t4.xy),\n"
    "                                  tx(t4.zy), tx(t3.xw), tx(t4.xw), tx(t4.zw), tx(t3.zy), tx(t3.zw), tx(t2.xw), tx(t2.zw)), 1.0);\n"
    "}\n";

/* pass 1, at twice the size: the picture's texels (u_orig), pass 0's
 * diagonals (u_tex, u_size texels) and the colours between them */
static const char filter_super_xbr1_fs[] =
    "uniform sampler2D u_orig;\n"
    "#define WP1 8.0\n#define WP2 0.0\n#define WP3 0.0\n#define WP4 0.0\n#define WP5 0.0\n#define WP6 0.0\n"
    "#define WEIGHT1 (1.75068/10.0)\n#define WEIGHT2 (1.29633/10.0/2.0)\n"
    SUPER_XBR_CORE
    "vec3 ox(vec2 c) { return texture2D(u_orig, c).rgb; }\n"
    "void main() {\n"
    "    vec2 tc = v_uv * 1.0001;\n"
    "    vec2 fp = fract(tc * u_size);\n"
    "    vec2 dir = fp - vec2(0.5, 0.5);\n"
    "    if ((dir.x * dir.y) > 0.0) {\n"
    "        gl_FragColor = vec4((fp.x > 0.5) ? tx(tc) : ox(tc), 1.0);\n"
    "        return;\n"
    "    }\n"
    "    vec2 g1 = (fp.x > 0.5) ? vec2(0.5 / u_size.x, 0.0) : vec2(0.0, 0.5 / u_size.y);\n"
    "    vec2 g2 = (fp.x > 0.5) ? vec2(0.0, 0.5 / u_size.y) : vec2(0.5 / u_size.x, 0.0);\n"
    "    gl_FragColor = vec4(super_xbr(ox(tc - 3.0*g1), tx(tc - 3.0*g2), tx(tc + 3.0*g2), ox(tc + 3.0*g1),\n"
    "                                  tx(tc - 2.0*g1 - g2), ox(tc - g1 - 2.0*g2), tx(tc - 2.0*g1 + g2), ox(tc - g1),\n"
    "                                  tx(tc - g2), ox(tc - g1 + 2.0*g2), tx(tc + g2), ox(tc + g1),\n"
    "                                  ox(tc + g1 - 2.0*g2), tx(tc + 2.0*g1 - g2), ox(tc + g1 + 2.0*g2), tx(tc + 2.0*g1 + g2)), 1.0);\n"
    "}\n";

/* pass 2, at twice the size: the edges of pass 1 once more */
static const char filter_super_xbr2_fs[] =
    "#define WP1 1.0\n#define WP2 0.0\n#define WP3 2.0\n#define WP4 3.0\n#define WP5 -2.0\n#define WP6 1.0\n"
    "#define WEIGHT1 (1.29633/10.0)\n#define WEIGHT2 (1.75068/10.0/2.0)\n"
    SUPER_XBR_CORE
    "void main() {\n"
    "    vec2 tc = v_uv * 1.0001;\n"
    "    float dx = 1.0 / u_size.x;\n"
    "    float dy = 1.0 / u_size.y;\n"
    "    vec4 t1 = tc.xyxy + vec4(-2.0*dx, -2.0*dy, dx, dy);\n"
    "    vec4 t2 = tc.xyxy + vec4(-dx, -2.0*dy, 0.0, dy);\n"
    "    vec4 t3 = tc.xyxy + vec4(-2.0*dx, -dy, dx, 0.0);\n"
    "    vec4 t4 = tc.xyxy + vec4(-dx, -dy, 0.0, 0.0);\n"
    "    gl_FragColor = vec4(super_xbr(tx(t1.xy), tx(t1.zy), tx(t1.xw), tx(t1.zw), tx(t2.xy), tx(t2.zy), tx(t3.xy), tx(t4.xy),\n"
    "                                  tx(t4.zy), tx(t3.xw), tx(t4.xw), tx(t4.zw), tx(t3.zy), tx(t3.zw), tx(t2.xw), tx(t2.zw)), 1.0);\n"
    "}\n";

#endif
