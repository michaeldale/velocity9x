/*
 * A general encoder for the Gen3 (i915) fragment program: step 2 of
 * docs\plans\gen3-ps11-pixel-shaders.md.
 *
 * The fixed programs (i9xx_fragprog.c, i9xx_multitex.c, i9xx_fog.c) write
 * their dwords by hand. This writes any declaration, arithmetic or texture
 * instruction from operands, and keeps the program's counts against the
 * limits both sources state, so a translator can be refused instead of
 * emitting a program the hardware would mis-run.
 *
 * Field placements are those intel_gen3_3d.h already carries (audited
 * 2026-09-14 and 2026-09-16), plus the ones defined below, each read
 * from both xf86-video-intel 2.20.19 src\i915_reg.h and Mesa gallium
 * drivers\i915\i915_reg.h under build\external\audit\, which agree on
 * every one. Not validated on hardware.
 *
 * Pure: no MMIO, no allocation. Host-tested in tests\host\test_i9xx_fs.c
 * against the fixed programs' dwords and a host model of the program
 * (tests\host\i9xx_fs_sim.c).
 */
#ifndef VELOCITY9X_I9XX_FS_H
#define VELOCITY9X_I9XX_FS_H

#include "velocity9x/intel_gen3_3d.h"

/* Register types beyond those intel_gen3_3d.h defines (R 0, T 1, CONST 2,
 * S 3, OC 4). OD is the depth output; U the four unpreserved temporaries,
 * which "do not retain their values between phases" (both sources). */
#define V9X_I9XX_FS_REG_TYPE_OD          ((v9x_u32)5ul)
#define V9X_I9XX_FS_REG_TYPE_U           ((v9x_u32)6ul)

/* T10 carries the interpolated fog factor in w. */
#define V9X_I9XX_FS_T_FOG                ((v9x_u32)10ul)

/* Register counts: I915_MAX_TEMPORARY 16, 32 constants, T0-T10, U0-U3. */
#define V9X_I9XX_FS_R_COUNT              16u
#define V9X_I9XX_FS_CONST_COUNT          32u
#define V9X_I9XX_FS_T_COUNT              11u
#define V9X_I9XX_FS_U_COUNT              4u
#define V9X_I9XX_FS_S_COUNT              16u

/* Program limits, I915_MAX_ALU_INSN, _TEX_INSN, _DECL_INSN and
 * _TEX_INDIRECT in Mesa; the same numbers in xf86's comments. */
#define V9X_I9XX_FS_MAX_ALU              64u
#define V9X_I9XX_FS_MAX_TEX              32u
#define V9X_I9XX_FS_MAX_DCL              27u
#define V9X_I9XX_FS_MAX_PHASES           4u

/* Arithmetic opcodes (A0 bits 24-28). MOV, MUL, MAD and ADD are in
 * intel_gen3_3d.h already and are not spelt twice. MOD and TRC exist in
 * both headers; nothing here uses them. */
#define V9X_I9XX_FS_A0_DP2ADD            ((v9x_u32)0x05000000ul)
#define V9X_I9XX_FS_A0_DP3               ((v9x_u32)0x06000000ul)
#define V9X_I9XX_FS_A0_DP4               ((v9x_u32)0x07000000ul)
#define V9X_I9XX_FS_A0_FRC               ((v9x_u32)0x08000000ul)
#define V9X_I9XX_FS_A0_RCP               ((v9x_u32)0x09000000ul)
#define V9X_I9XX_FS_A0_RSQ               ((v9x_u32)0x0a000000ul)
#define V9X_I9XX_FS_A0_EXP               ((v9x_u32)0x0b000000ul)
#define V9X_I9XX_FS_A0_LOG               ((v9x_u32)0x0c000000ul)
#define V9X_I9XX_FS_A0_CMP               ((v9x_u32)0x0d000000ul)
#define V9X_I9XX_FS_A0_MIN               ((v9x_u32)0x0e000000ul)
#define V9X_I9XX_FS_A0_MAX               ((v9x_u32)0x0f000000ul)
#define V9X_I9XX_FS_A0_FLR               ((v9x_u32)0x10000000ul)
#define V9X_I9XX_FS_A0_SGE               ((v9x_u32)0x13000000ul)
#define V9X_I9XX_FS_A0_SLT               ((v9x_u32)0x14000000ul)
#define V9X_I9XX_FS_A0_OPCODE_MASK       ((v9x_u32)0x1f000000ul)

