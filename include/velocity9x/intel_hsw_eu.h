/*
 * Haswell (Gen7.5) EU instruction encoding: the subset a Direct3D pixel
 * shader needs.
 *
 * Phase E of docs\plans\intel-haswell-direct3d.md. Pure encoding, no I/O:
 * each 128-bit native instruction is built from the field tables of HSW
 * PRM Vol 7 ("Instruction Machine Formats": DW0 operation, DW1 destination,
 * DW2 source 0, DW3 source 1, and the separate 3-source layout), and a
 * decoder takes it apart again so every encoding is checked by round trip.
 *
 * Covered:
 *   - 1- and 2-source instructions, direct addressing, Align1 and Align16;
 *     an immediate is allowed as the last source only (Vol 7 "Imm32").
 *   - 3-source instructions (mad, lrp), which are Align16 only.
 *   - send, with the shared function ID in DW0 27:24 and the message
 *     descriptor plus EOT in DW3 (Vol 7 DW3 notes; Vol 2d MsgDescpt31).
 * Not covered: indirect addressing, compaction, flow control, flags beyond
 * f0.0/f0.1 selection.
 *
 * The encodings are checked against the PRM's bit tables and by round
 * trip; none has executed on hardware.
 */
#ifndef VELOCITY9X_INTEL_HSW_EU_H
#define VELOCITY9X_INTEL_HSW_EU_H

#include "velocity9x/status.h"
#include "velocity9x/types.h"

/* Opcodes, Vol 7 "Opcode Encoding". */
#define V9X_HSW_EU_OP_MOV  ((v9x_u8)0x01u)
#define V9X_HSW_EU_OP_SEL  ((v9x_u8)0x02u)
#define V9X_HSW_EU_OP_AND  ((v9x_u8)0x05u)
#define V9X_HSW_EU_OP_OR   ((v9x_u8)0x06u)
#define V9X_HSW_EU_OP_CMP  ((v9x_u8)0x10u)
#define V9X_HSW_EU_OP_SEND ((v9x_u8)0x31u)
#define V9X_HSW_EU_OP_MATH ((v9x_u8)0x38u)
#define V9X_HSW_EU_OP_ADD  ((v9x_u8)0x40u)
#define V9X_HSW_EU_OP_MUL  ((v9x_u8)0x41u)
#define V9X_HSW_EU_OP_FRC  ((v9x_u8)0x43u)
#define V9X_HSW_EU_OP_RNDD ((v9x_u8)0x45u)
#define V9X_HSW_EU_OP_MAC  ((v9x_u8)0x48u)
#define V9X_HSW_EU_OP_DP4  ((v9x_u8)0x54u)
#define V9X_HSW_EU_OP_DP3  ((v9x_u8)0x56u)
#define V9X_HSW_EU_OP_LINE ((v9x_u8)0x59u)
#define V9X_HSW_EU_OP_PLN  ((v9x_u8)0x5au)
#define V9X_HSW_EU_OP_MAD  ((v9x_u8)0x5bu)
#define V9X_HSW_EU_OP_LRP  ((v9x_u8)0x5cu)
#define V9X_HSW_EU_OP_NOP  ((v9x_u8)0x7eu)

/* ExecSize, DW0 23:21: log2 of the channel count. */
#define V9X_HSW_EU_EXEC_1  ((v9x_u8)0u)
#define V9X_HSW_EU_EXEC_2  ((v9x_u8)1u)
#define V9X_HSW_EU_EXEC_4  ((v9x_u8)2u)
#define V9X_HSW_EU_EXEC_8  ((v9x_u8)3u)
#define V9X_HSW_EU_EXEC_16 ((v9x_u8)4u)

/* Register files, DW1 RegFile fields. 10b is reserved. */
#define V9X_HSW_EU_FILE_ARF ((v9x_u8)0u)
#define V9X_HSW_EU_FILE_GRF ((v9x_u8)1u)
#define V9X_HSW_EU_FILE_IMM ((v9x_u8)3u)

/*
 * Register types, DW1 "Source Register Type Encoding" (identical for the
 * destination). An immediate uses the Source Immediate Type Encoding, which
 * agrees for UD, D, UW, W and F and puts the packed vectors UV, VF and V at
 * 100b-110b; V9X_HSW_EU_TYPE_IMM_VF is that 101b.
 */
