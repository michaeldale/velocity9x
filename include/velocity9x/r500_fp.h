/*
 * R500 (Radeon X1000 series) fragment programs: the six-dword instruction
 * words the R500's pixel shader takes through GA_US_VECTOR_INDEX /
 * GA_US_VECTOR_DATA, built from a small description of each instruction.
 *
 * Pure encoding, no I/O, so the same code serves a host test, the Win32
 * probe and, later, the HAL. Field layouts are Mesa 22.3's r300_reg.h
 * (src\gallium\drivers\r300, the R500_INST_* / R500_RGB_ADDR* /
 * R500_ALPHA_* / R500_ALU_RGB* / R500_ALU_RGBA* / R500_TEX_* defines);
 * tests\host\test_r500_fp.c holds the encoder to words generated from
 * those defines in the shapes xf86-video-ati 6.14.6's R500 composite
 * builds (radeon_exa_render.c:1975-2130).
 *
 * An ALU instruction is RGB = A * B + C and alpha = A * B + C (MAD; the
 * only op this encoder emits). Each of A, B and C picks one of three
 * source slots and a per-channel swizzle and a modifier; the three slots
 * name a temporary or a constant, separately for the RGB and alpha units.
 * The result goes to a temporary (write masks), to the output (output
 * masks), or both.
 *
 * Measured on silicon: the colour program below (v9x_r500_alu_init and
 * V9X_R500_ALU_OUT) drew exactly on M-5CAPM3's X1300
 * (docs\decisions\2026-11-21-rv515-3d-first-draw.md). Nothing else here
 * has run yet.
 */
#ifndef VELOCITY9X_R500_FP_H
#define VELOCITY9X_R500_FP_H

#include "velocity9x/status.h"

/* Instructions per program. The R500 holds 512; the programs this driver
 * needs are short. */
#define V9X_R500_FP_MAX_INSTRUCTIONS 16u
#define V9X_R500_FP_WORDS_PER_INST   6u

/* Temporaries an address can name (Mesa: the address field is 8 bits,
 * the R500 has 128 temporaries). */
#define V9X_R500_TEMP_MAX            127ul

/* An address that names a constant instead of a temporary. */
#define V9X_R500_ADDR_CONST          0x00000100ul

/* Swizzle selects, per channel: R500_ALU_*_SWIZ_*. */
#define V9X_R500_SWZ_R               0ul
#define V9X_R500_SWZ_G               1ul
#define V9X_R500_SWZ_B               2ul
#define V9X_R500_SWZ_A               3ul
#define V9X_R500_SWZ_ZERO            4ul
#define V9X_R500_SWZ_HALF            5ul
#define V9X_R500_SWZ_ONE             6ul

/* An RGB swizzle: three channel selects packed r | g << 3 | b << 6. */
#define V9X_R500_RGB_SWIZZLE(r, g, b) \
    ((v9x_u32)(r) | ((v9x_u32)(g) << 3) | ((v9x_u32)(b) << 6))
#define V9X_R500_RGB_IDENTITY \
    V9X_R500_RGB_SWIZZLE(V9X_R500_SWZ_R, V9X_R500_SWZ_G, V9X_R500_SWZ_B)
#define V9X_R500_RGB_AAA \
    V9X_R500_RGB_SWIZZLE(V9X_R500_SWZ_A, V9X_R500_SWZ_A, V9X_R500_SWZ_A)
#define V9X_R500_RGB_ONE \
    V9X_R500_RGB_SWIZZLE(V9X_R500_SWZ_ONE, V9X_R500_SWZ_ONE, \
                         V9X_R500_SWZ_ONE)
#define V9X_R500_RGB_ZERO \
    V9X_R500_RGB_SWIZZLE(V9X_R500_SWZ_ZERO, V9X_R500_SWZ_ZERO, \
                         V9X_R500_SWZ_ZERO)

/* Source modifiers: R500_*_MOD_*. */
#define V9X_R500_MOD_NOP             0ul
#define V9X_R500_MOD_NEG             1ul
#define V9X_R500_MOD_ABS             2ul
#define V9X_R500_MOD_NAB             3ul

/* Operand indexes into the A/B/C arrays. */
#define V9X_R500_OP_A                0u
#define V9X_R500_OP_B                1u
#define V9X_R500_OP_C                2u

/* ALU instruction flags. */
#define V9X_R500_ALU_OUT             0x00000001ul /* write the output */
#define V9X_R500_ALU_WRITE_RGB       0x00000002ul /* write temp dest.rgb */
#define V9X_R500_ALU_WRITE_ALPHA     0x00000004ul /* write temp dest.a */
#define V9X_R500_ALU_CLAMP           0x00000008ul /* clamp both to [0,1] */
#define V9X_R500_ALU_TEX_WAIT        0x00000010ul /* wait for texture loads */

/*
 * One ALU instruction. rgb_addr and alpha_addr: the three source slots, a
 * temporary index or a constant index with V9X_R500_ADDR_CONST. For each of
 * A, B, C: the slot it reads (0-2), its swizzle (RGB: packed three
 * selects; alpha: one select) and its modifier.
 */
struct v9x_r500_alu {
    v9x_u32 rgb_addr[3];
    v9x_u32 alpha_addr[3];
    v9x_u32 rgb_slot[3];
    v9x_u32 rgb_swizzle[3];
    v9x_u32 rgb_mod[3];
    v9x_u32 alpha_slot[3];
    v9x_u32 alpha_swizzle[3];
    v9x_u32 alpha_mod[3];
    v9x_u32 dest;
    v9x_u32 flags;
};

/* Texture instruction kinds. */
#define V9X_R500_TEX_LD              1ul
#define V9X_R500_TEX_KILL            2ul

struct v9x_r500_fp {
    v9x_u32 words[V9X_R500_FP_MAX_INSTRUCTIONS * V9X_R500_FP_WORDS_PER_INST];
    v9x_u32 count;          /* instructions */
    v9x_u32 max_temp;       /* highest temporary named, for US_PIXSIZE */
    v9x_status status;      /* the first refusal, or V9X_STATUS_OK */
};

void v9x_r500_fp_begin(struct v9x_r500_fp *fp);

/* A MOV of source slot 0 (temp 0): A = slot 0 RGB / A, B = one, C = zero,
 * all addresses temp 0, no flags, dest 0. Callers change what differs. */
void v9x_r500_alu_init(struct v9x_r500_alu *alu);

/* Appends one ALU instruction. A refusal (a field out of range, the
 * program full) is kept in fp->status and later calls do nothing. */
void v9x_r500_fp_alu(struct v9x_r500_fp *fp, const struct v9x_r500_alu *alu);

/*
 * Appends one texture instruction on texture unit `unit`. LD: dest.rgba =
 * sample(coords.st) with the result clamped, waiting for earlier loads
 * (TEX_SEM_WAIT) and acquiring the semaphore, as xf86's R500 composite
 * does. KILL: discard the pixel where any of coords.rgba is negative.
 */
void v9x_r500_fp_tex(struct v9x_r500_fp *fp, v9x_u32 kind, v9x_u32 unit,
                     v9x_u32 coords, v9x_u32 dest);

/* Marks the last instruction LAST. Returns fp->status; refuses an empty
 * program. */
v9x_status v9x_r500_fp_end(struct v9x_r500_fp *fp);

#endif
