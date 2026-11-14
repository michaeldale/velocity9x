/*
 * The Direct3D core's pixel shader state (src\display32\d3d\d3d_ps.h):
 * the shader table, the constants, the per-stage terms a shader reads,
 * and the wide vertex. Step 5b of docs\plans\haswell-ddi8-pixel-shaders.md.
 */
#include <stdio.h>
#include <string.h>
#include "../../src/display32/d3d/d3d_ps.h"

static unsigned int failures;
#define CHECK(e) do { if (!(e)) { ++failures; \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
} } while (0)

/* D3DFVF bits (d3d8types.h). */
#define FVF_XYZRHW   0x004ul
#define FVF_DIFFUSE  0x040ul
#define FVF_SPECULAR 0x080ul
#define FVF_TEX(n)   ((v9x_u32)(n) << 8)
/* D3DFVF_TEXCOORDSIZEn(set): 3 floats 1, 4 floats 2, 1 float 3. */
#define FVF_SIZE3(set) (1ul << (16 + 2 * (set)))
#define FVF_SIZE4(set) (2ul << (16 + 2 * (set)))
#define FVF_SIZE1(set) (3ul << (16 + 2 * (set)))

static v9x_u32 bits(float value)
{
    union {
        float value;
        v9x_u32 bits;
    } convert;

    convert.value = value;
    return convert.bits;
}

static void stage_tests(void)
{
    V9X_D3D_PS_STAGE stage;

    v9x_d3d_ps_stage_reset(&stage, 2u);
    CHECK(stage.texture == 0ul && stage.texcoord == 2ul &&
          stage.min_filter == 1ul && stage.mag_filter == 1ul &&
          stage.mip_filter == 0ul && stage.address_u == 1ul &&
          stage.address_v == 1ul && stage.bump[0] == 0.0f &&
          stage.lscale == 0.0f && stage.transform == 0ul);

    CHECK(v9x_d3d_ps_stage_state(&stage, V9X_D3D_PS_TSS_TEXTUREMAP, 7ul));
    CHECK(v9x_d3d_ps_stage_state(&stage, V9X_D3D_PS_TSS_BUMPENVMAT00,
                                 bits(0.25f)));
    CHECK(v9x_d3d_ps_stage_state(&stage, V9X_D3D_PS_TSS_BUMPENVMAT01,
                                 bits(-0.5f)));
    CHECK(v9x_d3d_ps_stage_state(&stage, V9X_D3D_PS_TSS_BUMPENVMAT10,
                                 bits(0.125f)));
    CHECK(v9x_d3d_ps_stage_state(&stage, V9X_D3D_PS_TSS_BUMPENVMAT11,
                                 bits(2.0f)));
    CHECK(v9x_d3d_ps_stage_state(&stage, V9X_D3D_PS_TSS_BUMPENVLSCALE,
                                 bits(0.75f)));
    CHECK(v9x_d3d_ps_stage_state(&stage, V9X_D3D_PS_TSS_BUMPENVLOFFSET,
                                 bits(0.1f)));
    CHECK(v9x_d3d_ps_stage_state(&stage, V9X_D3D_PS_TSS_TEXCOORDINDEX,
                                 0x00010003ul));
    CHECK(v9x_d3d_ps_stage_state(&stage, V9X_D3D_PS_TSS_ADDRESS, 3ul));
    CHECK(v9x_d3d_ps_stage_state(&stage, V9X_D3D_PS_TSS_ADDRESSV, 2ul));
    CHECK(v9x_d3d_ps_stage_state(&stage, V9X_D3D_PS_TSS_MINFILTER, 2ul));
    CHECK(v9x_d3d_ps_stage_state(&stage, V9X_D3D_PS_TSS_MAGFILTER, 2ul));
    CHECK(v9x_d3d_ps_stage_state(&stage, V9X_D3D_PS_TSS_MIPFILTER, 1ul));
    CHECK(v9x_d3d_ps_stage_state(&stage, V9X_D3D_PS_TSS_TRANSFORMFLAGS,
                                 0x102ul));
    CHECK(stage.texture == 7ul && stage.bump[0] == 0.25f &&
          stage.bump[1] == -0.5f && stage.bump[2] == 0.125f &&
          stage.bump[3] == 2.0f && stage.lscale == 0.75f &&
          stage.loffset == 0.1f && stage.texcoord == 0x00010003ul &&
          stage.address_u == 3ul && stage.address_v == 2ul &&
          stage.min_filter == 2ul && stage.mag_filter == 2ul &&
          stage.mip_filter == 1ul && stage.transform == 0x102ul);
    /* COLOROP and the like are the fixed-function path's, not read here. */
    CHECK(!v9x_d3d_ps_stage_state(&stage, 1ul, 4ul));
}

