/*
 * AMD Cayman shader words and the 3D bring-up's shaders. See
 * include\velocity9x\cm_shader.h for the contract and its sources.
 */
#include "velocity9x/cm_shader.h"
#include "velocity9x/eg_shader.h"

/* Field positions, Mesa r600 eg_sq.h, which Cayman shares. */
#define CM_CF_INST_SHIFT            22
#define CM_CF_BARRIER               0x80000000ul
#define CM_VTX_MEGA_COUNT_SHIFT     26
#define CM_VTX_SEL_X_SHIFT          9
#define CM_VTX_SEL_Y_SHIFT          12
#define CM_VTX_SEL_Z_SHIFT          15
#define CM_VTX_SEL_W_SHIFT          18
#define CM_VTX_FORMAT_SHIFT         22
#define CM_VTX_NUM_FORMAT_SHIFT     28
#define CM_VTX_FORMAT_COMP_SIGNED   0x40000000ul

/* Linux's Cayman blit keeps a mega-fetch count of 1 in every fetch's word
 * 0 and, as on Evergreen, NUM_FORMAT_ALL 2 (scaled) with signed
 * components, which change nothing for float formats. Kept as Linux has
 * them. */
#define CM_VTX_MEGA_COUNT_LINUX     1ul
#define CM_VTX_NUM_FORMAT_SCALED    2ul

/* The blit's layout: four CF slots (the fourth CF_END), so a VS's fetch
 * clause and a PS's ALU clause both start at dword 8, address 4. */
#define CM_CLAUSE_DWORD             8ul

v9x_u32 v9x_cm_cf_end(v9x_u32 *out)
{
    out[0] = 0ul;
    out[1] = CM_CF_BARRIER | (V9X_CM_CF_END << CM_CF_INST_SHIFT);
    return 2ul;
}

v9x_u32 v9x_cm_vtx_fetch(v9x_u32 dst_gpr, v9x_u32 offset,
                         v9x_u32 data_format, v9x_u32 sel_x, v9x_u32 sel_y,
                         v9x_u32 sel_z, v9x_u32 sel_w, v9x_u32 *out)
{
    out[0] = V9X_EG_VTX_FETCH |
             (CM_VTX_MEGA_COUNT_LINUX << CM_VTX_MEGA_COUNT_SHIFT);
    out[1] = dst_gpr | (sel_x << CM_VTX_SEL_X_SHIFT) |
             (sel_y << CM_VTX_SEL_Y_SHIFT) | (sel_z << CM_VTX_SEL_Z_SHIFT) |
             (sel_w << CM_VTX_SEL_W_SHIFT) |
             (data_format << CM_VTX_FORMAT_SHIFT) |
             (CM_VTX_NUM_FORMAT_SCALED << CM_VTX_NUM_FORMAT_SHIFT) |
             CM_VTX_FORMAT_COMP_SIGNED;
    out[2] = offset;
    out[3] = 0ul;
    return 4ul;
}

v9x_u32 v9x_cm_shader_blit_vs(v9x_u32 *out)
{
    v9x_u32 at = 0ul;

    /* Cayman has no vertex-fetch clause: the fetches run in a TC one. */
    at += v9x_eg_cf_clause(V9X_EG_CF_TC, CM_CLAUSE_DWORD / 2ul, 2ul,
                           out + at);
    at += v9x_eg_cf_export(V9X_EG_EXPORT_POS, V9X_EG_EXPORT_POS0, 1ul, 1ul,
                           0ul, 1ul, out + at);
    at += v9x_eg_cf_export(V9X_EG_EXPORT_PARAM, 0ul, 0ul, 1ul, 0ul, 0ul,
                           out + at);
    at += v9x_cm_cf_end(out + at);
    /* Position into R1, texture coordinate into R0: two floats each, z
     * and w filled 0 and 1. */
    at += v9x_cm_vtx_fetch(1ul, 0ul, V9X_EG_FMT_32_32_FLOAT, V9X_EG_SEL_X,
                           V9X_EG_SEL_Y, V9X_EG_SEL_0, V9X_EG_SEL_1,
                           out + at);
    at += v9x_cm_vtx_fetch(0ul, 8ul, V9X_EG_FMT_32_32_FLOAT, V9X_EG_SEL_X,
                           V9X_EG_SEL_Y, V9X_EG_SEL_0, V9X_EG_SEL_1,
                           out + at);
    return at;
}

