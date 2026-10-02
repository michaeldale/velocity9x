/*
 * The Rage IIC draw path: policy, state stream, and that the trapezoids a
 * triangle's packets trigger - split for height and, under perspective,
 * the triangle split in pieces - draw exactly the pixels the reference
 * rasteriser covers, each once.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/ati_rage2_draw.h"
#include "velocity9x/ati_mach64_regs.h"
#include "rage2_reference.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define GRID 256

static unsigned char drawn[GRID][GRID];

static void base_request(struct v9x_m64_draw_request *r)
{
    memset(r, 0, sizeof(*r));
    r->target_format = 1ul;
    r->target_width = 640ul;
    r->target_height = 480ul;
    r->scissor_right = 640ul;
    r->scissor_bottom = 480ul;
    r->write_mask = 7ul;
    r->shade_mode = 2ul;
}

/* A fresh request with a 64x32 texture; resets everything else. */
static void textured(struct v9x_m64_draw_request *r, v9x_u32 format)
{
    base_request(r);
    r->textured = 1ul;
    r->texture_format = format;
    r->texture_width = 64ul;
    r->texture_height = 32ul;
    r->texture_levels = 1ul;
    r->texture_min_filter = 1ul;
    r->texture_mag_filter = 1ul;
    r->texture_address = 1ul;
    r->texture_op = 2ul;        /* MODULATE */
}

