#include <stdio.h>
#include <string.h>

#include "velocity9x/intel_g4_3d.h"
#include "velocity9x/intel_g4_eu.h"

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

    for (stage = 0u; stage <= V9X_G4_PS_ADD; ++stage) {
        for (extra = 0u; extra < 4u; ++extra) {
            desc.stage0 = stage;
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
            CHECK(used > 0u && used <= 64u);
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
                    CHECK((storage[index].dw[3] & 0xfful) == 1ul);
                }
            }
            CHECK(eots == 1ul);
            CHECK(samples == (stage == V9X_G4_PS_DIFFUSE ? 0ul : 1ul));
        }
    }
    desc.stage0 = (v9x_u8)(V9X_G4_PS_ADD + 1u);
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
    v9x_u32 wm_end = wm + wm_slots * wm_bytes;
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
    CHECK(sig + 4ul * 4ul * (wm_slots + 1ul) <= pipe);
    CHECK(pipe + 4ul * (1ul + slots) <= state);
    CHECK(state_bytes <= state_stride);
    CHECK(state + (slots - 1ul) * state_stride + state_bytes <= batch);
    CHECK(batch_bytes <= batch_stride);
    CHECK(batch + (slots - 1ul) * batch_stride + batch_bytes <= vertex);
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
    v9x_u32 out[8];
    struct v9x_g4_cc cc;
    struct v9x_g4_sampler s;

    CHECK(v9x_g4_wm_state(out, 0x21000ul, 58u, 0x30180ul, 1u, 2u) ==
          V9X_STATUS_OK);
    CHECK(out[0] == (0x21000ul | (3ul << 1)));
    CHECK(((out[1] >> 18) & 0xfful) == 2ul);
    CHECK(((out[1] >> 8) & 0x3ful) == 1ul);
    CHECK((out[3] & 0xful) == 3ul);
    CHECK(((out[3] >> 11) & 0x3ful) == V9X_G4_WM_SETUP_GRFS);
    CHECK((out[4] & ~31ul) == 0x30180ul && ((out[4] >> 2) & 7ul) == 1ul);
    CHECK((out[5] & 3ul) == 2ul);                    /* 16-pixel only */
    CHECK((out[5] & (1ul << 19)) != 0ul);
    CHECK((out[5] >> 25) == V9X_G4_WM_MAX_THREADS - 1u);
    CHECK(v9x_g4_wm_state(out, 0x21010ul, 58u, 0ul, 0u, 2u) ==
          V9X_STATUS_INVALID_ARGUMENT);

    CHECK(v9x_g4_sf_state(out, 0x20000ul, 21u, 0x30140ul) == V9X_STATUS_OK);
    CHECK(out[0] == (0x20000ul | (1ul << 1)));
    CHECK((out[3] & 0xful) == 3ul && ((out[3] >> 4) & 0x3ful) == 1ul &&
          ((out[3] >> 11) & 0x3ful) == V9X_G4_SETUP_REGS);
    CHECK(((out[4] >> 11) & 0xfful) == V9X_G4_URB_SF_ENTRIES);
    CHECK(((out[4] >> 19) & 0x1ful) == V9X_G4_URB_SF_SIZE - 1u);
    CHECK((out[5] & 2ul) == 0ul);                    /* no viewport xform */
    CHECK(((out[6] >> 29) & 3ul) == 1ul);            /* cull none */

    CHECK(v9x_g4_vs_state(out) == V9X_STATUS_OK);
    CHECK((out[6] & 1ul) == 0ul && (out[6] & 2ul) != 0ul);
    CHECK(((out[4] >> 11) & 0xfful) == V9X_G4_URB_VS_ENTRIES);

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
    CHECK(v9x_g4_sampler_state(out, &s, 0x301c0ul) == V9X_STATUS_OK);
    CHECK(out[0] == ((1ul << 28) | (1ul << 17) | (1ul << 14)));
    CHECK(out[1] == ((2ul << 6) | (2ul << 3) | 2ul));
    CHECK(out[2] == 0x301c0ul);
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
    for (pre = 0ul; pre < 16ul; ++pre) {
        v9x_g4_batch_init(&batch, words, 512ul);
        CHECK(v9x_g4_draw_batch(&batch, &d) == V9X_STATUS_OK);
        CHECK(v9x_g4_batch_check(batch.dw, batch.count) == V9X_TRUE);
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
    /* With depth. */
    d.depth_address = 0x00200000ul;
    d.depth_pitch = 1280ul;
    d.depth_format = V9X_G4_DEPTH_D16_UNORM;
    v9x_g4_batch_init(&batch, words, 512ul);
    CHECK(v9x_g4_draw_batch(&batch, &d) == V9X_STATUS_OK);
    CHECK(v9x_g4_batch_check(batch.dw, batch.count) == V9X_TRUE);

    /* An unknown command, a short buffer and a missing end are refused. */
    words[2] ^= 0x00010000ul;
    CHECK(v9x_g4_batch_check(batch.dw, batch.count) == V9X_FALSE);
    words[2] ^= 0x00010000ul;
    CHECK(v9x_g4_batch_check(batch.dw, 5ul) == V9X_FALSE);
    CHECK(v9x_g4_batch_check(batch.dw, 0ul) == V9X_FALSE);

    /* Too small a buffer overflows rather than writing past it. */
    v9x_g4_batch_init(&batch, words, 8ul);
    CHECK(v9x_g4_draw_batch(&batch, &d) == V9X_STATUS_INSUFFICIENT_MEMORY);
    CHECK(v9x_g4_batch_start_header() == 0x18800180ul);
}

unsigned int v9x_run_intel_g4_tests(void)
{
    failures = 0u;
    test_encoder_against_xf86();
    test_sf_kernel();
    test_wm_kernels();
    test_layout();
    test_state();
    test_batch();
    return failures;
}
