/*
 * V9XHAL.DLL - flat 32-bit DirectDraw HAL for Velocity9x (S3 ViRGE/DX).
 *
 * DDRAW loads this DLL by the name returned from the 16-bit driver's
 * DDGET32BITDRIVERNAME escape and calls DriverInit with the linear address
 * of the shared V9X_DD_SHARED block. This module owns all DirectDraw
 * content: caps, mode table, callback tables, heap policy, and the runtime
 * callbacks. Flip programs the S3 CRTC display-start registers directly
 * (ring-3 port I/O; the driver's VDD registration stopped VGA trapping).
 *
 * The DLL is linked at a fixed base inside the Win9x shared arena with all
 * sections marked shared, so the flat callback pointers stored in the
 * shared block are valid in every process.
 */
#include "ddhal_internal.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

static const char v9x_hal_build_id[] = "V9XHAL build=" V9X_BUILD_ID;

/* Declared in ddhal_internal.h: every module reads the shared block. */
V9X_DD_SHARED *v9x_hal;

/*
 * Bounded callback trace (Hellbender plan H1). Events land in the shared
 * block so the last callbacks before a fault survive the faulting process.
 * Writers are allocation-free and import-free; status polls are counted
 * but kept out of the ring so a wait loop cannot flush the history.
 */
void v9x_trace_push(WORD id, DWORD detail)
{
    V9X_DD_TRACE *trace;
    DWORD slot;

    if (v9x_hal == 0) {
        return;
    }
    trace = &v9x_hal->trace;
    slot = trace->head < V9X_DD_TRACE_RING_COUNT ? trace->head : 0ul;
    trace->ring[slot].id = id;
    trace->ring[slot].seq = (WORD)trace->seq;
    trace->ring[slot].detail = detail;
    trace->head = slot + 1ul < V9X_DD_TRACE_RING_COUNT ? slot + 1ul : 0ul;
    ++trace->seq;
}

void v9x_trace_count(WORD id, DWORD detail)
{
    if (v9x_hal == 0) {
        return;
    }
    v9x_hal->trace.last_enter_id = id;
    v9x_hal->trace.last_enter_detail = detail;
    if (id < V9X_DD_TRACE_ID_COUNT) {
        ++v9x_hal->trace.counters[id];
    }
}

void v9x_trace_enter(WORD id, DWORD detail)
{
    v9x_trace_count(id, detail);
    v9x_trace_push(id, detail);
}

void v9x_trace_exit(WORD id, DWORD result)
{
    if (v9x_hal == 0) {
        return;
    }
    v9x_hal->trace.last_exit_id = id;
    v9x_hal->trace.last_exit_result = result;
    v9x_trace_push((WORD)(id | V9X_DD_TRACE_EXIT_FLAG), result);
}

#include "velocity9x/diagpaths.h"

#define V9X_TRACE_PATH V9X_DIAG_TRACE_INI

static int v9x_fault_flush_active;

static const char *v9x_trace_name(WORD id)
{
    switch (id & (WORD)~V9X_DD_TRACE_EXIT_FLAG) {
    case V9X_TRACE_DRIVERINIT:           return "DriverInit";
    case V9X_TRACE_DD16_CREATEOBJECT:    return "Dd16CreateObject";
    case V9X_TRACE_DD16_DESTROYDRIVER:   return "Dd16DestroyDriver";
    case V9X_TRACE_DD16_NEWCALLBACKFNS:  return "Dd16NewCallbackFns";
    case V9X_TRACE_DD16_GET32BITNAME:    return "Dd16Get32BitName";
    case V9X_TRACE_FLIP:                 return "Flip";
    case V9X_TRACE_GETFLIPSTATUS:        return "GetFlipStatus";
    case V9X_TRACE_LOCK:                 return "Lock";
    case V9X_TRACE_UNLOCK:               return "Unlock";
    case V9X_TRACE_BLT:                  return "Blt";
    case V9X_TRACE_GETBLTSTATUS:         return "GetBltStatus";
    case V9X_TRACE_WAITFORVBLANK:        return "WaitForVerticalBlank";
    case V9X_TRACE_SETEXCLUSIVE:         return "SetExclusiveMode";
    case V9X_TRACE_FLIPTOGDI:            return "FlipToGDISurface";
    case V9X_TRACE_GETDRIVERINFO:        return "GetDriverInfo";
    case V9X_TRACE_CANCREATESURFACE:     return "CanCreateSurface";
    case V9X_TRACE_CREATESURFACE:        return "CreateSurface";
    case V9X_TRACE_DESTROYSURFACE:       return "DestroySurface";
    case V9X_TRACE_ADDATTACHEDSURFACE:   return "AddAttachedSurface";
    case V9X_TRACE_BLT_ENGINE:           return "BltEngine";
    case V9X_TRACE_D3D_CTXCREATE:        return "D3dContextCreate";
    case V9X_TRACE_D3D_CTXDESTROY:       return "D3dContextDestroy";
    case V9X_TRACE_D3D_CTXDESTROYALL:    return "D3dContextDestroyAll";
    case V9X_TRACE_D3D_RENDERSTATE:      return "D3dRenderState";
    case V9X_TRACE_D3D_RENDERPRIM:       return "D3dRenderPrimitive";
    case V9X_TRACE_D3D_SETRENDERTARGET:  return "D3dSetRenderTarget";
    case V9X_TRACE_D3D_DRAWONEPRIM:      return "D3dDrawOnePrimitive";
    case V9X_TRACE_D3D_DRAWPRIMS:        return "D3dDrawPrimitives";
    case V9X_TRACE_D3D_DRAWONEINDEXED:   return "D3dDrawOneIndexed";
    case V9X_TRACE_D3D_TARGET_LAYOUT:    return "D3dTargetLayout";
    case V9X_TRACE_D3D_EXECUTE:          return "D3dExecute";
    case V9X_TRACE_EXEBUF_CANCREATE:     return "ExeBufCanCreate";
    case V9X_TRACE_EXEBUF_CREATE:        return "ExeBufCreate";
    case V9X_TRACE_EXEBUF_DESTROY:       return "ExeBufDestroy";
    case V9X_TRACE_EXEBUF_LOCK:          return "ExeBufLock";
    case V9X_TRACE_EXEBUF_UNLOCK:        return "ExeBufUnlock";
    case V9X_TRACE_D3D_TEXTURECREATE:    return "D3dTextureCreate";
    case V9X_TRACE_D3D_TEXTUREDESTROY:   return "D3dTextureDestroy";
    case V9X_TRACE_D3D_TEXTURESWAP:      return "D3dTextureSwap";
    case V9X_TRACE_D3D_TEXTUREGETSURF:   return "D3dTextureGetSurf";
    case V9X_TRACE_D3D_PRIMREJECT:       return "D3dPrimitiveReject";
    default:                             return "Unknown";
    }
}

static char *v9x_text_append(char *at, const char *text)
{
    while (*text != '\0') {
        *at++ = *text++;
    }
    return at;
}

static char *v9x_hex_append(char *at, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift;

    *at++ = '0';
    *at++ = 'x';
    for (shift = 28; shift >= 0; shift -= 4) {
        *at++ = digits[(value >> shift) & 0xful];
    }
    return at;
}

static void v9x_file_text(HANDLE file, const char *text)
{
    DWORD length = 0ul;
    DWORD written;

    while (text[length] != '\0') {
        ++length;
    }
    WriteFile(file, text, length, &written, 0);
}

/* This deliberately uses only fixed storage and KERNEL32 file I/O. It is
 * callable after an engine timeout and from the process exception filter,
 * where profile APIs, heap allocation, and GDI re-entry are unsafe. */
