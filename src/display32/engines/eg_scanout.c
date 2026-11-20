/*
 * The HD 5450's scanout controls for V9XHAL.DLL: the start address in
 * display controller D1's GRPH_PRIMARY_SURFACE_ADDRESS, the flip status in
 * GRPH_UPDATE's SURFACE_UPDATE_PENDING, and the vertical blank in
 * CRTC_STATUS (Linux evergreen_reg.h; the write sequence is
 * evergreen_page_flip's).
 *
 * Without these every flip was declined and DirectDraw emulated it by
 * swapping the buffers' memory, so the surface the monitor scans took its
 * turn as the back buffer: the owner of M-BXMFA8 saw 3DMark 2000 corrupt
 * every second frame while screenshots, taken after the drawing, looked
 * clean (2026-10-27). With GRPH_FLIP_CONTROL clear the controller takes a
 * new address at the vertical blank, so a flip is written at any line and
 * is pending until the update-pending bit clears, as the Rage 128's is.
 *
 * D1 is the controller the VBE drove on M-BXMFA8 (its register probe read
 * D1 scanning GPU address 0, docs\decisions\2026-10-26-cedar-register-
 * probe.md) and on M-D7TFW4's Caicos, whose DCE 5 Linux flips with the
 * same evergreen_page_flip (its probe read D1 scanning GPU F_00000000,
 * docs\decisions\2026-10-31-caicos-register-probe.md). Flips are taken only while D1's CRTC and graphics plane are
 * enabled; otherwise the scanout is not live and the core declines them as
 * before. Nothing here runs unless the 16-bit side stamped the Evergreen
 * engine with V9X_DD_ENGINE_CAP_FLIP. The address arithmetic is
 * eg_dma.c's v9x_eg_scanout_address, host-tested.
 *
 * Carrizo's DCE 11 controller D1 has the same registers at the same byte
 * offsets and the same fields (Linux dce_11_0_d.h and _sh_mask.h), and
 * Linux's dce_v11_0_page_flip writes the same sequence. What differs is the
 * address: the carve-out sits at GPU F4_00000000 on M-XP7053, so
 * GRPH_PRIMARY_SURFACE_ADDRESS_HIGH takes bits 39:32 where Evergreen's is
 * 0, and the heap's address and the drain before a flip are the SDMA
 * engine's (eng_vi.c). D1 is the controller the VBE drives there too
 * (docs\decisions\2026-10-28-carrizo-register-probe.md).
 */
#include "ddhal_internal.h"
#include "velocity9x/eg_dma.h"

/* D1's registers (evergreen_reg.h, CRTC0's offset 0). */
#define V9X_EG_GRPH_ENABLE              0x6800ul
#define V9X_EG_GRPH_PRIMARY_ADDRESS     0x6810ul
#define V9X_EG_GRPH_PRIMARY_ADDRESS_HI  0x681cul
#define V9X_EG_GRPH_UPDATE              0x6844ul
#define V9X_EG_GRPH_FLIP_CONTROL        0x6848ul
#define V9X_EG_CRTC_CONTROL             0x6e70ul
#define V9X_EG_CRTC_STATUS              0x6e8cul
#define V9X_EG_GRPH_ENABLE_BIT          0x00000001ul
#define V9X_EG_GRPH_UPDATE_PENDING      0x00000004ul
#define V9X_EG_CRTC_MASTER_EN           0x00000001ul
#define V9X_EG_CRTC_V_BLANK             0x00000001ul
/* GRPH_FLIP_CONTROL clear: the update is taken at the vertical blank.
 * SURFACE_UPDATE_H_RETRACE_EN takes it at the next horizontal blank, the
 * unsynced flip: Linux's evergreen_page_flip and dce_v6_0_page_flip write
 * it for an async flip (evergreen_reg.h, dce_6_0_sh_mask.h: bit 0). */
#define V9X_EG_FLIP_AT_VBLANK           0x00000000ul
#define V9X_EG_FLIP_AT_HRETRACE         0x00000001ul

static int v9x_eg_scanout_pending = 0;

