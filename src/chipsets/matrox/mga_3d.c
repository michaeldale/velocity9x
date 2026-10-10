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

/* MACCESS (3-70): pwidth<1:0> 00 8 bpp, 01 16, 10 32; zwidth<3> 32-bit Z.
 * Written with depth only, so the Z width is never inherited from whatever
 * drew last. */
#define V9X_MGA3D_PWIDTH_16 0x1ul
#define V9X_MGA3D_PWIDTH_32 0x2ul
#define V9X_MGA3D_ZWIDTH_32 0x8ul

/* ZORG (3-91): a 24-bit byte address whose low nine bits must be zero. */
#define V9X_MGA3D_ZORG_ALIGN 0x00000200ul
#define V9X_MGA3D_ZORG_LIMIT 0x01000000ul

/* A 33.15 value's top half occupies <15:0> of the MSB register (3-39). */
#define V9X_MGA3D_Z48_HI_LIMIT 0x00008000L

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
        v9x_mga3d_abs(edge->dx) < V9X_MGA3D_EDGE_LIMIT;
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
            maccess = trap->bytes_per_pixel == 4ul ? V9X_MGA3D_PWIDTH_32
                                                   : V9X_MGA3D_PWIDTH_16;
            if (trap->depth == V9X_MGA3D_DEPTH_32) {
                maccess |= V9X_MGA3D_ZWIDTH_32;
            }
            v9x_mga3d_put(writes, V9X_MGA_MACCESS, maccess);
            dwgctl = V9X_MGA3D_DWGCTL_TRAP_SHADED |
                ((trap->z_write != 0ul ? V9X_MGA3D_ATYPE_ZI
                                        : V9X_MGA3D_ATYPE_I)
                 << V9X_MGA3D_ATYPE_SHIFT) |
                (trap->zmode << V9X_MGA3D_ZMODE_SHIFT);
        }
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

v9x_status v9x_mga3d_model_trap(const struct v9x_mga3d_trap *trap,
                                v9x_u32 fold, v9x_mga3d_plot_fn plot,
                                void *context,
                                const struct v9x_mga3d_depth_io *depth_io)
{
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
    if (trap->shade == V9X_MGA3D_SHADE_GOURAUD &&
        trap->bytes_per_pixel != 4ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (trap->depth != V9X_MGA3D_DEPTH_NONE &&
        (depth_io == 0 || depth_io->read == 0 || depth_io->write == 0)) {
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
    for (row = 0ul; row < trap->length; ++row) {
        red = row_red;
        green = row_green;
        blue = row_blue;
        z32 = row_z32;
        for (x = left.x; x < right.x; ++x) {
            if (trap->shade == V9X_MGA3D_SHADE_GOURAUD) {
                pixel = (trap->color & 0xff000000ul) |
                    (v9x_mga3d_level(red) << 16) |
                    (v9x_mga3d_level(green) << 8) | v9x_mga3d_level(blue);
                red += trap->red[1];
                green += trap->green[1];
                blue += trap->blue[1];
            } else {
                pixel = trap->color;
            }
            pass = 1;
            if (trap->depth != V9X_MGA3D_DEPTH_NONE) {
                incoming = trap->depth == V9X_MGA3D_DEPTH_32
                    ? v9x_mga3d_z32_stored(&z32) : v9x_mga3d_z16_stored(&z32);
                pass = v9x_mga3d_z_pass(trap->zmode, incoming,
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
        if (fold == V9X_MGA3D_FOLD_EDGE) {
            row_red += moved * trap->red[1];
            row_green += moved * trap->green[1];
            row_blue += moved * trap->blue[1];
            v9x_mga3d_z48_add_times(&row_z32, &z_step[1], moved);
        }
    }
    return V9X_STATUS_OK;
}
