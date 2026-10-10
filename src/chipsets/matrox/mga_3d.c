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
    v9x_u32 sgn = 0ul;
    v9x_status status;

    if (trap == 0 || writes == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    writes->count = 0ul;
    status = v9x_mga3d_check(trap, &pitch_pixels, &origin_pixels);
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
        v9x_mga3d_put(writes, V9X_MGA_DWGCTL, V9X_MGA3D_DWGCTL_GOURAUD);
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

v9x_status v9x_mga3d_model_trap(const struct v9x_mga3d_trap *trap,
                                v9x_u32 fold, v9x_mga3d_plot_fn plot,
                                void *context)
{
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
    if (trap->shade == V9X_MGA3D_SHADE_GOURAUD &&
        trap->bytes_per_pixel != 4ul) {
        return V9X_STATUS_UNSUPPORTED;
    }

    v9x_mga3d_edge_terms(&trap->left, &left);
    v9x_mga3d_edge_terms(&trap->right, &right);
    row_red = trap->red[0];
    row_green = trap->green[0];
    row_blue = trap->blue[0];
    for (row = 0ul; row < trap->length; ++row) {
        red = row_red;
        green = row_green;
        blue = row_blue;
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
            plot(context, x, row, pixel);
        }
        moved = v9x_mga3d_edge_step(&left);
        v9x_mga3d_edge_step(&right);
        row_red += trap->red[2];
        row_green += trap->green[2];
        row_blue += trap->blue[2];
        if (fold == V9X_MGA3D_FOLD_EDGE) {
            row_red += moved * trap->red[1];
            row_green += moved * trap->green[1];
            row_blue += moved * trap->blue[1];
        }
    }
    return V9X_STATUS_OK;
}
