/*
 * 16-bit DirectDraw ABI glue.
 *
 * This module holds no policy: it answers the DCICOMMAND escape, hands
 * DDRAW the linear address of the shared block and the V9XHAL.DLL name,
 * stamps the few 16:16 far pointers DDRAW16 requires, refreshes the
 * framebuffer descriptor from the active mode, and calls the SetInfo
 * entry captured from DDNEWCALLBACKFNS. All DirectDraw content (caps,
 * mode table, callback tables, heap policy) is built by V9XHAL.DLL.
 */
#define SetCursor V9xUserSetCursor
#include <windows.h>
#undef SetCursor

#include "velocity9x/d3dmode.h"
#include "velocity9x/diagpaths.h"
#include "velocity9x/hw16.h"
#include "velocity9x/vbe_modes.h"
#include "velocity9x/win9x_ddraw_abi.h"
#include "dd16.h"
#include "gdi_accel.h"

extern void v9x_serial_write(const char FAR *message);
extern LONG FAR PASCAL V9xDibControlCall(LPVOID device, WORD function,
                                         LPVOID input, LPVOID output);

/*
 * Which Direct3D back end this boot serves, resolved at Enable.
 *
 * Outside the target guard below, and deliberately: matrox-m2 has no
 * DirectDraw HAL but still publishes a Direct3DMode= key, because a
 * diagnostics reader that could not distinguish "this family has no HAL" from
 * "the key was never written" would be unable to tell a stale file from a
 * working one. The state it reports there is NONE, which is the truth.
 *
 * The chip's own descriptor is read here rather than taken from the shared
 * block, for the same reason gdi_accel.c reads it: the shared block does not
 * exist yet at Enable, and DirectDraw may never create it at all.
 */
#define V9X_SETTINGS_INI     "SYSTEM.INI"
#define V9X_SETTINGS_SECTION "Velocity9x"

extern const V9X_HW16_DEVICE *v9x_hw16_active_device(void);
extern DWORD FAR PASCAL V9xLinearBase(void);

static v9x_u16 v9x_dd_d3d_state = V9X_D3D_STATE_NONE;

void v9x_dd_d3d_configure(void)
{
    const V9X_HW16_DEVICE *device = v9x_hw16_active_device();
    DWORD control_base = 0ul;
    DWORD aperture_bytes = 0ul;
    DWORD engine_type = V9X_DD_ENGINE_TYPE_NONE;
    DWORD engine_caps = 0ul;
    WORD requested;

    if (device != 0 && device->fill_engine_descriptor != 0) {
        device->fill_engine_descriptor(V9xLinearBase(), &control_base,
                                       &aperture_bytes, &engine_type,
                                       &engine_caps);
    }

    /* Same section, same call and same enable-time timing as the GdiAccel
     * keys. An absent key is V9X_D3D_REQUEST_HARDWARE, so a machine that
     * never heard of this setting behaves exactly as it did before it. */
    requested = (WORD)GetPrivateProfileInt(V9X_SETTINGS_SECTION,
                                           V9X_D3D_SETTING_KEY,
                                           (int)V9X_D3D_REQUEST_HARDWARE,
                                           V9X_SETTINGS_INI);

    v9x_dd_d3d_state = v9x_d3d_mode_resolve(
        (v9x_u16)requested,
        (engine_caps & V9X_DD_ENGINE_CAP_D3D) != 0ul ? V9X_TRUE : V9X_FALSE);
}

const char *v9x_dd_d3d_state_text(void)
{
    return v9x_d3d_mode_text(v9x_dd_d3d_state);
}

#ifndef V9X_TARGET_MATROX_MILLENNIUM2

extern WORD FAR PASCAL V9xDdSharedAlloc(void);
extern DWORD FAR PASCAL V9xDdSharedLinear(void);
extern DWORD FAR PASCAL V9xHardwareBase(void);

typedef WORD (FAR PASCAL *V9X_SETINFO_FN)(V9X_DDHALINFO FAR *info,
                                          WORD reset);

static V9X_DD_SHARED FAR *v9x_dd_shared;
static V9X_SETINFO_FN v9x_dd_set_info;
static V9X_DDHALINFO v9x_dd_info16;
static V9X_DDHAL_DDCALLBACKS v9x_dd_callbacks16;
static V9X_DDHAL_DDSURFACECALLBACKS v9x_dd_surface_callbacks16;
static V9X_DDHAL_DDPALETTECALLBACKS v9x_dd_palette_callbacks16;
static V9X_VIDMEM v9x_dd_heap16;
static V9X_DDHALMODEINFO v9x_dd_modes16[V9X_DD_MODE_COUNT];