void v9x_trace_flush_fault(DWORD code, DWORD address)
{
    HANDLE file;
    char line[112];
    char *at;
    DWORD index;

    if (v9x_hal == 0 || v9x_fault_flush_active) {
        return;
    }
    v9x_fault_flush_active = 1;
    /* The diag directory normally exists by now (the 16-bit driver creates it
     * at its first boot write), but a fault dump must not depend on that. */
    CreateDirectoryA(V9X_DIAG_DIR, 0);
    file = CreateFileA(V9X_TRACE_PATH, GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, 0, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, 0);
    if (file == INVALID_HANDLE_VALUE) {
        v9x_fault_flush_active = 0;
        return;
    }
    v9x_file_text(file, "[Velocity9xTrace]\r\nFaultFlush=1\r\nBuild=");
    v9x_file_text(file, v9x_hal_build_id);
    v9x_file_text(file, "\r\n");

#define V9X_WRITE_HEX_KEY(key, value) \
    do { \
        at = v9x_text_append(line, key "="); \
        at = v9x_hex_append(at, (DWORD)(value)); \
        at = v9x_text_append(at, "\r\n"); \
        *at = '\0'; \
        v9x_file_text(file, line); \
    } while (0)

    V9X_WRITE_HEX_KEY("FaultCode", code);
    V9X_WRITE_HEX_KEY("FaultAddress", address);
    V9X_WRITE_HEX_KEY("ModeWidth", v9x_hal->fb.width);
    V9X_WRITE_HEX_KEY("ModeHeight", v9x_hal->fb.height);
    V9X_WRITE_HEX_KEY("ModeBpp", v9x_hal->fb.bits_per_pixel);
    V9X_WRITE_HEX_KEY("ModePitch", v9x_hal->fb.pitch);
    V9X_WRITE_HEX_KEY("DisplayPitch", v9x_hal->info.vmiData.lDisplayPitch);
    V9X_WRITE_HEX_KEY("DisplayFormatFlags",
                      v9x_hal->info.vmiData.ddpfDisplay.dwFlags);
    V9X_WRITE_HEX_KEY("DisplayRMask",
                      v9x_hal->info.vmiData.ddpfDisplay.dwRBitMask);
    V9X_WRITE_HEX_KEY("DisplayGMask",
                      v9x_hal->info.vmiData.ddpfDisplay.dwGBitMask);
    V9X_WRITE_HEX_KEY("DisplayBMask",
                      v9x_hal->info.vmiData.ddpfDisplay.dwBBitMask);
    V9X_WRITE_HEX_KEY("TraceEvents", v9x_hal->trace.seq);
    V9X_WRITE_HEX_KEY("LastEnterId", v9x_hal->trace.last_enter_id);
    V9X_WRITE_HEX_KEY("LastEnterDetail",
                      v9x_hal->trace.last_enter_detail);
    V9X_WRITE_HEX_KEY("LastExitId", v9x_hal->trace.last_exit_id);
    V9X_WRITE_HEX_KEY("LastExitResult",
                      v9x_hal->trace.last_exit_result);
    V9X_WRITE_HEX_KEY("EngineFifoTimeouts",
                      v9x_hal->engine.fifo_timeouts);
    V9X_WRITE_HEX_KEY("EngineIdleTimeouts",
                      v9x_hal->engine.idle_timeouts);
    V9X_WRITE_HEX_KEY("EngineResets", v9x_hal->engine.reset_count);

    for (index = 0ul; index < V9X_DD_TRACE_RING_COUNT; ++index) {
        DWORD slot = v9x_hal->trace.head + index;
        const V9X_DD_TRACE_ENTRY *entry;

        if (slot >= V9X_DD_TRACE_RING_COUNT) {
            slot -= V9X_DD_TRACE_RING_COUNT;
        }
        entry = &v9x_hal->trace.ring[slot];
        if (entry->id == 0u && entry->seq == 0u && entry->detail == 0ul) {
            continue;
        }
        at = v9x_text_append(line, "Ring=");
        at = v9x_hex_append(at, entry->seq);
        *at++ = ' ';
        at = v9x_text_append(at, v9x_trace_name(entry->id));
        at = v9x_text_append(at,
            (entry->id & V9X_DD_TRACE_EXIT_FLAG) != 0u ? " exit "
                                                       : " enter ");
        at = v9x_hex_append(at, entry->detail);
        at = v9x_text_append(at, "\r\n");
        *at = '\0';
        v9x_file_text(file, line);
    }
#undef V9X_WRITE_HEX_KEY
    FlushFileBuffers(file);
    CloseHandle(file);
    v9x_fault_flush_active = 0;
}

static LONG WINAPI v9x_unhandled_exception_filter(
    struct _EXCEPTION_POINTERS *exception)
{
    DWORD code = 0ul;
    DWORD address = 0ul;

    if (exception != 0 && exception->ExceptionRecord != 0) {
        code = exception->ExceptionRecord->ExceptionCode;
        address = (DWORD)exception->ExceptionRecord->ExceptionAddress;
    }
    v9x_trace_flush_fault(code, address);
    return EXCEPTION_CONTINUE_SEARCH;
}

/*
 * Consume one armed fault injection, if any.
 *
 * Callers gate this on the wait actually being a blocking one, so a
 * non-blocking probe that legitimately reports "not ready" never spends an
 * injection and never counts a timeout it did not take.
 */
int v9x_fault_injected(void)
{
    if (v9x_hal == 0 || v9x_hal->engine.fault_inject == 0ul) {
        return 0;
    }
    --v9x_hal->engine.fault_inject;
    return 1;
}

DWORD v9x_surface_offset(const V9X_DD_SURFACE_LCL *surface)
{
    DWORD address;

    if (surface == 0 || surface->lpGbl == 0 || v9x_hal == 0 ||
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) == 0ul) {
        return 0xfffffffful;
    }
    address = surface->lpGbl->fpVidMem;
    if (address < v9x_hal->fb.linear_base ||
        address >= v9x_hal->fb.linear_base + v9x_hal->fb.vram_bytes) {
        return 0xfffffffful;
    }
    return address - v9x_hal->fb.linear_base;
}

/*
 * Can this family program the display start?
 *
 * v9x_set_display_start writes S3 extension register CR69 to carry address
 * bits 19:16, so it is only correct on a chip that actually has S3-style
 * scanout. V9X_DD_ENGINE_CAP_FLIP says precisely that, and until now nothing
 * read it: the two S3 chips declare it, and the matrox-m2, vbe and ati
 * families declare no capabilities at all.
 *
 * Without this gate every family programs CR69 regardless of what silicon it
 * is talking to. That is a live defect in the shipping tier-0 package, which
 * writes an S3 extension register on QEMU std-vga, and it would have been an
 * unaudited register write on an ATI Rage Mobility whose only display is an
 * internal panel with no recovery path.
 *
 * The sibling v9x_in_vblank is deliberately NOT gated. It reads the standard
 * VGA input-status port 0x3DA, which is valid on every chip this driver will
 * meet, so gating it would change Matrox behaviour for no safety gain. Only
 * the CR69 write is chip-specific.
 *
 * The eventual home for both is the engine vtable, as
 * docs\plans\multi-chip-restructure.md anticipates. This is the smaller change
 * that removes the defect without moving the files.
 */
static int v9x_can_set_display_start(void)
{
    if (v9x_hal == 0 ||
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) == 0ul) {
        return 0;
    }
    return (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_FLIP) != 0ul;
}

