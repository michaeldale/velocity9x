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

    /* VOP2 op 2 (VI v_sub_f32) is not mapped: refused at word 1. */
    words[0] = V9X_VI_S_ENDPGM;
    words[1] = v9x_vi_vop2(2ul, 1ul, 0x102ul, 3ul);
    CHECK(v9x_cik_shader_from_vi(words, 2ul, &at) == V9X_STATUS_UNSUPPORTED);
    CHECK(at == 1ul);
    /* SMEM (C0h prefix) is not translated. */
    words[0] = 0xc0020000ul;
    words[1] = 0ul;
    CHECK(v9x_cik_shader_from_vi(words, 2ul, &at) == V9X_STATUS_UNSUPPORTED);
    CHECK(at == 0ul);
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

unsigned int v9x_run_cik_shader_tests(void)
{
    failures = 0u;
    test_encodings();
    test_opcode_maps();
    test_literal();
    test_refusals();
    test_whole_shaders();
    return failures;
}
