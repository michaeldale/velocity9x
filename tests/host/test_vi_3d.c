#include <stdio.h>
#include <string.h>

#include "velocity9x/vi_3d.h"
#include "velocity9x/vi_shader.h"
#include "velocity9x/vi_clear_state.h"
#include "velocity9x/vi_gpu_init.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* The encoders against LLVM's GFX8 assembler tests, the little-endian
 * bytes read as a dword (build\external\carrizo\llvm-mc\). */
static void test_encodings(void)
{
    v9x_u32 out[2];

    /* gfx8_asm_vop1.s: v_mov_b32 v5, s1 [01 02 0a 7e]; v5, 0 [80 02 0a 7e]. */
    CHECK(v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 5ul, 1ul) == 0x7e0a0201ul);
    CHECK(v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 5ul, V9X_VI_INLINE_INT(0)) ==
          0x7e0a0280ul);
    /* gfx8_asm_vop2.s: v_cndmask_b32 v5, v1, v2, vcc [01 05 0a 00]. */
    CHECK(v9x_vi_vop2(V9X_VI_VOP2_CNDMASK_B32, 5ul, V9X_VI_VGPR(1), 2ul) ==
          0x000a0501ul);
    /* gfx8_asm_vopc.s: v_cmp_eq_u32 vcc, s1, v2 [01 04 94 7d];
     * vcc, -1, v2 [c1 04 94 7d]. */
    CHECK(v9x_vi_vopc(V9X_VI_VOPC_CMP_EQ_U32, 1ul, 2ul) == 0x7d940401ul);
    CHECK(v9x_vi_vopc(V9X_VI_VOPC_CMP_EQ_U32, 0xc1ul, 2ul) == 0x7d9404c1ul);
    /* exp.s: exp mrt0 v4, v3, v2, v1 done [0f 08 00 c4 04 03 02 01]. */
    CHECK(v9x_vi_exp(V9X_VI_EXP_MRT0, 1, 0, 4ul, 3ul, 2ul, 1ul, out) == 2ul);
    CHECK(out[0] == 0xc400080ful);
    CHECK(out[1] == 0x01020304ul);
    /* gfx8_asm_exp.s: exp pos0 v0 x4 [cf 00 00 c4], param0 [0f 02 00 c4],
     * mrt0 vm [0f 10 00 c4]. */
    (void)v9x_vi_exp(V9X_VI_EXP_POS0, 0, 0, 0ul, 0ul, 0ul, 0ul, out);
    CHECK(out[0] == 0xc40000cful);
    CHECK(out[1] == 0ul);
    (void)v9x_vi_exp(V9X_VI_EXP_PARAM0, 0, 0, 0ul, 0ul, 0ul, 0ul, out);
    CHECK(out[0] == 0xc400020ful);
    (void)v9x_vi_exp(V9X_VI_EXP_MRT0, 0, 1, 0ul, 0ul, 0ul, 0ul, out);
    CHECK(out[0] == 0xc400100ful);
    /* gfx8_asm_flat.s: flat_store_dword v[1:2], v2 [00 00 70 dc 01 02 00 00];
     * v[1:2], v255 [.. 01 ff 00 00]. */
    CHECK(v9x_vi_flat_store_dword(1ul, 2ul, out) == 2ul);
    CHECK(out[0] == 0xdc700000ul);
    CHECK(out[1] == 0x00000201ul);
    (void)v9x_vi_flat_store_dword(1ul, 255ul, out);
    CHECK(out[1] == 0x0000ff01ul);
    /* gfx8_asm_vop2.s: v_add_u32 v5, vcc, s1, v2 [01 04 0a 32];
     * v_lshlrev_b32 v5, s1, v2 [01 04 0a 24]. */
    CHECK(v9x_vi_vop2(V9X_VI_VOP2_ADD_U32, 5ul, 1ul, 2ul) == 0x320a0401ul);
    CHECK(v9x_vi_vop2(V9X_VI_VOP2_LSHLREV_B32, 5ul, 1ul, 2ul) ==
          0x240a0401ul);
    /* v_and_b32 v5, s1, v2 [01 04 0a 26]. */
    CHECK(v9x_vi_vop2(V9X_VI_VOP2_AND_B32, 5ul, 1ul, 2ul) == 0x260a0401ul);
    /* gfx8_asm_vop2.s: v_addc_u32 v5, vcc, v1, v2, vcc [01 05 0a 38];
     * v_mul_f32 v5, v1, v2 [01 05 0a 0a]. */
    CHECK(v9x_vi_vop2(V9X_VI_VOP2_ADDC_U32, 5ul, V9X_VI_VGPR(1), 2ul) ==
          0x380a0501ul);
    CHECK(v9x_vi_vop2(V9X_VI_VOP2_MUL_F32, 5ul, V9X_VI_VGPR(1), 2ul) ==
          0x0a0a0501ul);
    /* gfx8_asm_vop1.s: v_cvt_f32_ubyte0 v5, v1 [01 23 0a 7e]; ubyte1
     * [01 25 ..]; ubyte2 [01 27 ..]; ubyte3 [01 29 ..]; v_mov_b32 v5,
     * 0xaf123456 [ff 02 0a 7e] then the literal. */
    CHECK(v9x_vi_vop1(V9X_VI_VOP1_CVT_F32_UBYTE0, 5ul, V9X_VI_VGPR(1)) ==
          0x7e0a2301ul);
    CHECK(v9x_vi_vop1(V9X_VI_VOP1_CVT_F32_UBYTE1, 5ul, V9X_VI_VGPR(1)) ==
          0x7e0a2501ul);
    CHECK(v9x_vi_vop1(V9X_VI_VOP1_CVT_F32_UBYTE2, 5ul, V9X_VI_VGPR(1)) ==
          0x7e0a2701ul);
    CHECK(v9x_vi_vop1(V9X_VI_VOP1_CVT_F32_UBYTE3, 5ul, V9X_VI_VGPR(1)) ==
          0x7e0a2901ul);
    CHECK(v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 5ul, V9X_VI_LITERAL) ==
          0x7e0a02fful);
    /* gfx8_asm_flat.s: flat_load_dwordx4 v[5:8], v[1:2]
     * [00 00 5c dc 01 00 00 05]. */
    CHECK(v9x_vi_flat_load_dwordx4(1ul, 5ul, out) == 2ul);
    CHECK(out[0] == 0xdc5c0000ul);
    CHECK(out[1] == 0x05000001ul);
    /* vintrp.s, VI: v_interp_p1_f32 v1, v0, attr0.x [00 00 04 d4];
     * v10, v0, attr32.w [00 83 28 d4]; v_interp_p2_f32 v3, v1, attr0.y
     * [01 01 0d d4]. */
    CHECK(v9x_vi_vintrp(V9X_VI_VINTRP_P1_F32, 1ul, 0ul, 0ul, 0ul) ==
          0xd4040000ul);
    CHECK(v9x_vi_vintrp(V9X_VI_VINTRP_P1_F32, 10ul, 0ul, 32ul, 3ul) ==
          0xd4288300ul);
    CHECK(v9x_vi_vintrp(V9X_VI_VINTRP_P2_F32, 3ul, 1ul, 0ul, 1ul) ==
          0xd40d0101ul);
    /* gfx8_asm_sop1.s: s_mov_b32 m0, s1 [01 00 fc be]; gfx8_asm_sopp.s:
     * s_nop 0x3141 [41 31 80 bf]. */
    CHECK(v9x_vi_s_mov_m0(1ul) == 0xbefc0001ul);
    CHECK(v9x_vi_s_nop(0x3141ul) == 0xbf803141ul);
    /* mimg.s, GCN: image_sample v193, v237, s[28:35], s[4:7]
     * [00 00 80 f0 ed c1 27 00]; dmask:0x7 unorm [00 17 80 f0 ...], unorm
     * being bit 12, which the encoder never sets. */
    CHECK(v9x_vi_image_sample(193ul, 237ul, 28ul, 4ul, 0ul, out) == 2ul);
    CHECK(out[0] == 0xf0800000ul);
    CHECK(out[1] == 0x0027c1edul);
    (void)v9x_vi_image_sample(193ul, 237ul, 28ul, 4ul, 7ul, out);
    CHECK((out[0] | 0x1000ul) == 0xf0801700ul);
    /* gfx8_asm_sopp.s: s_waitcnt 0x3141 [41 31 8c bf]. */
    CHECK(v9x_vi_s_waitcnt(0x3141ul) == 0xbf8c3141ul);
}