#define V9X_HSW_EU_TYPE_UD     ((v9x_u8)0u)
#define V9X_HSW_EU_TYPE_D      ((v9x_u8)1u)
#define V9X_HSW_EU_TYPE_UW     ((v9x_u8)2u)
#define V9X_HSW_EU_TYPE_W      ((v9x_u8)3u)
#define V9X_HSW_EU_TYPE_UB     ((v9x_u8)4u)
#define V9X_HSW_EU_TYPE_B      ((v9x_u8)5u)
#define V9X_HSW_EU_TYPE_F      ((v9x_u8)7u)
#define V9X_HSW_EU_TYPE_IMM_VF ((v9x_u8)5u)

/* 3-source types, DW1 16:15 and 14:13. */
#define V9X_HSW_EU_TYPE3_F  ((v9x_u8)0u)
#define V9X_HSW_EU_TYPE3_D  ((v9x_u8)1u)
#define V9X_HSW_EU_TYPE3_UD ((v9x_u8)2u)

/* Region encodings, Vol 7 "Common Instruction Fields". */
#define V9X_HSW_EU_VSTRIDE_0 ((v9x_u8)0u)
#define V9X_HSW_EU_VSTRIDE_1 ((v9x_u8)1u)
#define V9X_HSW_EU_VSTRIDE_2 ((v9x_u8)2u)
#define V9X_HSW_EU_VSTRIDE_4 ((v9x_u8)3u)
#define V9X_HSW_EU_VSTRIDE_8 ((v9x_u8)4u)
#define V9X_HSW_EU_VSTRIDE_16 ((v9x_u8)5u)
#define V9X_HSW_EU_WIDTH_1   ((v9x_u8)0u)
#define V9X_HSW_EU_WIDTH_2   ((v9x_u8)1u)
#define V9X_HSW_EU_WIDTH_4   ((v9x_u8)2u)
#define V9X_HSW_EU_WIDTH_8   ((v9x_u8)3u)
#define V9X_HSW_EU_WIDTH_16  ((v9x_u8)4u)
#define V9X_HSW_EU_HSTRIDE_0 ((v9x_u8)0u)
#define V9X_HSW_EU_HSTRIDE_1 ((v9x_u8)1u)
#define V9X_HSW_EU_HSTRIDE_2 ((v9x_u8)2u)
#define V9X_HSW_EU_HSTRIDE_4 ((v9x_u8)3u)

/* SrcMod. */
#define V9X_HSW_EU_MOD_NONE    ((v9x_u8)0u)
#define V9X_HSW_EU_MOD_ABS     ((v9x_u8)1u)
#define V9X_HSW_EU_MOD_NEG     ((v9x_u8)2u)
#define V9X_HSW_EU_MOD_NEG_ABS ((v9x_u8)3u)

/* The identity swizzle xyzw: ChanSel 11 10 01 00b. */
#define V9X_HSW_EU_SWIZZLE_XYZW ((v9x_u8)0xe4u)

/* Shared function IDs for send, Vol 2a "SFID". */
#define V9X_HSW_SFID_NULL       ((v9x_u8)0x0u)
#define V9X_HSW_SFID_SAMPLER    ((v9x_u8)0x2u)
#define V9X_HSW_SFID_GATEWAY    ((v9x_u8)0x3u)
#define V9X_HSW_SFID_DP_SAMPLER ((v9x_u8)0x4u)
#define V9X_HSW_SFID_DP_RC      ((v9x_u8)0x5u)
#define V9X_HSW_SFID_URB        ((v9x_u8)0x6u)
#define V9X_HSW_SFID_SPAWNER    ((v9x_u8)0x7u)
#define V9X_HSW_SFID_DP_DC0     ((v9x_u8)0xau)
#define V9X_HSW_SFID_PI         ((v9x_u8)0xbu)

/* Render target write message types, Vol 2d "Message Descriptor - Render
 * Target Write" 10:8. */
#define V9X_HSW_RT_SIMD16        ((v9x_u8)0u)
#define V9X_HSW_RT_SIMD16_REP    ((v9x_u8)1u)
#define V9X_HSW_RT_SIMD8_DUAL_LO ((v9x_u8)2u)
#define V9X_HSW_RT_SIMD8_DUAL_HI ((v9x_u8)3u)
#define V9X_HSW_RT_SIMD8_LO      ((v9x_u8)4u)
/* Data port render cache message type 17:14, Vol 7: Render Target Write. */
#define V9X_HSW_DP_RC_RT_WRITE   ((v9x_u8)0xcu)