static void constant_tests(void)
{
    float c[8][4];
    v9x_u32 values[12];
    V9X_D3D_PS_STAGE stage[V9X_D3D_PS_STAGES];
    V9X_PS11_ENV env;
    v9x_u32 i;

    memset(c, 0, sizeof(c));
    for (i = 0ul; i < 12ul; ++i) {
        values[i] = bits(0.5f + (float)i);
    }
    /* c6 and c7 written; the third register (c8) is not kept. */
    v9x_d3d_ps_set_constants(c, 6ul, 3ul, values);
    CHECK(c[6][0] == 0.5f && c[6][3] == 3.5f && c[7][0] == 4.5f &&
          c[7][3] == 7.5f && c[5][3] == 0.0f);
    /* A first register past c7 writes nothing. */
    v9x_d3d_ps_set_constants(c, 9ul, 1ul, values);
    CHECK(c[0][0] == 0.0f);

    for (i = 0ul; i < V9X_D3D_PS_STAGES; ++i) {
        v9x_d3d_ps_stage_reset(&stage[i], i);
    }
    stage[1].bump[1] = -0.25f;
    stage[1].lscale = 0.5f;
    stage[1].loffset = 0.2f;
    stage[2].transform = V9X_D3D_PS_TTFF_PROJECTED | 3ul;
    v9x_d3d_ps_env(c, stage, 0x8ul, &env);
    CHECK(env.c[6][0] == 0.5f && env.c[7][3] == 7.5f);
    CHECK(env.bump[1][1] == -0.25f && env.luminance[1][0] == 0.5f &&
          env.luminance[1][1] == 0.2f);
    CHECK(env.cube == 0x8ul && env.projected == 0x4ul);
}

static void wide_tests(void)
{
    V9X_DP2_FVF layout;
    V9X_D3D_PS_STAGE stage[V9X_D3D_PS_STAGES];
    v9x_u32 vertex[16];
    float out[V9X_D3D_PS_WIDE_FLOATS];
    v9x_u32 i;

    /* XYZRHW, diffuse, specular, set 0 two floats, set 1 three, set 2
     * four, set 3 one: 4 + 1 + 1 + 2 + 3 + 4 + 1 = 16 dwords. */
    CHECK(v9x_dp2_fvf_layout(FVF_XYZRHW | FVF_DIFFUSE | FVF_SPECULAR |
                                 FVF_TEX(4) | FVF_SIZE3(1) | FVF_SIZE4(2) |
                                 FVF_SIZE1(3),
                             &layout));
    CHECK(layout.stride == 64ul);
    vertex[0] = bits(10.0f);
    vertex[1] = bits(20.0f);
    vertex[2] = bits(0.5f);
    vertex[3] = bits(0.25f);
    vertex[4] = 0x80ff4000ul;   /* A 80h, R FFh, G 40h, B 00h */
    vertex[5] = 0x00102030ul;
    for (i = 6ul; i < 16ul; ++i) {
        vertex[i] = bits(0.1f * (float)i);
    }
    for (i = 0ul; i < V9X_D3D_PS_STAGES; ++i) {
        v9x_d3d_ps_stage_reset(&stage[i], i);
    }
    /* Stage 1 reads set 2, stage 2 reads a set the vertex lacks. */
    stage[1].texcoord = 2ul;
    stage[2].texcoord = 0x00010006ul;
    v9x_d3d_ps_wide_vertex(&layout, stage, (const v9x_u8 *)vertex, out);
    CHECK(out[0] == 10.0f && out[1] == 20.0f && out[2] == 0.5f &&
          out[3] == 0.25f);
    CHECK(out[4] == 1.0f && bits(out[5]) == bits(64.0f / 255.0f) &&
          out[6] == 0.0f && bits(out[7]) == bits(128.0f / 255.0f));
    CHECK(bits(out[8]) == bits(16.0f / 255.0f) &&
          bits(out[9]) == bits(32.0f / 255.0f) &&
          bits(out[10]) == bits(48.0f / 255.0f) && out[11] == 0.0f);
    /* Stage 0: set 0's two floats (dwords 6, 7), then 0 and 1. */
    CHECK(bits(out[12]) == vertex[6] && bits(out[13]) == vertex[7] &&
          out[14] == 0.0f && out[15] == 1.0f);
    /* Stage 1: set 2's four (dwords 11-14). */
    CHECK(bits(out[16]) == vertex[11] && bits(out[17]) == vertex[12] &&
          bits(out[18]) == vertex[13] && bits(out[19]) == vertex[14]);
    /* Stage 2: no set 6. */
    CHECK(out[20] == 0.0f && out[21] == 0.0f && out[22] == 0.0f &&
          out[23] == 1.0f);
    /* Stage 3: set 3's one float (dword 15). */
    CHECK(bits(out[24]) == vertex[15] && out[25] == 0.0f &&
          out[26] == 0.0f && out[27] == 1.0f);

    /* No colours: diffuse opaque white, specular black with alpha 1, the
     * FF000000h d3d_dp2.c gives the fixed-function vertex: no fog. */
    CHECK(v9x_dp2_fvf_layout(FVF_XYZRHW | FVF_TEX(1), &layout));
    v9x_d3d_ps_wide_vertex(&layout, stage, (const v9x_u8 *)vertex, out);
    CHECK(out[4] == 1.0f && out[5] == 1.0f && out[6] == 1.0f &&
          out[7] == 1.0f && out[8] == 0.0f && out[10] == 0.0f &&
          out[11] == 1.0f);
}

