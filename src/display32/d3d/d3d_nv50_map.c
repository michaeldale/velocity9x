/*
 * The neutral draw description to the GT218's Tesla 3D state; see
 * d3d_nv50_map.h. The codes are nv50_d3d.h's.
 */
#include "d3d_nv50_map.h"
#include "velocity9x/r3d_abi.h"

#define V9X_D3D_NV50_ALPHA_REF_MASK 0xfful

/* The colour mask's nibbles (Mesa nv50_colormask). */
#define V9X_D3D_NV50_MASK_RED       0x00000001ul
#define V9X_D3D_NV50_MASK_GREEN     0x00000010ul
#define V9X_D3D_NV50_MASK_BLUE      0x00000100ul

/* Direct3D's compare functions, NEVER to ALWAYS, are the 3D class's
 * 200h to 207h in the same order. */
static int v9x_d3d_nv50_compare(v9x_u32 func, v9x_u32 *code)
{
    if (func < V9X_R3D_CMP_NEVER || func > V9X_R3D_CMP_ALWAYS) {
        return 0;
    }
    *code = V9X_NV50_D3D_CMP_NEVER + (func - V9X_R3D_CMP_NEVER);
    return 1;
}

/*
 * Direct3D's blend factors to the class's. Destination alpha is refused:
 * neither target holds alpha the application put there (RGB565 has none,
 * and an XRGB8888 target's top byte is whatever was last written).
 * SRCALPHASAT is a source factor only.
 */
static int v9x_d3d_nv50_factor(v9x_u32 factor, int source, v9x_u32 *code)
{
    switch (factor) {
    case V9X_R3D_BLEND_ZERO:
        *code = V9X_NV50_D3D_BLEND_ZERO;
        return 1;
    case V9X_R3D_BLEND_ONE:
        *code = V9X_NV50_D3D_BLEND_ONE;
        return 1;
    case V9X_R3D_BLEND_SRCCOLOR:
        *code = V9X_NV50_D3D_BLEND_SRC_COLOR;
        return 1;
    case V9X_R3D_BLEND_INVSRCCOLOR:
        *code = V9X_NV50_D3D_BLEND_ONE_MINUS_SRC_COLOR;
        return 1;
    case V9X_R3D_BLEND_SRCALPHA:
        *code = V9X_NV50_D3D_BLEND_SRC_ALPHA;
        return 1;
    case V9X_R3D_BLEND_INVSRCALPHA:
        *code = V9X_NV50_D3D_BLEND_ONE_MINUS_SRC_ALPHA;
        return 1;
    case V9X_R3D_BLEND_DESTCOLOR:
        *code = V9X_NV50_D3D_BLEND_DST_COLOR;
        return 1;
    case V9X_R3D_BLEND_INVDESTCOLOR:
        *code = V9X_NV50_D3D_BLEND_ONE_MINUS_DST_COLOR;
        return 1;
    case V9X_R3D_BLEND_SRCALPHASAT:
        if (source) {
            *code = V9X_NV50_D3D_BLEND_SRC_ALPHA_SATURATE;
            return 1;
        }
        return 0;
    }
    return 0;
}

/*
 * A filter as linear or not, level 0 alone: there are no mip chains here
 * yet, so a mip filter reads as its filter within the level (MIPNEAREST
 * and LINEARMIPNEAREST point, MIPLINEAR and LINEARMIPLINEAR bilinear, as
 * d3d_eg_map.c reads them). A magnification filter is NEAREST or LINEAR.
 */
static int v9x_d3d_nv50_filter(v9x_u32 filter, int magnify, v9x_u32 *linear)
{
    switch (filter) {
    case V9X_R3D_FILTER_NEAREST:
        *linear = 0ul;
        return 1;
    case V9X_R3D_FILTER_LINEAR:
        *linear = 1ul;
        return 1;
    case V9X_R3D_FILTER_MIPNEAREST:
    case V9X_R3D_FILTER_LINEARMIPNEAREST:
        *linear = 0ul;
        return !magnify;
    case V9X_R3D_FILTER_MIPLINEAR:
    case V9X_R3D_FILTER_LINEARMIPLINEAR:
        *linear = 1ul;
        return !magnify;
    }
    return 0;
}