/* Sampler SIMD modes, Vol 7 sampler message descriptor 18:17. */
#define V9X_HSW_SAMPLER_SIMD4X2 ((v9x_u8)0u)
#define V9X_HSW_SAMPLER_SIMD8   ((v9x_u8)1u)
#define V9X_HSW_SAMPLER_SIMD16  ((v9x_u8)2u)

/*
 * One operand. For a GRF operand, nr is the register (0-127) and subnr the
 * byte offset within it (0-31; Align16 uses only bit 4). For ARF, nr and
 * subnr are the architecture register encoding. For IMM, imm holds the 32
 * bits and only type matters. hstride applies to the destination and, with
 * vstride and width, to Align1 sources. chan is the destination's channel
 * enable (Align16) or a source's swizzle (Align16).
 */
struct v9x_hsw_eu_operand {
    v9x_u8 file;
    v9x_u8 type;
    v9x_u8 nr;
    v9x_u8 subnr;
    v9x_u8 vstride;
    v9x_u8 width;
    v9x_u8 hstride;
    v9x_u8 mod;
    v9x_u8 chan;
    v9x_u8 rep;     /* 3-source replicate control */
    v9x_u32 imm;
};

struct v9x_hsw_eu_inst {
    v9x_u8 opcode;
    v9x_u8 exec_size;
    v9x_u8 align16;      /* AccessMode */
    v9x_u8 mask_disable; /* MaskCtrl = NoMask */
    v9x_u8 saturate;
    v9x_u8 pred_ctrl;
    v9x_u8 pred_inv;
    v9x_u8 cond_mod;     /* not send */
    v9x_u8 sfid;         /* send only: DW0 27:24 */
    v9x_u8 flag_reg;     /* f0 or f1 */
    v9x_u8 flag_subreg;
    v9x_u8 eot;          /* send only */
    v9x_u8 sources;      /* 1, 2 or 3 */
    struct v9x_hsw_eu_operand dst;
    struct v9x_hsw_eu_operand src0;
    struct v9x_hsw_eu_operand src1;
    struct v9x_hsw_eu_operand src2;
    v9x_u32 desc;        /* send only: MsgDescpt31 */
};

v9x_status v9x_intel_hsw_eu_encode(const struct v9x_hsw_eu_inst *inst,
                                   v9x_u32 out[4]);

v9x_status v9x_intel_hsw_eu_decode(const v9x_u32 in[4],
                                   struct v9x_hsw_eu_inst *inst);

/* MsgDescpt31: message length 28:25 (1-15), response length 24:20 (0-16),
 * header present 19, function control 18:0 (Vol 2d). */
v9x_status v9x_intel_hsw_eu_send_desc(v9x_u8 mlen, v9x_u8 rlen,
                                      v9x_u8 header, v9x_u32 function,
                                      v9x_u32 *desc);

/* Render target write function control: last RT 12, slot group 11,
 * message type 10:8, data port message type 17:14, binding table 7:0. */
v9x_status v9x_intel_hsw_eu_rt_write_fc(v9x_u8 last_rt, v9x_u8 slot_hi,
                                        v9x_u8 msg_type, v9x_u8 bti,
                                        v9x_u32 *function);

/* Sampler function control: SIMD mode 18:17, message type 16:12, sampler
 * index 11:8, binding table 7:0. Header present is the descriptor's bit. */
v9x_status v9x_intel_hsw_eu_sampler_fc(v9x_u8 simd, v9x_u8 msg_type,
                                       v9x_u8 sampler, v9x_u8 bti,
                                       v9x_u32 *function);

/*
 * Phase D pixel shader kernels, SIMD8, ending in a render target write to
 * binding table entry 0 with EOT. The colour payload goes in g112-g115,
 * the range Mesa requires an EOT send's payload to be in on Gen7 (the PRM
 * text read here does not state it). Each writes whole 4-dword
 * instructions into out (capacity in dwords) and reports the dwords used.
 */
#define V9X_HSW_PS_PAYLOAD_GRF ((v9x_u8)112u)

/* Four movs of rgba (IEEE float bit patterns) and the write. */
v9x_status v9x_intel_hsw_ps_kernel_constant(v9x_u32 *out, v9x_u32 capacity,
                                            const v9x_u32 rgba[4],
                                            v9x_u32 *used);

/* Four plns of attribute 0's planes, two components per register from
 * setup_grf (component c at register setup_grf + c / 2, byte (c % 2) * 16),
 * against the perspective pixel barycentrics in g2-g3; then the write. */