/* ps.1.1: tex t0; mov r0, t0. */
static const v9x_u32 shader_ok[] = {
    0xffff0101ul,
    66ul, 0xb00f0000ul,
    1ul, 0x800f0000ul, 0xb0e40000ul,
    0x0000fffful
};
/* ps.1.4: refused. */
static const v9x_u32 shader_14[] = { 0xffff0104ul, 0x0000fffful };

static void table_tests(void)
{
    static V9X_D3D_PS_SHADER table[3];
    v9x_u32 serial = 0ul;
    int a;
    int b;
    const V9X_D3D_PS_SHADER *found;
    v9x_u32 first;

    memset(table, 0, sizeof(table));
    CHECK(v9x_d3d_ps_create(table, 3ul, &serial, &a, 5ul, shader_ok,
                            sizeof(shader_ok) / 4u) == V9X_PS11_OK);
    found = v9x_d3d_ps_find(table, 3ul, &a, 5ul);
    CHECK(found != 0 && found->serial != 0ul && found->program.samples == 1ul);
    first = found != 0 ? found->serial : 0ul;
    /* The same handle on another context is another shader. */
    CHECK(v9x_d3d_ps_find(table, 3ul, &b, 5ul) == 0);
    CHECK(v9x_d3d_ps_create(table, 3ul, &serial, &b, 5ul, shader_ok,
                            sizeof(shader_ok) / 4u) == V9X_PS11_OK);
    found = v9x_d3d_ps_find(table, 3ul, &b, 5ul);
    CHECK(found != 0 && found->serial != first);
    /* Refused programs create nothing. */
    CHECK(v9x_d3d_ps_create(table, 3ul, &serial, &a, 6ul, shader_14, 2ul) ==
          V9X_PS11_VERSION);
    CHECK(v9x_d3d_ps_find(table, 3ul, &a, 6ul) == 0);
    /* Re-creating a live handle replaces it with a new serial. */
    CHECK(v9x_d3d_ps_create(table, 3ul, &serial, &a, 5ul, shader_ok,
                            sizeof(shader_ok) / 4u) == V9X_PS11_OK);
    found = v9x_d3d_ps_find(table, 3ul, &a, 5ul);
    CHECK(found != 0 && found->serial != first);
    CHECK(v9x_d3d_ps_create(table, 3ul, &serial, &a, 7ul, shader_ok,
                            sizeof(shader_ok) / 4u) == V9X_PS11_OK);
    /* Three live shaders fill three slots. */
    CHECK(v9x_d3d_ps_create(table, 3ul, &serial, &a, 8ul, shader_ok,
                            sizeof(shader_ok) / 4u) == V9X_D3D_PS_FULL);
    v9x_d3d_ps_delete(table, 3ul, &a, 7ul);
    CHECK(v9x_d3d_ps_find(table, 3ul, &a, 7ul) == 0);
    CHECK(v9x_d3d_ps_create(table, 3ul, &serial, &a, 8ul, shader_ok,
                            sizeof(shader_ok) / 4u) == V9X_PS11_OK);
    v9x_d3d_ps_forget(table, 3ul, &a);
    CHECK(v9x_d3d_ps_find(table, 3ul, &a, 5ul) == 0 &&
          v9x_d3d_ps_find(table, 3ul, &a, 8ul) == 0 &&
          v9x_d3d_ps_find(table, 3ul, &b, 5ul) != 0);
}

