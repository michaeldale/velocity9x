#include <stdio.h>

#include "velocity9x/cik_shader.h"
#include "velocity9x/vi_shader.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* One VI word or pair in, the CI word or pair out: the encodings are
 * LLVM's assembler tests (llvm/test/MC/AMDGPU), SI/CI line against VI
 * line, as little-endian bytes read into dwords. */
static void expect1(v9x_u32 vi, v9x_u32 ci)
{
    v9x_u32 word = vi;
    v9x_u32 at = 0xfffffffful;

    CHECK(v9x_cik_shader_from_vi(&word, 1ul, &at) == V9X_STATUS_OK);
    CHECK(word == ci);
    if (word != ci) {
        printf("  %08lx -> %08lx, expected %08lx\n", (unsigned long)vi,
               (unsigned long)word, (unsigned long)ci);
    }
}

static void expect2(v9x_u32 vi0, v9x_u32 vi1, v9x_u32 ci0, v9x_u32 ci1)
{
    v9x_u32 words[2];
    v9x_u32 at = 0xfffffffful;

    words[0] = vi0;
    words[1] = vi1;
    CHECK(v9x_cik_shader_from_vi(words, 2ul, &at) == V9X_STATUS_OK);
    CHECK(words[0] == ci0 && words[1] == ci1);
    if (words[0] != ci0 || words[1] != ci1) {
        printf("  %08lx %08lx -> %08lx %08lx\n", (unsigned long)vi0,
               (unsigned long)vi1, (unsigned long)words[0],
               (unsigned long)words[1]);
    }
}

static void test_encodings(void)
{
    /* vop2.s: v_add_u32_e32 v1, vcc, v2, v3 (VI) / v_add_i32 (SICI). */
    expect1(0x32020702ul, 0x4a020702ul);
    /* vop2.s: v_and_b32_e32 v1, v2, v3. */
    expect1(0x26020702ul, 0x36020702ul);
    /* exp.s: exp mrt0 off, off, off, off done. */
    expect2(0xc4000800ul, 0x00000000ul, 0xf8000800ul, 0x00000000ul);
    /* vintrp.s: v_interp_p1_f32 v1, v0, attr0.x. */
    expect1(0xd4040000ul, 0xc8040000ul);
    /* flat.s: flat_load_dwordx4 v[5:8], v[3:4]. */
    expect2(0xdc5c0000ul, 0x05000003ul, 0xdc380000ul, 0x05000003ul);
    /* flat.s: flat_store_dword v[3:4], v1 (CIVI, the same). */
    expect2(0xdc700000ul, 0x00000103ul, 0xdc700000ul, 0x00000103ul);
    /* vop3.s VI: v_cvt_pkrtz_f16_f32 v1, v2, v3; CI's VOP3 holds the
     * VOP2 opcode 2Fh plus 100h in 25:17 (vop3.s SICI v_addc_u32_e64:
     * D2500001h, 100h + 28h). */
    expect2(0xd2960001ul, 0x00020702ul, 0xd25e0001ul, 0x00020702ul);
    /* sop1.s: s_mov_b32 s1, s2 (VI op 0, SICI 3). */
    expect1(0xbe810002ul, 0xbe810302ul);
    /* sop1.s: s_mov_b64 s[2:3], s[4:5] and s_wqm_b64 s[2:3], s[4:5]. */
    expect1(0xbe820104ul, 0xbe820404ul);
    expect1(0xbe820704ul, 0xbe820a04ul);
    /* SOPP and image_sample pass through. */
    expect1(V9X_VI_S_ENDPGM, V9X_VI_S_ENDPGM);
    expect2(0xf0800f00ul, 0x00020100ul, 0xf0800f00ul, 0x00020100ul);
}

/* Pixel shader 1.1's instructions (docs\plans\ati-gcn-ps11.md step 1):
 * LLVM's SICI line against its VI line where vop1.s/vop3.s have one,
 * else Mesa's aco_opcodes.py columns. */
