/*
 * The neutral draw description to SiS 6326 state, texture and triangle
 * words; see d3d_sis6326_map.h. The measurements behind each mapping are in
 * docs\decisions\2026-10-05-sis6326-3d-*.md: first-triangle (direction,
 * sampling, ties), shading-and-depth (the 1/256 shift, Z16 to 1.0, the
 * compare and blend codes) and textures (formats, addressing, W as RHW,
 * filters and blend modes).
 */
#include "d3d_sis6326_map.h"

/* The driver's tie shift: vertices up and left by 1/256 pixel turn the
 * engine's bottom-right tie rule into Direct3D's top-left one. */
#define V9X_D3D_SIS_TIE_SHIFT      0.00390625f
/* Z16 stores z x 2^15 and wraps to 0 at 1.0: the largest single below it. */
#define V9X_D3D_SIS_Z_MAX_BITS     0x3f7ffffful
#define V9X_D3D_SIS_FLOAT_ONE_BITS 0x3f800000ul
#define V9X_D3D_SIS_FLOAT_EXPONENT 0x7f800000ul

/* The texture-blend colour modes (8A3Ch D[29:26]) the record measured. */
#define V9X_D3D_SIS_TBLEND_MODULATE     2ul
#define V9X_D3D_SIS_TBLEND_DECALMASK    8ul
#define V9X_D3D_SIS_TBLEND_MODULATEMASK 12ul
/* The masked modes read one Atex bit; Direct3D's masks test the MSB. */
#define V9X_D3D_SIS_MASK_ALPHA_MSB      7ul

/* Minification codes 3 and 4 (8A38h D[2:0]): nearest-level bilinear, and
 * nearest texel blended between levels (run I, 2026-10-05). */
#define V9X_D3D_SIS_MIN_LINEAR_IN_LEVEL   3ul
#define V9X_D3D_SIS_MIN_BLEND_LEVELS      4ul

#define V9X_D3D_SIS_MIN_MASK 0x00000007ul
#define V9X_D3D_SIS_TEXTURE_SIDE_MAX 512ul
#define V9X_D3D_SIS_RGB_MASK 0x00fffffful
/* The pitch field's 4-byte unit: the dummy texel's row. */
#define V9X_D3D_SIS_PITCH_UNIT 4ul

static v9x_u32 v9x_d3d_sis_bits(float value)
{
    union {
        float value;
        v9x_u32 bits;
    } converted;

    converted.value = value;
    return converted.bits;
}

static int v9x_d3d_sis_finite(float value)
{
    return (v9x_d3d_sis_bits(value) & V9X_D3D_SIS_FLOAT_EXPONENT) !=
           V9X_D3D_SIS_FLOAT_EXPONENT;
}

/* A power of two from 1 to 512, as its log2; -1 otherwise. */
static int v9x_d3d_sis_log2(v9x_u32 side)
{
    int log2 = 0;

    if (side == 0ul || side > V9X_D3D_SIS_TEXTURE_SIDE_MAX ||
        (side & (side - 1ul)) != 0ul) {
        return -1;
    }
    while ((1ul << log2) != side) {
        ++log2;
    }
    return log2;
}

/* Direct3D's compare functions are the engine's codes plus one. */
static int v9x_d3d_sis_compare(v9x_u32 func, v9x_u32 *code)
{
    if (func < V9X_R3D_CMP_NEVER || func > V9X_R3D_CMP_ALWAYS) {
        return 0;
    }
    *code = func - V9X_R3D_CMP_NEVER;
    return 1;
}

/* ZERO, ONE, SRCALPHA and INVSRCALPHA: the factors phase 2 measured. */
static int v9x_d3d_sis_blend_measured(v9x_u32 factor)
{
    return factor == V9X_R3D_BLEND_ZERO || factor == V9X_R3D_BLEND_ONE ||
           factor == V9X_R3D_BLEND_SRCALPHA ||
           factor == V9X_R3D_BLEND_INVSRCALPHA;
}