static void v9x_dd_copy(void FAR *destination, const void FAR *source,
                        WORD bytes)
{
    BYTE FAR *out = (BYTE FAR *)destination;
    const BYTE FAR *in = (const BYTE FAR *)source;

    while (bytes-- != 0u) {
        *out++ = *in++;
    }
}

/* runtime.asm: create V9X_DIAG_DIR once before the first diagnostic write. */
extern void FAR PASCAL V9xEnsureDiagDir(void);

static void v9x_dd_trace(const char FAR *stage)
{
    V9xEnsureDiagDir();
    WritePrivateProfileString("Velocity9xDDraw", "Stage", stage,
                              V9X_DIAG_DDHOOK_INI);
}

/* Stage= is last-write-wins, and DDRAW's retry pattern makes the last write
 * frequently a transient ("setinfo-callback-missing" from a retry that raced
 * DDNEWCALLBACKFNS) even on a boot that went on to complete. Measured on the
 * GMA 950 netbook: CountDd16CreateObject=3 against one NewCallbackFns, session
 * Result=COMPLETE, yet the file's only Stage was the miss. LastGoodStage= is
 * the same key discipline applied only to stages that mean forward progress,
 * so the file can no longer report a healthy boot as a failure. */
static void v9x_dd_trace_good(const char FAR *stage)
{
    v9x_dd_trace(stage);
    WritePrivateProfileString("Velocity9xDDraw", "LastGoodStage", stage,
                              V9X_DIAG_DDHOOK_INI);
}

/* 16-bit writer for the shared callback trace ring (same record layout as
 * the 32-bit HAL writers in ddhal.c). */
static void v9x_dd_trace_event(WORD id, DWORD detail)
{
    V9X_DD_TRACE FAR *trace;
    DWORD slot;

    if (v9x_dd_shared == 0) {
        return;
    }
    trace = &v9x_dd_shared->trace;
    if ((id & V9X_DD_TRACE_EXIT_FLAG) != 0u) {
        trace->last_exit_id = id & (WORD)~V9X_DD_TRACE_EXIT_FLAG;
        trace->last_exit_result = detail;
    } else {
        trace->last_enter_id = id;
        trace->last_enter_detail = detail;
        if (id < V9X_DD_TRACE_ID_COUNT) {
            ++trace->counters[id];
        }
    }
    slot = trace->head < V9X_DD_TRACE_RING_COUNT ? trace->head : 0ul;
    trace->ring[slot].id = id;
    trace->ring[slot].seq = (WORD)trace->seq;
    trace->ring[slot].detail = detail;
    trace->head = slot + 1ul < V9X_DD_TRACE_RING_COUNT ? slot + 1ul : 0ul;
    ++trace->seq;
}

/* Provided by ddi.c: the live PDEVICE far pointer and the active mode. */
extern V9X_DD_VOID_PTR v9x_dd_active_pdevice(void);
extern WORD v9x_dd_active_mode(WORD FAR *width, WORD FAR *height,
                               WORD FAR *bpp, WORD FAR *pitch);
extern WORD v9x_dd_screen_selector(void);
extern WORD v9x_dd_enable_count(void);
extern WORD v9x_dd_disable_count(void);

/* enable16.c: VBE-reported VRAM, 0 unless the tier-0 path ran. */
extern DWORD v9x_vbe_vram_bytes;

/* modes16.c: the committed runtime table, its parallel masks and the per-row
 * publication bytes. */
extern V9X_HW16_MODE v9x_runtime_modes[];
extern struct v9x_mode_masks v9x_runtime_masks[];
extern v9x_u8 v9x_runtime_publication[];
extern WORD v9x_runtime_count;
extern WORD v9x_modes16_is_published(WORD index);

/*
 * Publish the runtime mode table to DirectDraw.
 *
 * This lives on the 16-bit side because this is the side that owns the table:
 * one V9XHAL.DLL ships to every family, and the rows here are whatever
 * modes16.c committed at load - the family baseline alone on a non-scanning
 * build, or the baseline merged with the BIOS's own list.
 *
 * Only published rows are offered, so this list is a subset of GDI's
 * published list by construction and can never advertise a geometry
 * ValidateMode would refuse. The masks come from the parallel runtime mask
 * table - the BIOS's own answer for a scanned row, the canonical layout for a
 * baseline one - so DirectDraw describes exactly the surface the DIB engine
 * draws. 8 bpp is palettized and carries none.
 *
 * Capacity is bounded: when more rows are published than the shared block
 * holds (QEMU publishes 46 against 32 slots), v9x_vbe_dd_subset chooses -
 * every 8/16-bpp row in table order, then 32 bpp by ascending area. The
 * active desktop row is then guaranteed a slot: if the subset cut it, it
 * replaces the last (lowest-priority) selection, because a DirectDraw list
 * without the mode the primary surface is in is useless to every caller.
 *
 * Refresh remains the 60 Hz convention, recorded as such in the inventory.
 */
