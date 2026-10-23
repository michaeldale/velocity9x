#include <stdio.h>
#include <string.h>

#include "velocity9x/intel_g4_3d.h"
#include "velocity9x/intel_g4_eu.h"
#include "velocity9x/intel_hsw_3d.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

static int same_inst(const struct v9x_g4_inst *inst, v9x_u32 a, v9x_u32 b,
                     v9x_u32 c, v9x_u32 d)
{
    if (inst->dw[0] == a && inst->dw[1] == b && inst->dw[2] == c &&
        inst->dw[3] == d) {
        return 1;
    }
    printf("  got %08lx %08lx %08lx %08lx\n  want %08lx %08lx %08lx %08lx\n",
           (unsigned long)inst->dw[0], (unsigned long)inst->dw[1],
           (unsigned long)inst->dw[2], (unsigned long)inst->dw[3],
           (unsigned long)a, (unsigned long)b, (unsigned long)c,
           (unsigned long)d);
    return 0;
}

/*
 * The encoder against xf86-video-intel 2.99.917's Gen4 kernels, which ran
 * on 965 hardware: each instruction below is built with this encoder from
 * the assembly source line beside it and compared with the .g4b word for
 * word.
 */
static void test_encoder_against_xf86(void)
{
    struct v9x_g4_inst storage[4];
    struct v9x_g4_program p;
    struct v9x_g4_reg acc0;

    /* exa_wm_xy: add (16) g30<1>UW g1.8<2,4,0>UW 0x10101010V */
    v9x_g4_program_init(&p, storage, 4ul);
    v9x_g4_alu(&p, V9X_G4_EU_OP_ADD, V9X_G4_EU_EXEC_16, V9X_G4_EU_QTR_NONE,
               0u, v9x_g4_grf(30u, 0u, V9X_G4_EU_TYPE_UW),
               v9x_g4_region(v9x_g4_grf(1u, 8u, V9X_G4_EU_TYPE_UW),
                             V9X_G4_EU_VSTRIDE_2, V9X_G4_EU_WIDTH_4,
                             V9X_G4_EU_HSTRIDE_0),
               v9x_g4_imm_v(0x10101010ul));
    CHECK(same_inst(&storage[0], 0x00800040ul, 0x23c06d29ul, 0x00480028ul,
                    0x10101010ul));

    /* exa_wm_xy: add (16) g8<1>F g30<8,8,1>UW -g1.0<0,1,0>F { compr } */
    v9x_g4_program_init(&p, storage, 4ul);
    v9x_g4_alu(&p, V9X_G4_EU_OP_ADD, V9X_G4_EU_EXEC_16, V9X_G4_EU_QTR_COMPR,
               0u, v9x_g4_grf(8u, 0u, V9X_G4_EU_TYPE_F),
               v9x_g4_grf(30u, 0u, V9X_G4_EU_TYPE_UW),
               v9x_g4_neg(v9x_g4_scalar(1u, 0u, V9X_G4_EU_TYPE_F)));
    CHECK(same_inst(&storage[0], 0x00802040ul, 0x2100753dul, 0x008d03c0ul,
                    0x00004020ul));

    /* exa_wm_src_affine: mul (16) g30<1>F g8<8,8,1>F g3.0<0,1,0>F */
    v9x_g4_program_init(&p, storage, 4ul);
    v9x_g4_alu(&p, V9X_G4_EU_OP_MUL, V9X_G4_EU_EXEC_16, V9X_G4_EU_QTR_COMPR,
               0u, v9x_g4_grf(30u, 0u, V9X_G4_EU_TYPE_F),
               v9x_g4_grf(8u, 0u, V9X_G4_EU_TYPE_F),
               v9x_g4_scalar(3u, 0u, V9X_G4_EU_TYPE_F));
    CHECK(same_inst(&storage[0], 0x00802041ul, 0x23c077bdul, 0x008d0100ul,
                    0x00000060ul));

    /* exa_sf: send (4) 0 g6<1>F g1.12<4,4,1>F math inv mlen 1 rlen 1 */
    v9x_g4_program_init(&p, storage, 4ul);
    v9x_g4_send(&p, V9X_G4_EU_EXEC_4, V9X_G4_EU_QTR_NONE,
                v9x_g4_grf(6u, 0u, V9X_G4_EU_TYPE_F),
                v9x_g4_region(v9x_g4_grf(1u, 12u, V9X_G4_EU_TYPE_F),
                              V9X_G4_EU_VSTRIDE_4, V9X_G4_EU_WIDTH_4,
                              V9X_G4_EU_HSTRIDE_1),
                0u, V9X_G4_SFID_MATH, 1u, 1u,
                v9x_g4_math_desc(V9X_G4_MATH_INV), 0u);
    CHECK(same_inst(&storage[0], 0x00400031ul, 0x20c01fbdul, 0x0069002cul,
                    0x01110001ul));

    /* exa_sf: send (8) 0 null g0<8,8,1>F urb 0 transpose used complete
     * mlen 4 rlen 0 { EOT } */
    v9x_g4_program_init(&p, storage, 4ul);
    v9x_g4_send(&p, V9X_G4_EU_EXEC_8, V9X_G4_EU_QTR_NONE,
                v9x_g4_null(V9X_G4_EU_TYPE_F),
                v9x_g4_grf(0u, 0u, V9X_G4_EU_TYPE_F), 0u, V9X_G4_SFID_URB,
                4u, 0u, v9x_g4_urb_write_desc(0u, 1u), 1u);
    CHECK(same_inst(&storage[0], 0x00600031ul, 0x20001fbcul, 0x008d0000ul,
                    0x8640c800ul));

    /* exa_wm_src_sample_argb: mov (1) g0.8<1>UD 0x0UD { mask_disable } */
    v9x_g4_program_init(&p, storage, 4ul);
    v9x_g4_alu(&p, V9X_G4_EU_OP_MOV, V9X_G4_EU_EXEC_1, V9X_G4_EU_QTR_NONE,
               1u, v9x_g4_grf(0u, 8u, V9X_G4_EU_TYPE_UD), v9x_g4_imm_ud(0ul),
               v9x_g4_null(V9X_G4_EU_TYPE_UD));
    CHECK(same_inst(&storage[0], 0x00000201ul, 0x20080061ul, 0x00000000ul,
                    0x00000000ul));

    /* mov (8) m1<1>UD g0<8,8,1>UD */
    v9x_g4_program_init(&p, storage, 4ul);
    v9x_g4_alu(&p, V9X_G4_EU_OP_MOV, V9X_G4_EU_EXEC_8, V9X_G4_EU_QTR_NONE,
               0u, v9x_g4_mrf(1u, V9X_G4_EU_TYPE_UD),
               v9x_g4_grf(0u, 0u, V9X_G4_EU_TYPE_UD),
               v9x_g4_null(V9X_G4_EU_TYPE_UD));
    CHECK(same_inst(&storage[0], 0x00600001ul, 0x20200022ul, 0x008d0000ul,
                    0x00000000ul));

    /* send (16) 1 g14<1>UW null sampler (1,0,F) mlen 5 rlen 8 */
    v9x_g4_program_init(&p, storage, 4ul);
    v9x_g4_send(&p, V9X_G4_EU_EXEC_16, V9X_G4_EU_QTR_NONE,
                v9x_g4_grf(14u, 0u, V9X_G4_EU_TYPE_UW),
                v9x_g4_null(V9X_G4_EU_TYPE_UD), 1u, V9X_G4_SFID_SAMPLER, 5u,
                8u, v9x_g4_sampler_desc(1u, 0u), 0u);
    CHECK(same_inst(&storage[0], 0x01800031ul, 0x21c01c09ul, 0x00000000ul,
                    0x02580001ul));

    /* exa_wm_write: mov (8) m6<1>F g15<8,8,1>F { sechalf } */
    v9x_g4_program_init(&p, storage, 4ul);
    v9x_g4_alu(&p, V9X_G4_EU_OP_MOV, V9X_G4_EU_EXEC_8, V9X_G4_EU_QTR_SECHALF,
               0u, v9x_g4_mrf(6u, V9X_G4_EU_TYPE_F),
               v9x_g4_grf(15u, 0u, V9X_G4_EU_TYPE_F),
               v9x_g4_null(V9X_G4_EU_TYPE_F));
    CHECK(same_inst(&storage[0], 0x00601001ul, 0x20c003beul, 0x008d01e0ul,
                    0x00000000ul));

    /* exa_wm_write: send (16) 0 acc0<1>UW g0<8,8,1>UW write (0, 8, 4, 0)
     * mlen 10 rlen 0 { EOT }. The kernels here write to null; the
     * descriptor and everything else are this instruction's. */
    acc0 = v9x_g4_null(V9X_G4_EU_TYPE_UW);
    acc0.nr = 0x20u;
    v9x_g4_program_init(&p, storage, 4ul);
    v9x_g4_send(&p, V9X_G4_EU_EXEC_16, V9X_G4_EU_QTR_NONE, acc0,
                v9x_g4_grf(0u, 0u, V9X_G4_EU_TYPE_UW), 0u,
                V9X_G4_SFID_DP_WRITE, 10u, 0u, v9x_g4_rt_write_desc(0u), 1u);
    CHECK(same_inst(&storage[0], 0x00800031ul, 0x24001d28ul, 0x008d0000ul,
                    0x85a04800ul));

    /* nop */
    v9x_g4_program_init(&p, storage, 4ul);
    v9x_g4_nop(&p);
    CHECK(same_inst(&storage[0], 0x0000007eul, 0ul, 0ul, 0ul));

    /* Overflow is reported, not written past. */
    v9x_g4_program_init(&p, storage, 1ul);
    v9x_g4_nop(&p);
    v9x_g4_nop(&p);
    CHECK(p.overflow == 1u && p.count == 1ul);
}