/*
 * Direct3D's blend factors are the engine's codes plus one. Only the
 * measured four are taken, on either side: the colour factors went out
 * once on the datasheet's word, and the V9XDDP run that drew SRC DESTCOLOR
 * hard-locked A8U4I5 a few steps later (boot 212, cause not established).
 */
static v9x_u32 v9x_d3d_sis_map_blend(const V9X_R3D_DRAW *draw,
                                     struct v9x_sis3d_state *state)
{
    v9x_u32 source;
    v9x_u32 destination;

    state->blend_source = V9X_SIS3D_BLEND_ONE;
    state->blend_destination = V9X_SIS3D_BLEND_ZERO;
    if (draw->blend_enable == 0ul) {
        return V9X_D3D_SIS_REFUSE_NONE;
    }
    if (!v9x_d3d_sis_blend_measured(draw->src_blend) ||
        !v9x_d3d_sis_blend_measured(draw->dst_blend)) {
        return V9X_D3D_SIS_REFUSE_BLEND;
    }
    source = draw->src_blend - V9X_R3D_BLEND_ZERO;
    destination = draw->dst_blend - V9X_R3D_BLEND_ZERO;
    state->enable |= V9X_SIS3D_ENABLE_BLEND;
    state->blend_source = source;
    state->blend_destination = destination;
    return V9X_D3D_SIS_REFUSE_NONE;
}

/*
 * Minification for a chain of `levels`: a mip filter on a single level
 * keeps its within-level half. Magnification is nearest or bilinear.
 */
static int v9x_d3d_sis_map_filter(const V9X_R3D_TEXTURE *sampler,
                                  v9x_u32 levels, v9x_u32 *filter)
{
    v9x_u32 min;
    int mip = levels > 1ul;

    switch (sampler->min_filter) {
    case V9X_R3D_FILTER_NEAREST:
        min = V9X_SIS3D_MIN_NEAREST;
        break;
    case V9X_R3D_FILTER_LINEAR:
        min = V9X_SIS3D_MIN_LINEAR;
        break;
    case V9X_R3D_FILTER_MIPNEAREST:
        min = mip ? V9X_SIS3D_MIN_NEAREST_MIP_NEAREST
                  : V9X_SIS3D_MIN_NEAREST;
        break;
    case V9X_R3D_FILTER_MIPLINEAR:
        min = mip ? V9X_D3D_SIS_MIN_LINEAR_IN_LEVEL : V9X_SIS3D_MIN_LINEAR;
        break;
    case V9X_R3D_FILTER_LINEARMIPNEAREST:
        min = mip ? V9X_D3D_SIS_MIN_BLEND_LEVELS : V9X_SIS3D_MIN_NEAREST;
        break;
    case V9X_R3D_FILTER_LINEARMIPLINEAR:
        min = mip ? V9X_SIS3D_MIN_LINEAR_MIP_LINEAR : V9X_SIS3D_MIN_LINEAR;
        break;
    default:
        return 0;
    }
    /* Magnification has no levels to choose between, so a mip filter set
     * there takes its within-level half, as d3d_i9xx.c folds it. Final
     * Reality's full run had all 17,192 of its refusals here (A8U4I5 boot
     * 290). */
    switch (sampler->mag_filter) {
    case V9X_R3D_FILTER_LINEAR:
    case V9X_R3D_FILTER_MIPLINEAR:
    case V9X_R3D_FILTER_LINEARMIPLINEAR:
        *filter = V9X_SIS3D_MAG_LINEAR | min;
        return 1;
    case V9X_R3D_FILTER_NEAREST:
    case V9X_R3D_FILTER_MIPNEAREST:
    case V9X_R3D_FILTER_LINEARMIPNEAREST:
        *filter = min;
        return 1;
    }
    return 0;
}

/*
 * The texture-blend modes measured on 2026-10-05: 0 Ctex, 2 Cpix x Ctex,
 * 4 a lerp by Atex, 8 and 12 the masked decal and modulate. The alpha half
 * (8A3Ch D[25:24]) follows Direct3D's definitions; it was not measured.
 */
