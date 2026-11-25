/*
 * The GCN 3 one-pixel model (gcn_sim.c) held to shaders that already draw
 * right on Carrizo, Mullins and the Oland: the Direct3D pixel shaders
 * vi_shader.c builds, with the results worked out here in C. Then each
 * instruction pixel shader 1.1 adds, alone (docs\plans\ati-gcn-ps11.md
 * step 1).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gcn_sim.h"
#include "velocity9x/vi_shader.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* FP16 rounded toward zero keeps 11 bits: 1/1024 relative is enough. */
#define NEAR(a, b) (fabs((double)(a) - (double)(b)) <= 0.002)

static const float texel_value[4] = { 0.5f, 0.25f, 1.0f, 0.75f };
static float sampled_at[4];
static v9x_u32 samples;

static void sample(const v9x_u32 *t_sharp, const v9x_u32 *s_sharp,
                   const float *address, v9x_u32 dmask, float rgba[4])
{
    v9x_u32 chan;

    (void)t_sharp;
    (void)s_sharp;
    (void)dmask;
    ++samples;
    for (chan = 0ul; chan < 4ul; ++chan) {
        sampled_at[chan] = address[chan];
        rgba[chan] = texel_value[chan];
    }
}

static v9x_u32 bits(float value)
{
    union {
        v9x_u32 u;
        float f;
    } c;

    c.f = value;
    return c.u;
}

/* attribute 0 the diffuse, 1 (u, v, 0, 1), 2 the specular and fog. */
static void inputs(struct v9x_gcn_sim *sim, float fog)
{
    v9x_gcn_sim_reset(sim);
    sim->attr[0][0] = 0.2f;
    sim->attr[0][1] = 0.4f;
    sim->attr[0][2] = 0.6f;
    sim->attr[0][3] = 0.8f;
    sim->attr[1][0] = 0.125f;
    sim->attr[1][1] = 0.625f;
    sim->attr[1][2] = 0.0f;
    sim->attr[1][3] = 1.0f;
    sim->attr[2][0] = 0.1f;
    sim->attr[2][1] = 0.05f;
    sim->attr[2][2] = 0.0f;
    sim->attr[2][3] = fog;
    sim->sample = sample;
    samples = 0ul;
}

static int run(struct v9x_gcn_sim *sim, const v9x_u32 *code, v9x_u32 count)
{
    int ok = v9x_gcn_sim_run(sim, code, count);

    if (!ok) {
        printf("  model stopped at word %lu (%08lx): %s\n",
               (unsigned long)sim->error_at,
               (unsigned long)(sim->error_at < count ? code[sim->error_at] : 0ul),
               sim->error);
    }
    return ok;
}

static void test_fixed_shaders(void)
{
    struct v9x_gcn_sim sim;
    v9x_u32 code[V9X_VI_D3D_PS_DWORDS_MAX];
    v9x_u32 count;
    v9x_u32 chan;

    /* Gouraud: parameter 0 exported. */
    count = v9x_vi_shader_d3d_ps(V9X_VI_PS_COLOUR, 0, code);
    inputs(&sim, 1.0f);
    CHECK(run(&sim, code, count));
    CHECK(sim.exported && sim.done && !sim.killed);
    for (chan = 0ul; chan < 4ul; ++chan) {
        CHECK(NEAR(sim.mrt0[chan], sim.attr[0][chan]));
    }

    /* Replace: the texel, sampled at (u, v). */
    count = v9x_vi_shader_d3d_ps(V9X_VI_PS_TEXTURE, 0, code);
    inputs(&sim, 1.0f);
    sim.s[0] = 0x11111111ul;
    CHECK(run(&sim, code, count));
    CHECK(samples == 1ul);
    CHECK(sampled_at[0] == 0.125f && sampled_at[1] == 0.625f);
    for (chan = 0ul; chan < 4ul; ++chan) {
        CHECK(NEAR(sim.mrt0[chan], texel_value[chan]));
    }
    /* EXEC back to the one pixel after whole-quad sampling. */
    CHECK((sim.exec[0] & 0xful) == 1ul);

    /* Modulate with specular: texel x diffuse, plus specular on B G R. */
    count = v9x_vi_shader_d3d_ps(V9X_VI_PS_MODULATE, 1, code);
    inputs(&sim, 1.0f);
    CHECK(run(&sim, code, count));
    for (chan = 0ul; chan < 4ul; ++chan) {
        float expect = texel_value[chan] * sim.attr[0][chan] +
                       (chan < 3ul ? sim.attr[2][chan] : 0.0f);

        CHECK(NEAR(sim.mrt0[chan], expect));
    }
}

