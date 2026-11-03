#include <stdio.h>
#include <string.h>

#include "velocity9x/nv3_3d.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define D3D_USER (V9X_NV3_USER + V9X_NV3_SUB_D3D * V9X_NV3_SUBCHANNEL_STEP)

static v9x_u32 list_value(const struct v9x_nv3_write *list, v9x_u32 count,
                          v9x_u32 offset)
{
    v9x_u32 index;
    v9x_u32 value = 0xdeadbeeful;

    for (index = 0u; index < count; ++index) {
        if (list[index].offset == offset) {
            value = list[index].value;
        }
    }
    return value;
}

/* rivafb's object and the whole-memory DMA object /d3dstate's BigDma case
 * drew through, at 8 MiB (M-7V636C) and 4 MiB. */
static void test_object(void)
{
    struct v9x_nv3_3d_object object;
    struct v9x_nv3_write entry;

    CHECK(v9x_nv3_3d_build_object(0x800000ul, &object) == V9X_STATUS_OK);
    CHECK(object.ramin_count == 6u);
    CHECK(list_value(object.ramin, object.ramin_count, 0x0930ul) ==
          0x80000013ul);
    CHECK(list_value(object.ramin, object.ramin_count, 0x0934ul) ==
          0x00d70347ul);
    CHECK(list_value(object.ramin, object.ramin_count, 0x3470ul) ==
          0x10419208ul);
    CHECK(list_value(object.ramin, object.ramin_count, 0x3474ul) ==
          0x00000400ul);
    CHECK(list_value(object.ramin, object.ramin_count, 0x4000ul) ==
          0x00010000ul);
    CHECK(list_value(object.ramin, object.ramin_count, 0x4004ul) ==
          0x007ffffful);
    CHECK(object.entry_count == 0x800ul);
    CHECK(object.bind.offset == D3D_USER && object.bind.value == 0x80000013ul);

    CHECK(v9x_nv3_3d_build_object(0x400000ul, &object) == V9X_STATUS_OK);
    CHECK(list_value(object.ramin, object.ramin_count, 0x4004ul) ==
          0x003ffffful);
    CHECK(object.entry_count == 0x400ul);

    CHECK(v9x_nv3_3d_build_object(0ul, &object) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_nv3_3d_build_object(0x800800ul, &object) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_nv3_3d_build_object(0x1000000ul, &object) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_nv3_3d_build_object(0x800000ul, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);

    /* The page the BigDma texture (20F000h) sat in. */
    entry = v9x_nv3_3d_dma_entry(0x20ful);
    CHECK(entry.offset == 0x4000ul + 8ul + 0x20ful * 4ul);
    CHECK(entry.value == 0x0020f003ul);
    entry = v9x_nv3_3d_dma_entry(0ul);
    CHECK(entry.offset == 0x4008ul && entry.value == 3ul);
}

/* Surfaces 2 and 3 as the probe placed them: colour at the scratch window,
 * zeta 8000h on, both at the 1600-byte pitch. */
static void probe_target(struct v9x_nv3_3d_target *target)
{
    target->vram_bytes = 0x800000ul;
    target->colour_offset = 0x200000ul;
    target->colour_pitch_bytes = 1600ul;
    target->zeta_offset = 0x208000ul;
    target->zeta_pitch_bytes = 1600ul;
    target->width = 800ul;
    target->height = 16ul;
}

static void test_target(void)
{
    struct v9x_nv3_3d_target target;
    struct v9x_nv3_op op;

    probe_target(&target);
    CHECK(v9x_nv3_3d_build_target(&target, &op) == V9X_STATUS_OK);
    CHECK(op.surface_count == 4u);
    CHECK(list_value(op.surface, 4u, V9X_NV3_PGRAPH_SURF_OFFSET + 8ul) ==
          0x200000ul);
    CHECK(list_value(op.surface, 4u, V9X_NV3_PGRAPH_SURF_PITCH + 8ul) ==
          1600ul);
    CHECK(list_value(op.surface, 4u, V9X_NV3_PGRAPH_SURF_OFFSET + 12ul) ==
          0x208000ul);
    CHECK(list_value(op.surface, 4u, V9X_NV3_PGRAPH_SURF_PITCH + 12ul) ==
          1600ul);
    /* The clip as the probe had it: 0,0 and 800 x 16. */
    CHECK(op.method_count == 2u);
    CHECK(op.methods[0].offset == V9X_NV3_USER + V9X_NV3_SUB_CLIP *
          V9X_NV3_SUBCHANNEL_STEP + V9X_NV3_M_CLIP_POINT &&
          op.methods[0].value == 0ul);
    CHECK(op.methods[1].offset == V9X_NV3_USER + V9X_NV3_SUB_CLIP *
          V9X_NV3_SUBCHANNEL_STEP + V9X_NV3_M_CLIP_SIZE &&
          op.methods[1].value == (800ul | (16ul << 16)));

    /* Alignment, the 4 MiB surface limit, the pitch field, the end. */
    probe_target(&target);
    target.colour_offset = 0x200008ul;
    CHECK(v9x_nv3_3d_build_target(&target, &op) ==
          V9X_STATUS_INVALID_ARGUMENT);
    probe_target(&target);
    target.zeta_pitch_bytes = 1608ul;
    CHECK(v9x_nv3_3d_build_target(&target, &op) ==
          V9X_STATUS_INVALID_ARGUMENT);
    probe_target(&target);
    target.colour_pitch_bytes = 1400ul; /* below 800 x 2 */
    CHECK(v9x_nv3_3d_build_target(&target, &op) ==
          V9X_STATUS_INVALID_ARGUMENT);
    probe_target(&target);
    target.colour_offset = 0x3ff000ul;  /* 16 lines run past 4 MiB */
    CHECK(v9x_nv3_3d_build_target(&target, &op) == V9X_STATUS_UNSUPPORTED);
    probe_target(&target);
    target.colour_pitch_bytes = 0x2000ul;
    CHECK(v9x_nv3_3d_build_target(&target, &op) == V9X_STATUS_UNSUPPORTED);
    probe_target(&target);
    target.height = 0ul;
    CHECK(v9x_nv3_3d_build_target(&target, &op) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_nv3_3d_build_target(0, &op) == V9X_STATUS_INVALID_ARGUMENT);
}

static void base_state(struct v9x_nv3_3d_state *state)
{
    memset(state, 0, sizeof(*state));
    state->vram_bytes = 0x800000ul;
    state->texture_offset = 0x20f000ul;
    state->texture_format = V9X_NV3_TEX_A1R5G5B5;
    state->texture_log2 = 3ul;
    state->filter = V9X_NV3_FILTER_POINT;
    state->wrap_u = V9X_NV3_WRAP;
    state->wrap_v = V9X_NV3_WRAP;
    state->z_func = 4ul;
    state->alpha_func = V9X_NV3_CMP_ALWAYS;
    state->blend = V9X_NV3_BLEND_SRC_ALPHA;
}

static v9x_u32 method(const struct v9x_nv3_3d_words *words, v9x_u32 m)
{
    return list_value(words->methods, 6u, D3D_USER + m);
}

/* The CONFIG words /d3dstate drew with, less the ROP ADD the probe used to
 * show its source over a cleared surface. */
static void test_state(void)
{
    struct v9x_nv3_3d_state state;
    struct v9x_nv3_3d_words words;

    /* BlendHalf: source alpha blend, no Z, the 8x8 A1R5G5B5 texture. */
    base_state(&state);
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(words.methods[0].offset == D3D_USER + V9X_NV3_M_D3D_TEX_OFFSET);
    CHECK(words.methods[1].offset == D3D_USER + V9X_NV3_M_D3D_TEX_FORMAT);
    CHECK(words.methods[2].offset == D3D_USER + V9X_NV3_M_D3D_FILTER);
    CHECK(words.methods[3].offset == D3D_USER + V9X_NV3_M_D3D_FOG_COLOUR);
    CHECK(words.methods[4].offset == D3D_USER + V9X_NV3_M_D3D_CONFIG);
    CHECK(words.methods[5].offset == D3D_USER + V9X_NV3_M_D3D_ALPHA);
    CHECK(method(&words, V9X_NV3_M_D3D_TEX_OFFSET) == 0x20f000ul);
    CHECK(method(&words, V9X_NV3_M_D3D_TEX_FORMAT) == 0x33000000ul);
    CHECK(method(&words, V9X_NV3_M_D3D_FILTER) == 0ul);
    CHECK(method(&words, V9X_NV3_M_D3D_CONFIG) == 0x04081150ul);
    CHECK(method(&words, V9X_NV3_M_D3D_ALPHA) == 0x00000800ul);
    CHECK(words.colour_or == 0ul && words.fog == 0);

    /* Opaque: the same blend with alpha made 1, texels read X1R5G5B5 and
     * every vertex's alpha FFh. */
    state.blend = V9X_NV3_BLEND_OPAQUE;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(method(&words, V9X_NV3_M_D3D_TEX_FORMAT) == 0x33100000ul);
    CHECK(method(&words, V9X_NV3_M_D3D_CONFIG) == 0x04081150ul);
    CHECK(words.colour_or == 0xff000000ul);

    /* Opaque R5G6B5 has no alpha to hide. */
    state.texture_format = V9X_NV3_TEX_R5G6B5;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(method(&words, V9X_NV3_M_D3D_TEX_FORMAT) == 0x33300000ul);

    /* Opaque A4R4G4B4 has no alpha-less twin: refused. */
    state.texture_format = V9X_NV3_TEX_A4R4G4B4;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_UNSUPPORTED);

    /* Opaque with the alpha test keeps A1R5G5B5's alpha: what passes has
     * alpha 1, which blends as the source alone. */
    base_state(&state);
    state.blend = V9X_NV3_BLEND_OPAQUE;
    state.alpha_test = 1;
    state.alpha_func = 5ul;
    state.alpha_reference = 0x7ful;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(method(&words, V9X_NV3_M_D3D_TEX_FORMAT) == 0x33000000ul);
    CHECK(method(&words, V9X_NV3_M_D3D_CONFIG) == 0x01081150ul);
    CHECK(method(&words, V9X_NV3_M_D3D_ALPHA) == 0x0000057ful);
    CHECK(words.colour_or == 0xff000000ul);
    state.texture_format = V9X_NV3_TEX_A4R4G4B4;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_UNSUPPORTED);

    /* ZLess: Z_TEST gates colour and zeta. */
    base_state(&state);
    state.z_enable = 1;
    state.z_write = 1;
    state.z_func = V9X_NV3_CMP_LESS;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(method(&words, V9X_NV3_M_D3D_CONFIG) == 0x03321150ul);
    state.z_write = 0;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(method(&words, V9X_NV3_M_D3D_CONFIG) == 0x03021150ul);

    /* Z and the alpha test together: neither written unless both pass. */
    state.z_write = 1;
    state.alpha_test = 1;
    state.alpha_func = 5ul;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(method(&words, V9X_NV3_M_D3D_CONFIG) == 0x02221150ul);

    /* The other blends, bilinear, clamp, colour key, fog. */
    base_state(&state);
    state.blend = V9X_NV3_BLEND_ADD;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(method(&words, V9X_NV3_M_D3D_CONFIG) == 0x14081150ul);
    state.blend = V9X_NV3_BLEND_MODULATE;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(method(&words, V9X_NV3_M_D3D_CONFIG) == 0x64081150ul);
    CHECK(words.premultiply == 0);

    /* SRCALPHA, ONE: the add, A1R5G5B5 read as X1R5G5B5, the vertex
     * colour scaled by its alpha; refused with fog. */
    state.blend = V9X_NV3_BLEND_ADD_SRC_ALPHA;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(method(&words, V9X_NV3_M_D3D_CONFIG) == 0x14081150ul);
    CHECK(method(&words, V9X_NV3_M_D3D_TEX_FORMAT) == 0x33100000ul);
    CHECK(words.premultiply == 1 && words.colour_or == 0ul);
    state.fog = 1;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_UNSUPPORTED);
    state.fog = 0;
    state.blend = 5ul;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_UNSUPPORTED);

    /* A chain, 128x128 down to 8x8: SIZE_MIN 3, the level bias; one level
     * of 0 or 1 sends neither. Below 8x8 the chip's levels overlap. */
    base_state(&state);
    state.texture_log2 = 7ul;
    state.texture_levels = 5ul;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(method(&words, V9X_NV3_M_D3D_TEX_FORMAT) == 0x73000000ul);
    CHECK(method(&words, V9X_NV3_M_D3D_FILTER) == 0x00f40000ul);
    state.texture_levels = 1ul;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(method(&words, V9X_NV3_M_D3D_TEX_FORMAT) == 0x77000000ul);
    CHECK(method(&words, V9X_NV3_M_D3D_FILTER) == 0ul);
    state.texture_levels = 6ul;
    CHECK(v9x_nv3_3d_build_state(&state, &words) ==
          V9X_STATUS_INVALID_ARGUMENT);
    state.texture_log2 = 3ul;
    state.texture_levels = 2ul;
    CHECK(v9x_nv3_3d_build_state(&state, &words) ==
          V9X_STATUS_INVALID_ARGUMENT);
    /* The chain has to fit in video memory, all of it. */
    state.texture_log2 = 7ul;
    state.texture_levels = 5ul;
    state.texture_offset = 0x800000ul - 0x8000ul;
    CHECK(v9x_nv3_3d_build_state(&state, &words) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_nv3_3d_chain_bytes(7ul, 5ul) == 43648ul);
    CHECK(v9x_nv3_3d_chain_bytes(3ul, 0ul) == 128ul);
    CHECK(v9x_nv3_3d_chain_bytes(3ul, 1ul) == 128ul);

    base_state(&state);
    state.filter = V9X_NV3_FILTER_LINEAR;
    state.wrap_v = V9X_NV3_CLAMP;
    state.colour_key = 1;
    state.colour_key_texel = 0x9080ul;
    state.fog = 1;
    state.fog_colour = 0x00123456ul;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(method(&words, V9X_NV3_M_D3D_CONFIG) == 0x040811d2ul);
    CHECK(method(&words, V9X_NV3_M_D3D_TEX_FORMAT) == 0x33019080ul);
    CHECK(method(&words, V9X_NV3_M_D3D_FOG_COLOUR) == 0x00123456ul);
    CHECK(words.fog == 1);

    /* The largest texture, then what the fields cannot hold. */
    base_state(&state);
    state.texture_offset = 0ul;
    state.texture_log2 = 11ul;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(method(&words, V9X_NV3_M_D3D_TEX_FORMAT) == 0xbb000000ul);
    state.texture_log2 = 12ul;
    CHECK(v9x_nv3_3d_build_state(&state, &words) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_state(&state);
    state.texture_offset = 0x7ff000ul;
    state.texture_log2 = 6ul;    /* 8 KiB, past the end */
    CHECK(v9x_nv3_3d_build_state(&state, &words) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_state(&state);
    state.texture_offset = 0x20f001ul;
    CHECK(v9x_nv3_3d_build_state(&state, &words) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_state(&state);
    state.wrap_u = 0ul;
    CHECK(v9x_nv3_3d_build_state(&state, &words) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_state(&state);
    state.z_enable = 1;
    state.z_func = 9ul;
    CHECK(v9x_nv3_3d_build_state(&state, &words) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_state(&state);
    state.alpha_test = 1;
    state.alpha_func = 5ul;
    state.alpha_reference = 256ul;
    CHECK(v9x_nv3_3d_build_state(&state, &words) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_state(&state);
    state.texture_format = 4ul;
    CHECK(v9x_nv3_3d_build_state(&state, &words) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_nv3_3d_build_state(0, &words) == V9X_STATUS_INVALID_ARGUMENT);
}

/* The probe's specular words, and Direct3D's fog factor turned round. */
static void test_vertex(void)
{
    static const v9x_u32 v2_others[2] = { 0ul, 1ul };
    static const v9x_u32 v3_others[2] = { 1ul, 2ul };
    static const v9x_u32 same[2] = { 1ul, 1ul };
    struct v9x_nv3_3d_state state;
    struct v9x_nv3_3d_words words;
    struct v9x_nv3_3d_vertex vertex;
    struct v9x_nv3_write methods[V9X_NV3_D3D_VERTEX_METHODS];
    v9x_u32 index;

    base_state(&state);
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(v9x_nv3_3d_specular(0ul, 0, &words, 0xff000000ul) == 0ul);
    CHECK(v9x_nv3_3d_specular(1ul, 0, &words, 0xff000000ul) ==
          0x00111111ul);
    CHECK(v9x_nv3_3d_specular(2ul, v2_others, &words, 0ul) == 0x00222102ul);
    CHECK(v9x_nv3_3d_specular(3ul, v3_others, &words, 0ul) == 0x00333213ul);

    /* Fog on: Direct3D's 255 (no fog) is the chip's 0, its 0 all fog. */
    state.fog = 1;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(v9x_nv3_3d_specular(2ul, v2_others, &words, 0xff123456ul) ==
          0x00222102ul);
    CHECK(v9x_nv3_3d_specular(2ul, v2_others, &words, 0x00123456ul) ==
          0xff222102ul);
    CHECK(v9x_nv3_3d_specular(2ul, v2_others, &words, 0x7f000000ul) ==
          0x80222102ul);

    /* A whole vertex: the probe's v1, opaque, so its alpha is forced. */
    state.fog = 0;
    state.blend = V9X_NV3_BLEND_OPAQUE;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_OK);
    vertex.sx = 0x42900000ul;   /* 72.0 */
    vertex.sy = 0x40000000ul;   /* 2.0 */
    vertex.sz = 0x3f000000ul;
    vertex.rhw = 0x3f800000ul;
    vertex.colour = 0x00ff0000ul;
    vertex.specular = 0ul;
    vertex.tu = 0x3f800000ul;
    vertex.tv = 0ul;
    CHECK(v9x_nv3_3d_build_vertex(&vertex, 1ul, 0, &words, methods) ==
          V9X_STATUS_OK);
    for (index = 0u; index < V9X_NV3_D3D_VERTEX_METHODS; ++index) {
        CHECK(methods[index].offset ==
              D3D_USER + V9X_NV3_M_D3D_VERTEX + index * 4ul);
    }
    CHECK(methods[0].value == 0x00111111ul);
    CHECK(methods[1].value == 0xffff0000ul);
    CHECK(methods[2].value == 0x42900000ul);
    CHECK(methods[3].value == 0x40000000ul);
    CHECK(methods[4].value == 0x3f000000ul);
    CHECK(methods[5].value == 0x3f800000ul);
    CHECK(methods[6].value == 0x3f800000ul);
    CHECK(methods[7].value == 0ul);

    /* A 64 x 16 texture in a 64 x 64 square: v scaled by 1/4 through the
     * exponent, u unchanged; zero and tiny values go to zero. */
    base_state(&state);
    state.v_shift = 2ul;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(words.u_shift == 0ul && words.v_shift == 2ul);
    vertex.tu = 0x3f800000ul;   /* 1.0 */
    vertex.tv = 0x3f800000ul;
    CHECK(v9x_nv3_3d_build_vertex(&vertex, 0ul, 0, &words, methods) ==
          V9X_STATUS_OK);
    CHECK(methods[6].value == 0x3f800000ul);
    CHECK(methods[7].value == 0x3e800000ul);    /* 0.25 */
    vertex.tv = 0xc0000000ul;   /* -2.0 */
    CHECK(v9x_nv3_3d_build_vertex(&vertex, 0ul, 0, &words, methods) ==
          V9X_STATUS_OK);
    CHECK(methods[7].value == 0xbf000000ul);    /* -0.5 */
    vertex.tv = 0ul;
    CHECK(v9x_nv3_3d_build_vertex(&vertex, 0ul, 0, &words, methods) ==
          V9X_STATUS_OK);
    CHECK(methods[7].value == 0ul);
    vertex.tv = 0x00800000ul;   /* the smallest normal */
    CHECK(v9x_nv3_3d_build_vertex(&vertex, 0ul, 0, &words, methods) ==
          V9X_STATUS_OK);
    CHECK(methods[7].value == 0ul);
    state.v_shift = 12ul;
    CHECK(v9x_nv3_3d_build_state(&state, &words) ==
          V9X_STATUS_INVALID_ARGUMENT);
    state.v_shift = 4ul;        /* 1/16 of an 8-texel side */
    CHECK(v9x_nv3_3d_build_state(&state, &words) ==
          V9X_STATUS_INVALID_ARGUMENT);

    /* DECAL and COPY: every vertex white and opaque. */
    base_state(&state);
    state.vertex_white = 1;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(words.colour_or == 0xfffffffful);

    /* SRCALPHA, ONE: RGB x A / 255, rounded, A kept. */
    base_state(&state);
    state.blend = V9X_NV3_BLEND_ADD_SRC_ALPHA;
    CHECK(v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_OK);
    vertex.colour = 0x80ff4001ul;
    CHECK(v9x_nv3_3d_build_vertex(&vertex, 0ul, 0, &words, methods) ==
          V9X_STATUS_OK);
    CHECK(methods[1].value == 0x80802001ul);
    vertex.colour = 0xffff8001ul;
    CHECK(v9x_nv3_3d_build_vertex(&vertex, 0ul, 0, &words, methods) ==
          V9X_STATUS_OK);
    CHECK(methods[1].value == 0xffff8001ul);
    vertex.colour = 0x00fffffful;
    CHECK(v9x_nv3_3d_build_vertex(&vertex, 0ul, 0, &words, methods) ==
          V9X_STATUS_OK);
    CHECK(methods[1].value == 0ul);

    /* Slots past 15, and triangles that would not draw. */
    CHECK(v9x_nv3_3d_build_vertex(&vertex, 16ul, 0, &words, methods) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_nv3_3d_build_vertex(&vertex, 2ul, same, &words, methods) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_nv3_3d_build_vertex(&vertex, 1ul, v2_others, &words,
                                  methods) == V9X_STATUS_INVALID_ARGUMENT);
}

/* Morton order with u in the even bits, as case TexelZohCorner read it. */
static void test_swizzle(void)
{
    CHECK(v9x_nv3_3d_swizzle(0ul, 0ul, 3ul) == 0ul);
    CHECK(v9x_nv3_3d_swizzle(1ul, 0ul, 3ul) == 1ul);
    CHECK(v9x_nv3_3d_swizzle(2ul, 0ul, 3ul) == 4ul);
    CHECK(v9x_nv3_3d_swizzle(4ul, 0ul, 3ul) == 16ul);
    CHECK(v9x_nv3_3d_swizzle(7ul, 0ul, 3ul) == 21ul);
    CHECK(v9x_nv3_3d_swizzle(0ul, 1ul, 3ul) == 2ul);
    CHECK(v9x_nv3_3d_swizzle(2ul, 1ul, 3ul) == 6ul);
    CHECK(v9x_nv3_3d_swizzle(0ul, 4ul, 3ul) == 32ul);
    CHECK(v9x_nv3_3d_swizzle(7ul, 7ul, 3ul) == 63ul);
    CHECK(v9x_nv3_3d_swizzle(2047ul, 2047ul, 11ul) == 0x3ffffful);
    /* Coordinates past the side wrap inside it. */
    CHECK(v9x_nv3_3d_swizzle(9ul, 0ul, 3ul) == 1ul);
}

/* A4R4G4B4 to X1R5G5B5 for the opaque copy: alpha gone, each nibble
 * widened by repeating its top bit. */
static void test_opaque_texel(void)
{
    CHECK(v9x_nv3_3d_opaque_texel(0xf000u) == 0x0000u);
    CHECK(v9x_nv3_3d_opaque_texel(0x0fffu) == 0x7fffu);
    CHECK(v9x_nv3_3d_opaque_texel(0x0f00u) == 0x7c00u);
    CHECK(v9x_nv3_3d_opaque_texel(0x00f0u) == 0x03e0u);
    CHECK(v9x_nv3_3d_opaque_texel(0x000fu) == 0x001fu);
    CHECK(v9x_nv3_3d_opaque_texel(0x8888u) == 0x4631u);
    CHECK(v9x_nv3_3d_opaque_texel(0x7777u) == 0x39ceu);
}

unsigned int v9x_run_nv3_3d_tests(void)
{
    failures = 0u;
    test_object();
    test_target();
    test_state();
    test_vertex();
    test_swizzle();
    test_opaque_texel();
    return failures;
}
