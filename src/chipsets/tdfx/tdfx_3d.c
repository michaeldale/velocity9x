/*
 * 3dfx VSA-100 3D register values. See include\velocity9x\tdfx_3d.h for the
 * contract and docs\specifications\vsa100-registers.md section 8 for the
 * Napalm spec citations.
 */
#include "velocity9x/tdfx_3d.h"

/* Colour and aux buffers (sections 8.40-8.43): base 25:4, so 16-byte
 * aligned; a linear stride is bytes in 13:0, a multiple of 16. */
#define TDFX3D_ALIGN_MASK           0x0000000ful
#define TDFX3D_BASE_LIMIT           0x04000000ul
#define TDFX3D_STRIDE_MAX           0x00003ffful

/* Clip fields are 12 bits (section 8.44). */
#define TDFX3D_CLIP_MAX             0x00000ffful
#define TDFX3D_CLIP_HIGH_SHIFT      16

/* The target is RGB565 and the depth buffer Z16: two bytes a pixel. */
#define TDFX3D_BYTES_PER_PIXEL      2ul

/* fbzMode (section 8.25). */
#define TDFX3D_FBZ_CLIP             0x00000001ul
#define TDFX3D_FBZ_DEPTH            0x00000010ul
#define TDFX3D_FBZ_DEPTH_FUNC_SHIFT 5
#define TDFX3D_FBZ_DITHER           0x00000100ul
#define TDFX3D_FBZ_RGB_WRITE        0x00000200ul
#define TDFX3D_FBZ_DEPTH_WRITE      0x00000400ul

/* fbzColorPath (section 8.20). RGB and alpha select 1 = the TMU; the
 * multiply select is 12:10 for colour and 21:19 for alpha, value 1 being
 * the local colour or alpha; reverse blend (13, 22) makes the factor the
 * selection rather than one minus it, which is what Glide's composite
 * "multiply" sets. Iterated colour is the local term when 4 and 6:5 are
 * clear. */
#define TDFX3D_CP_RGB_TMU           0x00000001ul
#define TDFX3D_CP_ALPHA_TMU         0x00000004ul
#define TDFX3D_CP_RGB_MUL_LOCAL     0x00000400ul
#define TDFX3D_CP_RGB_REVERSE       0x00002000ul
#define TDFX3D_CP_ALPHA_MUL_LOCAL   0x00080000ul
#define TDFX3D_CP_ALPHA_REVERSE     0x00400000ul
#define TDFX3D_CP_TEXTURE_ENABLE    0x08000000ul

/* alphaMode (section 8.23). The alpha channel's own factors are ONE and
 * ZERO: an RGB565 target holds no alpha. */
#define TDFX3D_AM_TEST              0x00000001ul
#define TDFX3D_AM_FUNC_SHIFT        1
#define TDFX3D_AM_BLEND             0x00000010ul
#define TDFX3D_AM_SRC_SHIFT         8
#define TDFX3D_AM_DST_SHIFT         12
#define TDFX3D_AM_ASRC_SHIFT        16
#define TDFX3D_AM_ADST_SHIFT        20
#define TDFX3D_AM_REF_SHIFT         24
#define TDFX3D_FACTOR_MAX           0xful

/* textureMode (section 8.76): perspective 0, bilinear 1 and 2, clamp 6 and
 * 7, format 11:8; the colour combine 20:12 and alpha combine 29:21 each set
 * to "replace" - zero other (12, 21) and add local (18, 27) - so TMU0
 * passes its texel on. */
#define TDFX3D_TM_PERSPECTIVE       0x00000001ul
#define TDFX3D_TM_MIN_BILINEAR      0x00000002ul
#define TDFX3D_TM_MAG_BILINEAR      0x00000004ul
#define TDFX3D_TM_CLAMP             0x000000c0ul
#define TDFX3D_TM_FORMAT_SHIFT      8
#define TDFX3D_TM_REPLACE           0x08241000ul

/* tLOD (section 8.77): LOD min 5:0, max 11:6 and bias 17:12 in 4.2
 * fixed point. A chain is biased +0.5: the chip truncates its LOD, and
 * Direct3D's nearest level is the rounded one (boot 433: unbiased, the
 * levels changed at 2, 4 and 8 texels a pixel; 3dfx's own driver changed
 * them near 1.4, 2.8 and 5.7). Only where minification and magnification
 * filter alike: the chip picks between them on the biased LOD, so a bias
 * filters a 1:1 texture with the minification filter, which smeared
 * Half-Life's point-sampled console text (boot 434). */