/* Control-flow fields the relocator reads (Mesa eg_sq.h): a CF_ALU word 1
 * has bit 29 set (its 4-bit CF_INST is 8 or more), its count in 24:18 and
 * its address in word 0's 21:0; any other CF word 1 has its 8-bit CF_INST
 * in 29:22 and count in 15:10, its address in word 0's 23:0. Both count
 * less one. */
#define CM_CF_IS_ALU                0x20000000ul
#define CM_CF_EOP                   0x00200000ul
#define CM_CF_INST_MASK             0x3fc00000ul
#define CM_CF_COUNT_SHIFT           10
#define CM_CF_COUNT_MASK            0x3ful
#define CM_CF_ALU_COUNT_SHIFT       18
#define CM_CF_ALU_COUNT_MASK        0x7ful
#define CM_CF_ADDR_MASK             0x00fffffful
#define CM_CF_ALU_ADDR_MASK         0x003ffffful
#define CM_CF_SLOTS_MAX             16ul
/* Dwords: an ALU slot, a fetch; the alignment of each kind of clause. */
#define CM_ALU_SLOT_DWORDS          2ul
#define CM_FETCH_DWORDS             4ul
#define CM_ALU_ALIGN_DWORDS         2ul
#define CM_FETCH_ALIGN_DWORDS       4ul
/* The vertex-fetch fields Cayman drops: word 0's mega-fetch count (Linux
 * writes 1 there) and word 2's MEGA_FETCH. */
#define CM_VTX_MEGA_COUNT_MASK      0xfc000000ul
#define CM_VTX_MEGA_FETCH           0x00080000ul

#define CM_KIND_ALU                 0u
#define CM_KIND_TC                  1u
#define CM_KIND_VC                  2u
#define CM_KIND_EXPORT              3u