/*
 * The same sends encoded for Gen5, against xf86-video-intel 2.99.917's
 * exa_*.g4b.gen5: the Gen4 sources assembled for Ironlake, which the UXA
 * path ran there. Only the send words differ from the .g4b; the ALU words
 * are identical, which the ALU cases above already cover.
 */
static void test_encoder_against_xf86_gen5(void)
{
    struct v9x_g4_inst storage[4];
    struct v9x_g4_program p;
    struct v9x_g4_reg acc0;

    /* exa_sf.g4b.gen5: send (4) 0 g6<1>F g1.12<4,4,1>F math inv mlen 1
     * rlen 1 - no header for math. */
    v9x_g4_program_init(&p, storage, 4ul);
    p.gen = V9X_G4_GEN5;
    v9x_g4_send(&p, V9X_G4_EU_EXEC_4, V9X_G4_EU_QTR_NONE,
                v9x_g4_grf(6u, 0u, V9X_G4_EU_TYPE_F),
                v9x_g4_region(v9x_g4_grf(1u, 12u, V9X_G4_EU_TYPE_F),
                              V9X_G4_EU_VSTRIDE_4, V9X_G4_EU_WIDTH_4,
                              V9X_G4_EU_HSTRIDE_1),
                0u, V9X_G4_SFID_MATH, 1u, 1u,
                v9x_g4_math_desc(V9X_G4_MATH_INV), 0u);
    CHECK(same_inst(&storage[0], 0x00400031ul, 0x20c01fbdul, 0x1069002cul,
                    0x02100001ul));

    /* exa_wm_src_projective.g4b.gen5: send (8) 0 g12<1>F g30<8,8,1>F math
     * inv mlen 1 rlen 1. */
    v9x_g4_program_init(&p, storage, 4ul);
    p.gen = V9X_G4_GEN5;
    v9x_g4_send(&p, V9X_G4_EU_EXEC_8, V9X_G4_EU_QTR_NONE,
                v9x_g4_grf(12u, 0u, V9X_G4_EU_TYPE_F),
                v9x_g4_grf(30u, 0u, V9X_G4_EU_TYPE_F), 0u, V9X_G4_SFID_MATH,
                1u, 1u, v9x_g4_math_desc(V9X_G4_MATH_INV), 0u);
    CHECK(same_inst(&storage[0], 0x00600031ul, 0x21801fbdul, 0x108d03c0ul,
                    0x02100001ul));

    /* exa_sf.g4b.gen5: send (8) 0 null g0<8,8,1>F urb 0 transpose used
     * complete mlen 4 rlen 0 { EOT } - header present, EOT in DW2 too. */
    v9x_g4_program_init(&p, storage, 4ul);
    p.gen = V9X_G4_GEN5;
    v9x_g4_send(&p, V9X_G4_EU_EXEC_8, V9X_G4_EU_QTR_NONE,
                v9x_g4_null(V9X_G4_EU_TYPE_F),
                v9x_g4_grf(0u, 0u, V9X_G4_EU_TYPE_F), 0u, V9X_G4_SFID_URB,
                4u, 0u, v9x_g4_urb_write_desc(0u, 1u), 1u);
    CHECK(same_inst(&storage[0], 0x00600031ul, 0x20001fbcul, 0x648d0000ul,
                    0x8808c800ul));

    /* exa_wm_src_sample_argb.g4b.gen5: send (16) 1 g14<1>UW null sampler
     * (1,0,F) mlen 5 rlen 8 - header present, SIMD16 mode at 17:16. */
    v9x_g4_program_init(&p, storage, 4ul);
    p.gen = V9X_G4_GEN5;
    v9x_g4_send(&p, V9X_G4_EU_EXEC_16, V9X_G4_EU_QTR_NONE,
                v9x_g4_grf(14u, 0u, V9X_G4_EU_TYPE_UW),
                v9x_g4_null(V9X_G4_EU_TYPE_UD), 1u, V9X_G4_SFID_SAMPLER, 5u,
                8u, v9x_g4_sampler_desc(1u, 0u), 0u);
    CHECK(same_inst(&storage[0], 0x01800031ul, 0x21c01c09ul, 0x20000000ul,
                    0x0a8a0001ul));

    /* exa_wm_write.g4b.gen5: send (16) 0 acc0<1>UW g0<8,8,1>UW write
     * (0, 8, 4, 0) mlen 10 rlen 0 { EOT }. */
    acc0 = v9x_g4_null(V9X_G4_EU_TYPE_UW);
    acc0.nr = 0x20u;
    v9x_g4_program_init(&p, storage, 4ul);
    p.gen = V9X_G4_GEN5;
    v9x_g4_send(&p, V9X_G4_EU_EXEC_16, V9X_G4_EU_QTR_NONE, acc0,
                v9x_g4_grf(0u, 0u, V9X_G4_EU_TYPE_UW), 0u,
                V9X_G4_SFID_DP_WRITE, 10u, 0u, v9x_g4_rt_write_desc(0u), 1u);
    CHECK(same_inst(&storage[0], 0x00800031ul, 0x24001d28ul, 0x548d0000ul,
                    0x94084800ul));
}