/* A0_DEST_SATURATE: the result clamped to [0, 1]. */
#define V9X_I9XX_FS_A0_SATURATE          ((v9x_u32)0x00400000ul)

/* Texture opcodes. TEXLD is in intel_gen3_3d.h. TEXLDP divides the
 * address by its w; TEXLDB biases the level of detail by w; TEXKILL
 * samples nothing and kills the pixel "if any channel of the address
 * register is < 0.0" (xf86), or of x, y and z only when
 * _3DSTATE_RASTER_RULES selects TEXKILL_3D. Both drivers select 4D; this
 * tree does not send that packet, so the state is the hardware's default,
 * not known. The PS 1.1 translator moves w to one before TEXKILL so that
 * either reading gives the same answer. */
#define V9X_I9XX_T0_TEXLDP               ((v9x_u32)0x16000000ul)
#define V9X_I9XX_T0_TEXLDB               ((v9x_u32)0x17000000ul)
#define V9X_I9XX_T0_TEXKILL              ((v9x_u32)0x18000000ul)

/* A sampler declaration's sample type, D0 bits 22-23. */
#define V9X_I9XX_FS_D0_SAMPLE_2D         ((v9x_u32)0x00000000ul)
#define V9X_I9XX_FS_D0_SAMPLE_CUBE       ((v9x_u32)0x00400000ul)
#define V9X_I9XX_FS_D0_SAMPLE_VOLUME     ((v9x_u32)0x00800000ul)

/* Write and declaration masks, as four bits; the encoder shifts them to
 * bit 10. */
#define V9X_I9XX_FS_MASK_X               1u
#define V9X_I9XX_FS_MASK_Y               2u
#define V9X_I9XX_FS_MASK_Z               4u
#define V9X_I9XX_FS_MASK_W               8u
#define V9X_I9XX_FS_MASK_XYZ             7u
#define V9X_I9XX_FS_MASK_XYZW            15u

/* Swizzle selectors, SRC_X to SRC_ONE in both sources. */
#define V9X_I9XX_FS_SEL_X                0u
#define V9X_I9XX_FS_SEL_Y                1u
#define V9X_I9XX_FS_SEL_Z                2u
#define V9X_I9XX_FS_SEL_W                3u
#define V9X_I9XX_FS_SEL_ZERO             4u
#define V9X_I9XX_FS_SEL_ONE              5u

/*
 * A source operand in one word, this encoder's own packing (not a
 * hardware format): register number in bits 0-4, type in 5-7, then one
 * nibble a channel from bit 8 - a three-bit selector and, above it, that
 * channel's negate. V9X_I9XX_FS_SRC is the register unswizzled, xyzw.
 */
#define V9X_I9XX_FS_SRC_NR_MASK          0x1ful
#define V9X_I9XX_FS_SRC_TYPE_SHIFT       5u
#define V9X_I9XX_FS_SRC_CHANNEL_SHIFT    8u
#define V9X_I9XX_FS_SRC_IDENTITY         ((v9x_u32)0x00321000ul)
#define V9X_I9XX_FS_SRC(type, nr) \
    ((v9x_u32)(((v9x_u32)(nr) & V9X_I9XX_FS_SRC_NR_MASK) | \
               ((v9x_u32)(type) << V9X_I9XX_FS_SRC_TYPE_SHIFT) | \
               V9X_I9XX_FS_SRC_IDENTITY))

/* The source reselected: each argument picks one of src's own channels
 * (X to W, carrying that channel's negate) or a literal ZERO or ONE. */