/*
 * A flip is not done when its registers are written.
 *
 * The CRTC latches a new start address at the beginning of the next frame -
 * 86Box models it the way the silicon behaves, a memaddr_latch copied into the
 * live address at frame start - so between the write and the next vertical
 * retrace the OLD page is still what the monitor shows. Until 2026-09-03
 * GetFlipStatus answered "done" the instant Flip returned, and every double-
 * buffered application then drew its next frame into the page still being
 * scanned out. Final Reality on the emulated ViRGE flickered for exactly that
 * reason: 77 flips, 821 status polls, and every poll told it to go ahead.
 *
 * So a flip is pending until a retrace has begun since it was issued, and the
 * status call is what says so. Three states, because the flip may itself land
 * during a retrace: then the latch the application needs is the one after
 * next, so the machine waits to see the blank end before it waits to see one
 * begin. DDRAW polls the status call before it lets an application touch a
 * flip-chain surface or flip again, so this single answer is what throttles
 * the application to the refresh rate - which is what a flip is for.
 *
 * Deliberately not a wait inside Flip itself: the HAL must not spin for a
 * frame with the Win16 lock held, and an application that asked for
 * DDFLIP_NOVSYNC gets the old behaviour on request.
 */
#define V9X_FLIP_IDLE          0ul
#define V9X_FLIP_WAIT_BLANK    1ul   /* issued mid-frame: done at next blank */
#define V9X_FLIP_WAIT_UNBLANK  2ul   /* issued in a blank: see it end first  */

static DWORD v9x_flip_state = V9X_FLIP_IDLE;

static void v9x_flip_arm(int novsync)
{
    if (novsync) {
        v9x_flip_state = V9X_FLIP_IDLE;
        return;
    }
    v9x_flip_state = v9x_in_vblank() ? V9X_FLIP_WAIT_UNBLANK
                                     : V9X_FLIP_WAIT_BLANK;
}

/* Advance the state from what the CRTC says now; non-zero when the flip has
 * been taken by the scanout. */
static int v9x_flip_done(void)
{
    int blank;

    if (v9x_flip_state == V9X_FLIP_IDLE) {
        return 1;
    }
    blank = v9x_in_vblank();
    if (v9x_flip_state == V9X_FLIP_WAIT_UNBLANK) {
        if (!blank) {
            v9x_flip_state = V9X_FLIP_WAIT_BLANK;
        }
        return 0;
    }
    if (blank) {
        v9x_flip_state = V9X_FLIP_IDLE;
        return 1;
    }
    return 0;
}