static void test_built_shaders(void)
{
    struct v9x_gcn_sim sim;
    struct v9x_vi_d3d_ps ps;
    v9x_u32 code[V9X_VI_D3D_PS_DWORDS_MAX];
    v9x_u32 count;
    v9x_u32 chan;
    const float fog_rgb[3] = { 1.0f, 0.5f, 0.0f };

    /* Modulate, fogged at f = 0.25 towards fog_rgb, alpha test GE 0.5. */
    memset(&ps, 0, sizeof(ps));
    ps.op = V9X_VI_PS_MODULATE;
    ps.fog = 1ul;
    for (chan = 0ul; chan < 3ul; ++chan) {
        ps.fog_colour[chan] = bits(fog_rgb[chan]);
    }
    ps.alpha_test = 1ul;
    ps.alpha_func = V9X_VI_VOPC_CMPX_GE_F32;
    ps.alpha_ref = bits(0.5f);
    count = v9x_vi_shader_d3d_ps_built(&ps, code);
    CHECK(count != 0ul && count <= V9X_VI_D3D_PS_DWORDS_MAX);
    inputs(&sim, 0.25f);
    CHECK(run(&sim, code, count));
    CHECK(!sim.killed && sim.exported);
    for (chan = 0ul; chan < 3ul; ++chan) {
        float colour = texel_value[chan] * sim.attr[0][chan];
        float expect = fog_rgb[chan] + 0.25f * (colour - fog_rgb[chan]);

        CHECK(NEAR(sim.mrt0[chan], expect));
    }
    /* Alpha 0.75 x 0.8 = 0.6 passes GE 0.5; against 0.7 it is killed and
     * the wave ends through exp null. */
    ps.alpha_ref = bits(0.7f);
    count = v9x_vi_shader_d3d_ps_built(&ps, code);
    inputs(&sim, 0.25f);
    CHECK(run(&sim, code, count));
    CHECK(sim.killed && !sim.exported && sim.done);

    /* Modulate with specular under fog: Direct3D saturates the specular
     * add before fog. Blue is 1.0 x 0.6 + 0.7 = 1.3, so 1.0 fogged at
     * f = 0.25 towards 0 is 0.25, not 0.325 (V9XD32 T19 FfSpecFog, too
     * green on the Oland: docs\issues\2026-11-17-t19-ffspecfog-mismatch-
     * oland-gt218.md). */
    memset(&ps, 0, sizeof(ps));
    ps.op = V9X_VI_PS_MODULATE;
    ps.specular = 1ul;
    ps.fog = 1ul;
    for (chan = 0ul; chan < 3ul; ++chan) {
        ps.fog_colour[chan] = bits(fog_rgb[chan]);
    }
    count = v9x_vi_shader_d3d_ps_built(&ps, code);
    CHECK(count != 0ul && count <= V9X_VI_D3D_PS_DWORDS_MAX);
    inputs(&sim, 0.25f);
    sim.attr[2][2] = 0.7f;
    CHECK(run(&sim, code, count));
    for (chan = 0ul; chan < 3ul; ++chan) {
        float colour = texel_value[chan] * sim.attr[0][chan] +
                       sim.attr[2][chan];
        float expect;

        colour = colour > 1.0f ? 1.0f : colour;
        expect = fog_rgb[chan] + 0.25f * (colour - fog_rgb[chan]);
        CHECK(NEAR(sim.mrt0[chan], expect));
    }

    /* DX5 MODULATE on a texture with alpha keeps the texel's alpha: the
     * diffuse alpha, 0.3 here, does not enter it (docs\issues\2026-11-17-
     * oland-nature-tree-leaves-missing-most-frames.md, Nature's leaves).
     * Texel alpha 0.75 passes GE 0.5; multiplied it would be 0.225 and
     * killed. */
    memset(&ps, 0, sizeof(ps));
    ps.op = V9X_VI_PS_MODULATE;
    ps.alpha_texel = 1ul;
    ps.alpha_test = 1ul;
    ps.alpha_func = V9X_VI_VOPC_CMPX_GE_F32;
    ps.alpha_ref = bits(0.5f);
    count = v9x_vi_shader_d3d_ps_built(&ps, code);
    CHECK(count != 0ul && count <= V9X_VI_D3D_PS_DWORDS_MAX);
    inputs(&sim, 1.0f);
    sim.attr[0][3] = 0.3f;
    CHECK(run(&sim, code, count));
    CHECK(!sim.killed && sim.exported);
    for (chan = 0ul; chan < 3ul; ++chan) {
        CHECK(NEAR(sim.mrt0[chan], texel_value[chan] * sim.attr[0][chan]));
    }
    CHECK(NEAR(sim.mrt0[3], texel_value[3]));

    /* Colour key on the texel: the key equal to it kills, one far away
     * does not. */
    memset(&ps, 0, sizeof(ps));
    ps.op = V9X_VI_PS_TEXTURE;
    ps.key = 1ul;
    for (chan = 0ul; chan < 3ul; ++chan) {
        ps.key_colour[chan] = bits(texel_value[chan]);
    }
    ps.key_tolerance2 = bits(0.0001f);
    count = v9x_vi_shader_d3d_ps_built(&ps, code);
    inputs(&sim, 1.0f);
    CHECK(run(&sim, code, count));
    CHECK(sim.killed);
    ps.key_colour[0] = bits(0.0f);
    count = v9x_vi_shader_d3d_ps_built(&ps, code);
    inputs(&sim, 1.0f);
    CHECK(run(&sim, code, count));
    CHECK(!sim.killed && sim.exported);
}