static int v9x_d3d_sis_map_op(v9x_u32 op, int has_alpha,
                              struct v9x_sis3d_texture *texture)
{
    texture->blend_mask_bit = 0ul;
    switch (op) {
    case V9X_R3D_TEXOP_DECAL:
    case V9X_R3D_TEXOP_COPY:
        texture->colour_mode = V9X_SIS3D_TBLEND_CTEX;
        texture->alpha_mode = V9X_SIS3D_TBLEND_ATEX;
        return 1;
    case V9X_R3D_TEXOP_MODULATE:
        texture->colour_mode = V9X_D3D_SIS_TBLEND_MODULATE;
        texture->alpha_mode = has_alpha ? V9X_SIS3D_TBLEND_ATEX
                                        : V9X_SIS3D_TBLEND_APIX;
        return 1;
    case V9X_R3D_TEXOP_DECALALPHA:
        texture->colour_mode = V9X_SIS3D_TBLEND_DECALALPHA;
        texture->alpha_mode = V9X_SIS3D_TBLEND_APIX;
        return 1;
    case V9X_R3D_TEXOP_MODULATEALPHA:
        texture->colour_mode = V9X_D3D_SIS_TBLEND_MODULATE;
        texture->alpha_mode = V9X_SIS3D_TBLEND_APIX_ATEX;
        return 1;
    case V9X_R3D_TEXOP_DECALMASK:
        texture->colour_mode = V9X_D3D_SIS_TBLEND_DECALMASK;
        texture->alpha_mode = V9X_SIS3D_TBLEND_APIX;
        texture->blend_mask_bit = V9X_D3D_SIS_MASK_ALPHA_MSB;
        return 1;
    case V9X_R3D_TEXOP_MODULATEMASK:
        texture->colour_mode = V9X_D3D_SIS_TBLEND_MODULATEMASK;
        texture->alpha_mode = V9X_SIS3D_TBLEND_APIX;
        texture->blend_mask_bit = V9X_D3D_SIS_MASK_ALPHA_MSB;
        return 1;
    }
    return 0;
}

static v9x_u32 v9x_d3d_sis_map_texture(const V9X_R3D_DRAW *draw,
                                       const V9X_D3D_SIS_TEXTURE *resolved,
                                       v9x_u32 vram_bytes,
                                       struct v9x_sis3d_texture *texture)
{
    const V9X_R3D_TEXTURE *sampler = &draw->texture;
    int log2_width;
    int log2_height;
    v9x_u32 level;

    if (resolved == 0 || resolved->format == V9X_D3D_SIS_TEXTURE_UNKNOWN) {
        return V9X_D3D_SIS_REFUSE_TEXTURE_FORMAT;
    }
    log2_width = v9x_d3d_sis_log2(resolved->width);
    log2_height = v9x_d3d_sis_log2(resolved->height);
    if (log2_width < 0 || log2_height < 0 || resolved->levels == 0ul ||
        resolved->levels > V9X_D3D_SIS_TEXTURE_LEVELS) {
        return V9X_D3D_SIS_REFUSE_TEXTURE_SHAPE;
    }
    if (draw->color_key_enable != 0ul) {
        return V9X_D3D_SIS_REFUSE_COLOR_KEY;
    }
    if (!v9x_d3d_sis_map_op(sampler->op, resolved->has_alpha, texture)) {
        return V9X_D3D_SIS_REFUSE_TEXTURE_OP;
    }
    /* The cylindrical WRAPU/WRAPV render states have no engine function.
     * wrap_either is not read: the core starts it at 1 whatever the
     * application set (the ViRGE's folded flag), so refusing on it refuses
     * every textured draw - which fits V9XDDP reading 0 from every textured
     * check on A8U4I5 boot 212. */
    if (sampler->wrap_u != 0ul || sampler->wrap_v != 0ul) {
        return V9X_D3D_SIS_REFUSE_TEXTURE_ADDRESS;
    }
    switch (sampler->address) {
    case V9X_R3D_ADDRESS_WRAP:
        texture->mapping = V9X_SIS3D_TEXTURE_WRAP_U |
                           V9X_SIS3D_TEXTURE_WRAP_V;
        break;
    case V9X_R3D_ADDRESS_MIRROR:
        texture->mapping = V9X_SIS3D_TEXTURE_MIRROR_U |
                           V9X_SIS3D_TEXTURE_MIRROR_V;
        break;
    case V9X_R3D_ADDRESS_CLAMP:
        texture->mapping = V9X_SIS3D_TEXTURE_CLAMP_U |
                           V9X_SIS3D_TEXTURE_CLAMP_V;
        break;
    default:
        return V9X_D3D_SIS_REFUSE_TEXTURE_ADDRESS;
    }
    if (!v9x_d3d_sis_map_filter(sampler, resolved->levels,
                                &texture->filter)) {
        return V9X_D3D_SIS_REFUSE_TEXTURE_FILTER;
    }

