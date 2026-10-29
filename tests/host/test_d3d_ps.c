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

unsigned int v9x_run_d3d_ps_tests(void)
{
    failures = 0u;
    stage_tests();
    constant_tests();
    wide_tests();
    table_tests();
    return failures;
}
