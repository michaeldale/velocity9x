/*
 * Gen4 (965 / GM965) EU encoding and the Direct3D SF and WM kernels.
 * See velocity9x\intel_g4_eu.h for the sources and what is not covered.
 *
 * Field positions are the Gen4 columns of Mesa's brw_inst.h:
 *   DW0  6:0 opcode, 9 mask control, 13:12 compression, 23:21 exec size,
 *        27:24 condition modifier (for send: the message register base);
 *   DW1  1:0 dst file, 4:2 dst type, 6:5 src0 file, 9:7 src0 type,
 *        11:10 src1 file, 14:12 src1 type, 20:16 dst subregister (bytes),
 *        28:21 dst register, 30:29 dst horizontal stride;
 *   DW2  src0: 4:0 subregister, 12:5 register, 14 negate, 17:16 hstride,
 *        20:18 width, 24:21 vstride;
 *   DW3  src1 in DW2's layout, or an immediate, or for send the message
 *        descriptor: 15:0 function control, 19:16 response length,
 *        23:20 message length, 27:24 target, 31 end of thread.
 * The host test checks these against xf86-video-intel's exa_*.g4b.
 */
#include "velocity9x/intel_g4_eu.h"

/* Register numbers and offsets the kernels use. */
#define V9X_G4_SF_VERTEX_BASE 3u   /* SF_STATE dispatch GRF start for URB */
#define V9X_G4_WM_SETUP_BASE  3u   /* WM_STATE dispatch GRF start         */

/* EU prefetch runs past a kernel's last instruction; xf86's Gen4 kernels
 * end in eight nops for it, and these do the same. */
#define V9X_G4_KERNEL_TAIL_NOPS 8u

/* URB write: swizzle control 10b, transpose (SF to windower). */
#define V9X_G4_URB_SWIZZLE_TRANSPOSE 2u

/* Render target write, Gen4: message type 4, control 0 (SIMD16 single
 * source) with bit 3 set as xf86's exa_wm_write.g4b has it (desc 0x4800). */
#define V9X_G4_RT_WRITE_TYPE    4u
#define V9X_G4_RT_WRITE_CONTROL 8u

static v9x_u32 v9x_g4_float_bits(float value)
{
    union {
        float f;
        v9x_u32 u;
    } convert;

    convert.f = value;
    return convert.u;
}

struct v9x_g4_reg v9x_g4_grf(v9x_u8 nr, v9x_u8 sub, v9x_u8 type)
{
    struct v9x_g4_reg reg;

    reg.file = V9X_G4_EU_FILE_GRF;
    reg.type = type;
    reg.nr = nr;
    reg.sub = sub;
    reg.vstride = V9X_G4_EU_VSTRIDE_8;
    reg.width = V9X_G4_EU_WIDTH_8;
    reg.hstride = V9X_G4_EU_HSTRIDE_1;
    reg.negate = 0u;
    reg.imm = 0ul;
    return reg;
}

struct v9x_g4_reg v9x_g4_mrf(v9x_u8 nr, v9x_u8 type)
{
    struct v9x_g4_reg reg = v9x_g4_grf(nr, 0u, type);

    reg.file = V9X_G4_EU_FILE_MRF;
    return reg;
}

struct v9x_g4_reg v9x_g4_null(v9x_u8 type)
{
    struct v9x_g4_reg reg = v9x_g4_grf(V9X_G4_EU_ARF_NULL, 0u, type);

    reg.file = V9X_G4_EU_FILE_ARF;
    return reg;
}

struct v9x_g4_reg v9x_g4_imm_ud(v9x_u32 value)
{
    struct v9x_g4_reg reg = v9x_g4_grf(0u, 0u, V9X_G4_EU_TYPE_UD);

    reg.file = V9X_G4_EU_FILE_IMM;
    reg.vstride = 0u;
    reg.width = 0u;
    reg.hstride = 0u;
    reg.imm = value;
    return reg;
}

struct v9x_g4_reg v9x_g4_imm_f(float value)
{
    struct v9x_g4_reg reg = v9x_g4_imm_ud(v9x_g4_float_bits(value));

    reg.type = V9X_G4_EU_TYPE_F;
    return reg;
}

/* A packed vector of eight signed 4-bit integers, element 0 in 3:0. */
struct v9x_g4_reg v9x_g4_imm_v(v9x_u32 nibbles)
{
    struct v9x_g4_reg reg = v9x_g4_imm_ud(nibbles);