/* The two shaders word for word, as assembled by hand from those rules. */
static void test_shaders(void)
{
    static const v9x_u32 vs[V9X_VI_SOLID_VS_DWORDS] = {
        0x7d940081ul,   /* v_cmp_eq_u32 vcc, 1, v0 */
        0x7e020200ul,   /* v_mov_b32 v1, s0 (x1) */
        0x7e040202ul,   /* v_mov_b32 v2, s2 (x2) */
        0x00020501ul,   /* v_cndmask_b32 v1, v1, v2, vcc */
        0x7d940082ul,   /* v_cmp_eq_u32 vcc, 2, v0 */
        0x7e060201ul,   /* v_mov_b32 v3, s1 (y1) */
        0x7e080203ul,   /* v_mov_b32 v4, s3 (y2) */
        0x00060903ul,   /* v_cndmask_b32 v3, v3, v4, vcc */
        0x7e080280ul,   /* v_mov_b32 v4, 0 */
        0x7e0a02f2ul,   /* v_mov_b32 v5, 1.0 */
        0xc400020ful,   /* exp param0 v4, v4, v4, v4 */
        0x04040404ul,
        0xc40008cful,   /* exp pos0 v1, v3, v4, v5 done */
        0x05040301ul,
        0xbf810000ul    /* s_endpgm */
    };
    static const v9x_u32 ps[V9X_VI_SOLID_PS_DWORDS] = {
        0x7e000200ul,   /* v_mov_b32 v0, s0 */
        0x7e020201ul,   /* v_mov_b32 v1, s1 */
        0x7e040202ul,   /* v_mov_b32 v2, s2 */
        0x7e060203ul,   /* v_mov_b32 v3, s3 */
        0xc400180ful,   /* exp mrt0 v0, v1, v2, v3 done vm */
        0x03020100ul,
        0xbf810000ul    /* s_endpgm */
    };
    v9x_u32 out[16];

    CHECK(v9x_vi_shader_solid_vs(out) == V9X_VI_SOLID_VS_DWORDS);
    CHECK(memcmp(out, vs, sizeof(vs)) == 0);
    CHECK(v9x_vi_shader_solid_ps(out) == V9X_VI_SOLID_PS_DWORDS);
    CHECK(memcmp(out, ps, sizeof(ps)) == 0);
}

/* The probe VS: its head, the solid VS's eight ALU dwords, the position
 * stores, then the solid VS's five export dwords. */
typedef char probe_vs_length[
    V9X_VI_PROBE_VS_DWORDS == 11ul + V9X_VI_SOLID_VS_DWORDS + 16ul ? 1 : -1];

/* The probe shaders: a FLAT store of a marker to the address in s4:s5,
 * waited for, then the solid shader unchanged. */
