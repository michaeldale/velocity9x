/*
 * Gen4 (965 / GM965) EU instruction encoding, and the two kernels a
 * Direct3D draw needs: the SF setup kernel and the WM pixel kernel.
 *
 * docs\plans\intel-gen4-gen5-bringup.md, Phase 2. Pure encoding, no I/O.
 * Each 128-bit native instruction is built from the Gen4 columns of Mesa's
 * brw_inst.h field table (src\intel\compiler, Mesa 24.0.9), which is the
 * machine-readable form of 965 PRM Vol 4 "EU ISA"; the host tests check the
 * encoder against xf86-video-intel's Gen4 kernel binaries (render_program
 * exa_*.g4b), which ran on 965 hardware.
 *
 * Gen4 differs from the Haswell encoder beside it (intel_hsw_eu.h) where it
 * matters here: send names its shared function in DW3 27:24 with the
 * message length at 23:20 and the response length at 19:16, the message
 * register base is in DW0 27:24, and math is a message to a shared
 * function, not an instruction. There is no 3-source form, no pln, and
 * this encoder does not rely on the implicit accumulator (no mac).
 *
 * Nothing in this file has executed on hardware.
 */
#ifndef VELOCITY9X_INTEL_G4_EU_H
#define VELOCITY9X_INTEL_G4_EU_H

#include "velocity9x/status.h"
#include "velocity9x/types.h"

/* Opcodes (965 PRM Vol 4, "Opcode Encoding"). */
#define V9X_G4_EU_OP_MOV  ((v9x_u8)0x01u)
#define V9X_G4_EU_OP_SEND ((v9x_u8)0x31u)
#define V9X_G4_EU_OP_ADD  ((v9x_u8)0x40u)
#define V9X_G4_EU_OP_MUL  ((v9x_u8)0x41u)
#define V9X_G4_EU_OP_NOP  ((v9x_u8)0x7eu)

/* ExecSize, DW0 23:21: log2 of the channel count. */
#define V9X_G4_EU_EXEC_1  ((v9x_u8)0u)
#define V9X_G4_EU_EXEC_2  ((v9x_u8)1u)
#define V9X_G4_EU_EXEC_4  ((v9x_u8)2u)
#define V9X_G4_EU_EXEC_8  ((v9x_u8)3u)
#define V9X_G4_EU_EXEC_16 ((v9x_u8)4u)

/* Compression control, DW0 13:12. */
#define V9X_G4_EU_QTR_NONE    ((v9x_u8)0u)
#define V9X_G4_EU_QTR_SECHALF ((v9x_u8)1u)
#define V9X_G4_EU_QTR_COMPR   ((v9x_u8)2u)

/* Register files. */
#define V9X_G4_EU_FILE_ARF ((v9x_u8)0u)
#define V9X_G4_EU_FILE_GRF ((v9x_u8)1u)
#define V9X_G4_EU_FILE_MRF ((v9x_u8)2u)
#define V9X_G4_EU_FILE_IMM ((v9x_u8)3u)

/* ARF numbers. */
#define V9X_G4_EU_ARF_NULL ((v9x_u8)0x00u)

/* Register types, and the immediate types where they differ. */
#define V9X_G4_EU_TYPE_UD    ((v9x_u8)0u)
#define V9X_G4_EU_TYPE_D     ((v9x_u8)1u)
#define V9X_G4_EU_TYPE_UW    ((v9x_u8)2u)
#define V9X_G4_EU_TYPE_W     ((v9x_u8)3u)
#define V9X_G4_EU_TYPE_F     ((v9x_u8)7u)
#define V9X_G4_EU_TYPE_IMM_V ((v9x_u8)6u)