    reg.type = V9X_G4_EU_TYPE_IMM_V;
    return reg;
}

struct v9x_g4_reg v9x_g4_region(struct v9x_g4_reg reg, v9x_u8 vstride,
                                v9x_u8 width, v9x_u8 hstride)
{
    reg.vstride = vstride;
    reg.width = width;
    reg.hstride = hstride;
    return reg;
}

/* One element of a GRF, replicated: <0,1,0>. */
struct v9x_g4_reg v9x_g4_scalar(v9x_u8 nr, v9x_u8 sub, v9x_u8 type)
{
    return v9x_g4_region(v9x_g4_grf(nr, sub, type), V9X_G4_EU_VSTRIDE_0,
                         V9X_G4_EU_WIDTH_1, V9X_G4_EU_HSTRIDE_0);
}

struct v9x_g4_reg v9x_g4_neg(struct v9x_g4_reg reg)
{
    reg.negate = (v9x_u8)(reg.negate ^ 1u);
    return reg;
}

void v9x_g4_program_init(struct v9x_g4_program *program,
                         struct v9x_g4_inst *storage, v9x_u32 capacity)
{
    program->inst = storage;
    program->capacity = capacity;
    program->count = 0ul;
    program->overflow = 0u;
}

static struct v9x_g4_inst *v9x_g4_next(struct v9x_g4_program *program)
{
    struct v9x_g4_inst *inst;

    if (program->count >= program->capacity) {
        program->overflow = 1u;
        return 0;
    }
    inst = &program->inst[program->count++];
    inst->dw[0] = 0ul;
    inst->dw[1] = 0ul;
    inst->dw[2] = 0ul;
    inst->dw[3] = 0ul;
    return inst;
}

/* A source's register-region word, DW2's layout (DW3's for src1). */
static v9x_u32 v9x_g4_source_word(const struct v9x_g4_reg *reg)
{
    return ((v9x_u32)reg->sub & 0x1ful) |
           ((v9x_u32)reg->nr << 5) |
           ((v9x_u32)reg->negate << 14) |
           ((v9x_u32)reg->hstride << 16) |
           ((v9x_u32)reg->width << 18) |
           ((v9x_u32)reg->vstride << 21);
}

/* DW1's destination half; the destination stride is always 1. */
static v9x_u32 v9x_g4_dst_word(const struct v9x_g4_reg *dst)
{
    return (v9x_u32)dst->file |
           ((v9x_u32)dst->type << 2) |
           (((v9x_u32)dst->sub & 0x1ful) << 16) |
           ((v9x_u32)dst->nr << 21) |
           ((v9x_u32)V9X_G4_EU_HSTRIDE_1 << 29);
}

void v9x_g4_alu(struct v9x_g4_program *program, v9x_u8 opcode,
                v9x_u8 exec_size, v9x_u8 qtr, v9x_u8 mask_disable,
                struct v9x_g4_reg dst, struct v9x_g4_reg src0,
                struct v9x_g4_reg src1)
{
    struct v9x_g4_inst *inst = v9x_g4_next(program);
    int has_src1 = opcode != V9X_G4_EU_OP_MOV;

    if (inst == 0) {
        return;
    }
    inst->dw[0] = (v9x_u32)opcode | ((v9x_u32)mask_disable << 9) |
                  ((v9x_u32)qtr << 12) | ((v9x_u32)exec_size << 21);
    inst->dw[1] = v9x_g4_dst_word(&dst) |
                  ((v9x_u32)src0.file << 5) | ((v9x_u32)src0.type << 7);

    /* An immediate is allowed as the last source only, and sits in DW3. */
    if (src0.file == V9X_G4_EU_FILE_IMM) {
        inst->dw[3] = src0.imm;
        return;
    }
    inst->dw[2] = v9x_g4_source_word(&src0);
    if (!has_src1) {
        return;
    }
    inst->dw[1] |= ((v9x_u32)src1.file << 10) | ((v9x_u32)src1.type << 12);
    inst->dw[3] = src1.file == V9X_G4_EU_FILE_IMM ? src1.imm
                                                   : v9x_g4_source_word(&src1);
}

