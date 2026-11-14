/*
 * The GT218's Tesla 3D setup push and EXA's programs for a first
 * triangle. include\velocity9x\nv50_3d.h declares them;
 * docs\specifications\gt218-tesla-3d-first-triangle.md lists the methods
 * and where each value comes from. Pure arithmetic, no I/O.
 *
 * The setup is xf86-video-nouveau's NVAccelInitNV50TCL (nv50_accel.c),
 * method for method, with our VM DMA object for its fifo->vram and
 * without its DMA_NOTIFY. The two program arrays are its words, copied,
 * with its own disassembly beside them. xf86-video-nouveau's copyright and
 * permission notice follow, as its licence requires:
 *
 * Copyright 2008 Ben Skeggs
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "velocity9x/nv50_3d.h"
#include "velocity9x/nv50_shader.h"

void v9x_nv50_3d_scratch_at(v9x_u32 base, struct v9x_nv50_3d_scratch *out)
{
    out->vp = base + V9X_NV50_3D_SCRATCH_VP;
    out->fp = base + V9X_NV50_3D_SCRATCH_FP;
    out->tic = base + V9X_NV50_3D_SCRATCH_TIC;
    out->tsc = base + V9X_NV50_3D_SCRATCH_TSC;
    out->vp_constants = base + V9X_NV50_3D_SCRATCH_VP_DATA;
    out->fp_constants = base + V9X_NV50_3D_SCRATCH_FP_DATA;
    out->texel = base + V9X_NV50_3D_SCRATCH_TEXEL;
    out->texture = base + V9X_NV50_3D_SCRATCH_TEXTURE;
}

/* TIC word 0 for an A8R8G8B8 texel (nv50_texture.h, EXA's _() macro):
 * every channel UNORM, memory components B G R A mapped to C0..C3,
 * format 8_8_8_8. */
#define V9X_NV50_TIC0_TYPE_UNORM     0x00012480ul
#define V9X_NV50_TIC0_MAP_BGRA       0x2a700000ul
#define V9X_NV50_TIC0_FMT_8_8_8_8    0x00000008ul
/* TIC words 2 to 7 for a 1x1 texture, as NV50EXAPictSolid writes them;
 * word 2 also carries the texel address bits 39:32, 0 here. */
#define V9X_NV50_TIC2_SOLID          0xd005d000ul
#define V9X_NV50_TIC3_SOLID          0x00300000ul
#define V9X_NV50_TIC4_SOLID          0x00000001ul
#define V9X_NV50_TIC5_SOLID          0x00010001ul
#define V9X_NV50_TIC6_SOLID          0x03000000ul
/* TSC: wrap S, T and R repeat (all 0) | EXA's 24000h; magnify and
 * minify nearest, no mip. */
#define V9X_NV50_TSC0_REPEAT         0x00024000ul
#define V9X_NV50_TSC1_NEAREST        0x00000051ul
/* 1.0f, for the identity transform and a 1x1 source's 1/size. */
#define V9X_NV50_FLOAT_ONE           0x3f800000ul
#define V9X_NV50_3D_UNIT_CONSTANTS   11ul

