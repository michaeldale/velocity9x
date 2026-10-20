/*
 * Gen4 (965 / GM965) 3D state and command encoding for a Direct3D draw.
 *
 * docs\plans\intel-gen4-gen5-bringup.md, Phase 2. Pure encoding, no I/O.
 * Layouts are Mesa's genxml gen4.xml (the 965 PRM Vol 2 tables in
 * machine-readable form); the command order and the static choices - VS,
 * GS and CLIP passed through, the SF viewport transform off, the URB
 * partition, 16-pixel dispatch - are xf86-video-intel's Gen4 render path
 * (sna\gen4_render.c), which drew on 965 hardware.
 *
 * Everything the GPU reads lives in the Gen3 engine reserve the mini-VDD
 * mapped (1 MiB at the top of the VBE memory): the ring and the Gen3 status
 * page take the first 68 KiB, and this engine's area starts at
 * V9X_G4_AREA_OFF. STATE_BASE_ADDRESS points both state bases at the
 * reserve's first byte, so every state and kernel pointer below is an
 * offset in the reserve.
 *
 * Nothing in this file has executed on hardware.
 */
#ifndef VELOCITY9X_INTEL_G4_3D_H
#define VELOCITY9X_INTEL_G4_3D_H

#include "velocity9x/status.h"
#include "velocity9x/types.h"

/* The engine's area in the reserve, and what is in it. */
#define V9X_G4_AREA_OFF         0x00020000ul
#define V9X_G4_SF_KERNEL_OFF    0x00020000ul  /* 1 KiB                     */
#define V9X_G4_WM_KERNEL_OFF    0x00021000ul  /* slots of 2 KiB            */
#define V9X_G4_WM_KERNEL_BYTES  0x00000800ul
#define V9X_G4_WM_KERNEL_SLOTS  24ul
#define V9X_G4_SIG_OFF          0x0002f000ul  /* kernel signatures         */
/* The pipeline's shared words, after the signatures: the slot the last
 * draw took, and the sequence each slot was last submitted with. */
#define V9X_G4_PIPE_OFF         0x0002fc00ul
#define V9X_G4_PIPE_LAST_SLOT   0ul           /* dword index */
#define V9X_G4_PIPE_SLOT_SEQ    1ul           /* dwords 1 and 2 */
/*
 * Two slots, so the CPU writes one draw while the GPU runs the other
 * (d3d_g4.c). Slot k's state block, batch and vertices are at the base
 * plus k times the stride.
 */
#define V9X_G4_SLOTS            2ul
#define V9X_G4_STATE_OFF        0x00030000ul  /* slot 0's state block      */
#define V9X_G4_STATE_STRIDE     0x00001000ul
#define V9X_G4_BATCH_OFF        0x00032000ul  /* slot 0's batch, 4 KiB     */
#define V9X_G4_BATCH_STRIDE     0x00001000ul
#define V9X_G4_BATCH_BYTES      0x00001000ul
/* Kernels with a second texture unit (stage1), in slots of their own: the
 * one-unit slots above are full. */
#define V9X_G4_WM_KERNEL2_OFF   0x00034000ul
#define V9X_G4_VERTEX_OFF       0x00040000ul  /* slot 0's vertices         */
#define V9X_G4_VERTEX_STRIDE    0x00058000ul
#define V9X_G4_VERTEX_BYTES     0x00058000ul  /* per slot                  */

/* Offsets inside the state block, aligned as the PRM asks (32 bytes for
 * state, 64 for COLOR_CALC_STATE as xf86 places it). */
#define V9X_G4_ST_VS        0x000ul
#define V9X_G4_ST_SF        0x040ul
#define V9X_G4_ST_WM        0x080ul
#define V9X_G4_ST_CC        0x0c0ul
#define V9X_G4_ST_CC_VP     0x100ul
#define V9X_G4_ST_SF_VP     0x140ul
#define V9X_G4_ST_SAMPLER   0x180ul
#define V9X_G4_ST_BORDER    0x1c0ul
#define V9X_G4_ST_BINDING   0x200ul
#define V9X_G4_ST_RT_SURF   0x220ul
#define V9X_G4_ST_TEX_SURF  0x240ul
#define V9X_G4_ST_TEX1_SURF 0x260ul
#define V9X_G4_ST_BYTES     0x280ul
/* SAMPLER_STATE is four dwords: sampler 1 follows sampler 0. */
#define V9X_G4_SAMPLER_BYTES 16ul