/* The SF kernel: fits its 1 KiB, ends in the URB write with EOT then the
 * prefetch nops, and writes each setup register at its own offset. */
static void test_sf_kernel(void)
{
    struct v9x_g4_inst storage[64];
    struct v9x_g4_program p;
    v9x_u8 used = 0u;
    v9x_u32 index;
    v9x_u32 urb_writes = 0ul;
    v9x_u32 last_send = 0ul;

    v9x_g4_program_init(&p, storage, 64ul);
    CHECK(v9x_g4_sf_kernel(&p, V9X_G4_SETUP_REGS, &used) == V9X_STATUS_OK);
    CHECK(p.overflow == 0u);
    CHECK(p.count * 16ul <= 1024ul);
    CHECK(used <= 32u);
    for (index = 0ul; index < p.count; ++index) {
        if ((storage[index].dw[0] & 0x7ful) == 0x31ul &&
            ((storage[index].dw[3] >> 24) & 0xful) == V9X_G4_SFID_URB) {
            /* Offsets 0, 4, 8: one setup register each. */
            CHECK(((storage[index].dw[3] >> 4) & 0x3ful) == urb_writes * 4ul);
            ++urb_writes;
            last_send = index;
        }
    }
    CHECK(urb_writes == V9X_G4_SETUP_REGS);
    CHECK((storage[last_send].dw[3] & 0x80000000ul) != 0ul);
    CHECK((storage[last_send].dw[3] & 0x8000ul) != 0ul);  /* complete */
    CHECK(last_send + 9ul == p.count);
    for (index = last_send + 1ul; index < p.count; ++index) {
        CHECK(storage[index].dw[0] == 0x7eul);
    }
    /* No EOT before the last write. */
    for (index = 0ul; index < last_send; ++index) {
        if ((storage[index].dw[0] & 0x7ful) == 0x31ul) {
            CHECK((storage[index].dw[3] & 0x80000000ul) == 0ul);
        }
    }
    CHECK(v9x_g4_sf_kernel(&p, 0u, &used) == V9X_STATUS_INVALID_ARGUMENT);
}

/* Every pixel-kernel description fits its 2 KiB slot and ends in exactly
 * one render target write with EOT; textured ones sample binding table 1. */
static void test_wm_kernels(void)
{
    struct v9x_g4_inst storage[128];
    struct v9x_g4_program p;
    struct v9x_g4_ps_desc desc;
    v9x_u8 used;
    v9x_u8 stage;
    v9x_u8 extra;
    v9x_u32 index;
    v9x_u32 eots;
    v9x_u32 samples;

    for (stage = 0u; stage <= V9X_G4_PS_DECAL; ++stage) {
        for (extra = 0u; extra < 8u; ++extra) {
            desc.stage0 = stage;
            desc.stage1 = (v9x_u8)((extra >> 2) & 1u);
            if (stage == V9X_G4_PS_DIFFUSE && desc.stage1 != 0u) {
                /* A second unit needs the first. */
                v9x_g4_program_init(&p, storage, 128ul);
                CHECK(v9x_g4_wm_kernel(&p, &desc, &used) ==
                      V9X_STATUS_INVALID_ARGUMENT);
                continue;
            }
            desc.specular = (v9x_u8)(extra & 1u);
            desc.fog = (v9x_u8)((extra >> 1) & 1u);
            desc.fog_rgb[0] = 0.25f;
            desc.fog_rgb[1] = 0.5f;
            desc.fog_rgb[2] = 0.75f;
            v9x_g4_program_init(&p, storage, 128ul);
            used = 0u;
            CHECK(v9x_g4_wm_kernel(&p, &desc, &used) == V9X_STATUS_OK);
            CHECK(p.overflow == 0u);
            CHECK(p.count * 16ul <= V9X_G4_WM_KERNEL_BYTES);
            CHECK(used > 0u && used <= 128u);
            eots = 0ul;
            samples = 0ul;
            for (index = 0ul; index < p.count; ++index) {
                if ((storage[index].dw[0] & 0x7ful) != 0x31ul) {
                    continue;
                }
                if ((storage[index].dw[3] & 0x80000000ul) != 0ul) {
                    ++eots;
                    CHECK(((storage[index].dw[3] >> 24) & 0xful) ==
                          V9X_G4_SFID_DP_WRITE);
                    CHECK(((storage[index].dw[3] >> 20) & 0xful) == 10ul);
                    CHECK(index + 9ul == p.count);
                }
                if (((storage[index].dw[3] >> 24) & 0xful) ==
                    V9X_G4_SFID_SAMPLER) {
                    ++samples;
                    /* Entry n + 1 with sampler n, in order. */
                    CHECK((storage[index].dw[3] & 0xfful) == samples);
                    CHECK(((storage[index].dw[3] >> 8) & 0xful) ==
                          samples - 1ul);
                }
            }
            CHECK(eots == 1ul);
            CHECK(samples == (stage == V9X_G4_PS_DIFFUSE
                                  ? 0ul : 1ul + desc.stage1));
        }
    }
    desc.stage1 = V9X_G4_PS_STAGE1_NONE;
    desc.stage0 = (v9x_u8)(V9X_G4_PS_DECAL + 1u);
    v9x_g4_program_init(&p, storage, 128ul);
    CHECK(v9x_g4_wm_kernel(&p, &desc, &used) == V9X_STATUS_INVALID_ARGUMENT);
}