void v9x_nv50_3d_solid_tables(const struct v9x_nv50_3d_scratch *scratch,
                              v9x_u32 argb, struct v9x_nv50_3d_solid *out)
{
    v9x_u32 index;
    v9x_u32 unit;

    out->texel = argb;

    out->tic[0] = V9X_NV50_TIC0_TYPE_UNORM | V9X_NV50_TIC0_MAP_BGRA |
                  V9X_NV50_TIC0_FMT_8_8_8_8;
    out->tic[1] = scratch->texel;
    out->tic[2] = V9X_NV50_TIC2_SOLID;
    out->tic[3] = V9X_NV50_TIC3_SOLID;
    out->tic[4] = V9X_NV50_TIC4_SOLID;
    out->tic[5] = V9X_NV50_TIC5_SOLID;
    out->tic[6] = V9X_NV50_TIC6_SOLID;
    out->tic[7] = 0ul;

    for (index = 0ul; index < V9X_NV50_3D_TSC_WORDS; ++index) {
        out->tsc[index] = 0ul;
    }
    out->tsc[0] = V9X_NV50_TSC0_REPEAT;
    out->tsc[1] = V9X_NV50_TSC1_NEAREST;

    /* Both units, though PFP_S samples only unit 0: the vertex program
     * computes unit 1's coordinates too, and zeros would divide by zero
     * there. */
    for (index = 0ul; index < V9X_NV50_3D_VP_CONSTANT_WORDS; ++index) {
        out->vp_constants[index] = 0ul;
    }
    for (unit = 0ul; unit < 2ul; ++unit) {
        v9x_u32 *c = out->vp_constants + unit * V9X_NV50_3D_UNIT_CONSTANTS;

        c[0] = V9X_NV50_FLOAT_ONE;
        c[4] = V9X_NV50_FLOAT_ONE;
        c[8] = V9X_NV50_FLOAT_ONE;
        c[9] = V9X_NV50_FLOAT_ONE;
        c[10] = V9X_NV50_FLOAT_ONE;
    }
}


static void v9x_nv50_3d_one(struct v9x_nv50_push *push, v9x_u32 method,
                            v9x_u32 value)
{
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, method, 1ul, &value);
}

static void v9x_nv50_3d_two(struct v9x_nv50_push *push, v9x_u32 method,
                            v9x_u32 first, v9x_u32 second)
{
    v9x_u32 data[2];

    data[0] = first;
    data[1] = second;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, method, 2ul, data);
}

/* An address method's high and low words, then a third word. Our VM
 * addresses are below 4 GiB, so the high word is 0. */
static void v9x_nv50_3d_address(struct v9x_nv50_push *push, v9x_u32 method,
                                v9x_u32 address, v9x_u32 third)
{
    v9x_u32 data[3];

    data[0] = 0ul;
    data[1] = address;
    data[2] = third;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, method, 3ul, data);
}

/* The same object handle in count consecutive methods. */
static void v9x_nv50_3d_repeat(struct v9x_nv50_push *push, v9x_u32 method,
                               v9x_u32 count, v9x_u32 value)
{
    v9x_u32 data[V9X_8597_DMA_ZETA_COUNT];
    v9x_u32 index;

    for (index = 0ul; index < count; ++index) {
        data[index] = value;
    }
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, method, count, data);
}

void v9x_nv50_push_bind_3d(struct v9x_nv50_push *push)
{
    v9x_u32 handle = V9X_NV50_HANDLE_3D;

    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, V9X_NV50_MTHD_SET_OBJECT,
                         1ul, &handle);
}