/*
 * The VUE, written by the vertex fetcher with the VS passed through:
 * dwords 0-3 header, 4-7 position (screen x, y, z, 1), then from dword 8
 * the attributes the SF reads (VERTEX URB ENTRY READ OFFSET 1, in 8-dword
 * units): a copy of the position whose z and w the SF kernel replaces with
 * z and 1/w, diffuse, specular (fog in alpha), texture coordinates (u/w,
 * v/w, 1/w, 1), the second unit's (u1/w, v1/w, 0, 1; zero for a one-unit
 * draw), and a zero slot to fill the third setup register.
 */
#define V9X_G4_VUE_DWORDS      32u
#define V9X_G4_SETUP_REGS      3u
#define V9X_G4_WM_SETUP_GRFS   10u   /* five attributes, two GRFs each */

/* The vertex buffer's layout, built by the HAL from a TL vertex. */
#define V9X_G4_VB_PITCH        44u   /* x y z, diffuse, specular, u v w, u1 v1 */
#define V9X_G4_VB_POS          0u
#define V9X_G4_VB_DIFFUSE      12u
#define V9X_G4_VB_SPECULAR     16u
#define V9X_G4_VB_TEX0         20u
#define V9X_G4_VB_TEX1         32u

/* URB partition, 512-bit rows (965 has 256): VS 32 entries of 2 (a 32-dword
 * VUE), SF 24 of 6 (three setup registers, transposed), none for GS, CLIP
 * or the constant buffer. */
#define V9X_G4_URB_VS_ENTRIES 32u
#define V9X_G4_URB_VS_SIZE    2u
#define V9X_G4_URB_SF_ENTRIES 24u
#define V9X_G4_URB_SF_SIZE    6u

/* Thread limits: GM965 has 24 SF and 32 WM threads (xf86 gen4_render.c). */
#define V9X_G4_SF_MAX_THREADS 24u
#define V9X_G4_WM_MAX_THREADS 32u

/* Surface formats (965 PRM Vol 1, "Surface Format"). */
#define V9X_G4_FMT_B8G8R8A8_UNORM     0x0c0u
#define V9X_G4_FMT_B8G8R8X8_UNORM     0x0e9u
#define V9X_G4_FMT_B5G6R5_UNORM       0x100u
#define V9X_G4_FMT_B5G5R5A1_UNORM     0x102u
#define V9X_G4_FMT_B4G4R4A4_UNORM     0x104u
#define V9X_G4_FMT_R32G32B32A32_FLOAT 0x000u
#define V9X_G4_FMT_R32G32B32_FLOAT    0x040u
#define V9X_G4_FMT_R32G32_FLOAT       0x085u

/* 3DSTATE_DEPTH_BUFFER formats. */
#define V9X_G4_DEPTH_D32_FLOAT       1u
#define V9X_G4_DEPTH_D24_UNORM_X8    3u
#define V9X_G4_DEPTH_D16_UNORM       5u

/* A batch being built: overflow is set, not returned. */
struct v9x_g4_batch {
    v9x_u32 *dw;
    v9x_u32 capacity;
    v9x_u32 count;
    v9x_u8 overflow;
};

/* One sampler, in SAMPLER_STATE's terms. */
struct v9x_g4_sampler {
    v9x_u8 min_filter;   /* MAPFILTER: 0 nearest, 1 linear */
    v9x_u8 mag_filter;
    v9x_u8 mip_filter;   /* MIPFILTER: 0 none, 1 nearest, 3 linear */
    v9x_u8 address;      /* TCM for all three axes */
    v9x_u8 max_lod;      /* levels - 1 */
    /* Chroma key table entry 0, KEYFILTER_REPLACE_BLACK (DW3 25, 24:23,
     * 22): a matching texel samples as (0, 0, 0, 0). */
    v9x_u8 chroma_key;
};

/* What COLOR_CALC_STATE carries. Factors and functions are genxml's. */
struct v9x_g4_cc {
    v9x_u8 depth_test;
    v9x_u8 depth_write;
    v9x_u8 depth_function;
    v9x_u8 alpha_test;
    v9x_u8 alpha_function;
    v9x_u8 alpha_ref;
    v9x_u8 blend;
    v9x_u8 src_factor;
    v9x_u8 dst_factor;
};

