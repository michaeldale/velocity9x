/*
 * R500 fragment-program encoder. See include\velocity9x\r500_fp.h for the
 * model and the sources; every shift and bit below is the Mesa 22.3
 * r300_reg.h define named beside it.
 */
#include "velocity9x/r500_fp.h"

/* Instruction word 0 (US_CMN_INST). */
#define V9X_R500_INST_TYPE_ALU       0x00000000ul /* R500_INST_TYPE_ALU */
#define V9X_R500_INST_TYPE_OUT       0x00000001ul /* R500_INST_TYPE_OUT */
#define V9X_R500_INST_TYPE_TEX       0x00000003ul /* R500_INST_TYPE_TEX */
#define V9X_R500_INST_TEX_SEM_WAIT   0x00000004ul /* R500_INST_TEX_SEM_WAIT */
#define V9X_R500_INST_LAST           0x00000100ul /* R500_INST_LAST */
#define V9X_R500_INST_RGB_WMASK      0x00003800ul /* R500_INST_RGB_WMASK_RGB */
#define V9X_R500_INST_ALPHA_WMASK    0x00004000ul /* R500_INST_ALPHA_WMASK */
#define V9X_R500_INST_RGB_OMASK      0x00038000ul /* R500_INST_RGB_OMASK_RGB */
#define V9X_R500_INST_ALPHA_OMASK    0x00040000ul /* R500_INST_ALPHA_OMASK */
#define V9X_R500_INST_RGB_CLAMP      0x00080000ul /* R500_INST_RGB_CLAMP */
#define V9X_R500_INST_ALPHA_CLAMP    0x00100000ul /* R500_INST_ALPHA_CLAMP */

/* Words 1 and 2 (RGB_ADDR, ALPHA_ADDR): three 9-bit slots, each an 8-bit
 * address and its CONST bit (R500_RGB_ADDR0/1/2, *_CONST at 8, 18, 28). */
#define V9X_R500_ADDR_SLOT_MASK      0x000001fful
#define V9X_R500_ADDR1_SHIFT         10
#define V9X_R500_ADDR2_SHIFT         20

/* Word 3 (RGB_INST). */
#define V9X_R500_RGB_SEL_A_SHIFT     0  /* R500_ALU_RGB_SEL_A_SHIFT */
#define V9X_R500_RGB_SWIZ_A_SHIFT    2  /* R500_ALU_RGB_R_SWIZ_A_* */
#define V9X_R500_RGB_MOD_A_SHIFT     11 /* R500_ALU_RGB_MOD_A_* */
#define V9X_R500_RGB_SEL_B_SHIFT     13 /* R500_ALU_RGB_SEL_B_SHIFT */
#define V9X_R500_RGB_SWIZ_B_SHIFT    15 /* R500_ALU_RGB_R_SWIZ_B_* */
#define V9X_R500_RGB_MOD_B_SHIFT     24 /* R500_ALU_RGB_MOD_B_* */

/* Word 4 (ALPHA_INST); OP 3:0 is MAD (0), TARGET 0. */
#define V9X_R500_ALPHA_ADDRD_SHIFT   4  /* R500_ALPHA_ADDRD */
#define V9X_R500_ALPHA_SEL_A_SHIFT   12 /* R500_ALPHA_SEL_A_SHIFT */
#define V9X_R500_ALPHA_SWIZ_A_SHIFT  14 /* R500_ALPHA_SWIZ_A_* */
#define V9X_R500_ALPHA_MOD_A_SHIFT   17 /* R500_ALPHA_MOD_A_* */
#define V9X_R500_ALPHA_SEL_B_SHIFT   19 /* R500_ALPHA_SEL_B_SHIFT */
#define V9X_R500_ALPHA_SWIZ_B_SHIFT  21 /* R500_ALPHA_SWIZ_B_* */
#define V9X_R500_ALPHA_MOD_B_SHIFT   24 /* R500_ALPHA_MOD_B_* */