void v9x_nv50_push_3d_setup(struct v9x_nv50_push *push,
                            const struct v9x_nv50_3d_scratch *scratch)
{
    v9x_nv50_3d_one(push, V9X_8597_COND_MODE, V9X_8597_COND_MODE_ALWAYS);
    v9x_nv50_3d_repeat(push, V9X_8597_DMA_ZETA, V9X_8597_DMA_ZETA_COUNT,
                       V9X_NV50_HANDLE_DMA_VM);
    v9x_nv50_3d_repeat(push, V9X_8597_DMA_COLOR, V9X_8597_DMA_COLOR_COUNT,
                       V9X_NV50_HANDLE_DMA_VM);
    v9x_nv50_3d_one(push, V9X_8597_RT_CONTROL, 1ul);

    /* Vertices arrive in window coordinates; colour writes on; no Z. */
    v9x_nv50_3d_one(push, V9X_8597_VIEWPORT_TRANSFORM_EN, 0ul);
    v9x_nv50_3d_one(push, V9X_8597_COLOR_MASK_COMMON, 1ul);
    v9x_nv50_3d_one(push, V9X_8597_ZETA_ENABLE, 0ul);

    /* Texture image and sampler tables: TIC limit 800h, TSC limit 0, one
     * sampler per texture (LINKED_TSC), and EXA's texture limits for the
     * fragment stage. */
    v9x_nv50_3d_address(push, V9X_8597_TIC_ADDRESS_HIGH, scratch->tic,
                        0x00000800ul);
    v9x_nv50_3d_address(push, V9X_8597_TSC_ADDRESS_HIGH, scratch->tsc, 0ul);
    v9x_nv50_3d_one(push, V9X_8597_LINKED_TSC, 1ul);
    v9x_nv50_3d_one(push, V9X_8597_TEX_LIMITS2, 0x54ul);

    /* The vertex program: attributes 0, 8 and 9 (x and y of each), six
     * result registers, a result map of 8 with 4 temporaries, its address
     * and constant buffer 1, entry 0. */
    v9x_nv50_3d_two(push, V9X_8597_VP_ATTR_EN0, 0x00000003ul, 0x00000033ul);
    v9x_nv50_3d_one(push, V9X_8597_VP_REG_ALLOC_RESULT, 6ul);
    v9x_nv50_3d_two(push, V9X_8597_VP_RESULT_MAP_SIZE, 8ul, 4ul);
    v9x_nv50_3d_two(push, V9X_8597_VP_ADDRESS_HIGH, 0ul, scratch->vp);
    v9x_nv50_3d_address(push, V9X_8597_CB_DEF_ADDRESS_HIGH,
                        scratch->vp_constants,
                        (V9X_8597_CB_VP << V9X_8597_CB_DEF_BUFFER_SHIFT) |
                            V9X_8597_CB_ENTRIES);
    v9x_nv50_3d_one(push, V9X_8597_SET_PROGRAM_CB,
                    0x00000001ul | (V9X_8597_CB_VP << 12));
    v9x_nv50_3d_one(push, V9X_8597_VP_START_ID, 0ul);

    /* Position from results 0 and 1, z 0 and w 1, then results 2 to 5
     * to the interpolants; the fragment program and constant buffer 2. */
    v9x_nv50_3d_two(push, V9X_8597_VP_RESULT_MAP0, 0x41400100ul,
                    0x05040302ul);
    v9x_nv50_3d_one(push, V9X_8597_POINT_SPRITE_ENABLE, 0ul);
    v9x_nv50_3d_two(push, V9X_8597_FP_INTERPOLANT_CTRL, 0x08040404ul, 8ul);
    v9x_nv50_3d_two(push, V9X_8597_FP_ADDRESS_HIGH, 0ul, scratch->fp);
    v9x_nv50_3d_address(push, V9X_8597_CB_DEF_ADDRESS_HIGH,
                        scratch->fp_constants,
                        (V9X_8597_CB_FP << V9X_8597_CB_DEF_BUFFER_SHIFT) |
                            V9X_8597_CB_ENTRIES);
    v9x_nv50_3d_one(push, V9X_8597_SET_PROGRAM_CB,
                    0x00000031ul | (V9X_8597_CB_FP << 12));

    /* Scissor on, and viewport, scissor and screen scissor all at EXA's
     * 8192 bounds; each draw narrows the scissor to its rectangle. */
    v9x_nv50_3d_one(push, V9X_8597_SCISSOR_ENABLE0, 1ul);
    v9x_nv50_3d_two(push, V9X_8597_VIEWPORT_HORIZ0, V9X_8597_BOUND,
                    V9X_8597_BOUND);
    v9x_nv50_3d_two(push, V9X_8597_SCISSOR_HORIZ0, V9X_8597_BOUND,
                    V9X_8597_BOUND);
    v9x_nv50_3d_two(push, V9X_8597_SCREEN_SCISSOR_HORIZ, V9X_8597_BOUND,
                    V9X_8597_BOUND);
}

/* TIC word 2 for a pitch-linear texture (Mesa g80_texture.xml.h): the
 * bits Mesa always sets (10001000h), border from the colour, normalized
 * coordinates, pitch layout, 2D without mipmaps. It is also the word
 * EXA's 1x1 solid texture uses. Word 5 carries a depth of 1. */
#define V9X_NV50_TIC2_PITCH_2D       0xd005d000ul
#define V9X_NV50_TIC5_DEPTH_ONE      0x00010000ul

