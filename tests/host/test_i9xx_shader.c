/*
 * The Gen3 shader stream builders (step 5c of docs\plans\gen3-ps11-pixel-
 * shaders.md): the state block (i9xx_3d.c), the constants packet and the
 * vertex run (i9xx_shader.c). Dwords checked against the packet layouts
 * the two-unit builders already emit, the new fields against Mesa's and
 * xf86's i915_reg.h. Not run on hardware.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/intel_gen3_3d.h"

static unsigned int failures;
#define CHECK(e) do { if (!(e)) { ++failures; \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
} } while (0)

#define CAPACITY 2048ul

static v9x_u32 bits_of(float value)
{
    v9x_u32 bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void set_map(struct v9x_i9xx_texture *map, v9x_u32 offset,
                    v9x_u32 edge, v9x_u32 pitch, v9x_u32 format)
{
    map->offset = offset;
    map->width = edge;
    map->height = edge;
    map->pitch = pitch;
    map->format = format;
    map->wrap = 0ul;
    map->mag_linear = 0ul;
    map->min_linear = 0ul;
    map->mip_filter = 0ul;
    map->max_lod = 0ul;
}

/* Find the first dword equal to `header` with the low 16 bits masked. */
static v9x_u32 find_packet(const v9x_u32 *stream, v9x_u32 dwords,
                           v9x_u32 header)
{
    v9x_u32 index;

    for (index = 0ul; index < dwords; ++index) {
        if ((stream[index] & 0xffff0000ul) == header) {
            return index;
        }
    }
    return dwords;
}

/*
 * Stages 0 and 3 sampled, stage 3 a cube: MAP_STATE and SAMPLER_STATE
 * name units 0 and 3 (mask 9) with two entries each; the cube's MS4 has
 * the six face enables; sampler 3 names map 3. S2 declares three sets of
 * four floats, S4 position, diffuse and specular.
 */