static void test_probe_shaders(void)
{
    static const v9x_u32 vs_head[11] = {
        0x7e0c0204ul,   /* v_mov_b32 v6, s4 */
        0x7e0e0205ul,   /* v_mov_b32 v7, s5 */
        0xdc700000ul,   /* flat_store_dword v[6:7], v0 (VertexID) */
        0x00000006ul,
        0x260c0083ul,   /* v_and_b32 v6, 3, v0 */
        0x240c0c82ul,   /* v_lshlrev_b32 v6, 2, v6 */
        0x320c0c04ul,   /* v_add_u32 v6, vcc, s4, v6 */
        0x320c0c90ul,   /* v_add_u32 v6, vcc, 16, v6 */
        0xdc700000ul,   /* flat_store_dword v[6:7], v0 */
        0x00000006ul,
        0xbf8c0000ul    /* s_waitcnt 0 */
    };
    static const v9x_u32 ps_head[6] = {
        0x7e080204ul,   /* v_mov_b32 v4, s4 */
        0x7e0a0205ul,   /* v_mov_b32 v5, s5 */
        0x7e0c0200ul,   /* v_mov_b32 v6, s0 (red) */
        0xdc700000ul,   /* flat_store_dword v[4:5], v6 */
        0x00000604ul,
        0xbf8c0000ul    /* s_waitcnt 0 */
    };
    /* After the position is computed and before it is exported: x at
     * +32 + 4 x id, y at +48 + 4 x id. */
    /* Then v2 at +64 + 4 x id, and VCC as v_cmp_eq_u32 1, v0 sets it
     * (1.0 where set, 0 elsewhere) at +80 + 4 x id, v4 put back to 0. */
    static const v9x_u32 vs_positions[15] = {
        0x320c0c90ul,   /* v_add_u32 v6, vcc, 16, v6 */
        0xdc700000ul,   /* flat_store_dword v[6:7], v1 (x) */
        0x00000106ul,
        0x320c0c90ul,   /* v_add_u32 v6, vcc, 16, v6 */
        0xdc700000ul,   /* flat_store_dword v[6:7], v3 (y) */
        0x00000306ul,
        0x320c0c90ul,   /* v_add_u32 v6, vcc, 16, v6 */
        0xdc700000ul,   /* flat_store_dword v[6:7], v2 (x2) */
        0x00000206ul,
        0x7d940081ul,   /* v_cmp_eq_u32 vcc, 1, v0 */
        0x00080a80ul,   /* v_cndmask_b32 v4, 0, v5 (1.0), vcc */
        0x320c0c90ul,   /* v_add_u32 v6, vcc, 16, v6 */
        0xdc700000ul,   /* flat_store_dword v[6:7], v4 */
        0x00000406ul,
        0xbf8c0000ul    /* s_waitcnt 0 */
    };
    static const v9x_u32 vs_restore = 0x7e080280ul;  /* v_mov_b32 v4, 0 */
    v9x_u32 solid[16];
    v9x_u32 out[48];

    /* The solid VS is ten ALU dwords, then its exports and s_endpgm. */
    CHECK(v9x_vi_shader_probe_vs(out) == V9X_VI_PROBE_VS_DWORDS);
    CHECK(memcmp(out, vs_head, 11ul * sizeof(v9x_u32)) == 0);
    (void)v9x_vi_shader_solid_vs(solid);
    CHECK(memcmp(out + 11, solid, 10ul * sizeof(v9x_u32)) == 0);
    CHECK(memcmp(out + 21, vs_positions, sizeof(vs_positions)) == 0);
    CHECK(out[36] == vs_restore);
    CHECK(memcmp(out + 37, solid + 10, 5ul * sizeof(v9x_u32)) == 0);
    CHECK(v9x_vi_shader_probe_ps(out) == 6ul + V9X_VI_SOLID_PS_DWORDS);
    CHECK(memcmp(out, ps_head, 6ul * sizeof(v9x_u32)) == 0);
    (void)v9x_vi_shader_solid_ps(solid);
    CHECK(memcmp(out + 6, solid, V9X_VI_SOLID_PS_DWORDS * sizeof(v9x_u32)) == 0);
}

/* The Direct3D TL-vertex VS and the Gouraud PS, hand-assembled. Every VALU
 * word reads at most one SGPR, VCC or literal. */
static void test_tl_shaders(void)
{
    static const v9x_u32 vs[V9X_VI_TL_VS_DWORDS] = {
        0x24020085ul,   /* v_lshlrev_b32 v1, 5, v0 (32 bytes a vertex) */
        0x32020200ul,   /* v_add_u32 v1, vcc, s0, v1 */
        0x7e040201ul,   /* v_mov_b32 v2, s1 */
        0x38040480ul,   /* v_addc_u32 v2, vcc, 0, v2, vcc */
        0xdc5c0000ul,   /* flat_load_dwordx4 v[3:6], v[1:2] (sx sy sz rhw) */
        0x03000001ul,
        0x320e0290ul,   /* v_add_u32 v7, vcc, 16, v1 */
        0x38100480ul,   /* v_addc_u32 v8, vcc, 0, v2, vcc */
        0xdc5c0000ul,   /* flat_load_dwordx4 v[9:12], v[7:8] (colour ..) */
        0x09000007ul,
        0xbf8c0000ul,   /* s_waitcnt 0 */
        0x7e022709ul,   /* v_cvt_f32_ubyte2 v1, v9 (red) */
        0x7e042509ul,   /* v_cvt_f32_ubyte1 v2, v9 (green) */
        0x7e0e2309ul,   /* v_cvt_f32_ubyte0 v7, v9 (blue) */
        0x7e102909ul,   /* v_cvt_f32_ubyte3 v8, v9 (alpha) */
        0x0a0202fful,   /* v_mul_f32 v1, 1/255, v1 */
        0x3b808081ul,
        0x0a0404fful,   /* v_mul_f32 v2, 1/255, v2 */
        0x3b808081ul,
        0x0a0e0efful,   /* v_mul_f32 v7, 1/255, v7 */
        0x3b808081ul,
        0x0a1010fful,   /* v_mul_f32 v8, 1/255, v8 */
        0x3b808081ul,
        0xc400020ful,   /* exp param0 v1, v2, v7, v8 */
        0x08070201ul,
        0x7e120280ul,   /* v_mov_b32 v9, 0 */
        0x7e1402f2ul,   /* v_mov_b32 v10, 1.0 */
        0xc400021ful,   /* exp param1 v11, v12, v9, v10 (tu tv 0 1) */
        0x0a090c0bul,
        0xc40008cful,   /* exp pos0 v3, v4, v5, v6 done */
        0x06050403ul,
        0xbf810000ul    /* s_endpgm */
    };
    static const v9x_u32 ps[V9X_VI_GOURAUD_PS_DWORDS] = {
        0xbefc0000ul,   /* s_mov_b32 m0, s0 (PRIM_MASK) */
        0xbf800000ul,   /* s_nop 0: M0 to v_interp needs a wait state */
        0xd4080000ul,   /* v_interp_p1_f32 v2, v0, attr0.x */
        0xd4090001ul,   /* v_interp_p2_f32 v2, v1, attr0.x */
        0xd40c0100ul,   /* v_interp_p1_f32 v3, v0, attr0.y */
        0xd40d0101ul,   /* v_interp_p2_f32 v3, v1, attr0.y */
        0xd4100200ul,   /* v_interp_p1_f32 v4, v0, attr0.z */
        0xd4110201ul,   /* v_interp_p2_f32 v4, v1, attr0.z */
        0xd4140300ul,   /* v_interp_p1_f32 v5, v0, attr0.w */
        0xd4150301ul,   /* v_interp_p2_f32 v5, v1, attr0.w */
        0xc400180ful,   /* exp mrt0 v2, v3, v4, v5 done vm */
        0x05040302ul,
        0xbf810000ul    /* s_endpgm */
    };
    v9x_u32 out[48];

    CHECK(v9x_vi_shader_tl_vs(out) == V9X_VI_TL_VS_DWORDS);
    CHECK(memcmp(out, vs, sizeof(vs)) == 0);
    CHECK(v9x_vi_shader_gouraud_ps(out) == V9X_VI_GOURAUD_PS_DWORDS);
    CHECK(memcmp(out, ps, sizeof(ps)) == 0);
}