/* The IEEE single nearest to 1 / n, for a texture size; 0 for 0. */
static v9x_u32 v9x_nv50_3d_reciprocal(v9x_u32 n)
{
    union {
        float f;
        v9x_u32 u;
    } value;

    if (n == 0ul) {
        return 0ul;
    }
    value.f = 1.0f / (float)n;
    return value.u;
}

void v9x_nv50_3d_texture_tables(v9x_u32 texture, v9x_u32 pitch,
                                v9x_u32 width, v9x_u32 height,
                                struct v9x_nv50_3d_solid *out)
{
    struct v9x_nv50_3d_scratch unused;
    v9x_u32 unit;

    /* Start from the solid tables: the same TSC and identity transform. */
    v9x_nv50_3d_scratch_at(0ul, &unused);
    v9x_nv50_3d_solid_tables(&unused, 0ul, out);

    out->tic[1] = texture;
    out->tic[2] = V9X_NV50_TIC2_PITCH_2D;
    out->tic[3] = pitch;
    out->tic[4] = width;
    out->tic[5] = V9X_NV50_TIC5_DEPTH_ONE | height;
    out->tic[6] = 0ul;
    out->tic[7] = 0ul;
    for (unit = 0ul; unit < 2ul; ++unit) {
        v9x_u32 *c = out->vp_constants + unit * V9X_NV50_3D_UNIT_CONSTANTS;

        c[9] = v9x_nv50_3d_reciprocal(width);
        c[10] = v9x_nv50_3d_reciprocal(height);
    }
}

void v9x_nv50_push_3d_vp_constants(struct v9x_nv50_push *push,
                                   const v9x_u32 *words, v9x_u32 count)
{
    v9x_u32 header_at;

    v9x_nv50_3d_one(push, V9X_8597_CB_ADDR,
                    (0ul << V9X_8597_CB_ADDR_ID_SHIFT) | V9X_8597_CB_VP);
    /* The shared method writer only makes incrementing headers; write
     * the run with it and turn its header into a non-incrementing one,
     * which leaves the method at CB_DATA(0) for every word. */
    header_at = push->count;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, V9X_8597_CB_DATA0, count,
                         words);
    if (push->overflow == 0ul) {
        push->words[header_at] |= V9X_NV50_PUSH_NONINC;
    }
}

/* Two 16-bit coordinates in one word, y high (nv50_accel.h PUSH_VTX2s). */
static v9x_u32 v9x_nv50_3d_xy(v9x_u32 x, v9x_u32 y)
{
    return ((y & 0xfffful) << 16) | (x & 0xfffful);
}

/* One vertex: the same coordinates for source and mask (attribute 8,
 * two words), then the destination position (attribute 0). */
static void v9x_nv50_3d_vertex(struct v9x_nv50_push *push, v9x_u32 sx,
                               v9x_u32 sy, v9x_u32 dx, v9x_u32 dy)
{
    v9x_u32 source = v9x_nv50_3d_xy(sx, sy);

    v9x_nv50_3d_two(push, V9X_8597_VTX_ATTR_2I8, source, source);
    v9x_nv50_3d_one(push, V9X_8597_VTX_ATTR_2I0, v9x_nv50_3d_xy(dx, dy));
}

/* A pitch-linear render target, as Mesa programs one with no Z: tile
 * mode and layer stride 0, RT_HORIZ the linear bit with the pitch,
 * RT_ARRAY_MODE 0 (EXA's own target is tiled, mode 1). Then no blending,
 * EXA's op Src. */
static void v9x_nv50_3d_target(struct v9x_nv50_push *push, v9x_u32 target,
                               v9x_u32 pitch, v9x_u32 height,
                               v9x_u32 format)
{
    v9x_u32 rt[5];

    rt[0] = 0ul;
    rt[1] = target;
    rt[2] = format;
    rt[3] = 0ul;
    rt[4] = 0ul;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, V9X_8597_RT_ADDRESS_HIGH0,
                         5ul, rt);
    v9x_nv50_3d_two(push, V9X_8597_RT_HORIZ0,
                    V9X_8597_RT_HORIZ_LINEAR | pitch, height);
    v9x_nv50_3d_one(push, V9X_8597_RT_ARRAY_MODE, 0ul);
    v9x_nv50_3d_one(push, V9X_8597_BLEND_ENABLE0, 0ul);
}