static void test_state_block(void)
{
    struct v9x_i9xx_texture maps[V9X_I9XX_SHADER_STAGES];
    v9x_u32 stream[CAPACITY];
    v9x_u32 written = 0ul;
    v9x_u32 at;
    v9x_u32 extent;

    memset(maps, 0, sizeof(maps));
    set_map(&maps[0], 0x00300000ul, 64ul, 128ul,
            V9X_I9XX_MAPSURF_16BIT_RGB565);
    set_map(&maps[3], 0x00400000ul, 16ul, 128ul,
            V9X_I9XX_MAPSURF_32BIT_ARGB8888);

    extent = v9x_i9xx_shader_state_extent(0x9ul, 0ul, 0ul);
    CHECK(extent != 0ul);
    CHECK(v9x_i9xx_build_shader_state(0x00200000ul, 2048ul, 640ul, 480ul,
                                      maps, 0x9ul, 0x8ul, 3ul, 0ul, 0ul,
                                      0ul, 0ul, 0ul, 0ul, 0ul, stream,
                                      CAPACITY, &written) == V9X_STATUS_OK);
    CHECK(written == extent);

    at = find_packet(stream, written, V9X_I9XX_3DSTATE_MAP_STATE);
    CHECK(at < written);
    if (at < written) {
        CHECK(stream[at] == (V9X_I9XX_3DSTATE_MAP_STATE | 6ul));
        CHECK(stream[at + 1ul] == 0x9ul);
        /* Unit 0: address, MS3, MS4 as the plain builder makes them. */
        CHECK(stream[at + 2ul] == 0x00300000ul);
        CHECK((stream[at + 4ul] & V9X_I9XX_MS4_CUBE_FACE_ENA_ALL) == 0ul);
        /* Unit 3: the cube, every face enabled, its 32-bit format. */
        CHECK(stream[at + 5ul] == 0x00400000ul);
        CHECK((stream[at + 6ul] & 0x000003f8ul) ==
              V9X_I9XX_MAPSURF_32BIT_ARGB8888);
        CHECK((stream[at + 7ul] & V9X_I9XX_MS4_CUBE_FACE_ENA_ALL) ==
              V9X_I9XX_MS4_CUBE_FACE_ENA_ALL);
        CHECK((stream[at + 7ul] >> V9X_I9XX_MS4_PITCH_SHIFT) ==
              (128ul / 4ul) - 1ul);
    }

    at = find_packet(stream, written, V9X_I9XX_3DSTATE_SAMPLER_STATE);
    CHECK(at < written);
    if (at < written) {
        CHECK(stream[at] == (V9X_I9XX_3DSTATE_SAMPLER_STATE | 6ul));
        CHECK(stream[at + 1ul] == 0x9ul);
        CHECK((stream[at + 3ul] & V9X_I9XX_SS3_MAP_INDEX_MASK) ==
              (0ul << V9X_I9XX_SS3_MAP_INDEX_SHIFT));
        CHECK((stream[at + 6ul] & V9X_I9XX_SS3_MAP_INDEX_MASK) ==
              (3ul << V9X_I9XX_SS3_MAP_INDEX_SHIFT));
        /* The cube's sampler: CLAMP_EDGE on x, y and z. */
        CHECK(((stream[at + 6ul] >> V9X_I9XX_SS3_TCX_SHIFT) & 7ul) ==
              V9X_I9XX_TEXCOORDMODE_CLAMP_EDGE);
        CHECK(((stream[at + 6ul] >> V9X_I9XX_SS3_TCZ_SHIFT) & 7ul) ==
              V9X_I9XX_TEXCOORDMODE_CLAMP_EDGE);
    }

    /* The S2-S6 load ends the block: S2 three 4D sets, S4 position,
     * diffuse, specular. */
    at = written - 6ul;
    CHECK(stream[at + 1ul] == 0xfffff222ul);
    CHECK((stream[at + 3ul] & (V9X_I9XX_S4_VFMT_XYZW |
                               V9X_I9XX_S4_VFMT_COLOR |
                               V9X_I9XX_S4_VFMT_SPEC_FOG)) ==
          (V9X_I9XX_S4_VFMT_XYZW | V9X_I9XX_S4_VFMT_COLOR |
           V9X_I9XX_S4_VFMT_SPEC_FOG));

    /* The block feeds v9x_i9xx_retarget_state like the others do. */
    CHECK(v9x_i9xx_retarget_state(stream, written,
                                  V9X_I9XX_COLR_BUF_ARGB8888 |
                                      V9X_I9XX_DEPTH_FRMT_16_FIXED) ==
          V9X_STATUS_OK);
}

/* No textures: no map or sampler packets; no sets: S2 all absent. And
 * the refusals. */