static int v9x_d3d_nv50_address(v9x_u32 address, v9x_u32 *wrap)
{
    switch (address) {
    case V9X_R3D_ADDRESS_WRAP:
        *wrap = V9X_NV50_D3D_WRAP_REPEAT;
        return 1;
    case V9X_R3D_ADDRESS_MIRROR:
        *wrap = V9X_NV50_D3D_WRAP_MIRROR;
        return 1;
    case V9X_R3D_ADDRESS_CLAMP:
        *wrap = V9X_NV50_D3D_WRAP_CLAMP;
        return 1;
    }
    return 0;
}

/*
 * Direct3D's DX5 texture blends to a program: DECAL and COPY take the
 * texel (REPLACE); MODULATE and MODULATEALPHA multiply all four
 * channels, which for a texture without alpha keeps the vertex's (the
 * sampler reads alpha 1), as d3d_eg_map.c reasons.
 */
static int v9x_d3d_nv50_op(v9x_u32 op, v9x_u32 *program_op)
{
    switch (op) {
    case V9X_R3D_TEXOP_DECAL:
    case V9X_R3D_TEXOP_COPY:
        *program_op = V9X_NV50_D3D_OP_REPLACE;
        return 1;
    case V9X_R3D_TEXOP_MODULATE:
    case V9X_R3D_TEXOP_MODULATEALPHA:
        *program_op = V9X_NV50_D3D_OP_MODULATE;
        return 1;
    }
    return 0;
}

static v9x_u32 v9x_d3d_nv50_bytes_per_texel(v9x_u32 format)
{
    if (format == V9X_NV50_D3D_TEX_B8G8R8A8 ||
        format == V9X_NV50_D3D_TEX_B8G8R8X8) {
        return 4ul;
    }
    return 2ul;
}

/* A float's bits for ref / 255, the alpha test's reference. */
static v9x_u32 v9x_d3d_nv50_alpha_ref(v9x_u32 ref)
{
    union {
        float value;
        v9x_u32 bits;
    } converted;

    converted.value =
        (float)(long)(ref & V9X_D3D_NV50_ALPHA_REF_MASK) / 255.0f;
    return converted.bits;
}

/* A two-unit draw's unit combine (r3d_abi.h's colour and alpha ops, and
 * r3d.h's ADD) as nv50_d3d.h numbers them; 0 for one the programs do not
 * draw, MODULATE2X among them (the limits do not claim it). */
static int v9x_d3d_nv50_combine_ops(v9x_u32 color_op, v9x_u32 alpha_op,
                                    v9x_u32 *color, v9x_u32 *alpha)
{
    switch (color_op) {
    case V9X_R3D_ABI_COLOROP_REPLACE:
        *color = V9X_NV50_D3D_COLOR_REPLACE;
        break;
    case V9X_R3D_ABI_COLOROP_MODULATE:
        *color = V9X_NV50_D3D_COLOR_MODULATE;
        break;
    case V9X_R3D_ABI_COLOROP_DECALALPHA:
        *color = V9X_NV50_D3D_COLOR_DECAL;
        break;
    case V9X_R3D_COLOROP_ADD:
        *color = V9X_NV50_D3D_COLOR_ADD;
        break;
    default:
        return 0;
    }
    switch (alpha_op) {
    case V9X_R3D_ABI_ALPHAOP_FRAGMENT:
        *alpha = V9X_NV50_D3D_ALPHA_FRAGMENT;
        return 1;
    case V9X_R3D_ABI_ALPHAOP_REPLACE:
        *alpha = V9X_NV50_D3D_ALPHA_REPLACE;
        return 1;
    case V9X_R3D_ABI_ALPHAOP_MODULATE:
        *alpha = V9X_NV50_D3D_ALPHA_MODULATE;
        return 1;
    default:
        return 0;
    }
}