void v9x_nv50_push_3d_solid_rect(struct v9x_nv50_push *push, v9x_u32 target,
                                 v9x_u32 pitch, v9x_u32 height,
                                 v9x_u32 format, v9x_u32 x, v9x_u32 y,
                                 v9x_u32 w, v9x_u32 h)
{
    /* EXA passes the rectangle's own size as the source extent: with a
     * 1x1 repeating texture every coordinate is the colour. */
    v9x_nv50_push_3d_textured_rect(push, target, pitch, height, format, x,
                                   y, w, h, w, h);
}

void v9x_nv50_push_3d_textured_rect(struct v9x_nv50_push *push,
                                    v9x_u32 target, v9x_u32 pitch,
                                    v9x_u32 height, v9x_u32 format,
                                    v9x_u32 x, v9x_u32 y, v9x_u32 w,
                                    v9x_u32 h, v9x_u32 src_w, v9x_u32 src_h)
{
    /* PFP_S, the texture just written, and EXA's vertex program with its
     * attributes 0, 8 and 9: set by every draw, because a Gouraud draw
     * leaves its own program and attribute enables behind, and the first
     * /3d:texture runs after one sampled texel (0, 0) everywhere. */
    v9x_nv50_3d_target(push, target, pitch, height, format);
    v9x_nv50_3d_one(push, V9X_8597_FP_START_ID, 0ul);
    v9x_nv50_3d_one(push, V9X_8597_VP_START_ID, 0ul);
    v9x_nv50_3d_two(push, V9X_8597_VP_ATTR_EN0, 0x00000003ul, 0x00000033ul);
    v9x_nv50_3d_one(push, V9X_8597_TIC_FLUSH, 0ul);
    v9x_nv50_3d_one(push, V9X_8597_BIND_TIC2, V9X_8597_BIND_TIC_FIRST);
    v9x_nv50_3d_one(push, V9X_8597_BIND_TIC2, V9X_8597_BIND_TIC_UNIT0);

    /* The scissor cuts EXA's one triangle, twice the rectangle in each
     * direction, down to the rectangle. */
    v9x_nv50_3d_two(push, V9X_8597_SCISSOR_HORIZ0, ((x + w) << 16) | x,
                    ((y + h) << 16) | y);
    v9x_nv50_3d_one(push, V9X_8597_VERTEX_BEGIN_GL,
                    V9X_8597_PRIMITIVE_TRIANGLES);
    v9x_nv50_3d_vertex(push, 0ul, src_h * 2ul, x, y + h * 2ul);
    v9x_nv50_3d_vertex(push, 0ul, 0ul, x, y);
    v9x_nv50_3d_vertex(push, src_w * 2ul, 0ul, x + w * 2ul, y);
    v9x_nv50_3d_one(push, V9X_8597_VERTEX_END_GL, 0ul);
}

v9x_u32 v9x_nv50_3d_gouraud_vp(v9x_u32 *words, v9x_u32 max)
{
    struct v9x_nv50_sh sh;
    v9x_u32 slot;

    /* o[0..14] = a[0..14]: position x, y, then the colour's r, g, b, a. */
    v9x_nv50_sh_init(&sh, words, max);
    for (slot = 0ul; slot < 6ul; ++slot) {
        v9x_nv50_sh_mov(&sh, V9X_NV50_SH_LONG, V9X_NV50_SH_O(slot * 4ul),
                        V9X_NV50_SH_A(slot * 4ul));
    }
    v9x_nv50_sh_exit(&sh);
    if (sh.overflow != 0ul || sh.invalid != 0ul) {
        return 0ul;
    }
    return sh.count;
}