static void v9x_dd_fill_modes(V9X_DD_SHARED FAR *shared)
{
    /* Static, not stack: 64 words is more than the Win16 stack owes us. */
    static v9x_u16 chosen_rows[V9X_MODE_TABLE_MAX];
    WORD chosen;
    WORD index;
    WORD active_width;
    WORD active_height;
    WORD active_bpp;
    WORD active_pitch;
    WORD have_active;

    chosen = 0u;
    for (index = 0u; index < v9x_runtime_count &&
                     chosen < (WORD)V9X_MODE_TABLE_MAX; ++index) {
        if (v9x_modes16_is_published(index) != 0u) {
            chosen_rows[chosen++] = index;
        }
    }
    if (chosen > (WORD)V9X_DD_MODE_COUNT) {
        chosen = v9x_vbe_dd_subset(v9x_runtime_modes, v9x_runtime_count,
                                   v9x_runtime_publication, chosen_rows,
                                   (v9x_u16)V9X_DD_MODE_COUNT);
    }

    /* The active desktop row must be present. v9x_dd_active_mode is gated on
     * an enabled PDEVICE; before the first Enable there is no desktop row to
     * guarantee and the ordinary selection stands. */
    have_active = v9x_dd_active_mode(&active_width, &active_height,
                                     &active_bpp, &active_pitch);
    if (have_active != 0u && chosen != 0u) {
        WORD present = 0u;

        for (index = 0u; index < chosen; ++index) {
            const V9X_HW16_MODE *row = &v9x_runtime_modes[chosen_rows[index]];

            if (row->width == active_width && row->height == active_height &&
                row->bits_per_pixel == active_bpp) {
                present = 1u;
                break;
            }
        }
        if (present == 0u) {
            for (index = 0u; index < v9x_runtime_count; ++index) {
                const V9X_HW16_MODE *row = &v9x_runtime_modes[index];

                if (v9x_modes16_is_published(index) != 0u &&
                    row->width == active_width &&
                    row->height == active_height &&
                    row->bits_per_pixel == active_bpp) {
                    chosen_rows[chosen - 1u] = index;
                    break;
                }
            }
        }
    }

    for (index = 0u; index < chosen; ++index) {
        const V9X_HW16_MODE *source = &v9x_runtime_modes[chosen_rows[index]];
        const struct v9x_mode_masks *layout =
            &v9x_runtime_masks[chosen_rows[index]];
        V9X_DDHALMODEINFO FAR *mode = &shared->modes[index];

        mode->dwWidth = (DWORD)source->width;
        mode->dwHeight = (DWORD)source->height;
        mode->lPitch = (LONG)(DWORD)source->pitch;
        mode->dwBPP = (DWORD)source->bits_per_pixel;
        mode->wRefreshRate = 60u;
        mode->dwAlphaBitMask = 0ul;
        if (source->bits_per_pixel == 8u) {
            mode->wFlags = V9X_DDMODEINFO_PALETTIZED;
            mode->dwRBitMask = 0ul;
            mode->dwGBitMask = 0ul;
            mode->dwBBitMask = 0ul;
        } else {
            mode->wFlags = 0u;
            mode->dwRBitMask = layout->red;
            mode->dwGBitMask = layout->green;
            mode->dwBBitMask = layout->blue;
        }
    }
    shared->mode_count = (DWORD)chosen;
}

static V9X_DD_SHARED FAR *v9x_dd_block(void)
{
    WORD selector;

    if (v9x_dd_shared != 0) {
        return v9x_dd_shared;
    }
    selector = V9xDdSharedAlloc();
    if (selector == 0u) {
        return 0;
    }
    v9x_dd_shared = (V9X_DD_SHARED FAR *)MAKELP(selector, 0u);
    {
        BYTE FAR *bytes = (BYTE FAR *)v9x_dd_shared;
        WORD index;

        for (index = 0u; index < sizeof(V9X_DD_SHARED); ++index) {
            bytes[index] = 0u;
        }
    }
    v9x_dd_shared->dwSize = sizeof(V9X_DD_SHARED);
    v9x_dd_shared->abi = V9X_DD_SHARED_ABI;
    v9x_dd_fill_modes(v9x_dd_shared);
    v9x_dd_trace_good("shared-ready");
    return v9x_dd_shared;
}