/* sampler's texture, resolved as texture, into tic and tsc. program_op is
 * the one-unit blend's variant op, or null for a unit of a two-unit draw,
 * whose combine is in color_op and alpha_op instead (r3d.h). */
static v9x_u32 v9x_d3d_nv50_map_texture(const V9X_R3D_DRAW *draw,
                                        const V9X_R3D_TEXTURE *sampler,
                                        const V9X_D3D_NV50_TEXTURE *texture,
                                        v9x_u32 *program_op, v9x_u32 *tic,
                                        v9x_u32 *tsc)
{
    v9x_u32 wrap;
    v9x_u32 mag;
    v9x_u32 min;

    if (texture == 0 || texture->format == V9X_D3D_NV50_TEXTURE_UNKNOWN) {
        return V9X_D3D_NV50_REFUSE_TEXTURE_FORMAT;
    }
    if (texture->width == 0ul || texture->height == 0ul ||
        texture->width > V9X_D3D_NV50_TEXTURE_SIDE_MAX ||
        texture->height > V9X_D3D_NV50_TEXTURE_SIDE_MAX ||
        (texture->pitch % V9X_D3D_NV50_PITCH_ALIGN) != 0ul ||
        texture->pitch <
            texture->width * v9x_d3d_nv50_bytes_per_texel(texture->format) ||
        (texture->offset % V9X_D3D_NV50_SURFACE_ALIGN) != 0ul) {
        return V9X_D3D_NV50_REFUSE_TEXTURE_SHAPE;
    }
    if (program_op != 0 && !v9x_d3d_nv50_op(sampler->op, program_op)) {
        return V9X_D3D_NV50_REFUSE_TEXTURE_OP;
    }
    /* The cylindrical WRAPU/WRAPV states have no sampler function. */
    if (sampler->wrap_u != 0ul || sampler->wrap_v != 0ul ||
        !v9x_d3d_nv50_address(sampler->address, &wrap)) {
        return V9X_D3D_NV50_REFUSE_TEXTURE_ADDRESS;
    }
    if (!v9x_d3d_nv50_filter(sampler->mag_filter, 1, &mag) ||
        !v9x_d3d_nv50_filter(sampler->min_filter, 0, &min)) {
        return V9X_D3D_NV50_REFUSE_TEXTURE_FILTER;
    }
    /* A colour key needs a kill instruction the encoder does not have. */
    if (draw->color_key_enable != 0ul && texture->has_key != 0ul) {
        return V9X_D3D_NV50_REFUSE_COLOR_KEY;
    }
    if (!v9x_nv50_d3d_texture(texture->format, texture->offset,
                              texture->pitch, texture->width,
                              texture->height, wrap, mag, min, tic, tsc)) {
        return V9X_D3D_NV50_REFUSE_TEXTURE_FORMAT;
    }
    return V9X_D3D_NV50_REFUSE_NONE;
}