static void test_ps11_encodings(void)
{
    v9x_u32 smem[2];

    /* vop1.s: v_rcp_f32_e32 v1, v2. */
    expect1(0x7e024502ul, 0x7e025502ul);
    /* aco_opcodes.py: v_min_f32 VI 0Ah / SI 0Fh, v_max_f32 0Bh / 10h. */
    expect1(v9x_vi_vop2(V9X_VI_VOP2_MIN_F32, 5ul, V9X_VI_VGPR(1), 2ul),
            0x1e0a0501ul);
    expect1(v9x_vi_vop2(V9X_VI_VOP2_MAX_F32, 5ul, V9X_VI_VGPR(1), 2ul),
            0x200a0501ul);
    /* vop3.s: v_sub_f32_e64 v1, v3, s5. */
    expect2(0xd1020001ul, 0x00000b03ul, 0xd2080001ul, 0x00000b03ul);
    /* vop3.s: v_mad_f32 v9, 0.5, v5, -v8 (neg survives). */
    expect2(0xd1c10009ul, 0x84220af0ul, 0xd2820009ul, 0x84220af0ul);
    /* vop3.s: v_cubeid_f32 v0, |-1|, |-1.0|, |1.0| (abs survives). */
    expect2(0xd1c40700ul, 0x03c9e6c1ul, 0xd2880700ul, 0x03c9e6c1ul);
    /* vop3.s: v_fract_f32_e64 v1, v2 clamp div:2: a promoted VOP1 at
     * 180h + 20h, clamp moved to bit 11, omod kept. */
    expect2(0xd15b8001ul, 0x18000102ul, 0xd3400801ul, 0x18000102ul);
    /* vop3.s: v_mac_f32_e64 v0, v1, v2 mul:2. */
    expect2(0xd1160000ul, 0x08020501ul, 0xd23e0000ul, 0x08020501ul);
    /* vop3.s: v_cmp_lt_f32_e64 s[2:3], v4, -v6 (VOPC in VOP3). */
    expect2(0xd0410002ul, 0x40020d04ul, 0xd0020002ul, 0x40020d04ul);
    /* vop3.s: v_cndmask_b32_e64 v1, v3, v5, s[4:5]. */
    expect2(0xd1000001ul, 0x00120b03ul, 0xd2000001ul, 0x00120b03ul);
    /* aco_assembler.cpp's SMRD: s_load_dwordx4 s[20:23], s[2:3], 16 is
     * C0h | op 2 << 22 | 20 << 15 | 1 << 9 | imm | 4, then the s_nop. */
    CHECK(v9x_vi_s_load(4ul, 20ul, 2ul, 16ul, smem) == 2ul);
    expect2(smem[0], smem[1], 0xc08a0304ul, 0xbf800000ul);
    CHECK(v9x_vi_s_load(8ul, 8ul, 0ul, 0x3fcul, smem) == 2ul);
    expect2(smem[0], smem[1], 0xc0c401fful, 0xbf800000ul);
}

static void test_opcode_maps(void)
{
    /* The encoder's own words, through its constants. */
    expect1(v9x_vi_vop2(V9X_VI_VOP2_MUL_F32, 1ul, 0x102ul, 3ul),
            (0x08ul << 25) | (1ul << 17) | (3ul << 9) | 0x102ul);
    expect1(v9x_vi_vop2(V9X_VI_VOP2_MAC_F32, 4ul, 0x105ul, 6ul),
            (0x1ful << 25) | (4ul << 17) | (6ul << 9) | 0x105ul);
    expect1(v9x_vi_vopc(V9X_VI_VOPC_CMPX_LT_F32, 0x101ul, 2ul),
            0x7c000000ul | (0x11ul << 17) | (2ul << 9) | 0x101ul);
    expect1(v9x_vi_vopc(V9X_VI_VOPC_CMP_EQ_U32, 0x101ul, 2ul),
            0x7c000000ul | (0xc2ul << 17) | (2ul << 9) | 0x101ul);
    /* VOP1 is the same on both. */
    expect1(v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 3ul, 0x104ul),
            v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 3ul, 0x104ul));
}

static void test_literal(void)
{
    v9x_u32 words[3];

    /* v_mul_f32 v1, literal, v3; the literal must survive even when it
     * looks like an instruction. */
    words[0] = v9x_vi_vop2(V9X_VI_VOP2_MUL_F32, 1ul, 0xfful, 3ul);
    words[1] = 0xc4000800ul;
    words[2] = V9X_VI_S_ENDPGM;
    CHECK(v9x_cik_shader_from_vi(words, 3ul, 0) == V9X_STATUS_OK);
    CHECK(words[1] == 0xc4000800ul);
    CHECK(words[2] == V9X_VI_S_ENDPGM);
}

static void test_refusals(void)
{
    v9x_u32 words[4];
    v9x_u32 at = 0ul;

    /* VOP2 op 4 (VI v_mul_legacy_f32) is not mapped: refused at word 1. */
    words[0] = V9X_VI_S_ENDPGM;
    words[1] = v9x_vi_vop2(4ul, 1ul, 0x102ul, 3ul);
    CHECK(v9x_cik_shader_from_vi(words, 2ul, &at) == V9X_STATUS_UNSUPPORTED);
    CHECK(at == 1ul);
    /* SMEM (C0h prefix) is not translated. */
    words[0] = 0xc0020000ul;
    words[1] = 0ul;
    CHECK(v9x_cik_shader_from_vi(words, 2ul, &at) == V9X_STATUS_UNSUPPORTED);
    CHECK(at == 0ul);
    /* SMEM with an offset SMRD cannot hold, or with GLC. */
    words[0] = 0xc00a0501ul;
    words[1] = 0x400ul;
    CHECK(v9x_cik_shader_from_vi(words, 2ul, &at) == V9X_STATUS_UNSUPPORTED);
    words[0] = 0xc00a0501ul | 0x10000ul;
    words[1] = 0x10ul;
    CHECK(v9x_cik_shader_from_vi(words, 2ul, &at) == V9X_STATUS_UNSUPPORTED);
    /* A VOP3-only opcode with no row (VI v_fma_f32, 1CBh). */
    words[0] = 0xd1cb0005ul;
    words[1] = 0x040e0501ul;
    CHECK(v9x_cik_shader_from_vi(words, 2ul, &at) == V9X_STATUS_UNSUPPORTED);
    /* A two-dword instruction cut off by the count. */
    words[0] = 0xc4000800ul;
    CHECK(v9x_cik_shader_from_vi(words, 1ul, &at) == V9X_STATUS_UNSUPPORTED);
    /* A null pointer. */
    CHECK(v9x_cik_shader_from_vi(0, 1ul, &at) == V9X_STATUS_INVALID_ARGUMENT);
}