/* Word 5 (RGBA_INST); OP 3:0 is MAD (0). */
#define V9X_R500_RGBA_ADDRD_SHIFT    4  /* R500_ALU_RGBA_ADDRD */
#define V9X_R500_RGBA_SEL_C_SHIFT    12 /* R500_ALU_RGBA_SEL_C_SHIFT */
#define V9X_R500_RGBA_SWIZ_C_SHIFT   14 /* R500_ALU_RGBA_R_SWIZ_* */
#define V9X_R500_RGBA_MOD_C_SHIFT    23 /* R500_ALU_RGBA_MOD_C_* */
#define V9X_R500_RGBA_ASEL_C_SHIFT   25 /* R500_ALU_RGBA_ALPHA_SEL_C_SHIFT */
#define V9X_R500_RGBA_ASWIZ_C_SHIFT  27 /* R500_ALU_RGBA_A_SWIZ_* */
#define V9X_R500_RGBA_AMOD_C_SHIFT   30 /* R500_ALU_RGBA_ALPHA_MOD_C_* */

/* Texture word 1 (TEX_INST). */
#define V9X_R500_TEX_ID_SHIFT        16         /* R500_TEX_ID */
#define V9X_R500_TEX_INST_SHIFT      22         /* R500_TEX_INST_* */
#define V9X_R500_TEX_SEM_ACQUIRE     0x02000000ul /* R500_TEX_SEM_ACQUIRE */
#define V9X_R500_TEX_IGNORE_UNCOVERED 0x04000000ul
/* Texture word 2 (TEX_ADDR): source at 6:0, S/T/R/Q swizzles at 9:8,
 * 11:10, 13:12, 15:14; destination at 22:16, R/G/B/A swizzles at 25:24 up. */
#define V9X_R500_TEX_DST_SHIFT       16           /* R500_TEX_DST_ADDR */
#define V9X_R500_TEX_SRC_ST          0x00000400ul /* S = R, T = G */
#define V9X_R500_TEX_SRC_STRQ        0x0000e400ul /* S = R ... Q = A */
#define V9X_R500_TEX_DST_RGBA        0xe4000000ul /* R = R ... A = A */
#define V9X_R500_TEX_UNIT_MAX        15ul

/* Field limits: a slot 0-2, a select 0-6, a modifier 0-3, a packed RGB
 * swizzle nine bits. */
#define V9X_R500_SLOT_MAX            2ul
#define V9X_R500_SWZ_MAX             V9X_R500_SWZ_ONE
#define V9X_R500_MOD_MAX             V9X_R500_MOD_NAB
#define V9X_R500_RGB_SWIZZLE_MASK    0x000001fful
#define V9X_R500_SWZ_FIELD_MASK      7ul

void v9x_r500_fp_begin(struct v9x_r500_fp *fp)
{
    fp->count = 0ul;
    fp->max_temp = 0ul;
    fp->status = V9X_STATUS_OK;
}

void v9x_r500_alu_init(struct v9x_r500_alu *alu)
{
    v9x_u32 index;

    for (index = 0ul; index < 3ul; ++index) {
        alu->rgb_addr[index] = 0ul;
        alu->alpha_addr[index] = 0ul;
        alu->rgb_slot[index] = 0ul;
        alu->rgb_mod[index] = V9X_R500_MOD_NOP;
        alu->alpha_slot[index] = 0ul;
        alu->alpha_mod[index] = V9X_R500_MOD_NOP;
    }
    alu->rgb_swizzle[V9X_R500_OP_A] = V9X_R500_RGB_IDENTITY;
    alu->rgb_swizzle[V9X_R500_OP_B] = V9X_R500_RGB_ONE;
    alu->rgb_swizzle[V9X_R500_OP_C] = V9X_R500_RGB_ZERO;
    alu->alpha_swizzle[V9X_R500_OP_A] = V9X_R500_SWZ_A;
    alu->alpha_swizzle[V9X_R500_OP_B] = V9X_R500_SWZ_ONE;
    alu->alpha_swizzle[V9X_R500_OP_C] = V9X_R500_SWZ_ZERO;
    alu->dest = 0ul;
    alu->flags = 0ul;
}