    texture->vram_bytes = vram_bytes;
    texture->format = resolved->format;
    texture->log2_width = (v9x_u32)log2_width;
    texture->log2_height = (v9x_u32)log2_height;
    texture->offset = resolved->offset;
    texture->pitch_bytes = resolved->pitch_bytes;
    texture->clear_cache = 0;
    /* A chain only matters to a mip filter; a single level is field 0. */
    texture->levels = (texture->filter & V9X_D3D_SIS_MIN_MASK) >=
                              V9X_SIS3D_MIN_NEAREST_MIP_NEAREST
                          ? resolved->levels - 1ul
                          : 0ul;
    for (level = 0ul; level < V9X_SIS3D_MIP_LEVELS_MAX; ++level) {
        texture->level_offsets[level] =
            level + 1ul < resolved->levels
                ? resolved->level_offsets[level + 1ul]
                : resolved->offset;
    }
    return V9X_D3D_SIS_REFUSE_NONE;
}

/*
 * An untextured draw, drawn textured. A batch with texturing off before a
 * textured one leaves the engine drawing the textured triangle and then
 * never idle (89FCh 00200074h) - whether the first batch blends or not,
 * whatever the write order or cache bits; a textured batch alone, or after
 * a textured batch whose colour comes from the vertex, goes idle (SIS3D
 * /phase4b, A8U4I5 boots 219-227). So texturing stays on: colour mode Cpix
 * and the vertex's alpha, a 1x1 RGB565 texel at the target's own offset,
 * which the blend ignores, and perspective off so the vertices' W - which
 * an untextured TLVERTEX need not set - is not read.
 */
static void v9x_d3d_sis_map_untextured(const V9X_R3D_DRAW *draw,
                                       v9x_u32 vram_bytes,
                                       struct v9x_sis3d_texture *texture)
{
    v9x_u32 level;

    texture->vram_bytes = vram_bytes;
    texture->format = V9X_SIS3D_TEXEL_RGB565;
    texture->log2_width = 0ul;
    texture->log2_height = 0ul;
    texture->levels = 0ul;
    texture->offset = draw->target.offset;
    texture->pitch_bytes = V9X_D3D_SIS_PITCH_UNIT;
    texture->mapping = V9X_SIS3D_TEXTURE_WRAP_U | V9X_SIS3D_TEXTURE_WRAP_V;
    texture->filter = V9X_SIS3D_MIN_NEAREST;
    texture->colour_mode = V9X_SIS3D_TBLEND_CPIX;
    texture->alpha_mode = V9X_SIS3D_TBLEND_APIX;
    texture->clear_cache = 0;
    texture->blend_mask_bit = 0ul;
    for (level = 0ul; level < V9X_SIS3D_MIP_LEVELS_MAX; ++level) {
        texture->level_offsets[level] = draw->target.offset;
    }
}

