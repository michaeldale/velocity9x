/*
 * Gen4 (965 / GM965) 3D state and command encoding. See
 * velocity9x\intel_g4_3d.h. Bit positions are genxml gen4.xml's (bit n of
 * a structure is dword n / 32, bit n % 32); the values chosen are xf86's
 * sna\gen4_render.c unless a comment says otherwise.
 */
#include "velocity9x/intel_g4_3d.h"

/* Command headers: GEN4_3D(pipeline, opcode, subopcode) is
 * 3 << 29 | pipeline << 27 | opcode << 24 | subopcode << 16, and the low
 * byte is the length less two. */
#define V9X_G4_CMD(p, o, s, len) \
    (0x60000000ul | ((v9x_u32)(p) << 27) | ((v9x_u32)(o) << 24) | \
     ((v9x_u32)(s) << 16) | ((v9x_u32)(len) - 2ul))

#define V9X_G4_MI_NOOP             0x00000000ul
#define V9X_G4_MI_FLUSH            0x02000000ul
#define V9X_G4_MI_FLUSH_EXE        0x00000002ul /* state/instruction invalidate */
#define V9X_G4_MI_BATCH_END        0x05000000ul
/* MI_BATCH_BUFFER_START, 31h << 23, GTT (2 << 6), non-secure (bit 8). */
#define V9X_G4_MI_BATCH_START      0x18800180ul

#define V9X_G4_PIPELINE_SELECT_3D  0x61040000ul
#define V9X_G4_SBA_DWORDS          6u
#define V9X_G4_URB_FENCE_DWORDS    3u
#define V9X_G4_CS_URB_DWORDS       2u
#define V9X_G4_POINTERS_DWORDS     7u
#define V9X_G4_BINDING_DWORDS      6u
#define V9X_G4_RECT_DWORDS         4u
#define V9X_G4_DEPTH_DWORDS        5u
#define V9X_G4_VB_DWORDS           5u
#define V9X_G4_VE_COUNT            7u
#define V9X_G4_VE_DWORDS           (1u + 2u * V9X_G4_VE_COUNT)
#define V9X_G4_PRIM_DWORDS         6u

/* URB_FENCE reallocation requests, DW0 13:8: CS, SF, CLIP, GS, VS. */
#define V9X_G4_URB_REALLOC         0x00002f00ul

#define V9X_G4_PRIM_TRILIST        4ul
#define V9X_G4_SURFTYPE_2D         1ul
#define V9X_G4_SURFTYPE_NULL       7ul

/* VERTEX_ELEMENT component controls. */
#define V9X_G4_VFCOMP_STORE_SRC  1ul
#define V9X_G4_VFCOMP_STORE_0    2ul
#define V9X_G4_VFCOMP_STORE_1_FP 3ul

/* SF_STATE: cull mode NONE, origin bias 0.5 both ways (Mesa crocus and
 * xf86 alike), triangle fan provoking vertex 2 (xf86). */
#define V9X_G4_CULL_NONE   1ul
#define V9X_G4_ORIGIN_BIAS 8ul  /* u0.4: 0.5 */
#define V9X_G4_TRIFAN_PV   2ul

/* COLOR_CALC_STATE: logic op COPY, clamps on, UNORM range. */
#define V9X_G4_LOGICOP_COPY 0xcul

static v9x_u32 v9x_g4_float(float value)
{
    union {
        float f;
        v9x_u32 u;
    } convert;

    convert.f = value;
    return convert.u;
}

/* GRF Register Count: blocks of 16 registers, less one. */
static v9x_u32 v9x_g4_grf_blocks(v9x_u8 used)
{
    return ((v9x_u32)used + 15ul) / 16ul - 1ul;
}

void v9x_g4_batch_init(struct v9x_g4_batch *batch, v9x_u32 *storage,
                       v9x_u32 capacity)
{
    batch->dw = storage;
    batch->capacity = capacity;
    batch->count = 0ul;
    batch->overflow = 0u;
}

static void v9x_g4_emit(struct v9x_g4_batch *batch, v9x_u32 value)
{
    if (batch->count >= batch->capacity) {
        batch->overflow = 1u;
        return;
    }
    batch->dw[batch->count++] = value;
}

