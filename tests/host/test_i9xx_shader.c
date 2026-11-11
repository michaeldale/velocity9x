/*
 * The Gen3 shader stream builders (step 5c of docs\plans\gen3-ps11-pixel-
 * shaders.md): the state block (i9xx_3d.c), the constants packet and the
 * vertex run (i9xx_shader.c). Dwords checked against the packet layouts
 * the two-unit builders already emit, the new fields against Mesa's and
 * xf86's i915_reg.h. Not run on hardware.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/i9xx_fs.h"
#include "velocity9x/intel_gen3_3d.h"
#include "velocity9x/intel_gma.h"

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

/* ------------------------------------------------------------------ */
/* The licence (step 5d)                                               */
/* ------------------------------------------------------------------ */

#define TARGET      0x00200000ul
#define TARGET_W    640ul
#define TARGET_H    480ul
#define TARGET_P    2048ul
#define MAP0        0x00400000ul
#define CUBE3       0x00500000ul

struct shader_case {
    struct v9x_i9xx_texture maps[V9X_I9XX_SHADER_STAGES];
    struct v9x_i9xx_decode_limits limits;
    struct v9x_i9xx_decode_shader shader;
    v9x_u32 stream[CAPACITY];
    v9x_u32 dwords;
    /* Where each part starts, for the mutations. */
    v9x_u32 map_at;
    v9x_u32 sampler_at;
    v9x_u32 load_at;
    v9x_u32 constants_at;
    v9x_u32 program_at;
    v9x_u32 run_at;
};

static void declare_unit(struct v9x_i9xx_decode_unit1 *unit,
                         const struct v9x_i9xx_texture *map, v9x_u32 rows)
{
    memset(unit, 0, sizeof(*unit));
    unit->offset = map->offset;
    unit->bytes = map->pitch * rows;
    unit->width = map->width;
    unit->height = map->height;
    unit->pitch = map->pitch;
    unit->format = map->format;
    unit->wrap = map->wrap;
    unit->mag_linear = map->mag_linear;
    unit->min_linear = map->min_linear;
    unit->mip_filter = map->mip_filter;
    unit->max_lod = map->max_lod;
}

/*
 * Stage 0 a 64x64 565 map, stage 3 a 16x16 ARGB8888 cube; four sets;
 * eighteen constants. The program: dcl T0 T3 T8 T9, S0 2D, S3 cube;
 * texld R2 from S0 at T0; texld R5 from S3 at T3; mad oC, R2, R5, C17.
 */
static void build_case(struct shader_case *c, const v9x_u32 *program,
                       v9x_u32 program_dwords)
{
    v9x_u32 constants[18ul * 4ul];
    float wide[3ul * V9X_I9XX_WIDE_FLOATS];
    v9x_u32 produced = 0ul;
    v9x_u32 at = 0ul;
    v9x_u32 index;

    memset(c, 0, sizeof(*c));
    set_map(&c->maps[0], MAP0, 64ul, 128ul, V9X_I9XX_MAPSURF_16BIT_RGB565);
    set_map(&c->maps[3], CUBE3, 16ul, 128ul,
            V9X_I9XX_MAPSURF_32BIT_ARGB8888);

    c->limits.target_offset = TARGET;
    c->limits.target_bytes = TARGET_P * TARGET_H;
    c->limits.target_pitch = TARGET_P;
    c->limits.target_width = TARGET_W;
    c->limits.target_height = TARGET_H;
    c->limits.kind = V9X_I9XX_SCENE_RUNTIME;
    c->shader.units = 0x9ul;
    c->shader.cubes = 0x8ul;
    c->shader.sets = 4ul;
    c->shader.constants = 18ul;
    declare_unit(&c->shader.maps[0], &c->maps[0], 64ul);
    declare_unit(&c->shader.maps[3], &c->maps[3], 64ul);

    c->stream[at++] = V9X_I9XX_MI_FLUSH_READ;
    c->stream[at++] = V9X_I9XX_3DSTATE_STIPPLE;
    c->stream[at++] = V9X_I9XX_STIPPLE_OFF;
    CHECK(v9x_i9xx_build_shader_state(TARGET, TARGET_P, TARGET_W, TARGET_H,
                                      c->maps, 0x9ul, 0x8ul, 4ul, 0ul, 0ul,
                                      0ul, 0ul, 0ul, 0ul, 0ul,
                                      c->stream + at, CAPACITY - at,
                                      &produced) == V9X_STATUS_OK);
    c->map_at = at + find_packet(c->stream + at, produced,
                                 V9X_I9XX_3DSTATE_MAP_STATE);
    c->sampler_at = at + find_packet(c->stream + at, produced,
                                     V9X_I9XX_3DSTATE_SAMPLER_STATE);
    at += produced;
    c->load_at = at - 6ul;

    for (index = 0ul; index < 18ul * 4ul; ++index) {
        constants[index] = bits_of(0.25f * (float)(index % 5ul));
    }
    c->constants_at = at;
    CHECK(v9x_i9xx_build_shader_constants(constants, 18ul, c->stream + at,
                                          CAPACITY - at, &produced) ==
          V9X_STATUS_OK);
    at += produced;

    c->program_at = at;
    for (index = 0ul; index < program_dwords; ++index) {
        c->stream[at++] = program[index];
    }

    wide_vertex(&wide[0], 10.0f, 10.0f);
    wide_vertex(&wide[V9X_I9XX_WIDE_FLOATS], 100.0f, 10.0f);
    wide_vertex(&wide[2ul * V9X_I9XX_WIDE_FLOATS], 10.0f, 100.0f);
    c->run_at = at;
    CHECK(v9x_i9xx_build_shader_run(wide, 1ul, 4ul, TARGET_W, TARGET_H,
                                    c->stream + at, CAPACITY - at,
                                    &produced) == V9X_STATUS_OK);
    at += produced;
    c->stream[at++] = V9X_I9XX_MI_FLUSH;
    if ((at & 1ul) != 0ul) {
        c->stream[at++] = V9X_I9XX_MI_NOOP;
    }
    c->dwords = at;
}

