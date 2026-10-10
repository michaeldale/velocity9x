/*
 * Matrox MGA-2164W 3D: trapezoid register values, and the model of what
 * the engine draws for them. Pure policy, no I/O; see mga_3d.h.
 *
 * Every register value is from the MGA-2164W Developer Specification
 * (1997), section 4.5.5 and the register pages it names; page numbers are
 * the document's own, collected in docs\specifications\mga2164w-3d-engine.md.
 * The model's stepping is the specification's Bresenham terms run the way
 * 86Box's vid_mga.c runs them (blit_trap, blit_texture_trap). That source is
 * an emulator, not Matrox's: the write probe settles it on the card.
 */
#include "velocity9x/mga_3d.h"

/*
 * DWGCTL (3-55..3-60). Both are TRAP (opcod 0100) with shftzero and bop C,
 * sgnzero and arzero clear so SGN and the AR registers carry the edges:
 * section 4.5.5.2's constant trapezoid and 4.5.5.4's Gouraud one (4-36,
 * 4-40). Flat is atype RPL with solid set, so FCOL is the colour. Gouraud
 * is atype I (111, "Gouraud with depth compare") with zmode NOZCMP: the
 * compare always passes and atype I never writes depth (3-56), so neither
 * ZORG nor DR0-DR3 matter and none is written. ZI would write depth.
 */
#define V9X_MGA3D_DWGCTL_FLAT    0x000c4804ul
#define V9X_MGA3D_DWGCTL_GOURAUD 0x000c4074ul

/*
 * With depth the same Gouraud TRAP takes its atype and zmode from the
 * trapezoid: ZI (011, compare and write depth) or I (111, compare only),
 * and the compare in zmode<10:8> (3-56).
 */
#define V9X_MGA3D_DWGCTL_TRAP_SHADED 0x000c4004ul
#define V9X_MGA3D_ATYPE_ZI    0x3ul
#define V9X_MGA3D_ATYPE_I     0x7ul
#define V9X_MGA3D_ATYPE_SHIFT 4u
#define V9X_MGA3D_ZMODE_SHIFT 8u

/* MACCESS (3-70): pwidth<1:0> 00 8 bpp, 01 16, 10 32; zwidth<3> 32-bit Z;
 * dit555<31> dithers shading for 5:5:5. Written with every shaded
 * trapezoid. */
#define V9X_MGA3D_PWIDTH_16 0x1ul
#define V9X_MGA3D_PWIDTH_32 0x2ul
#define V9X_MGA3D_ZWIDTH_32 0x8ul
#define V9X_MGA3D_DIT555    0x80000000ul

/* DWGCTL trans <23:20> (3-59). */
#define V9X_MGA3D_TRANS_SHIFT 20u
#define V9X_MGA3D_TRANS_MAX   15ul

/* ZORG (3-91): a 24-bit byte address whose low nine bits must be zero. */
#define V9X_MGA3D_ZORG_ALIGN 0x00000200ul
#define V9X_MGA3D_ZORG_LIMIT 0x01000000ul

/* A 33.15 value's top half occupies <15:0> of the MSB register (3-39). */
#define V9X_MGA3D_Z48_HI_LIMIT 0x00008000L

/*
 * Texturing, all hypothesised from 86Box's vid_mga.c: Matrox's public
 * specification omits it (mga_3d.h). TEXTURE_TRAP is opcod 0110, with
 * atype I or ZI. TEXCTL: texformat<2:0>, tpitch<18:16> (8 << tpitch
 * texels a row), npcen<21> (no perspective), clampv<27>, clampu<28>,
 * tmodulate<29>. TEXWIDTH and TEXHEIGHT: log2 size in <5:0>, size - 1 in
 * <28:18>. TEXTRANS: key <15:0>, key mask <31:16>.
 */
#define V9X_MGA3D_DWGCTL_TEXTURE_TRAP 0x000c4006ul
#define V9X_MGA3D_OPCOD_MASK          0x0000000ful
#define V9X_MGA3D_TEXCTL_TPITCH_SHIFT 16u
#define V9X_MGA3D_TEXCTL_NPCEN        0x00200000ul
#define V9X_MGA3D_TEXCTL_TAKEY        0x02000000ul
#define V9X_MGA3D_TEXCTL_TAMASK       0x04000000ul
#define V9X_MGA3D_TEXCTL_CLAMPV       0x08000000ul
#define V9X_MGA3D_TEXCTL_CLAMPU       0x10000000ul
#define V9X_MGA3D_TEXCTL_TMODULATE    0x20000000ul
#define V9X_MGA3D_TEXSIZE_MASK_SHIFT  18u
#define V9X_MGA3D_TEX_LOG2_MAX        10ul
#define V9X_MGA3D_TEX_PITCH_LOG2_MIN  3ul
#define V9X_MGA3D_TEXORG_ALIGN        32ul
/* s and t: 1 << 20 spans the texture; q is 16.16. */
#define V9X_MGA3D_TEX_COORD_BITS      20u
/* The perspective path's 1/8-texel bias, in fraction bits. */
#define V9X_MGA3D_TEX_PERSPECTIVE_BIAS_BITS 3u

/* SGN (3-77): sdxl<1> and sdxr<5>, each edge moving left. scanleft<0> and
 * sdy<2> must stay 0 for a trapezoid. */
#define V9X_MGA3D_SGN_SDXL 0x00000002ul
#define V9X_MGA3D_SGN_SDXR 0x00000020ul

/* AR0, AR2, AR4, AR5 and AR6 are 18-bit signed; AR1 24-bit signed (3-23..
 * 3-29). An edge's terms are bounded by its |dx| and dy, so both are kept
 * below 2^16, which also keeps every sum in the stepping below 2^18. */