#define TDFX3D_LOD_FRACTION_SHIFT   2
#define TDFX3D_LOD_MAX_SHIFT        6
#define TDFX3D_LOD_BIAS_SHIFT       12
#define TDFX3D_LOD_S_WIDER          0x00100000ul
#define TDFX3D_LOD_ASPECT_SHIFT     21
#define TDFX3D_LOD_BIAS_ROUND       2ul

/* texBaseAddr (section 8.79): address bits 24:4 in place, address bit 25
 * in bit 1; bit 0 (tiled) stays clear. The address wraps at 2^26, so a
 * base "below zero" (section 20 p.228) is expressible. */
#define TDFX3D_TEX_ADDR_MASK        0x01fffff0ul
#define TDFX3D_TEX_ADDR_BIT25       0x02000000ul
#define TDFX3D_TEX_BIT25_FIELD      0x00000002ul
#define TDFX3D_TEX_WRAP_MASK        0x03fffffful

/* sSetupMode (section 8.59): which parameters each vertex sends. Strip
 * (16 clear), no culling (17 clear). */
#define TDFX3D_SETUP_RGB            0x00000001ul
#define TDFX3D_SETUP_ALPHA          0x00000002ul
#define TDFX3D_SETUP_Z              0x00000004ul
#define TDFX3D_SETUP_WB             0x00000008ul
#define TDFX3D_SETUP_W0             0x00000010ul
#define TDFX3D_SETUP_ST0            0x00000020ul

/* The most writes one vertex can need: X, Y, four colours, Z, Wb, W0, S, T
 * and the command. */
#define TDFX3D_VERTEX_WRITES_MAX    12u

static void tdfx3d_emit(struct v9x_tdfx_3d_words *words, v9x_u32 offset,
                        v9x_u32 value)
{
    words->offsets[words->count] = offset;
    words->values[words->count] = value;
    ++words->count;
}

/* A linear surface of rows x pitch at offset: aligned, inside memory, and
 * a stride the register can hold. */
static v9x_status tdfx3d_check_surface(const struct v9x_tdfx_3d_surface *s,
                                       v9x_u32 width, v9x_u32 height,
                                       v9x_u32 vram_bytes)
{
    v9x_u32 bytes;

    if ((s->pitch_bytes & TDFX3D_ALIGN_MASK) != 0ul ||
        s->pitch_bytes == 0ul || s->pitch_bytes > TDFX3D_STRIDE_MAX) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if ((s->offset & TDFX3D_ALIGN_MASK) != 0ul ||
        width * TDFX3D_BYTES_PER_PIXEL > s->pitch_bytes) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    bytes = s->pitch_bytes * height;
    if (vram_bytes > TDFX3D_BASE_LIMIT || s->offset >= vram_bytes ||
        bytes > vram_bytes - s->offset) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    return V9X_STATUS_OK;
}

/* Bytes of a level whose long side is 2^log2_size, the short side aspect
 * halvings less but at least one texel; two bytes a texel. */
static v9x_u32 tdfx3d_level_bytes(v9x_u32 log2_size, v9x_u32 aspect)
{
    v9x_u32 log2_short = log2_size > aspect ? log2_size - aspect : 0ul;

    return (1ul << (log2_size + log2_short)) * TDFX3D_BYTES_PER_PIXEL;
}

/* Levels sampled, 1 for a texture without a chain. */
static v9x_u32 tdfx3d_levels(const struct v9x_tdfx_3d_texture *t)
{
    return t->levels == 0ul ? 1ul : t->levels;
}

static v9x_status tdfx3d_check_texture(const struct v9x_tdfx_3d_texture *t,
                                       v9x_u32 vram_bytes)
{
    v9x_u32 bytes = 0ul;
    v9x_u32 level;

    if (t->log2_size > V9X_TDFX_TEX_LOG2_MAX ||
        t->aspect > V9X_TDFX_TEX_ASPECT_MAX ||
        t->aspect > t->log2_size ||
        tdfx3d_levels(t) > t->log2_size + 1ul ||
        (t->format != V9X_TDFX_TEX_RGB565 &&
         t->format != V9X_TDFX_TEX_ARGB1555 &&
         t->format != V9X_TDFX_TEX_ARGB4444) ||
        (t->combine != V9X_TDFX_COMBINE_REPLACE &&
         t->combine != V9X_TDFX_COMBINE_MODULATE &&
         t->combine != V9X_TDFX_COMBINE_MODULATE_TEXALPHA)) {
        return V9X_STATUS_UNSUPPORTED;
    }
    for (level = 0ul; level < tdfx3d_levels(t); ++level) {
        bytes += tdfx3d_level_bytes(t->log2_size - level, t->aspect);
    }
    if ((t->offset & TDFX3D_ALIGN_MASK) != 0ul || t->offset >= vram_bytes ||
        bytes > vram_bytes - t->offset) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    return V9X_STATUS_OK;
}