static void test_policy(void)
{
    struct v9x_m64_draw_request r;
    struct v9x_r2_draw_decision d;

    CHECK(v9x_r2_check_draw(0, 0ul, &d) == V9X_M64_REFUSE_ARGUMENT);

    /* Gouraud, untextured: the shading function, nothing else. */
    base_request(&r);
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_NONE);
    CHECK(d.scale_3d_cntl == V9X_R2_SCALE_3D_SHADE);

    r.target_format = 2ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_TARGET_FORMAT);
    base_request(&r);
    r.scissor_right = 641ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_SCISSOR);
    base_request(&r);
    r.write_mask = 3ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_WRITE_MASK);
    base_request(&r);
    r.specular_enable = 1ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_SPECULAR);
    base_request(&r);
    r.color_key_enable = 1ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_COLOR_KEY);
    base_request(&r);
    r.depth_enable = 1ul;
    r.depth_bits = 16ul;
    r.depth_func = 4ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_NONE);
    r.depth_bits = 24ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_DEPTH);

    /* Textures: format, shape, filter, address, op. */
    base_request(&r);
    textured(&r, V9X_M64_TEXTURE_FORMAT_RGB565);
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_NONE);
    CHECK(d.scale_3d_cntl == (V9X_R2_SCALE_3D_TEXTURE | V9X_R2_TEX_CACHE_DIS |
                              V9X_R2_MIP_MAP_DISABLE |
                              V9X_R2_TEX_LIGHT_MODULATE));
    CHECK(d.texture_format == V9X_R2_TEX_FORMAT_565);
    r.texture_format = 7ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_TEXTURE_FORMAT);
    textured(&r, V9X_M64_TEXTURE_FORMAT_ARGB4444);
    r.texture_width = 512ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_TEXTURE_SHAPE);
    textured(&r, V9X_M64_TEXTURE_FORMAT_ARGB4444);
    r.texture_width = 48ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_TEXTURE_SHAPE);
    /* Bilinear both ways; linear-mag alone; point-mag linear-min refused;
     * a mip filter is its filter within level 0. */
    textured(&r, V9X_M64_TEXTURE_FORMAT_RGB565);
    r.texture_mag_filter = 2ul;
    r.texture_min_filter = 6ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_NONE);
    CHECK((d.scale_3d_cntl & (V9X_R2_BILINEAR_TEX_EN | V9X_R2_TEX_BLEND_MASK))
          == (V9X_R2_BILINEAR_TEX_EN | V9X_R2_TEX_BLEND_2X2));
    r.texture_min_filter = 3ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_NONE);
    CHECK((d.scale_3d_cntl & (V9X_R2_BILINEAR_TEX_EN | V9X_R2_TEX_BLEND_MASK))
          == V9X_R2_BILINEAR_TEX_EN);
    r.texture_mag_filter = 1ul;
    r.texture_min_filter = 2ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_TEXTURE_FILTER);
    /* Wrap only; clamp where every coordinate is in [0, 1]. */
    textured(&r, V9X_M64_TEXTURE_FORMAT_RGB565);
    r.texture_address = 3ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_TEXTURE_ADDRESS);
    CHECK(v9x_r2_check_draw(&r, 1ul, &d) == V9X_M64_REFUSE_NONE);
    CHECK(d.clamp_in_unit == 1ul);
    r.texture_address = 2ul;
    CHECK(v9x_r2_check_draw(&r, 1ul, &d) == V9X_M64_REFUSE_TEXTURE_ADDRESS);
    textured(&r, V9X_M64_TEXTURE_FORMAT_RGB565);
    r.texture_wrap_u = 1ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_TEXTURE_ADDRESS);
    /* Ops: alpha textures take TEX_MAP_AEN; MODULATEALPHA only where one
     * alpha is 1; DECALALPHA without blending; ADD refused. */
    textured(&r, V9X_M64_TEXTURE_FORMAT_ARGB4444);
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_NONE);
    CHECK((d.scale_3d_cntl & V9X_R2_TEX_MAP_AEN) != 0ul);
    r.texture_op = 4ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_TEXTURE_OP);
    r.vertex_alpha_opaque = 1ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_NONE);
    r.texture_op = 3ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_NONE);
    CHECK((d.scale_3d_cntl & V9X_R2_TEX_LIGHT_DECAL) != 0ul);
    r.blend_enable = 1ul;
    r.src_blend = 5ul;
    r.dst_blend = 6ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_TEXTURE_OP);
    textured(&r, V9X_M64_TEXTURE_FORMAT_RGB565);
    r.texture_op = 8ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_TEXTURE_OP);

    /* Blending: the six factors each way; ONE/ZERO is no blend. */
    base_request(&r);
    r.blend_enable = 1ul;
    r.src_blend = 5ul;
    r.dst_blend = 6ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_NONE);
    CHECK(d.scale_3d_cntl == (V9X_R2_SCALE_3D_SHADE | V9X_R2_ALPHA_FOG_BLEND |
                              (4ul << V9X_R2_BLEND_SRC_SHIFT) |
                              (5ul << V9X_R2_BLEND_DST_SHIFT)));
    r.src_blend = 9ul;
    r.dst_blend = 3ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_NONE);
    r.src_blend = 7ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_BLEND_FACTOR);
    r.src_blend = 2ul;
    r.dst_blend = 9ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_BLEND_FACTOR);
    r.dst_blend = 1ul;
    r.fog_enable = 1ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_NONE);
    CHECK((d.scale_3d_cntl & V9X_R2_ALPHA_FOG_FOG) != 0ul &&
          d.alpha_from_fog == 1ul);
    r.dst_blend = 6ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_FOG_WITH_BLEND);

    /* Fog: As / 1-As. */
    base_request(&r);
    r.fog_enable = 1ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_NONE);
    CHECK(d.scale_3d_cntl == (V9X_R2_SCALE_3D_SHADE | V9X_R2_ALPHA_FOG_FOG |
                              (4ul << V9X_R2_BLEND_SRC_SHIFT) |
                              (5ul << V9X_R2_BLEND_DST_SHIFT)));

    /* Fog over an alpha texture: the factor must be the interpolator's,
     * so TEX_MAP_AEN goes; with the alpha mask or decal it cannot. */
    textured(&r, V9X_M64_TEXTURE_FORMAT_ARGB1555);
    r.texture_op = 7ul;
    r.fog_enable = 1ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_NONE);
    CHECK((d.scale_3d_cntl & V9X_R2_TEX_MAP_AEN) == 0ul &&
          (d.scale_3d_cntl & V9X_R2_ALPHA_FOG_FOG) != 0ul);
    r.alpha_test_enable = 1ul;
    r.alpha_func = 5ul;
    r.alpha_ref = 127ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_FOG_WITH_TEXTURE);
    textured(&r, V9X_M64_TEXTURE_FORMAT_ARGB4444);
    r.texture_op = 3ul;
    r.fog_enable = 1ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_FOG_WITH_TEXTURE);

    /* The alpha test is the 1555 mask or nothing. */
    base_request(&r);
    textured(&r, V9X_M64_TEXTURE_FORMAT_ARGB1555);
    r.alpha_test_enable = 1ul;
    r.alpha_func = 5ul;
    r.alpha_ref = 127ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_NONE);
    CHECK((d.scale_3d_cntl & (V9X_R2_TEX_AMASK_AEN | V9X_R2_TEX_MAP_AEN)) ==
          (V9X_R2_TEX_AMASK_AEN | V9X_R2_TEX_MAP_AEN));
    r.alpha_ref = 255ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_ALPHA_TEST);
    r.alpha_func = 7ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_NONE);
    r.alpha_func = 2ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_ALPHA_TEST);
    r.alpha_func = 8ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_NONE);
    CHECK((d.scale_3d_cntl & V9X_R2_TEX_AMASK_AEN) == 0ul);
    textured(&r, V9X_M64_TEXTURE_FORMAT_ARGB4444);
    r.alpha_test_enable = 1ul;
    r.alpha_func = 5ul;
    r.alpha_ref = 0ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_ALPHA_TEST);
}