v9x_u32 v9x_i9xx_fs_swizzle(v9x_u32 src, v9x_u32 x, v9x_u32 y,
                            v9x_u32 z, v9x_u32 w);

/* The source with the channels in mask (V9X_I9XX_FS_MASK_*) negated, or
 * un-negated if they were. */
v9x_u32 v9x_i9xx_fs_negate(v9x_u32 src, v9x_u32 mask);

/*
 * The encoder. stream[0] is kept for the _3DSTATE_PIXEL_SHADER_PROGRAM
 * header, written by v9x_i9xx_fs_end. Declarations come first, as both
 * drivers emit them; a declaration after an instruction is refused.
 *
 * Every call after the first failure does nothing, and end returns that
 * failure: V9X_STATUS_INSUFFICIENT_MEMORY for the stream's capacity,
 * V9X_STATUS_UNSUPPORTED for a program over a hardware limit (counts,
 * phases, two constant registers in one instruction), and
 * V9X_STATUS_INVALID_ARGUMENT for a malformed instruction (a register
 * read before it is declared or written, a bad destination, a scalar
 * operation whose source is not one channel replicated).
 */
struct v9x_i9xx_fs {
    v9x_u32 *stream;
    v9x_u32 capacity;
    v9x_u32 at;
    v9x_u32 alu;
    v9x_u32 tex;
    v9x_u32 dcl;
    v9x_u32 phases;                  /* texture indirections, from 1 */
    v9x_u32 r_phase[V9X_I9XX_FS_R_COUNT];
    v9x_u32 u_phase[V9X_I9XX_FS_U_COUNT];
    v9x_u32 r_written;               /* one bit an R register */
    v9x_u32 u_written;
    v9x_u32 t_declared;              /* one bit a T register */
    v9x_u32 s_declared;
    v9x_u32 body;                   /* an instruction has been emitted */
    v9x_status failed;
};

void v9x_i9xx_fs_begin(struct v9x_i9xx_fs *fs, v9x_u32 *stream,
                       v9x_u32 capacity);

/* Declare T register nr with a channel mask (one of x, xy, xyz, w, xyzw:
 * the erratum both sources record), or sampler nr with its
 * V9X_I9XX_FS_D0_SAMPLE_* type. A second declaration of the same
 * register is not emitted. */
void v9x_i9xx_fs_dcl_t(struct v9x_i9xx_fs *fs, v9x_u32 nr, v9x_u32 mask);
void v9x_i9xx_fs_dcl_s(struct v9x_i9xx_fs *fs, v9x_u32 nr,
                       v9x_u32 sample_type);

/* An arithmetic instruction: opcode V9X_I9XX_FS_A0_*, destination R, U,
 * OC or OD with a write mask, saturate V9X_TRUE or V9X_FALSE, and as many
 * sources as the opcode takes (the rest are ignored and encoded zero). */
void v9x_i9xx_fs_alu(struct v9x_i9xx_fs *fs, v9x_u32 opcode,
                     v9x_u32 dest_type, v9x_u32 dest_nr, v9x_u32 mask,
                     v9x_u32 saturate,
                     v9x_u32 src0, v9x_u32 src1, v9x_u32 src2);

/* A texture instruction: opcode V9X_I9XX_T0_*, destination R, U, OC or
 * OD (all four channels: the instruction has no write mask), a declared
 * sampler (ignored for TEXKILL), and the address register, T or R or U,
 * unswizzled. */
void v9x_i9xx_fs_tex(struct v9x_i9xx_fs *fs, v9x_u32 opcode,
                     v9x_u32 dest_type, v9x_u32 dest_nr, v9x_u32 sampler,
                     v9x_u32 addr_type, v9x_u32 addr_nr);

/* Write the header and return the program's length in dwords, header
 * included, or the first failure. */
v9x_status v9x_i9xx_fs_end(struct v9x_i9xx_fs *fs, v9x_u32 *written);

#endif