/* D3DTEXTUREOP and D3DTA (d3d8types.h). */
#define TOP_DISABLE    1ul
#define TOP_SELECT1    2ul
#define TOP_SELECT2    3ul
#define TOP_MODULATE   4ul
#define TOP_MODULATE2X 5ul
#define TOP_ADD        7ul
#define TOP_BLENDDIFFUSEALPHA 12ul
#define TOP_BUMPENVMAP 22ul
#define TOP_BUMPENVMAPLUMINANCE 23ul
#define TOP_DOT3       24ul
#define TA_DIFFUSE     0ul
#define TA_CURRENT     1ul
#define TA_TEXTURE     2ul
#define TA_TFACTOR     3ul
#define TA_COMPLEMENT  0x10ul

static void ff_stage(V9X_D3D_STATE_STAGE *s, v9x_u32 cop, v9x_u32 c1,
                     v9x_u32 c2, v9x_u32 aop, v9x_u32 a1, v9x_u32 a2)
{
    s->color_op = cop;
    s->color_arg1 = c1;
    s->color_arg2 = c2;
    s->alpha_op = aop;
    s->alpha_arg1 = a1;
    s->alpha_arg2 = a2;
}

/* Stages 0 and 1 through the generator's array form: `two` draws both. */
static v9x_u32 fixed2(const V9X_D3D_STATE_STAGE *s0,
                      const V9X_D3D_STATE_STAGE *s1, int two, int specular,
                      v9x_u32 tokens[V9X_D3D_PS_FIXED_DWORDS])
{
    V9X_D3D_STATE_STAGE stages[2];

    stages[0] = *s0;
    stages[1] = *s1;
    return v9x_d3d_ps_fixed(stages, two ? 2ul : 1ul, specular, tokens);
}

static int needs2(const V9X_D3D_STATE_STAGE *s0,
                  const V9X_D3D_STATE_STAGE *s1, int two)
{
    V9X_D3D_STATE_STAGE stages[2];

    stages[0] = *s0;
    stages[1] = *s1;
    return v9x_d3d_ps_fixed_needs(stages, two ? 2ul : 1ul);
}

/* Stage 0's texel and stage 1's, distinct in every channel. */
static void ff_sample(void *user, v9x_u32 stage, v9x_u32 count,
                      const float *coords, float rgba[4])
{
    (void)user;
    (void)count;
    (void)coords;
    if (stage == 0ul) {
        rgba[0] = 0.25f; rgba[1] = 0.5f; rgba[2] = 0.75f; rgba[3] = 0.5f;
    } else {
        rgba[0] = 0.5f; rgba[1] = 0.125f; rgba[2] = 0.375f; rgba[3] = 0.25f;
    }
}

/* Generate, parse and run on one pixel: diffuse (0.5, 0.25, 1, 0.75),
 * specular (0.125, 0.25, 0.5, 1). 0 when nothing was generated or the
 * parser refused it. */
static int ff_run(const V9X_D3D_STATE_STAGE *s0,
                  const V9X_D3D_STATE_STAGE *s1, int two, int specular,
                  float out[4])
{
    v9x_u32 tokens[V9X_D3D_PS_FIXED_DWORDS];
    V9X_PS11_PROGRAM program;
    V9X_PS11_ENV env;
    V9X_PS11_PIXEL pixel;
    v9x_u32 dwords;
    v9x_u32 where;

    dwords = fixed2(s0, s1, two, specular, tokens);
    if (dwords == 0ul ||
        v9x_ps11_parse(tokens, dwords, &program, &where) != V9X_PS11_OK) {
        return 0;
    }
    memset(&env, 0, sizeof(env));
    env.cube = 1ul;
    memset(&pixel, 0, sizeof(pixel));
    pixel.v[0][0] = 0.5f; pixel.v[0][1] = 0.25f;
    pixel.v[0][2] = 1.0f; pixel.v[0][3] = 0.75f;
    pixel.v[1][0] = 0.125f; pixel.v[1][1] = 0.25f;
    pixel.v[1][2] = 0.5f; pixel.v[1][3] = 1.0f;
    pixel.tc[0][0] = 1.0f;
    pixel.tc[1][1] = 1.0f;
    return v9x_ps11_run(&program, &env, &pixel, ff_sample, 0, out);
}

static int ff_is(const float out[4], float r, float g, float b, float a)
{
    return out[0] == r && out[1] == g && out[2] == b && out[3] == a;
}

/*
 * Fixed-function stages as a ps.1.1 program (v9x_d3d_ps_fixed), judged by
 * the reference interpreter against D3D's stage arithmetic: each stage
 * saturated, CURRENT the diffuse colour on stage 0, specular added last.
 * UT2004's environment maps on the 4570 (boot 236, the d3d8 proxy): stage
 * 0 selects its cube's texel, stage 1 passes CURRENT.
 */