/* The only 16-bit HAL callback: DDRAW is done with the driver object. */
static DWORD __loadds FAR PASCAL v9x_dd_destroy_driver(
    V9X_DDHAL_DESTROYDRIVERDATA FAR *data)
{
    v9x_dd_trace_event(V9X_TRACE_DD16_DESTROYDRIVER, 0ul);
    data->ddRVal = V9X_DD_OK;
    v9x_dd_set_info = 0;
    v9x_serial_write("V9X-DD destroy-driver\r\n");
    return V9X_DDHAL_DRIVER_HANDLED;
}

static void v9x_dd_refresh_framebuffer(void)
{
    V9X_DD_SHARED FAR *shared = v9x_dd_shared;
    const V9X_HW16_DEVICE *device;
    WORD width;
    WORD height;
    WORD bpp;
    WORD pitch;

    if (shared == 0) {
        return;
    }
    if (v9x_dd_active_mode(&width, &height, &bpp, &pitch) == 0u) {
        shared->fb.flags &= ~V9X_DD_FB_VALID;
        return;
    }
    shared->fb.linear_base = V9xLinearBase();
    shared->fb.physical_base = V9xHardwareBase();
    /* Tier-0 learns this from VBE 4F00h, and a family with a read_video_memory
     * hook from its own chip - CR36 on both S3 parts. The literal is the last
     * resort for a family with neither, or one whose size code did not decode:
     * the Millennium II today. It is an assumption, and on a card holding less
     * than 4 MiB it is an over-advertisement that DirectDraw will allocate
     * against, so a family that can read the real size should. */
    shared->fb.vram_bytes = v9x_vbe_vram_bytes != 0ul ? v9x_vbe_vram_bytes
                                                      : 0x00400000ul;
    shared->fb.pitch = pitch;
    shared->fb.width = width;
    shared->fb.height = height;
    shared->fb.bits_per_pixel = bpp;
    shared->fb.visible_bytes = (DWORD)pitch * (DWORD)height;
    shared->fb.screen_selector = (DWORD)v9x_dd_screen_selector();
    shared->fb.enable_count = (DWORD)v9x_dd_enable_count();
    shared->fb.disable_count = (DWORD)v9x_dd_disable_count();
    shared->fb.flags |= V9X_DD_FB_VALID;

    /* V9xHardwareEnable maps the complete 64-MiB ViRGE linear aperture.
     * New-MMIO is a 64-KiB window at BAR + 16 MiB; register offsets such as
     * SUBSYS_STAT (0x8504) are relative to that window, not to VRAM. */
    device = v9x_hw16_active_device();
    if (device != 0 && device->fill_engine_descriptor != 0) {
        DWORD control_base = 0ul;
        DWORD aperture_bytes = 0ul;
        DWORD engine_type = V9X_DD_ENGINE_TYPE_NONE;
        DWORD engine_caps = 0ul;

        device->fill_engine_descriptor(shared->fb.linear_base,
                                       &control_base, &aperture_bytes,
                                       &engine_type, &engine_caps);
        /*
         * The user's Direct3D setting, applied before the stamp rather than
         * after it.
         *
         * Everything downstream already reads engine_caps as the statement of
         * what may be advertised - V9xDdCreateDriverObject's clamp below nulls
         * GetDriverInfo and both lpD3D* pointers when the D3D bit is clear,
         * and has done since the Trio64 shipped - so clearing the bit here
         * needs no new code anywhere else and no change at all on the 32-bit
         * side. It also keeps the 16-bit side the single capability authority
         * it is documented to be: a setting can take a capability away, and
         * v9x_d3d_mode_resolve is what guarantees it cannot grant one.
         */
        if (v9x_d3d_mode_advertises(v9x_dd_d3d_state) == V9X_FALSE) {
            engine_caps &= ~V9X_DD_ENGINE_CAP_D3D;
        }
        shared->engine.control_linear_base = control_base;
        shared->engine.mapped_aperture_bytes = aperture_bytes;
        shared->engine.engine_type = engine_type;
        shared->engine.engine_caps = engine_caps;
        /* VALID says the descriptor was filled in, nothing more. Which chip
         * this is, and what its engine will do, are engine_type and
         * engine_caps above; the per-chip identity bits that used to be
         * derived here retired with the 32-bit vtable.
         *
         * That derivation also read as ViRGE for any engine_type it did not
         * recognise, including NONE. A family with a descriptor hook but no
         * engine now says so. */
        shared->engine.flags = V9X_DD_ENGINE_VALID;
    } else {
        shared->engine.control_linear_base = 0ul;
        shared->engine.mapped_aperture_bytes = 0ul;
        shared->engine.engine_type = V9X_DD_ENGINE_TYPE_NONE;
        shared->engine.engine_caps = 0ul;
        shared->engine.flags = 0ul;
    }
    shared->engine.io_base = 0ul;
    shared->engine.crtc_index_port = 0ul;
    /* fault_inject is deliberately NOT cleared here. This runs on every
     * DirectDraw session setup, which is exactly between arming the injector
     * and the workload that is supposed to consume it, so clearing it made
     * the knob unusable. v9x_dd_block zeroes the whole block on allocation,
     * so it starts disarmed; after that the escape is the only writer and
     * the HAL's own consumption is the only decrementer. A forced timeout
     * acts on whichever bounded wait runs next, which is mode-independent. */
    shared->engine.reserved1 = 0ul;
}