#define V9X_MGA3D_EDGE_LIMIT 0x00010000L

/* YDSTLEN's y and length are 16-bit fields, FXBNDRY's edges 16-bit signed
 * (3-90, 3-65); the clip window's x limit is 2047 (3-29). */
#define V9X_MGA3D_Y_LIMIT 0x00008000ul
#define V9X_MGA3D_X_LIMIT 2048L

/* The linear line address the clipper compares against YBOT, opened to its
 * largest multiple of 32 by the setup (mga_engine.c). */
#define V9X_MGA3D_YBOT_OPEN 0x007fffe0ul

/* Colour interpolants occupy <23:0>; bit 23 is the sign (3-45). */
#define V9X_MGA3D_DR_MASK 0x00fffffful
#define V9X_MGA3D_DR_SIGN 0x00800000ul
#define V9X_MGA3D_DR_LIMIT 0x00800000L

/* One edge as the engine steps it: x, the error term (AR1/AR4), the major
 * step (AR0/AR6), the minor step (AR2/AR5) and the direction (SGN). */
struct v9x_mga3d_stepper {
    v9x_s32 x;
    v9x_s32 error;
    v9x_s32 major;
    v9x_s32 minor;
    v9x_s32 direction;
};

static v9x_s32 v9x_mga3d_abs(v9x_s32 value)
{
    return value < 0L ? -value : value;
}

/*
 * Section 4.5.5.1 (4-35): for an edge running dX over dY,
 *   AR0/AR6 = dY, AR2/AR5 = -|dX|,
 *   AR1/AR4 = sdx ? dX + dY - 1 : -dX.
 */
static void v9x_mga3d_edge_terms(const struct v9x_mga3d_edge *edge,
                                 struct v9x_mga3d_stepper *stepper)
{
    stepper->x = edge->x;
    stepper->major = edge->dy;
    stepper->minor = -v9x_mga3d_abs(edge->dx);
    if (edge->dx < 0L) {
        stepper->error = edge->dx + edge->dy - 1L;
        stepper->direction = -1L;
    } else {
        stepper->error = -edge->dx;
        stepper->direction = 1L;
    }
    stepper->error += edge->error_bias;
}

/* Advance one row; returns the columns moved. The loop ends because each
 * pass adds the positive major step to a negative error. */
static v9x_s32 v9x_mga3d_edge_step(struct v9x_mga3d_stepper *stepper)
{
    v9x_s32 moved = 0L;

    while (stepper->error < 0L && stepper->major != 0L) {
        stepper->error += stepper->major;
        stepper->x += stepper->direction;
        moved += stepper->direction;
    }
    stepper->error += stepper->minor;
    return moved;
}

static int v9x_mga3d_edge_ok(const struct v9x_mga3d_edge *edge)
{
    return edge->dy >= 1L && edge->dy < V9X_MGA3D_EDGE_LIMIT &&
        v9x_mga3d_abs(edge->dx) < V9X_MGA3D_EDGE_LIMIT &&
        v9x_mga3d_abs(edge->error_bias) < edge->dy;
}

static int v9x_mga3d_dr_ok(const v9x_s32 *channel)
{
    v9x_u32 index;

    for (index = 0ul; index < 3ul; ++index) {
        if (channel[index] >= V9X_MGA3D_DR_LIMIT ||
            channel[index] < -V9X_MGA3D_DR_LIMIT) {
            return 0;
        }
    }
    return 1;
}

/*
 * Run both edges over every row: each row's span must lie inside the
 * surface's pitch and the clip window, and the edges must not cross. An
 * empty row (left == right) is allowed.
 */
static v9x_status v9x_mga3d_check_spans(const struct v9x_mga3d_trap *trap,
                                        v9x_s32 pitch_pixels)
{
    struct v9x_mga3d_stepper left;
    struct v9x_mga3d_stepper right;
    v9x_u32 row;

    v9x_mga3d_edge_terms(&trap->left, &left);
    v9x_mga3d_edge_terms(&trap->right, &right);
    for (row = 0ul; row < trap->length; ++row) {
        if (left.x < 0L || right.x > pitch_pixels ||
            right.x > V9X_MGA3D_X_LIMIT) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        if (left.x > right.x) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        v9x_mga3d_edge_step(&left);
        v9x_mga3d_edge_step(&right);
    }
    return V9X_STATUS_OK;
}

static v9x_u32 v9x_mga3d_color_field(v9x_u32 color, v9x_u32 bytes_per_pixel)
{
    if (bytes_per_pixel == 1ul) {
        color &= 0xfful;
        return color | (color << 8) | (color << 16) | (color << 24);
    }
    if (bytes_per_pixel == 2ul) {
        color &= 0xfffful;
        return color | (color << 16);
    }
    return color;
}

static void v9x_mga3d_put(struct v9x_mga3d_writes *writes, v9x_u32 offset,
                          v9x_u32 value)
{
    writes->offsets[writes->count] = offset;
    writes->values[writes->count] = value;
    ++writes->count;
}

static void v9x_mga3d_put_channel(struct v9x_mga3d_writes *writes,
                                  v9x_u32 start, v9x_u32 x_step,
                                  v9x_u32 y_step, const v9x_s32 *channel)
{
    v9x_mga3d_put(writes, start, (v9x_u32)channel[0] & V9X_MGA3D_DR_MASK);
    v9x_mga3d_put(writes, x_step, (v9x_u32)channel[1] & V9X_MGA3D_DR_MASK);
    v9x_mga3d_put(writes, y_step, (v9x_u32)channel[2] & V9X_MGA3D_DR_MASK);
}