/* Two units (step 4 of docs\plans\ati-gcn-ps11.md): each unit's texel
 * told apart by its T#'s first dword, which the table holds. */
static const float unit_texel[2][4] = {
    { 0.5f, 0.25f, 0.875f, 0.75f },
    { 0.375f, 0.625f, 0.5f, 0.25f }
};
static float unit_at[2][2];
static int unit_wrong;

static void unit_sample(const v9x_u32 *t_sharp, const v9x_u32 *s_sharp,
                        const float *address, v9x_u32 dmask, float rgba[4])
{
    v9x_u32 unit = t_sharp[0] - 0x7000ul;
    v9x_u32 chan;

    if (unit > 1ul || s_sharp[0] != 0x5000ul + unit || dmask != 0xful) {
        unit_wrong = 1;
        unit = 0ul;
    }
    unit_at[unit][0] = address[0];
    unit_at[unit][1] = address[1];
    for (chan = 0ul; chan < 4ul; ++chan) {
        rgba[chan] = unit_texel[unit][chan];
    }
}

/* The combine in C, as nv50_d3d.c's model test states it. */
static void combine_c(v9x_u32 color_op, v9x_u32 alpha_op, const float *t,
                      float *c)
{
    v9x_u32 chan;
    float in;

    for (chan = 0ul; chan < 3ul; ++chan) {
        in = c[chan];
        if (color_op == V9X_VI_COMBINE_COLOR_REPLACE) {
            c[chan] = t[chan];
        } else if (color_op == V9X_VI_COMBINE_COLOR_MODULATE) {
            c[chan] = in * t[chan];
        } else if (color_op == V9X_VI_COMBINE_COLOR_DECAL) {
            c[chan] = in + t[3] * (t[chan] - in);
        } else {
            c[chan] = in + t[chan] > 1.0f ? 1.0f : in + t[chan];
        }
    }
    if (alpha_op == V9X_VI_COMBINE_ALPHA_REPLACE) {
        c[3] = t[3];
    } else if (alpha_op == V9X_VI_COMBINE_ALPHA_MODULATE) {
        c[3] *= t[3];
    }
}