/* Records a refusal once; later appends see it and do nothing. */
static void v9x_r500_fp_refuse(struct v9x_r500_fp *fp, v9x_status status)
{
    if (fp->status == V9X_STATUS_OK) {
        fp->status = status;
    }
}

/* Non-zero for a usable address: a temporary in range, or a constant
 * whose index fits the 8-bit field. Updates max_temp for a temporary. */
static int v9x_r500_fp_address(struct v9x_r500_fp *fp, v9x_u32 address)
{
    v9x_u32 index = address & ~V9X_R500_ADDR_CONST;

    if ((address & ~V9X_R500_ADDR_SLOT_MASK) != 0ul) {
        return 0;
    }
    if ((address & V9X_R500_ADDR_CONST) != 0ul) {
        return 1;
    }
    if (index > V9X_R500_TEMP_MAX) {
        return 0;
    }
    if (index > fp->max_temp) {
        fp->max_temp = index;
    }
    return 1;
}

/* Non-zero when every channel select of a packed RGB swizzle is 0-6. */
static int v9x_r500_rgb_swizzle_ok(v9x_u32 swizzle)
{
    v9x_u32 channel;

    if ((swizzle & ~V9X_R500_RGB_SWIZZLE_MASK) != 0ul) {
        return 0;
    }
    for (channel = 0ul; channel < 3ul; ++channel) {
        if (((swizzle >> (channel * 3ul)) & V9X_R500_SWZ_FIELD_MASK) >
            V9X_R500_SWZ_MAX) {
            return 0;
        }
    }
    return 1;
}

/* Non-zero when the description's fields are all in range. */
static int v9x_r500_alu_valid(struct v9x_r500_fp *fp,
                              const struct v9x_r500_alu *alu)
{
    v9x_u32 index;

    if (alu->dest > V9X_R500_TEMP_MAX) {
        return 0;
    }
    for (index = 0ul; index < 3ul; ++index) {
        if (!v9x_r500_fp_address(fp, alu->rgb_addr[index]) ||
            !v9x_r500_fp_address(fp, alu->alpha_addr[index]) ||
            alu->rgb_slot[index] > V9X_R500_SLOT_MAX ||
            alu->alpha_slot[index] > V9X_R500_SLOT_MAX ||
            !v9x_r500_rgb_swizzle_ok(alu->rgb_swizzle[index]) ||
            alu->alpha_swizzle[index] > V9X_R500_SWZ_MAX ||
            alu->rgb_mod[index] > V9X_R500_MOD_MAX ||
            alu->alpha_mod[index] > V9X_R500_MOD_MAX) {
            return 0;
        }
    }
    return 1;
}

/* The next instruction's six words, or 0 when the program is full or
 * has been refused. */
static v9x_u32 *v9x_r500_fp_next(struct v9x_r500_fp *fp)
{
    v9x_u32 *words;
    v9x_u32 index;

    if (fp->status != V9X_STATUS_OK) {
        return 0;
    }
    if (fp->count >= V9X_R500_FP_MAX_INSTRUCTIONS) {
        v9x_r500_fp_refuse(fp, V9X_STATUS_INSUFFICIENT_MEMORY);
        return 0;
    }
    words = &fp->words[fp->count * V9X_R500_FP_WORDS_PER_INST];
    for (index = 0ul; index < V9X_R500_FP_WORDS_PER_INST; ++index) {
        words[index] = 0ul;
    }
    return words;
}

static v9x_u32 v9x_r500_pack_addresses(const v9x_u32 *addresses)
{
    return addresses[0] |
           (addresses[1] << V9X_R500_ADDR1_SHIFT) |
           (addresses[2] << V9X_R500_ADDR2_SHIFT);
}