v9x_u32 v9x_d3d_nv50_map_draw(const V9X_R3D_DRAW *draw,
                              const V9X_D3D_NV50_TEXTURE *texture,
                              const V9X_D3D_NV50_SHADOW_VIEW *shadow,
                              const v9x_u32 *fp_registers,
                              struct v9x_nv50_d3d_state *state,
                              v9x_u32 *tic, v9x_u32 *tsc,
                              v9x_u32 *with_specular)
{
    unsigned char *bytes = (unsigned char *)state;
    v9x_u32 bytes_per_pixel;
    v9x_u32 index;
    v9x_u32 code;
    v9x_u32 src;
    v9x_u32 dst;
    v9x_u32 reason;
    v9x_u32 program_op = V9X_NV50_D3D_OP_NONE;
    v9x_u32 lit;

    for (index = 0ul; index < sizeof(*state); ++index) {
        bytes[index] = 0u;
    }
    /* Levels in CPU memory are not sampled, and a second unit comes with
     * its coordinates or not at all: the fallback draws those. */
    if (draw->texture.levels != 0 || draw->texture1.levels != 0 ||
        (draw->texcoords1 == 0 && draw->texture1.object != 0) ||
        (draw->texcoords1 != 0 &&
         (draw->texture.object == 0 || draw->texture1.object == 0))) {
        return V9X_D3D_NV50_REFUSE_EXPLICIT;
    }

    /* The target: RGB565 or XRGB8888, pitch-linear, rows on 64 bytes and
     * the base on 256. */
    if (draw->target.format == V9X_R3D_FORMAT_RGB565) {
        bytes_per_pixel = 2ul;
        state->target_format = V9X_NV50_3D_RT_B5G6R5;
    } else if (draw->target.format == V9X_R3D_FORMAT_XRGB8888) {
        bytes_per_pixel = 4ul;
        state->target_format = V9X_NV50_3D_RT_BGRX8;
    } else {
        return V9X_D3D_NV50_REFUSE_TARGET;
    }
    if (draw->target.width == 0ul || draw->target.height == 0ul ||
        (draw->target.pitch % V9X_D3D_NV50_PITCH_ALIGN) != 0ul ||
        draw->target.pitch / bytes_per_pixel < draw->target.width ||
        (draw->target.offset % V9X_D3D_NV50_SURFACE_ALIGN) != 0ul) {
        return V9X_D3D_NV50_REFUSE_TARGET;
    }
    state->target.address = draw->target.offset;
    state->target.pitch = draw->target.pitch;
    state->target.width = draw->target.width;
    state->target.height = draw->target.height;
    state->scissor_right = draw->target.width;
    state->scissor_bottom = draw->target.height;
    /* The X byte of an XRGB8888 target is not the application's to have
     * drawn: alpha writes off, as d3d_eg_map.c does. */
    state->color_mask = V9X_NV50_D3D_MASK_RGB;

    /* An explicit draw's scissor must be a non-empty rectangle inside the
     * target; its write mask names the channels written. */
    if (draw->explicit_state != 0ul) {
        if (draw->scissor_left >= draw->scissor_right ||
            draw->scissor_top >= draw->scissor_bottom ||
            draw->scissor_right > draw->target.width ||
            draw->scissor_bottom > draw->target.height) {
            return V9X_D3D_NV50_REFUSE_EXPLICIT;
        }
        state->scissor_left = draw->scissor_left;
        state->scissor_top = draw->scissor_top;
        state->scissor_right = draw->scissor_right;
        state->scissor_bottom = draw->scissor_bottom;
        state->color_mask = 0ul;
        if ((draw->write_mask & V9X_D3D_NV50_WRITE_RED) != 0ul) {
            state->color_mask |= V9X_D3D_NV50_MASK_RED;
        }
        if ((draw->write_mask & V9X_D3D_NV50_WRITE_GREEN) != 0ul) {
            state->color_mask |= V9X_D3D_NV50_MASK_GREEN;
        }
        if ((draw->write_mask & V9X_D3D_NV50_WRITE_BLUE) != 0ul) {
            state->color_mask |= V9X_D3D_NV50_MASK_BLUE;
        }
    }

    /* A shadow holding the target takes the draw: the tiled colour, the
     * Z buffer's size, which must cover the target; the scissor above
     * stays the target's. */
    if (shadow != 0) {
        if (shadow->colour.width < draw->target.width ||
            shadow->colour.height < draw->target.height) {
            return V9X_D3D_NV50_REFUSE_TARGET;
        }
        state->target = shadow->colour;
    }

    /* Depth: a pitch-linear target cannot have a Z buffer, so a Z-tested
     * draw on a bound buffer needs its shadow (step 5b). With Z off the
     * draw needs no buffer, Direct3D's ZENABLE FALSE. */
    if (draw->depth_enable != 0ul && draw->depth.object != 0) {
        if (!v9x_d3d_nv50_compare(draw->depth_func, &code)) {
            return V9X_D3D_NV50_REFUSE_DEPTH_FUNC;
        }
        if (shadow == 0 || shadow->with_zeta == 0ul) {
            return V9X_D3D_NV50_REFUSE_DEPTH_SURFACE;
        }
        state->has_zeta = 1ul;
        state->zeta = shadow->zeta;
        state->zeta_format = shadow->zeta_format;
        state->depth_test = 1ul;
        state->depth_write = draw->depth_write != 0ul ? 1ul : 0ul;
        state->depth_func = code;
    }

    if (draw->blend_enable != 0ul) {
        if (!v9x_d3d_nv50_factor(draw->src_blend, 1, &src) ||
            !v9x_d3d_nv50_factor(draw->dst_blend, 0, &dst)) {
            return V9X_D3D_NV50_REFUSE_BLEND;
        }
        state->blend = 1ul;
        state->blend_src = src;
        state->blend_dst = dst;
    }
    if (draw->alpha_test_enable != 0ul) {
        if (!v9x_d3d_nv50_compare(draw->alpha_func, &code)) {
            return V9X_D3D_NV50_REFUSE_ALPHA_FUNC;
        }
        state->alpha_test = 1ul;
        state->alpha_func = code;
        state->alpha_ref = v9x_d3d_nv50_alpha_ref(draw->alpha_ref);
    }

    if (draw->texcoords1 != 0) {
        /* Two units: each unit's tables, then its combine as the program
         * numbers it. The colour key needs a kill the programs do not
         * have, as for one unit. */
        if (draw->color_key_enable != 0ul) {
            return V9X_D3D_NV50_REFUSE_COLOR_KEY;
        }
        for (index = 0ul; index < 2ul; ++index) {
            const V9X_R3D_TEXTURE *unit =
                index == 0ul ? &draw->texture : &draw->texture1;

            reason = v9x_d3d_nv50_map_texture(
                draw, unit, &texture[index], 0,
                tic + index * V9X_NV50_3D_TIC_WORDS,
                tsc + index * V9X_NV50_3D_TSC_WORDS);
            if (reason != V9X_D3D_NV50_REFUSE_NONE) {
                return reason;
            }
            if (!v9x_d3d_nv50_combine_ops(unit->color_op, unit->alpha_op,
                                          &state->combine.color[index],
                                          &state->combine.alpha[index])) {
                return V9X_D3D_NV50_REFUSE_TEXTURE_OP;
            }
        }
        state->textured = 1ul;
        state->two_units = 1ul;
        state->combine.specular = draw->specular_enable != 0ul ? 1ul : 0ul;
        state->combine.fog = draw->fog_enable != 0ul ? 1ul : 0ul;
        state->fog = state->combine.fog;
        state->fog_color = draw->fog_color;
        *with_specular = draw->specular_enable != 0ul ||
                         draw->fog_enable != 0ul ? 1ul : 0ul;
        return V9X_D3D_NV50_REFUSE_NONE;
    }
    if (draw->texture.object != 0) {
        reason = v9x_d3d_nv50_map_texture(draw, &draw->texture, texture,
                                          &program_op, tic, tsc);
        if (reason != V9X_D3D_NV50_REFUSE_NONE) {
            return reason;
        }
        state->textured = 1ul;
    }

    /* Specular and fog, vertex or table: the core leaves the fog factor
     * in the specular alpha either way (d3d_fog.h), so either sends the
     * specular colour with each vertex. */
    lit = draw->specular_enable != 0ul || draw->fog_enable != 0ul ? 1ul : 0ul;
    state->fp_variant = v9x_nv50_d3d_fp_variant(program_op,
                                                draw->specular_enable,
                                                draw->fog_enable);
    state->fp_registers = fp_registers[state->fp_variant];
    state->fog_color = draw->fog_color;
    *with_specular = lit;
    return V9X_D3D_NV50_REFUSE_NONE;
}