v9x_u32 v9x_d3d_sis_map_draw(const V9X_R3D_DRAW *draw,
                             const V9X_D3D_SIS_TEXTURE *resolved,
                             v9x_u32 vram_bytes,
                             v9x_u32 specular_rgb,
                             struct v9x_sis3d_state *state,
                             struct v9x_sis3d_texture *texture,
                             int *textured)
{
    v9x_u32 code;
    v9x_u32 reason;

    *textured = 0;
    if (draw->explicit_state != 0ul) {
        return V9X_D3D_SIS_REFUSE_EXPLICIT;
    }
    if (draw->target.format != V9X_R3D_FORMAT_RGB565) {
        return V9X_D3D_SIS_REFUSE_TARGET;
    }
    if (draw->shade_mode != V9X_R3D_SHADE_FLAT &&
        draw->shade_mode != V9X_R3D_SHADE_GOURAUD) {
        return V9X_D3D_SIS_REFUSE_SHADE;
    }

    state->target.vram_bytes = vram_bytes;
    state->target.offset = draw->target.offset;
    state->target.pitch_bytes = draw->target.pitch;
    state->target.width = draw->target.width;
    state->target.height = draw->target.height;
    state->enable = V9X_SIS3D_ENABLE_PRIM_SETUP;
    state->z_offset = 0ul;
    state->z_pitch_bytes = draw->target.pitch;
    state->z_compare = V9X_SIS3D_CMP_ALWAYS;
    state->alpha_compare = V9X_SIS3D_CMP_ALWAYS;
    state->alpha_reference = 0ul;
    state->fog_color = 0ul;

    /*
     * Vertex fog and specular: each vertex's specular dword (fog factor in
     * the alpha byte, colour in RGB) goes to the fog/specular register as
     * it is (v9x_d3d_sis_triangle), 8A20h takes the fog colour. Specular
     * is enabled only where some vertex carries colour - SPECULARENABLE
     * defaults on, and adding black would only cost the engine. Measured
     * against Direct3D's formulas by SIS3D /phase5.
     */
    if (draw->fog_enable != 0ul) {
        state->enable |= V9X_SIS3D_ENABLE_FOG;
        state->fog_color = draw->fog_color & V9X_D3D_SIS_RGB_MASK;
    }
    if (draw->specular_enable != 0ul && specular_rgb != 0ul) {
        state->enable |= V9X_SIS3D_ENABLE_SPECULAR;
    }

    /* Depth needs the state, a bound surface and a pitch. */
    if (draw->depth_enable != 0ul && draw->depth.object != 0 &&
        draw->depth.pitch != 0ul) {
        if (!v9x_d3d_sis_compare(draw->depth_func, &code)) {
            return V9X_D3D_SIS_REFUSE_DEPTH_FUNC;
        }
        state->enable |= V9X_SIS3D_ENABLE_Z_TEST;
        if (draw->depth_write != 0ul) {
            state->enable |= V9X_SIS3D_ENABLE_Z_WRITE;
        }
        state->z_compare = code;
        state->z_offset = draw->depth.offset;
        state->z_pitch_bytes = draw->depth.pitch;
    }
    if (draw->alpha_test_enable != 0ul) {
        if (!v9x_d3d_sis_compare(draw->alpha_func, &code)) {
            return V9X_D3D_SIS_REFUSE_ALPHA_FUNC;
        }
        state->enable |= V9X_SIS3D_ENABLE_ALPHA_TEST;
        state->alpha_compare = code;
        state->alpha_reference = draw->alpha_ref & 0xfful;
    }
    reason = v9x_d3d_sis_map_blend(draw, state);
    if (reason != V9X_D3D_SIS_REFUSE_NONE) {
        return reason;
    }

    /* Texturing as SiS's HAL enabled it (8A00h 00208CA0h), on for every
     * draw. */
    state->enable |= V9X_SIS3D_ENABLE_TEXTURE |
                     V9X_SIS3D_ENABLE_TEXTURE_CACHE |
                     V9X_SIS3D_ENABLE_LARGE_CACHE |
                     V9X_SIS3D_ENABLE_BIT15;
    if (draw->texture.object == 0) {
        v9x_d3d_sis_map_untextured(draw, vram_bytes, texture);
        return V9X_D3D_SIS_REFUSE_NONE;
    }
    reason = v9x_d3d_sis_map_texture(draw, resolved, vram_bytes, texture);
    if (reason != V9X_D3D_SIS_REFUSE_NONE) {
        return reason;
    }
    /* W as RHW for perspective (textures record). */
    state->enable |= V9X_SIS3D_ENABLE_PERSPECTIVE;
    *textured = 1;
    return V9X_D3D_SIS_REFUSE_NONE;
}