/* The reserve layout: every area inside the 1 MiB reserve, clear of the
 * ring and status page (the first 68 KiB) and of each other. */
static int disjoint(v9x_u32 a, v9x_u32 a_bytes, v9x_u32 b, v9x_u32 b_bytes)
{
    return a + a_bytes <= b || b + b_bytes <= a;
}

static void test_layout(void)
{
    /* Read through volatile so the compiler does not fold the checks. */
    volatile v9x_u32 area = V9X_G4_AREA_OFF;
    volatile v9x_u32 sf = V9X_G4_SF_KERNEL_OFF;
    volatile v9x_u32 wm = V9X_G4_WM_KERNEL_OFF;
    volatile v9x_u32 wm_slots = V9X_G4_WM_KERNEL_SLOTS;
    volatile v9x_u32 wm_bytes = V9X_G4_WM_KERNEL_BYTES;
    volatile v9x_u32 sig = V9X_G4_SIG_OFF;
    volatile v9x_u32 state = V9X_G4_STATE_OFF;
    volatile v9x_u32 state_bytes = V9X_G4_ST_BYTES;
    volatile v9x_u32 batch = V9X_G4_BATCH_OFF;
    volatile v9x_u32 batch_bytes = V9X_G4_BATCH_BYTES;
    volatile v9x_u32 vertex = V9X_G4_VERTEX_OFF;
    volatile v9x_u32 vertex_bytes = V9X_G4_VERTEX_BYTES;
    volatile v9x_u32 vertex_stride = V9X_G4_VERTEX_STRIDE;
    volatile v9x_u32 state_stride = V9X_G4_STATE_STRIDE;
    volatile v9x_u32 batch_stride = V9X_G4_BATCH_STRIDE;
    volatile v9x_u32 slots = V9X_G4_SLOTS;
    volatile v9x_u32 pipe = V9X_G4_PIPE_OFF;
    volatile v9x_u32 st[7];
    volatile v9x_u32 urb_vs = V9X_G4_URB_VS_ENTRIES * V9X_G4_URB_VS_SIZE;
    volatile v9x_u32 urb_sf = V9X_G4_URB_SF_ENTRIES * V9X_G4_URB_SF_SIZE;
    volatile v9x_u32 vs_size = V9X_G4_URB_VS_SIZE;
    volatile v9x_u32 sf_size = V9X_G4_URB_SF_SIZE;
    volatile v9x_u32 wm2 = V9X_G4_WM_KERNEL2_OFF;
    v9x_u32 wm_end = wm + wm_slots * wm_bytes;
    v9x_u32 wm2_end = wm2 + wm_slots * wm_bytes;
    v9x_u32 index;

    st[0] = V9X_G4_ST_SF;
    st[1] = V9X_G4_ST_WM;
    st[2] = V9X_G4_ST_RT_SURF;
    st[3] = V9X_G4_ST_TEX_SURF;
    st[4] = V9X_G4_ST_SAMPLER;
    st[5] = V9X_G4_ST_BINDING;
    st[6] = V9X_G4_ST_CC;

    CHECK(area >= 0x11000ul);
    CHECK(vertex + (slots - 1ul) * vertex_stride + vertex_bytes <=
          0x100000ul);
    CHECK(vertex_bytes <= vertex_stride);
    CHECK(disjoint(sf, 0x1000ul, wm, wm_end - wm));
    CHECK(wm_end <= sig);
    /* Signatures for both sets of slots, the SF mark first. */
    CHECK(sig + 4ul * 4ul * (2ul * wm_slots + 1ul) <= pipe);
    CHECK(pipe + 4ul * (1ul + slots) <= state);
    CHECK(state_bytes <= state_stride);
    CHECK(state + (slots - 1ul) * state_stride + state_bytes <= batch);
    CHECK(batch_bytes <= batch_stride);
    CHECK(batch + (slots - 1ul) * batch_stride + batch_bytes <= wm2);
    CHECK(wm2_end <= vertex && (wm2 & 63ul) == 0ul);
    CHECK((batch_stride & 63ul) == 0ul && (state_stride & 63ul) == 0ul);
    CHECK((batch & 63ul) == 0ul);
    CHECK((sf & 63ul) == 0ul);
    CHECK((wm & 63ul) == 0ul);
    CHECK((st[6] & 63ul) == 0ul);
    for (index = 0ul; index < 6ul; ++index) {
        CHECK((st[index] & 31ul) == 0ul);
    }
    /* The URB partition fits the 965's 256 rows; a VS entry holds the VUE
     * and an SF entry the transposed setup. */
    CHECK(urb_vs + urb_sf <= 256ul);
    CHECK(vs_size * 16ul >= V9X_G4_VUE_DWORDS);
    CHECK(sf_size * 16ul >= V9X_G4_SETUP_REGS * 32ul);
}