static volatile DWORD *v9x_eg_scanout_reg(DWORD offset)
{
    return (volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

int v9x_eg_scanout_active(void)
{
    if (v9x_hal == 0 ||
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) == 0ul ||
        (v9x_hal->engine.engine_type != V9X_DD_ENGINE_TYPE_ATI_EVERGREEN &&
         v9x_hal->engine.engine_type != V9X_DD_ENGINE_TYPE_ATI_VI) ||
        v9x_hal->engine.control_linear_base == 0ul ||
        v9x_hal->engine.mapped_aperture_bytes < V9X_EG_CRTC_STATUS + 4ul) {
        return 0;
    }
    return (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_FLIP) != 0ul;
}

int v9x_eg_scanout_live(void)
{
    if (!v9x_eg_scanout_active()) {
        return 0;
    }
    return (*v9x_eg_scanout_reg(V9X_EG_CRTC_CONTROL) &
            V9X_EG_CRTC_MASTER_EN) != 0ul &&
           (*v9x_eg_scanout_reg(V9X_EG_GRPH_ENABLE) &
            V9X_EG_GRPH_ENABLE_BIT) != 0ul;
}

int v9x_eg_in_vblank(void)
{
    if (!v9x_eg_scanout_active()) {
        return 0;
    }
    return (*v9x_eg_scanout_reg(V9X_EG_CRTC_STATUS) &
            V9X_EG_CRTC_V_BLANK) != 0ul;
}

/* The heap's GPU address and the frame finished before it is shown: on
 * Evergreen the DMA engine's address with its high byte (0 on Cedar, 0Fh on
 * Caicos), after the CP's drawing and the DMA engine's last fill or copy;
 * on Carrizo the SDMA engine's. The CP's copy of the address served Cedar
 * until Caicos, whose VRAM above 4 GiB the CP refuses. */
static int v9x_eg_scanout_heap(DWORD *high, DWORD *low)
{
    if (v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_ATI_VI) {
        return v9x_vi_heap_address(high, low) && v9x_vi_dma_settle();
    }
    return v9x_eg_heap_address(high, low) && v9x_eg_cp_drain(1) &&
           v9x_eg_dma_settle();
}

/* An unsynced write is taken at the next line, not the next blank: until
 * then the buffer leaving the screen is still scanned. Taken at the blank
 * and called done once written, the GCN path gave that buffer back to the
 * game at once, and 3DMark 2001 SE at 160 fps drew into it on the panel:
 * the R7 250 showed frames missing sky, car bodies and the HUD (OBS,
 * 2026-11-20). The pending bit is polled either way. */
static void v9x_eg_scanout_write(DWORD heap_high, v9x_u32 address, int now)
{
    *v9x_eg_scanout_reg(V9X_EG_GRPH_FLIP_CONTROL) =
        now ? V9X_EG_FLIP_AT_HRETRACE : V9X_EG_FLIP_AT_VBLANK;
    *v9x_eg_scanout_reg(V9X_EG_GRPH_PRIMARY_ADDRESS_HI) = heap_high;
    *v9x_eg_scanout_reg(V9X_EG_GRPH_PRIMARY_ADDRESS) = address;
    /* Posts the write, as Linux does. */
    (void)*v9x_eg_scanout_reg(V9X_EG_GRPH_PRIMARY_ADDRESS);
}

/*
 * QUEUED FLIPS on the GCN parts (engine type VI: Carrizo, Mullins, Oland).
 *
 * Before these, every Flip waited for the GPU to finish the frame, then
 * DirectDraw spun on Flip until the scanout took it: the CPU built nothing
 * while the GPU drew, and the GPU drew nothing while the CPU waited. On
 * the Oland's 3DMark 2000 run (boot 95) 22 % of the program's samples
 * were the flip-pending reads and 9 % and 5 % the SDMA and CP fence waits
 * (docs\decisions\2026-11-17-oland-queued-flips.md).
 *
 * Now the flip notes the frame's end - the CP's last fence and SDMA's
 * last sequence (v9x_vi_frame_mark) - and returns. The address is written
 * by the first poll that finds both engines past the mark: the flip state
 * machine's (Flip, GetFlipStatus, a draw or fill waiting on the buffer
 * still shown) or a submission's (v9x_eg_flip_advance). The register
 * sequence is the one that was measured, unchanged: GRPH_FLIP_CONTROL
 * clear, taken at the next vertical blank. Until the write the flip is
 * pending, so a second Flip, and anything drawn into the buffer on the
 * panel, waits as it did for the latch: the CPU runs one frame ahead and
 * no further. An unsynced flip is written at the same point, to be taken
 * at the next line, and is pending until the controller says it took it
 * (v9x_eg_set_display_start_now).
 *
 * Evergreen's own CP is not queued: it keeps the drain and the immediate
 * write.
 */
static int v9x_eg_deferred = 0;
static int v9x_eg_deferred_now = 0;
static DWORD v9x_eg_deferred_high;
static v9x_u32 v9x_eg_deferred_address;
static DWORD v9x_eg_deferred_cp;
static DWORD v9x_eg_deferred_dma;

static int v9x_eg_queues(void)
{
    return v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_ATI_VI;
}

int v9x_eg_flip_queues(void)
{
    return v9x_eg_scanout_active() && v9x_eg_queues();
}

/* The deferred address, once its frame is finished; non-zero when nothing
 * is left deferred. A scanout that went away drops it. */
static int v9x_eg_flip_release(void)
{
    if (!v9x_eg_deferred) {
        return 1;
    }
    if (!v9x_vi_frame_done(v9x_eg_deferred_cp, v9x_eg_deferred_dma)) {
        return 0;
    }
    v9x_eg_deferred = 0;
    if (v9x_eg_scanout_live()) {
        v9x_eg_scanout_write(v9x_eg_deferred_high, v9x_eg_deferred_address,
                             v9x_eg_deferred_now);
    }
    return 1;
}

void v9x_eg_flip_advance(void)
{
    if (v9x_eg_deferred) {
        (void)v9x_eg_flip_release();
    }
}

static int v9x_eg_display_start(DWORD byte_offset, int now)
{
    DWORD heap_high;
    DWORD heap_low;
    v9x_u32 address;

    if (!v9x_eg_scanout_live()) {
        return 0;
    }
    if (v9x_eg_queues()) {
        if (!v9x_vi_heap_address(&heap_high, &heap_low) ||
            v9x_eg_scanout_address(heap_low, byte_offset,
                                   v9x_hal->fb.vram_bytes, &address) !=
                V9X_STATUS_OK) {
            return 0;
        }
        /* A newer start replaces one still deferred: the core only gets
         * here once the last flip is done, so that is the GDI return
         * (FlipToGDISurface, SetExclusiveMode), which drained first. */
        v9x_eg_deferred_high = heap_high;
        v9x_eg_deferred_address = address;
        v9x_eg_deferred_now = now;
        v9x_vi_frame_mark(&v9x_eg_deferred_cp, &v9x_eg_deferred_dma);
        v9x_eg_deferred = 1;
        v9x_eg_scanout_pending = 1;
        (void)v9x_eg_flip_release();
        return 1;
    }
    if (!v9x_eg_scanout_heap(&heap_high, &heap_low) ||
        v9x_eg_scanout_address(heap_low, byte_offset,
                               v9x_hal->fb.vram_bytes, &address) !=
            V9X_STATUS_OK) {
        return 0;
    }
    v9x_eg_scanout_write(heap_high, address, now);
    v9x_eg_scanout_pending = 1;
    return 1;
}

int v9x_eg_set_display_start(DWORD byte_offset)
{
    return v9x_eg_display_start(byte_offset, 0);
}

/* Unsynced: the same write, taken at the next horizontal blank, so it
 * tears instead of waiting for the frame. */
int v9x_eg_set_display_start_now(DWORD byte_offset)
{
    return v9x_eg_display_start(byte_offset, 1);
}

/* Pending while a queued flip's frame is unfinished, then while the
 * controller has the address but has not yet taken it: at a vertical
 * blank, or for an unsynced flip at the next horizontal one. */
int v9x_eg_flip_pending(void)
{
    if (!v9x_eg_scanout_pending) {
        return 0;
    }
    if (!v9x_eg_flip_release()) {
        return 1;
    }
    if (!v9x_eg_scanout_live()) {
        return 0;
    }
    return (*v9x_eg_scanout_reg(V9X_EG_GRPH_UPDATE) &
            V9X_EG_GRPH_UPDATE_PENDING) != 0ul;
}

/* The flip state machine has called the flip done: count whether the chip
 * agreed, as the Rage 128 and Haswell paths do. */
void v9x_eg_note_flip_done(void)
{
    if (!v9x_eg_scanout_pending) {
        return;
    }
    v9x_eg_scanout_pending = 0;
    if (v9x_eg_scanout_live() &&
        (*v9x_eg_scanout_reg(V9X_EG_GRPH_UPDATE) &
         V9X_EG_GRPH_UPDATE_PENDING) == 0ul) {
        ++v9x_hal->d3d_diagnostics.flip_taken_at_done;
    } else {
        ++v9x_hal->d3d_diagnostics.flip_not_taken_at_done;
    }
}

/* A new session or mode: the video BIOS rewrote the controller. */
void v9x_eg_scanout_reset(void)
{
    v9x_eg_scanout_pending = 0;
    v9x_eg_deferred = 0;
}