static void v9x_g4_zero(v9x_u32 *out, v9x_u32 dwords)
{
    v9x_u32 index;

    for (index = 0ul; index < dwords; ++index) {
        out[index] = 0ul;
    }
}

/* The VS passed through: URB entries and size only, the unit disabled and
 * its vertex cache off (xf86 gen4_create_vs_unit_state). */
v9x_status v9x_g4_vs_state(v9x_u32 *out)
{
    if (out == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_g4_zero(out, 7ul);
    out[4] = ((v9x_u32)V9X_G4_URB_VS_ENTRIES << 11) |
             (((v9x_u32)V9X_G4_URB_VS_SIZE - 1ul) << 19);
    out[6] = 1ul << 1;   /* Enable 0, Vertex Cache Disable 1 */
    return V9X_STATUS_OK;
}

v9x_status v9x_g4_sf_state(v9x_u32 *out, v9x_u32 kernel_offset,
                           v9x_u8 grf_used, v9x_u32 viewport_offset)
{
    if (out == 0 || (kernel_offset & 63ul) != 0ul ||
        (viewport_offset & 31ul) != 0ul || grf_used == 0u ||
        grf_used > 128u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_g4_zero(out, 8ul);
    out[0] = (v9x_g4_grf_blocks(grf_used) << 1) | kernel_offset;
    /* Alternate floating point mode, as Mesa's Gen4 SF: a degenerate
     * triangle's 1/det is then not an IEEE infinity. */
    out[1] = 1ul << 16;
    out[3] = 3ul |                                   /* dispatch GRF g3   */
             (1ul << 4) |                            /* read offset 1     */
             ((v9x_u32)V9X_G4_SETUP_REGS << 11);     /* read length       */
    out[4] = ((v9x_u32)V9X_G4_URB_SF_ENTRIES << 11) |
             (((v9x_u32)V9X_G4_URB_SF_SIZE - 1ul) << 19) |
             (((v9x_u32)V9X_G4_SF_MAX_THREADS - 1ul) << 25);
    /* Front winding CW, viewport transform off, the viewport state named
     * anyway. */
    out[5] = viewport_offset;
    out[6] = (V9X_G4_ORIGIN_BIAS << 9) | (V9X_G4_ORIGIN_BIAS << 13) |
             (V9X_G4_CULL_NONE << 29);
    out[7] = V9X_G4_TRIFAN_PV << 25;
    return V9X_STATUS_OK;
}

v9x_status v9x_g4_wm_state(v9x_u32 *out, v9x_u32 kernel_offset,
                           v9x_u8 grf_used, v9x_u32 sampler_offset,
                           v9x_u8 sampler_count, v9x_u8 binding_count)
{
    if (out == 0 || (kernel_offset & 63ul) != 0ul ||
        (sampler_offset & 31ul) != 0ul || grf_used == 0u ||
        grf_used > 128u || sampler_count > 4u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_g4_zero(out, 8ul);
    out[0] = (v9x_g4_grf_blocks(grf_used) << 1) | kernel_offset;
    /* Depth coefficients at URB offset 1 (crocus), the binding table
     * entry count. */
    out[1] = (1ul << 8) | ((v9x_u32)binding_count << 18);
    out[3] = 3ul |                                       /* setup at g3  */
             (0ul << 4) |                                /* read offset  */
             ((v9x_u32)V9X_G4_WM_SETUP_GRFS << 11);      /* read length  */
    out[4] = ((v9x_u32)sampler_count << 2) | sampler_offset;
    /* 16-pixel dispatch only, early depth test, thread dispatch, line
     * end cap 1.0 pixel (crocus), the thread limit. */
    out[5] = (1ul << 1) | (1ul << 16) | (1ul << 18) | (1ul << 19) |
             (((v9x_u32)V9X_G4_WM_MAX_THREADS - 1ul) << 25);
    return V9X_STATUS_OK;
}

v9x_status v9x_g4_cc_state(v9x_u32 *out, const struct v9x_g4_cc *cc,
                           v9x_u32 viewport_offset, v9x_u8 write_disable)
{
    if (out == 0 || cc == 0 || (viewport_offset & 31ul) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    (void)write_disable;
    v9x_g4_zero(out, 8ul);
    if (cc->depth_test != 0u) {
        out[2] = (1ul << 15) | (((v9x_u32)cc->depth_function & 7ul) << 12) |
                 ((cc->depth_write != 0u) ? (1ul << 11) : 0ul);
    }
    if (cc->alpha_test != 0u) {
        out[3] |= (1ul << 11) | (((v9x_u32)cc->alpha_function & 7ul) << 8);
    }
    if (cc->blend != 0u) {
        out[3] |= 1ul << 12;
    }
    out[4] = viewport_offset;
    /* Alpha factors as the colour's (independent alpha off), COPY. */
    out[5] = (((v9x_u32)cc->dst_factor & 0x1ful) << 2) |
             (((v9x_u32)cc->src_factor & 0x1ful) << 7) |
             (V9X_G4_LOGICOP_COPY << 16);
    /* Pre- and post-blend clamps on (xf86), UNORM range, ADD. */
    out[6] = 1ul | (1ul << 1) |
             (((v9x_u32)cc->dst_factor & 0x1ful) << 19) |
             (((v9x_u32)cc->src_factor & 0x1ful) << 24);
    out[7] = (v9x_u32)cc->alpha_ref;
    return V9X_STATUS_OK;
}

void v9x_g4_cc_viewport(v9x_u32 *out)
{
    out[0] = v9x_g4_float(0.0f);
    out[1] = v9x_g4_float(1.0f);
}

/* Named by SF_STATE even with the transform off: an identity matrix and an
 * empty scissor. */
void v9x_g4_sf_viewport(v9x_u32 *out)
{
    v9x_g4_zero(out, 8ul);
    out[0] = v9x_g4_float(1.0f);
    out[1] = v9x_g4_float(1.0f);
    out[2] = v9x_g4_float(1.0f);
}

v9x_status v9x_g4_surface(v9x_u32 *out, v9x_u16 format, v9x_u32 address,
                          v9x_u16 width, v9x_u16 height, v9x_u32 pitch,
                          v9x_u8 render_target, v9x_u8 write_disable,
                          v9x_u8 levels)
{
    if (out == 0 || width == 0u || height == 0u || width > 8192u ||
        height > 8192u || pitch == 0ul || pitch > 0x20000ul ||
        levels == 0u || levels > 12u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_g4_zero(out, 8ul);
    /* 2D, the format, colour blend enabled as xf86 sets it on every
     * surface, write disables, and read/write render cache mode for a
     * target (xf86 gen4_bind_bo). */
    out[0] = (V9X_G4_SURFTYPE_2D << 29) | ((v9x_u32)format << 18) |
             (1ul << 13) | (((v9x_u32)write_disable & 0xful) << 14) |
             ((render_target != 0u) ? (1ul << 8) : 0ul);
    out[1] = address;
    out[2] = (((v9x_u32)height - 1ul) << 19) | (((v9x_u32)width - 1ul) << 6) |
             (((v9x_u32)levels - 1ul) << 2);
    out[3] = (pitch - 1ul) << 3;
    return V9X_STATUS_OK;
}

v9x_status v9x_g4_sampler_state(v9x_u32 *out,
                                const struct v9x_g4_sampler *sampler,
                                v9x_u32 border_offset)
{
    v9x_u32 tcm;

    if (out == 0 || sampler == 0 || (border_offset & 31ul) != 0ul ||
        sampler->address > 5u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_g4_zero(out, 4ul);
    tcm = (v9x_u32)sampler->address;
    /* LOD preclamp (GL mode, xf86), filters. */
    out[0] = (1ul << 28) | (((v9x_u32)sampler->mip_filter & 3ul) << 20) |
             (((v9x_u32)sampler->mag_filter & 7ul) << 17) |
             (((v9x_u32)sampler->min_filter & 7ul) << 14);
    /* TCX 8:6, TCY 5:3, TCZ 2:0; MAX LOD u4.6 at 21:12. */
    out[1] = (tcm << 6) | (tcm << 3) | tcm |
             (((v9x_u32)sampler->max_lod & 0xful) << 18);
    out[2] = border_offset;
    return V9X_STATUS_OK;
}

static void v9x_g4_vertex_element(struct v9x_g4_batch *batch, v9x_u32 format,
                                  v9x_u32 offset, v9x_u32 dest, v9x_u32 c0,
                                  v9x_u32 c1, v9x_u32 c2, v9x_u32 c3)
{
    v9x_g4_emit(batch, (1ul << 26) | (format << 16) | offset);
    v9x_g4_emit(batch, (c0 << 28) | (c1 << 24) | (c2 << 20) | (c3 << 16) |
                           dest);
}

v9x_status v9x_g4_draw_batch(struct v9x_g4_batch *batch,
                             const struct v9x_g4_draw *draw)
{
    v9x_u32 st;
    v9x_u32 vs_end;
    v9x_u32 sf_end;

    if (batch == 0 || draw == 0 || draw->vertex_count == 0ul ||
        draw->width == 0u || draw->height == 0u ||
        (draw->state_base & 0xffful) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    st = draw->state_offset;

    v9x_g4_emit(batch, V9X_G4_MI_FLUSH | V9X_G4_MI_FLUSH_EXE);
    v9x_g4_emit(batch, V9X_G4_PIPELINE_SELECT_3D);

    /* Both state bases at the reserve; upper bounds disabled. */
    v9x_g4_emit(batch, V9X_G4_CMD(0, 1, 1, V9X_G4_SBA_DWORDS));
    v9x_g4_emit(batch, draw->state_base | 1ul);
    v9x_g4_emit(batch, draw->state_base | 1ul);
    v9x_g4_emit(batch, 0ul);
    v9x_g4_emit(batch, 1ul);
    v9x_g4_emit(batch, 0ul);

    v9x_g4_emit(batch, V9X_G4_CMD(3, 0, 1, V9X_G4_BINDING_DWORDS));
    v9x_g4_emit(batch, 0ul);
    v9x_g4_emit(batch, 0ul);
    v9x_g4_emit(batch, 0ul);
    v9x_g4_emit(batch, 0ul);
    v9x_g4_emit(batch, st + V9X_G4_ST_BINDING);

    /* VS, GS and CLIP passed through; SF, WM and CC from the block. */
    v9x_g4_emit(batch, V9X_G4_CMD(3, 0, 0, V9X_G4_POINTERS_DWORDS));
    v9x_g4_emit(batch, st + V9X_G4_ST_VS);
    v9x_g4_emit(batch, 0ul);
    v9x_g4_emit(batch, 0ul);
    v9x_g4_emit(batch, st + V9X_G4_ST_SF);
    v9x_g4_emit(batch, st + V9X_G4_ST_WM);
    v9x_g4_emit(batch, st + V9X_G4_ST_CC);

    /* URB_FENCE may not cross a 64-byte line (xf86 gen4_emit_urb). */
    while ((batch->count & 15ul) > 12ul) {
        v9x_g4_emit(batch, V9X_G4_MI_NOOP);
    }
    vs_end = (v9x_u32)V9X_G4_URB_VS_ENTRIES * V9X_G4_URB_VS_SIZE;
    sf_end = vs_end + (v9x_u32)V9X_G4_URB_SF_ENTRIES * V9X_G4_URB_SF_SIZE;
    v9x_g4_emit(batch, V9X_G4_CMD(0, 0, 0, V9X_G4_URB_FENCE_DWORDS) |
                           V9X_G4_URB_REALLOC);
    v9x_g4_emit(batch, (vs_end << 20) | (vs_end << 10) | vs_end);
    v9x_g4_emit(batch, (sf_end << 20) | sf_end);
    v9x_g4_emit(batch, V9X_G4_CMD(0, 0, 1, V9X_G4_CS_URB_DWORDS));
    v9x_g4_emit(batch, 0ul);

    v9x_g4_emit(batch, V9X_G4_CMD(3, 1, 0, V9X_G4_RECT_DWORDS));
    v9x_g4_emit(batch, 0ul);
    v9x_g4_emit(batch, (((v9x_u32)draw->height - 1ul) << 16) |
                           ((v9x_u32)draw->width - 1ul));
    v9x_g4_emit(batch, 0ul);

    v9x_g4_emit(batch, V9X_G4_CMD(3, 1, 5, V9X_G4_DEPTH_DWORDS));
    if (draw->depth_address != 0ul) {
        v9x_g4_emit(batch, (V9X_G4_SURFTYPE_2D << 29) |
                               ((v9x_u32)draw->depth_format << 18) |
                               (draw->depth_pitch - 1ul));
        v9x_g4_emit(batch, draw->depth_address);
        v9x_g4_emit(batch, (((v9x_u32)draw->height - 1ul) << 19) |
                               (((v9x_u32)draw->width - 1ul) << 6));
    } else {
        v9x_g4_emit(batch, (V9X_G4_SURFTYPE_NULL << 29) |
                               ((v9x_u32)V9X_G4_DEPTH_D32_FLOAT << 18));
        v9x_g4_emit(batch, 0ul);
        v9x_g4_emit(batch, 0ul);
    }
    v9x_g4_emit(batch, 0ul);

    v9x_g4_emit(batch, V9X_G4_CMD(3, 0, 8, V9X_G4_VB_DWORDS));
    v9x_g4_emit(batch, (v9x_u32)V9X_G4_VB_PITCH);
    v9x_g4_emit(batch, draw->vertex_address);
    v9x_g4_emit(batch, draw->vertex_count - 1ul);
    v9x_g4_emit(batch, 0ul);

    v9x_g4_emit(batch, V9X_G4_CMD(3, 0, 9, V9X_G4_VE_DWORDS));
    /* Position at dword 4, and again at 8 for the SF's z and 1/w. */
    v9x_g4_vertex_element(batch, V9X_G4_FMT_R32G32B32_FLOAT, V9X_G4_VB_POS,
                          4ul, V9X_G4_VFCOMP_STORE_SRC,
                          V9X_G4_VFCOMP_STORE_SRC, V9X_G4_VFCOMP_STORE_SRC,
                          V9X_G4_VFCOMP_STORE_1_FP);
    v9x_g4_vertex_element(batch, V9X_G4_FMT_R32G32B32_FLOAT, V9X_G4_VB_POS,
                          8ul, V9X_G4_VFCOMP_STORE_SRC,
                          V9X_G4_VFCOMP_STORE_SRC, V9X_G4_VFCOMP_STORE_SRC,
                          V9X_G4_VFCOMP_STORE_1_FP);
    v9x_g4_vertex_element(batch, V9X_G4_FMT_B8G8R8A8_UNORM,
                          V9X_G4_VB_DIFFUSE, 12ul, V9X_G4_VFCOMP_STORE_SRC,
                          V9X_G4_VFCOMP_STORE_SRC, V9X_G4_VFCOMP_STORE_SRC,
                          V9X_G4_VFCOMP_STORE_SRC);
    v9x_g4_vertex_element(batch, V9X_G4_FMT_B8G8R8A8_UNORM,
                          V9X_G4_VB_SPECULAR, 16ul, V9X_G4_VFCOMP_STORE_SRC,
                          V9X_G4_VFCOMP_STORE_SRC, V9X_G4_VFCOMP_STORE_SRC,
                          V9X_G4_VFCOMP_STORE_SRC);
    v9x_g4_vertex_element(batch, V9X_G4_FMT_R32G32B32_FLOAT, V9X_G4_VB_TEX0,
                          20ul, V9X_G4_VFCOMP_STORE_SRC,
                          V9X_G4_VFCOMP_STORE_SRC, V9X_G4_VFCOMP_STORE_SRC,
                          V9X_G4_VFCOMP_STORE_1_FP);
    v9x_g4_vertex_element(batch, V9X_G4_FMT_R32G32B32A32_FLOAT, 0ul, 24ul,
                          V9X_G4_VFCOMP_STORE_0, V9X_G4_VFCOMP_STORE_0,
                          V9X_G4_VFCOMP_STORE_0, V9X_G4_VFCOMP_STORE_0);
    v9x_g4_vertex_element(batch, V9X_G4_FMT_R32G32B32A32_FLOAT, 0ul, 28ul,
                          V9X_G4_VFCOMP_STORE_0, V9X_G4_VFCOMP_STORE_0,
                          V9X_G4_VFCOMP_STORE_0, V9X_G4_VFCOMP_STORE_0);

    v9x_g4_emit(batch, V9X_G4_CMD(3, 3, 0, V9X_G4_PRIM_DWORDS) |
                           (V9X_G4_PRIM_TRILIST << 10));
    v9x_g4_emit(batch, draw->vertex_count);
    v9x_g4_emit(batch, 0ul);
    v9x_g4_emit(batch, 1ul);
    v9x_g4_emit(batch, 0ul);
    v9x_g4_emit(batch, 0ul);

    /* Render cache to memory before the breadcrumb that follows in the
     * ring says the draw is done. */
    v9x_g4_emit(batch, V9X_G4_MI_FLUSH);
    v9x_g4_emit(batch, V9X_G4_MI_BATCH_END);
    if ((batch->count & 1ul) != 0ul) {
        v9x_g4_emit(batch, V9X_G4_MI_NOOP);
    }
    return batch->overflow ? V9X_STATUS_INSUFFICIENT_MEMORY : V9X_STATUS_OK;
}

v9x_u32 v9x_g4_batch_start_header(void)
{
    return V9X_G4_MI_BATCH_START;
}

/* The length of a command this file emits, from its header; 0 for one it
 * does not. */
static v9x_u32 v9x_g4_command_length(v9x_u32 header)
{
    if (header == V9X_G4_MI_NOOP ||
        header == V9X_G4_MI_FLUSH ||
        header == (V9X_G4_MI_FLUSH | V9X_G4_MI_FLUSH_EXE) ||
        header == V9X_G4_MI_BATCH_END ||
        header == V9X_G4_PIPELINE_SELECT_3D) {
        return 1ul;
    }
    if (header == V9X_G4_CMD(0, 1, 1, V9X_G4_SBA_DWORDS)) {
        return V9X_G4_SBA_DWORDS;
    }
    if (header == (V9X_G4_CMD(0, 0, 0, V9X_G4_URB_FENCE_DWORDS) |
                   V9X_G4_URB_REALLOC)) {
        return V9X_G4_URB_FENCE_DWORDS;
    }
    if (header == V9X_G4_CMD(0, 0, 1, V9X_G4_CS_URB_DWORDS)) {
        return V9X_G4_CS_URB_DWORDS;
    }
    if (header == V9X_G4_CMD(3, 0, 0, V9X_G4_POINTERS_DWORDS)) {
        return V9X_G4_POINTERS_DWORDS;
    }
    if (header == V9X_G4_CMD(3, 0, 1, V9X_G4_BINDING_DWORDS)) {
        return V9X_G4_BINDING_DWORDS;
    }
    if (header == V9X_G4_CMD(3, 1, 0, V9X_G4_RECT_DWORDS)) {
        return V9X_G4_RECT_DWORDS;
    }
    if (header == V9X_G4_CMD(3, 1, 5, V9X_G4_DEPTH_DWORDS)) {
        return V9X_G4_DEPTH_DWORDS;
    }
    if (header == V9X_G4_CMD(3, 0, 8, V9X_G4_VB_DWORDS)) {
        return V9X_G4_VB_DWORDS;
    }
    if (header == V9X_G4_CMD(3, 0, 9, V9X_G4_VE_DWORDS)) {
        return V9X_G4_VE_DWORDS;
    }
    if (header == (V9X_G4_CMD(3, 3, 0, V9X_G4_PRIM_DWORDS) |
                   (V9X_G4_PRIM_TRILIST << 10))) {
        return V9X_G4_PRIM_DWORDS;
    }
    return 0ul;
}

v9x_u16 v9x_g4_batch_check(const v9x_u32 *dw, v9x_u32 count)
{
    v9x_u32 at = 0ul;
    v9x_u32 length;
    v9x_u16 ended = V9X_FALSE;

    if (dw == 0 || count == 0ul) {
        return V9X_FALSE;
    }
    while (at < count) {
        if (ended == V9X_TRUE) {
            /* Only the pad after MI_BATCH_BUFFER_END. */
            if (dw[at] != V9X_G4_MI_NOOP) {
                return V9X_FALSE;
            }
            ++at;
            continue;
        }
        length = v9x_g4_command_length(dw[at]);
        if (length == 0ul || at + length > count) {
            return V9X_FALSE;
        }
        /* URB_FENCE inside one 64-byte line. */
        if (length == V9X_G4_URB_FENCE_DWORDS &&
            dw[at] == (V9X_G4_CMD(0, 0, 0, V9X_G4_URB_FENCE_DWORDS) |
                       V9X_G4_URB_REALLOC) &&
            (at & 15ul) > 12ul) {
            return V9X_FALSE;
        }
        if (dw[at] == V9X_G4_MI_BATCH_END) {
            ended = V9X_TRUE;
        }
        at += length;
    }
    return ended;
}