/* State blocks: a few fields by genxml's positions. */
static void test_state(void)
{
    v9x_u32 out[16];
    struct v9x_g4_cc cc;
    struct v9x_g4_sampler s;
    v9x_u32 index;

    /* Ironlake's WM_STATE is 11 dwords: KSP1-3 and their GRF counts in
     * 8-10, zero for SIMD16-only dispatch. The binding table and sampler
     * counts are written 0, as xf86 writes them there ([DevILK-A,B] MBZ). */
    for (index = 0ul; index < 16ul; ++index) {
        out[index] = 0xfffffffful;
    }
    CHECK(v9x_g4_wm_state(out, 0x21000ul, 58u, 0x30180ul, 1u, 2u,
                          V9X_G4_GEN5) == V9X_STATUS_OK);
    CHECK(out[0] == (0x21000ul | (3ul << 1)));
    CHECK(((out[1] >> 18) & 0xfful) == 0ul);
    CHECK((out[4] & ~31ul) == 0x30180ul && ((out[4] >> 2) & 7ul) == 0ul);
    CHECK(out[8] == 0ul && out[9] == 0ul && out[10] == 0ul);
    CHECK(out[11] == 0xfffffffful);

    CHECK(v9x_g4_wm_state(out, 0x21000ul, 58u, 0x30180ul, 1u, 2u,
                          V9X_G4_GEN4) == V9X_STATUS_OK);
    CHECK(out[0] == (0x21000ul | (3ul << 1)));
    CHECK(((out[1] >> 18) & 0xfful) == 2ul);
    CHECK(((out[1] >> 8) & 0x3ful) == 1ul);
    CHECK((out[3] & 0xful) == 3ul);
    CHECK(((out[3] >> 11) & 0x3ful) == V9X_G4_WM_SETUP_GRFS);
    CHECK((out[4] & ~31ul) == 0x30180ul && ((out[4] >> 2) & 7ul) == 1ul);
    CHECK((out[5] & 3ul) == 2ul);                    /* 16-pixel only */
    CHECK((out[5] & (1ul << 19)) != 0ul);
    CHECK((out[5] >> 25) == V9X_G4_WM_MAX_THREADS - 1u);
    CHECK(v9x_g4_wm_state(out, 0x21010ul, 58u, 0ul, 0u, 2u, V9X_G4_GEN4) ==
          V9X_STATUS_INVALID_ARGUMENT);

    CHECK(v9x_g4_sf_state(out, 0x20000ul, 21u, 0x30140ul) == V9X_STATUS_OK);
    CHECK(out[0] == (0x20000ul | (1ul << 1)));
    CHECK((out[3] & 0xful) == 3ul && ((out[3] >> 4) & 0x3ful) == 1ul &&
          ((out[3] >> 11) & 0x3ful) == V9X_G4_SETUP_REGS);
    CHECK(((out[4] >> 11) & 0xfful) == V9X_G4_URB_SF_ENTRIES);
    CHECK(((out[4] >> 19) & 0x1ful) == V9X_G4_URB_SF_SIZE - 1u);
    CHECK((out[5] & 2ul) == 0ul);                    /* no viewport xform */
    CHECK(((out[6] >> 29) & 3ul) == 1ul);            /* cull none */

    CHECK(v9x_g4_vs_state(out, V9X_G4_GEN4) == V9X_STATUS_OK);
    CHECK((out[6] & 1ul) == 0ul && (out[6] & 2ul) != 0ul);
    CHECK(((out[4] >> 11) & 0xfful) == V9X_G4_URB_VS_ENTRIES);
    /* Ironlake: the entry count is U9 shifted right by 2 (PRM Vol 2 Part 1
     * VS_STATE, "8=32 entries"). */
    CHECK(v9x_g4_vs_state(out, V9X_G4_GEN5) == V9X_STATUS_OK);
    CHECK(((out[4] >> 11) & 0x7ful) == V9X_G4_URB_VS_ENTRIES / 4u);
    CHECK(((out[4] >> 19) & 0x1ful) == V9X_G4_URB_VS_SIZE - 1u);
    CHECK(v9x_g4_vs_state(out, 6u) == V9X_STATUS_INVALID_ARGUMENT);

    memset(&cc, 0, sizeof(cc));
    cc.depth_test = 1u;
    cc.depth_write = 1u;
    cc.depth_function = 4u;
    cc.blend = 1u;
    cc.src_factor = 0x03u;
    cc.dst_factor = 0x13u;
    cc.alpha_test = 1u;
    cc.alpha_function = 5u;
    cc.alpha_ref = 0x80u;
    CHECK(v9x_g4_cc_state(out, &cc, 0x30100ul, 0u) == V9X_STATUS_OK);
    CHECK(out[2] == ((1ul << 15) | (4ul << 12) | (1ul << 11)));
    CHECK(out[3] == ((1ul << 12) | (1ul << 11) | (5ul << 8)));
    CHECK(out[4] == 0x30100ul);
    CHECK(((out[6] >> 24) & 0x1ful) == 0x03ul &&
          ((out[6] >> 19) & 0x1ful) == 0x13ul && (out[6] & 3ul) == 3ul);
    CHECK(out[7] == 0x80ul);

    CHECK(v9x_g4_surface(out, V9X_G4_FMT_B5G6R5_UNORM, 0x00100000ul, 640u,
                         480u, 1280ul, 1u, 0u, 1u) == V9X_STATUS_OK);
    CHECK(out[0] == ((1ul << 29) | (0x100ul << 18) | (1ul << 13) |
                     (1ul << 8)));
    CHECK(out[1] == 0x00100000ul);
    CHECK(out[2] == ((479ul << 19) | (639ul << 6)));
    CHECK(out[3] == (1279ul << 3));

    s.min_filter = 1u;
    s.mag_filter = 1u;
    s.mip_filter = 0u;
    s.address = 2u;
    s.max_lod = 0u;
    s.chroma_key = 0u;
    CHECK(v9x_g4_sampler_state(out, &s, 0x301c0ul) == V9X_STATUS_OK);
    CHECK(out[0] == ((1ul << 28) | (1ul << 17) | (1ul << 14)));
    CHECK(out[1] == ((2ul << 6) | (2ul << 3) | 2ul));
    CHECK(out[2] == 0x301c0ul);
}

/* A mip chain as Gen4 samples it: the tree d3d_g4.c places, by 965 PRM Vol
 * 1 6.17.3.2's MIPLAYOUT_BELOW equations with i = 4, j = 2 (LOD1 (0, h0),
 * LOD2 (w1, h0), LOD3 (w1, h0 + h2)); SURFACE_STATE's MIP count in DW2 5:2
 * with the layout bit (DW0 10) left BELOW; trilinear with Max LOD in U4.6. */
static void test_mips(void)
{
    struct v9x_hsw_miptree tree;
    struct v9x_g4_sampler s;
    v9x_u32 out[8];

    CHECK(v9x_intel_hsw_miptree(256u, 256u, 9u, 2u, &tree) ==
          V9X_STATUS_OK);
    CHECK(tree.pitch == 512ul);
    CHECK(tree.level_offset[1] == 256ul * 512ul);
    CHECK(tree.level_offset[2] == 256ul * 512ul + 128ul * 2ul);
    CHECK(tree.level_offset[3] == (256ul + 64ul) * 512ul + 128ul * 2ul);

    CHECK(v9x_g4_surface(out, V9X_G4_FMT_B5G6R5_UNORM, 0x00200000ul, 256u,
                         256u, tree.pitch, 0u, 0u, 9u) == V9X_STATUS_OK);
    CHECK(((out[2] >> 2) & 0xful) == 8ul);
    CHECK((out[0] & (1ul << 10)) == 0ul);
    CHECK(v9x_g4_surface(out, V9X_G4_FMT_B5G6R5_UNORM, 0x00200000ul, 256u,
                         256u, tree.pitch, 0u, 0u, 13u) ==
          V9X_STATUS_INVALID_ARGUMENT);

    s.min_filter = 1u;
    s.mag_filter = 1u;
    s.mip_filter = 3u;
    s.address = 0u;
    s.max_lod = 8u;
    s.chroma_key = 0u;
    CHECK(v9x_g4_sampler_state(out, &s, 0x301c0ul) == V9X_STATUS_OK);
    CHECK(((out[0] >> 20) & 3ul) == 3ul);
    CHECK(((out[1] >> 12) & 0x3fful) == (8ul << 6));
}

