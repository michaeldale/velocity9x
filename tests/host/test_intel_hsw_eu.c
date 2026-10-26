#include <stdio.h>

#include "velocity9x/intel_hsw_eu.h"

static unsigned int failures = 0u;

#define CHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++failures; \
    } \
} while (0)

/*
 * Expected dwords below are worked by hand from HSW PRM Vol 7's field
 * positions, so these tests pin the encoder to the tables as read. They
 * cannot show the reading is right; a kernel running on the 4570 does.
 */

static void v9x_clear(struct v9x_hsw_eu_inst *inst)
{
    unsigned char *bytes = (unsigned char *)inst;
    unsigned int index;

    for (index = 0u; index < sizeof(*inst); ++index) {
        bytes[index] = 0u;
    }
}

static void v9x_grf(struct v9x_hsw_eu_operand *op, v9x_u8 nr, v9x_u8 type)
{
    op->file = V9X_HSW_EU_FILE_GRF;
    op->type = type;
    op->nr = nr;
    op->subnr = 0u;
    op->vstride = V9X_HSW_EU_VSTRIDE_8;
    op->width = V9X_HSW_EU_WIDTH_8;
    op->hstride = V9X_HSW_EU_HSTRIDE_1;
}

/* mov(8) g10<1>:F g2<8,8,1>:F */
static void test_mov(void)
{
    struct v9x_hsw_eu_inst inst;
    struct v9x_hsw_eu_inst back;
    v9x_u32 out[4];

    v9x_clear(&inst);
    inst.opcode = V9X_HSW_EU_OP_MOV;
    inst.exec_size = V9X_HSW_EU_EXEC_8;
    v9x_grf(&inst.dst, 10u, V9X_HSW_EU_TYPE_F);
    v9x_grf(&inst.src0, 2u, V9X_HSW_EU_TYPE_F);
    CHECK(v9x_intel_hsw_eu_encode(&inst, out) == V9X_STATUS_OK);
    CHECK(out[0] == 0x00600001ul);
    /* Dst hstride 1 at 30:29, reg 10 at 28:21; src1 null ARF typed F
     * ("Non-present Operands"); src0 F/GRF; dst F/GRF. */
    CHECK(out[1] == 0x214073bdul);
    CHECK(out[2] == 0x008d0040ul);
    CHECK(out[3] == 0ul);

    CHECK(v9x_intel_hsw_eu_decode(out, &back) == V9X_STATUS_OK);
    CHECK(back.opcode == V9X_HSW_EU_OP_MOV && back.sources == 1u);
    CHECK(back.dst.nr == 10u && back.dst.type == V9X_HSW_EU_TYPE_F);
    CHECK(back.src0.nr == 2u && back.src0.vstride == V9X_HSW_EU_VSTRIDE_8);
    CHECK(back.src0.width == V9X_HSW_EU_WIDTH_8);
    CHECK(back.src1.file == V9X_HSW_EU_FILE_ARF &&
          back.src1.type == V9X_HSW_EU_TYPE_F);
}