void v9x_g4_send(struct v9x_g4_program *program, v9x_u8 exec_size,
                 v9x_u8 qtr, struct v9x_g4_reg dst, struct v9x_g4_reg src0,
                 v9x_u8 base_mrf, v9x_u8 sfid, v9x_u8 mlen, v9x_u8 rlen,
                 v9x_u16 function_control, v9x_u8 eot)
{
    struct v9x_g4_inst *inst = v9x_g4_next(program);

    if (inst == 0) {
        return;
    }
    inst->dw[0] = (v9x_u32)V9X_G4_EU_OP_SEND | ((v9x_u32)qtr << 12) |
                  ((v9x_u32)exec_size << 21) | ((v9x_u32)base_mrf << 24);
    /* The descriptor is an immediate D in src1's place, as Mesa and xf86
     * encode it. */
    inst->dw[1] = v9x_g4_dst_word(&dst) |
                  ((v9x_u32)src0.file << 5) | ((v9x_u32)src0.type << 7) |
                  ((v9x_u32)V9X_G4_EU_FILE_IMM << 10) |
                  ((v9x_u32)V9X_G4_EU_TYPE_D << 12);
    if (src0.file != V9X_G4_EU_FILE_ARF) {
        inst->dw[2] = v9x_g4_source_word(&src0);
    }
    inst->dw[3] = (v9x_u32)function_control |
                  ((v9x_u32)(rlen & 0xfu) << 16) |
                  ((v9x_u32)(mlen & 0xfu) << 20) |
                  ((v9x_u32)(sfid & 0xfu) << 24) |
                  ((v9x_u32)(eot != 0u) << 31);
}

void v9x_g4_nop(struct v9x_g4_program *program)
{
    struct v9x_g4_inst *inst = v9x_g4_next(program);

    if (inst != 0) {
        inst->dw[0] = V9X_G4_EU_OP_NOP;
    }
}

v9x_u16 v9x_g4_math_desc(v9x_u8 function)
{
    /* 3:0 function; precision full (5 clear), no saturate, float data. */
    return (v9x_u16)(function & 0xfu);
}

v9x_u16 v9x_g4_urb_write_desc(v9x_u8 offset, v9x_u8 complete)
{
    /* 15 complete, 14 used, 13 allocate (clear), 11:10 swizzle,
     * 9:4 global offset, 3:0 opcode 0 (write). */
    return (v9x_u16)(((complete != 0u) ? 0x8000u : 0u) | 0x4000u |
                     (V9X_G4_URB_SWIZZLE_TRANSPOSE << 10) |
                     (((v9x_u16)offset & 0x3fu) << 4));
}

v9x_u16 v9x_g4_sampler_desc(v9x_u8 binding_table, v9x_u8 sampler)
{
    /* 15:14 message type 0 (SIMD16 sample), 13:12 return format 0
     * (float32), 11:8 sampler, 7:0 binding table index. */
    return (v9x_u16)(((v9x_u16)(sampler & 0xfu) << 8) | binding_table);
}

v9x_u16 v9x_g4_rt_write_desc(v9x_u8 binding_table)
{
    return (v9x_u16)((V9X_G4_RT_WRITE_TYPE << 12) |
                     (V9X_G4_RT_WRITE_CONTROL << 8) | binding_table);
}

static void v9x_g4_tail(struct v9x_g4_program *program)
{
    v9x_u32 index;

    for (index = 0ul; index < V9X_G4_KERNEL_TAIL_NOPS; ++index) {
        v9x_g4_nop(program);
    }
}

/* SIMD8 ALU on whole registers, float. */
static void v9x_g4_alu8(struct v9x_g4_program *program, v9x_u8 opcode,
                        struct v9x_g4_reg dst, struct v9x_g4_reg src0,
                        struct v9x_g4_reg src1)
{
    v9x_g4_alu(program, opcode, V9X_G4_EU_EXEC_8, V9X_G4_EU_QTR_NONE, 0u, dst,
               src0, src1);
}

static struct v9x_g4_reg v9x_g4_f(v9x_u8 nr)
{
    return v9x_g4_grf(nr, 0u, V9X_G4_EU_TYPE_F);
}

/*
 * The SF payload (Mesa brw_compile_sf.c alloc_regs): g1.2 det, g1.3 dx0,
 * g1.4 dx2, g1.5 dy0, g1.6 dy2 as floats; g2 z0, 1/w0, z1, 1/w1, z2, 1/w2;
 * vertex i's setup registers from g3 + i * setup_regs.
 */