void v9x_r500_fp_alu(struct v9x_r500_fp *fp, const struct v9x_r500_alu *alu)
{
    v9x_u32 *words;
    v9x_u32 inst;

    if (fp == 0 || alu == 0) {
        return;
    }
    if (fp->status != V9X_STATUS_OK) {
        return;
    }
    if (!v9x_r500_alu_valid(fp, alu)) {
        v9x_r500_fp_refuse(fp, V9X_STATUS_INVALID_ARGUMENT);
        return;
    }
    if ((alu->flags & (V9X_R500_ALU_WRITE_RGB | V9X_R500_ALU_WRITE_ALPHA)) !=
        0ul && alu->dest > fp->max_temp) {
        fp->max_temp = alu->dest;
    }
    words = v9x_r500_fp_next(fp);
    if (words == 0) {
        return;
    }

    /* Word 0: an OUT instruction when it writes the output, else ALU;
     * the masks and clamps the flags ask for. */
    inst = (alu->flags & V9X_R500_ALU_OUT) != 0ul ?
           V9X_R500_INST_TYPE_OUT | V9X_R500_INST_RGB_OMASK |
           V9X_R500_INST_ALPHA_OMASK :
           V9X_R500_INST_TYPE_ALU;
    if ((alu->flags & V9X_R500_ALU_WRITE_RGB) != 0ul) {
        inst |= V9X_R500_INST_RGB_WMASK;
    }
    if ((alu->flags & V9X_R500_ALU_WRITE_ALPHA) != 0ul) {
        inst |= V9X_R500_INST_ALPHA_WMASK;
    }
    if ((alu->flags & V9X_R500_ALU_CLAMP) != 0ul) {
        inst |= V9X_R500_INST_RGB_CLAMP | V9X_R500_INST_ALPHA_CLAMP;
    }
    if ((alu->flags & V9X_R500_ALU_TEX_WAIT) != 0ul) {
        inst |= V9X_R500_INST_TEX_SEM_WAIT;
    }
    words[0] = inst;
    words[1] = v9x_r500_pack_addresses(alu->rgb_addr);
    words[2] = v9x_r500_pack_addresses(alu->alpha_addr);

    /* Word 3: RGB operands A and B. */
    words[3] =
        (alu->rgb_slot[V9X_R500_OP_A] << V9X_R500_RGB_SEL_A_SHIFT) |
        (alu->rgb_swizzle[V9X_R500_OP_A] << V9X_R500_RGB_SWIZ_A_SHIFT) |
        (alu->rgb_mod[V9X_R500_OP_A] << V9X_R500_RGB_MOD_A_SHIFT) |
        (alu->rgb_slot[V9X_R500_OP_B] << V9X_R500_RGB_SEL_B_SHIFT) |
        (alu->rgb_swizzle[V9X_R500_OP_B] << V9X_R500_RGB_SWIZ_B_SHIFT) |
        (alu->rgb_mod[V9X_R500_OP_B] << V9X_R500_RGB_MOD_B_SHIFT);

    /* Word 4: the alpha MAD's destination and operands A and B. */
    words[4] =
        (alu->dest << V9X_R500_ALPHA_ADDRD_SHIFT) |
        (alu->alpha_slot[V9X_R500_OP_A] << V9X_R500_ALPHA_SEL_A_SHIFT) |
        (alu->alpha_swizzle[V9X_R500_OP_A] << V9X_R500_ALPHA_SWIZ_A_SHIFT) |
        (alu->alpha_mod[V9X_R500_OP_A] << V9X_R500_ALPHA_MOD_A_SHIFT) |
        (alu->alpha_slot[V9X_R500_OP_B] << V9X_R500_ALPHA_SEL_B_SHIFT) |
        (alu->alpha_swizzle[V9X_R500_OP_B] << V9X_R500_ALPHA_SWIZ_B_SHIFT) |
        (alu->alpha_mod[V9X_R500_OP_B] << V9X_R500_ALPHA_MOD_B_SHIFT);

    /* Word 5: the RGB MAD's destination and both units' operand C. */
    words[5] =
        (alu->dest << V9X_R500_RGBA_ADDRD_SHIFT) |
        (alu->rgb_slot[V9X_R500_OP_C] << V9X_R500_RGBA_SEL_C_SHIFT) |
        (alu->rgb_swizzle[V9X_R500_OP_C] << V9X_R500_RGBA_SWIZ_C_SHIFT) |
        (alu->rgb_mod[V9X_R500_OP_C] << V9X_R500_RGBA_MOD_C_SHIFT) |
        (alu->alpha_slot[V9X_R500_OP_C] << V9X_R500_RGBA_ASEL_C_SHIFT) |
        (alu->alpha_swizzle[V9X_R500_OP_C] << V9X_R500_RGBA_ASWIZ_C_SHIFT) |
        (alu->alpha_mod[V9X_R500_OP_C] << V9X_R500_RGBA_AMOD_C_SHIFT);
    ++fp->count;
}