/* The case's program, by the encoder. */
static v9x_u32 case_program(v9x_u32 *out, v9x_u32 sampler3_type,
                            v9x_u32 extra_sampler)
{
    struct v9x_i9xx_fs fs;
    v9x_u32 length = 0ul;

    v9x_i9xx_fs_begin(&fs, out, 256ul);
    v9x_i9xx_fs_dcl_t(&fs, 0ul, V9X_I9XX_FS_MASK_XYZW);
    v9x_i9xx_fs_dcl_t(&fs, 3ul, V9X_I9XX_FS_MASK_XYZW);
    v9x_i9xx_fs_dcl_t(&fs, V9X_I9XX_FS_T_DIFFUSE, V9X_I9XX_FS_MASK_XYZW);
    v9x_i9xx_fs_dcl_t(&fs, V9X_I9XX_FS_T_SPECULAR, V9X_I9XX_FS_MASK_XYZW);
    v9x_i9xx_fs_dcl_s(&fs, 0ul, V9X_I9XX_FS_D0_SAMPLE_2D);
    v9x_i9xx_fs_dcl_s(&fs, 3ul, sampler3_type);
    if (extra_sampler) {
        v9x_i9xx_fs_dcl_s(&fs, 2ul, V9X_I9XX_FS_D0_SAMPLE_2D);
    }
    v9x_i9xx_fs_tex(&fs, V9X_I9XX_T0_TEXLD, V9X_I9XX_FS_REG_TYPE_R, 2ul,
                    0ul, V9X_I9XX_FS_REG_TYPE_T, 0ul);
    v9x_i9xx_fs_tex(&fs, V9X_I9XX_T0_TEXLD, V9X_I9XX_FS_REG_TYPE_R, 5ul,
                    3ul, V9X_I9XX_FS_REG_TYPE_T, 3ul);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MAD, V9X_I9XX_FS_REG_TYPE_OC, 0ul,
                    V9X_I9XX_FS_MASK_XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(V9X_I9XX_FS_REG_TYPE_R, 2ul),
                    V9X_I9XX_FS_SRC(V9X_I9XX_FS_REG_TYPE_R, 5ul),
                    V9X_I9XX_FS_SRC(V9X_I9XX_FS_REG_TYPE_CONST, 17ul));
    CHECK(v9x_i9xx_fs_end(&fs, &length) == V9X_STATUS_OK);
    return length;
}

static v9x_u16 decode(const struct shader_case *c, v9x_u32 *where)
{
    return v9x_i9xx_decode_shader_stream(c->stream, c->dwords, &c->limits,
                                         &c->shader, where);
}