v9x_status v9x_g4_sf_kernel(struct v9x_g4_program *program,
                            v9x_u8 setup_regs, v9x_u8 *grf_used)
{
    v9x_u8 vertex[3];
    v9x_u8 inv_det;
    v9x_u8 d1;
    v9x_u8 d2;
    v9x_u8 t3;
    v9x_u8 t4;
    v9x_u8 i;
    v9x_u8 r;
    struct v9x_g4_reg dx0 = v9x_g4_scalar(1u, 12u, V9X_G4_EU_TYPE_F);
    struct v9x_g4_reg dx2 = v9x_g4_scalar(1u, 16u, V9X_G4_EU_TYPE_F);
    struct v9x_g4_reg dy0 = v9x_g4_scalar(1u, 20u, V9X_G4_EU_TYPE_F);
    struct v9x_g4_reg dy2 = v9x_g4_scalar(1u, 24u, V9X_G4_EU_TYPE_F);

    if (program == 0 || grf_used == 0 || setup_regs == 0u ||
        setup_regs > 8u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (i = 0u; i < 3u; ++i) {
        vertex[i] = (v9x_u8)(V9X_G4_SF_VERTEX_BASE + i * setup_regs);
    }
    inv_det = (v9x_u8)(V9X_G4_SF_VERTEX_BASE + 3u * setup_regs);
    d1 = (v9x_u8)(inv_det + 1u);
    d2 = (v9x_u8)(inv_det + 2u);
    t3 = (v9x_u8)(inv_det + 3u);
    t4 = (v9x_u8)(inv_det + 4u);
    *grf_used = (v9x_u8)(inv_det + 5u);

    /* 1/det in inv_det.0: a 4-wide inverse of g1.2-g1.5, as xf86's
     * exa_sf.g4b inverts g1.3-g1.6. */
    v9x_g4_send(program, V9X_G4_EU_EXEC_4, V9X_G4_EU_QTR_NONE,
                v9x_g4_f(inv_det),
                v9x_g4_region(v9x_g4_grf(1u, 8u, V9X_G4_EU_TYPE_F),
                              V9X_G4_EU_VSTRIDE_4, V9X_G4_EU_WIDTH_4,
                              V9X_G4_EU_HSTRIDE_1),
                0u, V9X_G4_SFID_MATH, 1u, 1u,
                v9x_g4_math_desc(V9X_G4_MATH_INV), 0u);

    /* z and 1/w into the first attribute's z and w, per vertex. */
    for (i = 0u; i < 3u; ++i) {
        v9x_g4_alu(program, V9X_G4_EU_OP_MOV, V9X_G4_EU_EXEC_2,
                   V9X_G4_EU_QTR_NONE, 0u,
                   v9x_g4_grf(vertex[i], 8u, V9X_G4_EU_TYPE_F),
                   v9x_g4_region(v9x_g4_grf(2u, (v9x_u8)(i * 8u),
                                            V9X_G4_EU_TYPE_F),
                                 V9X_G4_EU_VSTRIDE_2, V9X_G4_EU_WIDTH_2,
                                 V9X_G4_EU_HSTRIDE_1),
                   v9x_g4_null(V9X_G4_EU_TYPE_F));
    }

    for (r = 0u; r < setup_regs; ++r) {
        struct v9x_g4_reg a0 = v9x_g4_f((v9x_u8)(vertex[0] + r));
        struct v9x_g4_reg a1 = v9x_g4_f((v9x_u8)(vertex[1] + r));
        struct v9x_g4_reg a2 = v9x_g4_f((v9x_u8)(vertex[2] + r));
        struct v9x_g4_reg det = v9x_g4_scalar(inv_det, 0u, V9X_G4_EU_TYPE_F);

        v9x_g4_alu8(program, V9X_G4_EU_OP_ADD, v9x_g4_f(d1), a1,
                    v9x_g4_neg(a0));
        v9x_g4_alu8(program, V9X_G4_EU_OP_ADD, v9x_g4_f(d2), a2,
                    v9x_g4_neg(a0));
        /* dA/dx = (d1 * dy2 - d2 * dy0) / det */
        v9x_g4_alu8(program, V9X_G4_EU_OP_MUL, v9x_g4_f(t3), v9x_g4_f(d1), dy2);
        v9x_g4_alu8(program, V9X_G4_EU_OP_MUL, v9x_g4_f(t4), v9x_g4_f(d2), dy0);
        v9x_g4_alu8(program, V9X_G4_EU_OP_ADD, v9x_g4_f(t3), v9x_g4_f(t3),
                    v9x_g4_neg(v9x_g4_f(t4)));
        v9x_g4_alu8(program, V9X_G4_EU_OP_MUL,
                    v9x_g4_mrf(1u, V9X_G4_EU_TYPE_F), v9x_g4_f(t3), det);
        /* dA/dy = (d2 * dx0 - d1 * dx2) / det */
        v9x_g4_alu8(program, V9X_G4_EU_OP_MUL, v9x_g4_f(t3), v9x_g4_f(d2), dx0);
        v9x_g4_alu8(program, V9X_G4_EU_OP_MUL, v9x_g4_f(t4), v9x_g4_f(d1), dx2);
        v9x_g4_alu8(program, V9X_G4_EU_OP_ADD, v9x_g4_f(t3), v9x_g4_f(t3),
                    v9x_g4_neg(v9x_g4_f(t4)));
        v9x_g4_alu8(program, V9X_G4_EU_OP_MUL,
                    v9x_g4_mrf(2u, V9X_G4_EU_TYPE_F), v9x_g4_f(t3), det);
        /* The value at vertex 0. */
        v9x_g4_alu8(program, V9X_G4_EU_OP_MOV,
                    v9x_g4_mrf(3u, V9X_G4_EU_TYPE_F), a0,
                    v9x_g4_null(V9X_G4_EU_TYPE_F));
        /* m0 is g0, by the send's implied move. */
        v9x_g4_send(program, V9X_G4_EU_EXEC_8, V9X_G4_EU_QTR_NONE,
                    v9x_g4_null(V9X_G4_EU_TYPE_F), v9x_g4_f(0u), 0u,
                    V9X_G4_SFID_URB, 4u, 0u,
                    v9x_g4_urb_write_desc((v9x_u8)(r * 4u),
                                          (v9x_u8)(r + 1u == setup_regs)),
                    (v9x_u8)(r + 1u == setup_regs));
    }
    v9x_g4_tail(program);
    return program->overflow ? V9X_STATUS_INSUFFICIENT_MEMORY : V9X_STATUS_OK;
}

/* WM register plan, SIMD16 vectors being two registers. */
#define V9X_G4_WM_X     14u
#define V9X_G4_WM_Y     16u
#define V9X_G4_WM_XUW   18u
#define V9X_G4_WM_YUW   19u
#define V9X_G4_WM_T0    20u
#define V9X_G4_WM_T1    22u
#define V9X_G4_WM_COLOR 24u   /* r g b a: 24-31 */
#define V9X_G4_WM_DIFF  32u   /* 32-39 */
#define V9X_G4_WM_TEX   40u   /* sampler return, r g b a: 40-47 */
#define V9X_G4_WM_SPEC  48u   /* 48-55 */
#define V9X_G4_WM_W     56u
#define V9X_G4_WM_USED  58u

/* Attribute order in the setup data (intel_g4_3d.h's VUE). */
#define V9X_G4_ATTR_DIFFUSE  1u
#define V9X_G4_ATTR_SPECULAR 2u
#define V9X_G4_ATTR_TEX0     3u

static void v9x_g4_alu16(struct v9x_g4_program *program, v9x_u8 opcode,
                         struct v9x_g4_reg dst, struct v9x_g4_reg src0,
                         struct v9x_g4_reg src1)
{
    v9x_g4_alu(program, opcode, V9X_G4_EU_EXEC_16, V9X_G4_EU_QTR_COMPR, 0u,
               dst, src0, src1);
}

/* A coefficient of attribute k, component c: Cx at +0, Cy at +4, C0 at
 * +12 bytes of the component's 16-byte group, two components a register. */
static struct v9x_g4_reg v9x_g4_coef(v9x_u8 k, v9x_u8 c, v9x_u8 which)
{
    return v9x_g4_scalar((v9x_u8)(V9X_G4_WM_SETUP_BASE + k * 2u + c / 2u),
                         (v9x_u8)((c % 2u) * 16u + which),
                         V9X_G4_EU_TYPE_F);
}

/* dst = X * Cx + Y * Cy + C0 for attribute k, component c. */
static void v9x_g4_interp(struct v9x_g4_program *program,
                          struct v9x_g4_reg dst, v9x_u8 k, v9x_u8 c)
{
    v9x_g4_alu16(program, V9X_G4_EU_OP_MUL, v9x_g4_f(V9X_G4_WM_T0),
                 v9x_g4_f(V9X_G4_WM_X), v9x_g4_coef(k, c, 0u));
    v9x_g4_alu16(program, V9X_G4_EU_OP_MUL, v9x_g4_f(V9X_G4_WM_T1),
                 v9x_g4_f(V9X_G4_WM_Y), v9x_g4_coef(k, c, 4u));
    v9x_g4_alu16(program, V9X_G4_EU_OP_ADD, v9x_g4_f(V9X_G4_WM_T0),
                 v9x_g4_f(V9X_G4_WM_T0), v9x_g4_f(V9X_G4_WM_T1));
    v9x_g4_alu16(program, V9X_G4_EU_OP_ADD, dst, v9x_g4_f(V9X_G4_WM_T0),
                 v9x_g4_coef(k, c, 12u));
}

static struct v9x_g4_reg v9x_g4_comp(v9x_u8 base, v9x_u8 c)
{
    return v9x_g4_f((v9x_u8)(base + c * 2u));
}

v9x_status v9x_g4_wm_kernel(struct v9x_g4_program *program,
                            const struct v9x_g4_ps_desc *desc,
                            v9x_u8 *grf_used)
{
    v9x_u8 c;
    int textured;

    if (program == 0 || desc == 0 || grf_used == 0 ||
        desc->stage0 > V9X_G4_PS_ADD) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    textured = desc->stage0 != V9X_G4_PS_DIFFUSE;
    *grf_used = V9X_G4_WM_USED;

    /*
     * Pixel positions relative to the setup origin: xf86's exa_wm_xy.g4a
     * and Mesa's Gen4 delta_xy. g1.8 holds each subspan's upper-left x and
     * g1.10 its y as words; the packed vectors spread a 2x2 subspan.
     */
    v9x_g4_alu(program, V9X_G4_EU_OP_ADD, V9X_G4_EU_EXEC_16,
               V9X_G4_EU_QTR_NONE, 0u,
               v9x_g4_grf(V9X_G4_WM_XUW, 0u, V9X_G4_EU_TYPE_UW),
               v9x_g4_region(v9x_g4_grf(1u, 8u, V9X_G4_EU_TYPE_UW),
                             V9X_G4_EU_VSTRIDE_2, V9X_G4_EU_WIDTH_4,
                             V9X_G4_EU_HSTRIDE_0),
               v9x_g4_imm_v(0x10101010ul));
    v9x_g4_alu(program, V9X_G4_EU_OP_ADD, V9X_G4_EU_EXEC_16,
               V9X_G4_EU_QTR_NONE, 0u,
               v9x_g4_grf(V9X_G4_WM_YUW, 0u, V9X_G4_EU_TYPE_UW),
               v9x_g4_region(v9x_g4_grf(1u, 10u, V9X_G4_EU_TYPE_UW),
                             V9X_G4_EU_VSTRIDE_2, V9X_G4_EU_WIDTH_4,
                             V9X_G4_EU_HSTRIDE_0),
               v9x_g4_imm_v(0x11001100ul));
    v9x_g4_alu16(program, V9X_G4_EU_OP_ADD, v9x_g4_f(V9X_G4_WM_X),
                 v9x_g4_grf(V9X_G4_WM_XUW, 0u, V9X_G4_EU_TYPE_UW),
                 v9x_g4_neg(v9x_g4_scalar(1u, 0u, V9X_G4_EU_TYPE_F)));
    v9x_g4_alu16(program, V9X_G4_EU_OP_ADD, v9x_g4_f(V9X_G4_WM_Y),
                 v9x_g4_grf(V9X_G4_WM_YUW, 0u, V9X_G4_EU_TYPE_UW),
                 v9x_g4_neg(v9x_g4_scalar(1u, 4u, V9X_G4_EU_TYPE_F)));

    for (c = 0u; c < 4u; ++c) {
        v9x_g4_interp(program, v9x_g4_comp(V9X_G4_WM_DIFF, c),
                      V9X_G4_ATTR_DIFFUSE, c);
    }

    if (textured) {
        /*
         * Perspective: the vertices carry u/w, v/w and 1/w (the CPU
         * multiplied by rhw), so 1/w is interpolated, inverted per pixel in
         * two SIMD8 halves as xf86's exa_wm_projective.g4i does, and
         * multiplies the interpolated u/w and v/w into the message.
         */
        v9x_g4_interp(program, v9x_g4_f(V9X_G4_WM_T1), V9X_G4_ATTR_TEX0, 2u);
        v9x_g4_send(program, V9X_G4_EU_EXEC_8, V9X_G4_EU_QTR_NONE,
                    v9x_g4_f(V9X_G4_WM_W), v9x_g4_f(V9X_G4_WM_T1), 0u,
                    V9X_G4_SFID_MATH, 1u, 1u,
                    v9x_g4_math_desc(V9X_G4_MATH_INV), 0u);
        v9x_g4_send(program, V9X_G4_EU_EXEC_8, V9X_G4_EU_QTR_SECHALF,
                    v9x_g4_f((v9x_u8)(V9X_G4_WM_W + 1u)),
                    v9x_g4_f((v9x_u8)(V9X_G4_WM_T1 + 1u)), 0u,
                    V9X_G4_SFID_MATH, 1u, 1u,
                    v9x_g4_math_desc(V9X_G4_MATH_INV), 0u);
        v9x_g4_interp(program, v9x_g4_f(V9X_G4_WM_T1), V9X_G4_ATTR_TEX0, 0u);
        v9x_g4_alu16(program, V9X_G4_EU_OP_MUL,
                     v9x_g4_mrf(2u, V9X_G4_EU_TYPE_F), v9x_g4_f(V9X_G4_WM_T1),
                     v9x_g4_f(V9X_G4_WM_W));
        v9x_g4_interp(program, v9x_g4_f(V9X_G4_WM_T1), V9X_G4_ATTR_TEX0, 1u);
        v9x_g4_alu16(program, V9X_G4_EU_OP_MUL,
                     v9x_g4_mrf(4u, V9X_G4_EU_TYPE_F), v9x_g4_f(V9X_G4_WM_T1),
                     v9x_g4_f(V9X_G4_WM_W));
        /* The header, as exa_wm_src_sample_argb.g4i builds it. */
        v9x_g4_alu(program, V9X_G4_EU_OP_MOV, V9X_G4_EU_EXEC_1,
                   V9X_G4_EU_QTR_NONE, 1u,
                   v9x_g4_grf(0u, 8u, V9X_G4_EU_TYPE_UD), v9x_g4_imm_ud(0ul),
                   v9x_g4_null(V9X_G4_EU_TYPE_UD));
        v9x_g4_alu(program, V9X_G4_EU_OP_MOV, V9X_G4_EU_EXEC_8,
                   V9X_G4_EU_QTR_NONE, 0u, v9x_g4_mrf(1u, V9X_G4_EU_TYPE_UD),
                   v9x_g4_grf(0u, 0u, V9X_G4_EU_TYPE_UD),
                   v9x_g4_null(V9X_G4_EU_TYPE_UD));
        v9x_g4_send(program, V9X_G4_EU_EXEC_16, V9X_G4_EU_QTR_NONE,
                    v9x_g4_grf(V9X_G4_WM_TEX, 0u, V9X_G4_EU_TYPE_UW),
                    v9x_g4_null(V9X_G4_EU_TYPE_UD), 1u, V9X_G4_SFID_SAMPLER,
                    5u, 8u, v9x_g4_sampler_desc(1u, 0u), 0u);
    }

    /* The stage's combine into the colour. */
    for (c = 0u; c < 4u; ++c) {
        struct v9x_g4_reg out = v9x_g4_comp(V9X_G4_WM_COLOR, c);
        struct v9x_g4_reg d = v9x_g4_comp(V9X_G4_WM_DIFF, c);
        struct v9x_g4_reg t = v9x_g4_comp(V9X_G4_WM_TEX, c);

        switch (desc->stage0) {
        case V9X_G4_PS_TEXTURE:
            v9x_g4_alu16(program, V9X_G4_EU_OP_MOV, out, t,
                         v9x_g4_null(V9X_G4_EU_TYPE_F));
            break;
        case V9X_G4_PS_MODULATE:
            v9x_g4_alu16(program, V9X_G4_EU_OP_MUL, out, d, t);
            break;
        case V9X_G4_PS_MODULATE_TEXALPHA:
            if (c == 3u) {
                v9x_g4_alu16(program, V9X_G4_EU_OP_MOV, out, t,
                             v9x_g4_null(V9X_G4_EU_TYPE_F));
            } else {
                v9x_g4_alu16(program, V9X_G4_EU_OP_MUL, out, d, t);
            }
            break;
        case V9X_G4_PS_ADD:
            if (c == 3u) {
                v9x_g4_alu16(program, V9X_G4_EU_OP_MOV, out, d,
                             v9x_g4_null(V9X_G4_EU_TYPE_F));
            } else {
                v9x_g4_alu16(program, V9X_G4_EU_OP_ADD, out, d, t);
            }
            break;
        default:
            v9x_g4_alu16(program, V9X_G4_EU_OP_MOV, out, d,
                         v9x_g4_null(V9X_G4_EU_TYPE_F));
            break;
        }
    }

    /* Specular adds to rgb; its alpha is the vertex fog factor. */
    if (desc->specular != 0u || desc->fog != 0u) {
        for (c = 0u; c < 4u; ++c) {
            v9x_g4_interp(program, v9x_g4_comp(V9X_G4_WM_SPEC, c),
                          V9X_G4_ATTR_SPECULAR, c);
        }
    }
    if (desc->specular != 0u) {
        for (c = 0u; c < 3u; ++c) {
            v9x_g4_alu16(program, V9X_G4_EU_OP_ADD,
                         v9x_g4_comp(V9X_G4_WM_COLOR, c),
                         v9x_g4_comp(V9X_G4_WM_COLOR, c),
                         v9x_g4_comp(V9X_G4_WM_SPEC, c));
        }
    }
    /* Fog: colour = fog + f * (colour - fog), f the specular alpha. */
    if (desc->fog != 0u) {
        for (c = 0u; c < 3u; ++c) {
            v9x_g4_alu16(program, V9X_G4_EU_OP_ADD, v9x_g4_f(V9X_G4_WM_T0),
                         v9x_g4_comp(V9X_G4_WM_COLOR, c),
                         v9x_g4_imm_f(-desc->fog_rgb[c]));
            v9x_g4_alu16(program, V9X_G4_EU_OP_MUL, v9x_g4_f(V9X_G4_WM_T0),
                         v9x_g4_f(V9X_G4_WM_T0),
                         v9x_g4_comp(V9X_G4_WM_SPEC, 3u));
            v9x_g4_alu16(program, V9X_G4_EU_OP_ADD,
                         v9x_g4_comp(V9X_G4_WM_COLOR, c),
                         v9x_g4_f(V9X_G4_WM_T0),
                         v9x_g4_imm_f(desc->fog_rgb[c]));
        }
    }

    /*
     * The render target write, exa_wm_write.g4a's: the first eight pixels'
     * r g b a in m2-m5, the second eight's in m6-m9, g1 in m1, g0 to m0 by
     * the implied move.
     */
    for (c = 0u; c < 4u; ++c) {
        v9x_g4_alu(program, V9X_G4_EU_OP_MOV, V9X_G4_EU_EXEC_8,
                   V9X_G4_EU_QTR_NONE, 0u,
                   v9x_g4_mrf((v9x_u8)(2u + c), V9X_G4_EU_TYPE_F),
                   v9x_g4_comp(V9X_G4_WM_COLOR, c),
                   v9x_g4_null(V9X_G4_EU_TYPE_F));
    }
    for (c = 0u; c < 4u; ++c) {
        v9x_g4_alu(program, V9X_G4_EU_OP_MOV, V9X_G4_EU_EXEC_8,
                   V9X_G4_EU_QTR_SECHALF, 0u,
                   v9x_g4_mrf((v9x_u8)(6u + c), V9X_G4_EU_TYPE_F),
                   v9x_g4_f((v9x_u8)(V9X_G4_WM_COLOR + c * 2u + 1u)),
                   v9x_g4_null(V9X_G4_EU_TYPE_F));
    }
    v9x_g4_alu(program, V9X_G4_EU_OP_MOV, V9X_G4_EU_EXEC_8,
               V9X_G4_EU_QTR_NONE, 1u, v9x_g4_mrf(1u, V9X_G4_EU_TYPE_UD),
               v9x_g4_grf(1u, 0u, V9X_G4_EU_TYPE_UD),
               v9x_g4_null(V9X_G4_EU_TYPE_UD));
    v9x_g4_send(program, V9X_G4_EU_EXEC_16, V9X_G4_EU_QTR_NONE,
                v9x_g4_null(V9X_G4_EU_TYPE_UW),
                v9x_g4_grf(0u, 0u, V9X_G4_EU_TYPE_UW), 0u,
                V9X_G4_SFID_DP_WRITE, 10u, 0u, v9x_g4_rt_write_desc(0u), 1u);
    v9x_g4_tail(program);
    return program->overflow ? V9X_STATUS_INSUFFICIENT_MEMORY : V9X_STATUS_OK;
}