/* The textured pixel shaders: T# s[0:7] and S# s[8:11] are user SGPRs, so
 * PRIM_MASK is s12. Replace samples parameter 1's (u, v); modulate also
 * interpolates parameter 0 and multiplies. */
static void test_texture_shaders(void)
{
    static const v9x_u32 replace[V9X_VI_TEXTURE_PS_DWORDS] = {
        0xbefc000cul,   /* s_mov_b32 m0, s12 (PRIM_MASK) */
        0xbf800000ul,   /* s_nop 0 */
        0xd4080400ul,   /* v_interp_p1_f32 v2, v0, attr1.x */
        0xd4090401ul,   /* v_interp_p2_f32 v2, v1, attr1.x */
        0xd40c0500ul,   /* v_interp_p1_f32 v3, v0, attr1.y */
        0xd40d0501ul,   /* v_interp_p2_f32 v3, v1, attr1.y */
        0xf0800f00ul,   /* image_sample v[4:7], v[2:3], s[0:7], s[8:11] */
        0x00400402ul,   /*   dmask:0xf */
        0xbf8c0000ul,   /* s_waitcnt 0 */
        0xc400180ful,   /* exp mrt0 v4, v5, v6, v7 done vm */
        0x07060504ul,
        0xbf810000ul    /* s_endpgm */
    };
    static const v9x_u32 modulate_tail[V9X_VI_MODULATE_PS_DWORDS - 6u] = {
        0xd4200000ul,   /* v_interp_p1_f32 v8, v0, attr0.x */
        0xd4210001ul,   /* v_interp_p2_f32 v8, v1, attr0.x */
        0xd4240100ul,   /* v_interp_p1_f32 v9, v0, attr0.y */
        0xd4250101ul,   /* v_interp_p2_f32 v9, v1, attr0.y */
        0xd4280200ul,   /* v_interp_p1_f32 v10, v0, attr0.z */
        0xd4290201ul,   /* v_interp_p2_f32 v10, v1, attr0.z */
        0xd42c0300ul,   /* v_interp_p1_f32 v11, v0, attr0.w */
        0xd42d0301ul,   /* v_interp_p2_f32 v11, v1, attr0.w */
        0xf0800f00ul,   /* image_sample v[4:7], v[2:3], s[0:7], s[8:11] */
        0x00400402ul,
        0xbf8c0000ul,   /* s_waitcnt 0 */
        0x0a080908ul,   /* v_mul_f32 v4, v8, v4 */
        0x0a0a0b09ul,   /* v_mul_f32 v5, v9, v5 */
        0x0a0c0d0aul,   /* v_mul_f32 v6, v10, v6 */
        0x0a0e0f0bul,   /* v_mul_f32 v7, v11, v7 */
        0xc400180ful,   /* exp mrt0 v4, v5, v6, v7 done vm */
        0x07060504ul,
        0xbf810000ul    /* s_endpgm */
    };
    v9x_u32 out[48];

    CHECK(v9x_vi_shader_texture_ps(out) == V9X_VI_TEXTURE_PS_DWORDS);
    CHECK(memcmp(out, replace, sizeof(replace)) == 0);
    CHECK(v9x_vi_shader_modulate_ps(out) == V9X_VI_MODULATE_PS_DWORDS);
    /* The same six-dword head: M0, the wait, parameter 1's u and v. */
    CHECK(memcmp(out, replace, 6u * sizeof(v9x_u32)) == 0);
    CHECK(memcmp(out + 6, modulate_tail, sizeof(modulate_tail)) == 0);
}

/* Every type-3 packet's count must land the next header exactly at the
 * stream's end. Returns the number of packets, 0 on a mismatch. */
static v9x_u32 walk(const v9x_u32 *dwords, v9x_u32 count)
{
    v9x_u32 at = 0ul;
    v9x_u32 packets = 0ul;

    while (at < count) {
        if ((dwords[at] >> 30) != 3ul) {
            return 0ul;
        }
        if (dwords[at] == V9X_VI_RING_NOP) {
            at += 1ul;
        } else {
            at += ((dwords[at] >> 16) & 0x3ffful) + 2ul;
        }
        ++packets;
    }
    return at == count ? packets : 0ul;
}

static v9x_u32 buffer[2048];

static void test_clear_state(void)
{
    v9x_u32 index;
    v9x_u32 total = 0ul;

    for (index = 0ul; index < V9X_VI_CLEAR_STATE_RUNS; ++index) {
        total += v9x_vi_clear_state_runs[index].count;
    }
    CHECK(total == V9X_VI_CLEAR_STATE_DWORDS);
    CHECK(v9x_vi_clear_state_runs[0].first == 0xa000ul);
    CHECK(v9x_vi_clear_state_runs[0].count == 212ul);
    CHECK(v9x_vi_clear_state_runs[6].first == 0xa2a5ul);
    CHECK(v9x_vi_clear_state_runs[6].count == 233ul);
}

static void test_start(void)
{
    struct v9x_vi_pm4 stream;

    v9x_vi_pm4_init(&stream, buffer, 2048ul);
    v9x_vi_3d_start(&stream);
    CHECK(stream.overflow == 0ul);
    CHECK(stream.count == V9X_VI_START_DWORDS);
    CHECK(walk(buffer, stream.count) != 0ul);
    CHECK(buffer[0] == 0xc0004a00ul);
    CHECK(buffer[1] == 0x20000000ul);
    CHECK(buffer[2] == 0xc0012800ul);
    /* The first context run: SET_CONTEXT_REG of 212 at offset 0. */
    CHECK(buffer[5] == V9X_VI_PACKET3(V9X_VI_P3_SET_CONTEXT_REG, 212ul));
    CHECK(buffer[6] == 0ul);
    /* The end: CLEAR_STATE, then SET_BASE CE partitions 8000h. */
    CHECK(buffer[stream.count - 6ul] == 0xc0001200ul);
    CHECK(buffer[stream.count - 4ul] == 0xc0021100ul);
    CHECK(buffer[stream.count - 3ul] == 3ul);
    CHECK(buffer[stream.count - 1ul] == 0x8000ul);
}

