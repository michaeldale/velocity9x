/*
 * Haswell's scanout controls for V9XHAL.DLL: plane A's base, its live
 * readback and pipe A's line. Phase F3 of
 * docs\plans\intel-haswell-direct3d.md.
 *
 * Measured with V9XHSWMM /flip on the i5-4570 (boot 16): a DSPAOFFSET x
 * then a page-aligned DSPASURF, written mid-frame, was taken at the next
 * blank, and DSPASURFLIVE then read the new base; written back, the same.
 * So a flip is a hardware flip: written at any line, pending until the
 * live base is the one written, and nothing waits for a window
 * (docs\decisions\2026-10-17-haswell-phase-f3-flips.md).
 *
 * Nothing here runs unless the 16-bit side stamped INTEL_GEN75 with
 * V9X_DD_ENGINE_CAP_FLIP, and no register is read beyond PIPEACONF and
 * DSPACNTR until both say pipe A and plane A are live. The arithmetic is
 * src\chipsets\intel\hsw_display.c, host-tested.
 */
#include "ddhal_internal.h"
#include "velocity9x/intel_hsw_display.h"

/* The base the last flip wrote, so its completion can be recognised. */
static DWORD v9x_hsw_scanout_surf = 0ul;
static int v9x_hsw_scanout_pending = 0;
/* The plane is on the vsync-off copy target: unsynced flips copy into it
 * and leave the plane where it is. Any other base write clears it. */
static int v9x_hsw_copy_scanning = 0;
/*
 * The copy target: a block of DirectDraw's heap held only while a game
 * flips unsynced, taken from the flipping object's heap (lpDD) and given
 * back at the next synced base write. A boot-time carve of 8 MiB below the
 * reserve cost every Direct3D program its textures: 3DMark 2001 SE's Car
 * Chase fell to 1-2 fps with vsync on as well as off, its refused mip trees
 * four times the full heap's (docs\decisions\2026-11-19-haswell-vsync-off-
 * async-flips.md). Its offset from fb.linear_base, page-aligned for DSPASURF.
 */
static DWORD v9x_hsw_copy_dd = 0ul;
static DWORD v9x_hsw_copy_block = 0ul;
static DWORD v9x_hsw_copy_offset = 0ul;
static DWORD v9x_hsw_note_dd = 0ul;
#define V9X_HSW_COPY_ALIGN 0x1000ul

/* Give the copy target back to the heap it came from. */
static void v9x_hsw_copy_release(void)
{
    if (v9x_hsw_copy_block != 0ul) {
        v9x_d3d_place_raw_free(v9x_hsw_copy_dd, v9x_hsw_copy_block);
    }
    v9x_hsw_copy_dd = 0ul;
    v9x_hsw_copy_block = 0ul;
    v9x_hsw_copy_offset = 0ul;
}

void v9x_hsw_scanout_note_dd(DWORD lpDD)
{
    v9x_hsw_note_dd = lpDD;
}