v9x_u32 v9x_nv50_3d_gouraud_fp(v9x_u32 *words, v9x_u32 max)
{
    struct v9x_nv50_sh sh;
    v9x_u32 channel;

    /* v[0] is w, as in EXA's programs: 1/w into $r4, then the four
     * colour varyings, perspective-correct, into $r0..$r3. A long move
     * ends it, since only a long instruction can carry the exit. */
    v9x_nv50_sh_init(&sh, words, max);
    v9x_nv50_sh_interp(&sh, V9X_NV50_SH_R(0), V9X_NV50_SH_V(0x0),
                       V9X_NV50_SH_NONE);
    v9x_nv50_sh_rcp(&sh, V9X_NV50_SH_SHORT, V9X_NV50_SH_R(4),
                    V9X_NV50_SH_R(0));
    for (channel = 0ul; channel < 4ul; ++channel) {
        v9x_nv50_sh_interp(&sh, V9X_NV50_SH_R(channel),
                           V9X_NV50_SH_V(0x4ul + channel * 4ul),
                           V9X_NV50_SH_R(4));
    }
    v9x_nv50_sh_mov(&sh, V9X_NV50_SH_LONG, V9X_NV50_SH_R(3),
                    V9X_NV50_SH_R(3));
    v9x_nv50_sh_exit(&sh);
    if (sh.overflow != 0ul || sh.invalid != 0ul) {
        return 0ul;
    }
    return sh.count;
}

/* IEEE single-precision constants the Gouraud colours use. */
#define V9X_NV50_FLOAT_HALF          0x3f000000ul
#define V9X_NV50_FLOAT_TWO           0x40000000ul

/* A Gouraud vertex: attribute 1's four floats, then the position. */
static void v9x_nv50_3d_colour_vertex(struct v9x_nv50_push *push,
                                      v9x_u32 red, v9x_u32 green,
                                      v9x_u32 x, v9x_u32 y)
{
    v9x_u32 colour[4];

    colour[0] = red;
    colour[1] = green;
    colour[2] = V9X_NV50_FLOAT_HALF;
    colour[3] = V9X_NV50_FLOAT_ONE;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, V9X_8597_VTX_ATTR_4F_X1,
                         4ul, colour);
    v9x_nv50_3d_one(push, V9X_8597_VTX_ATTR_2I0, v9x_nv50_3d_xy(x, y));
}

void v9x_nv50_push_3d_gouraud_rect(struct v9x_nv50_push *push,
                                   v9x_u32 target, v9x_u32 pitch,
                                   v9x_u32 height, v9x_u32 format,
                                   v9x_u32 x, v9x_u32 y, v9x_u32 w,
                                   v9x_u32 h)
{
    v9x_nv50_3d_target(push, target, pitch, height, format);
    v9x_nv50_3d_one(push, V9X_8597_FP_START_ID,
                    V9X_NV50_3D_GOURAUD_FP_START);
    v9x_nv50_3d_one(push, V9X_8597_VP_START_ID,
                    V9X_NV50_3D_GOURAUD_VP_START);
    v9x_nv50_3d_two(push, V9X_8597_VP_ATTR_EN0,
                    V9X_NV50_3D_GOURAUD_ATTR_EN0, 0ul);
    v9x_nv50_3d_two(push, V9X_8597_SCISSOR_HORIZ0, ((x + w) << 16) | x,
                    ((y + h) << 16) | y);
    v9x_nv50_3d_one(push, V9X_8597_VERTEX_BEGIN_GL,
                    V9X_8597_PRIMITIVE_TRIANGLES);
    v9x_nv50_3d_colour_vertex(push, 0ul, V9X_NV50_FLOAT_TWO, x,
                              y + h * 2ul);
    v9x_nv50_3d_colour_vertex(push, 0ul, 0ul, x, y);
    v9x_nv50_3d_colour_vertex(push, V9X_NV50_FLOAT_TWO, 0ul, x + w * 2ul,
                              y);
    v9x_nv50_3d_one(push, V9X_8597_VERTEX_END_GL, 0ul);
}