/* Colour keys as 3DSTATE_CHROMA_KEY's ARGB8888 range (965 PRM Vol 4
 * 4.7.5): channels widened by replicating high bits, alpha the full range,
 * low and high ordered per channel; and SAMPLER_STATE DW3's key bits. */
static void test_chroma(void)
{
    struct v9x_g4_sampler s;
    v9x_u32 low = 0ul;
    v9x_u32 high = 0ul;
    v9x_u32 out[4];

    /* 565 magenta and 1555 magenta: 1F widens to FF, 0 stays 0. */
    CHECK(v9x_g4_chroma_key(0xf81ful, 0xf81ful, 0xf800ul, 0x07e0ul,
                            0x001ful, &low, &high) == V9X_STATUS_OK);
    CHECK(low == 0x00ff00fful && high == 0xffff00fful);
    CHECK(v9x_g4_chroma_key(0x7c1ful, 0x7c1ful, 0x7c00ul, 0x03e0ul,
                            0x001ful, &low, &high) == V9X_STATUS_OK);
    CHECK(low == 0x00ff00fful && high == 0xffff00fful);
    /* 565: red 10000b to 84h, green 100000b to 82h, blue 00001b to 08h. */
    CHECK(v9x_g4_chroma_key(0x8401ul, 0x8401ul, 0xf800ul, 0x07e0ul,
                            0x001ful, &low, &high) == V9X_STATUS_OK);
    CHECK(low == 0x00848208ul && high == 0xff848208ul);
    /* 8888, and a range given high first. */
    CHECK(v9x_g4_chroma_key(0x00102030ul, 0x00010203ul, 0x00ff0000ul,
                            0x0000ff00ul, 0x000000fful, &low, &high) ==
          V9X_STATUS_OK);
    CHECK(low == 0x00010203ul && high == 0xff102030ul);
    /* A split or empty mask. */
    CHECK(v9x_g4_chroma_key(0ul, 0ul, 0xf00ful, 0x07e0ul, 0x001ful, &low,
                            &high) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_g4_chroma_key(0ul, 0ul, 0ul, 0x07e0ul, 0x001ful, &low,
                            &high) == V9X_STATUS_INVALID_ARGUMENT);

    s.min_filter = 0u;
    s.mag_filter = 0u;
    s.mip_filter = 0u;
    s.address = 0u;
    s.max_lod = 0u;
    s.chroma_key = 0u;
    CHECK(v9x_g4_sampler_state(out, &s, 0ul) == V9X_STATUS_OK);
    CHECK(out[3] == 0ul);
    s.chroma_key = 1u;
    CHECK(v9x_g4_sampler_state(out, &s, 0ul) == V9X_STATUS_OK);
    CHECK(out[3] == ((1ul << 25) | (1ul << 22)));
}

/* The batch: builds, passes its own check with URB_FENCE inside one line
 * wherever the stream starts, and the check refuses a damaged copy. */
static void test_batch(void)
{
    v9x_u32 words[512];
    struct v9x_g4_batch batch;
    struct v9x_g4_draw d;
    v9x_u32 index;
    v9x_u32 pre;
    v9x_u16 saw_end;

    d.state_base = 0x00670000ul;
    d.state_offset = V9X_G4_STATE_OFF;
    d.vertex_address = 0x00670000ul + V9X_G4_VERTEX_OFF;
    d.vertex_count = 3ul;
    d.width = 640u;
    d.height = 480u;
    d.depth_address = 0ul;
    d.depth_pitch = 0ul;
    d.depth_format = 0u;
    d.depth_tiled = 0u;
    d.chroma_key = 0u;
    d.chroma_low = 0ul;
    d.chroma_high = 0ul;
    d.gen = V9X_G4_GEN4;
    for (pre = 0ul; pre < 16ul; ++pre) {
        v9x_g4_batch_init(&batch, words, 512ul);
        CHECK(v9x_g4_draw_batch(&batch, &d) == V9X_STATUS_OK);
        CHECK(v9x_g4_batch_check(batch.dw, batch.count, V9X_G4_GEN4) ==
              V9X_TRUE);
        CHECK((batch.count & 1ul) == 0ul);
        saw_end = V9X_FALSE;
        for (index = 0ul; index < batch.count; ++index) {
            if (words[index] == 0x05000000ul) {
                saw_end = V9X_TRUE;
            }
        }
        CHECK(saw_end == V9X_TRUE);
        /* The fence's three dwords in one 16-dword line. */
        for (index = 0ul; index < batch.count; ++index) {
            if (words[index] == 0x60002f01ul) {
                CHECK((index & 15ul) <= 13ul);
            }
        }
        (void)pre;
    }
    /* With a chroma key: 3DSTATE_CHROMA_KEY, entry 0, the range. */
    d.chroma_key = 1u;
    d.chroma_low = 0x00ff00fful;
    d.chroma_high = 0xffff00fful;
    v9x_g4_batch_init(&batch, words, 512ul);
    CHECK(v9x_g4_draw_batch(&batch, &d) == V9X_STATUS_OK);
    CHECK(v9x_g4_batch_check(batch.dw, batch.count, V9X_G4_GEN4) == V9X_TRUE);
    saw_end = V9X_FALSE;
    for (index = 0ul; index + 3ul < batch.count; ++index) {
        if (words[index] == 0x79040002ul && words[index + 1ul] == 0ul &&
            words[index + 2ul] == 0x00ff00fful &&
            words[index + 3ul] == 0xffff00fful) {
            saw_end = V9X_TRUE;
        }
    }
    CHECK(saw_end == V9X_TRUE);
    d.chroma_key = 0u;
    /* With depth. */
    d.depth_address = 0x00200000ul;
    d.depth_pitch = 1280ul;
    d.depth_format = V9X_G4_DEPTH_D16_UNORM;
    v9x_g4_batch_init(&batch, words, 512ul);
    CHECK(v9x_g4_draw_batch(&batch, &d) == V9X_STATUS_OK);
    CHECK(v9x_g4_batch_check(batch.dw, batch.count, V9X_G4_GEN4) == V9X_TRUE);
    /* Y-tiled D24_UNORM_S8: 3DSTATE_DEPTH_BUFFER DW1 tiled (27) and the Y
     * walk (26), format 2 at 20:18, pitch less one. */
    d.depth_pitch = 2560ul;
    d.depth_format = V9X_G4_DEPTH_D24_UNORM_S8;
    d.depth_tiled = 1u;
    v9x_g4_batch_init(&batch, words, 512ul);
    CHECK(v9x_g4_draw_batch(&batch, &d) == V9X_STATUS_OK);
    CHECK(v9x_g4_batch_check(batch.dw, batch.count, V9X_G4_GEN4) == V9X_TRUE);
    saw_end = V9X_FALSE;
    for (index = 0ul; index + 2ul < batch.count; ++index) {
        if (words[index] == 0x79050003ul) {
            CHECK(words[index + 1ul] ==
                  ((1ul << 29) | (1ul << 27) | (1ul << 26) | (2ul << 18) |
                   2559ul));
            CHECK(words[index + 2ul] == 0x00200000ul);
            saw_end = V9X_TRUE;
        }
    }
    CHECK(saw_end == V9X_TRUE);
    d.depth_tiled = 0u;

    /* An unknown command, a short buffer and a missing end are refused. */
    words[2] ^= 0x00010000ul;
    CHECK(v9x_g4_batch_check(batch.dw, batch.count, V9X_G4_GEN4) == V9X_FALSE);
    words[2] ^= 0x00010000ul;
    CHECK(v9x_g4_batch_check(batch.dw, 5ul, V9X_G4_GEN4) == V9X_FALSE);
    CHECK(v9x_g4_batch_check(batch.dw, 0ul, V9X_G4_GEN4) == V9X_FALSE);

    /* Too small a buffer overflows rather than writing past it. */
    v9x_g4_batch_init(&batch, words, 8ul);
    CHECK(v9x_g4_draw_batch(&batch, &d) == V9X_STATUS_INSUFFICIENT_MEMORY);
    CHECK(v9x_g4_batch_start_header() == 0x18800180ul);
}

