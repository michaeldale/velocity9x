/*
 * GCN 3 to GCN 2 shader translation for Mullins; see
 * include\velocity9x\cik_shader.h for what changes and where each mapping
 * comes from.
 */
#include "velocity9x/cik_shader.h"

/* Encoding prefixes, the bits that name a format. */
#define CIK_VOP2_MASK       0x80000000ul    /* bit 31 clear */
#define CIK_VOP1_MASK       0xfe000000ul
#define CIK_VOP1_PREFIX     0x7e000000ul
#define CIK_VOPC_PREFIX     0x7c000000ul
#define CIK_SOP1_MASK       0xff800000ul
#define CIK_SOP1_PREFIX     0xbe800000ul
#define CIK_SOPP_PREFIX     0xbf800000ul
#define CIK_FORMAT6_MASK    0xfc000000ul
#define CIK_VOP3_PREFIX     0xd0000000ul
#define CIK_VI_EXP_PREFIX   0xc4000000ul
#define CIK_CI_EXP_PREFIX   0xf8000000ul
#define CIK_VI_VINTRP       0xd4000000ul
#define CIK_CI_VINTRP       0xc8000000ul
#define CIK_FLAT_PREFIX     0xdc000000ul
#define CIK_MIMG_PREFIX     0xf0000000ul

/* src0 of 0FFh: a 32-bit literal follows the instruction. */
#define CIK_LITERAL         0x0fful

/* VOP3: VI op 25:16 and clamp 15; CI op 25:17 and clamp 11. */
#define CIK_VI_VOP3_OP_SHIFT 16u
#define CIK_VI_VOP3_OP_MASK  0x3fful
#define CIK_VI_VOP3_CLAMP    0x00008000ul
#define CIK_CI_VOP3_OP_SHIFT 17u
#define CIK_CI_VOP3_CLAMP    0x00000800ul
#define CIK_VOP3_KEEP        0x000007fful   /* abs 10:8, vdst 7:0 */

/* FLAT: op 24:18. */
#define CIK_FLAT_OP_SHIFT    18u
#define CIK_FLAT_OP_MASK     0x7ful

/* MIMG: op 24:18; only image_sample (20h) is used. */
#define CIK_MIMG_SAMPLE      0x20ul

struct cik_op_map {
    v9x_u32 vi;
    v9x_u32 ci;
};

/* VOP2Instructions.td: VOP2_Real_e32e64_vi (or _vi_only) against
 * VOP2_Real_gfx6_gfx7 (VOP2be_ for the carry ops). */
static const struct cik_op_map cik_vop2[] = {
    { 0x00ul, 0x00ul },     /* v_cndmask_b32 */
    { 0x01ul, 0x03ul },     /* v_add_f32 */
    { 0x03ul, 0x05ul },     /* v_subrev_f32 */
    { 0x05ul, 0x08ul },     /* v_mul_f32 */
    { 0x12ul, 0x1aul },     /* v_lshlrev_b32 */
    { 0x13ul, 0x1bul },     /* v_and_b32 */
    { 0x16ul, 0x1ful },     /* v_mac_f32 */
    { 0x19ul, 0x25ul },     /* VI v_add_u32 is CI v_add_i32 */
    { 0x1cul, 0x28ul }      /* v_addc_u32 */
};

/* VOP1Instructions.td: identical on both; listed so nothing else gets
 * through. */
static const struct cik_op_map cik_vop1[] = {
    { 0x01ul, 0x01ul },     /* v_mov_b32 */
    { 0x11ul, 0x11ul },     /* v_cvt_f32_ubyte0 */
    { 0x12ul, 0x12ul },     /* v_cvt_f32_ubyte1 */
    { 0x13ul, 0x13ul },     /* v_cvt_f32_ubyte2 */
    { 0x14ul, 0x14ul }      /* v_cvt_f32_ubyte3 */
};