static volatile DWORD *v9x_hsw_scanout_reg(DWORD offset)
{
    return (volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

int v9x_hsw_scanout_active(void)
{
    if (v9x_hal == 0 ||
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) == 0ul ||
        v9x_hal->engine.engine_type != V9X_DD_ENGINE_TYPE_INTEL_GEN75 ||
        v9x_hal->engine.control_linear_base == 0ul ||
        v9x_hal->engine.mapped_aperture_bytes <
            V9X_INTEL_HSW_DSPASURFLIVE + 4ul) {
        return 0;
    }
    return (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_FLIP) != 0ul;
}

int v9x_hsw_scanout_live(void)
{
    if (!v9x_hsw_scanout_active()) {
        return 0;
    }
    return v9x_intel_hsw_scanout_live(
        *v9x_hsw_scanout_reg(V9X_INTEL_HSW_PIPEACONF),
        *v9x_hsw_scanout_reg(V9X_INTEL_HSW_DSPACNTR));
}

int v9x_hsw_in_vblank(void)
{
    if (!v9x_hsw_scanout_live()) {
        return 0;
    }
    return v9x_intel_hsw_in_vblank(
        *v9x_hsw_scanout_reg(V9X_INTEL_HSW_PIPEADSL),
        *v9x_hsw_scanout_reg(V9X_INTEL_HSW_TRANS_VTOTAL_A));
}

/* Ivy Bridge pans a linear plane through PRI_LINOFF in bytes; Haswell's
 * DSPOFFSET x is IVB's tiled offset, which a linear plane ignores. */
static int v9x_hsw_scanout_is_ivb(void)
{
    return (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_GEN7_IVB) != 0ul;
}

/*
 * The offset first, then the base, whose write arms the plane's update at
 * the next blank; the read back posts it. With async, DSPACNTR's async-flip
 * bit is set first and the base is taken at once, mid-scan: the tearing
 * flip vsync off asks for. Without it the bit is cleared, so a synced flip
 * after an unsynced one waits for the blank again (the first such write
 * may still be taken at once). DSPACNTR is written only when the bit
 * changes, and only that bit.
 */
static int v9x_hsw_write_base(DWORD byte_offset, int async)
{
    v9x_u32 surf;
    v9x_u32 plane_offset;
    v9x_status split;
    DWORD offset_register;
    DWORD control;
    DWORD wanted;

    if (!v9x_hsw_scanout_live()) {
        return 0;
    }
    v9x_hsw_copy_scanning = 0;
    if (v9x_hsw_scanout_is_ivb()) {
        split = v9x_intel_ivb_plane_split(byte_offset,
                                          v9x_hal->fb.bits_per_pixel / 8ul,
                                          &surf, &plane_offset);
        offset_register = V9X_INTEL_IVB_DSPALINOFF;
    } else {
        split = v9x_intel_hsw_plane_split(byte_offset,
                                          v9x_hal->fb.bits_per_pixel / 8ul,
                                          &surf, &plane_offset);
        offset_register = V9X_INTEL_HSW_DSPAOFFSET;
    }
    if (split != V9X_STATUS_OK) {
        return 0;
    }
    control = *v9x_hsw_scanout_reg(V9X_INTEL_HSW_DSPACNTR);
    wanted = async ? (control | V9X_INTEL_HSW_DSPCNTR_ASYNC_FLIP)
                   : (control & ~V9X_INTEL_HSW_DSPCNTR_ASYNC_FLIP);
    if (wanted != control) {
        *v9x_hsw_scanout_reg(V9X_INTEL_HSW_DSPACNTR) = wanted;
    }
    *v9x_hsw_scanout_reg(offset_register) = plane_offset;
    *v9x_hsw_scanout_reg(V9X_INTEL_HSW_DSPASURF) = surf;
    (void)*v9x_hsw_scanout_reg(V9X_INTEL_HSW_DSPASURF);
    v9x_hsw_scanout_surf = surf;
    v9x_hsw_scanout_pending = 1;
    return 1;
}

/* Synced: the plane leaves the copy target, if it was on it, and the
 * target goes back to the heap. */
int v9x_hsw_set_display_start(DWORD byte_offset)
{
    int written = v9x_hsw_write_base(byte_offset, 0);

    v9x_hsw_copy_release();
    return written;
}

/*
 * Unsynced. The flipped-to frame is copied by the blitter into the copy
 * target, and the plane scans that: the copy passes the beam once, so the
 * frame tears once, as a native driver's tearing looks. An async flip of
 * the linear plane itself tore with an ~11-line strip of a third frame at
 * every tear (docs\decisions\2026-11-19-haswell-vsync-off-async-flips.md).
 * The first such flip takes the target from the flipping object's heap and
 * moves the plane onto it with an ordinary latched write, tracked as any
 * flip; after that nothing is pending, since the plane does not move. The
 * next Flip waits for the blitter before it copies again, so copies never
 * overlap.
 *
 * No room in the heap, or a copy the blitter refuses, falls back to the
 * base written with the async-flip bit set.
 */
int v9x_hsw_set_display_start_now(DWORD byte_offset)
{
    DWORD rows = v9x_hal->fb.height;
    DWORD row_bytes = v9x_hal->fb.pitch;

    if (!v9x_hsw_scanout_live()) {
        return 0;
    }
    if (v9x_hsw_copy_block == 0ul && v9x_hsw_note_dd != 0ul &&
        v9x_d3d_place_raw(v9x_hsw_note_dd, row_bytes, rows,
                          V9X_HSW_COPY_ALIGN, &v9x_hsw_copy_block,
                          &v9x_hsw_copy_offset)) {
        v9x_hsw_copy_dd = v9x_hsw_note_dd;
    }
    if (v9x_hsw_copy_block != 0ul &&
        v9x_hsw_present_copy(byte_offset, v9x_hsw_copy_offset)) {
        if (v9x_hsw_copy_scanning) {
            v9x_hsw_scanout_pending = 0;
            return 1;
        }
        if (!v9x_hsw_write_base(v9x_hsw_copy_offset, 0)) {
            return 0;
        }
        v9x_hsw_copy_scanning = 1;
        return 1;
    }
    return v9x_hsw_write_base(byte_offset, 1);
}

/* Pending until the live base is the one written. A plane that stopped
 * being live answers "not pending"; the flip state machine has already
 * asked v9x_scanout_vblank_available and abandons the flip. */
int v9x_hsw_flip_pending(void)
{
    if (!v9x_hsw_scanout_pending || !v9x_hsw_scanout_live()) {
        return 0;
    }
    return *v9x_hsw_scanout_reg(V9X_INTEL_HSW_DSPASURFLIVE) !=
           v9x_hsw_scanout_surf;
}

/* The flip state machine has called the flip done: count whether the live
 * base held it, as the Gen3 path counts its readback. */
void v9x_hsw_note_flip_done(void)
{
    if (!v9x_hsw_scanout_pending) {
        return;
    }
    v9x_hsw_scanout_pending = 0;
    if (v9x_hsw_scanout_live() &&
        *v9x_hsw_scanout_reg(V9X_INTEL_HSW_DSPASURFLIVE) ==
            v9x_hsw_scanout_surf) {
        ++v9x_hal->d3d_diagnostics.flip_taken_at_done;
    } else {
        ++v9x_hal->d3d_diagnostics.flip_not_taken_at_done;
    }
}

/* The byte offset the plane is scanning from: the live base plus the x the
 * last write gave it (y is always 0 here), or on Ivy Bridge its LINOFF. */
DWORD v9x_hsw_displayed_offset(void)
{
    if (!v9x_hsw_scanout_live()) {
        return 0xfffffffful;
    }
    if (v9x_hsw_scanout_is_ivb()) {
        return *v9x_hsw_scanout_reg(V9X_INTEL_HSW_DSPASURFLIVE) +
               *v9x_hsw_scanout_reg(V9X_INTEL_IVB_DSPALINOFF);
    }
    return *v9x_hsw_scanout_reg(V9X_INTEL_HSW_DSPASURFLIVE) +
           (*v9x_hsw_scanout_reg(V9X_INTEL_HSW_DSPAOFFSET) &
            V9X_INTEL_HSW_OFFSET_X_MAX) *
               (v9x_hal->fb.bits_per_pixel / 8ul);
}

/* A new session or mode: the video BIOS rewrote the plane. */
void v9x_hsw_scanout_reset(void)
{
    v9x_hsw_scanout_surf = 0ul;
    v9x_hsw_scanout_pending = 0;
    v9x_hsw_copy_scanning = 0;
    /* DirectDraw rebuilds its heap for a new mode: the block is not there
     * to free. */
    v9x_hsw_copy_dd = 0ul;
    v9x_hsw_copy_block = 0ul;
    v9x_hsw_copy_offset = 0ul;
}