/* Region encodings. */
#define V9X_G4_EU_VSTRIDE_0 ((v9x_u8)0u)
#define V9X_G4_EU_VSTRIDE_2 ((v9x_u8)2u)
#define V9X_G4_EU_VSTRIDE_4 ((v9x_u8)3u)
#define V9X_G4_EU_VSTRIDE_8 ((v9x_u8)4u)
#define V9X_G4_EU_WIDTH_1   ((v9x_u8)0u)
#define V9X_G4_EU_WIDTH_2   ((v9x_u8)1u)
#define V9X_G4_EU_WIDTH_4   ((v9x_u8)2u)
#define V9X_G4_EU_WIDTH_8   ((v9x_u8)3u)
#define V9X_G4_EU_HSTRIDE_0 ((v9x_u8)0u)
#define V9X_G4_EU_HSTRIDE_1 ((v9x_u8)1u)

/* Message targets for send, DW3 27:24 (Mesa's BRW_SFID_*). */
#define V9X_G4_SFID_MATH     ((v9x_u8)1u)
#define V9X_G4_SFID_SAMPLER  ((v9x_u8)2u)
#define V9X_G4_SFID_DP_WRITE ((v9x_u8)5u)
#define V9X_G4_SFID_URB      ((v9x_u8)6u)

/* Math function, descriptor 3:0. */
#define V9X_G4_MATH_INV ((v9x_u8)1u)

/* An operand. sub is a byte offset; for an immediate, imm holds the bits. */
struct v9x_g4_reg {
    v9x_u8 file;
    v9x_u8 type;
    v9x_u8 nr;
    v9x_u8 sub;
    v9x_u8 vstride;
    v9x_u8 width;
    v9x_u8 hstride;
    v9x_u8 negate;
    v9x_u32 imm;
};

/* One native instruction: four dwords, DW0 first. */
struct v9x_g4_inst {
    v9x_u32 dw[4];
};

/*
 * The generation a program is encoded for. Gen5 (Ironlake) keeps Gen4's
 * opcodes, regions and ALU words - xf86's exa_*.g4b.gen5 kernels are the
 * .g4b ALU words unchanged - and moves send's descriptor:
 *   DW2 31:28 shared function, DW2 26 end of thread (as well as DW3 31);
 *   DW3 28:25 message length, 24:20 response length, 19 header present,
 *   18:0 function control (the sampler's SIMD mode at 17:16).
 * Mesa 24.0.9 brw_inst.h's Gen5 columns; checked against the .g4b.gen5
 * words in the host test.
 */
#define V9X_G4_GEN4 ((v9x_u8)4u)
#define V9X_G4_GEN5 ((v9x_u8)5u)
/* G4x (G45, Q45: Gen4.5). Its EU sends are the 965's; only g4_3d.c's
 * command encodings tell it from Gen4
 * (docs\decisions\2026-10-23-g4x-gma4500-from-surveys.md). Not ordered
 * against the other two: compare for equality only. */
#define V9X_G4_GEN45 ((v9x_u8)45u)

/* An instruction stream being built. overflow is set, not returned, when
 * a write would pass the end, so a builder checks once at the end. gen is
 * V9X_G4_GEN4 after v9x_g4_program_init; set it to V9X_G4_GEN5 before
 * building for Ironlake. V9X_G4_GEN45 builds the 965's sends. */
struct v9x_g4_program {
    struct v9x_g4_inst *inst;
    v9x_u32 capacity;
    v9x_u32 count;
    v9x_u8 overflow;
    v9x_u8 gen;
};

/* Operand constructors. */
struct v9x_g4_reg v9x_g4_grf(v9x_u8 nr, v9x_u8 sub, v9x_u8 type);
struct v9x_g4_reg v9x_g4_mrf(v9x_u8 nr, v9x_u8 type);
struct v9x_g4_reg v9x_g4_null(v9x_u8 type);
struct v9x_g4_reg v9x_g4_imm_f(float value);
struct v9x_g4_reg v9x_g4_imm_ud(v9x_u32 value);
struct v9x_g4_reg v9x_g4_imm_v(v9x_u32 nibbles);
struct v9x_g4_reg v9x_g4_region(struct v9x_g4_reg reg, v9x_u8 vstride,
                                v9x_u8 width, v9x_u8 hstride);