static void fixed_tests(void)
{
    V9X_D3D_STATE_STAGE s0;
    V9X_D3D_STATE_STAGE s1;
    v9x_u32 tokens[V9X_D3D_PS_FIXED_DWORDS];
    float out[4];

    /* UT2004's: the texel, colour and alpha; stage 1 passing. */
    ff_stage(&s0, TOP_SELECT1, TA_TEXTURE, TA_DIFFUSE,
             TOP_SELECT1, TA_TEXTURE, TA_DIFFUSE);
    ff_stage(&s1, TOP_SELECT2, TA_DIFFUSE, TA_CURRENT,
             TOP_SELECT2, TA_DIFFUSE, TA_CURRENT);
    CHECK(ff_run(&s0, &s1, 1, 0, out) &&
          ff_is(out, 0.25f, 0.5f, 0.75f, 0.5f));
    /* The same with stage 1 disabled, and with the vertex's alpha. */
    s1.color_op = TOP_DISABLE;
    s0.alpha_op = TOP_SELECT2;
    CHECK(ff_run(&s0, &s1, 1, 0, out) &&
          ff_is(out, 0.25f, 0.5f, 0.75f, 0.75f));
    /* Alpha disabled under an enabled colour passes the vertex's. */
    s0.alpha_op = TOP_DISABLE;
    CHECK(ff_run(&s0, &s1, 1, 0, out) &&
          ff_is(out, 0.25f, 0.5f, 0.75f, 0.75f));

    /* MODULATE2X saturated per channel: 2 x 0.75 x 1 clamps at one. */
    ff_stage(&s0, TOP_MODULATE2X, TA_TEXTURE, TA_DIFFUSE,
             TOP_MODULATE, TA_TEXTURE, TA_DIFFUSE);
    CHECK(ff_run(&s0, &s1, 1, 0, out) &&
          ff_is(out, 0.25f, 0.25f, 1.0f, 0.375f));

    /* Two stages: stage 1 adds its texel to stage 0's result, saturated;
     * alpha from stage 0. */
    ff_stage(&s0, TOP_SELECT1, TA_TEXTURE, TA_DIFFUSE,
             TOP_SELECT1, TA_TEXTURE, TA_DIFFUSE);
    ff_stage(&s1, TOP_ADD, TA_TEXTURE, TA_CURRENT,
             TOP_SELECT2, TA_TEXTURE, TA_CURRENT);
    CHECK(ff_run(&s0, &s1, 1, 0, out) &&
          ff_is(out, 0.75f, 0.625f, 1.0f, 0.5f));
    /* Without a second stage on the engine, stage 1 is not drawn. */
    CHECK(ff_run(&s0, &s1, 0, 0, out) &&
          ff_is(out, 0.25f, 0.5f, 0.75f, 0.5f));

    /* Specular added after the stages, colour only, saturated. */
    s1.color_op = TOP_DISABLE;
    CHECK(ff_run(&s0, &s1, 1, 1, out) &&
          ff_is(out, 0.375f, 0.75f, 1.0f, 0.5f));

    /* Stage 0's colour disabled: the vertex colour, nothing sampled. */
    s0.color_op = TOP_DISABLE;
    CHECK(ff_run(&s0, &s1, 1, 0, out) &&
          ff_is(out, 0.5f, 0.25f, 1.0f, 0.75f));

    /* Not expressible here: a complemented argument, an op without a
     * translation. */
    ff_stage(&s0, TOP_MODULATE, TA_TEXTURE | TA_COMPLEMENT, TA_DIFFUSE,
             TOP_SELECT1, TA_TEXTURE, TA_DIFFUSE);
    CHECK(fixed2(&s0, &s1, 1, 0, tokens) == 0ul);
    ff_stage(&s0, TOP_BLENDDIFFUSEALPHA, TA_TEXTURE, TA_DIFFUSE,
             TOP_SELECT1, TA_TEXTURE, TA_DIFFUSE);
    CHECK(fixed2(&s0, &s1, 1, 0, tokens) == 0ul);
}

/* Stage 0's texel a (du, dv) pair; stage 1's its own coordinates, so a
 * perturbed lookup shows where it read. */
static void ffb_sample(void *user, v9x_u32 stage, v9x_u32 count,
                       const float *coords, float rgba[4])
{
    (void)user;
    (void)count;
    if (stage == 0ul) {
        rgba[0] = 0.5f; rgba[1] = 0.25f; rgba[2] = 0.0f; rgba[3] = 1.0f;
    } else {
        rgba[0] = coords[0]; rgba[1] = coords[1];
        rgba[2] = 0.5f; rgba[3] = 1.0f;
    }
}