v9x_u32 v9x_cm_shader_from_eg(const v9x_u32 *eg, v9x_u32 eg_count,
                              v9x_u32 *out)
{
    unsigned int kind[CM_CF_SLOTS_MAX];
    v9x_u32 start[CM_CF_SLOTS_MAX];
    v9x_u32 dwords[CM_CF_SLOTS_MAX];
    v9x_u32 placed[CM_CF_SLOTS_MAX];
    v9x_u32 cf_count = 0ul;
    v9x_u32 slot;
    v9x_u32 w0;
    v9x_u32 w1;
    v9x_u32 cf_inst;
    v9x_u32 next;
    v9x_u32 index;
    v9x_u32 pick;
    v9x_u32 align;
    v9x_u32 done;

    /* The Evergreen control flow, to its end of program. */
    for (slot = 0ul; slot < CM_CF_SLOTS_MAX; ++slot) {
        if (slot * 2ul + 1ul >= eg_count) {
            return 0ul;
        }
        w0 = eg[slot * 2ul];
        w1 = eg[slot * 2ul + 1ul];
        placed[slot] = 0ul;
        if ((w1 & CM_CF_IS_ALU) != 0ul) {
            kind[slot] = CM_KIND_ALU;
            start[slot] = (w0 & CM_CF_ALU_ADDR_MASK) * 2ul;
            dwords[slot] = (((w1 >> CM_CF_ALU_COUNT_SHIFT) &
                             CM_CF_ALU_COUNT_MASK) + 1ul) * CM_ALU_SLOT_DWORDS;
        } else {
            cf_inst = (w1 & CM_CF_INST_MASK) >> 22;
            if (cf_inst == V9X_EG_CF_TC || cf_inst == V9X_EG_CF_VC) {
                kind[slot] = cf_inst == V9X_EG_CF_TC ? CM_KIND_TC :
                                                       CM_KIND_VC;
                start[slot] = (w0 & CM_CF_ADDR_MASK) * 2ul;
                dwords[slot] = (((w1 >> CM_CF_COUNT_SHIFT) &
                                 CM_CF_COUNT_MASK) + 1ul) * CM_FETCH_DWORDS;
            } else if (cf_inst == V9X_EG_CF_EXPORT ||
                       cf_inst == V9X_EG_CF_EXPORT_DONE) {
                kind[slot] = CM_KIND_EXPORT;
                start[slot] = 0ul;
                dwords[slot] = 0ul;
            } else {
                return 0ul;
            }
        }
        if (kind[slot] != CM_KIND_EXPORT &&
            start[slot] + dwords[slot] > eg_count) {
            return 0ul;
        }
        if ((w1 & CM_CF_EOP) != 0ul) {
            cf_count = slot + 1ul;
            break;
        }
    }
    if (cf_count == 0ul || (cf_count + 1ul) * 2ul > V9X_CM_SHADER_DWORDS_MAX) {
        return 0ul;
    }

    /* The CF list with CF_END after it, then the clauses in the order the
     * Evergreen shader held them, each on its boundary. */
    next = (cf_count + 1ul) * 2ul;
    for (index = 0ul; index < next; ++index) {
        out[index] = 0ul;
    }
    for (done = 0ul; done < cf_count; ++done) {
        pick = CM_CF_SLOTS_MAX;
        for (slot = 0ul; slot < cf_count; ++slot) {
            if (kind[slot] == CM_KIND_EXPORT || placed[slot] != 0ul) {
                continue;
            }
            if (pick == CM_CF_SLOTS_MAX || start[slot] < start[pick]) {
                pick = slot;
            }
        }
        if (pick == CM_CF_SLOTS_MAX) {
            break;
        }
        align = kind[pick] == CM_KIND_ALU ? CM_ALU_ALIGN_DWORDS :
                                            CM_FETCH_ALIGN_DWORDS;
        while ((next % align) != 0ul) {
            if (next >= V9X_CM_SHADER_DWORDS_MAX) {
                return 0ul;
            }
            out[next++] = 0ul;
        }
        if (next + dwords[pick] > V9X_CM_SHADER_DWORDS_MAX) {
            return 0ul;
        }
        for (index = 0ul; index < dwords[pick]; ++index) {
            out[next + index] = eg[start[pick] + index];
            if (kind[pick] == CM_KIND_VC) {
                if ((index % CM_FETCH_DWORDS) == 0ul) {
                    out[next + index] =
                        (out[next + index] & ~CM_VTX_MEGA_COUNT_MASK) |
                        (CM_VTX_MEGA_COUNT_LINUX << CM_VTX_MEGA_COUNT_SHIFT);
                } else if ((index % CM_FETCH_DWORDS) == 2ul) {
                    out[next + index] &= ~CM_VTX_MEGA_FETCH;
                }
            }
        }
        placed[pick] = 1ul;
        start[pick] = next;
        next += dwords[pick];
    }

    /* The CF words, each clause's address its new place, VC made TC, the
     * end of program left to CF_END. */
    for (slot = 0ul; slot < cf_count; ++slot) {
        w0 = eg[slot * 2ul];
        w1 = eg[slot * 2ul + 1ul] & ~CM_CF_EOP;
        if (kind[slot] == CM_KIND_ALU) {
            w0 = (w0 & ~CM_CF_ALU_ADDR_MASK) | (start[slot] / 2ul);
        } else if (kind[slot] != CM_KIND_EXPORT) {
            w0 = (w0 & ~CM_CF_ADDR_MASK) | (start[slot] / 2ul);
            if (kind[slot] == CM_KIND_VC) {
                w1 = (w1 & ~CM_CF_INST_MASK) |
                     (V9X_EG_CF_TC << CM_CF_INST_SHIFT);
            }
        }
        out[slot * 2ul] = w0;
        out[slot * 2ul + 1ul] = w1;
    }
    (void)v9x_cm_cf_end(out + cf_count * 2ul);
    return next;
}

v9x_u32 v9x_cm_shader_blit_ps(v9x_u32 *out)
{
    v9x_u32 at = 0ul;
    v9x_u32 slot;

    at += v9x_eg_cf_alu(CM_CLAUSE_DWORD / 2ul, 4ul, out + at);
    at += v9x_eg_cf_clause(V9X_EG_CF_TC, 8ul, 1ul, out + at);
    at += v9x_eg_cf_export(V9X_EG_EXPORT_PIXEL, 0ul, 0ul, 1ul, 0ul, 1ul,
                           out + at);
    at += v9x_cm_cf_end(out + at);
    /* The ALU clause fills dwords 8 to 15, so the texture clause at
     * address 8 needs no padding. */
    for (slot = 0ul; slot < 4ul; ++slot) {
        at += v9x_eg_alu_interp(V9X_EG_ALU_INTERP_XY, slot, 0ul, 0ul,
                                slot < 2ul, slot == 3ul, out + at);
    }
    at += v9x_eg_tex_sample(0ul, 0ul, out + at);
    return at;
}
