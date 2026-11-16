/*
 * GCN 1 shaders for Oland; see include\velocity9x\si_shader.h.
 *
 * MUBUF (LLVM BUFInstructions.td, MUBUF_Real_gfx6_gfx7, and its SI
 * assembler tests): dword 0 is 111000 at 31:26, OP 24:18, ADDR64 15,
 * GLC 14, IDXEN 13, OFFEN 12, OFFSET 11:0; dword 1 SOFFSET 31:24, TFE 23,
 * SLC 22, SRSRC 20:16 (the first SGPR / 4), VDATA 15:8, VADDR 7:0.
 * buffer_load_dwordx4 is 0Eh on SI and CI. SOFFSET 80h is the inline
 * constant 0. The buffer resource the loads read is vi_3d.c's
 * (VI_GFX6_TL_RESOURCE_WORD3).
 */
#include "velocity9x/si_shader.h"
#include "velocity9x/vi_shader.h"
#include "velocity9x/cik_shader.h"

#define SI_MUBUF_PREFIX             0xe0000000ul
#define SI_MUBUF_OP_SHIFT           18u
#define SI_MUBUF_LOAD_DWORDX4       0x0eul
#define SI_MUBUF_OFFEN              0x00001000ul
#define SI_MUBUF_IDXEN              0x00002000ul
#define SI_MUBUF_SOFFSET_ZERO       0x80000000ul
#define SI_MUBUF_VDATA_SHIFT        8u
#define SI_MUBUF_SRSRC_SHIFT        16u

/* The GCN 3 program's fetch, which this replaces (vi_shader.c): the
 * shift (1 dword), the add, mov and addc of the address (3), the first
 * FLAT load (2), the add and addc of the second address (2), the second
 * FLAT load (2), and s_waitcnt (1). The shift and the wait are kept. */
#define SI_TL_VI_PROLOGUE           11ul
#define SI_TL_VI_FLAT_1             4ul
#define SI_TL_VI_FLAT_2             8ul
#define SI_TL_VI_WAITCNT            10ul
#define SI_FORMAT6_MASK             0xfc000000ul
#define SI_FLAT_PREFIX              0xdc000000ul
#define SI_SOPP_MASK                0xff800000ul
#define SI_SOPP_PREFIX              0xbf800000ul
#define SI_SOPP_WAITCNT             0x0cul

/* The VGPRs: v0 VertexID, v1 the byte offset, the loads into v3 and v9,
 * where the FLAT loads put them. s0-s3 the V#. */
#define SI_TL_OFFSET_VGPR           1ul
#define SI_TL_POSITION_VGPR         3ul
#define SI_TL_ATTRIBUTE_VGPR        9ul
#define SI_TL_RESOURCE_SGPR         0ul
#define SI_TL_SECOND_HALF           16ul

static void si_buffer_load_dwordx4(v9x_u32 vdata, v9x_u32 vaddr,
                                   v9x_u32 srsrc, v9x_u32 offset,
                                   v9x_u32 *out)
{
    out[0] = SI_MUBUF_PREFIX | (SI_MUBUF_LOAD_DWORDX4 << SI_MUBUF_OP_SHIFT) |
             SI_MUBUF_OFFEN | (offset & 0xffful);
    out[1] = SI_MUBUF_SOFFSET_ZERO | ((srsrc / 4ul) << SI_MUBUF_SRSRC_SHIFT) |
             (vdata << SI_MUBUF_VDATA_SHIFT) | vaddr;
}

/* The same indexed: VADDR is the record, the V#'s STRIDE its size. */
static void si_buffer_load_dwordx4_idxen(v9x_u32 vdata, v9x_u32 vaddr,
                                         v9x_u32 srsrc, v9x_u32 offset,
                                         v9x_u32 *out)
{
    out[0] = SI_MUBUF_PREFIX | (SI_MUBUF_LOAD_DWORDX4 << SI_MUBUF_OP_SHIFT) |
             SI_MUBUF_IDXEN | (offset & 0xffful);
    out[1] = SI_MUBUF_SOFFSET_ZERO | ((srsrc / 4ul) << SI_MUBUF_SRSRC_SHIFT) |
             (vdata << SI_MUBUF_VDATA_SHIFT) | vaddr;
}