static DWORD v9x_flip_body(V9X_DDHAL_FLIPDATA *data)
{
    DWORD offset = v9x_surface_offset(data->lpSurfTarg);

    if (offset == 0xfffffffful) {
        data->ddRVal = V9X_DD_OK;
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    if (v9x_engine_status_validated() &&
        !v9x_wait_idle((data->dwFlags & V9X_DDFLIP_DONOTWAIT) == 0ul)) {
        data->ddRVal = V9X_DDERR_WASSTILLDRAWING;
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    /* The previous flip has to have been taken by the scanout before this
     * one is issued, or two start addresses race for one latch and the
     * application's frame ordering is lost. DDRAW retries on this answer
     * unless the application said DONOTWAIT, in which case it is the
     * application's answer too. */
    if (!v9x_flip_done()) {
        data->ddRVal = V9X_DDERR_WASSTILLDRAWING;
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    if (data->lpSurfCurr != 0 &&
        (data->lpSurfCurr->ddsCaps & V9X_DDSCAPS_PRIMARYSURFACE) != 0ul) {
        /* Decline rather than report a flip that did not happen. A family
         * with no display-start control cannot move the scanout, and claiming
         * success would leave the caller believing a frame was presented. */
        if (!v9x_can_set_display_start()) {
            data->ddRVal = V9X_DD_OK;
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        /* Same reasoning one step further in: an offset the display-start
         * registers cannot express is declined rather than rounded, which at
         * 24 bpp would shift every pixel of the frame. */
        if (!v9x_set_display_start(offset)) {
            data->ddRVal = V9X_DD_OK;
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        v9x_flip_arm((data->dwFlags & V9X_DDFLIP_NOVSYNC) != 0ul);
    }
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xHalCanCreateSurface(
    V9X_DDHAL_CANCREATESURFACEDATA *data)
{
    const V9X_DDSURFACEDESC *desc = data != 0
        ? (const V9X_DDSURFACEDESC *)data->lpDDSurfaceDesc : 0;
    DWORD caps = desc != 0 ? desc->ddsCaps.dwCaps : 0ul;

    v9x_trace_enter(V9X_TRACE_CANCREATESURFACE, caps);
    if (data != 0) {
        data->ddRVal = V9X_DD_OK;
    }
    v9x_trace_exit(V9X_TRACE_CANCREATESURFACE, V9X_DD_OK);
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xHalCreateSurface(V9X_DDHAL_CREATESURFACEDATA *data)
{
    v9x_trace_enter(V9X_TRACE_CREATESURFACE,
                    data != 0 ? data->dwSCnt : 0ul);
    if (data != 0) {
        data->ddRVal = V9X_DD_OK;
    }
    v9x_trace_exit(V9X_TRACE_CREATESURFACE, V9X_DD_OK);
    return V9X_DDHAL_DRIVER_NOTHANDLED;
}

DWORD __stdcall V9xHalDestroySurface(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
    v9x_trace_enter(V9X_TRACE_DESTROYSURFACE,
                    data != 0 ? data->lpDDSurface : 0ul);
    if (data != 0) {
        data->ddRVal = V9X_DD_OK;
    }
    v9x_trace_exit(V9X_TRACE_DESTROYSURFACE, V9X_DD_OK);
    return V9X_DDHAL_DRIVER_NOTHANDLED;
}

DWORD __stdcall V9xHalAddAttachedSurface(
    V9X_DDHAL_ADDATTACHEDSURFACEDATA *data)
{
    v9x_trace_enter(V9X_TRACE_ADDATTACHEDSURFACE,
                    data != 0 ? data->lpSurfAttached : 0ul);
    if (data != 0) {
        data->ddRVal = V9X_DD_OK;
    }
    v9x_trace_exit(V9X_TRACE_ADDATTACHEDSURFACE, V9X_DD_OK);
    return V9X_DDHAL_DRIVER_NOTHANDLED;
}

DWORD __stdcall V9xHalFlip(V9X_DDHAL_FLIPDATA *data)
{
    V9X_FPU_AREA fpu;
    DWORD result;

    v9x_trace_enter(V9X_TRACE_FLIP, data->dwFlags);
    v9x_fpu_save(&fpu);
    result = v9x_flip_body(data);
    v9x_fpu_restore(&fpu);
    v9x_trace_exit(V9X_TRACE_FLIP, data->ddRVal);
    return result;
}

DWORD __stdcall V9xHalGetFlipStatus(V9X_DDHAL_GETFLIPSTATUSDATA *data)
{
    v9x_trace_count(V9X_TRACE_GETFLIPSTATUS, data->dwFlags);
    /* Both questions - "can I flip" and "is the last flip done" - have the
     * same answer here: not until the scanout has taken the last start
     * address. See v9x_flip_arm. */
    data->ddRVal = v9x_flip_done() ? V9X_DD_OK : V9X_DDERR_WASSTILLDRAWING;
    return V9X_DDHAL_DRIVER_HANDLED;
}

/* DDRAW leaves exclusive mode: the visible page must return to the GDI
 * surface at the start of VRAM. Layout: {lpDD, dwToGDI, ddRVal, fn}. */
typedef struct v9x_ddhal_fliptogdidata {
    DWORD lpDD;
    DWORD dwToGDI;
    DWORD dwReserved;
    DWORD ddRVal;
    DWORD FlipToGDISurface;
} V9X_DDHAL_FLIPTOGDIDATA;

DWORD __stdcall V9xHalFlipToGDISurface(V9X_DDHAL_FLIPTOGDIDATA *data)
{
    v9x_trace_enter(V9X_TRACE_FLIPTOGDI, data->dwToGDI);
    if (data->dwToGDI != 0ul) {
        if (v9x_engine_status_validated() && !v9x_wait_idle(1)) {
            data->ddRVal = V9X_DDERR_WASSTILLDRAWING;
            v9x_trace_exit(V9X_TRACE_FLIPTOGDI, data->ddRVal);
            return V9X_DDHAL_DRIVER_HANDLED;
        }
        /* Returning to GDI means scanning out from offset 0, which is where a
         * family that cannot program the display start is already sitting.
         * Skipping the write is correct rather than merely safe. */
        if (v9x_can_set_display_start()) {
            v9x_set_display_start(0ul);
        }
    }
    data->ddRVal = V9X_DD_OK;
    v9x_trace_exit(V9X_TRACE_FLIPTOGDI, data->ddRVal);
    return V9X_DDHAL_DRIVER_HANDLED;
}

typedef struct v9x_ddhal_setexclusivemodedata {
    DWORD lpDD;
    DWORD dwEnterExcl;
    DWORD dwReserved;
    DWORD ddRVal;
    DWORD SetExclusiveMode;
} V9X_DDHAL_SETEXCLUSIVEMODEDATA;

DWORD __stdcall V9xHalSetExclusiveMode(
    V9X_DDHAL_SETEXCLUSIVEMODEDATA *data)
{
    if (data == 0) {
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    v9x_trace_enter(V9X_TRACE_SETEXCLUSIVE, data->dwEnterExcl);
    if (data->dwEnterExcl == 0ul) {
        if (v9x_engine_status_validated() && !v9x_wait_idle(1)) {
            data->ddRVal = V9X_DDERR_WASSTILLDRAWING;
            v9x_trace_exit(V9X_TRACE_SETEXCLUSIVE, data->ddRVal);
            return V9X_DDHAL_DRIVER_HANDLED;
        }
        /* Leaving exclusive mode restores the GDI scanout at offset 0; same
         * reasoning as V9xHalFlipToGDISurface above. */
        if (v9x_can_set_display_start()) {
            v9x_set_display_start(0ul);
        }
    }
    data->ddRVal = V9X_DD_OK;
    v9x_trace_exit(V9X_TRACE_SETEXCLUSIVE, data->ddRVal);
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xHalLock(V9X_DDHAL_LOCKDATA *data)
{
    v9x_trace_enter(V9X_TRACE_LOCK, data->dwFlags);
    /* Serialize CPU access after asynchronous engine work. DDRAW still
     * computes and returns the actual surface pointer. */
    if (v9x_engine_status_validated() &&
        !v9x_wait_idle((data->dwFlags & V9X_DDLOCK_DONOTWAIT) == 0ul)) {
        data->ddRVal = V9X_DDERR_WASSTILLDRAWING;
        v9x_trace_exit(V9X_TRACE_LOCK, data->ddRVal);
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    data->ddRVal = V9X_DD_OK;
    v9x_trace_exit(V9X_TRACE_LOCK, data->ddRVal);
    return V9X_DDHAL_DRIVER_NOTHANDLED;
}

DWORD __stdcall V9xHalUnlock(V9X_DDHAL_UNLOCKDATA *data)
{
    v9x_trace_enter(V9X_TRACE_UNLOCK, 0ul);
    data->ddRVal = V9X_DD_OK;
    v9x_trace_exit(V9X_TRACE_UNLOCK, data->ddRVal);
    return V9X_DDHAL_DRIVER_NOTHANDLED;
}

DWORD __stdcall V9xExeBufCanCreate(V9X_DDHAL_CANCREATESURFACEDATA *data)
{
    v9x_trace_enter(V9X_TRACE_EXEBUF_CANCREATE,
                    data != 0 ? data->bIsDifferentPixelFormat : 0ul);
    if (data != 0) {
        data->ddRVal = V9X_DD_OK;
    }
    v9x_trace_exit(V9X_TRACE_EXEBUF_CANCREATE, V9X_DD_OK);
    return V9X_DDHAL_DRIVER_NOTHANDLED;
}

DWORD __stdcall V9xExeBufCreate(V9X_DDHAL_CREATESURFACEDATA *data)
{
    v9x_trace_enter(V9X_TRACE_EXEBUF_CREATE,
                    data != 0 ? data->dwSCnt : 0ul);
    if (data != 0) {
        data->ddRVal = V9X_DD_OK;
    }
    v9x_trace_exit(V9X_TRACE_EXEBUF_CREATE, V9X_DD_OK);
    return V9X_DDHAL_DRIVER_NOTHANDLED;
}

DWORD __stdcall V9xExeBufDestroy(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
    v9x_trace_enter(V9X_TRACE_EXEBUF_DESTROY, 0ul);
    if (data != 0) {
        data->ddRVal = V9X_DD_OK;
    }
    v9x_trace_exit(V9X_TRACE_EXEBUF_DESTROY, V9X_DD_OK);
    return V9X_DDHAL_DRIVER_NOTHANDLED;
}

DWORD __stdcall V9xExeBufLock(V9X_DDHAL_LOCKDATA *data)
{
    v9x_trace_enter(V9X_TRACE_EXEBUF_LOCK,
                    data != 0 ? data->dwFlags : 0ul);
    if (data != 0) {
        data->ddRVal = V9X_DD_OK;
    }
    v9x_trace_exit(V9X_TRACE_EXEBUF_LOCK, V9X_DD_OK);
    return V9X_DDHAL_DRIVER_NOTHANDLED;
}

DWORD __stdcall V9xExeBufUnlock(V9X_DDHAL_UNLOCKDATA *data)
{
    v9x_trace_enter(V9X_TRACE_EXEBUF_UNLOCK, 0ul);
    if (data != 0) {
        data->ddRVal = V9X_DD_OK;
    }
    v9x_trace_exit(V9X_TRACE_EXEBUF_UNLOCK, V9X_DD_OK);
    return V9X_DDHAL_DRIVER_NOTHANDLED;
}

/*
 * Depths the blit callbacks will take on at all.
 *
 * Widening this past 8 and 16 is not optional once 24/32-bpp modes exist:
 * DDCAPS_BLT is advertised, so DDHAL_DRIVER_NOTHANDLED reaches the application
 * as DDERR_UNSUPPORTED rather than falling back to the HEL. A depth that is
 * offered as a display mode has to have a working blit path, and the CPU
 * fallbacks in blt_cpu.c are it.
 *
 * The engines are a separate question and answer it themselves: both S3
 * engines decline above 16 bpp, so admitting the depth here routes those blits
 * to the CPU rather than to a blitter that would corrupt them.
 */
static int v9x_depth_is_blittable(DWORD bits_per_pixel)
{
    return bits_per_pixel == 8ul || bits_per_pixel == 16ul ||
           bits_per_pixel == 24ul || bits_per_pixel == 32ul;
}

static int v9x_fill_rect_valid(const V9X_DDHAL_BLTDATA *data,
                               DWORD bytes_per_pixel,
                               DWORD *offset_out)
{
    const V9X_DD_SURFACE_GBL *surface;
    DWORD offset;
    DWORD right_bytes;
    DWORD last_row;

    if (data == 0 || data->lpDDDestSurface == 0 ||
        data->lpDDDestSurface->lpGbl == 0 || bytes_per_pixel == 0ul ||
        data->rDest[0] < 0l || data->rDest[1] < 0l ||
        data->rDest[2] <= data->rDest[0] ||
        data->rDest[3] <= data->rDest[1]) {
        return 0;
    }
    surface = data->lpDDDestSurface->lpGbl;
    if (data->rDest[2] > (LONG)surface->wWidth ||
        data->rDest[3] > (LONG)surface->wHeight ||
        data->rDest[2] > 2048l || data->rDest[3] > 2048l ||
        surface->lPitch <= 0l || ((DWORD)surface->lPitch & 7ul) != 0ul) {
        return 0;
    }
    offset = v9x_surface_offset(data->lpDDDestSurface);
    if (offset == 0xfffffffful || (offset & 7ul) != 0ul) {
        return 0;
    }
    right_bytes = (DWORD)data->rDest[2] * bytes_per_pixel;
    last_row = (DWORD)(data->rDest[3] - 1l) * (DWORD)surface->lPitch;
    if (right_bytes > v9x_hal->fb.vram_bytes ||
        last_row > v9x_hal->fb.vram_bytes - right_bytes ||
        offset > v9x_hal->fb.vram_bytes - right_bytes - last_row) {
        return 0;
    }
    *offset_out = offset;
    return 1;
}

/*
 * Bounded video-memory source copy.
 *
 * A driver that sets DDCAPS_BLT owns every blit DirectDraw can express with
 * the ROPs it advertises, and the Win9x runtime will not accept DDCAPS_BLT
 * without ROP3 SRCCOPY (measured; see dd16.c). Declining a source copy after
 * claiming it does not fall back to the HEL - the runtime returns
 * DDERR_UNSUPPORTED to the application - so the claim has to be honoured.
 *
 * This is a CPU copy through the mapped linear aperture, which is what the
 * HEL would have done, so it costs nothing relative to the previous
 * behaviour while keeping the engine-accelerated colour fill reachable.
 * Replacing it with the Trio64 screen-to-screen BitBLT is the next bounded
 * 2D primitive; the surface validation here already matches that engine's
 * display-pitch constraint.
 */
static int v9x_copy_rect_valid(const V9X_DD_SURFACE_LCL *surface,
                               const LONG *rect, DWORD bytes_per_pixel,
                               DWORD *offset_out)
{
    const V9X_DD_SURFACE_GBL *global;
    DWORD offset;
    DWORD right_bytes;
    DWORD last_row;

    if (surface == 0 || surface->lpGbl == 0 ||
        rect[0] < 0l || rect[1] < 0l ||
        rect[2] <= rect[0] || rect[3] <= rect[1]) {
        return 0;
    }
    global = surface->lpGbl;
    if (rect[2] > (LONG)global->wWidth || rect[3] > (LONG)global->wHeight ||
        global->lPitch <= 0l) {
        return 0;
    }
    offset = v9x_surface_offset(surface);
    if (offset == 0xfffffffful) {
        return 0;
    }
    right_bytes = (DWORD)rect[2] * bytes_per_pixel;
    /* The row must fit the surface's own pitch. This also rejects a surface
     * whose pixel format differs from the display, whose pitch would be too
     * small for the display's bytes-per-pixel - the copy assumes both
     * surfaces carry the display format. */
    if (right_bytes > (DWORD)global->lPitch) {
        return 0;
    }
    last_row = (DWORD)(rect[3] - 1l) * (DWORD)global->lPitch;
    if (right_bytes > v9x_hal->fb.vram_bytes ||
        last_row > v9x_hal->fb.vram_bytes - right_bytes ||
        offset > v9x_hal->fb.vram_bytes - right_bytes - last_row) {
        return 0;
    }
    *offset_out = offset;
    return 1;
}

/* Drain whichever engine owns this chipset before touching the same memory
 * from the CPU. With no engine enabled there is nothing in flight. */
static int v9x_blt_drain(int wait)
{
    const V9X_ENGINE32_OPS *ops = v9x_engine32();

    if (ops != 0 && ops->status_validated()) {
        return ops->wait_idle(wait);
    }
    return 1;
}

/*
 * Resolve the engine ops lazily.
 *
 * DriverInit runs before the 16-bit side has filled the engine descriptor, so
 * there is nothing to select from at load time. Selection is on
 * engine.engine_type rather than on the V9X_DD_ENGINE_S3_* identity bits,
 * which is what lets a new chip arrive as a new table and a new enum value
 * instead of another bit and another branch.
 *
 * The result is not cached. The descriptor is refreshed on every DirectDraw
 * session setup, and a switch over two values costs far less than the risk of
 * holding a pointer that was selected from a block since invalidated. Each
 * table's own ready/validate_status still re-checks the descriptor, so a
 * resolved pointer never means the engine is usable.
 */
const V9X_ENGINE32_OPS *v9x_engine32(void)
{
    if (v9x_hal == 0 ||
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) == 0ul) {
        return 0;
    }
    switch (v9x_hal->engine.engine_type) {
    case V9X_DD_ENGINE_TYPE_S3_VIRGE_DX:
        return &v9x_engine32_virge;
    case V9X_DD_ENGINE_TYPE_S3_TRIO64:
        return &v9x_engine32_trio;
    default:
        break;
    }
    return 0;
}

static DWORD v9x_srccopy_body(V9X_DDHAL_BLTDATA *data, int *engine_used)
{
    const DWORD allowed = V9X_DDBLT_ROP | V9X_DDBLT_WAIT |
                          V9X_DDBLT_DONOTWAIT | V9X_DDBLT_ASYNC;
    DWORD bytes_per_pixel;
    DWORD source_offset;
    DWORD destination_offset;
    int wait;
    const V9X_ENGINE32_OPS *ops;

    data->ddRVal = V9X_DD_OK;
    if ((data->dwFlags & ~allowed) != 0ul ||
        ((data->dwFlags & V9X_DDBLT_ROP) != 0ul &&
         data->bltFX.dwROP != V9X_DDROP_SRCCOPY) ||
        v9x_hal == 0 || (v9x_hal->fb.flags & V9X_DD_FB_VALID) == 0ul ||
        !v9x_depth_is_blittable(v9x_hal->fb.bits_per_pixel)) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    /* No stretching, mirroring, colour keying or format conversion. */
    if (data->rSrc[2] - data->rSrc[0] != data->rDest[2] - data->rDest[0] ||
        data->rSrc[3] - data->rSrc[1] != data->rDest[3] - data->rDest[1]) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    bytes_per_pixel = v9x_hal->fb.bits_per_pixel >> 3;
    if (!v9x_copy_rect_valid(data->lpDDSrcSurface, data->rSrc,
                             bytes_per_pixel, &source_offset) ||
        !v9x_copy_rect_valid(data->lpDDDestSurface, data->rDest,
                             bytes_per_pixel, &destination_offset)) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    wait = (data->dwFlags &
            (V9X_DDBLT_ASYNC | V9X_DDBLT_DONOTWAIT)) == 0ul;
    ops = v9x_engine32();
    if (ops != 0 && ops->validate_status()) {
        int outcome = ops->copy(data, source_offset, destination_offset,
                                bytes_per_pixel, wait);

        if (outcome == V9X_BLT_BUSY) {
            data->ddRVal = V9X_DDERR_WASSTILLDRAWING;
            return V9X_DDHAL_DRIVER_HANDLED;
        }
        if (outcome == V9X_BLT_DONE) {
            *engine_used = 1;
            return V9X_DDHAL_DRIVER_HANDLED;
        }
    }

    if (!v9x_blt_drain(wait)) {
        data->ddRVal = V9X_DDERR_WASSTILLDRAWING;
        return V9X_DDHAL_DRIVER_HANDLED;
    }

    v9x_cpu_copy(data, source_offset, destination_offset,
                 bytes_per_pixel);
    return V9X_DDHAL_DRIVER_HANDLED;
}

/*
 * Colour fill.
 *
 * Advertising DDCAPS_BLT makes the driver responsible for completing every
 * blit it admits: DDHAL_DRIVER_NOTHANDLED is reported to the application as
 * DDERR_UNSUPPORTED instead of being emulated. The engine paths below can
 * each decline a shape they cannot express, so a CPU fill through the mapped
 * aperture backstops them and the callback always succeeds.
 */
static DWORD v9x_colorfill_body(V9X_DDHAL_BLTDATA *data, int *engine_used)
{
    const DWORD allowed = V9X_DDBLT_COLORFILL | V9X_DDBLT_WAIT |
                          V9X_DDBLT_DONOTWAIT | V9X_DDBLT_ASYNC;
    DWORD bytes_per_pixel;
    DWORD offset;
    int wait;
    int outcome = V9X_BLT_DECLINED;
    const V9X_ENGINE32_OPS *ops;

    if ((data->dwFlags & V9X_DDBLT_COLORFILL) == 0ul ||
        (data->dwFlags & ~allowed) != 0ul ||
        v9x_hal == 0 || (v9x_hal->fb.flags & V9X_DD_FB_VALID) == 0ul ||
        !v9x_depth_is_blittable(v9x_hal->fb.bits_per_pixel)) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    bytes_per_pixel = v9x_hal->fb.bits_per_pixel >> 3;
    if (!v9x_fill_rect_valid(data, bytes_per_pixel, &offset) ||
        (DWORD)data->rDest[2] * bytes_per_pixel >
            (DWORD)data->lpDDDestSurface->lpGbl->lPitch) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    wait = (data->dwFlags &
            (V9X_DDBLT_ASYNC | V9X_DDBLT_DONOTWAIT)) == 0ul;

    ops = v9x_engine32();
    if (ops != 0 && ops->validate_status()) {
        outcome = ops->fill(data, offset, bytes_per_pixel, wait);
    }
    if (outcome == V9X_BLT_BUSY) {
        data->ddRVal = V9X_DDERR_WASSTILLDRAWING;
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    if (outcome == V9X_BLT_DONE) {
        *engine_used = 1;
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    if (!v9x_blt_drain(wait)) {
        data->ddRVal = V9X_DDERR_WASSTILLDRAWING;
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    v9x_cpu_fill(data, offset, bytes_per_pixel);
    return V9X_DDHAL_DRIVER_HANDLED;
}

/*
 * Depth fill.
 *
 * A depth clear is a solid fill of a 16-bit surface, so this is the colour
 * fill with three differences and no new engine code: the destination is the
 * Z buffer rather than the render target, the width of a pixel comes from the
 * engine's depth format rather than from the screen mode, and the value is
 * bltFX.dwFillDepth - the same DWORD as dwFillColor, which is why the engine
 * and the CPU fallback both serve it unchanged.
 *
 * Without DDCAPS_BLTDEPTHFILL the runtime clears a Z buffer by locking it and
 * writing every word from the CPU, once per frame, through the uncached
 * aperture. That cost is part of the 28.54 to 23.62 Kpolys/s that Final
 * Reality's 25-pixel figure lost when depth testing started actually
 * happening (docs\decisions\2026-08-30-virge-depth-fifo-reservation.md).
 *
 * The screen depth is deliberately NOT consulted. A depth surface is 16-bit
 * whatever the desktop is, and gating this on v9x_depth_is_blittable as the
 * colour fill does would refuse a legitimate depth clear on an 8- or 32-bpp
 * desktop for a reason that has nothing to do with the surface being filled.
 */
static DWORD v9x_depthfill_body(V9X_DDHAL_BLTDATA *data, int *engine_used)
{
    const DWORD allowed = V9X_DDBLT_DEPTHFILL | V9X_DDBLT_WAIT |
                          V9X_DDBLT_DONOTWAIT | V9X_DDBLT_ASYNC;
    DWORD bytes_per_pixel;
    DWORD offset;
    int wait;
    int outcome = V9X_BLT_DECLINED;
    const V9X_ENGINE32_OPS *ops;

    if ((data->dwFlags & ~allowed) != 0ul ||
        v9x_hal == 0 || (v9x_hal->fb.flags & V9X_DD_FB_VALID) == 0ul ||
        data->lpDDDestSurface == 0) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    /* The destination has to actually be a depth surface. A caller that asks
     * for a depth fill of the primary is asking for the screen to be filled
     * with a Z value, and the answer to that is no rather than a rectangle of
     * garbage. */
    if ((data->lpDDDestSurface->ddsCaps & V9X_DDSCAPS_ZBUFFER) == 0ul ||
        (data->lpDDDestSurface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    bytes_per_pixel = v9x_d3d_depth_bytes_per_pixel();
    if (bytes_per_pixel == 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    if (!v9x_fill_rect_valid(data, bytes_per_pixel, &offset) ||
        (DWORD)data->rDest[2] * bytes_per_pixel >
            (DWORD)data->lpDDDestSurface->lpGbl->lPitch) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    wait = (data->dwFlags &
            (V9X_DDBLT_ASYNC | V9X_DDBLT_DONOTWAIT)) == 0ul;

    ops = v9x_engine32();
    if (ops != 0 && ops->validate_status()) {
        outcome = ops->fill(data, offset, bytes_per_pixel, wait);
    }
    if (outcome == V9X_BLT_BUSY) {
        data->ddRVal = V9X_DDERR_WASSTILLDRAWING;
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    if (outcome == V9X_BLT_DONE) {
        *engine_used = 1;
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    if (!v9x_blt_drain(wait)) {
        data->ddRVal = V9X_DDERR_WASSTILLDRAWING;
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    v9x_cpu_fill(data, offset, bytes_per_pixel);
    return V9X_DDHAL_DRIVER_HANDLED;
}

static DWORD v9x_blt_body(V9X_DDHAL_BLTDATA *data, int *engine_used)
{
    if (data == 0) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    data->ddRVal = V9X_DD_OK;
    /* Tested before the source-surface split: DDBLT_DEPTHFILL carries no
     * source, so it would otherwise fall into the colour fill, which refuses
     * it as an unknown flag - correct, but as NOTHANDLED rather than as the
     * fill it is. */
    if ((data->dwFlags & V9X_DDBLT_DEPTHFILL) != 0ul) {
        return v9x_depthfill_body(data, engine_used);
    }
    if (data->lpDDSrcSurface != 0) {
        return v9x_srccopy_body(data, engine_used);
    }
    return v9x_colorfill_body(data, engine_used);
}

DWORD __stdcall V9xHalBlt(V9X_DDHAL_BLTDATA *data)
{
    DWORD result;

    int engine_used = 0;

    v9x_trace_enter(V9X_TRACE_BLT, data != 0 ? data->dwFlags : 0ul);
    result = v9x_blt_body(data, &engine_used);
    /* Three outcomes have to stay distinguishable, and ddRVal is DD_OK for
     * all of them: the engine executed the blit (BltEngine), the HAL
     * completed it on the CPU (Blt handled, no BltEngine), or the driver
     * declined it (Blt exit NOTHANDLED). The exit detail therefore records
     * the driver return, and the engine count is a separate counter because
     * GetBltStatus polling floods the trace ring after every blit. */
    if (engine_used) {
        v9x_trace_count(V9X_TRACE_BLT_ENGINE,
                        data != 0 ? data->bltFX.dwFillColor : 0ul);
    }
    v9x_trace_exit(V9X_TRACE_BLT, result);
    return result;
}

DWORD __stdcall V9xHalGetBltStatus(V9X_DDHAL_GETBLTSTATUSDATA *data)
{
    int ready;
    const V9X_ENGINE32_OPS *ops = v9x_engine32();

    v9x_trace_count(V9X_TRACE_GETBLTSTATUS, data->dwFlags);
    if (ops == 0 || !ops->ready()) {
        data->ddRVal = V9X_DD_OK;
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    if (data->dwFlags == V9X_DDGBS_CANBLT) {
        ready = ops->can_blt();
    } else if (data->dwFlags == V9X_DDGBS_ISBLTDONE) {
        /* An engine that has never been validated has issued nothing, so
         * "is the blit done" is not this driver's question to answer. On the
         * Trio64 this test is its plain readiness check and is already true.
         * The idle poll is non-blocking, which on the ViRGE is the same
         * status-register read this branch always did. */
        if (!ops->status_validated()) {
            data->ddRVal = V9X_DD_OK;
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        ready = ops->wait_idle(0);
    } else {
        data->ddRVal = V9X_DD_OK;
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    data->ddRVal = ready ? V9X_DD_OK : V9X_DDERR_WASSTILLDRAWING;
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xHalWaitForVerticalBlank(
    V9X_DDHAL_WAITFORVERTICALBLANKDATA *data)
{
    DWORD spins;

    v9x_trace_count(V9X_TRACE_WAITFORVBLANK, data->dwFlags);
    switch (data->dwFlags) {
    case V9X_DDWAITVB_I_TESTVB:
        data->bIsInVB = v9x_in_vblank() ? 1ul : 0ul;
        data->ddRVal = V9X_DD_OK;
        return V9X_DDHAL_DRIVER_HANDLED;
    case V9X_DDWAITVB_BLOCKBEGIN:
        spins = V9X_VBLANK_SPIN_LIMIT;
        while (v9x_in_vblank() && spins-- != 0ul) {
        }
        spins = V9X_VBLANK_SPIN_LIMIT;
        while (!v9x_in_vblank() && spins-- != 0ul) {
        }
        data->ddRVal = V9X_DD_OK;
        return V9X_DDHAL_DRIVER_HANDLED;
    case V9X_DDWAITVB_BLOCKEND:
        spins = V9X_VBLANK_SPIN_LIMIT;
        while (!v9x_in_vblank() && spins-- != 0ul) {
        }
        spins = V9X_VBLANK_SPIN_LIMIT;
        while (v9x_in_vblank() && spins-- != 0ul) {
        }
        data->ddRVal = V9X_DD_OK;
        return V9X_DDHAL_DRIVER_HANDLED;
    default:
        data->ddRVal = V9X_DD_OK;
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
}

/*
 * How many of shared->modes[] to publish, or 0 to refuse the whole block.
 *
 * The mode table used to be a third hardcoded copy of the same seven rows that
 * the family C table and the INF already stated, compiled into a HAL that
 * cannot see which family it is running under - so the Matrox build, whose
 * family offers one mode, published seven. The 16-bit side owns the list now,
 * because it is the side that knows the family, and this end only checks that
 * what it was handed fits.
 */
static DWORD v9x_published_mode_count(const V9X_DD_SHARED *shared)
{
    if (shared->mode_count == 0ul ||
        shared->mode_count > (DWORD)V9X_DD_MODE_COUNT) {
        return 0ul;
    }
    return shared->mode_count;
}

DWORD __stdcall DriverInit(DWORD context)
{
    V9X_DD_SHARED *shared = (V9X_DD_SHARED *)context;
    DWORD mode_count;

    if (shared == 0 || shared->dwSize != sizeof(V9X_DD_SHARED) ||
        shared->abi != V9X_DD_SHARED_ABI) {
        return 0ul;
    }
    /* Refuse alongside the dwSize/abi check rather than publishing an empty or
     * overlong mode list: a DDHALINFO whose dwNumModes does not describe
     * lpModeInfo is worse than no driver object, and the 16-bit side already
     * treats a failed DriverInit as driverinit-pending. */
    mode_count = v9x_published_mode_count(shared);
    if (mode_count == 0ul) {
        return 0ul;
    }
    v9x_hal = shared;
    SetUnhandledExceptionFilter(v9x_unhandled_exception_filter);
    v9x_trace_enter(V9X_TRACE_DRIVERINIT, (DWORD)shared);

    shared->info.dwSize = sizeof(V9X_DDHALINFO);
    shared->info.dwNumModes = mode_count;
    shared->info.dwFlags = V9X_DDHALINFO_ISPRIMARYDISPLAY;
    shared->info.dwMonitorFrequency = 60ul;
    /* The 16-bit side stamps the owning selector immediately before
     * DDHAL_SetInfo; a flat DLL base is not a valid DDRAW16 instance. */
    shared->info.hInstance = 0ul;
    shared->info.GetDriverInfo = (V9X_DD_CODE_PTR)V9xHalGetDriverInfo;
    shared->info.lpD3DGlobalDriverData = (DWORD)&shared->d3d_global;
    shared->info.lpD3DHALCallbacks = (DWORD)&shared->d3d_callbacks;
    shared->info.lpDDExeBufCallbacks = 0;

    shared->info.vmiData.dwFlags = 0ul;
    shared->info.vmiData.dwOffscreenAlign = 8ul;
    shared->info.vmiData.dwOverlayAlign = 8ul;
    shared->info.vmiData.dwTextureAlign = 8ul;
    shared->info.vmiData.dwZBufferAlign = 8ul;
    shared->info.vmiData.dwAlphaAlign = 8ul;
    shared->info.vmiData.dwNumHeaps = 1ul;

    shared->info.ddCaps.dwSize = sizeof(V9X_DDCORECAPS);
    shared->info.ddCaps.dwCaps = V9X_DDCAPS_3D | V9X_DDCAPS_GDI |
                                 V9X_DDCAPS_BLT | V9X_DDCAPS_BLTCOLORFILL |
                                 V9X_DDCAPS_BLTDEPTHFILL;
    /*
     * DDCAPS_BLTCOLORFILL on its own is inert: without DDCAPS_BLT the runtime
     * never dispatches the Blt callback at all, which is why the bounded
     * colour fill added in 2026-08-11-virge-engine-foundation.md had never
     * executed. Claiming DDCAPS_BLT additionally requires ROP3 SRCCOPY
     * (0xcc) in dwRops or the runtime discards the whole HAL - see
     * docs/issues/2026-08-14-directdraw-hal-nohardware.md. dwRops[6] bit 12
     * is SRCCOPY (0xcc = 6 * 32 + 12); dwRops[7] bit 16 is PATCOPY (0xf0),
     * the ROP the colour fill implements.
     *
     * DDCAPS_BLTDEPTHFILL is claimed here for the whole binary, like every
     * other cap in this word, and hidden for a chip that cannot serve it by
     * the same two gates the D3D tables use: dd16.c narrows dwCaps for a
     * family whose engine_caps lack D3D, and v9x_depthfill_body declines at
     * call time when the fitted chip has no depth format. Publishing it
     * unconditionally is safe for the same reason publishing the D3D tables
     * is - see v9x_d3d_publish - and it cannot be chip-selected here anyway,
     * because DriverInit runs before the 16-bit side has described anything.
     */
    shared->info.ddCaps.dwRops[6] = 0x00001000ul;
    shared->info.ddCaps.dwRops[7] = 0x00010000ul;
    shared->info.ddCaps.ddsCaps = V9X_DDSCAPS_3DDEVICE |
                                  V9X_DDSCAPS_OFFSCREENPLAIN |
                                  V9X_DDSCAPS_FLIP |
                                  V9X_DDSCAPS_PRIMARYSURFACE |
                                  V9X_DDSCAPS_TEXTURE |
                                  V9X_DDSCAPS_COMPLEX |
                                  V9X_DDSCAPS_MIPMAP |
                                  V9X_DDSCAPS_ZBUFFER;
    /*
     * Which depths a Z buffer may be created at. DDSCAPS_ZBUFFER above says
     * the driver understands depth surfaces at all; this says what to make
     * one out of, and DirectDraw consults it before allocating one in video
     * memory. It was never assigned, so it read as zero - no acceptable
     * depth - which is why an attached Z surface could not reach the driver
     * however completely the rest of the Z path was written. The Windows 98
     * DDK's ViRGE driver sets the same value at DDDRV.C:706.
     *
     * DDBD_16 and nothing else: the S3D unit's depth path is 16-bit
     * throughout, which is also what dwDeviceZBufferBitDepth advertises on
     * the Direct3D side.
     */
    shared->info.ddCaps.dwZBufferBitDepths = V9X_DDBD_16;
    shared->info.ddCaps.dwVidMemTotal =
        shared->fb.vram_bytes - shared->fb.visible_bytes;
    shared->info.ddCaps.dwVidMemFree = shared->info.ddCaps.dwVidMemTotal;

    shared->dd_callbacks.dwSize = sizeof(V9X_DDHAL_DDCALLBACKS);
    shared->dd_callbacks.dwFlags = V9X_DDHAL_CB32_CREATESURFACE |
                                   V9X_DDHAL_CB32_CANCREATESURFACE |
                                   V9X_DDHAL_CB32_WAITFORVERTICALBLANK |
                                   V9X_DDHAL_CB32_SETEXCLUSIVEMODE |
                                   V9X_DDHAL_CB32_FLIPTOGDISURFACE;
    shared->dd_callbacks.CreateSurface =
        (V9X_DD_CODE_PTR)V9xHalCreateSurface;
    shared->dd_callbacks.CanCreateSurface =
        (V9X_DD_CODE_PTR)V9xHalCanCreateSurface;
    shared->dd_callbacks.WaitForVerticalBlank =
        (V9X_DD_CODE_PTR)V9xHalWaitForVerticalBlank;
    shared->dd_callbacks.SetExclusiveMode =
        (V9X_DD_CODE_PTR)V9xHalSetExclusiveMode;
    shared->dd_callbacks.FlipToGDISurface =
        (V9X_DD_CODE_PTR)V9xHalFlipToGDISurface;

    shared->surface_callbacks.dwSize = sizeof(V9X_DDHAL_DDSURFACECALLBACKS);
    shared->surface_callbacks.dwFlags =
        V9X_DDHAL_SURFCB32_DESTROYSURFACE |
        V9X_DDHAL_SURFCB32_FLIP | V9X_DDHAL_SURFCB32_GETFLIPSTATUS |
        V9X_DDHAL_SURFCB32_LOCK | V9X_DDHAL_SURFCB32_UNLOCK |
        V9X_DDHAL_SURFCB32_BLT | V9X_DDHAL_SURFCB32_ADDATTACHEDSURFACE |
        V9X_DDHAL_SURFCB32_GETBLTSTATUS;
    shared->surface_callbacks.DestroySurface =
        (V9X_DD_CODE_PTR)V9xHalDestroySurface;
    shared->surface_callbacks.Flip = (V9X_DD_CODE_PTR)V9xHalFlip;
    shared->surface_callbacks.GetFlipStatus =
        (V9X_DD_CODE_PTR)V9xHalGetFlipStatus;
    shared->surface_callbacks.Lock = (V9X_DD_CODE_PTR)V9xHalLock;
    shared->surface_callbacks.Unlock = (V9X_DD_CODE_PTR)V9xHalUnlock;
    shared->surface_callbacks.Blt = (V9X_DD_CODE_PTR)V9xHalBlt;
    shared->surface_callbacks.AddAttachedSurface =
        (V9X_DD_CODE_PTR)V9xHalAddAttachedSurface;
    shared->surface_callbacks.GetBltStatus =
        (V9X_DD_CODE_PTR)V9xHalGetBltStatus;

    shared->palette_callbacks.dwSize =
        sizeof(V9X_DDHAL_DDPALETTECALLBACKS);
    shared->palette_callbacks.dwFlags = 0ul;

    shared->execute_buffer_callbacks.dwSize =
        sizeof(V9X_DDHAL_DDEXEBUFCALLBACKS);
    shared->execute_buffer_callbacks.dwFlags =
        V9X_DDHAL_EXEBUFCB32_CANCREATE |
        V9X_DDHAL_EXEBUFCB32_CREATE |
        V9X_DDHAL_EXEBUFCB32_DESTROY |
        V9X_DDHAL_EXEBUFCB32_LOCK |
        V9X_DDHAL_EXEBUFCB32_UNLOCK;
    shared->execute_buffer_callbacks.CanCreateExecuteBuffer =
        (V9X_DD_CODE_PTR)V9xExeBufCanCreate;
    shared->execute_buffer_callbacks.CreateExecuteBuffer =
        (V9X_DD_CODE_PTR)V9xExeBufCreate;
    shared->execute_buffer_callbacks.DestroyExecuteBuffer =
        (V9X_DD_CODE_PTR)V9xExeBufDestroy;
    shared->execute_buffer_callbacks.LockExecuteBuffer =
        (V9X_DD_CODE_PTR)V9xExeBufLock;
    shared->execute_buffer_callbacks.UnlockExecuteBuffer =
        (V9X_DD_CODE_PTR)V9xExeBufUnlock;

    v9x_d3d_publish(shared);

    shared->cb32.Flip = (DWORD)V9xHalFlip;
    shared->cb32.GetFlipStatus = (DWORD)V9xHalGetFlipStatus;
    shared->cb32.Lock = (DWORD)V9xHalLock;
    shared->cb32.Unlock = (DWORD)V9xHalUnlock;
    shared->cb32.WaitForVerticalBlank =
        (DWORD)V9xHalWaitForVerticalBlank;
    shared->cb32.flags = 0ul;

    /* Trio64 shares the S3 scanout/vblank controls but not the ViRGE new-MMIO
     * or S3D engines. The chipset is not known here: DriverInit runs from
     * DDRAW's DDGET32BITDRIVERNAME escape, before the 16-bit side has
     * refreshed the engine descriptor. The 16-bit driver therefore owns the
     * per-chipset clamp and applies it immediately before DDHAL_SetInfo. */

    shared->hInstance = V9X_HAL_BASE;
    shared->driver_init_done = 1ul;
    v9x_trace_exit(V9X_TRACE_DRIVERINIT, 1ul);
    return 1ul;
}

BOOL __stdcall V9xHalEntry(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reason;
    (void)reserved;
    /* Keep the linker-visible reference to the build marker. */
    return v9x_hal_build_id[0] != '\0';
}