static void make_state(struct v9x_r2_draw_state *s, v9x_u32 width,
                       v9x_u32 height)
{
    memset(s, 0, sizeof(*s));
    s->target.offset = 0ul;
    s->target.pitch_bytes = width * 2ul;
    s->target.width = width;
    s->target.height = height;
    s->target.vram_bytes = 0x00400000ul;
    s->target.scissor_right = width - 1ul;
    s->target.scissor_bottom = height - 1ul;
    s->texture.offset = 0x00300000ul;
    s->texture.log2_width = 6ul;
    s->texture.log2_height = 5ul;
    s->texture.log2_pitch = 6ul;
    s->texture.format = V9X_R2_TEX_FORMAT_565;
}

static void test_state(void)
{
    struct v9x_m64_draw_request r;
    struct v9x_r2_draw_decision d;
    struct v9x_r2_draw_state s;
    v9x_u32 offsets[V9X_R2_DRAW_STATE_DWORDS];
    v9x_u32 values[V9X_R2_DRAW_STATE_DWORDS];
    v9x_u32 written = 9ul;
    v9x_u32 index;
    int saw_z = 0;
    int saw_tex = 0;

    base_request(&r);
    textured(&r, V9X_M64_TEXTURE_FORMAT_RGB565);
    r.fog_enable = 1ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_NONE);
    make_state(&s, 640ul, 480ul);
    s.textured = 1ul;
    s.depth_enable = 1ul;
    s.depth_offset = 0x00200000ul;
    s.depth_pitch_bytes = 1280ul;
    s.depth_func = 4ul;
    s.depth_write = 1ul;
    s.fog_color = 0x00804020ul;
    CHECK(v9x_r2_build_draw_state(&s, &d, offsets, values,
                                  V9X_R2_DRAW_STATE_DWORDS - 1ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);
    CHECK(v9x_r2_build_draw_state(&s, &d, offsets, values,
                                  V9X_R2_DRAW_STATE_DWORDS, &written) ==
          V9X_STATUS_OK);
    CHECK(offsets[0] == V9X_M64_SCALE_3D_CNTL && values[0] == d.scale_3d_cntl);
    for (index = 0ul; index < written; ++index) {
        if (offsets[index] == V9X_M64_Z_CNTL) {
            /* Z_EN, LESSEQUAL (the chip's test 2), write. */
            CHECK(values[index] == 0x00000121ul);
            saw_z = 1;
        }
        if (offsets[index] == V9X_R2_TEX_SIZE_PITCH) {
            CHECK(values[index] == 0x566ul);
            saw_tex = 1;
        }
        if (offsets[index] == V9X_R2_TEX_0_OFF + 24ul) {
            CHECK(values[index] == 0x00300000ul);
        }
        if (offsets[index] == V9X_M64_DP_PIX_WIDTH) {
            CHECK(values[index] == 0x40040004ul);
        }
    }
    CHECK(saw_z && saw_tex);
    /* Every Direct3D compare onto Z_TEST's own order (Phase 3's bands:
     * never, <, <=, ==, >=, >, !=, always). */
    {
        static const v9x_u32 want[9] = { 0ul, 0ul, 1ul, 3ul, 2ul, 5ul, 6ul,
                                         4ul, 7ul };
        v9x_u32 func;

        for (func = 1ul; func <= 8ul; ++func) {
            v9x_u32 z_cntl = 0xfffffffful;

            s.depth_func = func;
            s.depth_write = 0ul;
            CHECK(v9x_r2_build_draw_state(&s, &d, offsets, values,
                                          V9X_R2_DRAW_STATE_DWORDS,
                                          &written) == V9X_STATUS_OK);
            for (index = 0ul; index < written; ++index) {
                if (offsets[index] == V9X_M64_Z_CNTL) {
                    z_cntl = values[index];
                }
            }
            CHECK(z_cntl == (0x1ul | (want[func] << 4)));
        }
        s.depth_func = 4ul;
        s.depth_write = 1ul;
    }
    CHECK(offsets[written - 1ul] == V9X_M64_DP_FRGD_CLR &&
          values[written - 1ul] == 0x00804020ul);
    /* A Z buffer past VRAM is refused. */
    s.depth_offset = 0x003f0000ul;
    CHECK(v9x_r2_build_draw_state(&s, &d, offsets, values,
                                  V9X_R2_DRAW_STATE_DWORDS, &written) ==
          V9X_STATUS_INSUFFICIENT_MEMORY);
}