/*
 * Copy the mode-dependent DDHALINFO fields from the DLL-built mode table
 * and the framebuffer descriptor (mechanical field plumbing only).
 */
static void v9x_dd_refresh_info(void)
{
    V9X_DD_SHARED FAR *shared = v9x_dd_shared;
    V9X_DDHALINFO FAR *info;
    WORD index;

    if (shared == 0 || shared->driver_init_done == 0ul ||
        (shared->fb.flags & V9X_DD_FB_VALID) == 0ul) {
        return;
    }
    info = &shared->info;
    info->vmiData.fpPrimary = shared->fb.linear_base;
    info->vmiData.dwDisplayWidth = shared->fb.width;
    info->vmiData.dwDisplayHeight = shared->fb.height;
    info->vmiData.lDisplayPitch = (LONG)shared->fb.pitch;
    for (index = 0u; index < (WORD)shared->mode_count; ++index) {
        if (shared->modes[index].dwWidth == shared->fb.width &&
            shared->modes[index].dwHeight == shared->fb.height &&
            shared->modes[index].dwBPP == shared->fb.bits_per_pixel) {
            info->dwModeIndex = index;
            info->vmiData.ddpfDisplay.dwSize = sizeof(V9X_DDPIXELFORMAT);
            info->vmiData.ddpfDisplay.dwFlags = V9X_DDPF_RGB;
            if ((shared->modes[index].wFlags &
                 V9X_DDMODEINFO_PALETTIZED) != 0u) {
                info->vmiData.ddpfDisplay.dwFlags |=
                    V9X_DDPF_PALETTEINDEXED8;
            }
            info->vmiData.ddpfDisplay.dwRGBBitCount =
                shared->modes[index].dwBPP;
            info->vmiData.ddpfDisplay.dwRBitMask =
                shared->modes[index].dwRBitMask;
            info->vmiData.ddpfDisplay.dwGBitMask =
                shared->modes[index].dwGBitMask;
            info->vmiData.ddpfDisplay.dwBBitMask =
                shared->modes[index].dwBBitMask;
            break;
        }
    }
    shared->heaps[0].dwFlags = V9X_VIDMEM_ISLINEAR;
    shared->heaps[0].fpStart =
        shared->fb.linear_base + shared->fb.visible_bytes;
    shared->heaps[0].fpEnd =
        shared->fb.linear_base + shared->fb.vram_bytes - 1ul;
    shared->heaps[0].ddsCaps = 0ul;
    shared->heaps[0].ddsCapsAlt = 0ul;
    shared->heaps[0].lpHeap = 0ul;
    info->vmiData.dwNumHeaps = 1ul;
    /* DriverInit runs before the framebuffer descriptor is valid, so the
     * video-memory totals it computed were zero. The heap is known here. */
    info->ddCaps.dwVidMemTotal =
        shared->fb.vram_bytes - shared->fb.visible_bytes;
    info->ddCaps.dwVidMemFree = info->ddCaps.dwVidMemTotal;

    /* 16:16 far aliases DDRAW16 dereferences. */
    info->lpDDCallbacks = &shared->dd_callbacks;
    info->lpDDSurfaceCallbacks = &shared->surface_callbacks;
    info->lpDDPaletteCallbacks = &shared->palette_callbacks;
    info->vmiData.pvmList = &shared->heaps[0];
    info->lpModeInfo = &shared->modes[0];
    info->lpdwFourCC = 0;
    /* DDRAW16 identifies the HAL by the selector which owns its callback
     * tables (Win98 DDK S3 sample: SELECTOROF(&sData)), not by the flat
     * image base of the 32-bit companion DLL. */
    info->hInstance = (DWORD)SELECTOROF((LPVOID)&v9x_dd_shared);
    /* DriverInit supplies the flat DX5 extension callback. Preserve it when
     * refreshing the mode-dependent fields before SetInfo. */
    info->lpPDevice = v9x_dd_active_pdevice();
    shared->dd_callbacks.DestroyDriver =
        (V9X_DD_CODE_PTR)v9x_dd_destroy_driver;
}