/*
 * The base the chip wants for one level stored at offset: where level 0 of
 * the same aspect, 256 texels on its long side, would start, so the offset
 * less every larger level (section 20 p.228), wrapped to the 26-bit address
 * and encoded.
 */
static v9x_u32 tdfx3d_texture_base(const struct v9x_tdfx_3d_texture *t)
{
    v9x_u32 base = t->offset;
    v9x_u32 log2_size;

    for (log2_size = V9X_TDFX_TEX_LOG2_MAX; log2_size > t->log2_size;
         --log2_size) {
        base -= tdfx3d_level_bytes(log2_size, t->aspect);
    }
    base &= TDFX3D_TEX_WRAP_MASK;
    return (base & TDFX3D_TEX_ADDR_MASK) |
           ((base & TDFX3D_TEX_ADDR_BIT25) != 0ul ? TDFX3D_TEX_BIT25_FIELD
                                                  : 0ul);
}

static v9x_u32 tdfx3d_color_path(const struct v9x_tdfx_3d_state *state)
{
    if (state->texture_enable == 0ul) {
        return 0ul;
    }
    if (state->texture.combine == V9X_TDFX_COMBINE_MODULATE) {
        return TDFX3D_CP_TEXTURE_ENABLE | TDFX3D_CP_RGB_TMU |
               TDFX3D_CP_ALPHA_TMU | TDFX3D_CP_RGB_MUL_LOCAL |
               TDFX3D_CP_RGB_REVERSE | TDFX3D_CP_ALPHA_MUL_LOCAL |
               TDFX3D_CP_ALPHA_REVERSE;
    }
    if (state->texture.combine == V9X_TDFX_COMBINE_MODULATE_TEXALPHA) {
        return TDFX3D_CP_TEXTURE_ENABLE | TDFX3D_CP_RGB_TMU |
               TDFX3D_CP_ALPHA_TMU | TDFX3D_CP_RGB_MUL_LOCAL |
               TDFX3D_CP_RGB_REVERSE;
    }
    return TDFX3D_CP_TEXTURE_ENABLE | TDFX3D_CP_RGB_TMU | TDFX3D_CP_ALPHA_TMU;
}

v9x_status v9x_tdfx_3d_build_state(const struct v9x_tdfx_3d_state *state,
                                   struct v9x_tdfx_3d_words *words)
{
    v9x_status status;
    v9x_u32 fbz_mode;
    v9x_u32 alpha_mode = 0ul;