static v9x_u32 v9x_mga3d_z_bytes(v9x_u32 depth)
{
    return depth == V9X_MGA3D_DEPTH_32 ? 4ul : 2ul;
}

static int v9x_mga3d_z48_ok(const struct v9x_mga3d_z48 *value)
{
    return value->hi < V9X_MGA3D_Z48_HI_LIMIT &&
        value->hi >= -V9X_MGA3D_Z48_HI_LIMIT;
}

/*
 * The depth half of the checks, once the colour half has passed: the
 * compare is a defined one, the values fit their registers, and the Z
 * buffer - the colour surface's rows at 2 or 4 bytes a pixel - lies in VRAM
 * clear of the colour surface (3-91: it "must not overlap"), with a ZORG the
 * register can hold. *zorg is the value to write.
 */
static v9x_status v9x_mga3d_check_depth(const struct v9x_mga3d_trap *trap,
                                        v9x_u32 pitch_pixels,
                                        v9x_u32 origin_pixels,
                                        v9x_u32 *zorg)
{
    v9x_u32 z_bytes;
    v9x_u32 z_base;
    v9x_u32 z_end;
    v9x_u32 color_end;
    v9x_u32 index;

    *zorg = 0ul;
    if (trap->depth == V9X_MGA3D_DEPTH_NONE) {
        return V9X_STATUS_OK;
    }
    if (trap->depth != V9X_MGA3D_DEPTH_16 &&
        trap->depth != V9X_MGA3D_DEPTH_32) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (trap->shade != V9X_MGA3D_SHADE_GOURAUD) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (trap->zmode > V9X_MGA3D_ZMODE_ZGTE || trap->zmode == 1ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (trap->depth == V9X_MGA3D_DEPTH_32) {
        for (index = 0ul; index < 3ul; ++index) {
            if (!v9x_mga3d_z48_ok(&trap->z32[index])) {
                return V9X_STATUS_UNSUPPORTED;
            }
        }
    }

    /* origin_pixels is below 2^22 and z_bytes at most 4, so the product
     * fits; so do the row extents, both bounded by v9x_mga3d_check. */
    z_bytes = v9x_mga3d_z_bytes(trap->depth);
    z_base = origin_pixels * z_bytes;
    if (trap->z_offset < z_base) {
        return V9X_STATUS_UNSUPPORTED;
    }
    *zorg = trap->z_offset - z_base;
    if ((*zorg % V9X_MGA3D_ZORG_ALIGN) != 0ul ||
        *zorg >= V9X_MGA3D_ZORG_LIMIT) {
        return V9X_STATUS_UNSUPPORTED;
    }
    z_end = (trap->top + trap->length) * pitch_pixels * z_bytes;
    if (trap->z_offset >= trap->vram_bytes ||
        z_end > trap->vram_bytes - trap->z_offset) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    z_end += trap->z_offset;
    color_end = trap->target_offset +
        (trap->top + trap->length) * trap->pitch_bytes;
    if (trap->z_offset < color_end && trap->target_offset < z_end) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    return V9X_STATUS_OK;
}

/*
 * The texture half of the checks: a format the model and builder know
 * (the 16-bit ones; the palettised ones need the LUT load, not yet
 * measured), sizes inside the fields, a pitch at least the width, and the
 * texels inside VRAM. Texturing writes the Gouraud registers too, so the
 * destination must be one Gouraud can draw (not 8 bpp).
 */
static v9x_status v9x_mga3d_check_texture(const struct v9x_mga3d_trap *trap)
{
    const struct v9x_mga3d_texture *texture = &trap->texture;
    v9x_u32 bytes;

    if (texture->enabled == 0ul) {
        return V9X_STATUS_OK;
    }
    if (trap->shade != V9X_MGA3D_SHADE_GOURAUD) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* TW12 (format 4 in 86Box) drew garbage on the 2164W, and Matrox's
     * HAL offers no 4444; the palettised formats wait on the LUT load. */
    if (texture->format != V9X_MGA3D_TEX_TW15 &&
        texture->format != V9X_MGA3D_TEX_TW16) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (trap->bytes_per_pixel == 1ul ||
        texture->log2_width > V9X_MGA3D_TEX_LOG2_MAX ||
        texture->log2_height > V9X_MGA3D_TEX_LOG2_MAX ||
        texture->log2_pitch < V9X_MGA3D_TEX_PITCH_LOG2_MIN ||
        texture->log2_pitch > V9X_MGA3D_TEX_LOG2_MAX ||
        texture->log2_pitch < texture->log2_width) {
        return V9X_STATUS_UNSUPPORTED;
    }
    /* The card ignores TEXORG's low five bits: 16 bytes off fetched 8
     * texels early, 32 off was exact (docs\decisions\
     * 2026-10-10-mga2164w-textures.md). */
    if ((texture->offset % V9X_MGA3D_TEXORG_ALIGN) != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    /* Two bytes a texel; both sizes at most 1024, so no overflow. */
    bytes = (1ul << texture->log2_height) * (1ul << texture->log2_pitch) *
        2ul;
    if (texture->offset >= trap->vram_bytes ||
        bytes > trap->vram_bytes - texture->offset) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    return V9X_STATUS_OK;
}

static v9x_u32 v9x_mga3d_texsize(v9x_u32 log2_size)
{
    return (((1ul << log2_size) - 1ul) << V9X_MGA3D_TEXSIZE_MASK_SHIFT) |
        log2_size;
}

/* TMR0-TMR8, then the texture's own registers, all ahead of DWGCTL. */
static void v9x_mga3d_put_texture(struct v9x_mga3d_writes *writes,
                                  const struct v9x_mga3d_texture *texture)
{
    v9x_u32 index;
    v9x_u32 texctl;

    for (index = 0ul; index < 9ul; ++index) {
        v9x_mga3d_put(writes, V9X_MGA_TMR0 + index * 4ul,
                      (v9x_u32)texture->tmr[index]);
    }
    texctl = texture->format |
        ((texture->log2_pitch - V9X_MGA3D_TEX_PITCH_LOG2_MIN) <<
         V9X_MGA3D_TEXCTL_TPITCH_SHIFT);
    if (texture->perspective == 0ul) {
        texctl |= V9X_MGA3D_TEXCTL_NPCEN;
    }
    if (texture->clamp_v != 0ul) {
        texctl |= V9X_MGA3D_TEXCTL_CLAMPV;
    }
    if (texture->clamp_u != 0ul) {
        texctl |= V9X_MGA3D_TEXCTL_CLAMPU;
    }
    if (texture->modulate != 0ul) {
        texctl |= V9X_MGA3D_TEXCTL_TMODULATE;
    }
    if (texture->alpha_key != 0ul) {
        texctl |= V9X_MGA3D_TEXCTL_TAKEY;
    }
    if (texture->alpha_mask != 0ul) {
        texctl |= V9X_MGA3D_TEXCTL_TAMASK;
    }
    v9x_mga3d_put(writes, V9X_MGA_TEXORG, texture->offset);
    v9x_mga3d_put(writes, V9X_MGA_TEXWIDTH,
                  v9x_mga3d_texsize(texture->log2_width));
    v9x_mga3d_put(writes, V9X_MGA_TEXHEIGHT,
                  v9x_mga3d_texsize(texture->log2_height));
    v9x_mga3d_put(writes, V9X_MGA_TEXCTL, texctl);
    v9x_mga3d_put(writes, V9X_MGA_TEXTRANS,
                  (texture->key & 0xfffful) |
                  ((texture->key_mask & 0xfffful) << 16));
}

static v9x_status v9x_mga3d_check(const struct v9x_mga3d_trap *trap,
                                  v9x_u32 *pitch_pixels,
                                  v9x_u32 *origin_pixels)
{
    v9x_u32 bpp;
    v9x_u32 last_line;
    v9x_u32 end;

    bpp = trap->bytes_per_pixel;
    if (bpp != 1ul && bpp != 2ul && bpp != 4ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (trap->shade != V9X_MGA3D_SHADE_FLAT &&
        trap->shade != V9X_MGA3D_SHADE_GOURAUD) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* 8 bpp has no Gouraud: shading writes RGB (4-40). */
    if (trap->shade == V9X_MGA3D_SHADE_GOURAUD && bpp == 1ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (trap->trans > V9X_MGA3D_TRANS_MAX) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (trap->trans != 0ul && trap->shade != V9X_MGA3D_SHADE_GOURAUD) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (trap->length == 0ul || trap->pitch_bytes == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (!v9x_mga3d_edge_ok(&trap->left) || !v9x_mga3d_edge_ok(&trap->right) ||
        trap->top >= V9X_MGA3D_Y_LIMIT ||
        trap->length >= V9X_MGA3D_Y_LIMIT - trap->top) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (trap->shade == V9X_MGA3D_SHADE_GOURAUD &&
        (!v9x_mga3d_dr_ok(trap->red) || !v9x_mga3d_dr_ok(trap->green) ||
         !v9x_mga3d_dr_ok(trap->blue))) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (!v9x_mga_surface_ok(trap->pitch_bytes, trap->target_offset, bpp)) {
        return V9X_STATUS_UNSUPPORTED;
    }
    *pitch_pixels = trap->pitch_bytes / bpp;
    *origin_pixels = trap->target_offset / bpp;

    /* The rows touched must be in VRAM, and the last line's linear address
     * inside the clip window the setup opened. top + length < 32768 and the
     * pitch is at most 2048 pixels, so neither product overflows. */
    last_line = trap->top + trap->length - 1ul;
    if (last_line * *pitch_pixels > V9X_MGA3D_YBOT_OPEN - *origin_pixels) {
        return V9X_STATUS_UNSUPPORTED;
    }
    end = (last_line + 1ul) * trap->pitch_bytes;
    if (trap->target_offset >= trap->vram_bytes ||
        end > trap->vram_bytes - trap->target_offset) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    return v9x_mga3d_check_spans(trap, (v9x_s32)*pitch_pixels);
}

v9x_status v9x_mga3d_build_trap(const struct v9x_mga3d_trap *trap,
                                struct v9x_mga3d_writes *writes)
{
    struct v9x_mga3d_stepper left;
    struct v9x_mga3d_stepper right;
    v9x_u32 pitch_pixels;
    v9x_u32 origin_pixels;
    v9x_u32 zorg;
    v9x_u32 sgn = 0ul;
    v9x_u32 dwgctl;
    v9x_u32 maccess;
    v9x_status status;

    if (trap == 0 || writes == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    writes->count = 0ul;
    status = v9x_mga3d_check(trap, &pitch_pixels, &origin_pixels);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    status = v9x_mga3d_check_depth(trap, pitch_pixels, origin_pixels, &zorg);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    status = v9x_mga3d_check_texture(trap);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    v9x_mga3d_edge_terms(&trap->left, &left);
    v9x_mga3d_edge_terms(&trap->right, &right);
    if (left.direction < 0L) {
        sgn |= V9X_MGA3D_SGN_SDXL;
    }
    if (right.direction < 0L) {
        sgn |= V9X_MGA3D_SGN_SDXR;
    }

    /* Address registers first, DWGCTL near the end and the start last, the
     * order the register map asks for (2-11, note 3). */
    v9x_mga3d_put(writes, V9X_MGA_PITCH, pitch_pixels);
    v9x_mga3d_put(writes, V9X_MGA_YDSTORG, origin_pixels);
    v9x_mga3d_put(writes, V9X_MGA_AR0, (v9x_u32)left.major);
    v9x_mga3d_put(writes, V9X_MGA_AR1, (v9x_u32)left.error);
    v9x_mga3d_put(writes, V9X_MGA_AR2, (v9x_u32)left.minor);
    v9x_mga3d_put(writes, V9X_MGA_AR4, (v9x_u32)right.error);
    v9x_mga3d_put(writes, V9X_MGA_AR5, (v9x_u32)right.minor);
    v9x_mga3d_put(writes, V9X_MGA_AR6, (v9x_u32)right.major);
    v9x_mga3d_put(writes, V9X_MGA_SGN, sgn);
    v9x_mga3d_put(writes, V9X_MGA_FXBNDRY,
                  ((v9x_u32)right.x << 16) | ((v9x_u32)left.x & 0xfffful));
    if (trap->shade == V9X_MGA3D_SHADE_GOURAUD) {
        v9x_mga3d_put_channel(writes, V9X_MGA_DR4, V9X_MGA_DR6, V9X_MGA_DR7,
                              trap->red);
        v9x_mga3d_put_channel(writes, V9X_MGA_DR8, V9X_MGA_DR10,
                              V9X_MGA_DR11, trap->green);
        v9x_mga3d_put_channel(writes, V9X_MGA_DR12, V9X_MGA_DR14,
                              V9X_MGA_DR15, trap->blue);
        v9x_mga3d_put(writes, V9X_MGA_FCOL, trap->color);
        dwgctl = V9X_MGA3D_DWGCTL_GOURAUD;
        if (trap->depth != V9X_MGA3D_DEPTH_NONE) {
            /* The 32-bit pairs low half first: a write to either half
             * leaves the other (86Box's model; 2164W spec 3-39..3-44). */
            if (trap->depth == V9X_MGA3D_DEPTH_32) {
                v9x_mga3d_put(writes, V9X_MGA_DR0_Z32_LSB, trap->z32[0].lo);
                v9x_mga3d_put(writes, V9X_MGA_DR0_Z32_MSB,
                              (v9x_u32)trap->z32[0].hi & 0xfffful);
                v9x_mga3d_put(writes, V9X_MGA_DR2_Z32_LSB, trap->z32[1].lo);
                v9x_mga3d_put(writes, V9X_MGA_DR2_Z32_MSB,
                              (v9x_u32)trap->z32[1].hi & 0xfffful);
                v9x_mga3d_put(writes, V9X_MGA_DR3_Z32_LSB, trap->z32[2].lo);
                v9x_mga3d_put(writes, V9X_MGA_DR3_Z32_MSB,
                              (v9x_u32)trap->z32[2].hi & 0xfffful);
            } else {
                v9x_mga3d_put(writes, V9X_MGA_DR0, (v9x_u32)trap->z[0]);
                v9x_mga3d_put(writes, V9X_MGA_DR2, (v9x_u32)trap->z[1]);
                v9x_mga3d_put(writes, V9X_MGA_DR3, (v9x_u32)trap->z[2]);
            }
            v9x_mga3d_put(writes, V9X_MGA_ZORG, zorg);
            dwgctl = V9X_MGA3D_DWGCTL_TRAP_SHADED |
                ((trap->z_write != 0ul ? V9X_MGA3D_ATYPE_ZI
                                        : V9X_MGA3D_ATYPE_I)
                 << V9X_MGA3D_ATYPE_SHIFT) |
                (trap->zmode << V9X_MGA3D_ZMODE_SHIFT);
        }
        if (trap->texture.enabled != 0ul) {
            v9x_mga3d_put_texture(writes, &trap->texture);
            dwgctl = (dwgctl & ~V9X_MGA3D_OPCOD_MASK) |
                (V9X_MGA3D_DWGCTL_TEXTURE_TRAP & V9X_MGA3D_OPCOD_MASK);
        }
        /* MACCESS on every shaded trapezoid, so neither the Z width nor the
         * dither layout is inherited from whatever drew last. */
        maccess = trap->bytes_per_pixel == 4ul ? V9X_MGA3D_PWIDTH_32
                                               : V9X_MGA3D_PWIDTH_16;
        if (trap->depth == V9X_MGA3D_DEPTH_32) {
            maccess |= V9X_MGA3D_ZWIDTH_32;
        }
        if (trap->dither_555 != 0ul) {
            maccess |= V9X_MGA3D_DIT555;
        }
        v9x_mga3d_put(writes, V9X_MGA_MACCESS, maccess);
        dwgctl |= trap->trans << V9X_MGA3D_TRANS_SHIFT;
        v9x_mga3d_put(writes, V9X_MGA_DWGCTL, dwgctl);
    } else {
        v9x_mga3d_put(writes, V9X_MGA_FCOL,
                      v9x_mga3d_color_field(trap->color,
                                            trap->bytes_per_pixel));
        v9x_mga3d_put(writes, V9X_MGA_DWGCTL, V9X_MGA3D_DWGCTL_FLAT);
    }
    v9x_mga3d_put(writes, V9X_MGA_YDSTLEN + V9X_MGA_GO,
                  (trap->top << 16) | trap->length);
    return V9X_STATUS_OK;
}

/* A 9.15 interpolant's colour level as the engine takes it: 0 when the
 * 24-bit value is negative, otherwise its integer part's low byte. */
static v9x_u32 v9x_mga3d_level(v9x_s32 value)
{
    v9x_u32 field = (v9x_u32)value & V9X_MGA3D_DR_MASK;

    if ((field & V9X_MGA3D_DR_SIGN) != 0ul) {
        return 0ul;
    }
    return (field >> 15) & 0xfful;
}

/*
 * Depth accumulates exactly, without wrapping at the register's width: hi
 * is the full upper word of a 64-bit two's-complement value. Measured on
 * the 2164W (docs\decisions\2026-10-10-mga2164w-depth.md): running past the
 * top stores the maximum, at both widths, where a 48-bit wrap would have
 * turned the value negative and stored 0. Whether the engine's adder
 * saturates or is simply wider is not distinguished; for values that only
 * run upwards past the top, as measured, the two agree.
 */
static void v9x_mga3d_z48_add(struct v9x_mga3d_z48 *value,
                              const struct v9x_mga3d_z48 *step)
{
    v9x_u32 lo = value->lo + step->lo;
    v9x_u32 hi = (v9x_u32)value->hi + (v9x_u32)step->hi +
        (lo < value->lo ? 1ul : 0ul);

    value->hi = (v9x_s32)hi;
    value->lo = lo;
}

/* value += count * step, for the left edge's column steps (|count| is an
 * edge's step per row, a handful at most). */
static void v9x_mga3d_z48_add_times(struct v9x_mga3d_z48 *value,
                                    const struct v9x_mga3d_z48 *step,
                                    v9x_s32 count)
{
    struct v9x_mga3d_z48 negated;

    if (count < 0L) {
        negated.lo = 0ul - step->lo;
        negated.hi = -step->hi - (step->lo != 0ul ? 1L : 0L);
        step = &negated;
        count = -count;
    }
    while (count-- > 0L) {
        v9x_mga3d_z48_add(value, step);
    }
}

/* The stored Z: the integer part of the exact value, clamped to 0 below and
 * to the width's maximum above (measured, as above). */
static v9x_u32 v9x_mga3d_z32_stored(const struct v9x_mga3d_z48 *value)
{
    if (value->hi < 0L) {
        return 0ul;
    }
    if (((v9x_u32)value->hi >> 15) != 0ul) {
        return 0xfffffffful;
    }
    return (value->lo >> 15) | ((v9x_u32)value->hi << 17);
}

static v9x_u32 v9x_mga3d_z16_stored(const struct v9x_mga3d_z48 *value)
{
    v9x_u32 stored = v9x_mga3d_z32_stored(value);

    return stored > 0xfffful ? 0xfffful : stored;
}

/* A 17.15 value as the exact accumulator holds it: sign-extended. */
static void v9x_mga3d_z48_from16(struct v9x_mga3d_z48 *value, v9x_s32 z)
{
    value->lo = (v9x_u32)z;
    value->hi = z < 0L ? -1L : 0L;
}

static int v9x_mga3d_z_pass(v9x_u32 zmode, v9x_u32 incoming, v9x_u32 stored)
{
    switch (zmode) {
    case V9X_MGA3D_ZMODE_ZE:
        return incoming == stored;
    case V9X_MGA3D_ZMODE_ZNE:
        return incoming != stored;
    case V9X_MGA3D_ZMODE_ZLT:
        return incoming < stored;
    case V9X_MGA3D_ZMODE_ZLTE:
        return incoming <= stored;
    case V9X_MGA3D_ZMODE_ZGT:
        return incoming > stored;
    case V9X_MGA3D_ZMODE_ZGTE:
        return incoming >= stored;
    default:
        return 1;
    }
}

/* value >> count rounding towards minus infinity, without relying on
 * C89's implementation-defined shift of a negative value. */
static v9x_s32 v9x_mga3d_asr(v9x_s32 value, unsigned int count)
{
    if (value >= 0L) {
        return (v9x_s32)((v9x_u32)value >> count);
    }
    return (v9x_s32)~((~(v9x_u32)value) >> count);
}

/*
 * trunc(coord / (q / 65536)), q being 16.16, in integers: the model is
 * linked into tools that carry no floating-point runtime. The dividend
 * |coord| x 65536 is up to 48 bits, so the division is a 64-by-32 shift
 * and subtract. A quotient past 31 bits saturates; q = 0 gives 0, as
 * 86Box's division by an infinite q does.
 */
static v9x_s32 v9x_mga3d_div_q(v9x_s32 coord, v9x_s32 q)
{
    v9x_u32 magnitude = coord < 0L ? 0ul - (v9x_u32)coord : (v9x_u32)coord;
    v9x_u32 divisor = q < 0L ? 0ul - (v9x_u32)q : (v9x_u32)q;
    v9x_u32 high = magnitude >> 16;
    v9x_u32 low = magnitude << 16;
    v9x_u32 remainder = 0ul;
    v9x_u32 quotient = 0ul;
    int bit;
    int negative = (coord < 0L) != (q < 0L);

    if (divisor == 0ul) {
        return 0L;
    }
    if (high >= divisor) {
        quotient = 0x7ffffffful;
    } else {
        remainder = high;
        for (bit = 31; bit >= 0; --bit) {
            int carry = (remainder & 0x80000000ul) != 0ul;

            remainder = (remainder << 1) | ((low >> bit) & 1ul);
            quotient <<= 1;
            if (carry || remainder >= divisor) {
                remainder -= divisor;
                quotient |= 1ul;
            }
        }
        if (quotient > 0x7ffffffful) {
            quotient = 0x7ffffffful;
        }
    }
    return negative ? -(v9x_s32)quotient : (v9x_s32)quotient;
}

/* One texture coordinate: s (or t) at the texture's scale, divided by q
 * with perspective, then wrapped or clamped into 0 .. size - 1. */
static v9x_u32 v9x_mga3d_tex_coord(v9x_u32 accumulator, v9x_u32 q,
                                   v9x_u32 log2_size, v9x_u32 perspective,
                                   v9x_u32 clamp)
{
    v9x_s32 coord;
    v9x_s32 mask = (v9x_s32)((1ul << log2_size) - 1ul);

    if (perspective != 0ul) {
        /*
         * The card's perspective texel is floor(s / q + 1/8), not
         * floor(s / q): with q rising 1/16 a pixel it chose the next texel
         * whenever s / q fell within 0.111 to 0.130 of it, and only then
         * (docs\decisions\2026-10-10-mga2164w-textures.md). Taken in
         * eighths: floor(8v) + 1, then the integer part.
         */
        coord = v9x_mga3d_asr((v9x_s32)accumulator,
                              V9X_MGA3D_TEX_COORD_BITS -
                              V9X_MGA3D_TEX_PERSPECTIVE_BIAS_BITS -
                              (unsigned int)log2_size);
        coord = v9x_mga3d_asr(v9x_mga3d_div_q(coord, (v9x_s32)q) + 1L,
                              V9X_MGA3D_TEX_PERSPECTIVE_BIAS_BITS);
    } else {
        coord = v9x_mga3d_asr((v9x_s32)accumulator,
                              V9X_MGA3D_TEX_COORD_BITS -
                              (unsigned int)log2_size);
    }
    if (clamp != 0ul) {
        return coord < 0L ? 0ul
                          : (coord > mask ? (v9x_u32)mask : (v9x_u32)coord);
    }
    return (v9x_u32)coord & (v9x_u32)mask;
}

/* 5- and 6-bit channels widen to eight by replicating their top bits into
 * the bottom ones, as the card's modulated texels showed. */
static v9x_u32 v9x_mga3d_widen5(v9x_u32 value)
{
    return (value << 3) | (value >> 2);
}

static v9x_u32 v9x_mga3d_widen6(v9x_u32 value)
{
    return (value << 2) | (value >> 4);
}

/*
 * The textured pixel at 32 bpp, or 0 when the texel is keyed out. Started
 * from 86Box's blit_texture_trap and texture_read and corrected by the
 * card (docs\decisions\2026-10-10-mga2164w-textures.md): channels widen by
 * replication, and the written pixel carries FCOL's alpha byte. Decal shows
 * the Gouraud colour where the texel's alpha under tamask equals takey.
 * TW16 has no alpha bit and counts as 0, as measured: only tamask and
 * takey both set show its texels in decal. TW15's alpha is bit 15. TW12 is
 * not a format this chip has (format 4 drew garbage on the card), so only
 * the two 16-bit formats reach here.
 */
static int v9x_mga3d_tex_pixel(const struct v9x_mga3d_texture *texture,
                               const struct v9x_mga3d_depth_io *io,
                               const v9x_u32 *tmr, v9x_u32 i_red,
                               v9x_u32 i_green, v9x_u32 i_blue,
                               v9x_u32 *pixel)
{
    v9x_u32 s;
    v9x_u32 t;
    v9x_u32 texel;
    v9x_u32 red;
    v9x_u32 green;
    v9x_u32 blue;
    v9x_u32 alpha;
    v9x_u32 alpha_mask = texture->alpha_mask != 0ul ? 1ul : 0ul;
    v9x_u32 alpha_key = texture->alpha_key != 0ul ? 1ul : 0ul;
    int alpha_transparent;

    s = v9x_mga3d_tex_coord(tmr[0], tmr[2], texture->log2_width,
                            texture->perspective, texture->clamp_u);
    t = v9x_mga3d_tex_coord(tmr[1], tmr[2], texture->log2_height,
                            texture->perspective, texture->clamp_v);
    texel = io->texel(io->texel_context, s, t) & 0xfffful;
    if (texture->format == V9X_MGA3D_TEX_TW16) {
        red = v9x_mga3d_widen5(texel >> 11);
        green = v9x_mga3d_widen6((texel >> 5) & 0x3ful);
        blue = v9x_mga3d_widen5(texel & 0x1ful);
        alpha = 0ul;
    } else {
        red = v9x_mga3d_widen5((texel >> 10) & 0x1ful);
        green = v9x_mga3d_widen5((texel >> 5) & 0x1ful);
        blue = v9x_mga3d_widen5(texel & 0x1ful);
        alpha = texel >> 15;
    }
    alpha_transparent = (alpha & alpha_mask) == alpha_key;
    if ((texel & texture->key_mask & 0xfffful) ==
        (texture->key & 0xfffful)) {
        return 0;
    }
    if (texture->modulate != 0ul) {
        red = (red * i_red) >> 8;
        green = (green * i_green) >> 8;
        blue = (blue * i_blue) >> 8;
    } else if (alpha_transparent) {
        red = i_red;
        green = i_green;
        blue = i_blue;
    }
    *pixel = (red << 16) | (green << 8) | blue;
    return 1;
}

v9x_status v9x_mga3d_model_trap(const struct v9x_mga3d_trap *trap,
                                v9x_u32 fold, v9x_mga3d_plot_fn plot,
                                void *context,
                                const struct v9x_mga3d_depth_io *depth_io)
{
    /* s, t and q at the left edge of the row, and at the pixel. */
    v9x_u32 row_tmr[3];
    v9x_u32 tmr[3];
    v9x_u32 i_red;
    v9x_u32 i_green;
    v9x_u32 i_blue;
    v9x_u32 index;
    v9x_u32 zorg;
    struct v9x_mga3d_z48 z_steps[3];
    const struct v9x_mga3d_z48 *z_step;
    struct v9x_mga3d_z48 row_z32;
    struct v9x_mga3d_z48 z32;
    v9x_u32 incoming;
    int pass;
    struct v9x_mga3d_stepper left;
    struct v9x_mga3d_stepper right;
    v9x_u32 pitch_pixels;
    v9x_u32 origin_pixels;
    v9x_s32 row_red;
    v9x_s32 row_green;
    v9x_s32 row_blue;
    v9x_s32 red;
    v9x_s32 green;
    v9x_s32 blue;
    v9x_s32 moved;
    v9x_s32 x;
    v9x_u32 row;
    v9x_u32 pixel;
    v9x_status status;

    if (trap == 0 || plot == 0 ||
        (fold != V9X_MGA3D_FOLD_NONE && fold != V9X_MGA3D_FOLD_EDGE)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    status = v9x_mga3d_check(trap, &pitch_pixels, &origin_pixels);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    status = v9x_mga3d_check_depth(trap, pitch_pixels, origin_pixels, &zorg);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    status = v9x_mga3d_check_texture(trap);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    if ((trap->shade == V9X_MGA3D_SHADE_GOURAUD &&
         trap->bytes_per_pixel != 4ul) || trap->trans != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (trap->depth != V9X_MGA3D_DEPTH_NONE &&
        (depth_io == 0 || depth_io->read == 0 || depth_io->write == 0)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (trap->texture.enabled != 0ul &&
        (depth_io == 0 || depth_io->texel == 0)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    v9x_mga3d_edge_terms(&trap->left, &left);
    v9x_mga3d_edge_terms(&trap->right, &right);
    row_red = trap->red[0];
    row_green = trap->green[0];
    row_blue = trap->blue[0];
    /* Both widths run on the one exact accumulator: 16-bit values are
     * sign-extended into it. */
    z_step = trap->z32;
    if (trap->depth == V9X_MGA3D_DEPTH_16) {
        v9x_mga3d_z48_from16(&z_steps[0], trap->z[0]);
        v9x_mga3d_z48_from16(&z_steps[1], trap->z[1]);
        v9x_mga3d_z48_from16(&z_steps[2], trap->z[2]);
        z_step = z_steps;
    }
    row_z32 = z_step[0];
    for (index = 0ul; index < 3ul; ++index) {
        row_tmr[index] = (v9x_u32)trap->texture.tmr[6ul + index];
    }
    for (row = 0ul; row < trap->length; ++row) {
        red = row_red;
        green = row_green;
        blue = row_blue;
        z32 = row_z32;
        for (index = 0ul; index < 3ul; ++index) {
            tmr[index] = row_tmr[index];
        }
        for (x = left.x; x < right.x; ++x) {
            pass = 1;
            if (trap->shade == V9X_MGA3D_SHADE_GOURAUD) {
                i_red = v9x_mga3d_level(red);
                i_green = v9x_mga3d_level(green);
                i_blue = v9x_mga3d_level(blue);
                pixel = (trap->color & 0xff000000ul) | (i_red << 16) |
                    (i_green << 8) | i_blue;
                red += trap->red[1];
                green += trap->green[1];
                blue += trap->blue[1];
                if (trap->texture.enabled != 0ul) {
                    pass = v9x_mga3d_tex_pixel(&trap->texture, depth_io, tmr,
                                               i_red, i_green, i_blue,
                                               &pixel);
                    pixel |= trap->color & 0xff000000ul;
                    for (index = 0ul; index < 3ul; ++index) {
                        tmr[index] += (v9x_u32)trap->texture.tmr[index * 2ul];
                    }
                }
            } else {
                pixel = trap->color;
            }
            if (trap->depth != V9X_MGA3D_DEPTH_NONE) {
                incoming = trap->depth == V9X_MGA3D_DEPTH_32
                    ? v9x_mga3d_z32_stored(&z32) : v9x_mga3d_z16_stored(&z32);
                /* A transparent texel is skipped before the Z write. */
                pass = pass &&
                    v9x_mga3d_z_pass(trap->zmode, incoming,
                                     depth_io->read(depth_io->context, x,
                                                    row));
                if (pass && trap->z_write != 0ul) {
                    depth_io->write(depth_io->context, x, row, incoming);
                }
                v9x_mga3d_z48_add(&z32, &z_step[1]);
            }
            if (pass) {
                plot(context, x, row, pixel);
            }
        }
        moved = v9x_mga3d_edge_step(&left);
        v9x_mga3d_edge_step(&right);
        row_red += trap->red[2];
        row_green += trap->green[2];
        row_blue += trap->blue[2];
        v9x_mga3d_z48_add(&row_z32, &z_step[2]);
        for (index = 0ul; index < 3ul; ++index) {
            row_tmr[index] += (v9x_u32)trap->texture.tmr[index * 2ul + 1ul];
        }
        if (fold == V9X_MGA3D_FOLD_EDGE) {
            row_red += moved * trap->red[1];
            row_green += moved * trap->green[1];
            row_blue += moved * trap->blue[1];
            v9x_mga3d_z48_add_times(&row_z32, &z_step[1], moved);
            for (index = 0ul; index < 3ul; ++index) {
                row_tmr[index] += (v9x_u32)moved *
                    (v9x_u32)trap->texture.tmr[index * 2ul];
            }
        }
    }
    return V9X_STATUS_OK;
}