/* Z below the wrap at 1.0, and never negative. */
static v9x_u32 v9x_d3d_sis_z(float z)
{
    if (!(z > 0.0f)) {
        return 0ul;
    }
    if (z >= 1.0f) {
        return V9X_D3D_SIS_Z_MAX_BITS;
    }
    return v9x_d3d_sis_bits(z);
}

int v9x_d3d_sis_triangle(const V9X_R3D_VERTEX *triangle,
                         v9x_u32 shade_mode,
                         int textured,
                         struct v9x_sis3d_vertex *out,
                         v9x_u32 *primitive)
{
    const V9X_R3D_VERTEX *colour;
    float x[3];
    float y[3];
    float area;
    v9x_u32 top;
    v9x_u32 middle;
    v9x_u32 bottom;
    v9x_u32 index;
    int flat = shade_mode == V9X_R3D_SHADE_FLAT;
    int direction;

    for (index = 0ul; index < 3ul; ++index) {
        if (!v9x_d3d_sis_finite(triangle[index].sx) ||
            !v9x_d3d_sis_finite(triangle[index].sy) ||
            !v9x_d3d_sis_finite(triangle[index].sz)) {
            return 0;
        }
        if (textured &&
            (!v9x_d3d_sis_finite(triangle[index].rhw) ||
             !(triangle[index].rhw > 0.0f) ||
             !v9x_d3d_sis_finite(triangle[index].tu) ||
             !v9x_d3d_sis_finite(triangle[index].tv))) {
            return 0;
        }
        x[index] = triangle[index].sx - V9X_D3D_SIS_TIE_SHIFT;
        y[index] = triangle[index].sy - V9X_D3D_SIS_TIE_SHIFT;
    }
    area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
    if (!(area != 0.0f)) {
        return 0;
    }

    for (index = 0ul; index < 3ul; ++index) {
        /* Flat shading is Direct3D's: vertex 0's colours throughout. */
        colour = flat ? &triangle[0] : &triangle[index];
        out[index].x = v9x_d3d_sis_bits(x[index]);
        out[index].y = v9x_d3d_sis_bits(y[index]);
        out[index].z = v9x_d3d_sis_z(triangle[index].sz);
        out[index].argb = colour->color;
        out[index].fog_specular = colour->specular;
        out[index].u = textured ? v9x_d3d_sis_bits(triangle[index].tu) : 0ul;
        out[index].v = textured ? v9x_d3d_sis_bits(triangle[index].tv) : 0ul;
        out[index].w = textured ? v9x_d3d_sis_bits(triangle[index].rhw)
                                : V9X_D3D_SIS_FLOAT_ONE_BITS;
    }

    /* TDRAWDIR is 1 when the middle vertex lies left of the long top-to-
     * bottom edge (first-triangle record). */
    v9x_sis3d_order(out, &top, &middle, &bottom);
    direction = (x[middle] - x[top]) * (y[bottom] - y[top]) <
                (x[bottom] - x[top]) * (y[middle] - y[top]);
    *primitive = v9x_sis3d_primitive(out,
                                     flat ? V9X_SIS3D_SHADE_FLAT_TOP
                                          : V9X_SIS3D_SHADE_GOURAUD,
                                     direction);
    return 1;
}