    if (state == 0 || words == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    words->count = 0u;

    if (state->width == 0ul || state->height == 0ul ||
        state->width > TDFX3D_CLIP_MAX || state->height > TDFX3D_CLIP_MAX) {
        return V9X_STATUS_UNSUPPORTED;
    }
    status = tdfx3d_check_surface(&state->target, state->width,
                                  state->height, state->vram_bytes);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    if (state->depth_enable != 0ul) {
        status = tdfx3d_check_surface(&state->depth, state->width,
                                      state->height, state->vram_bytes);
        if (status != V9X_STATUS_OK) {
            return status;
        }
        if (state->depth_func > V9X_TDFX_CMP_ALWAYS) {
            return V9X_STATUS_UNSUPPORTED;
        }
    }
    if (state->blend_enable != 0ul &&
        (state->blend_src > TDFX3D_FACTOR_MAX ||
         state->blend_dst > TDFX3D_FACTOR_MAX)) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (state->alpha_test != 0ul &&
        (state->alpha_func > V9X_TDFX_CMP_ALWAYS ||
         state->alpha_ref > 0xfful)) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (state->texture_enable != 0ul) {
        status = tdfx3d_check_texture(&state->texture, state->vram_bytes);
        if (status != V9X_STATUS_OK) {
            return status;
        }
    }

    /* renderMode 0: 16 bpp RGB565, every other field off. */
    tdfx3d_emit(words, V9X_TDFX_3D_RENDER_MODE, 0ul);
    tdfx3d_emit(words, V9X_TDFX_3D_COL_BUFFER_ADDR, state->target.offset);
    tdfx3d_emit(words, V9X_TDFX_3D_COL_BUFFER_STRIDE,
                state->target.pitch_bytes);
    if (state->depth_enable != 0ul) {
        tdfx3d_emit(words, V9X_TDFX_3D_AUX_BUFFER_ADDR, state->depth.offset);
        tdfx3d_emit(words, V9X_TDFX_3D_AUX_BUFFER_STRIDE,
                    state->depth.pitch_bytes);
    }
    tdfx3d_emit(words, V9X_TDFX_3D_CLIP_LEFT_RIGHT, state->width);
    tdfx3d_emit(words, V9X_TDFX_3D_CLIP_TOP_BOTTOM, state->height);

    fbz_mode = TDFX3D_FBZ_CLIP | TDFX3D_FBZ_RGB_WRITE;
    if (state->dither != 0ul) {
        fbz_mode |= TDFX3D_FBZ_DITHER;
    }
    if (state->depth_enable != 0ul) {
        fbz_mode |= TDFX3D_FBZ_DEPTH |
                    (state->depth_func << TDFX3D_FBZ_DEPTH_FUNC_SHIFT);
        if (state->depth_write != 0ul) {
            fbz_mode |= TDFX3D_FBZ_DEPTH_WRITE;
        }
    }
    tdfx3d_emit(words, V9X_TDFX_3D_FBZ_MODE, fbz_mode);
    tdfx3d_emit(words, V9X_TDFX_3D_FBZ_COLOR_PATH, tdfx3d_color_path(state));

    if (state->alpha_test != 0ul) {
        alpha_mode |= TDFX3D_AM_TEST |
                      (state->alpha_func << TDFX3D_AM_FUNC_SHIFT) |
                      (state->alpha_ref << TDFX3D_AM_REF_SHIFT);
    }
    if (state->blend_enable != 0ul) {
        alpha_mode |= TDFX3D_AM_BLEND |
                      (state->blend_src << TDFX3D_AM_SRC_SHIFT) |
                      (state->blend_dst << TDFX3D_AM_DST_SHIFT) |
                      (V9X_TDFX_BLEND_ONE << TDFX3D_AM_ASRC_SHIFT) |
                      (V9X_TDFX_BLEND_ZERO << TDFX3D_AM_ADST_SHIFT);
    }
    tdfx3d_emit(words, V9X_TDFX_3D_ALPHA_MODE, alpha_mode);
    tdfx3d_emit(words, V9X_TDFX_3D_FOG_MODE, 0ul);

    if (state->texture_enable != 0ul) {
        const struct v9x_tdfx_3d_texture *t = &state->texture;
        v9x_u32 mode = TDFX3D_TM_PERSPECTIVE | TDFX3D_TM_REPLACE |
                       (t->format << TDFX3D_TM_FORMAT_SHIFT);
        /* LOD n is a 256 >> n texel level: the minimum is level 0, the
         * maximum the smallest level of the chain. */
        v9x_u32 lod = (V9X_TDFX_TEX_LOG2_MAX - t->log2_size) <<
                      TDFX3D_LOD_FRACTION_SHIFT;
        v9x_u32 lod_max = (V9X_TDFX_TEX_LOG2_MAX - t->log2_size +
                           tdfx3d_levels(t) - 1ul) <<
                          TDFX3D_LOD_FRACTION_SHIFT;
        v9x_u32 lod_bias = 0ul;
        v9x_u32 lod_shape = t->aspect << TDFX3D_LOD_ASPECT_SHIFT;

        if (tdfx3d_levels(t) > 1ul &&
            (t->min_bilinear != 0ul) == (t->mag_bilinear != 0ul)) {
            lod_bias = TDFX3D_LOD_BIAS_ROUND << TDFX3D_LOD_BIAS_SHIFT;
        }
        if (t->s_wider != 0ul) {
            lod_shape |= TDFX3D_LOD_S_WIDER;
        }
        if (t->min_bilinear != 0ul) {
            mode |= TDFX3D_TM_MIN_BILINEAR;
        }
        if (t->mag_bilinear != 0ul) {
            mode |= TDFX3D_TM_MAG_BILINEAR;
        }
        if (t->clamp != 0ul) {
            mode |= TDFX3D_TM_CLAMP;
        }
        tdfx3d_emit(words, V9X_TDFX_3D_TMU0 | V9X_TDFX_3D_TEXTURE_MODE, mode);
        tdfx3d_emit(words, V9X_TDFX_3D_TMU0 | V9X_TDFX_3D_TLOD,
                    lod | (lod_max << TDFX3D_LOD_MAX_SHIFT) | lod_bias |
                    lod_shape);
        /* Twice, a different base first: a CPU download reaches the
         * texture cache when texBaseAddr is rewritten (section 20.3
         * p.234), and the texels may have changed since the last draw. */
        tdfx3d_emit(words, V9X_TDFX_3D_TMU0 | V9X_TDFX_3D_TEX_BASE_ADDR,
                    tdfx3d_texture_base(t) ^ TDFX3D_TEX_ADDR_MASK);
        tdfx3d_emit(words, V9X_TDFX_3D_TMU0 | V9X_TDFX_3D_TEX_BASE_ADDR,
                    tdfx3d_texture_base(t));
    }
    return V9X_STATUS_OK;
}

v9x_status v9x_tdfx_3d_build_triangle(const struct v9x_tdfx_3d_state *state,
                                      const struct v9x_tdfx_3d_vertex *v,
                                      struct v9x_tdfx_3d_words *words)
{
    v9x_u32 setup = TDFX3D_SETUP_RGB | TDFX3D_SETUP_ALPHA;
    v9x_u32 index;