static void base_rect(struct v9x_vi_solid_rect *rect)
{
    memset(rect, 0, sizeof(*rect));
    rect->target_low = 0x0f809000ul;
    rect->target_high = 0xf4ul;
    rect->pitch_pixels = 64ul;
    rect->height = 16ul;
    rect->format = V9X_VI_CB_FORMAT_8_8_8_8;
    rect->tile_mode_index = V9X_VI_TILE_LINEAR_ALIGNED;
    rect->vs_low = 0x0f804000ul;
    rect->ps_low = 0x0f804100ul;
    rect->shader_high = 0xf4ul;
    rect->left = 8ul;
    rect->top = 4ul;
    rect->right = 40ul;
    rect->bottom = 12ul;
    rect->color[0] = 0x3f800000ul;
    rect->fence_low = 0x0f808100ul;
    rect->fence_high = 0xf4ul;
    rect->fence_value = 0x12345678ul;
}

/* Finds the first SET_CONTEXT_REG whose offset is reg's; returns the index
 * of its first value, 0 if none. */
static v9x_u32 find_context(const v9x_u32 *dwords, v9x_u32 count, v9x_u32 reg)
{
    v9x_u32 at = 0ul;

    while (at < count) {
        if ((dwords[at] & 0xff00ul) ==
                (V9X_VI_P3_SET_CONTEXT_REG << 8) &&
            dwords[at + 1ul] == reg - V9X_VI_CONTEXT_REG_START) {
            return at + 2ul;
        }
        at += dwords[at] == V9X_VI_RING_NOP ? 1ul :
              ((dwords[at] >> 16) & 0x3ffful) + 2ul;
    }
    return 0ul;
}

static void test_solid_rect(void)
{
    struct v9x_vi_pm4 stream;
    struct v9x_vi_solid_rect rect;
    v9x_u32 at;

    base_rect(&rect);
    v9x_vi_pm4_init(&stream, buffer, 2048ul);
    CHECK(v9x_vi_3d_solid_rect(&stream, &rect) == V9X_STATUS_OK);
    CHECK(stream.overflow == 0ul);
    CHECK(walk(buffer, stream.count) != 0ul);

    /* First, gfx_v8_0_emit_mem_sync: SURFACE_SYNC invalidating the SQ
     * instruction and constant caches and TC L1/L2, writing L2 back. */
    CHECK(buffer[0] == V9X_VI_PACKET3(V9X_VI_P3_SURFACE_SYNC, 3ul));
    CHECK(buffer[1] == 0x28c40000ul);
    CHECK(buffer[2] == 0xfffffffful);
    CHECK(buffer[3] == 0ul);
    CHECK(buffer[4] == 0x0000000aul);

    /* CB_COLOR0_BASE..ATTRIB: the 40-bit address >> 8, pitch and slice in
     * 8-pixel and 64-pixel tiles less one, INFO's format, the tile index. */
    at = find_context(buffer, stream.count, 0xa318ul);
    CHECK(at != 0ul);
    CHECK(buffer[at] == 0xf40f8090ul);
    CHECK(buffer[at + 1ul] == 7ul);
    CHECK(buffer[at + 2ul] == 15ul);
    CHECK(buffer[at + 4ul] == (V9X_VI_CB_FORMAT_8_8_8_8 << 2));
    CHECK(buffer[at + 5ul] == V9X_VI_TILE_LINEAR_ALIGNED);

    /* VGT_MAX_VTX_INDX, _MIN_, _INDX_OFFSET: radeonsi's values over the
     * clear state's maximum of 0. */
    at = find_context(buffer, stream.count, 0xa100ul);
    CHECK(at != 0ul);
    CHECK(buffer[at] == 0xfffffffful);
    CHECK(buffer[at + 1ul] == 0ul);
    CHECK(buffer[at + 2ul] == 0ul);

    /* The VS user SGPRs: 8, 4, 40 and 12 as float bits. */
    at = 0ul;
    while (at < stream.count &&
           !((buffer[at] & 0xff00ul) == (V9X_VI_P3_SET_SH_REG << 8) &&
             buffer[at + 1ul] == 0x4cul)) {
        at += buffer[at] == V9X_VI_RING_NOP ? 1ul :
              ((buffer[at] >> 16) & 0x3ffful) + 2ul;
    }
    CHECK(at < stream.count);
    CHECK(buffer[at + 2ul] == 0x41000000ul);
    CHECK(buffer[at + 3ul] == 0x40800000ul);
    CHECK(buffer[at + 4ul] == 0x42200000ul);
    CHECK(buffer[at + 5ul] == 0x41400000ul);

    /* The tail: DRAW_INDEX_AUTO of 3 auto-indexed vertices, then the
     * end-of-pipe fence (Linux's ring_emit_fence_gfx with a 32-bit value). */
    CHECK(buffer[stream.count - 9ul] ==
          V9X_VI_PACKET3(V9X_VI_P3_DRAW_INDEX_AUTO, 1ul));
    CHECK(buffer[stream.count - 8ul] == 3ul);
    CHECK(buffer[stream.count - 7ul] == 2ul);
    CHECK(buffer[stream.count - 6ul] ==
          V9X_VI_PACKET3(V9X_VI_P3_EVENT_WRITE_EOP, 4ul));
    /* EOP_TCL1_ACTION_EN | EOP_TC_ACTION_EN | EOP_TC_WB_ACTION_EN |
     * EVENT_TYPE(CACHE_FLUSH_AND_INV_TS_EVENT) | EVENT_INDEX(5). */
    CHECK(buffer[stream.count - 5ul] == 0x00038514ul);
    CHECK(buffer[stream.count - 4ul] == 0x0f808100ul);
    CHECK(buffer[stream.count - 3ul] == (0xf4ul | (1ul << 29)));
    CHECK(buffer[stream.count - 2ul] == 0x12345678ul);
    CHECK(buffer[stream.count - 1ul] == 0ul);

    /* Refusals: a rectangle past the target, an odd pitch, an unaligned
     * shader. */
    base_rect(&rect);
    rect.right = 65ul;
    CHECK(v9x_vi_3d_solid_rect(&stream, &rect) == V9X_STATUS_INVALID_ARGUMENT);
    base_rect(&rect);
    rect.pitch_pixels = 60ul;
    CHECK(v9x_vi_3d_solid_rect(&stream, &rect) == V9X_STATUS_INVALID_ARGUMENT);
    base_rect(&rect);
    rect.ps_low = 0x0f804104ul;
    CHECK(v9x_vi_3d_solid_rect(&stream, &rect) == V9X_STATUS_INVALID_ARGUMENT);
    base_rect(&rect);
    rect.probe_vs_low = 0x0f80a002ul;
    CHECK(v9x_vi_3d_solid_rect(&stream, &rect) == V9X_STATUS_INVALID_ARGUMENT);
}