struct v9x_g4_reg v9x_g4_scalar(v9x_u8 nr, v9x_u8 sub, v9x_u8 type);
struct v9x_g4_reg v9x_g4_neg(struct v9x_g4_reg reg);

void v9x_g4_program_init(struct v9x_g4_program *program,
                         struct v9x_g4_inst *storage, v9x_u32 capacity);

/* A 1- or 2-source ALU instruction (src1 ignored for mov). */
void v9x_g4_alu(struct v9x_g4_program *program, v9x_u8 opcode,
                v9x_u8 exec_size, v9x_u8 qtr, v9x_u8 mask_disable,
                struct v9x_g4_reg dst, struct v9x_g4_reg src0,
                struct v9x_g4_reg src1);

/* send: src0 is moved implicitly to m<base_mrf> when it is a GRF. */
void v9x_g4_send(struct v9x_g4_program *program, v9x_u8 exec_size,
                 v9x_u8 qtr, struct v9x_g4_reg dst, struct v9x_g4_reg src0,
                 v9x_u8 base_mrf, v9x_u8 sfid, v9x_u8 mlen, v9x_u8 rlen,
                 v9x_u16 function_control, v9x_u8 eot);

void v9x_g4_nop(struct v9x_g4_program *program);

/* Message descriptors' function-control halves. */
v9x_u16 v9x_g4_math_desc(v9x_u8 function);
v9x_u16 v9x_g4_urb_write_desc(v9x_u8 offset, v9x_u8 complete);
v9x_u16 v9x_g4_sampler_desc(v9x_u8 binding_table, v9x_u8 sampler);
v9x_u16 v9x_g4_rt_write_desc(v9x_u8 binding_table);

/*
 * The SF kernel: triangle setup of `setup_regs` pairs of vec4 attributes,
 * Mesa's brw_emit_tri_setup without flat shading or two-sided colour. The
 * first pair's first attribute carries z and 1/w from the payload, so the
 * WM reads depth at URB offset 1. Registers used: setup_regs * 3 + 8.
 */
v9x_status v9x_g4_sf_kernel(struct v9x_g4_program *program,
                            v9x_u8 setup_regs, v9x_u8 *grf_used);

/* The pixel kernel's description. */
#define V9X_G4_PS_DIFFUSE            ((v9x_u8)0u) /* diffuse only         */
#define V9X_G4_PS_TEXTURE            ((v9x_u8)1u) /* texel (DECAL, COPY)  */
#define V9X_G4_PS_MODULATE           ((v9x_u8)2u) /* diffuse x texel, a d */
#define V9X_G4_PS_MODULATE_TEXALPHA  ((v9x_u8)3u) /* rgb modulate, a texel*/
#define V9X_G4_PS_ADD                ((v9x_u8)4u) /* diffuse + texel rgb  */
#define V9X_G4_PS_DECAL              ((v9x_u8)5u) /* texel rgb, diffuse a */

/* The second unit's stage: none, or the colour so far multiplied by
 * texture 1 (binding table entry 2, sampler 1), as Haswell's one stage 1. */
#define V9X_G4_PS_STAGE1_NONE        ((v9x_u8)0u)
#define V9X_G4_PS_MODULATE_TEX1      ((v9x_u8)1u)

struct v9x_g4_ps_desc {
    v9x_u8 stage0;
    v9x_u8 stage1;
    v9x_u8 specular;
    v9x_u8 fog;
    float fog_rgb[3];
};

/*
 * The WM kernel for a description: SIMD16, setup data from g3 in the
 * attribute order of intel_g4_3d.h (z carrier, diffuse, specular, texture
 * coordinates divided by w on the CPU side, the second unit's after them),
 * and one render target write with EOT. Texture 0 is binding table entry
 * 1 with sampler 0; texture 1 entry 2 with sampler 1.
 */
v9x_status v9x_g4_wm_kernel(struct v9x_g4_program *program,
                            const struct v9x_g4_ps_desc *desc,
                            v9x_u8 *grf_used);

#endif /* VELOCITY9X_INTEL_G4_EU_H */