WORD FAR PASCAL V9xDdCreateDriverObject(WORD reset)
{
    WORD result;
    WORD index;

    if (v9x_dd_set_info == 0) {
        v9x_dd_trace("setinfo-callback-missing");
        return 0u;
    }
    if (v9x_dd_block() == 0) {
        return 0u;
    }
    v9x_dd_trace_event(V9X_TRACE_DD16_CREATEOBJECT, (DWORD)reset);
    /* Refill on every driver-object refresh, not just at allocation: a live
     * mode switch can move the desktop onto a row the previous subset cut,
     * and the fill is what guarantees the active row a slot. */
    v9x_dd_fill_modes(v9x_dd_shared);
    v9x_dd_refresh_framebuffer();
    v9x_dd_refresh_info();
    if (v9x_dd_shared->driver_init_done == 0ul) {
        /* DriverInit has not filled the content yet; DDRAW retries via
         * the DDCREATEDRIVEROBJECT escape after loading V9XHAL.DLL. */
        v9x_dd_trace("driverinit-pending");
        return 0u;
    }
    /* DDRAW16 retains these 16:16 tables after SetInfo. Keep them in the
     * display driver's DGROUP exactly like the Windows 98 S3 sample; the
     * separately allocated shared selector remains only 16/32-bit state. */
    v9x_dd_copy(&v9x_dd_info16, &v9x_dd_shared->info,
                sizeof(v9x_dd_info16));
    v9x_dd_copy(&v9x_dd_callbacks16, &v9x_dd_shared->dd_callbacks,
                sizeof(v9x_dd_callbacks16));
    v9x_dd_copy(&v9x_dd_surface_callbacks16,
                &v9x_dd_shared->surface_callbacks,
                sizeof(v9x_dd_surface_callbacks16));
    v9x_dd_copy(&v9x_dd_palette_callbacks16,
                &v9x_dd_shared->palette_callbacks,
                sizeof(v9x_dd_palette_callbacks16));
    v9x_dd_copy(&v9x_dd_heap16, &v9x_dd_shared->heaps[0],
                sizeof(v9x_dd_heap16));
    for (index = 0u; index < (WORD)v9x_dd_shared->mode_count; ++index) {
        v9x_dd_copy(&v9x_dd_modes16[index], &v9x_dd_shared->modes[index],
                    sizeof(v9x_dd_modes16[index]));
    }
    v9x_dd_info16.lpDDCallbacks = &v9x_dd_callbacks16;
    v9x_dd_info16.lpDDSurfaceCallbacks = &v9x_dd_surface_callbacks16;
    v9x_dd_info16.lpDDPaletteCallbacks = &v9x_dd_palette_callbacks16;
    v9x_dd_info16.vmiData.pvmList = &v9x_dd_heap16;
    v9x_dd_info16.lpModeInfo = &v9x_dd_modes16[0];
    v9x_dd_info16.hInstance =
        (DWORD)SELECTOROF((LPVOID)&v9x_dd_info16);

    /* The shared HAL binary describes the ViRGE feature set. A family whose
     * engine does not claim D3D keeps the scanout and vertical-blank services
     * but has neither the new-MMIO window nor the S3D engine, so the
     * description handed to DDRAW is narrowed here - on the DGROUP copy only,
     * leaving the shared block's full description intact for the 32-bit side.
     *
     * Driven by engine_caps rather than by a build-time define: the 16-bit
     * side is the capability authority, so a family that does not claim D3D
     * cannot have it advertised on its behalf. */
    if ((v9x_dd_shared->engine.engine_caps & V9X_DD_ENGINE_CAP_D3D) == 0ul) {
    v9x_dd_info16.GetDriverInfo = 0;
    v9x_dd_info16.lpD3DGlobalDriverData = 0ul;
    v9x_dd_info16.lpD3DHALCallbacks = 0ul;
    v9x_dd_info16.lpDDExeBufCallbacks = 0;
    /* Only the Direct3D bit differs from the shared description; the blitter
     * caps and the dwRops table it depends on are set once in DriverInit and
     * carried through by the copy above. */
    v9x_dd_info16.ddCaps.dwCaps = V9X_DDCAPS_GDI | V9X_DDCAPS_BLT |
        V9X_DDCAPS_BLTCOLORFILL;
    v9x_dd_info16.ddCaps.ddsCaps =
        V9X_DDSCAPS_OFFSCREENPLAIN | V9X_DDSCAPS_FLIP |
        V9X_DDSCAPS_PRIMARYSURFACE | V9X_DDSCAPS_COMPLEX;
    v9x_dd_surface_callbacks16.dwFlags =
        V9X_DDHAL_SURFCB32_DESTROYSURFACE |
        V9X_DDHAL_SURFCB32_FLIP |
        V9X_DDHAL_SURFCB32_GETFLIPSTATUS |
        V9X_DDHAL_SURFCB32_LOCK | V9X_DDHAL_SURFCB32_UNLOCK |
        V9X_DDHAL_SURFCB32_ADDATTACHEDSURFACE |
        V9X_DDHAL_SURFCB32_BLT | V9X_DDHAL_SURFCB32_GETBLTSTATUS;
    }

    v9x_dd_trace_event(6u, v9x_dd_callbacks16.dwFlags);
    v9x_dd_trace_event(7u, (DWORD)v9x_dd_callbacks16.WaitForVerticalBlank);
    v9x_dd_trace_event(8u, v9x_dd_info16.ddCaps.dwCaps);
    v9x_dd_trace_event(9u, v9x_dd_info16.ddCaps.ddsCaps);
    result = v9x_dd_set_info(&v9x_dd_info16, reset);
    v9x_dd_trace_event((WORD)(V9X_TRACE_DD16_CREATEOBJECT |
                              V9X_DD_TRACE_EXIT_FLAG),
                       (DWORD)result);
    v9x_serial_write(result != 0u ? "V9X-DD setinfo-ok\r\n"
                                  : "V9X-DD setinfo-fail\r\n");
    if (result != 0u) {
        v9x_dd_trace_good("setinfo-ok");
    } else {
        v9x_dd_trace("setinfo-fail");
    }
    return result;
}

