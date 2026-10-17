/*
 * Haswell (Gen7.5) EU instruction encoder and decoder. See
 * include\velocity9x\intel_hsw_eu.h for the scope and the PRM sections.
 * Every shift below is a field position from HSW PRM Vol 7 "Instruction
 * Machine Formats" unless the comment names another table.
 */
#include "velocity9x/intel_hsw_eu.h"

#define V9X_HSW_EU_GRF_COUNT  128u
#define V9X_HSW_EU_SUBNR_MAX  32u
#define V9X_HSW_EU_DESC_MASK  ((v9x_u32)0x7ffffffful)
#define V9X_HSW_EU_EOT        ((v9x_u32)0x80000000ul)

/* Opcodes this encoder accepts, and how many sources each takes. A send
 * counts as two: src0 is the payload, src1 the immediate descriptor. */
static v9x_u8 v9x_hsw_eu_sources(v9x_u8 opcode)
{
    switch (opcode) {
    case V9X_HSW_EU_OP_MOV:
    case V9X_HSW_EU_OP_FRC:
    case V9X_HSW_EU_OP_RNDD:
        return 1u;
    case V9X_HSW_EU_OP_SEL:
    case V9X_HSW_EU_OP_AND:
    case V9X_HSW_EU_OP_OR:
    case V9X_HSW_EU_OP_CMP:
    case V9X_HSW_EU_OP_SEND:
    case V9X_HSW_EU_OP_MATH:
    case V9X_HSW_EU_OP_ADD:
    case V9X_HSW_EU_OP_MUL:
    case V9X_HSW_EU_OP_MAC:
    case V9X_HSW_EU_OP_DP4:
    case V9X_HSW_EU_OP_DP3:
    case V9X_HSW_EU_OP_LINE:
    case V9X_HSW_EU_OP_PLN:
        return 2u;
    case V9X_HSW_EU_OP_MAD:
    case V9X_HSW_EU_OP_LRP:
        return 3u;
    case V9X_HSW_EU_OP_NOP:
        return 0u;
    default:
        return 0xffu;
    }
}

static int v9x_hsw_eu_file_ok(v9x_u8 file)
{
    return file == V9X_HSW_EU_FILE_ARF || file == V9X_HSW_EU_FILE_GRF ||
           file == V9X_HSW_EU_FILE_IMM;
}

/* A register operand's number and byte offset must name a GRF byte. */
static int v9x_hsw_eu_reg_ok(const struct v9x_hsw_eu_operand *op)
{
    if (op->file == V9X_HSW_EU_FILE_GRF && op->nr >= V9X_HSW_EU_GRF_COUNT) {
        return 0;
    }
    return op->subnr < V9X_HSW_EU_SUBNR_MAX && op->type <= 7u &&
           op->mod <= 3u;
}

/* Align16 addresses 16-byte halves only, and allows only vertical strides
 * 0, 2 and 4 elements (encodings 0000, 0010, 0011). */
static int v9x_hsw_eu_align16_ok(const struct v9x_hsw_eu_operand *op,
                                 int source)
{
    if (op->file == V9X_HSW_EU_FILE_IMM) {
        return 1;
    }
    if (op->subnr != 0u && op->subnr != 16u) {
        return 0;
    }
    if (source && op->vstride != V9X_HSW_EU_VSTRIDE_0 &&
        op->vstride != V9X_HSW_EU_VSTRIDE_2 &&
        op->vstride != V9X_HSW_EU_VSTRIDE_4) {
        return 0;
    }
    return 1;
}

/* DW0, shared by every format. CondModifier and SFID share 27:24. */
static v9x_u32 v9x_hsw_eu_dw0(const struct v9x_hsw_eu_inst *inst)
{
    v9x_u32 dw;

    dw = (v9x_u32)inst->opcode & 0x7ful;
    dw |= ((v9x_u32)(inst->align16 & 1u)) << 8;
    dw |= ((v9x_u32)(inst->mask_disable & 1u)) << 9;
    dw |= ((v9x_u32)(inst->pred_ctrl & 0xfu)) << 16;
    dw |= ((v9x_u32)(inst->pred_inv & 1u)) << 20;
    dw |= ((v9x_u32)(inst->exec_size & 7u)) << 21;
    if (inst->opcode == V9X_HSW_EU_OP_SEND) {
        dw |= ((v9x_u32)(inst->sfid & 0xfu)) << 24;
    } else {
        dw |= ((v9x_u32)(inst->cond_mod & 0xfu)) << 24;
    }
    dw |= ((v9x_u32)(inst->saturate & 1u)) << 31;
    return dw;
}

/* A register source in DW2 (src0) or DW3 (src1) layout; the two agree
 * from bit 24 down except that only src0 carries AddrMode and the flag
 * selection, which the caller adds. */
static v9x_u32 v9x_hsw_eu_src_bits(const struct v9x_hsw_eu_operand *op,
                                   v9x_u8 align16)
{
    v9x_u32 dw;

    dw = ((v9x_u32)(op->vstride & 0xfu)) << 21;
    dw |= ((v9x_u32)(op->mod & 3u)) << 13;
    dw |= ((v9x_u32)op->nr) << 5;
    if (align16) {
        dw |= ((v9x_u32)(op->chan >> 4)) << 16;
        dw |= ((v9x_u32)(op->subnr >> 4)) << 4;
        dw |= (v9x_u32)(op->chan & 0xfu);
    } else {
        dw |= ((v9x_u32)(op->width & 7u)) << 18;
        dw |= ((v9x_u32)(op->hstride & 3u)) << 16;
        dw |= (v9x_u32)(op->subnr & 0x1fu);
    }
    return dw;
}