/* A draw with nothing set, for a shader stage: no colour key (a shader
 * draw states none), no blend op. Static, as HAL stacks are small. */
static const V9X_R3D_DRAW v9x_d3d_nv50_plain_draw;

v9x_u32 v9x_d3d_nv50_map_unit(const V9X_R3D_TEXTURE *sampler,
                              const V9X_D3D_NV50_TEXTURE *texture,
                              v9x_u32 *tic, v9x_u32 *tsc)
{
    return v9x_d3d_nv50_map_texture(&v9x_d3d_nv50_plain_draw, sampler,
                                    texture, 0, tic, tsc);
}

void v9x_d3d_nv50_tiled_layout(v9x_u32 width, v9x_u32 height, v9x_u32 bytes,
                               v9x_u32 *pitch, v9x_u32 *rows)
{
    *pitch = (width * bytes + V9X_D3D_NV50_TILE_ROW_BYTES - 1ul) &
             ~(V9X_D3D_NV50_TILE_ROW_BYTES - 1ul);
    *rows = (height + V9X_D3D_NV50_TILE_ROWS - 1ul) &
            ~(V9X_D3D_NV50_TILE_ROWS - 1ul);
}

int v9x_d3d_nv50_depth_fill_draw(const V9X_D3D_NV50_SHADOW_VIEW *shadow,
                                 v9x_u32 left, v9x_u32 top, v9x_u32 right,
                                 v9x_u32 bottom, v9x_u32 value,
                                 const v9x_u32 *fp_registers,
                                 struct v9x_nv50_d3d_state *state,
                                 V9X_R3D_VERTEX *vertices)
{
    unsigned char *bytes = (unsigned char *)state;
    v9x_u32 index;
    float z;

    if (left >= right || top >= bottom || right > shadow->zeta.width ||
        bottom > shadow->zeta.height) {
        return 0;
    }
    for (index = 0ul; index < sizeof(*state); ++index) {
        bytes[index] = 0u;
    }
    /* The shadow's colour names the target the draw needs and does not
     * write: every channel masked. */
    state->target = shadow->colour;
    state->target_format = V9X_NV50_3D_RT_BGRX8;
    state->scissor_left = left;
    state->scissor_top = top;
    state->scissor_right = right;
    state->scissor_bottom = bottom;
    state->color_mask = 0ul;
    state->has_zeta = 1ul;
    state->zeta = shadow->zeta;
    state->zeta_format = shadow->zeta_format;
    state->depth_test = 1ul;
    state->depth_write = 1ul;
    state->depth_func = V9X_NV50_D3D_CMP_ALWAYS;
    state->fp_variant = 0ul;
    state->fp_registers = fp_registers[0];

    /* In double, as d3d_eg_map.c's fill: z passes through as the window
     * z (/z:depth16 drew 0.25 and 0.75 so), and the far plane is exactly
     * 1.0. The rectangle's edges are pixel edges, which the engine's
     * centres at x + 0.5 already respect: no half-pixel move. */
    z = (float)((double)(value & 0xfffful) / 65535.0);
    for (index = 0ul; index < 6ul; ++index) {
        vertices[index].sz = z;
        vertices[index].rhw = 1.0f;
        vertices[index].color = 0ul;
        vertices[index].specular = 0ul;
        vertices[index].tu = 0.0f;
        vertices[index].tv = 0.0f;
    }
    vertices[0].sx = (float)(long)left;
    vertices[0].sy = (float)(long)top;
    vertices[1].sx = (float)(long)right;
    vertices[1].sy = (float)(long)top;
    vertices[2].sx = (float)(long)left;
    vertices[2].sy = (float)(long)bottom;
    vertices[3].sx = (float)(long)right;
    vertices[3].sy = (float)(long)top;
    vertices[4].sx = (float)(long)right;
    vertices[4].sy = (float)(long)bottom;
    vertices[5].sx = (float)(long)left;
    vertices[5].sy = (float)(long)bottom;
    return 1;
}