static v9x_u32 lcg = 4242ul;

static v9x_u32 lcg_next(v9x_u32 range)
{
    lcg = lcg * 1103515245ul + 12345ul;
    return (lcg >> 8) % range;
}

/* Each trapezoid's pixels, by the measured walk, counted into drawn[]. */
static void draw_traps(const struct v9x_r2_flat_trap *traps, v9x_u32 count)
{
    static v9x_s32 lead[2048];
    static v9x_s32 trail[2048];
    v9x_u32 index;

    for (index = 0ul; index < count; ++index) {
        v9x_u32 row;

        v9x_r2_ref_walk(&traps[index], lead, trail);
        for (row = 0ul; row < traps[index].length; ++row) {
            v9x_u32 y = traps[index].y + row;
            v9x_s32 x;

            for (x = lead[row]; x < trail[row]; ++x) {
                if (y < GRID && x >= 0l && x < GRID) {
                    ++drawn[y][x];
                }
            }
        }
    }
}

static unsigned int compare_coverage(const struct v9x_r2_vertex *v)
{
    unsigned int bad = 0u;
    v9x_s32 x;
    v9x_s32 y;

    for (y = 0l; y < GRID; ++y) {
        for (x = 0l; x < GRID; ++x) {
            unsigned int want = (unsigned int)v9x_r2_ref_covers(v, x, y);

            if (drawn[y][x] != want) {
                ++bad;
            }
        }
    }
    return bad;
}