static v9x_status v9x_hsw_eu_encode_3src(const struct v9x_hsw_eu_inst *inst,
                                         v9x_u32 out[4])
{
    const struct v9x_hsw_eu_operand *s0 = &inst->src0;
    const struct v9x_hsw_eu_operand *s1 = &inst->src1;
    const struct v9x_hsw_eu_operand *s2 = &inst->src2;
    v9x_u32 sub1;

    /* 3-source instructions are Align16, all-GRF, with 3-bit dword
     * sub-register numbers (Vol 7 "DW1 3-src", "DW2 and DW3 3-Source"). */
    if (!inst->align16 || inst->dst.file != V9X_HSW_EU_FILE_GRF ||
        s0->file != V9X_HSW_EU_FILE_GRF || s1->file != V9X_HSW_EU_FILE_GRF ||
        s2->file != V9X_HSW_EU_FILE_GRF) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (inst->dst.nr >= V9X_HSW_EU_GRF_COUNT ||
        s0->nr >= V9X_HSW_EU_GRF_COUNT || s1->nr >= V9X_HSW_EU_GRF_COUNT ||
        s2->nr >= V9X_HSW_EU_GRF_COUNT) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (((inst->dst.subnr | s0->subnr | s1->subnr | s2->subnr) & 3u) != 0u ||
        inst->dst.subnr >= V9X_HSW_EU_SUBNR_MAX ||
        s0->subnr >= V9X_HSW_EU_SUBNR_MAX ||
        s1->subnr >= V9X_HSW_EU_SUBNR_MAX ||
        s2->subnr >= V9X_HSW_EU_SUBNR_MAX ||
        inst->dst.type > 3u || s0->type > 3u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    out[0] = v9x_hsw_eu_dw0(inst);
    out[1] = ((v9x_u32)inst->dst.nr) << 24;
    out[1] |= ((v9x_u32)(inst->dst.subnr >> 2)) << 21;
    out[1] |= ((v9x_u32)(inst->dst.chan & 0xfu)) << 17;
    out[1] |= ((v9x_u32)inst->dst.type) << 15;
    out[1] |= ((v9x_u32)s0->type) << 13;
    out[1] |= ((v9x_u32)(s2->mod & 3u)) << 8;
    out[1] |= ((v9x_u32)(s1->mod & 3u)) << 6;
    out[1] |= ((v9x_u32)(s0->mod & 3u)) << 4;
    out[1] |= ((v9x_u32)(inst->flag_reg & 1u)) << 2;
    out[1] |= ((v9x_u32)(inst->flag_subreg & 1u)) << 1;

    /* Source 1's sub-register number is split: 1:0 in DW2 31:30, 2 in
     * DW3 0. */
    sub1 = (v9x_u32)(s1->subnr >> 2);
    out[2] = (sub1 & 3ul) << 30;
    out[2] |= ((v9x_u32)s1->chan) << 22;
    out[2] |= ((v9x_u32)(s1->rep & 1u)) << 21;
    out[2] |= ((v9x_u32)s0->nr) << 12;
    out[2] |= ((v9x_u32)(s0->subnr >> 2)) << 9;
    out[2] |= ((v9x_u32)s0->chan) << 1;
    out[2] |= (v9x_u32)(s0->rep & 1u);

    out[3] = ((v9x_u32)s2->nr) << 22;
    out[3] |= ((v9x_u32)(s2->subnr >> 2)) << 19;
    out[3] |= ((v9x_u32)s2->chan) << 11;
    out[3] |= ((v9x_u32)(s2->rep & 1u)) << 10;
    out[3] |= ((v9x_u32)s1->nr) << 1;
    out[3] |= (sub1 >> 2) & 1ul;
    return V9X_STATUS_OK;
}

v9x_status v9x_intel_hsw_eu_encode(const struct v9x_hsw_eu_inst *inst,
                                   v9x_u32 out[4])
{
    struct v9x_hsw_eu_operand src1;
    v9x_u8 sources;

    if (inst == 0 || out == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    sources = v9x_hsw_eu_sources(inst->opcode);
    if (sources == 0xffu || inst->exec_size > V9X_HSW_EU_EXEC_16 ||
        inst->pred_ctrl > 0xdu) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (sources == 3u) {
        return v9x_hsw_eu_encode_3src(inst, out);
    }

    if (!v9x_hsw_eu_file_ok(inst->dst.file) ||
        inst->dst.file == V9X_HSW_EU_FILE_IMM ||
        !v9x_hsw_eu_reg_ok(&inst->dst)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (sources >= 1u && (!v9x_hsw_eu_file_ok(inst->src0.file) ||
                          !v9x_hsw_eu_reg_ok(&inst->src0))) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /*
     * Source 1. "If an operand is not present ... the second source
     * operand src1 must be set to null and also with the same type as the
     * first source operand src0" (Vol 7 "Non-present Operands"): ARF
     * register 0, which is null. For send, src1 is the immediate message
     * descriptor (Vol 7 DW3 notes).
     */
    src1 = inst->src1;
    if (sources <= 1u) {
        src1.file = V9X_HSW_EU_FILE_ARF;
        src1.type = inst->src0.type;
        src1.nr = 0u;
        src1.subnr = 0u;
        src1.vstride = 0u;
        src1.width = 0u;
        src1.hstride = 0u;
        src1.mod = 0u;
        src1.chan = 0u;
    } else if (inst->opcode == V9X_HSW_EU_OP_SEND) {
        if ((inst->desc & ~V9X_HSW_EU_DESC_MASK) != 0ul) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        src1.file = V9X_HSW_EU_FILE_IMM;
        src1.type = V9X_HSW_EU_TYPE_UD;
    } else {
        if (!v9x_hsw_eu_file_ok(src1.file) || !v9x_hsw_eu_reg_ok(&src1)) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        /* Only the last source may be immediate. */
        if (inst->src0.file == V9X_HSW_EU_FILE_IMM) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
    }

    if (inst->align16 &&
        (!v9x_hsw_eu_align16_ok(&inst->dst, 0) ||
         !v9x_hsw_eu_align16_ok(&inst->src0, 1) ||
         !v9x_hsw_eu_align16_ok(&src1, 1))) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    out[0] = v9x_hsw_eu_dw0(inst);

    /* DW1: destination region in 31:16, then the five type/file fields.
     * "Although Dst.HorzStride is a don't care for Align16, HW needs this
     * to be programmed as 01." */
    if (inst->align16) {
        out[1] = ((v9x_u32)V9X_HSW_EU_HSTRIDE_1) << 29;
        out[1] |= ((v9x_u32)inst->dst.nr) << 21;
        out[1] |= ((v9x_u32)(inst->dst.subnr >> 4)) << 20;
        out[1] |= ((v9x_u32)(inst->dst.chan & 0xfu)) << 16;
    } else {
        out[1] = ((v9x_u32)(inst->dst.hstride & 3u)) << 29;
        out[1] |= ((v9x_u32)inst->dst.nr) << 21;
        out[1] |= ((v9x_u32)(inst->dst.subnr & 0x1fu)) << 16;
    }
    out[1] |= ((v9x_u32)(src1.type & 7u)) << 12;
    out[1] |= ((v9x_u32)(src1.file & 3u)) << 10;
    out[1] |= ((v9x_u32)(inst->src0.type & 7u)) << 7;
    out[1] |= ((v9x_u32)(inst->src0.file & 3u)) << 5;
    out[1] |= ((v9x_u32)(inst->dst.type & 7u)) << 2;
    out[1] |= (v9x_u32)(inst->dst.file & 3u);

    /* DW2: source 0, plus the flag register selection (bits 26, 25). An
     * immediate src0 lives in DW3 and leaves only the flag bits here. */
    out[2] = ((v9x_u32)(inst->flag_reg & 1u)) << 26;
    out[2] |= ((v9x_u32)(inst->flag_subreg & 1u)) << 25;
    if (sources >= 1u && inst->src0.file != V9X_HSW_EU_FILE_IMM) {
        out[2] |= v9x_hsw_eu_src_bits(&inst->src0, inst->align16);
    }

    /* DW3: source 1, an immediate, or EOT plus the message descriptor. */
    if (inst->opcode == V9X_HSW_EU_OP_SEND) {
        out[3] = inst->desc & V9X_HSW_EU_DESC_MASK;
        if (inst->eot) {
            out[3] |= V9X_HSW_EU_EOT;
        }
    } else if (sources >= 1u && inst->src0.file == V9X_HSW_EU_FILE_IMM) {
        out[3] = inst->src0.imm;
    } else if (src1.file == V9X_HSW_EU_FILE_IMM) {
        out[3] = src1.imm;
    } else if (sources == 2u) {
        out[3] = v9x_hsw_eu_src_bits(&src1, inst->align16);
    } else {
        out[3] = 0ul;
    }
    return V9X_STATUS_OK;
}

static void v9x_hsw_eu_clear(struct v9x_hsw_eu_inst *inst)
{
    v9x_u8 *bytes;
    v9x_u32 index;

    bytes = (v9x_u8 *)inst;
    for (index = 0ul; index < (v9x_u32)sizeof(*inst); ++index) {
        bytes[index] = 0u;
    }
}

static void v9x_hsw_eu_src_decode(v9x_u32 dw, v9x_u8 align16,
                                  struct v9x_hsw_eu_operand *op)
{
    op->vstride = (v9x_u8)((dw >> 21) & 0xful);
    op->mod = (v9x_u8)((dw >> 13) & 3ul);
    op->nr = (v9x_u8)((dw >> 5) & 0xfful);
    if (align16) {
        op->chan = (v9x_u8)((((dw >> 16) & 0xful) << 4) | (dw & 0xful));
        op->subnr = (v9x_u8)(((dw >> 4) & 1ul) << 4);
    } else {
        op->width = (v9x_u8)((dw >> 18) & 7ul);
        op->hstride = (v9x_u8)((dw >> 16) & 3ul);
        op->subnr = (v9x_u8)(dw & 0x1ful);
    }
}

v9x_status v9x_intel_hsw_eu_decode(const v9x_u32 in[4],
                                   struct v9x_hsw_eu_inst *inst)
{
    v9x_u32 sub1;

    if (in == 0 || inst == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_hsw_eu_clear(inst);

    inst->opcode = (v9x_u8)(in[0] & 0x7ful);
    inst->align16 = (v9x_u8)((in[0] >> 8) & 1ul);
    inst->mask_disable = (v9x_u8)((in[0] >> 9) & 1ul);
    inst->pred_ctrl = (v9x_u8)((in[0] >> 16) & 0xful);
    inst->pred_inv = (v9x_u8)((in[0] >> 20) & 1ul);
    inst->exec_size = (v9x_u8)((in[0] >> 21) & 7ul);
    inst->saturate = (v9x_u8)((in[0] >> 31) & 1ul);
    inst->sources = v9x_hsw_eu_sources(inst->opcode);
    if (inst->sources == 0xffu) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (inst->opcode == V9X_HSW_EU_OP_SEND) {
        inst->sfid = (v9x_u8)((in[0] >> 24) & 0xful);
    } else {
        inst->cond_mod = (v9x_u8)((in[0] >> 24) & 0xful);
    }

    if (inst->sources == 3u) {
        inst->dst.file = V9X_HSW_EU_FILE_GRF;
        inst->src0.file = V9X_HSW_EU_FILE_GRF;
        inst->src1.file = V9X_HSW_EU_FILE_GRF;
        inst->src2.file = V9X_HSW_EU_FILE_GRF;
        inst->dst.nr = (v9x_u8)((in[1] >> 24) & 0xfful);
        inst->dst.subnr = (v9x_u8)(((in[1] >> 21) & 7ul) << 2);
        inst->dst.chan = (v9x_u8)((in[1] >> 17) & 0xful);
        inst->dst.type = (v9x_u8)((in[1] >> 15) & 3ul);
        inst->src0.type = (v9x_u8)((in[1] >> 13) & 3ul);
        inst->src1.type = inst->src0.type;
        inst->src2.type = inst->src0.type;
        inst->src2.mod = (v9x_u8)((in[1] >> 8) & 3ul);
        inst->src1.mod = (v9x_u8)((in[1] >> 6) & 3ul);
        inst->src0.mod = (v9x_u8)((in[1] >> 4) & 3ul);
        inst->flag_reg = (v9x_u8)((in[1] >> 2) & 1ul);
        inst->flag_subreg = (v9x_u8)((in[1] >> 1) & 1ul);
        sub1 = ((in[2] >> 30) & 3ul) | ((in[3] & 1ul) << 2);
        inst->src1.subnr = (v9x_u8)(sub1 << 2);
        inst->src1.chan = (v9x_u8)((in[2] >> 22) & 0xfful);
        inst->src1.rep = (v9x_u8)((in[2] >> 21) & 1ul);
        inst->src0.nr = (v9x_u8)((in[2] >> 12) & 0xfful);
        inst->src0.subnr = (v9x_u8)(((in[2] >> 9) & 7ul) << 2);
        inst->src0.chan = (v9x_u8)((in[2] >> 1) & 0xfful);
        inst->src0.rep = (v9x_u8)(in[2] & 1ul);
        inst->src2.nr = (v9x_u8)((in[3] >> 22) & 0xfful);
        inst->src2.subnr = (v9x_u8)(((in[3] >> 19) & 7ul) << 2);
        inst->src2.chan = (v9x_u8)((in[3] >> 11) & 0xfful);
        inst->src2.rep = (v9x_u8)((in[3] >> 10) & 1ul);
        inst->src1.nr = (v9x_u8)((in[3] >> 1) & 0xfful);
        return V9X_STATUS_OK;
    }

    inst->dst.file = (v9x_u8)(in[1] & 3ul);
    inst->dst.type = (v9x_u8)((in[1] >> 2) & 7ul);
    inst->src0.file = (v9x_u8)((in[1] >> 5) & 3ul);
    inst->src0.type = (v9x_u8)((in[1] >> 7) & 7ul);
    inst->src1.file = (v9x_u8)((in[1] >> 10) & 3ul);
    inst->src1.type = (v9x_u8)((in[1] >> 12) & 7ul);
    inst->dst.nr = (v9x_u8)((in[1] >> 21) & 0xfful);
    inst->dst.hstride = (v9x_u8)((in[1] >> 29) & 3ul);
    if (inst->align16) {
        inst->dst.subnr = (v9x_u8)(((in[1] >> 20) & 1ul) << 4);
        inst->dst.chan = (v9x_u8)((in[1] >> 16) & 0xful);
    } else {
        inst->dst.subnr = (v9x_u8)((in[1] >> 16) & 0x1ful);
    }

    inst->flag_reg = (v9x_u8)((in[2] >> 26) & 1ul);
    inst->flag_subreg = (v9x_u8)((in[2] >> 25) & 1ul);
    if (inst->src0.file == V9X_HSW_EU_FILE_IMM) {
        inst->src0.imm = in[3];
    } else {
        v9x_hsw_eu_src_decode(in[2], inst->align16, &inst->src0);
    }

    if (inst->opcode == V9X_HSW_EU_OP_SEND) {
        inst->desc = in[3] & V9X_HSW_EU_DESC_MASK;
        inst->eot = (v9x_u8)((in[3] & V9X_HSW_EU_EOT) != 0ul);
    } else if (inst->src1.file == V9X_HSW_EU_FILE_IMM) {
        inst->src1.imm = in[3];
    } else if (inst->sources == 2u) {
        v9x_hsw_eu_src_decode(in[3], inst->align16, &inst->src1);
    }
    return V9X_STATUS_OK;
}

v9x_status v9x_intel_hsw_eu_send_desc(v9x_u8 mlen, v9x_u8 rlen,
                                      v9x_u8 header, v9x_u32 function,
                                      v9x_u32 *desc)
{
    if (desc == 0 || mlen < 1u || mlen > 15u || rlen > 16u ||
        header > 1u || function > 0x7fffful) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *desc = ((v9x_u32)mlen << 25) | ((v9x_u32)rlen << 20) |
            ((v9x_u32)header << 19) | function;
    return V9X_STATUS_OK;
}

v9x_status v9x_intel_hsw_eu_rt_write_fc(v9x_u8 last_rt, v9x_u8 slot_hi,
                                        v9x_u8 msg_type, v9x_u8 bti,
                                        v9x_u32 *function)
{
    /* Binding table index 255 is stateless, which this message refuses
     * ("Binding Table Index cannot be 255"). Types 5 and 6 are unlisted. */
    if (function == 0 || last_rt > 1u || slot_hi > 1u || msg_type > 7u ||
        msg_type == 5u || msg_type == 6u || bti == 0xffu) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* The data port message type is 17:14 on Haswell (Vol 7, "RENDER
     * CACHE DATA PORT" descriptor). It was first encoded at 16:13, read
     * from the wrong column of that table; the 4570 then saw 0110b, a
     * reserved type, and the pixel shader never retired
     * (docs\decisions\2026-10-17-haswell-phase-d-first-triangle.md). */
    *function = ((v9x_u32)V9X_HSW_DP_RC_RT_WRITE << 14) |
                ((v9x_u32)last_rt << 12) | ((v9x_u32)slot_hi << 11) |
                ((v9x_u32)msg_type << 8) | (v9x_u32)bti;
    return V9X_STATUS_OK;
}

v9x_status v9x_intel_hsw_eu_sampler_fc(v9x_u8 simd, v9x_u8 msg_type,
                                       v9x_u8 sampler, v9x_u8 bti,
                                       v9x_u32 *function)
{
    if (function == 0 || simd > 3u || msg_type > 0x1fu || sampler > 15u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *function = ((v9x_u32)simd << 17) | ((v9x_u32)msg_type << 12) |
                ((v9x_u32)sampler << 8) | (v9x_u32)bti;
    return V9X_STATUS_OK;
}

/* Phase D kernels. */
#define V9X_HSW_PS_COMPONENTS ((v9x_u8)4u)
#define V9X_HSW_PS_BARY_GRF   ((v9x_u8)2u)

static void v9x_hsw_ps_zero(struct v9x_hsw_eu_inst *inst)
{
    v9x_u8 *bytes = (v9x_u8 *)inst;
    v9x_u32 index;

    for (index = 0ul; index < (v9x_u32)sizeof(*inst); ++index) {
        bytes[index] = 0u;
    }
}

/* A SIMD8 float register region: <8;8,1>, or <1> as a destination. */
static void v9x_hsw_ps_grf(struct v9x_hsw_eu_operand *op, v9x_u8 nr,
                           v9x_u8 type)
{
    op->file = V9X_HSW_EU_FILE_GRF;
    op->type = type;
    op->nr = nr;
    op->subnr = 0u;
    op->vstride = V9X_HSW_EU_VSTRIDE_8;
    op->width = V9X_HSW_EU_WIDTH_8;
    op->hstride = V9X_HSW_EU_HSTRIDE_1;
}

/* send(8) null g112 RT write: four payload registers (R, G, B, A for
 * slots 7:0, Vol 7 "Color Payload: SIMD8 Single Source"), no header, no
 * response, SIMD8 single source, last render target, EOT. */
static v9x_status v9x_hsw_ps_rt_write(v9x_u32 out[4])
{
    struct v9x_hsw_eu_inst inst;
    v9x_u32 function;
    v9x_u32 desc;

    if (v9x_intel_hsw_eu_rt_write_fc(1u, 0u, V9X_HSW_RT_SIMD8_LO, 0u,
                                     &function) != V9X_STATUS_OK ||
        v9x_intel_hsw_eu_send_desc(V9X_HSW_PS_COMPONENTS, 0u, 0u, function,
                                   &desc) != V9X_STATUS_OK) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_hsw_ps_zero(&inst);
    inst.opcode = V9X_HSW_EU_OP_SEND;
    inst.exec_size = V9X_HSW_EU_EXEC_8;
    inst.sfid = V9X_HSW_SFID_DP_RC;
    inst.eot = 1u;
    inst.desc = desc;
    inst.dst.file = V9X_HSW_EU_FILE_ARF; /* null */
    inst.dst.type = V9X_HSW_EU_TYPE_UD;
    inst.dst.hstride = V9X_HSW_EU_HSTRIDE_1;
    v9x_hsw_ps_grf(&inst.src0, V9X_HSW_PS_PAYLOAD_GRF, V9X_HSW_EU_TYPE_UD);
    return v9x_intel_hsw_eu_encode(&inst, out);
}

v9x_status v9x_intel_hsw_ps_kernel_constant(v9x_u32 *out, v9x_u32 capacity,
                                            const v9x_u32 rgba[4],
                                            v9x_u32 *used)
{
    struct v9x_hsw_eu_inst inst;
    v9x_u8 c;

    if (out == 0 || rgba == 0 || used == 0 ||
        capacity < 4ul * (V9X_HSW_PS_COMPONENTS + 1u)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (c = 0u; c < V9X_HSW_PS_COMPONENTS; ++c) {
        /* mov(8) g(112+c)<1>:F imm:F */
        v9x_hsw_ps_zero(&inst);
        inst.opcode = V9X_HSW_EU_OP_MOV;
        inst.exec_size = V9X_HSW_EU_EXEC_8;
        v9x_hsw_ps_grf(&inst.dst, (v9x_u8)(V9X_HSW_PS_PAYLOAD_GRF + c),
                       V9X_HSW_EU_TYPE_F);
        inst.src0.file = V9X_HSW_EU_FILE_IMM;
        inst.src0.type = V9X_HSW_EU_TYPE_F;
        inst.src0.imm = rgba[c];
        if (v9x_intel_hsw_eu_encode(&inst, out + 4u * c) != V9X_STATUS_OK) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
    }
    if (v9x_hsw_ps_rt_write(out + 4u * V9X_HSW_PS_COMPONENTS) !=
        V9X_STATUS_OK) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *used = 4ul * (V9X_HSW_PS_COMPONENTS + 1u);
    return V9X_STATUS_OK;
}

v9x_status v9x_intel_hsw_ps_kernel_interpolated(v9x_u32 *out,
                                                v9x_u32 capacity,
                                                v9x_u8 setup_grf,
                                                v9x_u32 *used)
{
    struct v9x_hsw_eu_inst inst;
    v9x_u8 c;

    if (out == 0 || used == 0 ||
        capacity < 4ul * (V9X_HSW_PS_COMPONENTS + 1u) ||
        setup_grf < V9X_HSW_PS_BARY_GRF + 2u || setup_grf > 125u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (c = 0u; c < V9X_HSW_PS_COMPONENTS; ++c) {
        /* pln(8) g(112+c)<1>:F g(setup + c/2).(c%2*4)<0;1,0>:F g2<8;8,1>:F
         * - the plane's x and y coefficients times the two barycentrics
         * in g2 and g3, plus its constant (Vol 7 pln). */
        v9x_hsw_ps_zero(&inst);
        inst.opcode = V9X_HSW_EU_OP_PLN;
        inst.exec_size = V9X_HSW_EU_EXEC_8;
        v9x_hsw_ps_grf(&inst.dst, (v9x_u8)(V9X_HSW_PS_PAYLOAD_GRF + c),
                       V9X_HSW_EU_TYPE_F);
        v9x_hsw_ps_grf(&inst.src0, (v9x_u8)(setup_grf + c / 2u),
                       V9X_HSW_EU_TYPE_F);
        inst.src0.subnr = (v9x_u8)((c % 2u) * 16u);
        inst.src0.vstride = V9X_HSW_EU_VSTRIDE_0;
        inst.src0.width = V9X_HSW_EU_WIDTH_1;
        inst.src0.hstride = V9X_HSW_EU_HSTRIDE_0;
        v9x_hsw_ps_grf(&inst.src1, V9X_HSW_PS_BARY_GRF, V9X_HSW_EU_TYPE_F);
        if (v9x_intel_hsw_eu_encode(&inst, out + 4u * c) != V9X_STATUS_OK) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
    }
    if (v9x_hsw_ps_rt_write(out + 4u * V9X_HSW_PS_COMPONENTS) !=
        V9X_STATUS_OK) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *used = 4ul * (V9X_HSW_PS_COMPONENTS + 1u);
    return V9X_STATUS_OK;
}

/*
 * Phase E: the kernel generator. Register map, all SIMD8 float:
 *   g2-g3     perspective pixel barycentrics (payload)
 *   g4-g11    setup data, two registers per attribute: diffuse, specular,
 *             texture coordinates 0, texture coordinates 1
 *   g20-g23   diffuse R G B A          g27      specular alpha (fog)
 *   g28-g29   u0 v0 (sample payload)   g30-g31  u1 v1
 *   g32-g35   texture 0 R G B A        g36-g39  texture 1 R G B A
 *   g40       fog scratch
 *   g112-g115 the colour written
 */
#define V9X_HSW_PSG_SETUP     ((v9x_u8)4u)
#define V9X_HSW_PSG_DIFFUSE   ((v9x_u8)20u)
#define V9X_HSW_PSG_FOG       ((v9x_u8)27u)
#define V9X_HSW_PSG_UV0       ((v9x_u8)28u)
#define V9X_HSW_PSG_UV1       ((v9x_u8)30u)
#define V9X_HSW_PSG_TEX0      ((v9x_u8)32u)
#define V9X_HSW_PSG_TEX1      ((v9x_u8)36u)
#define V9X_HSW_PSG_SCRATCH   ((v9x_u8)40u)
#define V9X_HSW_PSG_ATTR_SPEC ((v9x_u8)1u)
#define V9X_HSW_PSG_ATTR_UV0  ((v9x_u8)2u)
#define V9X_HSW_PSG_ATTR_UV1  ((v9x_u8)3u)
#define V9X_HSW_PSG_SIGN      ((v9x_u32)0x80000000ul)

struct v9x_hsw_psg {
    v9x_u32 *out;
    v9x_u32 capacity;
    v9x_u32 used;
    int failed;
};

static void v9x_hsw_psg_emit(struct v9x_hsw_psg *g,
                             const struct v9x_hsw_eu_inst *inst)
{
    if (g->failed || g->capacity - g->used < 4ul ||
        v9x_intel_hsw_eu_encode(inst, g->out + g->used) != V9X_STATUS_OK) {
        g->failed = 1;
        return;
    }
    g->used += 4ul;
}

/* pln(8) dst attribute a, component c. */
static void v9x_hsw_psg_pln(struct v9x_hsw_psg *g, v9x_u8 dst, v9x_u8 a,
                            v9x_u8 c)
{
    struct v9x_hsw_eu_inst inst;

    v9x_hsw_ps_zero(&inst);
    inst.opcode = V9X_HSW_EU_OP_PLN;
    inst.exec_size = V9X_HSW_EU_EXEC_8;
    v9x_hsw_ps_grf(&inst.dst, dst, V9X_HSW_EU_TYPE_F);
    v9x_hsw_ps_grf(&inst.src0,
                   (v9x_u8)(V9X_HSW_PSG_SETUP + 2u * a + c / 2u),
                   V9X_HSW_EU_TYPE_F);
    inst.src0.subnr = (v9x_u8)((c % 2u) * 16u);
    inst.src0.vstride = V9X_HSW_EU_VSTRIDE_0;
    inst.src0.width = V9X_HSW_EU_WIDTH_1;
    inst.src0.hstride = V9X_HSW_EU_HSTRIDE_0;
    v9x_hsw_ps_grf(&inst.src1, V9X_HSW_PS_BARY_GRF, V9X_HSW_EU_TYPE_F);
    v9x_hsw_psg_emit(g, &inst);
}

/* A two-source float operation; src1 is a register, or an immediate
 * when imm_file is set. */
static void v9x_hsw_psg_op(struct v9x_hsw_psg *g, v9x_u8 opcode, v9x_u8 dst,
                           v9x_u8 src0, v9x_u8 src1, int imm_file,
                           v9x_u32 imm, v9x_u8 saturate)
{
    struct v9x_hsw_eu_inst inst;

    v9x_hsw_ps_zero(&inst);
    inst.opcode = opcode;
    inst.exec_size = V9X_HSW_EU_EXEC_8;
    inst.saturate = saturate;
    v9x_hsw_ps_grf(&inst.dst, dst, V9X_HSW_EU_TYPE_F);
    v9x_hsw_ps_grf(&inst.src0, src0, V9X_HSW_EU_TYPE_F);
    if (opcode == V9X_HSW_EU_OP_MOV) {
        /* One source: the encoder makes src1 null. */
    } else if (imm_file) {
        inst.src1.file = V9X_HSW_EU_FILE_IMM;
        inst.src1.type = V9X_HSW_EU_TYPE_F;
        inst.src1.imm = imm;
    } else {
        v9x_hsw_ps_grf(&inst.src1, src1, V9X_HSW_EU_TYPE_F);
    }
    v9x_hsw_psg_emit(g, &inst);
}

/* send(8) dst..dst+3 the SIMD8 "sample" message (type 0) with u, v from
 * payload, sampler 0, binding table entry bti; no header (Vol 7). */
static void v9x_hsw_psg_sample(struct v9x_hsw_psg *g, v9x_u8 dst,
                               v9x_u8 payload, v9x_u8 bti)
{
    struct v9x_hsw_eu_inst inst;
    v9x_u32 function;
    v9x_u32 desc;

    if (v9x_intel_hsw_eu_sampler_fc(V9X_HSW_SAMPLER_SIMD8, 0u, 0u, bti,
                                    &function) != V9X_STATUS_OK ||
        v9x_intel_hsw_eu_send_desc(2u, V9X_HSW_PS_COMPONENTS, 0u, function,
                                   &desc) != V9X_STATUS_OK) {
        g->failed = 1;
        return;
    }
    v9x_hsw_ps_zero(&inst);
    inst.opcode = V9X_HSW_EU_OP_SEND;
    inst.exec_size = V9X_HSW_EU_EXEC_8;
    inst.sfid = V9X_HSW_SFID_SAMPLER;
    inst.desc = desc;
    v9x_hsw_ps_grf(&inst.dst, dst, V9X_HSW_EU_TYPE_UW);
    v9x_hsw_ps_grf(&inst.src0, payload, V9X_HSW_EU_TYPE_UD);
    v9x_hsw_psg_emit(g, &inst);
}

v9x_status v9x_intel_hsw_ps_kernel_build(v9x_u32 *out, v9x_u32 capacity,
                                         const struct v9x_hsw_ps_desc *desc,
                                         v9x_u32 *used)
{
    struct v9x_hsw_psg g;
    v9x_u8 c;
    v9x_u8 out_reg;
    int textured;

    if (out == 0 || desc == 0 || used == 0 ||
        desc->stage0 > V9X_HSW_PS_DECAL ||
        desc->stage1 > V9X_HSW_PS_MODULATE_TEX1) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    g.out = out;
    g.capacity = capacity;
    g.used = 0ul;
    g.failed = 0;
    textured = desc->stage0 != V9X_HSW_PS_DIFFUSE;

    /* Interpolate what the stages read. */
    if (desc->stage0 != V9X_HSW_PS_TEXTURE) {
        for (c = 0u; c < V9X_HSW_PS_COMPONENTS; ++c) {
            v9x_hsw_psg_pln(&g, (v9x_u8)(V9X_HSW_PSG_DIFFUSE + c), 0u, c);
        }
    }
    if (desc->fog) {
        v9x_hsw_psg_pln(&g, V9X_HSW_PSG_FOG, V9X_HSW_PSG_ATTR_SPEC, 3u);
    }
    if (textured) {
        v9x_hsw_psg_pln(&g, V9X_HSW_PSG_UV0, V9X_HSW_PSG_ATTR_UV0, 0u);
        v9x_hsw_psg_pln(&g, (v9x_u8)(V9X_HSW_PSG_UV0 + 1u),
                        V9X_HSW_PSG_ATTR_UV0, 1u);
        v9x_hsw_psg_sample(&g, V9X_HSW_PSG_TEX0, V9X_HSW_PSG_UV0, 1u);
    }
    if (desc->stage1 == V9X_HSW_PS_MODULATE_TEX1) {
        v9x_hsw_psg_pln(&g, V9X_HSW_PSG_UV1, V9X_HSW_PSG_ATTR_UV1, 0u);
        v9x_hsw_psg_pln(&g, (v9x_u8)(V9X_HSW_PSG_UV1 + 1u),
                        V9X_HSW_PSG_ATTR_UV1, 1u);
        v9x_hsw_psg_sample(&g, V9X_HSW_PSG_TEX1, V9X_HSW_PSG_UV1, 2u);
    }

    /* Stage 0 into g112-g115. */
    for (c = 0u; c < V9X_HSW_PS_COMPONENTS; ++c) {
        out_reg = (v9x_u8)(V9X_HSW_PS_PAYLOAD_GRF + c);
        switch (desc->stage0) {
        case V9X_HSW_PS_DIFFUSE:
            v9x_hsw_psg_op(&g, V9X_HSW_EU_OP_MOV, out_reg,
                           (v9x_u8)(V9X_HSW_PSG_DIFFUSE + c), 0u, 0, 0ul, 0u);
            break;
        case V9X_HSW_PS_TEXTURE:
            v9x_hsw_psg_op(&g, V9X_HSW_EU_OP_MOV, out_reg,
                           (v9x_u8)(V9X_HSW_PSG_TEX0 + c), 0u, 0, 0ul, 0u);
            break;
        case V9X_HSW_PS_MODULATE:
            v9x_hsw_psg_op(&g, V9X_HSW_EU_OP_MUL, out_reg,
                           (v9x_u8)(V9X_HSW_PSG_TEX0 + c),
                           (v9x_u8)(V9X_HSW_PSG_DIFFUSE + c), 0, 0ul, 1u);
            break;
        case V9X_HSW_PS_ADD:
            /* D3DTOP_ADD on colour, modulate on alpha. */
            v9x_hsw_psg_op(&g,
                           c == 3u ? V9X_HSW_EU_OP_MUL : V9X_HSW_EU_OP_ADD,
                           out_reg, (v9x_u8)(V9X_HSW_PSG_TEX0 + c),
                           (v9x_u8)(V9X_HSW_PSG_DIFFUSE + c), 0, 0ul, 1u);
            break;
        default:
            /* Decal: the texture's colour, the diffuse alpha. */
            v9x_hsw_psg_op(&g, V9X_HSW_EU_OP_MOV, out_reg,
                           (v9x_u8)(c == 3u ? V9X_HSW_PSG_DIFFUSE + c :
                                              V9X_HSW_PSG_TEX0 + c),
                           0u, 0, 0ul, 0u);
            break;
        }
    }
    /* Stage 1: modulate everything by the second texture. */
    if (desc->stage1 == V9X_HSW_PS_MODULATE_TEX1) {
        for (c = 0u; c < V9X_HSW_PS_COMPONENTS; ++c) {
            out_reg = (v9x_u8)(V9X_HSW_PS_PAYLOAD_GRF + c);
            v9x_hsw_psg_op(&g, V9X_HSW_EU_OP_MUL, out_reg, out_reg,
                           (v9x_u8)(V9X_HSW_PSG_TEX1 + c), 0, 0ul, 1u);
        }
    }
    /* Vertex fog: colour = (colour - fog) * f + fog, f = specular alpha. */
    if (desc->fog) {
        for (c = 0u; c < 3u; ++c) {
            out_reg = (v9x_u8)(V9X_HSW_PS_PAYLOAD_GRF + c);
            v9x_hsw_psg_op(&g, V9X_HSW_EU_OP_ADD, V9X_HSW_PSG_SCRATCH,
                           out_reg, 0u, 1,
                           desc->fog_rgb[c] ^ V9X_HSW_PSG_SIGN, 0u);
            v9x_hsw_psg_op(&g, V9X_HSW_EU_OP_MUL, V9X_HSW_PSG_SCRATCH,
                           V9X_HSW_PSG_SCRATCH, V9X_HSW_PSG_FOG, 0, 0ul, 0u);
            v9x_hsw_psg_op(&g, V9X_HSW_EU_OP_ADD, out_reg,
                           V9X_HSW_PSG_SCRATCH, 0u, 1, desc->fog_rgb[c], 1u);
        }
    }

    if (g.failed || g.capacity - g.used < 4ul ||
        v9x_hsw_ps_rt_write(g.out + g.used) != V9X_STATUS_OK) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *used = g.used + 4ul;
    return V9X_STATUS_OK;
}