/* nv50_accel.c, NVAccelInitNV50TCL: the vertex program. */
const v9x_u32 v9x_nv50_3d_exa_vp[V9X_NV50_3D_EXA_VP_WORDS] = {
    0x10000001ul, 0x0423c788ul, /* mov b32 o[0x0] a[0x0] */
    0x10000205ul, 0x0423c788ul, /* mov b32 o[0x4] a[0x4] */
    0xc0800401ul, 0x00200780ul, /* mul rn f32 $r0 a[0x8] c0[0x0] */
    0xc0830405ul, 0x00200780ul, /* mul rn f32 $r1 a[0x8] c0[0xc] */
    0xc0860409ul, 0x00200780ul, /* mul rn f32 $r2 a[0x8] c0[0x18] */
    0xe0810601ul, 0x00200780ul, /* add f32 $r0 (mul a[0xc] c0[0x4]) $r0 */
    0xe0840605ul, 0x00204780ul, /* add f32 $r1 (mul a[0xc] c0[0x10]) $r1 */
    0xe0870609ul, 0x00208780ul, /* add f32 $r2 (mul a[0xc] c0[0x1c]) $r2 */
    0xb1000001ul, 0x00008780ul, /* add rn f32 $r0 $r0 c0[0x8] */
    0xb1000205ul, 0x00014780ul, /* add rn f32 $r1 $r1 c0[0x14] */
    0xb1000409ul, 0x00020780ul, /* add rn f32 $r2 $r2 c0[0x20] */
    0x90000409ul, 0x00000780ul, /* rcp f32 $r2 $r2 */
    0xc0020001ul, 0x00000780ul, /* mul rn f32 $r0 $r0 $r2 */
    0xc0020205ul, 0x00000780ul, /* mul rn f32 $r1 $r1 $r2 */
    0xc0890009ul, 0x00000788ul, /* mul rn f32 o[0x8] $r0 c0[0x24] */
    0xc08a020dul, 0x00000788ul, /* mul rn f32 o[0xc] $r1 c0[0x28] */
    0xc08b0801ul, 0x00200780ul, /* mul rn f32 $r0 a[0x10] c0[0x2c] */
    0xc08e0805ul, 0x00200780ul, /* mul rn f32 $r1 a[0x10] c0[0x38] */
    0xc0910809ul, 0x00200780ul, /* mul rn f32 $r2 a[0x10] c0[0x44] */
    0xe08c0a01ul, 0x00200780ul, /* add f32 $r0 (mul a[0x14] c0[0x30]) $r0 */
    0xe08f0a05ul, 0x00204780ul, /* add f32 $r1 (mul a[0x14] c0[0x3c]) $r1 */
    0xe0920a09ul, 0x00208780ul, /* add f32 $r2 (mul a[0x14] c0[0x48]) $r2 */
    0xb1000001ul, 0x00034780ul, /* add rn f32 $r0 $r0 c0[0x34] */
    0xb1000205ul, 0x00040780ul, /* add rn f32 $r1 $r1 c0[0x40] */
    0xb1000409ul, 0x0004c780ul, /* add rn f32 $r2 $r2 c0[0x4c] */
    0x90000409ul, 0x00000780ul, /* rcp f32 $r2 $r2 */
    0xc0020001ul, 0x00000780ul, /* mul rn f32 $r0 $r0 $r2 */
    0xc0020205ul, 0x00000780ul, /* mul rn f32 $r1 $r1 $r2 */
    0xc0940011ul, 0x00000788ul, /* mul rn f32 o[0x10] $r0 c0[0x50] */
    0xc0950215ul, 0x00000789ul  /* exit mul rn f32 o[0x14] $r1 c0[0x54] */
};

/* nv50_accel.c, PFP_S: the colour is texture 0 at the coordinates. */
const v9x_u32 v9x_nv50_3d_exa_fp_s[V9X_NV50_3D_EXA_FP_S_WORDS] = {
    0x80000000ul,               /* interp $r0 v[0x0] */
    0x90000004ul,               /* rcp f32 $r1 $r0 */
    0x82010200ul,               /* interp $r0 v[0x4] $r1 */
    0x82020204ul,               /* interp $r1 v[0x8] $r1 */
    0xf6400001ul, 0x0000c785ul  /* exit texauto live $r0:$r1:$r2:$r3 $t0
                                   $s0 $r0:$r1 0x0 0x0 0x0 */
};