/* Find the first dword equal to header at or after from; count if none. */
static v9x_u32 find_header(const v9x_u32 *words, v9x_u32 count,
                           v9x_u32 header)
{
    v9x_u32 index;

    for (index = 0ul; index < count; ++index) {
        if (words[index] == header) {
            return index;
        }
    }
    return count;
}

/*
 * The Ironlake batch: the commands whose encoding moved between the 965
 * and Gen5 (Ironlake PRM Vol 1 Part 1 and Vol 2 Part 1; Mesa genxml
 * gen5.xml; xf86 i965_render.c's IS_GEN5 paths, which ran there).
 */
static void test_batch_gen5(void)
{
    v9x_u32 words[512];
    struct v9x_g4_batch batch;
    struct v9x_g4_draw d;
    v9x_u32 at;
    v9x_u32 index;

    d.state_base = 0x00670000ul;
    d.state_offset = V9X_G4_STATE_OFF;
    d.vertex_address = 0x00670000ul + V9X_G4_VERTEX_OFF;
    d.vertex_count = 6ul;
    d.width = 640u;
    d.height = 480u;
    d.depth_address = 0x00200000ul;
    d.depth_pitch = 2560ul;
    d.depth_format = V9X_G4_DEPTH_D24_UNORM_S8;
    d.depth_tiled = 1u;
    d.chroma_key = 0u;
    d.chroma_low = 0ul;
    d.chroma_high = 0ul;
    d.gen = V9X_G4_GEN5;
    v9x_g4_batch_init(&batch, words, 512ul);
    CHECK(v9x_g4_draw_batch(&batch, &d) == V9X_STATUS_OK);
    CHECK(v9x_g4_batch_check(batch.dw, batch.count, V9X_G4_GEN5) == V9X_TRUE);
    /* A Gen5 batch is not a Gen4 one. */
    CHECK(v9x_g4_batch_check(batch.dw, batch.count, V9X_G4_GEN4) ==
          V9X_FALSE);

    /* PIPELINE_SELECT, sub-type 1 (Vol 1 Part 1 3.3). */
    CHECK(find_header(words, batch.count, 0x69040000ul) < batch.count);
    CHECK(find_header(words, batch.count, 0x61040000ul) == batch.count);

    /* STATE_BASE_ADDRESS, eight dwords: general, surface, indirect and
     * instruction bases, then the three upper bounds, disabled. */
    at = find_header(words, batch.count, 0x61010006ul);
    CHECK(at + 8ul <= batch.count);
    if (at + 8ul <= batch.count) {
        CHECK(words[at + 1ul] == (0x00670000ul | 1ul));
        CHECK(words[at + 2ul] == (0x00670000ul | 1ul));
        CHECK(words[at + 3ul] == 1ul);
        CHECK(words[at + 4ul] == (0x00670000ul | 1ul));
        CHECK(words[at + 5ul] == 1ul && words[at + 6ul] == 1ul &&
              words[at + 7ul] == 1ul);
    }

    /* 3DSTATE_DEPTH_BUFFER, six dwords, the last the zero coordinate
     * offset (Vol 2 Part 1 p330). */
    at = find_header(words, batch.count, 0x79050004ul);
    CHECK(at + 6ul <= batch.count);
    if (at + 6ul <= batch.count) {
        CHECK(words[at + 1ul] == ((1ul << 29) | (1ul << 27) | (1ul << 26) |
                                  (2ul << 18) | 2559ul));
        CHECK(words[at + 2ul] == 0x00200000ul);
        CHECK(words[at + 5ul] == 0ul);
    }

    /* VERTEX_BUFFER_STATE DW3: the end address, its last byte. */
    at = find_header(words, batch.count, 0x78080003ul);
    CHECK(at + 5ul <= batch.count);
    if (at + 5ul <= batch.count) {
        CHECK(words[at + 2ul] == 0x00670000ul + V9X_G4_VERTEX_OFF);
        CHECK(words[at + 3ul] == 0x00670000ul + V9X_G4_VERTEX_OFF +
                                     6ul * V9X_G4_VB_PITCH - 1ul);
    }

    /* VERTEX_ELEMENTS: eight, the first the zero VUE header, and no
     * destination offsets - elements pack from dword 0 (Vol 2 Part 1
     * 3.5.1). */
    at = find_header(words, batch.count, 0x7809000ful);
    CHECK(at + 17ul <= batch.count);
    if (at + 17ul <= batch.count) {
        CHECK(words[at + 2ul] == 0x22220000ul);
        for (index = 0ul; index < 8ul; ++index) {
            CHECK((words[at + 1ul + index * 2ul] & (1ul << 26)) != 0ul);
            CHECK((words[at + 2ul + index * 2ul] & 0xfful) == 0ul);
        }
    }

    /* Anything but 4, 4.5 or 5 is refused. */
    d.gen = 6u;
    v9x_g4_batch_init(&batch, words, 512ul);
    CHECK(v9x_g4_draw_batch(&batch, &d) == V9X_STATUS_INVALID_ARGUMENT);
}

/*
 * The G4x batch (G45, Q45): the 965's encodings except PIPELINE_SELECT,
 * which is Gen5's (Mesa i965 and xf86 on G4x; the G45 PRM's table says
 * otherwise, docs\decisions\2026-10-23-g4x-gma4500-from-surveys.md), and
 * 3DSTATE_DEPTH_BUFFER, six dwords with the [DevCTG+] Depth Coordinate
 * Offset (G45 PRM Vol 2 8.4.4.1).
 */