/* One draw's batch. Addresses are graphics addresses; offsets are from
 * the reserve (the state base). */
struct v9x_g4_draw {
    v9x_u32 state_base;      /* the reserve's graphics address */
    v9x_u32 state_offset;    /* V9X_G4_STATE_OFF */
    v9x_u32 vertex_address;
    v9x_u32 vertex_count;
    v9x_u16 width;           /* the target's, for the drawing rectangle */
    v9x_u16 height;
    v9x_u32 depth_address;   /* 0: no depth buffer */
    v9x_u32 depth_pitch;
    v9x_u8 depth_format;
    /* 3DSTATE_CHROMA_KEY entry 0, when chroma_key is set: ARGB8888 low
     * and high, as v9x_g4_chroma_key makes them. */
    v9x_u8 chroma_key;
    v9x_u32 chroma_low;
    v9x_u32 chroma_high;
};

void v9x_g4_batch_init(struct v9x_g4_batch *batch, v9x_u32 *storage,
                       v9x_u32 capacity);

/* State blocks, each written at its pointer. */
v9x_status v9x_g4_vs_state(v9x_u32 *out);
v9x_status v9x_g4_sf_state(v9x_u32 *out, v9x_u32 kernel_offset,
                           v9x_u8 grf_used, v9x_u32 viewport_offset);
v9x_status v9x_g4_wm_state(v9x_u32 *out, v9x_u32 kernel_offset,
                           v9x_u8 grf_used, v9x_u32 sampler_offset,
                           v9x_u8 sampler_count, v9x_u8 binding_count);
v9x_status v9x_g4_cc_state(v9x_u32 *out, const struct v9x_g4_cc *cc,
                           v9x_u32 viewport_offset, v9x_u8 write_disable);
void v9x_g4_cc_viewport(v9x_u32 *out);
void v9x_g4_sf_viewport(v9x_u32 *out);
v9x_status v9x_g4_surface(v9x_u32 *out, v9x_u16 format, v9x_u32 address,
                          v9x_u16 width, v9x_u16 height, v9x_u32 pitch,
                          v9x_u8 render_target, v9x_u8 write_disable,
                          v9x_u8 levels);
v9x_status v9x_g4_sampler_state(v9x_u32 *out,
                                const struct v9x_g4_sampler *sampler,
                                v9x_u32 border_offset);

/*
 * A DirectDraw source colour key - low and high in the texture's own
 * format, the format's red, green and blue masks - as 3DSTATE_CHROMA_KEY's
 * ARGB8888 range (965 PRM Vol 4, 3DSTATE_CHROMA_KEY): each channel widened
 * to 8 bits by replicating its high bits, as the sampler widens the texel
 * it compares, and alpha the full 00-FF range so it is not compared.
 * Masks must be contiguous and 1 to 8 bits wide.
 */
v9x_status v9x_g4_chroma_key(v9x_u32 low_raw, v9x_u32 high_raw,
                             v9x_u32 red_mask, v9x_u32 green_mask,
                             v9x_u32 blue_mask, v9x_u32 *low,
                             v9x_u32 *high);

/* The whole draw: pipeline, URB, pointers, buffers, the primitive, and
 * MI_BATCH_BUFFER_END, with URB_FENCE kept inside one 64-byte line of the
 * batch (xf86's gen4_emit_urb). The batch storage must be 64-byte aligned
 * in graphics memory. */
v9x_status v9x_g4_draw_batch(struct v9x_g4_batch *batch,
                             const struct v9x_g4_draw *draw);

/* The ring's part: MI_BATCH_BUFFER_START of the batch (GTT, non-secure,
 * as i915's gen4_emit_bb_start). Two dwords. */
v9x_u32 v9x_g4_batch_start_header(void);

/* Check a batch against the commands this file emits: every header known,
 * every length the one emitted, MI_BATCH_BUFFER_END last. The role
 * i9xx_3d_decode.c plays for Gen3; returns V9X_FALSE on anything else. */
v9x_u16 v9x_g4_batch_check(const v9x_u32 *dw, v9x_u32 count);

#endif /* VELOCITY9X_INTEL_G4_3D_H */