v9x_status v9x_intel_hsw_ps_kernel_interpolated(v9x_u32 *out,
                                                v9x_u32 capacity,
                                                v9x_u8 setup_grf,
                                                v9x_u32 *used);

/*
 * Phase E: one generator for the texture-stage combinations. Stage 0
 * combines texture 0 (binding table entry 1) with the diffuse colour;
 * stage 1 optionally modulates by texture 1 (entry 2); vertex fog blends
 * towards fog_rgb (float bit patterns) by the specular alpha, as D3D's
 * vertex fog does. Attributes: 0 diffuse, 1 specular, 2 and 3 texture
 * coordinates. Sampler 0 for texture 0 and, since render interface step
 * 4, sampler 1 for texture 1.
 */
#define V9X_HSW_PS_DIFFUSE       ((v9x_u8)0u) /* SELECTARG diffuse */
#define V9X_HSW_PS_TEXTURE       ((v9x_u8)1u) /* SELECTARG texture */
#define V9X_HSW_PS_MODULATE      ((v9x_u8)2u)
#define V9X_HSW_PS_ADD           ((v9x_u8)3u) /* colour add, alpha modulate */
#define V9X_HSW_PS_DECAL         ((v9x_u8)4u) /* texture colour, diffuse alpha */
/* Phase F2: D3D's MODULATE on a texture with alpha, colour multiplied and
 * the texture's alpha. */
#define V9X_HSW_PS_MODULATE_TEXALPHA ((v9x_u8)5u)
#define V9X_HSW_PS_STAGE1_NONE   ((v9x_u8)0u)
#define V9X_HSW_PS_MODULATE_TEX1 ((v9x_u8)1u)

/*
 * General combines, for the second Direct3D texture stage (appended
 * 2026-10-25): a colour op and an alpha op, each against the stage's
 * input - the diffuse colour for stage 0, stage 0's result for stage 1 -
 * and the stage's texel. V9X_HSW_PS_STAGE0_OPS(c, a) is a stage0 code and
 * V9X_HSW_PS_STAGE1_OPS(c, a) a stage1 code, for the combinations the
 * codes above do not name.
 *   colour: REPLACE Ct; MODULATE C Ct; ADD C + Ct, saturated; DECALALPHA
 *           Ct At + C (1 - At)
 *   alpha:  INPUT A; REPLACE At; MODULATE A At
 */
#define V9X_HSW_PS_OP_REPLACE    ((v9x_u8)0u)
#define V9X_HSW_PS_OP_MODULATE   ((v9x_u8)1u)
#define V9X_HSW_PS_OP_ADD        ((v9x_u8)2u)
#define V9X_HSW_PS_OP_DECALALPHA ((v9x_u8)3u)
#define V9X_HSW_PS_AOP_INPUT     ((v9x_u8)0u)
#define V9X_HSW_PS_AOP_REPLACE   ((v9x_u8)1u)
#define V9X_HSW_PS_AOP_MODULATE  ((v9x_u8)2u)
#define V9X_HSW_PS_OPS_FIRST0    ((v9x_u8)6u)
#define V9X_HSW_PS_OPS_FIRST1    ((v9x_u8)2u)
#define V9X_HSW_PS_STAGE0_OPS(c, a) \
    ((v9x_u8)(V9X_HSW_PS_OPS_FIRST0 + (c) * 3u + (a)))
#define V9X_HSW_PS_STAGE1_OPS(c, a) \
    ((v9x_u8)(V9X_HSW_PS_OPS_FIRST1 + (c) * 3u + (a)))
#define V9X_HSW_PS_STAGE0_LAST V9X_HSW_PS_STAGE0_OPS(3u, 2u)
#define V9X_HSW_PS_STAGE1_LAST V9X_HSW_PS_STAGE1_OPS(3u, 2u)

struct v9x_hsw_ps_desc {
    v9x_u8 stage0;
    v9x_u8 stage1;
    v9x_u8 fog;
    v9x_u32 fog_rgb[3];
    /* Phase F2: add the specular colour after the stages, saturated, as
     * Direct3D's SPECULARENABLE does, before fog. */
    v9x_u8 specular;
};

v9x_status v9x_intel_hsw_ps_kernel_build(v9x_u32 *out, v9x_u32 capacity,
                                         const struct v9x_hsw_ps_desc *desc,
                                         v9x_u32 *used);

#endif