static void make_vertex(struct v9x_r2_draw_vertex *v, v9x_s32 x, v9x_s32 y)
{
    v->x = v9x_r2_snap(x);
    v->y = v9x_r2_snap(y);
    v->z = (v9x_u32)lcg_next(65536ul);
    v->argb = 0xff000000ul | lcg_next(0x01000000ul);
    v->fog = 255ul;
    v->tu = (double)(v9x_s32)lcg_next(64ul) / 16.0;
    v->tv = (double)(v9x_s32)lcg_next(64ul) / 16.0;
    v->q = 1.0;
}

/* Tall triangles in a 256x256 target: the pieces of every trapezoid draw
 * the triangle exactly, each pixel once. */
static void test_tall_triangles(void)
{
    struct v9x_m64_draw_request r;
    struct v9x_r2_draw_decision d;
    struct v9x_r2_draw_state s;
    static v9x_u32 offsets[V9X_R2_DRAW_TRAPS_MAX * V9X_R2_DRAW_TRAP_DWORDS];
    static v9x_u32 values[V9X_R2_DRAW_TRAPS_MAX * V9X_R2_DRAW_TRAP_DWORDS];
    struct v9x_r2_flat_trap traps[V9X_R2_DRAW_TRAPS_MAX];
    unsigned int index;
    unsigned int failed = 0u;
    unsigned long pixels = 0ul;
    v9x_u32 max_traps = 0ul;

    base_request(&r);
    r.depth_enable = 1ul;
    r.depth_bits = 16ul;
    r.depth_func = 4ul;
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_NONE);
    make_state(&s, GRID, GRID);
    s.depth_enable = 1ul;
    s.depth_offset = 0x00200000ul;
    s.depth_pitch_bytes = GRID * 2ul;
    for (index = 0u; index < 600u; ++index) {
        struct v9x_r2_draw_vertex v[3];
        struct v9x_r2_vertex pos[3];
        v9x_u32 written = 0ul;
        v9x_u32 count = 0ul;
        v9x_u32 k;
        v9x_status status;
        unsigned int bad;

        for (k = 0ul; k < 3ul; ++k) {
            make_vertex(&v[k], (v9x_s32)lcg_next(GRID * 16ul),
                        (v9x_s32)lcg_next(GRID * 16ul));
            pos[k].x = v[k].x;
            pos[k].y = v[k].y;
        }
        memset(drawn, 0, sizeof(drawn));
        status = v9x_r2_build_piece(&s, &d, v, offsets, values,
                                    V9X_R2_DRAW_TRAPS_MAX *
                                        V9X_R2_DRAW_TRAP_DWORDS,
                                    &written, traps, &count, 0);
        if (status == V9X_STATUS_UNSUPPORTED) {
            continue;           /* a sliver too steep for the colour */
        }
        CHECK(status == V9X_STATUS_OK);
        if (count > max_traps) {
            max_traps = count;
        }
        if (count != 0ul) {
            /* Colour 12, Z 3, the trajectory 9; the trigger last. */
            CHECK(written == count * 24ul);
            CHECK(offsets[written - 1ul] == 0x520ul);
        }
        draw_traps(traps, count);
        bad = compare_coverage(pos);
        if (bad != 0u && failed < 3u) {
            printf("  tall %u: (%ld,%ld) (%ld,%ld) (%ld,%ld): %u\n", index,
                   (long)pos[0].x, (long)pos[0].y, (long)pos[1].x,
                   (long)pos[1].y, (long)pos[2].x, (long)pos[2].y, bad);
        }
        if (bad != 0u) {
            ++failed;
        }
        for (k = 0ul; k < count; ++k) {
            pixels += traps[k].length;
            CHECK(traps[k].length <= V9X_R2_TRAP_LENGTH_MAX);
        }
    }
    CHECK(failed == 0u);
    /* Not vacuous: triangles up to 256 rows need four or more pieces. */
    CHECK(max_traps >= 4ul);
    CHECK(pixels > 20000ul);
}