/* Finds the first SET_SH_REG whose offset is reg's; returns the index of
 * its header, count if none. */
static v9x_u32 find_sh(const v9x_u32 *dwords, v9x_u32 count, v9x_u32 reg)
{
    v9x_u32 at = 0ul;

    while (at < count &&
           !((dwords[at] & 0xff00ul) == (V9X_VI_P3_SET_SH_REG << 8) &&
             dwords[at + 1ul] == reg - V9X_VI_SH_REG_START)) {
        at += dwords[at] == V9X_VI_RING_NOP ? 1ul :
              ((dwords[at] >> 16) & 0x3ffful) + 2ul;
    }
    return at;
}

static void base_tl(struct v9x_vi_tl_draw *draw)
{
    memset(draw, 0, sizeof(*draw));
    draw->target_low = 0x0f809000ul;
    draw->target_high = 0xf4ul;
    draw->pitch_pixels = 64ul;
    draw->height = 16ul;
    draw->format = V9X_VI_CB_FORMAT_8_8_8_8;
    draw->tile_mode_index = V9X_VI_TILE_LINEAR_ALIGNED;
    draw->vs_low = 0x0f804000ul;
    draw->ps_low = 0x0f804100ul;
    draw->shader_high = 0xf4ul;
    draw->vertex_low = 0x0f80b000ul;
    draw->vertex_high = 0xf4ul;
    draw->vertex_count = 6ul;
    draw->fence_low = 0x0f808100ul;
    draw->fence_high = 0xf4ul;
    draw->fence_value = 0x12345678ul;
}

/* A TL-vertex triangle list: the cache sync first, the same target and
 * screen-space state as the rectangle, the VS's vertex address as its
 * two user SGPRs, one Gouraud interpolant, a TRILIST of vertex_count. */
static void test_tl_draw(void)
{
    struct v9x_vi_pm4 stream;
    struct v9x_vi_tl_draw draw;
    v9x_u32 at;

    base_tl(&draw);
    v9x_vi_pm4_init(&stream, buffer, 2048ul);
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_OK);
    CHECK(stream.overflow == 0ul);
    CHECK(walk(buffer, stream.count) != 0ul);
    CHECK(buffer[0] == V9X_VI_PACKET3(V9X_VI_P3_SURFACE_SYNC, 3ul));

    at = find_context(buffer, stream.count, 0xa318ul);
    CHECK(at != 0ul && buffer[at] == 0xf40f8090ul);
    /* PA_CL_VTE_CNTL: screen-space XY, Z and 1/W. */
    at = find_context(buffer, stream.count, 0xa206ul);
    CHECK(at != 0ul && buffer[at] == 0x00000700ul);
    /* Two parameters, colour and (tu, tv): SPI_VS_OUT_CONFIG
     * VS_EXPORT_COUNT 1 at 5:1; SPI_PS_IN_CONTROL two interpolants;
     * SPI_PS_INPUT_CNTL_0 and _1 parameters 0 and 1. */
    at = find_context(buffer, stream.count, 0xa1b1ul);
    CHECK(at != 0ul && buffer[at] == 2ul);
    at = find_context(buffer, stream.count, 0xa1b6ul);
    CHECK(at != 0ul && buffer[at] == 2ul);
    at = find_context(buffer, stream.count, 0xa191ul);
    CHECK(at != 0ul && buffer[at] == 0ul);
    at = find_context(buffer, stream.count, 0xa192ul);
    CHECK(at != 0ul && buffer[at] == 1ul);

    /* VS: RSRC1 with 16 VGPRs (granule 3), two user SGPRs, then the
     * vertex address. */
    at = find_sh(buffer, stream.count, 0x2c46ul);
    CHECK(at < stream.count);
    CHECK(buffer[at + 4ul] == 0xf40f8040ul);
    CHECK((buffer[at + 6ul] & 0x3ful) == 3ul);
    CHECK(buffer[at + 7ul] == (2ul << 1));
    at = find_sh(buffer, stream.count, 0x2c4cul);
    CHECK(at < stream.count);
    CHECK(buffer[at] == V9X_VI_PACKET3(V9X_VI_P3_SET_SH_REG, 2ul));
    CHECK(buffer[at + 2ul] == 0x0f80b000ul);
    CHECK(buffer[at + 3ul] == 0xf4ul);
    /* PS: no user SGPRs, so PRIM_MASK lands in s0. */
    at = find_sh(buffer, stream.count, 0x2c07ul);
    CHECK(at < stream.count);
    CHECK(buffer[at + 6ul] == 0ul);

    /* VGT_PRIMITIVE_TYPE TRILIST, NUM_INSTANCES, then six auto-indexed
     * vertices and the fence. */
    CHECK(buffer[stream.count - 14ul] ==
          V9X_VI_PACKET3(V9X_VI_P3_SET_UCONFIG_REG, 1ul));
    CHECK(buffer[stream.count - 12ul] == 4ul);
    CHECK(buffer[stream.count - 9ul] ==
          V9X_VI_PACKET3(V9X_VI_P3_DRAW_INDEX_AUTO, 1ul));
    CHECK(buffer[stream.count - 8ul] == 6ul);
    CHECK(buffer[stream.count - 6ul] ==
          V9X_VI_PACKET3(V9X_VI_P3_EVENT_WRITE_EOP, 4ul));

    /* Refused: no vertices, a count not a whole number of triangles, an
     * unaligned vertex buffer. */
    base_tl(&draw);
    draw.vertex_count = 0ul;
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_INVALID_ARGUMENT);
    base_tl(&draw);
    draw.vertex_count = 4ul;
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_INVALID_ARGUMENT);
    base_tl(&draw);
    draw.vertex_low = 0x0f80b002ul;
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_INVALID_ARGUMENT);
}

/* With a depth buffer: Z16 in Carrizo's 1D depth tile mode 5, the size of
 * the target, LESS with writes; HiZ, HiS and compression forced off. */