    if (state == 0 || v == 0 || words == 0 ||
        words->count > V9X_TDFX_3D_WRITES ||
        V9X_TDFX_3D_WRITES - words->count <
            1u + 3u * TDFX3D_VERTEX_WRITES_MAX) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (state->depth_enable != 0ul) {
        setup |= TDFX3D_SETUP_Z;
    }
    if (state->texture_enable != 0ul) {
        setup |= TDFX3D_SETUP_WB | TDFX3D_SETUP_W0 | TDFX3D_SETUP_ST0;
    }
    tdfx3d_emit(words, V9X_TDFX_3D_S_SETUP_MODE, setup);

    /* Colour as four separate floats: how the setup unit would choose the
     * packed sARGB under direct writes is not established (D37). */
    for (index = 0u; index < 3u; ++index) {
        const struct v9x_tdfx_3d_vertex *p = &v[index];

        tdfx3d_emit(words, V9X_TDFX_3D_S_VX, v9x_tdfx_3d_float_bits(p->x));
        tdfx3d_emit(words, V9X_TDFX_3D_S_VY, v9x_tdfx_3d_float_bits(p->y));
        tdfx3d_emit(words, V9X_TDFX_3D_S_RED, v9x_tdfx_3d_float_bits(p->r));
        tdfx3d_emit(words, V9X_TDFX_3D_S_GREEN, v9x_tdfx_3d_float_bits(p->g));
        tdfx3d_emit(words, V9X_TDFX_3D_S_BLUE, v9x_tdfx_3d_float_bits(p->b));
        tdfx3d_emit(words, V9X_TDFX_3D_S_ALPHA, v9x_tdfx_3d_float_bits(p->a));
        if ((setup & TDFX3D_SETUP_Z) != 0ul) {
            tdfx3d_emit(words, V9X_TDFX_3D_S_VZ,
                        v9x_tdfx_3d_float_bits(p->z));
        }
        if ((setup & TDFX3D_SETUP_ST0) != 0ul) {
            tdfx3d_emit(words, V9X_TDFX_3D_S_WB,
                        v9x_tdfx_3d_float_bits(p->oow));
            tdfx3d_emit(words, V9X_TDFX_3D_S_WTMU0,
                        v9x_tdfx_3d_float_bits(p->oow));
            tdfx3d_emit(words, V9X_TDFX_3D_S_S_W0,
                        v9x_tdfx_3d_float_bits(p->sow));
            tdfx3d_emit(words, V9X_TDFX_3D_S_T_W0,
                        v9x_tdfx_3d_float_bits(p->tow));
        }
        /* Section 8.60: the first vertex begins, each later one draws. */
        tdfx3d_emit(words, index == 0u ? V9X_TDFX_3D_S_BEGIN_TRI_CMD
                                       : V9X_TDFX_3D_S_DRAW_TRI_CMD, 0ul);
    }
    return V9X_STATUS_OK;
}

v9x_u32 v9x_tdfx_3d_float_bits(float f)
{
    union {
        float f;
        v9x_u32 u;
    } bits;

    bits.f = f;
    return bits.u;
}