static void test_two_units(void)
{
    struct v9x_gcn_sim sim;
    struct v9x_vi_d3d_ps ps;
    v9x_u32 code[V9X_VI_D3D_PS_DWORDS_MAX];
    v9x_u32 table[V9X_VI_TWO_UNIT_TABLE_DWORDS];
    v9x_u32 count;
    v9x_u32 largest = 0ul;
    v9x_u32 combos = 0ul;
    v9x_u32 key;
    v9x_u32 chan;
    int bad = 0;

    memset(table, 0, sizeof(table));
    table[V9X_VI_TWO_UNIT_T0] = 0x7000ul;
    table[V9X_VI_TWO_UNIT_S0] = 0x5000ul;
    table[V9X_VI_TWO_UNIT_T1] = 0x7001ul;
    table[V9X_VI_TWO_UNIT_S1] = 0x5001ul;
    /* Every colour and alpha op on both units, half of them with the
     * specular added and every third fogged. */
    for (key = 0ul; key < 144ul; ++key) {
        float expect[4];
        float f = key % 3ul == 0ul ? 0.375f : 1.0f;
        const float fog_rgb[3] = { 0.25f, 0.5f, 1.0f };

        memset(&ps, 0, sizeof(ps));
        ps.two_units = 1ul;
        ps.color_op[0] = key % 4ul;
        ps.alpha_op[0] = (key / 4ul) % 3ul;
        ps.color_op[1] = (key / 12ul) % 4ul;
        ps.alpha_op[1] = (key / 48ul) % 3ul;
        ps.specular = key & 1ul;
        if (key % 3ul == 0ul) {
            ps.fog = 1ul;
            for (chan = 0ul; chan < 3ul; ++chan) {
                ps.fog_colour[chan] = bits(fog_rgb[chan]);
            }
        }
        count = v9x_vi_shader_d3d_ps_built(&ps, code);
        if (count == 0ul || count > V9X_VI_D3D_PS_DWORDS_MAX) {
            bad = 1;
            continue;
        }
        if (count > largest) {
            largest = count;
        }
        inputs(&sim, f);
        sim.attr[3][0] = 0.3125f;
        sim.attr[3][1] = 0.8125f;
        sim.attr[3][2] = 0.0f;
        sim.attr[3][3] = 1.0f;
        sim.sample = unit_sample;
        sim.memory = table;
        sim.memory_dwords = V9X_VI_TWO_UNIT_TABLE_DWORDS;
        sim.memory_low = 0x00400100ul;
        sim.memory_high = 0x2ul;
        sim.s[0] = 0x00400100ul;
        sim.s[1] = 0x2ul;
        unit_wrong = 0;
        if (!run(&sim, code, count) || unit_wrong || !sim.exported ||
            unit_at[0][0] != 0.125f || unit_at[0][1] != 0.625f ||
            unit_at[1][0] != 0.3125f || unit_at[1][1] != 0.8125f ||
            (sim.exec[0] & 0xful) != 1ul) {
            bad = 1;
            continue;
        }
        for (chan = 0ul; chan < 4ul; ++chan) {
            expect[chan] = sim.attr[0][chan];
        }
        combine_c(ps.color_op[0], ps.alpha_op[0], unit_texel[0], expect);
        combine_c(ps.color_op[1], ps.alpha_op[1], unit_texel[1], expect);
        for (chan = 0ul; chan < 3ul; ++chan) {
            if (ps.specular != 0ul) {
                expect[chan] += sim.attr[2][chan];
                expect[chan] = expect[chan] > 1.0f ? 1.0f : expect[chan];
            }
            if (ps.fog != 0ul) {
                expect[chan] = fog_rgb[chan] + f * (expect[chan] -
                                                   fog_rgb[chan]);
            }
        }
        for (chan = 0ul; chan < 4ul; ++chan) {
            if (!NEAR(sim.mrt0[chan], expect[chan])) {
                printf("  two units %lu: channel %lu %f, want %f\n",
                       (unsigned long)key, (unsigned long)chan,
                       (double)sim.mrt0[chan], (double)expect[chan]);
                bad = 1;
            }
        }
        ++combos;
    }
    CHECK(!bad);
    CHECK(combos == 144ul);
    printf("INFO gcn_sim: %lu two-unit combinations held to their "
           "arithmetic, the largest %lu words\n", (unsigned long)combos,
           (unsigned long)largest);
}