v9x_status v9x_si_shader_tl_vs(v9x_u32 *out, v9x_u32 *count)
{
    v9x_u32 vi[V9X_SI_TL_VS_MAX];
    v9x_u32 vi_count;
    v9x_u32 kept;
    v9x_u32 index;
    v9x_u32 at;

    if (out == 0 || count == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    vi_count = v9x_vi_shader_tl_vs(vi);
    if (vi_count <= SI_TL_VI_PROLOGUE || vi_count > V9X_SI_TL_VS_MAX ||
        (vi[SI_TL_VI_FLAT_1] & SI_FORMAT6_MASK) != SI_FLAT_PREFIX ||
        (vi[SI_TL_VI_FLAT_2] & SI_FORMAT6_MASK) != SI_FLAT_PREFIX ||
        (vi[SI_TL_VI_WAITCNT] & SI_SOPP_MASK) != SI_SOPP_PREFIX ||
        ((vi[SI_TL_VI_WAITCNT] >> 16) & 0x7ful) != SI_SOPP_WAITCNT) {
        return V9X_STATUS_UNSUPPORTED;
    }

    /* The shift, then the wait and everything after it, as one GCN 3
     * program to translate: none of it is FLAT. */
    kept = 0ul;
    vi[kept++] = vi[0];
    for (index = SI_TL_VI_WAITCNT; index < vi_count; ++index) {
        vi[kept++] = vi[index];
    }
    if (v9x_si_shader_from_vi(vi, kept, 0) != V9X_STATUS_OK) {
        return V9X_STATUS_UNSUPPORTED;
    }

    at = 0ul;
    out[at++] = vi[0];
    si_buffer_load_dwordx4(SI_TL_POSITION_VGPR, SI_TL_OFFSET_VGPR,
                           SI_TL_RESOURCE_SGPR, 0ul, out + at);
    at += 2ul;
    si_buffer_load_dwordx4(SI_TL_ATTRIBUTE_VGPR, SI_TL_OFFSET_VGPR,
                           SI_TL_RESOURCE_SGPR, SI_TL_SECOND_HALF, out + at);
    at += 2ul;
    for (index = 1ul; index < kept; ++index) {
        out[at++] = vi[index];
    }
    *count = at;
    return at == V9X_SI_TL_VS_DWORDS ? V9X_STATUS_OK : V9X_STATUS_UNSUPPORTED;
}

/* The TL2 VS: the TL VS's words, the third load after its two (before
 * its s_waitcnt, so the wait covers it), parameter 3's export before its
 * last export, POS0's. v0 is still VertexID there: the shift wrote v1. */
#define SI_TL2_RESOURCE_SGPR        4ul
#define SI_TL2_VGPR                 17ul
#define SI_TL2_PARAM                3ul
#define SI_TL_LOADS_END             5ul
#define SI_EXP_PREFIX               0xf8000000ul

v9x_status v9x_si_shader_tl2_vs(v9x_u32 *out, v9x_u32 *count)
{
    v9x_u32 tl[V9X_SI_TL_VS_MAX];
    v9x_u32 exp[2];
    v9x_u32 tl_count = 0ul;
    v9x_u32 last_exp = 0ul;
    v9x_u32 at = 0ul;
    v9x_u32 index;

    if (out == 0 || count == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (v9x_si_shader_tl_vs(tl, &tl_count) != V9X_STATUS_OK) {
        return V9X_STATUS_UNSUPPORTED;
    }
    /* The last export is two dwords before s_endpgm. */
    if (tl_count < SI_TL_LOADS_END + 3ul ||
        (tl[tl_count - 3ul] & SI_FORMAT6_MASK) != SI_EXP_PREFIX) {
        return V9X_STATUS_UNSUPPORTED;
    }
    last_exp = tl_count - 3ul;
    v9x_vi_exp(V9X_VI_EXP_PARAM0 + SI_TL2_PARAM, 0, 0, SI_TL2_VGPR,
               SI_TL2_VGPR + 1ul, SI_TL2_VGPR + 2ul, SI_TL2_VGPR + 3ul, exp);
    if (v9x_si_shader_from_vi(exp, 2ul, 0) != V9X_STATUS_OK) {
        return V9X_STATUS_UNSUPPORTED;
    }
    for (index = 0ul; index < SI_TL_LOADS_END; ++index) {
        out[at++] = tl[index];
    }
    si_buffer_load_dwordx4_idxen(SI_TL2_VGPR, 0ul, SI_TL2_RESOURCE_SGPR, 0ul,
                                 out + at);
    at += 2ul;
    for (index = SI_TL_LOADS_END; index < last_exp; ++index) {
        out[at++] = tl[index];
    }
    out[at++] = exp[0];
    out[at++] = exp[1];
    for (index = last_exp; index < tl_count; ++index) {
        out[at++] = tl[index];
    }
    *count = at;
    return at == V9X_SI_TL2_VS_DWORDS ? V9X_STATUS_OK
                                      : V9X_STATUS_UNSUPPORTED;
}

/* The wide vertex: v0 is VertexID, the seven quads load into v1-v28 in
 * order, so quad q is v[1 + 4q]. The loads are SI words; the wait and
 * the exports a GCN 3 tail rewritten, as the TL VS's are. */
#define SI_WIDE_QUADS               7ul
#define SI_WIDE_FIRST_VGPR          1ul

v9x_status v9x_si_shader_wide_vs(v9x_u32 *out, v9x_u32 *count)
{
    v9x_u32 tail[2ul * V9X_SI_WIDE_PARAMS + 4ul];
    v9x_u32 tail_count = 0ul;
    v9x_u32 at = 0ul;
    v9x_u32 quad;
    v9x_u32 v;

    if (out == 0 || count == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (quad = 0ul; quad < SI_WIDE_QUADS; ++quad) {
        si_buffer_load_dwordx4_idxen(SI_WIDE_FIRST_VGPR + 4ul * quad, 0ul,
                                     SI_TL_RESOURCE_SGPR, 16ul * quad,
                                     out + at);
        at += 2ul;
    }

    tail[tail_count++] = v9x_vi_s_waitcnt(V9X_VI_WAITCNT_ALL);
    for (quad = 1ul; quad < SI_WIDE_QUADS; ++quad) {
        v = SI_WIDE_FIRST_VGPR + 4ul * quad;
        tail_count += v9x_vi_exp(V9X_VI_EXP_PARAM0 + quad - 1ul, 0, 0, v,
                                 v + 1ul, v + 2ul, v + 3ul,
                                 tail + tail_count);
    }
    tail_count += v9x_vi_exp(V9X_VI_EXP_POS0, 1, 0, SI_WIDE_FIRST_VGPR,
                             SI_WIDE_FIRST_VGPR + 1ul,
                             SI_WIDE_FIRST_VGPR + 2ul,
                             SI_WIDE_FIRST_VGPR + 3ul, tail + tail_count);
    tail[tail_count++] = V9X_VI_S_ENDPGM;
    if (v9x_si_shader_from_vi(tail, tail_count, 0) != V9X_STATUS_OK) {
        return V9X_STATUS_UNSUPPORTED;
    }
    for (v = 0ul; v < tail_count; ++v) {
        out[at++] = tail[v];
    }
    *count = at;
    return at == V9X_SI_WIDE_VS_DWORDS ? V9X_STATUS_OK
                                       : V9X_STATUS_UNSUPPORTED;
}