static void test_tl_depth(void)
{
    struct v9x_vi_pm4 stream;
    struct v9x_vi_tl_draw draw;
    v9x_u32 at;
    v9x_u32 last;

    base_tl(&draw);
    draw.depth_low = 0x0f80d000ul;
    draw.depth_high = 0xf4ul;
    draw.depth_func = V9X_VI_ZFUNC_LESS;
    draw.depth_write = 1ul;
    v9x_vi_pm4_init(&stream, buffer, 2048ul);
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_OK);
    CHECK(walk(buffer, stream.count) != 0ul);

    /* The last write of each register is the one that holds: the depth
     * state comes after the shared no-depth state. */
    at = find_context(buffer, stream.count, 0xa00ful);
    CHECK(at != 0ul);
    CHECK(buffer[at - 2ul] ==
          V9X_VI_PACKET3(V9X_VI_P3_SET_CONTEXT_REG, 9ul));
    CHECK(buffer[at] == 0x00000021ul);         /* DB_DEPTH_INFO */
    CHECK(buffer[at + 1ul] == 0x0050a001ul);   /* DB_Z_INFO */
    CHECK(buffer[at + 2ul] == 0ul);            /* DB_STENCIL_INFO */
    CHECK(buffer[at + 3ul] == 0xf40f80d0ul);   /* DB_Z_READ_BASE */
    CHECK(buffer[at + 5ul] == 0xf40f80d0ul);   /* DB_Z_WRITE_BASE */
    CHECK(buffer[at + 7ul] == 0x00000807ul);   /* DB_DEPTH_SIZE */
    CHECK(buffer[at + 8ul] == 15ul);           /* DB_DEPTH_SLICE */
    /* Searches from the depth packet's header on. */
    last = at - 2ul;
    CHECK(find_context(buffer + last, stream.count - last, 0xa010ul) == 0ul);

    at = find_context(buffer + last, stream.count - last, 0xa200ul);
    CHECK(at != 0ul && buffer[last + at] == 0x00000016ul);
    at = find_context(buffer + last, stream.count - last, 0xa000ul);
    CHECK(at != 0ul && buffer[last + at] == 0x00000060ul);
    at = find_context(buffer + last, stream.count - last, 0xa003ul);
    CHECK(at != 0ul && buffer[last + at] == 0x0000002aul);
    at = find_context(buffer + last, stream.count - last, 0xa002ul);
    CHECK(at != 0ul && buffer[last + at] == 0ul);
    at = find_context(buffer + last, stream.count - last, 0xa005ul);
    CHECK(at != 0ul && buffer[last + at] == 0ul);

    /* Refused: an unaligned depth buffer, a function past ALWAYS. */
    base_tl(&draw);
    draw.depth_low = 0x0f80d080ul;
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_INVALID_ARGUMENT);
    base_tl(&draw);
    draw.depth_low = 0x0f80d000ul;
    draw.depth_func = 8ul;
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_INVALID_ARGUMENT);
}

/* With a texture: the PS's twelve user SGPRs are the T# (64x16 ARGB8888,
 * linear aligned, R, G, B, A selected from bytes 2, 1, 0, 3, 2D) and the
 * S# (clamp to the last texel, point or bilinear, LOD 0). */
static void test_tl_texture(void)
{
    static const v9x_u32 expected[12] = {
        0xf40f80e0ul,   /* T# 0: address 39:8 */
        0x00a00000ul,   /* 1: DATA_FORMAT 8_8_8_8, UNORM */
        0x0003c03ful,   /* 2: width 63, height 15 */
        0x90800f2eul,   /* 3: Z, Y, X, W; tiling index 8; IMG_2D */
        0x0007e000ul,   /* 4: pitch 63 */
        0ul, 0ul, 0ul,
        0x00000092ul,   /* S# 0: CLAMP_LAST_TEXEL in X, Y, Z */
        0ul,            /* 1: LOD 0 to 0 */
        0ul,            /* 2: point */
        0ul
    };
    struct v9x_vi_pm4 stream;
    struct v9x_vi_tl_draw draw;
    v9x_u32 at;

    base_tl(&draw);
    draw.texture_low = 0x0f80e000ul;
    draw.texture_high = 0xf4ul;
    draw.texture_width = 64ul;
    draw.texture_height = 16ul;
    draw.texture_pitch = 64ul;
    draw.texture_tile_mode_index = V9X_VI_TILE_LINEAR_ALIGNED;
    v9x_vi_pm4_init(&stream, buffer, 2048ul);
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_OK);
    CHECK(walk(buffer, stream.count) != 0ul);

    at = find_sh(buffer, stream.count, 0x2c07ul);
    CHECK(at < stream.count && buffer[at + 6ul] == (12ul << 1));
    at = find_sh(buffer, stream.count, 0x2c0cul);
    CHECK(at < stream.count);
    CHECK(buffer[at] == V9X_VI_PACKET3(V9X_VI_P3_SET_SH_REG, 12ul));
    CHECK(memcmp(buffer + at + 2, expected, sizeof(expected)) == 0);

    /* Bilinear: XY_MAG_FILTER and XY_MIN_FILTER 1 at 21:20 and 23:22. */
    draw.texture_bilinear = 1ul;
    v9x_vi_pm4_init(&stream, buffer, 2048ul);
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_OK);
    at = find_sh(buffer, stream.count, 0x2c0cul);
    CHECK(at < stream.count && buffer[at + 12ul] == 0x00500000ul);

    /* Refused: an unaligned texture, a zero or oversized width. */
    draw.texture_low = 0x0f80e010ul;
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_INVALID_ARGUMENT);
    draw.texture_low = 0x0f80e000ul;
    draw.texture_width = 0ul;
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_INVALID_ARGUMENT);
    draw.texture_width = 16385ul;
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_INVALID_ARGUMENT);
}

/* With probe addresses, each stage takes six user SGPRs: the four it had,
 * then its probe address low and high. */