/*
 * Perspective triangles split in pieces: together they cover the parent
 * exactly, once, and each piece's quadratic is within the bound or at the
 * split depth.
 */
static void test_split_coverage(void)
{
    struct v9x_m64_draw_request r;
    struct v9x_r2_draw_decision d;
    struct v9x_r2_draw_state s;
    static v9x_u32 offsets[V9X_R2_DRAW_TRAPS_MAX * V9X_R2_DRAW_TRAP_DWORDS];
    static v9x_u32 values[V9X_R2_DRAW_TRAPS_MAX * V9X_R2_DRAW_TRAP_DWORDS];
    struct v9x_r2_flat_trap traps[V9X_R2_DRAW_TRAPS_MAX];
    struct v9x_r2_draw_vertex pieces[V9X_R2_DRAW_SPLIT_MAX * 3u];
    unsigned int index;
    unsigned int failed = 0u;
    unsigned int split = 0u;
    unsigned int most = 0u;

    base_request(&r);
    textured(&r, V9X_M64_TEXTURE_FORMAT_RGB565);
    CHECK(v9x_r2_check_draw(&r, 0ul, &d) == V9X_M64_REFUSE_NONE);
    make_state(&s, GRID, GRID);
    s.textured = 1ul;
    for (index = 0u; index < 400u; ++index) {
        struct v9x_r2_draw_vertex v[3];
        struct v9x_r2_vertex pos[3];
        v9x_u32 count = 0ul;
        v9x_u32 piece;
        v9x_u32 k;
        unsigned int bad;
        int skipped = 0;

        /* q of a plane: screen-linear, 1..3 across the grid. */
        for (k = 0ul; k < 3ul; ++k) {
            make_vertex(&v[k], (v9x_s32)lcg_next(GRID * 16ul),
                        (v9x_s32)lcg_next(GRID * 16ul));
            v[k].q = 1.0 + 2.0 * (double)v[k].x / (double)(GRID * 16);
            v[k].tu = (double)v[k].x / 256.0 / v[k].q;
            v[k].tv = (double)v[k].y / 512.0 / v[k].q;
            pos[k].x = v[k].x;
            pos[k].y = v[k].y;
        }
        CHECK(v9x_r2_split_triangle(&s, &d, v, pieces, &count) ==
              V9X_STATUS_OK);
        if (count > 1ul) {
            ++split;
        }
        if (count > most) {
            most = (unsigned int)count;
        }
        memset(drawn, 0, sizeof(drawn));
        for (piece = 0ul; piece < count; ++piece) {
            v9x_u32 written = 0ul;
            v9x_u32 traps_in = 0ul;
            v9x_status status = v9x_r2_build_piece(
                &s, &d, pieces + piece * 3ul, offsets, values,
                V9X_R2_DRAW_TRAPS_MAX * V9X_R2_DRAW_TRAP_DWORDS, &written,
                traps, &traps_in, 0);

            if (status == V9X_STATUS_UNSUPPORTED) {
                skipped = 1;
                break;
            }
            CHECK(status == V9X_STATUS_OK);
            draw_traps(traps, traps_in);
        }
        if (skipped) {
            continue;
        }
        bad = compare_coverage(pos);
        if (bad != 0u && failed < 3u) {
            printf("  split %u (%lu pieces): %u pixels wrong\n", index,
                   (unsigned long)count, bad);
        }
        if (bad != 0u) {
            ++failed;
        }
    }
    printf("  rage2 draw: %u of 400 perspective triangles split, at most %u"
           " pieces\n", split, most);
    CHECK(failed == 0u);
    CHECK(split > 20u);
    CHECK(most <= V9X_R2_DRAW_SPLIT_MAX);
}

unsigned int v9x_run_rage2_draw_tests(void)
{
    test_policy();
    test_state();
    test_tall_triangles();
    test_split_coverage();
    return failures;
}