void FAR PASCAL V9xDdInvalidate(void)
{
    if (v9x_dd_shared != 0) {
        v9x_dd_shared->fb.flags &= ~V9X_DD_FB_VALID;
        v9x_dd_shared->engine.flags &= ~V9X_DD_ENGINE_VALID;
    }
}

static LONG v9x_dd_command(V9X_DCICMD FAR *command, LPVOID output)
{
    switch (command->dwCommand) {
    case V9X_DDCREATEDRIVEROBJECT:
        if (V9xDdCreateDriverObject(0u) == 0u) {
            return 0;
        }
        if (output != 0) {
            *(DWORD FAR *)output =
                (DWORD)SELECTOROF((LPVOID)&v9x_dd_shared);
        }
        return 1;
    case V9X_DDGET32BITDRIVERNAME:
        if (v9x_dd_block() == 0 || output == 0) {
            return 0;
        }
        {
            static const char name[] = "V9XHAL.DLL";
            static const char entry[] = "DriverInit";
            V9X_DD32BITDRIVERDATA FAR *data =
                (V9X_DD32BITDRIVERDATA FAR *)output;
            WORD index;

            for (index = 0u; index < sizeof(data->szName); ++index) {
                data->szName[index] =
                    index < sizeof(name) ? name[index] : '\0';
            }
            for (index = 0u; index < sizeof(data->szEntryPoint); ++index) {
                data->szEntryPoint[index] =
                    index < sizeof(entry) ? entry[index] : '\0';
            }
            data->dwContext = V9xDdSharedLinear();
        }
        v9x_dd_trace_event(V9X_TRACE_DD16_GET32BITNAME, 0ul);
        v9x_serial_write("V9X-DD get32bitname\r\n");
        v9x_dd_trace_good("get32bitname");
        return 1;
    case V9X_DDNEWCALLBACKFNS:
        {
            V9X_DDHALDDRAWFNS FAR *fns =
                (V9X_DDHALDDRAWFNS FAR *)command->dwParam1;

            if (fns == 0) {
                return 0;
            }
            v9x_dd_set_info = (V9X_SETINFO_FN)fns->lpSetInfo;
        }
        v9x_dd_trace_event(V9X_TRACE_DD16_NEWCALLBACKFNS, 0ul);
        v9x_serial_write("V9X-DD newcallbackfns\r\n");
        v9x_dd_trace_good("newcallbackfns");
        return 1;
    case V9X_DDGETTRACE:
        /* Copy a snapshot of the trace state for the diagnostics tool.
         * Byte copy keeps the 16-bit build free of runtime helpers. */
        if (v9x_dd_block() == 0 || output == 0) {
            return 0;
        }
        {
            V9X_DD_TRACE_SNAPSHOT FAR *snapshot =
                (V9X_DD_TRACE_SNAPSHOT FAR *)output;
            const BYTE FAR *source;
            BYTE FAR *destination;
            WORD index;

            snapshot->dwSize = sizeof(V9X_DD_TRACE_SNAPSHOT);
            snapshot->abi = v9x_dd_shared->abi;
            snapshot->driver_init_done = v9x_dd_shared->driver_init_done;
            source = (const BYTE FAR *)&v9x_dd_shared->fb;
            destination = (BYTE FAR *)&snapshot->fb;
            for (index = 0u; index < sizeof(V9X_DD_FRAMEBUFFER); ++index) {
                destination[index] = source[index];
            }
            source = (const BYTE FAR *)&v9x_dd_shared->engine;
            destination = (BYTE FAR *)&snapshot->engine;
            for (index = 0u; index < sizeof(V9X_DD_ENGINE); ++index) {
                destination[index] = source[index];
            }
            source = (const BYTE FAR *)&v9x_dd_shared->d3d_diagnostics;
            destination = (BYTE FAR *)&snapshot->d3d;
            for (index = 0u; index < sizeof(V9X_D3D_DIAGNOSTICS); ++index) {
                destination[index] = source[index];
            }
            source = (const BYTE FAR *)&v9x_dd_shared->trace;
            destination = (BYTE FAR *)&snapshot->trace;
            for (index = 0u; index < sizeof(V9X_DD_TRACE); ++index) {
                destination[index] = source[index];
            }
        }
        return 1;
    case V9X_DDFAULTINJECT:
        /* Arm the 32-bit side's engine fault injector. Writing the count is
         * the whole operation: this side never touches the engine, and the
         * HAL decrements it as the forced timeouts are consumed. */
        if (v9x_dd_block() == 0) {
            return 0;
        }
        v9x_dd_shared->engine.fault_inject = command->dwParam1;
        v9x_dd_trace("faultinject");
        return 1;
    case V9X_DDVERSIONINFO:
        if (output != 0) {
            V9X_DDVERSIONDATA FAR *version =
                (V9X_DDVERSIONDATA FAR *)output;

            version->dwHALVersion = V9X_DD_RUNTIME_VERSION;
            version->dwReserved1 = 0ul;
            version->dwReserved2 = 0ul;
        }
        return 1;
    default:
        return 0;
    }
}