void v9x_r500_fp_tex(struct v9x_r500_fp *fp, v9x_u32 kind, v9x_u32 unit,
                     v9x_u32 coords, v9x_u32 dest)
{
    v9x_u32 *words;

    if (fp == 0) {
        return;
    }
    if (fp->status != V9X_STATUS_OK) {
        return;
    }
    if ((kind != V9X_R500_TEX_LD && kind != V9X_R500_TEX_KILL) ||
        unit > V9X_R500_TEX_UNIT_MAX || coords > V9X_R500_TEMP_MAX ||
        dest > V9X_R500_TEMP_MAX) {
        v9x_r500_fp_refuse(fp, V9X_STATUS_INVALID_ARGUMENT);
        return;
    }
    words = v9x_r500_fp_next(fp);
    if (words == 0) {
        return;
    }
    if (coords > fp->max_temp) {
        fp->max_temp = coords;
    }
    if (kind == V9X_R500_TEX_LD) {
        if (dest > fp->max_temp) {
            fp->max_temp = dest;
        }
        words[0] = V9X_R500_INST_TYPE_TEX | V9X_R500_INST_TEX_SEM_WAIT |
                   V9X_R500_INST_RGB_WMASK | V9X_R500_INST_ALPHA_WMASK |
                   V9X_R500_INST_RGB_CLAMP | V9X_R500_INST_ALPHA_CLAMP;
        words[1] = (unit << V9X_R500_TEX_ID_SHIFT) |
                   (kind << V9X_R500_TEX_INST_SHIFT) |
                   V9X_R500_TEX_SEM_ACQUIRE | V9X_R500_TEX_IGNORE_UNCOVERED;
        words[2] = coords | V9X_R500_TEX_SRC_ST |
                   (dest << V9X_R500_TEX_DST_SHIFT) | V9X_R500_TEX_DST_RGBA;
    } else {
        words[0] = V9X_R500_INST_TYPE_TEX | V9X_R500_INST_TEX_SEM_WAIT;
        words[1] = (unit << V9X_R500_TEX_ID_SHIFT) |
                   (kind << V9X_R500_TEX_INST_SHIFT) |
                   V9X_R500_TEX_SEM_ACQUIRE;
        words[2] = coords | V9X_R500_TEX_SRC_STRQ;
    }
    ++fp->count;
}

v9x_status v9x_r500_fp_end(struct v9x_r500_fp *fp)
{
    if (fp == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (fp->status != V9X_STATUS_OK) {
        return fp->status;
    }
    if (fp->count == 0ul) {
        fp->status = V9X_STATUS_INVALID_STATE;
        return fp->status;
    }
    fp->words[(fp->count - 1ul) * V9X_R500_FP_WORDS_PER_INST] |=
        V9X_R500_INST_LAST;
    return V9X_STATUS_OK;
}