/* Every shader the GCN 3 encoder builds translates whole. */
static void test_whole_shaders(void)
{
    v9x_u32 words[512];
    v9x_u32 count;
    v9x_u32 at;

    count = v9x_vi_shader_solid_vs(words);
    CHECK(v9x_cik_shader_from_vi(words, count, &at) == V9X_STATUS_OK);
    count = v9x_vi_shader_solid_ps(words);
    CHECK(v9x_cik_shader_from_vi(words, count, &at) == V9X_STATUS_OK);
    count = v9x_vi_shader_probe_vs(words);
    CHECK(v9x_cik_shader_from_vi(words, count, &at) == V9X_STATUS_OK);
    count = v9x_vi_shader_probe_ps(words);
    CHECK(v9x_cik_shader_from_vi(words, count, &at) == V9X_STATUS_OK);
    count = v9x_vi_shader_tl_vs(words);
    CHECK(v9x_cik_shader_from_vi(words, count, &at) == V9X_STATUS_OK);
    count = v9x_vi_shader_gouraud_ps(words);
    CHECK(v9x_cik_shader_from_vi(words, count, &at) == V9X_STATUS_OK);
    count = v9x_vi_shader_texture_ps(words);
    CHECK(v9x_cik_shader_from_vi(words, count, &at) == V9X_STATUS_OK);
    count = v9x_vi_shader_modulate_ps(words);
    CHECK(v9x_cik_shader_from_vi(words, count, &at) == V9X_STATUS_OK);
}

/* The Direct3D pixel shaders the HAL builds: the slot shaders for every
 * op with and without specular, and the built one for every combination
 * of op, specular, colour key, fog and alpha test, the test under each of
 * the sixteen CMPX_*_F32 compares D3D's functions map to. */
static void test_d3d_shaders(void)
{
    v9x_u32 words[V9X_VI_D3D_PS_DWORDS_MAX];
    struct v9x_vi_d3d_ps ps;
    v9x_u32 count;
    v9x_u32 op;
    v9x_u32 options;
    v9x_u32 func;
    v9x_u32 at;

    for (op = V9X_VI_PS_COLOUR; op <= V9X_VI_PS_DECALALPHA; ++op) {
        count = v9x_vi_shader_d3d_ps(op, 0, words);
        CHECK(v9x_cik_shader_from_vi(words, count, &at) == V9X_STATUS_OK);
        count = v9x_vi_shader_d3d_ps(op, 1, words);
        CHECK(v9x_cik_shader_from_vi(words, count, &at) == V9X_STATUS_OK);
        for (options = 0ul; options < 16ul; ++options) {
            for (func = 0x50ul; func <= 0x5ful; ++func) {
                ps.op = op;
                ps.specular = options & 1ul;
                ps.key = (options >> 1) & 1ul;
                ps.key_colour[0] = 0x3f000000ul;
                ps.key_colour[1] = 0ul;
                ps.key_colour[2] = 0x3f800000ul;
                ps.key_tolerance2 = 0x3a000000ul;
                ps.fog = (options >> 2) & 1ul;
                ps.fog_colour[0] = 0ul;
                ps.fog_colour[1] = 0x3f800000ul;
                ps.fog_colour[2] = 0ul;
                ps.alpha_test = (options >> 3) & 1ul;
                ps.alpha_func = func;
                ps.alpha_ref = 0x3f000000ul;
                count = v9x_vi_shader_d3d_ps_built(&ps, words);
                CHECK(count != 0ul && count <= V9X_VI_D3D_PS_DWORDS_MAX);
                CHECK(v9x_cik_shader_from_vi(words, count, &at) ==
                      V9X_STATUS_OK);
                if (ps.alpha_test == 0ul) {
                    break;
                }
            }
        }
    }
}

unsigned int v9x_run_cik_shader_tests(void)
{
    failures = 0u;
    test_encodings();
    test_ps11_encodings();
    test_opcode_maps();
    test_literal();
    test_refusals();
    test_whole_shaders();
    test_d3d_shaders();
    return failures;
}