static void test_licence(void)
{
    static struct shader_case c;
    static struct shader_case m;
    v9x_u32 program[256];
    v9x_u32 length;
    v9x_u32 where = 0ul;

    length = case_program(program, V9X_I9XX_FS_D0_SAMPLE_CUBE, 0);
    build_case(&c, program, length);
    CHECK(decode(&c, &where) == V9X_I9XX_P5_OK);
    if (decode(&c, &where) != V9X_I9XX_P5_OK) {
        printf("  shader stream refused at dword %lu\n",
               (unsigned long)where);
    }

    /* Neither other licence takes it. */
    CHECK(v9x_i9xx_decode_phase5_stream(c.stream, c.dwords, &c.limits,
                                        &where) != V9X_I9XX_P5_OK);

    /* A map at another address; a cube without its face enables; the
     * stage mask; a sampler naming another stage's map. */
    m = c;
    m.stream[m.map_at + 2ul] += 0x1000ul;
    CHECK(decode(&m, &where) == V9X_I9XX_P5_TEXTURE_STATE);
    m = c;
    m.stream[m.map_at + 7ul] &= ~V9X_I9XX_MS4_CUBE_FACE_ENA_ALL;
    CHECK(decode(&m, &where) == V9X_I9XX_P5_TEXTURE_STATE);
    m = c;
    m.stream[m.map_at + 1ul] = 0xbul;
    CHECK(decode(&m, &where) != V9X_I9XX_P5_OK);
    m = c;
    m.stream[m.sampler_at + 6ul] &= ~V9X_I9XX_SS3_MAP_INDEX_MASK;
    CHECK(decode(&m, &where) == V9X_I9XX_P5_TEXTURE_STATE);
    /* The declaration disagreeing with the stream: stage 3 not a cube. */
    m = c;
    m.shader.cubes = 0ul;
    CHECK(decode(&m, &where) != V9X_I9XX_P5_OK);

    /* S2 not the declared sets. */
    m = c;
    m.stream[m.load_at + 1ul] = 0xffffff22ul;
    CHECK(decode(&m, &where) != V9X_I9XX_P5_OK);

    /* The constants: a count other than the declared one. */
    m = c;
    m.shader.constants = 17ul;
    CHECK(decode(&m, &where) != V9X_I9XX_P5_OK);

    /* The program: stage 3 sampled as 2D; a sampler for an unsampled
     * stage; a constant past the loaded ones. */
    length = case_program(program, V9X_I9XX_FS_D0_SAMPLE_2D, 0);
    build_case(&m, program, length);
    CHECK(decode(&m, &where) == V9X_I9XX_P5_SHADER);
    length = case_program(program, V9X_I9XX_FS_D0_SAMPLE_CUBE, 1);
    build_case(&m, program, length);
    CHECK(decode(&m, &where) == V9X_I9XX_P5_SHADER);
    length = case_program(program, V9X_I9XX_FS_D0_SAMPLE_CUBE, 0);
    build_case(&m, program, length);
    m.shader.constants = 18ul;
    m.stream[m.program_at + 1ul + 3ul * 8ul + 2ul] =
        (m.stream[m.program_at + 1ul + 3ul * 8ul + 2ul] & ~0x001f0000ul) |
        (20ul << 16);
    CHECK(decode(&m, &where) == V9X_I9XX_P5_SHADER);
    /* A T register past the declared sets is not declarable. */
    m = c;
    m.shader.sets = 3ul;
    m.stream[m.load_at + 1ul] = 0xfffff222ul;
    CHECK(decode(&m, &where) != V9X_I9XX_P5_OK);

    /* The vertices: off the target, and a coordinate that is a NaN. */
    m = c;
    m.stream[m.run_at + 1ul] = bits_of(5000.0f);
    CHECK(decode(&m, &where) == V9X_I9XX_P5_VERTEX_RANGE);
    m = c;
    m.stream[m.run_at + 1ul + 6ul + 13ul] = 0x7fc00000ul;
    CHECK(decode(&m, &where) == V9X_I9XX_P5_VERTEX_RANGE);

    /* No MAP_STATE at all: the packet is replaced by NOOPs. */
    m = c;
    {
        v9x_u32 k;

        for (k = 0ul; k < 2ul + 3ul * 2ul; ++k) {
            m.stream[m.map_at + k] = V9X_I9XX_MI_NOOP;
        }
    }
    CHECK(decode(&m, &where) == V9X_I9XX_P5_MISSING_PACKET);

    /* Fog or a texture range declared beside a shader: refused. */
    m = c;
    m.limits.fog = 1ul;
    CHECK(decode(&m, &where) != V9X_I9XX_P5_OK);
    m = c;
    m.limits.texture_offset = MAP0;
    m.limits.texture_bytes = 128ul * 64ul;
    CHECK(decode(&m, &where) != V9X_I9XX_P5_OK);
}