/* add(8) g20<1>:F g3<8,8,1>:F 1.0F, saturated */
static void test_add_immediate(void)
{
    struct v9x_hsw_eu_inst inst;
    struct v9x_hsw_eu_inst back;
    v9x_u32 out[4];

    v9x_clear(&inst);
    inst.opcode = V9X_HSW_EU_OP_ADD;
    inst.exec_size = V9X_HSW_EU_EXEC_8;
    inst.saturate = 1u;
    v9x_grf(&inst.dst, 20u, V9X_HSW_EU_TYPE_F);
    v9x_grf(&inst.src0, 3u, V9X_HSW_EU_TYPE_F);
    inst.src1.file = V9X_HSW_EU_FILE_IMM;
    inst.src1.type = V9X_HSW_EU_TYPE_F;
    inst.src1.imm = 0x3f800000ul;
    CHECK(v9x_intel_hsw_eu_encode(&inst, out) == V9X_STATUS_OK);
    CHECK(out[0] == 0x80600040ul);
    CHECK((out[1] & 0x00007c00ul) == 0x00007c00ul); /* src1 F, IMM */
    CHECK(out[3] == 0x3f800000ul);
    CHECK(v9x_intel_hsw_eu_decode(out, &back) == V9X_STATUS_OK);
    CHECK(back.saturate == 1u && back.src1.file == V9X_HSW_EU_FILE_IMM);
    CHECK(back.src1.imm == 0x3f800000ul && back.src0.nr == 3u);

    /* Only the last source may be immediate. */
    inst.src0.file = V9X_HSW_EU_FILE_IMM;
    CHECK(v9x_intel_hsw_eu_encode(&inst, out) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

/* send(8) null g112 RT write SIMD8_LO, last RT, BTI 0, mlen 5, EOT. */
static void test_send_rt_write(void)
{
    struct v9x_hsw_eu_inst inst;
    struct v9x_hsw_eu_inst back;
    v9x_u32 out[4];
    v9x_u32 function;
    v9x_u32 desc;

    CHECK(v9x_intel_hsw_eu_rt_write_fc(1u, 0u, V9X_HSW_RT_SIMD8_LO, 0u,
                                       &function) == V9X_STATUS_OK);
    CHECK(function == 0x00031400ul);
    CHECK(v9x_intel_hsw_eu_send_desc(5u, 0u, 0u, function, &desc) ==
          V9X_STATUS_OK);
    CHECK(desc == 0x0a031400ul);

    v9x_clear(&inst);
    inst.opcode = V9X_HSW_EU_OP_SEND;
    inst.exec_size = V9X_HSW_EU_EXEC_8;
    inst.sfid = V9X_HSW_SFID_DP_RC;
    inst.eot = 1u;
    inst.desc = desc;
    inst.dst.file = V9X_HSW_EU_FILE_ARF; /* null */
    inst.dst.type = V9X_HSW_EU_TYPE_UD;
    inst.dst.hstride = V9X_HSW_EU_HSTRIDE_1;
    v9x_grf(&inst.src0, 112u, V9X_HSW_EU_TYPE_UD);
    CHECK(v9x_intel_hsw_eu_encode(&inst, out) == V9X_STATUS_OK);
    CHECK(out[0] == 0x05600031ul);
    CHECK(out[3] == 0x8a031400ul);
    CHECK(v9x_intel_hsw_eu_decode(out, &back) == V9X_STATUS_OK);
    CHECK(back.sfid == V9X_HSW_SFID_DP_RC && back.eot == 1u);
    CHECK(back.desc == desc && back.src0.nr == 112u);

    /* A 32-bit descriptor would overwrite EOT. */
    inst.desc = 0x80000000ul;
    CHECK(v9x_intel_hsw_eu_encode(&inst, out) ==
          V9X_STATUS_INVALID_ARGUMENT);
    /* The render target write refuses the stateless binding table index. */
    CHECK(v9x_intel_hsw_eu_rt_write_fc(1u, 0u, V9X_HSW_RT_SIMD8_LO, 255u,
                                       &function) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_hsw_eu_send_desc(0u, 0u, 0u, 0ul, &desc) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_hsw_eu_send_desc(1u, 17u, 0u, 0ul, &desc) ==
          V9X_STATUS_INVALID_ARGUMENT);

    /* Sampler: SIMD8 "sample" (type 0), sampler 1, BTI 2. */
    CHECK(v9x_intel_hsw_eu_sampler_fc(V9X_HSW_SAMPLER_SIMD8, 0u, 1u, 2u,
                                      &function) == V9X_STATUS_OK);
    CHECK(function == 0x00020102ul);
}

/* mad(8) g30.xyzw:F g4:F g5:F g6:F, Align16 3-source. */
static void test_mad(void)
{
    struct v9x_hsw_eu_inst inst;
    struct v9x_hsw_eu_inst back;
    v9x_u32 out[4];

    v9x_clear(&inst);
    inst.opcode = V9X_HSW_EU_OP_MAD;
    inst.exec_size = V9X_HSW_EU_EXEC_8;
    inst.align16 = 1u;
    inst.dst.file = V9X_HSW_EU_FILE_GRF;
    inst.dst.nr = 30u;
    inst.dst.chan = 0xfu;
    inst.dst.type = V9X_HSW_EU_TYPE3_F;
    inst.src0.file = V9X_HSW_EU_FILE_GRF;
    inst.src0.nr = 4u;
    inst.src0.chan = V9X_HSW_EU_SWIZZLE_XYZW;
    inst.src0.type = V9X_HSW_EU_TYPE3_F;
    inst.src1.file = V9X_HSW_EU_FILE_GRF;
    inst.src1.nr = 5u;
    inst.src1.subnr = 16u;
    inst.src1.chan = V9X_HSW_EU_SWIZZLE_XYZW;
    inst.src2.file = V9X_HSW_EU_FILE_GRF;
    inst.src2.nr = 6u;
    inst.src2.chan = V9X_HSW_EU_SWIZZLE_XYZW;
    inst.src2.mod = V9X_HSW_EU_MOD_NEG;
    CHECK(v9x_intel_hsw_eu_encode(&inst, out) == V9X_STATUS_OK);
    CHECK(out[0] == 0x0060015bul);
    /* dst reg 30 at 31:24, ChanEn xyzw at 20:17, src2 negate at 9:8. */
    CHECK(out[1] == 0x1e1e0200ul);
    /* src1 subnr 16 bytes = 4 dwords = 100b: low bits 00 in DW2 31:30,
     * high bit in DW3 0. */
    CHECK((out[2] >> 30) == 0ul && (out[3] & 1ul) == 1ul);
    CHECK(((out[2] >> 12) & 0xfful) == 4ul);
    CHECK(((out[3] >> 22) & 0xfful) == 6ul);
    CHECK(((out[3] >> 1) & 0xfful) == 5ul);
    CHECK(v9x_intel_hsw_eu_decode(out, &back) == V9X_STATUS_OK);
    CHECK(back.sources == 3u && back.dst.nr == 30u && back.dst.chan == 0xfu);
    CHECK(back.src1.subnr == 16u && back.src1.nr == 5u);
    CHECK(back.src2.mod == V9X_HSW_EU_MOD_NEG && back.src2.nr == 6u);
    CHECK(back.src0.chan == V9X_HSW_EU_SWIZZLE_XYZW);

    /* 3-source is Align16 only. */
    inst.align16 = 0u;
    CHECK(v9x_intel_hsw_eu_encode(&inst, out) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

/* pln(8) g40<1>:F g4.0<0,1,0>:F g2<8,8,1>:F, Align1. */
static void test_pln_and_refusals(void)
{
    struct v9x_hsw_eu_inst inst;
    struct v9x_hsw_eu_inst back;
    v9x_u32 out[4];

    v9x_clear(&inst);
    inst.opcode = V9X_HSW_EU_OP_PLN;
    inst.exec_size = V9X_HSW_EU_EXEC_8;
    v9x_grf(&inst.dst, 40u, V9X_HSW_EU_TYPE_F);
    v9x_grf(&inst.src0, 4u, V9X_HSW_EU_TYPE_F);
    inst.src0.vstride = V9X_HSW_EU_VSTRIDE_0;
    inst.src0.width = V9X_HSW_EU_WIDTH_1;
    inst.src0.hstride = V9X_HSW_EU_HSTRIDE_0;
    v9x_grf(&inst.src1, 2u, V9X_HSW_EU_TYPE_F);
    CHECK(v9x_intel_hsw_eu_encode(&inst, out) == V9X_STATUS_OK);
    CHECK(out[0] == 0x0060005aul);
    CHECK(out[2] == 0x00000080ul);
    CHECK(out[3] == 0x008d0040ul);
    CHECK(v9x_intel_hsw_eu_decode(out, &back) == V9X_STATUS_OK);
    CHECK(back.src0.vstride == 0u && back.src0.width == 0u);
    CHECK(back.src1.nr == 2u && back.src1.width == V9X_HSW_EU_WIDTH_8);

    inst.dst.nr = 128u;
    CHECK(v9x_intel_hsw_eu_encode(&inst, out) ==
          V9X_STATUS_INVALID_ARGUMENT);
    inst.dst.nr = 40u;
    inst.dst.file = 2u; /* reserved register file */
    CHECK(v9x_intel_hsw_eu_encode(&inst, out) ==
          V9X_STATUS_INVALID_ARGUMENT);
    inst.dst.file = V9X_HSW_EU_FILE_GRF;
    inst.align16 = 1u;
    inst.dst.subnr = 4u; /* Align16 addresses 16-byte halves only */
    CHECK(v9x_intel_hsw_eu_encode(&inst, out) ==
          V9X_STATUS_INVALID_ARGUMENT);
    inst.align16 = 0u;
    inst.dst.subnr = 0u;
    inst.opcode = 0x60u; /* reserved opcode */
    CHECK(v9x_intel_hsw_eu_encode(&inst, out) == V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_intel_hsw_eu_encode(0, out) == V9X_STATUS_INVALID_ARGUMENT);
}

/* Phase D kernels: four movs or four plns into g112-g115, then the RT
 * write with EOT. Checked by decoding each instruction back. */
static void test_phase_d_kernels(void)
{
    static const v9x_u32 rgba[4] = {
        0x3f800000ul, 0x3e4ccccdul, 0x3f19999aul, 0x3f800000ul
    };
    struct v9x_hsw_eu_inst back;
    v9x_u32 out[20];
    v9x_u32 used;
    v9x_u32 c;

    CHECK(v9x_intel_hsw_ps_kernel_constant(out, 20ul, rgba, &used) ==
          V9X_STATUS_OK && used == 20ul);
    for (c = 0ul; c < 4ul; ++c) {
        CHECK(v9x_intel_hsw_eu_decode(out + 4ul * c, &back) ==
              V9X_STATUS_OK);
        CHECK(back.opcode == V9X_HSW_EU_OP_MOV &&
              back.exec_size == V9X_HSW_EU_EXEC_8 &&
              back.dst.nr == 112ul + c &&
              back.dst.type == V9X_HSW_EU_TYPE_F &&
              back.src0.file == V9X_HSW_EU_FILE_IMM &&
              back.src0.imm == rgba[c]);
    }
    CHECK(out[3] == 0x3f800000ul && out[7] == 0x3e4ccccdul);
    /* The write: SFID 5, src0 g112, mlen 4, rlen 0, no header, SIMD8
     * single source low (100b), last RT, BTI 0, EOT. */
    CHECK(out[16] == 0x05600031ul);
    CHECK(out[19] == 0x88031400ul);
    CHECK(v9x_intel_hsw_eu_decode(out + 16, &back) == V9X_STATUS_OK &&
          back.src0.nr == 112u && back.eot == 1u &&
          back.dst.file == V9X_HSW_EU_FILE_ARF && back.dst.nr == 0u);
    CHECK(v9x_intel_hsw_ps_kernel_constant(out, 16ul, rgba, &used) ==
          V9X_STATUS_INVALID_ARGUMENT);

    CHECK(v9x_intel_hsw_ps_kernel_interpolated(out, 20ul, 4u, &used) ==
          V9X_STATUS_OK && used == 20ul);
    for (c = 0ul; c < 4ul; ++c) {
        CHECK(v9x_intel_hsw_eu_decode(out + 4ul * c, &back) ==
              V9X_STATUS_OK);
        CHECK(back.opcode == V9X_HSW_EU_OP_PLN &&
              back.dst.nr == 112ul + c &&
              back.src0.nr == 4ul + c / 2ul &&
              back.src0.subnr == (c % 2ul) * 16ul &&
              back.src0.vstride == V9X_HSW_EU_VSTRIDE_0 &&
              back.src0.width == V9X_HSW_EU_WIDTH_1 &&
              back.src1.nr == 2u && back.src1.width == V9X_HSW_EU_WIDTH_8);
    }
    /* pln g112 g4.0<0;1,0> g2<8;8,1>: DW2 is src0 at register 4, byte 0,
     * region <0;1,0>; DW3 src1 at register 2, <8;8,1>. */
    CHECK(out[0] == 0x0060005aul && out[2] == 0x00000080ul &&
          out[3] == 0x008d0040ul);
    CHECK(out[6] == 0x00000090ul);
    CHECK(out[19] == 0x88031400ul);
    CHECK(v9x_intel_hsw_ps_kernel_interpolated(out, 20ul, 3u, &used) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

/* Phase E generator: instruction counts per mode, the sample send, and
 * the fog's negated immediate. */
static void test_phase_e_generator(void)
{
    struct v9x_hsw_ps_desc desc;
    struct v9x_hsw_eu_inst back;
    v9x_u32 out[256];
    v9x_u32 used;

    desc.stage0 = V9X_HSW_PS_MODULATE;
    desc.stage1 = V9X_HSW_PS_STAGE1_NONE;
    desc.fog = 0u;
    desc.specular = 0u;
    desc.fog_rgb[0] = desc.fog_rgb[1] = desc.fog_rgb[2] = 0ul;
    /* 4 pln diffuse, 2 pln uv, sample, 4 mul, write. */
    CHECK(v9x_intel_hsw_ps_kernel_build(out, 256ul, &desc, &used) ==
          V9X_STATUS_OK && used == 48ul);
    /* The sample: SFID 2, dst g32, src g28, mlen 2, rlen 4, SIMD8 (1 at
     * 18:17), type 0, sampler 0, BTI 1, no EOT. */
    CHECK(out[24] == 0x02600031ul && out[27] == 0x04420001ul);
    CHECK(v9x_intel_hsw_eu_decode(out + 24, &back) == V9X_STATUS_OK &&
          back.dst.nr == 32u && back.src0.nr == 28u && back.eot == 0u);
    /* The uv planes: attribute 2, registers g8 bytes 0 and 16. */
    CHECK(v9x_intel_hsw_eu_decode(out + 20, &back) == V9X_STATUS_OK &&
          back.opcode == V9X_HSW_EU_OP_PLN && back.dst.nr == 29u &&
          back.src0.nr == 8u && back.src0.subnr == 16u);
    CHECK(v9x_intel_hsw_eu_decode(out + 28, &back) == V9X_STATUS_OK &&
          back.opcode == V9X_HSW_EU_OP_MUL && back.saturate == 1u &&
          back.dst.nr == 112u && back.src0.nr == 32u && back.src1.nr == 20u);
    CHECK(out[47] == 0x88031400ul);

    desc.stage0 = V9X_HSW_PS_TEXTURE;
    CHECK(v9x_intel_hsw_ps_kernel_build(out, 256ul, &desc, &used) ==
          V9X_STATUS_OK && used == 32ul);

    desc.stage0 = V9X_HSW_PS_DIFFUSE;
    desc.fog = 1u;
    desc.specular = 0u;
    desc.fog_rgb[0] = 0x3f000000ul; /* 0.5 */
    /* 4 pln, fog pln, 4 mov, 3 x (add, mul, add), write. */
    CHECK(v9x_intel_hsw_ps_kernel_build(out, 256ul, &desc, &used) ==
          V9X_STATUS_OK && used == 76ul);
    CHECK(v9x_intel_hsw_eu_decode(out + 16, &back) == V9X_STATUS_OK &&
          back.dst.nr == 27u && back.src0.nr == 7u &&
          back.src0.subnr == 16u);
    CHECK(out[39] == 0xbf000000ul);
    CHECK(v9x_intel_hsw_eu_decode(out + 44, &back) == V9X_STATUS_OK &&
          back.opcode == V9X_HSW_EU_OP_ADD && back.saturate == 1u &&
          back.src1.file == V9X_HSW_EU_FILE_IMM &&
          back.src1.imm == 0x3f000000ul);

    desc.stage0 = V9X_HSW_PS_MODULATE;
    desc.stage1 = V9X_HSW_PS_MODULATE_TEX1;
    desc.fog = 0u;
    desc.specular = 0u;
    /* 4 pln, 2 pln, sample, 2 pln, sample, 4 mul, 4 mul, write. */
    CHECK(v9x_intel_hsw_ps_kernel_build(out, 256ul, &desc, &used) ==
          V9X_STATUS_OK && used == 76ul);
    /* Texture 1: BTI 2 and, since render interface step 4, its own
     * sampler, 1 at 11:8 - unit 1's filter and addressing are not unit
     * 0's. */
    CHECK(out[39] == 0x04420102ul);
    CHECK(v9x_intel_hsw_ps_kernel_build(out, 40ul, &desc, &used) ==
          V9X_STATUS_INVALID_ARGUMENT);
    desc.stage0 = (v9x_u8)(V9X_HSW_PS_STAGE0_LAST + 1u);
    CHECK(v9x_intel_hsw_ps_kernel_build(out, 256ul, &desc, &used) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

/* Phase F2: modulate with the texture's alpha, and the specular add. */
static void test_phase_f2_generator(void)
{
    struct v9x_hsw_ps_desc desc;
    struct v9x_hsw_eu_inst back;
    v9x_u32 out[256];
    v9x_u32 used;

    desc.stage0 = V9X_HSW_PS_MODULATE_TEXALPHA;
    desc.stage1 = V9X_HSW_PS_STAGE1_NONE;
    desc.fog = 0u;
    desc.fog_rgb[0] = desc.fog_rgb[1] = desc.fog_rgb[2] = 0ul;
    desc.specular = 1u;
    /* 4 pln diffuse, 3 pln specular, 2 pln uv, sample, 3 mul + mov,
     * 3 add, write: 18 instructions. */
    CHECK(v9x_intel_hsw_ps_kernel_build(out, 256ul, &desc, &used) ==
          V9X_STATUS_OK && used == 72ul);
    /* Specular R from attribute 1, component 0: g6 byte 0, into g24. */
    CHECK(v9x_intel_hsw_eu_decode(out + 16, &back) == V9X_STATUS_OK &&
          back.opcode == V9X_HSW_EU_OP_PLN && back.dst.nr == 24u &&
          back.src0.nr == 6u && back.src0.subnr == 0u);
    /* Alpha: a mov from the texture's alpha, g35. */
    CHECK(v9x_intel_hsw_eu_decode(out + 52, &back) == V9X_STATUS_OK &&
          back.opcode == V9X_HSW_EU_OP_MOV && back.dst.nr == 115u &&
          back.src0.nr == 35u);
    /* Then g112 += g24, saturated. */
    CHECK(v9x_intel_hsw_eu_decode(out + 56, &back) == V9X_STATUS_OK &&
          back.opcode == V9X_HSW_EU_OP_ADD && back.saturate == 1u &&
          back.dst.nr == 112u && back.src0.nr == 112u &&
          back.src1.nr == 24u);
    desc.stage0 = (v9x_u8)(V9X_HSW_PS_STAGE0_LAST + 1u);
    CHECK(v9x_intel_hsw_ps_kernel_build(out, 256ul, &desc, &used) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

/*
 * The URB write's function control (Vol 7 p316-323): WRITE_HWORD 0 in 2:0,
 * offset 13:3, interleaved 14, complete 15.
 */
static void test_urb_write_fc(void)
{
    v9x_u32 function = 0ul;

    CHECK(v9x_intel_hsw_eu_urb_write_fc(0ul, 1u, 1u, &function) ==
          V9X_STATUS_OK && function == 0x0000c000ul);
    CHECK(v9x_intel_hsw_eu_urb_write_fc(2ul, 1u, 0u, &function) ==
          V9X_STATUS_OK && function == 0x00004010ul);
    CHECK(v9x_intel_hsw_eu_urb_write_fc(0x7fful, 0u, 0u, &function) ==
          V9X_STATUS_OK && function == 0x00003ff8ul);
    CHECK(v9x_intel_hsw_eu_urb_write_fc(0x800ul, 0u, 0u, &function) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_hsw_eu_urb_write_fc(0ul, 2u, 0u, &function) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_hsw_eu_urb_write_fc(0ul, 0u, 0u, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

/* dph(8) g20.x:F g3.xyzw:F g1.xyzw<0>:F, Align16: a two-source opcode the
 * encoder now accepts, and which decodes back. */
static void test_dph(void)
{
    struct v9x_hsw_eu_inst inst;
    struct v9x_hsw_eu_inst back;
    v9x_u32 out[4];

    v9x_clear(&inst);
    inst.opcode = V9X_HSW_EU_OP_DPH;
    inst.exec_size = V9X_HSW_EU_EXEC_8;
    inst.align16 = 1u;
    v9x_grf(&inst.dst, 20u, V9X_HSW_EU_TYPE_F);
    inst.dst.chan = V9X_HSW_EU_MASK_X;
    v9x_grf(&inst.src0, 3u, V9X_HSW_EU_TYPE_F);
    inst.src0.vstride = V9X_HSW_EU_VSTRIDE_4;
    inst.src0.chan = V9X_HSW_EU_SWIZZLE_XYZW;
    v9x_grf(&inst.src1, 1u, V9X_HSW_EU_TYPE_F);
    inst.src1.vstride = V9X_HSW_EU_VSTRIDE_0;
    inst.src1.chan = V9X_HSW_EU_SWIZZLE_XYZW;
    CHECK(v9x_intel_hsw_eu_encode(&inst, out) == V9X_STATUS_OK);
    CHECK(out[0] == 0x00600155ul);
    CHECK(v9x_intel_hsw_eu_decode(out, &back) == V9X_STATUS_OK);
    CHECK(back.opcode == V9X_HSW_EU_OP_DPH && back.sources == 2u);
    CHECK(back.dst.nr == 20u && back.dst.chan == V9X_HSW_EU_MASK_X);
    CHECK(back.src0.vstride == V9X_HSW_EU_VSTRIDE_4 &&
          back.src0.chan == V9X_HSW_EU_SWIZZLE_XYZW);
    CHECK(back.src1.nr == 1u && back.src1.vstride == V9X_HSW_EU_VSTRIDE_0);
}

/*
 * Step 3's vertex kernel, instruction by instruction against the register
 * map in intel_hsw_eu.h: header, VUE header slot, four DP4s, four copies,
 * one URB write. Twelve instructions.
 */
static void test_vs_kernel_position(void)
{
    struct v9x_hsw_eu_inst back;
    v9x_u32 out[64];
    v9x_u32 used = 0ul;
    unsigned int row;
    unsigned int index;

    CHECK(v9x_intel_hsw_vs_kernel_position(out, 64ul, &used) ==
          V9X_STATUS_OK);
    CHECK(used == 48ul);

    /* mov(8) g112<1>:UD g0<8,8,1>:UD, NoMask. */
    CHECK(v9x_intel_hsw_eu_decode(out, &back) == V9X_STATUS_OK);
    CHECK(back.opcode == V9X_HSW_EU_OP_MOV && back.align16 == 0u &&
          back.mask_disable == 1u && back.exec_size == V9X_HSW_EU_EXEC_8 &&
          back.dst.nr == 112u && back.dst.type == V9X_HSW_EU_TYPE_UD &&
          back.src0.nr == 0u);

    /* or(1) g112.5<1>:UD g0.5<0,1,0>:UD 0xff00:UD, NoMask. */
    CHECK(v9x_intel_hsw_eu_decode(out + 4, &back) == V9X_STATUS_OK);
    CHECK(back.opcode == V9X_HSW_EU_OP_OR && back.align16 == 0u &&
          back.mask_disable == 1u && back.exec_size == V9X_HSW_EU_EXEC_1 &&
          back.dst.nr == 112u && back.dst.subnr == 20u &&
          back.src0.nr == 0u && back.src0.subnr == 20u &&
          back.src0.vstride == V9X_HSW_EU_VSTRIDE_0 &&
          back.src1.file == V9X_HSW_EU_FILE_IMM && back.src1.imm == 0xff00ul);

    /* mov(8) g113<1>:F 0.0F: the VUE header slot for both vertices. */
    CHECK(v9x_intel_hsw_eu_decode(out + 8, &back) == V9X_STATUS_OK);
    CHECK(back.opcode == V9X_HSW_EU_OP_MOV && back.dst.nr == 113u &&
          back.src0.file == V9X_HSW_EU_FILE_IMM && back.src0.imm == 0ul);

    /* dp4(8) g114.<r>:F g3.xyzw<4>:F g(1 + r/2).((r%2)*16).xyzw<0>:F. */
    for (row = 0u; row < 4u; ++row) {
        CHECK(v9x_intel_hsw_eu_decode(out + 12 + 4 * row, &back) ==
              V9X_STATUS_OK);
        CHECK(back.opcode == V9X_HSW_EU_OP_DP4 && back.align16 == 1u &&
              back.exec_size == V9X_HSW_EU_EXEC_8);
        CHECK(back.dst.nr == 114u && back.dst.chan == (v9x_u8)(1u << row));
        CHECK(back.src0.nr == 3u && back.src0.vstride == V9X_HSW_EU_VSTRIDE_4 &&
              back.src0.chan == V9X_HSW_EU_SWIZZLE_XYZW);
        CHECK(back.src1.nr == (v9x_u8)(1u + row / 2u) &&
              back.src1.subnr == (v9x_u8)((row % 2u) * 16u) &&
              back.src1.vstride == V9X_HSW_EU_VSTRIDE_0 &&
              back.src1.chan == V9X_HSW_EU_SWIZZLE_XYZW);
    }

    /* mov(8) g(115 + i) g(4 + i): the four attributes, whole registers. */
    for (index = 0u; index < 4u; ++index) {
        CHECK(v9x_intel_hsw_eu_decode(out + 28 + 4 * index, &back) ==
              V9X_STATUS_OK);
        CHECK(back.opcode == V9X_HSW_EU_OP_MOV && back.align16 == 0u &&
              back.dst.nr == (v9x_u8)(115u + index) &&
              back.src0.nr == (v9x_u8)(4u + index) &&
              back.src0.vstride == V9X_HSW_EU_VSTRIDE_8);
    }

    /* send(8) null g112 URB write: mlen 7, header, complete, interleaved,
     * offset 0, EOT. */
    CHECK(v9x_intel_hsw_eu_decode(out + 44, &back) == V9X_STATUS_OK);
    CHECK(back.opcode == V9X_HSW_EU_OP_SEND && back.sfid == V9X_HSW_SFID_URB &&
          back.eot == 1u && back.src0.nr == 112u &&
          back.dst.file == V9X_HSW_EU_FILE_ARF);
    CHECK(back.desc == 0x0e08c000ul);

    /* Too little room is refused, not truncated. */
    CHECK(v9x_intel_hsw_vs_kernel_position(out, 47ul, &used) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_hsw_vs_kernel_position(0, 64ul, &used) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

unsigned int v9x_run_intel_hsw_eu_tests(void)
{
    failures = 0u;
    test_urb_write_fc();
    test_dph();
    test_vs_kernel_position();
    test_phase_f2_generator();
    test_phase_e_generator();
    test_phase_d_kernels();
    test_mov();
    test_add_immediate();
    test_send_rt_write();
    test_mad();
    test_pln_and_refusals();
    return failures;
}