static void test_probe_rect(void)
{
    struct v9x_vi_pm4 stream;
    struct v9x_vi_solid_rect rect;
    v9x_u32 at;

    base_rect(&rect);
    rect.probe_vs_low = 0x0f80a000ul;
    rect.probe_ps_low = 0x0f80a004ul;
    rect.probe_high = 0xf4ul;
    v9x_vi_pm4_init(&stream, buffer, 2048ul);
    CHECK(v9x_vi_3d_solid_rect(&stream, &rect) == V9X_STATUS_OK);
    CHECK(walk(buffer, stream.count) != 0ul);

    /* SET_SH_REG from RSRC3_VS (2C46h): RSRC2 is the sixth value. */
    at = 0ul;
    while (at < stream.count &&
           !((buffer[at] & 0xff00ul) == (V9X_VI_P3_SET_SH_REG << 8) &&
             buffer[at + 1ul] == 0x46ul)) {
        at += buffer[at] == V9X_VI_RING_NOP ? 1ul :
              ((buffer[at] >> 16) & 0x3ffful) + 2ul;
    }
    CHECK(at < stream.count);
    CHECK(buffer[at + 7ul] == (6ul << 1));
    /* USER_DATA_VS: six values, the probe address last. */
    at += ((buffer[at] >> 16) & 0x3ffful) + 2ul;
    CHECK(buffer[at] == V9X_VI_PACKET3(V9X_VI_P3_SET_SH_REG, 6ul));
    CHECK(buffer[at + 1ul] == 0x4cul);
    CHECK(buffer[at + 6ul] == 0x0f80a000ul);
    CHECK(buffer[at + 7ul] == 0xf4ul);
    /* RSRC3_PS (2C07h): RSRC2 the fifth value; then USER_DATA_PS. */
    at += ((buffer[at] >> 16) & 0x3ffful) + 2ul;
    CHECK(buffer[at + 1ul] == 0x07ul);
    CHECK(buffer[at + 6ul] == (6ul << 1));
    at += ((buffer[at] >> 16) & 0x3ffful) + 2ul;
    CHECK(buffer[at] == V9X_VI_PACKET3(V9X_VI_P3_SET_SH_REG, 6ul));
    CHECK(buffer[at + 1ul] == 0x0cul);
    CHECK(buffer[at + 2ul] == 0x3f800000ul);
    CHECK(buffer[at + 6ul] == 0x0f80a004ul);
    CHECK(buffer[at + 7ul] == 0xf4ul);
}

/* The engine configuration list: Linux's tables in order, then gpu_init's
 * own writes, with the values Linux computes for Carrizo. */
static struct v9x_vi_reg_seq ops[V9X_VI_GPU_INIT_OPS_MAX];

static v9x_u32 find_op(v9x_u32 count, v9x_u32 offset, v9x_u32 from)
{
    v9x_u32 index;

    for (index = from; index < count; ++index) {
        if (ops[index].offset == offset) {
            return index;
        }
    }
    return count;
}

static void test_gpu_init(void)
{
    v9x_u32 count;
    v9x_u32 at;
    v9x_u32 vmid;

    count = v9x_vi_gpu_init_ops(ops, V9X_VI_GPU_INIT_OPS_MAX);
    /* 75 + 12 + 8 table entries, GRBM_CNTL, two address configs, 28 tile
     * and 15 macrotile modes, 16 VMIDs of select and four SH_MEM writes,
     * the select back, the broadcast and the FIFO sizes. */
    CHECK(count == 224ul);
    CHECK(v9x_vi_gpu_init_ops(ops, 100ul) == 0ul);

    /* The first entry is cz_mgcg_cgcg_init's RLC_CGTT_MGCG_OVERRIDE. */
    CHECK(ops[0].clear == 0xfffffffful && ops[0].set == 0xfffffffful);
    /* Golden settings' RLC_CGCG_CGLS_CTRL is a read-modify-write. */
    at = find_op(count, 0x3b124ul, 75ul);
    CHECK(at < count && ops[at].clear == 3ul && ops[at].set == 0x3cul);

    at = find_op(count, V9X_VI_GRBM_CNTL, 0ul);
    CHECK(at < count && ops[at].clear == 0xfful && ops[at].set == 0xfful);
    at = find_op(count, V9X_VI_HDP_ADDR_CONFIG, 0ul);
    CHECK(at < count && ops[at].set == 0x22010001ul);

    /* Tile modes: 0 and 8 as Linux's macros give them, 7 and 12 skipped;
     * macrotile 7 skipped. */
    at = find_op(count, V9X_VI_GB_TILE_MODE0, 0ul);
    CHECK(at < count && ops[at].set == 0x00800010ul);
    at = find_op(count, V9X_VI_GB_TILE_MODE0 + 8ul * 4ul, 0ul);
    CHECK(at < count && ops[at].set == 0x00000004ul);
    CHECK(find_op(count, V9X_VI_GB_TILE_MODE0 + 7ul * 4ul, 0ul) == count);
    CHECK(find_op(count, V9X_VI_GB_TILE_MODE0 + 12ul * 4ul, 0ul) == count);
    CHECK(find_op(count, V9X_VI_GB_MACROTILE_MODE0 + 7ul * 4ul, 0ul) ==
          count);

    /* SH_MEM_CONFIG: UC with unaligned access for VMID 0, NC for the
     * others, each behind its SRBM_GFX_CNTL select. */
    at = 0ul;
    for (vmid = 0ul; vmid < 16ul; ++vmid) {
        at = find_op(count, V9X_VI_SRBM_GFX_CNTL, at);
        CHECK(at < count && ops[at].set == (vmid << 4));
        at = find_op(count, V9X_VI_SH_MEM_CONFIG, at);
        CHECK(at < count && ops[at].set == (vmid == 0ul ? 0x378ul : 0x138ul));
    }
    at = find_op(count, V9X_VI_SRBM_GFX_CNTL, at);
    CHECK(at < count && ops[at].set == 0ul);

    /* The end: broadcast, then Carrizo's FIFO sizes 20h, 100h, 30h, 130h. */
    CHECK(ops[count - 2ul].offset == V9X_VI_GRBM_GFX_INDEX);
    CHECK(ops[count - 2ul].set == 0xe0000000ul);
    CHECK(ops[count - 1ul].offset == V9X_VI_PA_SC_FIFO_SIZE);
    CHECK(ops[count - 1ul].set == 0x98184020ul);
}

unsigned int v9x_run_vi_3d_tests(void)
{
    failures = 0u;
    test_gpu_init();
    test_encodings();
    test_shaders();
    test_clear_state();
    test_start();
    test_solid_rect();
    test_probe_shaders();
    test_probe_rect();
    test_tl_shaders();
    test_tl_draw();
    test_tl_depth();
    test_texture_shaders();
    test_tl_texture();
    return failures;
}