/*
 * For test_i9xx_ps11.c: a translated program in a whole stream, stages in
 * `units` sampled from 16x16 maps (those in `cubes` cubes), four sets,
 * `constants` constant registers; the licence's answer.
 */
v9x_u16 v9x_test_shader_licence(const v9x_u32 *program, v9x_u32 dwords,
                                v9x_u32 units, v9x_u32 cubes,
                                v9x_u32 constants, v9x_u32 *where)
{
    static struct shader_case c;
    v9x_u32 values[V9X_I9XX_SHADER_CONSTANTS_MAX * 4ul];
    float wide[3ul * V9X_I9XX_WIDE_FLOATS];
    v9x_u32 produced = 0ul;
    v9x_u32 at = 0ul;
    v9x_u32 stage;
    v9x_u32 index;

    memset(&c, 0, sizeof(c));
    for (stage = 0ul; stage < V9X_I9XX_SHADER_STAGES; ++stage) {
        if ((units & (1ul << stage)) != 0ul) {
            set_map(&c.maps[stage], MAP0 + 0x10000ul * stage, 16ul, 128ul,
                    V9X_I9XX_MAPSURF_16BIT_RGB565);
            declare_unit(&c.shader.maps[stage], &c.maps[stage], 64ul);
        }
    }
    c.limits.target_offset = TARGET;
    c.limits.target_bytes = TARGET_P * TARGET_H;
    c.limits.target_pitch = TARGET_P;
    c.limits.target_width = TARGET_W;
    c.limits.target_height = TARGET_H;
    c.limits.kind = V9X_I9XX_SCENE_RUNTIME;
    c.shader.units = units;
    c.shader.cubes = cubes;
    c.shader.sets = 4ul;
    c.shader.constants = constants;

    c.stream[at++] = V9X_I9XX_MI_FLUSH_READ;
    c.stream[at++] = V9X_I9XX_3DSTATE_STIPPLE;
    c.stream[at++] = V9X_I9XX_STIPPLE_OFF;
    if (v9x_i9xx_build_shader_state(TARGET, TARGET_P, TARGET_W, TARGET_H,
                                    c.maps, units, cubes, 4ul, 0ul, 0ul, 0ul,
                                    0ul, 0ul, 0ul, 0ul, c.stream + at,
                                    CAPACITY - at, &produced) !=
        V9X_STATUS_OK) {
        return 0xffffu;
    }
    at += produced;
    for (index = 0ul; index < constants * 4ul; ++index) {
        values[index] = 0ul;
    }
    if (v9x_i9xx_build_shader_constants(values, constants, c.stream + at,
                                        CAPACITY - at, &produced) !=
        V9X_STATUS_OK) {
        return 0xffffu;
    }
    at += produced;
    for (index = 0ul; index < dwords; ++index) {
        c.stream[at++] = program[index];
    }
    wide_vertex(&wide[0], 10.0f, 10.0f);
    wide_vertex(&wide[V9X_I9XX_WIDE_FLOATS], 100.0f, 10.0f);
    wide_vertex(&wide[2ul * V9X_I9XX_WIDE_FLOATS], 10.0f, 100.0f);
    if (v9x_i9xx_build_shader_run(wide, 1ul, 4ul, TARGET_W, TARGET_H,
                                  c.stream + at, CAPACITY - at,
                                  &produced) != V9X_STATUS_OK) {
        return 0xffffu;
    }
    at += produced;
    c.stream[at++] = V9X_I9XX_MI_FLUSH;
    if ((at & 1ul) != 0ul) {
        c.stream[at++] = V9X_I9XX_MI_NOOP;
    }
    c.dwords = at;
    return decode(&c, where);
}

unsigned int v9x_run_i9xx_shader_tests(void)
{
    failures = 0u;
    test_state_block();
    test_state_block_edges();
    test_constants();
    test_run();
    test_licence();
    return failures;
}