/* As ffb_sample, the bump texel carrying a luminance of 0.75 third. */
static void ffl_sample(void *user, v9x_u32 stage, v9x_u32 count,
                       const float *coords, float rgba[4])
{
    ffb_sample(user, stage, count, coords, rgba);
    if (stage == 0ul) {
        rgba[2] = 0.75f;
    }
}

/* Three stages: the base texel, a (du, dv) pair, and stage 2's own
 * coordinates. */
static void ff3_sample(void *user, v9x_u32 stage, v9x_u32 count,
                       const float *coords, float rgba[4])
{
    if (stage == 0ul) {
        ff_sample(user, stage, count, coords, rgba);
    } else {
        ffb_sample(user, stage == 1ul ? 0ul : 1ul, count, coords, rgba);
    }
}

/* As ff_run, with the environment a fixed-function draw is given
 * (v9x_d3d_ps_fixed_env): the texture factor, and `stages`' bump terms.
 * Stage 1's coordinates are (0, 0.5). */
static int ffe_run(const V9X_D3D_STATE_STAGE *s0,
                   const V9X_D3D_STATE_STAGE *s1,
                   const V9X_D3D_PS_STAGE stages[V9X_D3D_PS_STAGES],
                   v9x_u32 factor, v9x_ps11_sample_fn sample, float out[4])
{
    v9x_u32 tokens[V9X_D3D_PS_FIXED_DWORDS];
    V9X_PS11_PROGRAM program;
    V9X_PS11_ENV env;
    V9X_PS11_PIXEL pixel;
    float none[8][4];
    v9x_u32 dwords;
    v9x_u32 where;

    dwords = fixed2(s0, s1, 1, 0, tokens);
    if (dwords == 0ul ||
        v9x_ps11_parse(tokens, dwords, &program, &where) != V9X_PS11_OK) {
        return 0;
    }
    memset(none, 0, sizeof(none));
    v9x_d3d_ps_env(none, stages, 0ul, &env);
    env.unbound = 0ul;
    v9x_d3d_ps_fixed_env(&program, stages, factor, &env);
    memset(&pixel, 0, sizeof(pixel));
    pixel.v[0][0] = 0.5f; pixel.v[0][1] = 0.25f;
    pixel.v[0][2] = 1.0f; pixel.v[0][3] = 0.75f;
    pixel.tc[1][1] = 0.5f;
    return v9x_ps11_run(&program, &env, &pixel, sample, 0, out);
}

/*
 * DOT3, environment bump mapping and the texture factor as fixed-function
 * stage ops, through the same generator. DOT3 reads both arguments as
 * signed (2x - 1), saturates the sum and writes the colour channels only;
 * a BUMPENVMAP stage passes CURRENT and moves the next stage's lookup by
 * its own matrix times its texel's (du, dv).
 */