static void test_state_block_edges(void)
{
    struct v9x_i9xx_texture maps[V9X_I9XX_SHADER_STAGES];
    v9x_u32 stream[CAPACITY];
    v9x_u32 written = 0ul;

    memset(maps, 0, sizeof(maps));
    CHECK(v9x_i9xx_build_shader_state(0x00200000ul, 2048ul, 640ul, 480ul,
                                      maps, 0ul, 0ul, 0ul, 0ul, 0ul, 0ul,
                                      0ul, 0ul, 0ul, 0ul, stream, CAPACITY,
                                      &written) == V9X_STATUS_OK);
    CHECK(written == v9x_i9xx_shader_state_extent(0ul, 0ul, 0ul));
    CHECK(find_packet(stream, written, V9X_I9XX_3DSTATE_MAP_STATE) ==
          written);
    CHECK(stream[written - 5ul] == V9X_I9XX_S2_ALL_TEXCOORD_ABSENT);

    set_map(&maps[1], 0x00300000ul, 64ul, 128ul,
            V9X_I9XX_MAPSURF_16BIT_RGB565);
    /* A cube that is not a sampled stage; five sets; a stage past 3. */
    CHECK(v9x_i9xx_build_shader_state(0x00200000ul, 2048ul, 640ul, 480ul,
                                      maps, 0x2ul, 0x4ul, 2ul, 0ul, 0ul,
                                      0ul, 0ul, 0ul, 0ul, 0ul, stream,
                                      CAPACITY, &written) !=
          V9X_STATUS_OK);
    CHECK(written == 0ul);
    CHECK(v9x_i9xx_build_shader_state(0x00200000ul, 2048ul, 640ul, 480ul,
                                      maps, 0x2ul, 0ul, 5ul, 0ul, 0ul, 0ul,
                                      0ul, 0ul, 0ul, 0ul, stream, CAPACITY,
                                      &written) != V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_shader_state(0x00200000ul, 2048ul, 640ul, 480ul,
                                      maps, 0x12ul, 0ul, 2ul, 0ul, 0ul,
                                      0ul, 0ul, 0ul, 0ul, 0ul, stream,
                                      CAPACITY, &written) != V9X_STATUS_OK);
    /* A map the plain builder would refuse is refused here too. */
    maps[1].format = 0ul;
    CHECK(v9x_i9xx_build_shader_state(0x00200000ul, 2048ul, 640ul, 480ul,
                                      maps, 0x2ul, 0ul, 2ul, 0ul, 0ul, 0ul,
                                      0ul, 0ul, 0ul, 0ul, stream, CAPACITY,
                                      &written) != V9X_STATUS_OK);
    /* Too little room. */
    maps[1].format = V9X_I9XX_MAPSURF_16BIT_RGB565;
    CHECK(v9x_i9xx_build_shader_state(0x00200000ul, 2048ul, 640ul, 480ul,
                                      maps, 0x2ul, 0ul, 2ul, 0ul, 0ul, 0ul,
                                      0ul, 0ul, 0ul, 0ul, stream, 8ul,
                                      &written) != V9X_STATUS_OK);
}

static void test_constants(void)
{
    v9x_u32 values[V9X_I9XX_SHADER_CONSTANTS_MAX * 4ul];
    v9x_u32 stream[CAPACITY];
    v9x_u32 written = 0ul;
    v9x_u32 index;

    for (index = 0ul; index < 18ul * 4ul; ++index) {
        values[index] = 0x1000ul + index;
    }
    CHECK(v9x_i9xx_build_shader_constants(values, 18ul, stream, CAPACITY,
                                          &written) == V9X_STATUS_OK);
    CHECK(written == 2ul + 18ul * 4ul);
    CHECK(stream[0] == (V9X_I9XX_3DSTATE_PS_CONSTANTS | 72ul));
    CHECK(stream[1] == 0x0003fffful);
    CHECK(stream[2] == 0x1000ul && stream[2ul + 71ul] == 0x1000ul + 71ul);

    CHECK(v9x_i9xx_build_shader_constants(values, 0ul, stream, CAPACITY,
                                          &written) != V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_shader_constants(values, 33ul, stream, CAPACITY,
                                          &written) != V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_shader_constants(values, 18ul, stream, 73ul,
                                          &written) != V9X_STATUS_OK);
    CHECK(written == 0ul);
}

static void wide_vertex(float *v, float x, float y)
{
    v9x_u32 k;

    for (k = 0ul; k < V9X_I9XX_WIDE_FLOATS; ++k) {
        v[k] = 0.0f;
    }
    v[0] = x;
    v[1] = y;
    v[2] = 0.5f;
    v[3] = 1.0f;
    /* diffuse r g b a: 1, 0.5, 0, 1 */
    v[4] = 1.0f;
    v[5] = 0.5f;
    v[6] = 0.0f;
    v[7] = 1.0f;
    /* specular: past 1 and below 0 clamp; alpha the fog factor 0.25 */
    v[8] = 2.0f;
    v[9] = -1.0f;
    v[10] = 0.0f;
    v[11] = 0.25f;
    /* stage N's coordinates: 10N + 1 .. 10N + 4 */
    for (k = 0ul; k < 4ul; ++k) {
        v[12ul + 4ul * k] = 10.0f * (float)k + 1.0f;
        v[13ul + 4ul * k] = 10.0f * (float)k + 2.0f;
        v[14ul + 4ul * k] = 10.0f * (float)k + 3.0f;
        v[15ul + 4ul * k] = 10.0f * (float)k + 4.0f;
    }
}