/* One VOP3 instruction then s_endpgm; v1-v3 hold a, b, c. */
static float one(v9x_u32 op, v9x_u32 mods, float a, float b, float c)
{
    struct v9x_gcn_sim sim;
    v9x_u32 code[3];

    v9x_gcn_sim_reset(&sim);
    sim.v[1] = bits(a);
    sim.v[2] = bits(b);
    sim.v[3] = bits(c);
    v9x_vi_vop3(op, 5ul, V9X_VI_VGPR(1), V9X_VI_VGPR(2), V9X_VI_VGPR(3),
                mods, code);
    code[2] = V9X_VI_S_ENDPGM;
    CHECK(run(&sim, code, 3ul));
    return v9x_gcn_sim_vgpr(&sim, 5ul);
}

static void test_ps11_instructions(void)
{
    struct v9x_gcn_sim sim;
    v9x_u32 code[8];
    v9x_u32 table[8];
    v9x_u32 index;
    float id;

    CHECK(one(V9X_VI_VOP3_MAD_F32, 0ul, 2.0f, 3.0f, 1.0f) == 7.0f);
    CHECK(one(V9X_VI_VOP3_MAD_F32, V9X_VI_NEG(2), 2.0f, 3.0f, 1.0f) == 5.0f);
    CHECK(one(V9X_VI_VOP3_MAD_F32, V9X_VI_OMOD_X2, 0.25f, 1.0f, 0.0f) == 0.5f);
    CHECK(one(V9X_VI_VOP3_MAD_F32, V9X_VI_OMOD_X4, 0.25f, 1.0f, 0.0f) == 1.0f);
    CHECK(one(V9X_VI_VOP3_MAD_F32, V9X_VI_OMOD_D2, 0.25f, 1.0f, 0.0f) == 0.125f);
    CHECK(one(V9X_VI_VOP3_MAD_F32, V9X_VI_CLAMP, 2.0f, 3.0f, 1.0f) == 1.0f);
    CHECK(one(V9X_VI_VOP3_MAD_F32, V9X_VI_CLAMP, -2.0f, 3.0f, 1.0f) == 0.0f);
    /* omod before clamp: 0.75 x 2 clamps to 1. */
    CHECK(one(V9X_VI_VOP3_MAD_F32, V9X_VI_OMOD_X2 | V9X_VI_CLAMP, 0.75f, 1.0f,
              0.0f) == 1.0f);
    CHECK(one(V9X_VI_VOP3_MAD_F32, V9X_VI_ABS(0) | V9X_VI_NEG(0), -2.0f, 3.0f,
              0.0f) == -6.0f);
    CHECK(one(V9X_VI_VOP3_FROM_VOP2(V9X_VI_VOP2_MIN_F32), 0ul, 2.0f, -1.0f,
              0.0f) == -1.0f);
    CHECK(one(V9X_VI_VOP3_FROM_VOP2(V9X_VI_VOP2_MAX_F32), 0ul, 2.0f, -1.0f,
              0.0f) == 2.0f);
    CHECK(one(V9X_VI_VOP3_FROM_VOP2(V9X_VI_VOP2_SUB_F32), 0ul, 2.0f, 0.5f,
              0.0f) == 1.5f);
    CHECK(one(V9X_VI_VOP3_FROM_VOP1(V9X_VI_VOP1_RCP_F32), 0ul, 4.0f, 0.0f,
              0.0f) == 0.25f);
    /* The cube face of (0.5, -2, 1): -y is major, face 3, ma -4, sc x, tc
     * -z (ac_llvm_build.c's build_cube_select). */
    id = one(V9X_VI_VOP3_CUBEID_F32, 0ul, 0.5f, -2.0f, 1.0f);
    CHECK(id == 3.0f);
    CHECK(one(V9X_VI_VOP3_CUBEMA_F32, 0ul, 0.5f, -2.0f, 1.0f) == -4.0f);
    CHECK(one(V9X_VI_VOP3_CUBESC_F32, 0ul, 0.5f, -2.0f, 1.0f) == 0.5f);
    CHECK(one(V9X_VI_VOP3_CUBETC_F32, 0ul, 0.5f, -2.0f, 1.0f) == -1.0f);
    CHECK(one(V9X_VI_VOP3_CUBEID_F32, 0ul, -3.0f, 1.0f, 2.0f) == 1.0f);
    CHECK(one(V9X_VI_VOP3_CUBEID_F32, 0ul, 1.0f, 1.0f, 2.0f) == 4.0f);

    /* v_cmp_gt_f32 into s[6:7], then v_cndmask_b32 on it. */
    v9x_gcn_sim_reset(&sim);
    sim.v[1] = bits(0.75f);
    sim.v[2] = bits(10.0f);
    sim.v[3] = bits(20.0f);
    v9x_vi_vop3(V9X_VI_VOPC_CMP_GT_F32, 6ul, V9X_VI_VGPR(1), 0xf0ul, 0ul,
                0ul, code);
    v9x_vi_vop3(V9X_VI_VOP3_FROM_VOP2(V9X_VI_VOP2_CNDMASK_B32), 5ul,
                V9X_VI_VGPR(2), V9X_VI_VGPR(3), 6ul, 0ul, code + 2);
    code[4] = V9X_VI_S_ENDPGM;
    CHECK(run(&sim, code, 5ul));
    CHECK(sim.s[6] == 1ul && v9x_gcn_sim_vgpr(&sim, 5ul) == 20.0f);
    sim.v[1] = bits(0.25f);
    CHECK(run(&sim, code, 5ul));
    CHECK(sim.s[6] == 0ul && v9x_gcn_sim_vgpr(&sim, 5ul) == 10.0f);

    /* s_load_dwordx4 s[8:11] from the table at s[0:1] + 16. */
    v9x_gcn_sim_reset(&sim);
    for (index = 0ul; index < 8ul; ++index) {
        table[index] = 0xa0000000ul + index;
    }
    sim.memory = table;
    sim.memory_dwords = 8ul;
    sim.memory_low = 0x00400000ul;
    sim.memory_high = 0x1ul;
    sim.s[0] = 0x00400000ul;
    sim.s[1] = 0x1ul;
    CHECK(v9x_vi_s_load(4ul, 8ul, 0ul, 16ul, code) == 2ul);
    code[2] = v9x_vi_s_waitcnt(V9X_VI_WAITCNT_ALL);
    code[3] = V9X_VI_S_ENDPGM;
    CHECK(run(&sim, code, 4ul));
    CHECK(sim.s[8] == 0xa0000004ul && sim.s[11] == 0xa0000007ul);
    /* Past the table: the model refuses. */
    CHECK(v9x_vi_s_load(8ul, 8ul, 0ul, 16ul, code) == 2ul);
    CHECK(!v9x_gcn_sim_run(&sim, code, 4ul));

    /* The constant bus: v_mad_f32 reading s8 twice runs; s8 and s9 is
     * refused, as is v_cndmask_b32 (VOP2, VCC) with an SGPR src0 and
     * VOP3 v_cndmask_b32 reading an SGPR beside its VCC. */
    v9x_gcn_sim_reset(&sim);
    sim.s[8] = bits(2.0f);
    sim.s[9] = bits(3.0f);
    v9x_vi_vop3(V9X_VI_VOP3_MAD_F32, 5ul, 8ul, 8ul, V9X_VI_VGPR(1), 0ul,
                code);
    code[2] = V9X_VI_S_ENDPGM;
    CHECK(run(&sim, code, 3ul));
    v9x_vi_vop3(V9X_VI_VOP3_MAD_F32, 5ul, 8ul, 9ul, V9X_VI_VGPR(1), 0ul,
                code);
    CHECK(!v9x_gcn_sim_run(&sim, code, 3ul) &&
          strcmp(sim.error, "constant bus") == 0);
    v9x_gcn_sim_reset(&sim);
    code[0] = v9x_vi_vop2(V9X_VI_VOP2_CNDMASK_B32, 5ul, 8ul, 1ul);
    code[1] = V9X_VI_S_ENDPGM;
    CHECK(!v9x_gcn_sim_run(&sim, code, 2ul));
    v9x_gcn_sim_reset(&sim);
    v9x_vi_vop3(V9X_VI_VOP3_FROM_VOP2(V9X_VI_VOP2_CNDMASK_B32), 5ul, 8ul,
                V9X_VI_VGPR(1), V9X_VCC, 0ul, code);
    code[2] = V9X_VI_S_ENDPGM;
    CHECK(!v9x_gcn_sim_run(&sim, code, 3ul));
}

unsigned int v9x_run_gcn_sim_tests(void)
{
    failures = 0u;
    test_fixed_shaders();
    test_built_shaders();
    test_ps11_instructions();
    test_two_units();
    return failures;
}