static void fixed_bump_tests(void)
{
    V9X_D3D_STATE_STAGE s0;
    V9X_D3D_STATE_STAGE s1;
    V9X_D3D_PS_STAGE stages[V9X_D3D_PS_STAGES];
    v9x_u32 tokens[V9X_D3D_PS_FIXED_DWORDS];
    float out[4];
    v9x_u32 s;

    for (s = 0ul; s < V9X_D3D_PS_STAGES; ++s) {
        v9x_d3d_ps_stage_reset(&stages[s], s);
    }

    /* DOT3 of the texel and the vertex colour: (-0.5, 0, 0.5) .
     * (0, -0.5, 1) is 0.5 in every colour channel; alpha the vertex's. */
    ff_stage(&s0, TOP_DOT3, TA_TEXTURE, TA_DIFFUSE,
             TOP_SELECT2, TA_TEXTURE, TA_DIFFUSE);
    ff_stage(&s1, TOP_DISABLE, TA_TEXTURE, TA_CURRENT,
             TOP_DISABLE, TA_TEXTURE, TA_CURRENT);
    CHECK(ff_run(&s0, &s1, 1, 0, out) && ff_is(out, 0.5f, 0.5f, 0.5f, 0.75f));
    CHECK(needs2(&s0, &s1,1));

    /* On stage 1 against CURRENT, a negative sum saturating to zero:
     * (0, -0.75, -0.25) . (-0.5, 0, 0.5) is -0.125. */
    ff_stage(&s0, TOP_SELECT1, TA_TEXTURE, TA_DIFFUSE,
             TOP_SELECT1, TA_TEXTURE, TA_DIFFUSE);
    ff_stage(&s1, TOP_DOT3, TA_TEXTURE, TA_CURRENT,
             TOP_SELECT2, TA_TEXTURE, TA_CURRENT);
    CHECK(ff_run(&s0, &s1, 1, 0, out) && ff_is(out, 0.0f, 0.0f, 0.0f, 0.5f));
    CHECK(needs2(&s0, &s1,1));
    /* Stage 1 is not drawn on a one-stage engine, so nothing needs it. */
    CHECK(!needs2(&s0, &s1,0));

    /* DOT3 against the texture factor, the light vector of the usual
     * setup: factor (0, 128, 255) is (-1, 1/255, 1), the sum exactly 1. */
    ff_stage(&s0, TOP_DOT3, TA_TEXTURE, TA_TFACTOR,
             TOP_SELECT2, TA_TEXTURE, TA_DIFFUSE);
    s1.color_op = TOP_DISABLE;
    CHECK(ffe_run(&s0, &s1, stages, 0xff0080fful, ff_sample, out) &&
          ff_is(out, 1.0f, 1.0f, 1.0f, 0.75f));
    /* And the factor unsigned in an ordinary combine: texel times
     * (1, 0, 1), alpha the factor's 0. */
    ff_stage(&s0, TOP_MODULATE, TA_TEXTURE, TA_TFACTOR,
             TOP_SELECT2, TA_TEXTURE, TA_TFACTOR);
    CHECK(ffe_run(&s0, &s1, stages, 0x00ff00fful, ff_sample, out) &&
          ff_is(out, 0.25f, 0.0f, 0.75f, 0.0f));
    /* A factor alone is no reason to leave the fixed-function kernels. */
    CHECK(!needs2(&s0, &s1,1));

    /* DOT3 as an alpha op has no 1.1 instruction (dp3 cannot write alpha
     * alone). */
    ff_stage(&s0, TOP_SELECT1, TA_TEXTURE, TA_DIFFUSE,
             TOP_DOT3, TA_TEXTURE, TA_DIFFUSE);
    CHECK(fixed2(&s0, &s1, 1, 0, tokens) == 0ul);

    /* Environment bump: stage 0's matrix (0.5, 0, 0, 0.5) times its texel
     * (0.5, 0.25) moves stage 1's (0, 0.5) to (0.25, 0.625). The matrix
     * is the bump stage's; texbem reads the sampled stage's, which
     * v9x_d3d_ps_fixed_env fills from it. */
    stages[0].bump[0] = 0.5f;
    stages[0].bump[3] = 0.5f;
    stages[1].bump[0] = 8.0f;
    stages[1].bump[3] = 8.0f;
    ff_stage(&s0, TOP_BUMPENVMAP, TA_TEXTURE, TA_CURRENT,
             TOP_SELECT1, TA_TEXTURE, TA_CURRENT);
    ff_stage(&s1, TOP_SELECT1, TA_TEXTURE, TA_CURRENT,
             TOP_SELECT1, TA_TEXTURE, TA_CURRENT);
    CHECK(ffe_run(&s0, &s1, stages, 0ul, ffb_sample, out) &&
          ff_is(out, 0.25f, 0.625f, 0.5f, 1.0f));
    CHECK(needs2(&s0, &s1,1));
    /* The bump stage itself passes the vertex colour: stage 1 adding its
     * texel shows it under the sum, alpha passed through. */
    ff_stage(&s1, TOP_ADD, TA_TEXTURE, TA_CURRENT,
             TOP_SELECT2, TA_TEXTURE, TA_CURRENT);
    CHECK(ffe_run(&s0, &s1, stages, 0ul, ffb_sample, out) &&
          ff_is(out, 0.75f, 0.875f, 1.0f, 0.75f));
    /* With no stage after it there is nothing to move: the vertex colour. */
    s1.color_op = TOP_DISABLE;
    CHECK(ffe_run(&s0, &s1, stages, 0ul, ffb_sample, out) &&
          ff_is(out, 0.5f, 0.25f, 1.0f, 0.75f));

    /* Three stages as 3DMark 2001 SE's bump test sets them (the 4570,
     * boot 247): the base map, the bump map, the environment times
     * CURRENT. Stage 1's matrix moves stage 2's (0, 0.5) to (0.25,
     * 0.625); stage 2's texel is those coordinates, times the base
     * texel (0.25, 0.5, 0.75, 0.5). */
    {
        V9X_D3D_STATE_STAGE three[3];
        V9X_PS11_PROGRAM program;
        V9X_PS11_ENV env;
        V9X_PS11_PIXEL pixel;
        float none[8][4];
        v9x_u32 dwords;
        v9x_u32 where;

        ff_stage(&three[0], TOP_SELECT1, TA_TEXTURE, TA_DIFFUSE,
                 TOP_SELECT1, TA_TEXTURE, TA_DIFFUSE);
        ff_stage(&three[1], TOP_BUMPENVMAP, TA_TEXTURE, TA_CURRENT,
                 TOP_MODULATE, TA_TEXTURE, TA_CURRENT);
        ff_stage(&three[2], TOP_MODULATE, TA_TEXTURE, TA_CURRENT,
                 TOP_SELECT2, TA_TEXTURE, TA_CURRENT);
        stages[0].bump[0] = 8.0f;
        stages[0].bump[3] = 8.0f;
        stages[1].bump[0] = 0.5f;
        stages[1].bump[3] = 0.5f;
        CHECK(v9x_d3d_ps_fixed_needs(three, 3ul));
        dwords = v9x_d3d_ps_fixed(three, 3ul, 0, tokens);
        CHECK(dwords != 0ul &&
              v9x_ps11_parse(tokens, dwords, &program, &where) ==
                  V9X_PS11_OK);
        memset(none, 0, sizeof(none));
        v9x_d3d_ps_env(none, stages, 0ul, &env);
        env.unbound = 0ul;
        v9x_d3d_ps_fixed_env(&program, stages, 0ul, &env);
        memset(&pixel, 0, sizeof(pixel));
        pixel.v[0][3] = 1.0f;
        pixel.tc[2][1] = 0.5f;
        CHECK(v9x_ps11_run(&program, &env, &pixel, ff3_sample, 0, out) &&
              ff_is(out, 0.0625f, 0.3125f, 0.375f, 0.5f));
        /* Two stages only: the bump stage has nothing to move, and the
         * base texel passes. */
        dwords = v9x_d3d_ps_fixed(three, 2ul, 0, tokens);
        CHECK(dwords != 0ul &&
              v9x_ps11_parse(tokens, dwords, &program, &where) ==
                  V9X_PS11_OK &&
              v9x_ps11_run(&program, &env, &pixel, ff3_sample, 0, out) &&
              ff_is(out, 0.25f, 0.5f, 0.75f, 0.5f));
        /* A bump stage moving a bump stage is not drawn. */
        three[2].color_op = TOP_BUMPENVMAP;
        CHECK(v9x_d3d_ps_fixed(three, 3ul, 0, tokens) == 0ul);
    }

    /* The luminance form: as BUMPENVMAP, and stage 1's colour times the
     * bump texel's third channel scaled and offset by the bump stage's
     * own terms (0.75 x 0.5 + 0.25 is 0.625), alpha left alone. Stage 1's
     * own terms are not read. */
    for (s = 0ul; s < V9X_D3D_PS_STAGES; ++s) {
        v9x_d3d_ps_stage_reset(&stages[s], s);
    }
    stages[0].bump[0] = 0.5f;
    stages[0].bump[3] = 0.5f;
    stages[0].lscale = 0.5f;
    stages[0].loffset = 0.25f;
    stages[1].lscale = 8.0f;
    stages[1].loffset = 8.0f;
    ff_stage(&s0, TOP_BUMPENVMAPLUMINANCE, TA_TEXTURE, TA_CURRENT,
             TOP_SELECT1, TA_TEXTURE, TA_CURRENT);
    ff_stage(&s1, TOP_SELECT1, TA_TEXTURE, TA_CURRENT,
             TOP_SELECT1, TA_TEXTURE, TA_CURRENT);
    CHECK(ffe_run(&s0, &s1, stages, 0ul, ffl_sample, out) &&
          ff_is(out, 0.15625f, 0.390625f, 0.3125f, 1.0f));
    CHECK(needs2(&s0, &s1, 1));
    /* A luminance bump stage moving another bump stage is not drawn. */
    s1.color_op = TOP_BUMPENVMAP;
    CHECK(fixed2(&s0, &s1, 1, 0, tokens) == 0ul);

    /* Ordinary stages need no shader. */
    ff_stage(&s0, TOP_MODULATE, TA_TEXTURE, TA_DIFFUSE,
             TOP_SELECT1, TA_TEXTURE, TA_DIFFUSE);
    ff_stage(&s1, TOP_ADD, TA_TEXTURE, TA_CURRENT,
             TOP_SELECT2, TA_TEXTURE, TA_CURRENT);
    CHECK(!needs2(&s0, &s1,1));
}

unsigned int v9x_run_d3d_ps_tests(void)
{
    failures = 0u;
    stage_tests();
    constant_tests();
    wide_tests();
    table_tests();
    fixed_tests();
    fixed_bump_tests();
    return failures;
}
