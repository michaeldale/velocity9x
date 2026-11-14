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

void v9x_nv50_3d_scratch_at(v9x_u32 base, struct v9x_nv50_3d_scratch *out)
{
    out->vp = base + V9X_NV50_3D_SCRATCH_VP;
    out->fp = base + V9X_NV50_3D_SCRATCH_FP;
    out->tic = base + V9X_NV50_3D_SCRATCH_TIC;
    out->tsc = base + V9X_NV50_3D_SCRATCH_TSC;
    out->vp_constants = base + V9X_NV50_3D_SCRATCH_VP_DATA;
    out->fp_constants = base + V9X_NV50_3D_SCRATCH_FP_DATA;
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