/* VOP3Instructions.td / VOP2Instructions.td: VI's VOP3-only
 * v_cvt_pkrtz_f16_f32 (296h) is CI's VOP2 2Fh, which VOP3 encodes at
 * 100h + 2Fh. */
static const struct cik_op_map cik_vop3[] = {
    { 0x296ul, 0x12ful }    /* v_cvt_pkrtz_f16_f32 */
};

/* SOPInstructions.td: SOP1_Real_vi against SOP1_Real_gfx6_gfx7. */
static const struct cik_op_map cik_sop1[] = {
    { 0x00ul, 0x03ul },     /* s_mov_b32 */
    { 0x01ul, 0x04ul },     /* s_mov_b64 */
    { 0x07ul, 0x0aul }      /* s_wqm_b64 */
};

/* SOPP: the four the shaders use, the same numbers on both. */
static const struct cik_op_map cik_sopp[] = {
    { 0x00ul, 0x00ul },     /* s_nop */
    { 0x01ul, 0x01ul },     /* s_endpgm */
    { 0x08ul, 0x08ul },     /* s_cbranch_execz */
    { 0x0cul, 0x0cul }      /* s_waitcnt */
};

/* FLATInstructions.td: FLAT_Real_vi against FLAT_Real_ci. */
static const struct cik_op_map cik_flat[] = {
    { 0x1cul, 0x1cul },     /* flat_store_dword */
    { 0x17ul, 0x0eul }      /* flat_load_dwordx4 */
};

#define CIK_COUNT(table) (sizeof(table) / sizeof(table[0]))

/* The CI opcode for vi, or 0xFFFFFFFF if the table has none. */
static v9x_u32 cik_map(const struct cik_op_map *table, v9x_u32 entries,
                       v9x_u32 vi)
{
    v9x_u32 index;

    for (index = 0ul; index < entries; ++index) {
        if (table[index].vi == vi) {
            return table[index].ci;
        }
    }
    return 0xfffffffful;
}

/*
 * One instruction at words[at]. Returns its length in dwords (literal
 * included), or 0 if it cannot be translated. remaining is how many
 * words are left from at.
 */