static void test_batch_gen45(void)
{
    v9x_u32 words[512];
    v9x_u32 out[16];
    struct v9x_g4_batch batch;
    struct v9x_g4_draw d;
    struct v9x_g4_inst storage[2];
    struct v9x_g4_program p;
    v9x_u32 at;
    v9x_u32 index;

    d.state_base = 0x00670000ul;
    d.state_offset = V9X_G4_STATE_OFF;
    d.vertex_address = 0x00670000ul + V9X_G4_VERTEX_OFF;
    d.vertex_count = 6ul;
    d.width = 640u;
    d.height = 480u;
    d.depth_address = 0x00200000ul;
    d.depth_pitch = 2560ul;
    d.depth_format = V9X_G4_DEPTH_D24_UNORM_S8;
    d.depth_tiled = 1u;
    d.chroma_key = 0u;
    d.chroma_low = 0ul;
    d.chroma_high = 0ul;
    d.gen = V9X_G4_GEN45;
    v9x_g4_batch_init(&batch, words, 512ul);
    CHECK(v9x_g4_draw_batch(&batch, &d) == V9X_STATUS_OK);
    CHECK(v9x_g4_batch_check(batch.dw, batch.count, V9X_G4_GEN45) ==
          V9X_TRUE);
    /* Neither the 965's batch nor Ironlake's. */
    CHECK(v9x_g4_batch_check(batch.dw, batch.count, V9X_G4_GEN4) ==
          V9X_FALSE);
    CHECK(v9x_g4_batch_check(batch.dw, batch.count, V9X_G4_GEN5) ==
          V9X_FALSE);

    CHECK(find_header(words, batch.count, 0x69040000ul) < batch.count);
    CHECK(find_header(words, batch.count, 0x61040000ul) == batch.count);

    /* STATE_BASE_ADDRESS, the 965's six dwords: no Instruction Base
     * Address before Gen5 (G45 PRM Vol 1a 3.6.1). */
    CHECK(find_header(words, batch.count, 0x61010006ul) == batch.count);
    at = find_header(words, batch.count, 0x61010004ul);
    CHECK(at + 6ul <= batch.count);
    if (at + 6ul <= batch.count) {
        CHECK(words[at + 1ul] == (0x00670000ul | 1ul));
        CHECK(words[at + 2ul] == (0x00670000ul | 1ul));
        CHECK(words[at + 3ul] == 0ul && words[at + 4ul] == 1ul &&
              words[at + 5ul] == 0ul);
    }

    /* 3DSTATE_DEPTH_BUFFER, six dwords, the coordinate offset zero. */
    CHECK(find_header(words, batch.count, 0x79050003ul) == batch.count);
    at = find_header(words, batch.count, 0x79050004ul);
    CHECK(at + 6ul <= batch.count);
    if (at + 6ul <= batch.count) {
        CHECK(words[at + 1ul] == ((1ul << 29) | (1ul << 27) | (1ul << 26) |
                                  (2ul << 18) | 2559ul));
        CHECK(words[at + 2ul] == 0x00200000ul);
        CHECK(words[at + 5ul] == 0ul);
    }

    /* VERTEX_BUFFER_STATE DW3: the maximum index (Vol 2 3.4.2). */
    at = find_header(words, batch.count, 0x78080003ul);
    CHECK(at + 5ul <= batch.count);
    if (at + 5ul <= batch.count) {
        CHECK(words[at + 3ul] == 5ul);
    }

    /* VERTEX_ELEMENTS: seven, each at its own VUE dword from 4 (Vol 2
     * 3.5.2's Destination Element Offset). */
    CHECK(find_header(words, batch.count, 0x7809000ful) == batch.count);
    at = find_header(words, batch.count, 0x7809000dul);
    CHECK(at + 15ul <= batch.count);
    if (at + 15ul <= batch.count) {
        for (index = 0ul; index < 7ul; ++index) {
            CHECK((words[at + 2ul + index * 2ul] & 0xfful) ==
                  4ul * (index + 1ul));
        }
    }

    /* The unit states are the 965's. */
    CHECK(v9x_g4_vs_state(out, V9X_G4_GEN45) == V9X_STATUS_OK);
    CHECK(((out[4] >> 11) & 0xfful) == V9X_G4_URB_VS_ENTRIES);
    for (index = 0ul; index < 16ul; ++index) {
        out[index] = 0xfffffffful;
    }
    CHECK(v9x_g4_wm_state(out, 0x21000ul, 58u, 0x30180ul, 1u, 2u,
                          V9X_G4_GEN45) == V9X_STATUS_OK);
    CHECK(((out[1] >> 18) & 0xfful) == 2ul);
    CHECK((out[4] & ~31ul) == 0x30180ul && ((out[4] >> 2) & 7ul) == 1ul);
    CHECK(out[8] == 0xfffffffful);

    /* The send descriptor is the 965's (Mesa brw_inst.h's 4.5 column);
     * the sampler's message type 0 with FLOAT32 return is 15:12 = 0 in
     * the G4x layout too (G45 PRM Vol 4 4.8.1.1.2). The xf86 .g4b word. */
    v9x_g4_program_init(&p, storage, 2ul);
    p.gen = V9X_G4_GEN45;
    v9x_g4_send(&p, V9X_G4_EU_EXEC_16, V9X_G4_EU_QTR_NONE,
                v9x_g4_grf(14u, 0u, V9X_G4_EU_TYPE_UW),
                v9x_g4_null(V9X_G4_EU_TYPE_UD), 1u, V9X_G4_SFID_SAMPLER, 5u,
                8u, v9x_g4_sampler_desc(1u, 0u), 0u);
    v9x_g4_program_init(&p, storage + 1, 1ul);
    v9x_g4_send(&p, V9X_G4_EU_EXEC_16, V9X_G4_EU_QTR_NONE,
                v9x_g4_grf(14u, 0u, V9X_G4_EU_TYPE_UW),
                v9x_g4_null(V9X_G4_EU_TYPE_UD), 1u, V9X_G4_SFID_SAMPLER, 5u,
                8u, v9x_g4_sampler_desc(1u, 0u), 0u);
    CHECK(same_inst(&storage[0], storage[1].dw[0], storage[1].dw[1],
                    storage[1].dw[2], storage[1].dw[3]));
}

unsigned int v9x_run_intel_g4_tests(void)
{
    failures = 0u;
    test_encoder_against_xf86();
    test_encoder_against_xf86_gen5();
    test_sf_kernel();
    test_wm_kernels();
    test_layout();
    test_state();
    test_mips();
    test_chroma();
    test_batch();
    test_batch_gen5();
    test_batch_gen45();
    return failures;
}