static void test_run(void)
{
    float wide[3ul * V9X_I9XX_WIDE_FLOATS];
    v9x_u32 stream[CAPACITY];
    v9x_u32 written = 0ul;

    wide_vertex(&wide[0], 10.0f, 10.0f);
    wide_vertex(&wide[V9X_I9XX_WIDE_FLOATS], 100.0f, 10.0f);
    wide_vertex(&wide[2ul * V9X_I9XX_WIDE_FLOATS], 10.0f, 100.0f);

    /* Two sets: 4 + 1 + 1 + 8 dwords a vertex. */
    CHECK(v9x_i9xx_shader_run_dwords(1ul, 2ul) == 1ul + 3ul * 14ul);
    CHECK(v9x_i9xx_build_shader_run(wide, 1ul, 2ul, 640ul, 480ul, stream,
                                    CAPACITY, &written) == V9X_STATUS_OK);
    CHECK(written == 43ul);
    CHECK(stream[0] == (V9X_I9XX_3DPRIMITIVE_INLINE |
                        V9X_I9XX_PRIM3D_TRILIST | 41ul));
    CHECK(stream[1] == bits_of(10.0f) && stream[3] == bits_of(0.5f) &&
          stream[4] == bits_of(1.0f));
    CHECK(stream[5] == 0xffff8000ul);
    CHECK(stream[6] == 0x40ff0000ul);
    CHECK(stream[7] == bits_of(1.0f) && stream[10] == bits_of(4.0f));
    CHECK(stream[11] == bits_of(11.0f) && stream[14] == bits_of(14.0f));
    /* The second vertex starts 14 dwords on. */
    CHECK(stream[15] == bits_of(100.0f));

    /* No sets: position and the two colours only. */
    CHECK(v9x_i9xx_build_shader_run(wide, 1ul, 0ul, 640ul, 480ul, stream,
                                    CAPACITY, &written) == V9X_STATUS_OK);
    CHECK(written == 1ul + 3ul * 6ul);

    /* Refused: a coordinate not finite in a set emitted; a vertex off the
     * target; five sets; too little room. */
    {
        v9x_u32 nan_bits = 0x7fc00000ul;

        memcpy(&wide[13], &nan_bits, sizeof(nan_bits));
        CHECK(v9x_i9xx_build_shader_run(wide, 1ul, 2ul, 640ul, 480ul,
                                        stream, CAPACITY, &written) !=
              V9X_STATUS_OK);
        CHECK(written == 0ul);
        /* ...but only a set that is emitted counts. */
        CHECK(v9x_i9xx_build_shader_run(wide, 1ul, 0ul, 640ul, 480ul,
                                        stream, CAPACITY, &written) ==
              V9X_STATUS_OK);
        wide[13] = 2.0f;
    }
    wide[0] = 5000.0f;
    CHECK(v9x_i9xx_build_shader_run(wide, 1ul, 2ul, 640ul, 480ul, stream,
                                    CAPACITY, &written) != V9X_STATUS_OK);
    wide[0] = 10.0f;
    CHECK(v9x_i9xx_build_shader_run(wide, 1ul, 5ul, 640ul, 480ul, stream,
                                    CAPACITY, &written) != V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_shader_run(wide, 1ul, 2ul, 640ul, 480ul, stream,
                                    42ul, &written) != V9X_STATUS_OK);
}

unsigned int v9x_run_i9xx_shader_tests(void)
{
    failures = 0u;
    test_state_block();
    test_state_block_edges();
    test_constants();
    test_run();
    return failures;
}