#else /* no DirectDraw HAL on this target */

/*
 * ddi.c calls these on every Enable, Disable and ReEnable. Rather than guard
 * each call site on the target, a family without a HAL links the no-op forms.
 * The full versions are retired into a family capability at phase 6 of
 * docs\plans\multi-chip-restructure.md.
 */
WORD FAR PASCAL V9xDdCreateDriverObject(WORD reset)
{
    (void)reset;
    return 0u;
}

void FAR PASCAL V9xDdInvalidate(void)
{
}

#endif /* DirectDraw targets */

/*
 * The two GDI acceleration escapes.
 *
 * Served on every family, including one with no DirectDraw HAL at all, and so
 * deliberately outside the target guard below: GDI acceleration is a display
 * driver service, its code links into all four binaries, and its counters are
 * the only evidence that a primitive fired rather than declined. A harness
 * that could not read them on an engine-less family could not tell "declined
 * correctly" from "never reached", which is the whole failure mode the /accel
 * phase exists to detect.
 */
static LONG v9x_gdi_command(V9X_DCICMD FAR *command, LPVOID output)
{
    switch (command->dwCommand) {
    case V9X_GDIGETSTATS:
        return v9x_gdi_accel_stats(output) != 0u ? 1 : 0;
    case V9X_GDIFAULTINJECT:
        return v9x_gdi_accel_fault_inject(command->dwParam1) != 0u ? 1 : 0;
    default:
        return 0;
    }
}

LONG __loadds FAR PASCAL Control(LPVOID device,
                                 WORD function,
                                 LPVOID input,
                                 LPVOID output)
{
    if (function == V9X_DCICOMMAND && input != 0) {
        V9X_DCICMD FAR *command = (V9X_DCICMD FAR *)input;

        if (command->dwVersion == V9X_DD_VERSION &&
            (command->dwCommand == V9X_GDIGETSTATS ||
             command->dwCommand == V9X_GDIFAULTINJECT)) {
            return v9x_gdi_command(command, output);
        }
    }
#ifndef V9X_TARGET_MATROX_MILLENNIUM2
    if (function == V9X_QUERYESCSUPPORT && input != 0) {
        if (*(WORD FAR *)input == V9X_DCICOMMAND) {
            return (LONG)V9X_DD_HAL_VERSION;
        }
    } else if (function == V9X_DCICOMMAND && input != 0) {
        V9X_DCICMD FAR *command = (V9X_DCICMD FAR *)input;

        if (command->dwVersion == V9X_DD_VERSION) {
            return v9x_dd_command(command, output);
        }
        /* Real DCI and unknown versions fall through to the DIB engine
         * (required for correct behavior of the emulated path). */
    }
#endif
    return V9xDibControlCall(device, function, input, output);
}