static v9x_u32 cik_one(v9x_u32 *words, v9x_u32 remaining)
{
    v9x_u32 word = words[0];
    v9x_u32 op;
    v9x_u32 ci;

    /* VOPC: 0111110, op 24:17. VI's CMPX_*_F32 run 50h-5Fh is CI's
     * 10h-1Fh in the same order; v_cmp_eq_u32 is CAh on VI, C2h on CI. */
    if ((word & CIK_VOP1_MASK) == CIK_VOPC_PREFIX) {
        op = (word >> 17) & 0xfful;
        if (op >= 0x50ul && op <= 0x5ful) {
            ci = op - 0x40ul;
        } else if (op == 0xcaul) {
            ci = 0xc2ul;
        } else {
            return 0ul;
        }
        words[0] = (word & ~(0xfful << 17)) | (ci << 17);
        return (word & 0x1fful) == CIK_LITERAL ? 2ul : 1ul;
    }

    /* VOP1: 0111111, op 16:9. */
    if ((word & CIK_VOP1_MASK) == CIK_VOP1_PREFIX) {
        op = (word >> 9) & 0xfful;
        ci = cik_map(cik_vop1, CIK_COUNT(cik_vop1), op);
        if (ci == 0xfffffffful) {
            return 0ul;
        }
        words[0] = (word & ~(0xfful << 9)) | (ci << 9);
        return (word & 0x1fful) == CIK_LITERAL ? 2ul : 1ul;
    }

    /* VOP2: bit 31 clear, op 30:25. */
    if ((word & CIK_VOP2_MASK) == 0ul) {
        op = (word >> 25) & 0x3ful;
        ci = cik_map(cik_vop2, CIK_COUNT(cik_vop2), op);
        if (ci == 0xfffffffful) {
            return 0ul;
        }
        words[0] = (word & ~(0x3ful << 25)) | (ci << 25);
        return (word & 0x1fful) == CIK_LITERAL ? 2ul : 1ul;
    }

    /* SOPP: 101111111, op 22:16. */
    if ((word & CIK_SOP1_MASK) == CIK_SOPP_PREFIX) {
        op = (word >> 16) & 0x7ful;
        if (cik_map(cik_sopp, CIK_COUNT(cik_sopp), op) == 0xfffffffful) {
            return 0ul;
        }
        return 1ul;
    }

    /* SOP1: 101111101, op 15:8. */
    if ((word & CIK_SOP1_MASK) == CIK_SOP1_PREFIX) {
        op = (word >> 8) & 0xfful;
        ci = cik_map(cik_sop1, CIK_COUNT(cik_sop1), op);
        if (ci == 0xfffffffful) {
            return 0ul;
        }
        words[0] = (word & ~(0xfful << 8)) | (ci << 8);
        return (word & 0xfful) == CIK_LITERAL ? 2ul : 1ul;
    }

    /* VINTRP: one dword, the prefix only. */
    if ((word & CIK_FORMAT6_MASK) == CIK_VI_VINTRP) {
        words[0] = (word & ~CIK_FORMAT6_MASK) | CIK_CI_VINTRP;
        return 1ul;
    }

    /* The rest are two dwords. */
    if (remaining < 2ul) {
        return 0ul;
    }

    if ((word & CIK_FORMAT6_MASK) == CIK_VI_EXP_PREFIX) {
        words[0] = (word & ~CIK_FORMAT6_MASK) | CIK_CI_EXP_PREFIX;
        return 2ul;
    }

    /* VOP3: 110100 (D0h-D3h). */
    if ((word & CIK_FORMAT6_MASK) == CIK_VOP3_PREFIX) {
        op = (word >> CIK_VI_VOP3_OP_SHIFT) & CIK_VI_VOP3_OP_MASK;
        ci = cik_map(cik_vop3, CIK_COUNT(cik_vop3), op);
        if (ci == 0xfffffffful) {
            return 0ul;
        }
        words[0] = CIK_VOP3_PREFIX | (ci << CIK_CI_VOP3_OP_SHIFT) |
                   ((word & CIK_VI_VOP3_CLAMP) != 0ul ? CIK_CI_VOP3_CLAMP : 0ul) |
                   (word & CIK_VOP3_KEEP);
        return 2ul;
    }

    if ((word & CIK_FORMAT6_MASK) == CIK_FLAT_PREFIX) {
        op = (word >> CIK_FLAT_OP_SHIFT) & CIK_FLAT_OP_MASK;
        ci = cik_map(cik_flat, CIK_COUNT(cik_flat), op);
        if (ci == 0xfffffffful) {
            return 0ul;
        }
        words[0] = (word & ~(CIK_FLAT_OP_MASK << CIK_FLAT_OP_SHIFT)) |
                   (ci << CIK_FLAT_OP_SHIFT);
        return 2ul;
    }

    if ((word & CIK_FORMAT6_MASK) == CIK_MIMG_PREFIX) {
        if (((word >> 18) & 0x7ful) != CIK_MIMG_SAMPLE) {
            return 0ul;
        }
        return 2ul;
    }

    return 0ul;
}

v9x_status v9x_cik_shader_from_vi(v9x_u32 *words, v9x_u32 count,
                                  v9x_u32 *failed_at)
{
    v9x_u32 at = 0ul;
    v9x_u32 length;

    if (words == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    while (at < count) {
        length = cik_one(words + at, count - at);
        if (length == 0ul || length > count - at) {
            if (failed_at != 0) {
                *failed_at = at;
            }
            return V9X_STATUS_UNSUPPORTED;
        }
        at += length;
    }
    return V9X_STATUS_OK;
}
