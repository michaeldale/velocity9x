/*
 * Velocity9x HAL trace dump (Hellbender plan H1).
 *
 * Reads the bounded callback trace out of the driver's shared block with
 * the project-private V9X_DDGETTRACE DCICOMMAND escape and writes it to
 * C:\V9XDIAG\V9XSNAP.INI. The HAL reserves C:\V9XDIAG\V9XTRACE.INI for
 * automatic fault and
 * engine-timeout captures, so a manual snapshot cannot erase crash evidence. It
 * reports the last completed HAL callbacks, per-callback counts, and engine
 * timeout counters. The tool follows the diagnostic-suite rule of runtime-
 * free static imports (KERNEL32/USER32/GDI32 only).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/win9x_ddraw_abi.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_SECTION      "Velocity9xTrace"

/*
 * Where this snapshot goes, which is NOT always the same file.
 *
 * The counters in the shared block are cumulative from the boot: nothing
 * resets them per application, and DriverInit resets only the flip and
 * completion state. So a trace taken after one game and then another after
 * a second game describe the two games ADDED TOGETHER, and the second used
 * to overwrite the first - the section is cleared before writing - which
 * left no way to attribute anything to either.
 *
 * Rolling over fixes both at once. Each run takes the first free name, so
 * the traces survive side by side and the second game's own numbers are
 * the difference between them. Monotonic counters subtract; the "last",
 * "min" and "max" fields do not, and describe whichever run wrote them.
 *
 * Eight is the limit and the last one is reused after that, because a
 * machine reached by carrying a USB stick should not silently stop
 * recording, and because eight runs is more than one sitting.
 */
#define V9X_RESULT_MAX_FILES 8u
static char v9x_result_path[] = V9X_DIAG_SNAP_INI;
/* The digit sits where the final P of V9XSNAP does: V9XSNAP.INI becomes
 * V9XSNA1.INI and so on, which stays inside 8.3. */
#define V9X_RESULT_DIGIT (sizeof(v9x_result_path) - 6u)

static const char *v9x_result_file(void)
{
    static int chosen = 0;
    unsigned int index;

    if (chosen) {
        return v9x_result_path;
    }
    chosen = 1;
    for (index = 0u; index < V9X_RESULT_MAX_FILES; ++index) {
        if (index == 0u) {
            v9x_result_path[V9X_RESULT_DIGIT] = 'P';
        } else {
            v9x_result_path[V9X_RESULT_DIGIT] = (char)('0' + index);
        }
        if (GetFileAttributesA(v9x_result_path) == 0xFFFFFFFFul) {
            return v9x_result_path;
        }
    }
    /* All taken: reuse the last rather than stop recording. */
    v9x_result_path[V9X_RESULT_DIGIT] =
        (char)('0' + (V9X_RESULT_MAX_FILES - 1u));
    return v9x_result_path;
}

#define V9X_RESULT_PATH  v9x_result_file()

static void v9x_uint_text(char *text, DWORD value)
{
    char reverse[12];
    int count = 0;
    int index;

    do {
        reverse[count++] = (char)('0' + (value % 10ul));
        value /= 10ul;
    } while (value != 0ul);
    for (index = 0; index < count; ++index) {
        text[index] = reverse[count - index - 1];
    }
    text[count] = '\0';
}

static void v9x_hex_text(char *text, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    int index;

    text[0] = '0';
    text[1] = 'x';
    for (index = 0; index < 8; ++index) {
        text[2 + index] = digits[(value >> ((7 - index) * 4)) & 0xful];
    }
    text[10] = '\0';
}

static void v9x_write_text(const char *key, const char *value)
{
    WritePrivateProfileStringA(V9X_SECTION, key, value, V9X_RESULT_PATH);
}

static void v9x_write_uint(const char *key, DWORD value)
{
    char text[12];

    v9x_uint_text(text, value);
    v9x_write_text(key, text);
}

static void v9x_write_hex(const char *key, DWORD value)
{
    char text[11];

    v9x_hex_text(text, value);
    v9x_write_text(key, text);
}

/*
 * "Census<nn><field>" for the command-word table, assembled rather than
 * listed: five fields times up to thirty-two rows is too many names to spell
 * out, and this tool links no runtime string routines.
 */
static void v9x_census_name(char *out, DWORD index, const char *field)
{
    DWORD at = 0ul;
    const char *prefix = "Census";

    while (*prefix != 0) {
        out[at++] = *prefix++;
    }
    out[at++] = (char)('0' + (index / 10ul));
    out[at++] = (char)('0' + (index % 10ul));
    while (*field != 0) {
        out[at++] = *field++;
    }
    out[at] = 0;
}

/*
 * "Present<nn><field>" for the present trace, assembled for the same reason
 * the census names are: four fields times thirty-two rows is too many names
 * to spell out in a tool that links no runtime string routines.
 */
static void v9x_present_name(char *out, DWORD index, const char *field)
{
    DWORD at = 0ul;
    const char *prefix = "Present";

    while (*prefix != 0) {
        out[at++] = *prefix++;
    }
    out[at++] = (char)('0' + (index / 10ul));
    out[at++] = (char)('0' + (index % 10ul));
    while (*field != 0) {
        out[at++] = *field++;
    }
    out[at] = 0;
}

/* "Wm<n><field>" for the per-mode watermark log, assembled like the census
 * and present names because this tool links no runtime string routines. */
static void v9x_wm_name(char *out, DWORD index, const char *field)
{
    DWORD at = 0ul;
    const char *prefix = "Wm";

    while (*prefix != 0) {
        out[at++] = *prefix++;
    }
    out[at++] = (char)('0' + (index % 10ul));
    while (*field != 0) {
        out[at++] = *field++;
    }
    out[at] = 0;
}

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
    case V9X_TRACE_D3D_EXECUTE:          return "D3dExecute";
    case V9X_TRACE_EXEBUF_CANCREATE:     return "ExeBufCanCreate";
    case V9X_TRACE_EXEBUF_CREATE:        return "ExeBufCreate";
    case V9X_TRACE_EXEBUF_DESTROY:       return "ExeBufDestroy";
    case V9X_TRACE_EXEBUF_LOCK:          return "ExeBufLock";
    case V9X_TRACE_EXEBUF_UNLOCK:        return "ExeBufUnlock";
    case V9X_TRACE_D3D_SETRENDERTARGET:  return "D3dSetRenderTarget";
    case V9X_TRACE_D3D_DRAWONEPRIM:      return "D3dDrawOnePrimitive";
    case V9X_TRACE_D3D_DRAWPRIMS:        return "D3dDrawPrimitives";
    case V9X_TRACE_D3D_DRAWONEINDEXED:   return "D3dDrawOneIndexed";
    case V9X_TRACE_D3D_TARGET_LAYOUT:    return "D3dTargetLayout";
    case V9X_TRACE_D3D_TEXTURECREATE:    return "D3dTextureCreate";
    case V9X_TRACE_D3D_TEXTUREDESTROY:   return "D3dTextureDestroy";
    case V9X_TRACE_D3D_TEXTURESWAP:      return "D3dTextureSwap";
    case V9X_TRACE_D3D_TEXTUREGETSURF:   return "D3dTextureGetSurf";
    case V9X_TRACE_D3D_PRIMREJECT:       return "D3dPrimitiveReject";
    case V9X_TRACE_D3D_RENDERLOOP:       return "D3dRenderLoop";
    default:                             return "Unknown";
    }
}

/*
 * The reject reason as a name as well as a number. The number is the durable
 * record; the name is so a result file can be read without the header open
 * beside it, which is how the last two of these runs were actually read.
 */
static const char *v9x_depth_reject_name(DWORD reason)
{
    switch (reason) {
    case V9X_D3D_ZREJECT_NONE:          return "none-offered";
    case V9X_D3D_ZREJECT_ACCEPTED:      return "accepted";
    case V9X_D3D_ZREJECT_NO_LCL:        return "no-lcl";
    case V9X_D3D_ZREJECT_NO_GBL:        return "no-gbl";
    case V9X_D3D_ZREJECT_NOT_ZBUFFER:   return "not-zbuffer";
    case V9X_D3D_ZREJECT_SYSTEM_MEMORY: return "system-memory";
    case V9X_D3D_ZREJECT_DIMENSIONS:    return "dimensions";
    case V9X_D3D_ZREJECT_UNALIGNED:     return "unaligned";
    case V9X_D3D_ZREJECT_OVERLAPS_FB:   return "overlaps-framebuffer";
    case V9X_D3D_ZREJECT_PITCH:         return "pitch";
    case V9X_D3D_ZREJECT_BOUNDS:        return "bounds";
    default:                            return "unknown";
    }
}

static int v9x_append_text(char *buffer, int offset, const char *text)
{
    while (*text != '\0') {
        buffer[offset++] = *text++;
    }
    buffer[offset] = '\0';
    return offset;
}

static void v9x_write_ring(const V9X_DD_TRACE *trace)
{
    char key[16];
    char value[96];
    char number[12];
    DWORD slot;
    DWORD emitted = 0ul;
    DWORD index;

    /* Emit oldest-first: the ring head is the next slot to overwrite. */
    for (index = 0ul; index < V9X_DD_TRACE_RING_COUNT; ++index) {
        const V9X_DD_TRACE_ENTRY *entry;
        int offset = 0;

        slot = trace->head + index;
        if (slot >= V9X_DD_TRACE_RING_COUNT) {
            slot -= V9X_DD_TRACE_RING_COUNT;
        }
        entry = &trace->ring[slot];
        if (entry->id == 0u && entry->detail == 0ul && entry->seq == 0u) {
            continue;
        }
        key[0] = 'R'; key[1] = 'i'; key[2] = 'n'; key[3] = 'g';
        key[4] = (char)('0' + (emitted / 10ul));
        key[5] = (char)('0' + (emitted % 10ul));
        key[6] = '\0';
        v9x_uint_text(number, entry->seq);
        offset = v9x_append_text(value, offset, number);
        offset = v9x_append_text(value, offset, " ");
        offset = v9x_append_text(value, offset, v9x_trace_name(entry->id));
        offset = v9x_append_text(
            value, offset,
            (entry->id & V9X_DD_TRACE_EXIT_FLAG) != 0u ? " exit "
                                                       : " enter ");
        v9x_hex_text(number, entry->detail);
        offset = v9x_append_text(value, offset, number);
        v9x_write_text(key, value);
        ++emitted;
    }
    v9x_write_uint("RingEntries", emitted);
}

static void v9x_write_counters(const V9X_DD_TRACE *trace)
{
    char key[48];
    WORD id;

    for (id = 0u; id < V9X_DD_TRACE_ID_COUNT; ++id) {
        int offset = 0;

        if (trace->counters[id] == 0u) {
            continue;
        }
        offset = v9x_append_text(key, offset, "Count");
        offset = v9x_append_text(key, offset, v9x_trace_name(id));
        v9x_write_uint(key, trace->counters[id]);
    }
}

/*
 * Parse "-inject" / "-inject=N" off the command line.
 *
 * Returns the number of engine waits to force into their timeout path, or 0
 * when the switch is absent. Arming is a separate step from dumping on
 * purpose: nothing in this tool issues a blit, so the count is consumed by
 * whatever real workload runs between the arming call and the next dump.
 */
static DWORD v9x_parse_inject(void)
{
    const char *cmd = GetCommandLineA();
    const char *match = "-inject";
    int index;

    if (cmd == 0) {
        return 0ul;
    }
    for (; *cmd != '\0'; ++cmd) {
        for (index = 0; match[index] != '\0'; ++index) {
            if (cmd[index] != match[index]) {
                break;
            }
        }
        if (match[index] != '\0') {
            continue;
        }
        cmd += index;
        if (*cmd != '=') {
            return 1ul;
        }
        ++cmd;
        {
            DWORD value = 0ul;

            while (*cmd >= '0' && *cmd <= '9') {
                value = value * 10ul + (DWORD)(*cmd - '0');
                ++cmd;
            }
            return value;
        }
    }
    return 0ul;
}

void __stdcall V9xTraceDumpEntry(void)
{
    V9X_DCICMD command;
    V9X_DD_TRACE_SNAPSHOT snapshot;
    HDC screen;
    DWORD escape = V9X_DCICOMMAND;
    DWORD inject;
    int result;
    unsigned index;
    unsigned char *bytes;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    WritePrivateProfileStringA(V9X_SECTION, 0, 0, V9X_RESULT_PATH);
    v9x_write_text("Build", "V9XTRACEDUMP build=" V9X_BUILD_ID);

    screen = GetDC(0);
    if (screen == 0) {
        v9x_write_uint("Ok", 0ul);
        v9x_write_text("Error", "no-screen-dc");
        ExitProcess(1ul);
    }
    result = ExtEscape(screen, V9X_QUERYESCSUPPORT, sizeof(escape),
                       (LPCSTR)&escape, 0, 0);
    v9x_write_uint("DciEscapeSupported", result > 0 ? 1ul : 0ul);

    inject = v9x_parse_inject();
    if (inject != 0ul) {
        command.dwCommand = V9X_DDFAULTINJECT;
        command.dwParam1 = inject;
        command.dwParam2 = 0ul;
        command.dwVersion = V9X_DD_VERSION;
        command.dwReserved = 0ul;
        result = ExtEscape(screen, V9X_DCICOMMAND, sizeof(command),
                           (LPCSTR)&command, 0, 0);
        v9x_write_uint("InjectRequested", inject);
        v9x_write_uint("InjectArmed", result > 0 ? 1ul : 0ul);
    }

    bytes = (unsigned char *)&snapshot;
    for (index = 0u; index < sizeof(snapshot); ++index) {
        bytes[index] = 0u;
    }
    command.dwCommand = V9X_DDGETTRACE;
    command.dwParam1 = 0ul;
    command.dwParam2 = 0ul;
    command.dwVersion = V9X_DD_VERSION;
    command.dwReserved = 0ul;
    result = ExtEscape(screen, V9X_DCICOMMAND, sizeof(command),
                       (LPCSTR)&command, sizeof(snapshot),
                       (LPSTR)&snapshot);
    ReleaseDC(0, screen);
    if (result <= 0) {
        v9x_write_uint("Ok", 0ul);
        v9x_write_text("Error", "escape-rejected");
        ExitProcess(2ul);
    }
    if (snapshot.dwSize != sizeof(V9X_DD_TRACE_SNAPSHOT) ||
        snapshot.abi != V9X_DD_SHARED_ABI) {
        v9x_write_uint("Ok", 0ul);
        v9x_write_text("Error", "abi-mismatch");
        v9x_write_uint("SnapshotSize", snapshot.dwSize);
        v9x_write_uint("SnapshotAbi", snapshot.abi);
        ExitProcess(3ul);
    }

    v9x_write_uint("Ok", 1ul);
    v9x_write_uint("DriverInitDone", snapshot.driver_init_done);
    v9x_write_uint("ModeWidth", snapshot.fb.width);
    v9x_write_uint("ModeHeight", snapshot.fb.height);
    v9x_write_uint("ModeBpp", snapshot.fb.bits_per_pixel);
    v9x_write_uint("ModePitch", snapshot.fb.pitch);
    v9x_write_hex("ScreenSelector", snapshot.fb.screen_selector);
    v9x_write_uint("EnableCount", snapshot.fb.enable_count);
    v9x_write_uint("DisableCount", snapshot.fb.disable_count);
    /* EngineFlags is now runtime state only: VALID plus the STATUS_VALIDATED
     * latch. Chip identity moved to EngineType, and what that chip will do to
     * EngineCaps, so both have to be dumped or retiring the old identity bits
     * would leave the diagnostics unable to say which engine ran. */
    v9x_write_uint("EngineFlags", snapshot.engine.flags);
    v9x_write_uint("EngineType", snapshot.engine.engine_type);
    v9x_write_hex("EngineCaps", snapshot.engine.engine_caps);
    v9x_write_uint("EngineFifoTimeouts", snapshot.engine.fifo_timeouts);
    v9x_write_uint("EngineIdleTimeouts", snapshot.engine.idle_timeouts);
    v9x_write_uint("EngineResets", snapshot.engine.reset_count);
    v9x_write_uint("EngineFaultInjectRemaining", snapshot.engine.fault_inject);
    v9x_write_uint("D3dContextCreates", snapshot.d3d.context_creates);
    v9x_write_uint("D3dContextDestroys", snapshot.d3d.context_destroys);
    v9x_write_uint("D3dContextDestroyAlls",
                   snapshot.d3d.context_destroy_alls);
    v9x_write_uint("D3dContextRejects", snapshot.d3d.context_rejects);
    v9x_write_uint("D3dRenderStateCalls", snapshot.d3d.render_state_calls);
    v9x_write_uint("D3dRenderPrimitiveCalls",
                   snapshot.d3d.render_primitive_calls);
    v9x_write_uint("D3dExecuteCalls", snapshot.d3d.execute_calls);
    v9x_write_uint("D3dTextureCreates", snapshot.d3d.texture_creates);
    v9x_write_uint("D3dTextureDestroys", snapshot.d3d.texture_destroys);
    v9x_write_uint("D3dTextureSwaps", snapshot.d3d.texture_swaps);
    v9x_write_uint("D3dTextureGetSurfs", snapshot.d3d.texture_get_surfs);
    /*
     * Depth-surface plumbing. D3dDepthOffered is the key that separates "the
     * runtime never passed a depth surface" from "the driver refused the one
     * it passed": zero means the question is about the runtime and nothing in
     * the driver's depth validation is implicated.
     */
    v9x_write_uint("D3dDepthOffered", snapshot.d3d.depth_offered);
    v9x_write_uint("D3dDepthAccepted", snapshot.d3d.depth_accepted);
    v9x_write_uint("D3dMipChainChecks", snapshot.d3d.mip_chain_checks);
    v9x_write_uint("D3dMipChainGaps", snapshot.d3d.mip_chain_gaps);
    v9x_write_uint("D3dMipChainLevels", snapshot.d3d.mip_chain_levels);
    v9x_write_uint("D3dMipChainDelta", snapshot.d3d.mip_chain_delta);
    v9x_write_uint("D3dTextureRefusedFormat",
                   snapshot.d3d.texture_refused_format);
    v9x_write_uint("D3dTextureRefusedShape",
                   snapshot.d3d.texture_refused_shape);
    v9x_write_hex("D3dTextureRefusedLast", snapshot.d3d.texture_refused_last);
    /*
     * The Gen3 engine's own draw accounting. The first question intel52 could
     * not answer was what became of the RenderPrimitive calls that submitted
     * nothing, and I9xxRefuseLast is the answer: the reason code of the last
     * refusal, beside the counts.
     */
    v9x_write_uint("I9xxDrawsSubmitted", snapshot.d3d.i9xx_draws_submitted);
    v9x_write_uint("I9xxDrawsRefused", snapshot.d3d.i9xx_draws_refused);
    v9x_write_uint("I9xxRefuseLast", snapshot.d3d.i9xx_refuse_last);
    v9x_write_uint("I9xxTextureDraws", snapshot.d3d.i9xx_texture_draws);
    v9x_write_uint("I9xxDepthDraws", snapshot.d3d.i9xx_depth_draws);
    v9x_write_uint("I9xxDepthSkipped", snapshot.d3d.i9xx_depth_skipped);
    v9x_write_uint("SurfaceIntRejected", snapshot.d3d.surface_int_rejected);
    v9x_write_hex("SurfaceIntLast", snapshot.d3d.surface_int_last);
    v9x_write_uint("SurfaceIntSite", snapshot.d3d.surface_int_site);
    v9x_write_hex("SurfaceIntSites", snapshot.d3d.surface_int_sites);
    /* The scanout watch: per pipe, the display line's range and how often it
     * changed across the samples, and frames elapsed. See the ABI header. */
    v9x_write_uint("ScanSamples", snapshot.d3d.scan_samples);
    v9x_write_uint("ScanALineMin", snapshot.d3d.scan_a_line_min);
    v9x_write_uint("ScanALineMax", snapshot.d3d.scan_a_line_max);
    v9x_write_uint("ScanALineChanges", snapshot.d3d.scan_a_line_changes);
    v9x_write_uint("ScanAFrames", snapshot.d3d.scan_a_frames);
    v9x_write_uint("ScanBLineMin", snapshot.d3d.scan_b_line_min);
    v9x_write_uint("ScanBLineMax", snapshot.d3d.scan_b_line_max);
    v9x_write_uint("ScanBLineChanges", snapshot.d3d.scan_b_line_changes);
    v9x_write_uint("ScanBFrames", snapshot.d3d.scan_b_frames);
    v9x_write_uint("D3dTextureCreateSysmem",
                   snapshot.d3d.texture_create_sysmem);
    v9x_write_hex("D3dTextureCreateLastCaps",
                  snapshot.d3d.texture_create_last_caps);
    v9x_write_uint("I9xxDepthLastFunc", snapshot.d3d.i9xx_depth_last_func);
    v9x_write_uint("FlipHandled", snapshot.d3d.flip_handled);
    v9x_write_uint("FlipStillDrawing", snapshot.d3d.flip_still_drawing);
    v9x_write_uint("FlipWindowClosed", snapshot.d3d.flip_window_closed);
    v9x_write_uint("FlipDeclined", snapshot.d3d.flip_declined);
    v9x_write_uint("FlipForcedIdle", snapshot.d3d.flip_forced_idle);
    v9x_write_uint("ScanoutUnresolved", snapshot.d3d.scanout_unresolved);
    v9x_write_uint("ScanATickLine", snapshot.d3d.scan_a_tick_line);
    v9x_write_uint("ScanATickSeen", snapshot.d3d.scan_a_tick_seen);
    v9x_write_uint("ScanBTickLine", snapshot.d3d.scan_b_tick_line);
    v9x_write_uint("ScanBTickSeen", snapshot.d3d.scan_b_tick_seen);
    v9x_write_uint("FlipRingIssued", snapshot.d3d.flip_ring_issued);
    v9x_write_uint("FlipRingRefused", snapshot.d3d.flip_ring_refused);
    v9x_write_uint("FlipBaseImmediate", snapshot.d3d.flip_base_immediate);
    v9x_write_uint("FlipBaseDeferred", snapshot.d3d.flip_base_deferred);
    v9x_write_uint("FlipTakenAtDone", snapshot.d3d.flip_taken_at_done);
    v9x_write_uint("FlipNotTakenAtDone",
                   snapshot.d3d.flip_not_taken_at_done);
    v9x_write_uint("FlipRingPendingSeen",
                   snapshot.d3d.flip_ring_pending_seen);
    v9x_write_hex("IsrAfterFlipOr", snapshot.d3d.isr_after_flip_or);
    v9x_write_hex("IsrBeforeFlipOr", snapshot.d3d.isr_before_flip_or);
    v9x_write_uint("FlipFramesInSubmit", snapshot.d3d.flip_frames_in_submit);
    v9x_write_uint("DrawsToFront", snapshot.d3d.draws_to_front);
    v9x_write_uint("DrawsToBack", snapshot.d3d.draws_to_back);
    v9x_write_hex("DrawsTargetLast", snapshot.d3d.draws_target_last);
    v9x_write_hex("DrawsDisplayedLast", snapshot.d3d.draws_displayed_last);
    v9x_write_uint("DrawsFlipWaited", snapshot.d3d.draws_flip_waited);
    v9x_write_uint("DrawsFlipWaitTimeouts",
                   snapshot.d3d.draws_flip_wait_timeouts);
    v9x_write_hex("FlipStrideLast", snapshot.d3d.flip_stride_last);
    v9x_write_hex("FlipDspCntrLast", snapshot.d3d.flip_dspcntr_last);
    v9x_write_hex("FlipPipeSrcLast", snapshot.d3d.flip_pipesrc_last);
    v9x_write_hex("DrawsPitchLast", snapshot.d3d.draws_pitch_last);
    v9x_write_hex("DrawsExtentLast", snapshot.d3d.draws_extent_last);
    v9x_write_uint("BreadcrumbSubmits", snapshot.d3d.breadcrumb_submits);
    v9x_write_uint("BreadcrumbLagPollsMax",
                   snapshot.d3d.breadcrumb_lag_polls_max);
    v9x_write_uint("BreadcrumbLagPollsTotal",
                   snapshot.d3d.breadcrumb_lag_polls_total);
    v9x_write_uint("BreadcrumbTimeouts", snapshot.d3d.breadcrumb_timeouts);
    v9x_write_hex("HwsPgaBefore", snapshot.d3d.hws_pga_before);
    v9x_write_hex("HwsPgaWritten", snapshot.d3d.hws_pga_written);
    v9x_write_hex("HwsPgaAfter", snapshot.d3d.hws_pga_after);
    v9x_write_uint("HwsCpuProbe", snapshot.d3d.hws_cpu_probe);
    v9x_write_hex("HwsValueLast", snapshot.d3d.hws_value_last);
    v9x_write_uint("BreadcrumbLate", snapshot.d3d.breadcrumb_late);
    v9x_write_uint("HwsSelfTest", snapshot.d3d.hws_selftest);
    v9x_write_uint("HwsSelfTestPolls", snapshot.d3d.hws_selftest_polls);
    v9x_write_uint("BreadcrumbOutstanding",
                   snapshot.d3d.breadcrumb_outstanding);
    v9x_write_uint("BreadcrumbAbandoned", snapshot.d3d.breadcrumb_abandoned);
    v9x_write_uint("RenderDrainWaits", snapshot.d3d.render_drain_waits);
    v9x_write_uint("RenderDrainStalls", snapshot.d3d.render_drain_stalls);
    v9x_write_hex("ActhdAtHeadLast", snapshot.d3d.acthd_at_head_last);
    v9x_write_hex("ActhdAfterLast", snapshot.d3d.acthd_after_last);
    v9x_write_uint("ActhdMoved", snapshot.d3d.acthd_moved);
    v9x_write_uint("ActhdStill", snapshot.d3d.acthd_still);
    v9x_write_uint("ActhdChangesMax", snapshot.d3d.acthd_changes_max);
    v9x_write_hex("ActhdRawMin", snapshot.d3d.acthd_raw_min);
    v9x_write_hex("ActhdRawMax", snapshot.d3d.acthd_raw_max);
    v9x_write_hex("InstdoneAtHeadLast", snapshot.d3d.instdone_at_head_last);
    v9x_write_hex("InstdoneAfterLast", snapshot.d3d.instdone_after_last);
    v9x_write_hex("TailLast", snapshot.d3d.tail_last);
    v9x_write_hex("ScanSampleOffset", snapshot.d3d.scan_sample_offset);
    v9x_write_uint("ScanSampleFrame", snapshot.d3d.scan_sample_frame);
    v9x_write_uint("ScanLayoutSamples", snapshot.d3d.scan_layout_samples);
    {
        DWORD index;
        char key[16];

        for (index = 0ul; index < 24ul; ++index) {
            if (snapshot.d3d.scan_reg_offset[index] == 0ul) {
                continue;
            }
            key[0] = 'S'; key[1] = 'c'; key[2] = 'a'; key[3] = 'n';
            key[4] = 'R'; key[5] = 'e'; key[6] = 'g';
            key[7] = (char)('0' + index / 10ul);
            key[8] = (char)('0' + index % 10ul);
            key[9] = 'O'; key[10] = 'f'; key[11] = 'f'; key[12] = '\0';
            v9x_write_hex(key, snapshot.d3d.scan_reg_offset[index]);
            key[9] = 'V'; key[10] = 'a'; key[11] = 'l'; key[12] = '\0';
            v9x_write_hex(key, snapshot.d3d.scan_reg_value[index]);
        }
    }
    v9x_write_uint("D3dBlendSkipped", snapshot.d3d.blend_skipped);
    v9x_write_hex("D3dBlendLastPair", snapshot.d3d.blend_last_pair);
    v9x_write_uint("D3dColorKeySets", snapshot.d3d.color_key_sets);
    v9x_write_hex("D3dColorKeyRaw0", snapshot.d3d.color_key_raw[0]);
    v9x_write_hex("D3dColorKeyRaw1", snapshot.d3d.color_key_raw[1]);
    v9x_write_hex("D3dColorKeyRaw2", snapshot.d3d.color_key_raw[2]);
    v9x_write_hex("D3dColorKeyRaw3", snapshot.d3d.color_key_raw[3]);
    v9x_write_hex("D3dColorKeyRaw4", snapshot.d3d.color_key_raw[4]);
    v9x_write_hex("D3dColorKeyRaw5", snapshot.d3d.color_key_raw[5]);
    v9x_write_uint("D3dColorKeyDraws", snapshot.d3d.color_key_draws);
    v9x_write_uint("D3dColorKeyRewrites", snapshot.d3d.color_key_rewrites);
    v9x_write_uint("D3dLclTailCaptures", snapshot.d3d.lcl_tail_captures);
    v9x_write_uint("D3dTextureRefusedOther",
                   snapshot.d3d.texture_refused_other);
    v9x_write_hex("D3dTextureLastOffset", snapshot.d3d.texture_last_offset);
    v9x_write_uint("D3dTextureLastSize", snapshot.d3d.texture_last_size);
    v9x_write_hex("D3dTextureLastCaps", snapshot.d3d.texture_last_caps);
    v9x_write_hex("D3dTextureLastTexels", snapshot.d3d.texture_last_texels);
    v9x_write_uint("D3dTextureGreenDraws", snapshot.d3d.texture_green_draws);
    v9x_write_uint("D3dTextureAlphaDraws", snapshot.d3d.texture_alpha_draws);
    v9x_write_uint("D3dTextureRefusedSysmem",
                   snapshot.d3d.texture_refused_sysmem);
    v9x_write_uint("D3dTextureRefusedNoCap",
                   snapshot.d3d.texture_refused_nocap);
    v9x_write_uint("D3dTextureRefusedBounds",
                   snapshot.d3d.texture_refused_bounds);
    v9x_write_hex("D3dTextureRefusedCaps", snapshot.d3d.texture_refused_caps);
    v9x_write_hex("D3dTextureRefusedVidMem",
                  snapshot.d3d.texture_refused_vidmem);
    v9x_write_uint("D3dDoneSeen", snapshot.d3d.done_seen);
    v9x_write_uint("D3dDoneMissing", snapshot.d3d.done_missing);
    v9x_write_uint("D3dDoneSkipped", snapshot.d3d.done_skipped);
    /*
     * The command-word census. One row per distinct S3D command word the run
     * used, with the texture-size field masked out of the key and carried in
     * SizeMask instead. Read it beside a picture: the bits of a word say what
     * state produced the draws that look wrong.
     */
    v9x_write_uint("CensusSlots", snapshot.census.slots_used);
    v9x_write_uint("CensusOverflow", snapshot.census.overflow);
    {
        DWORD census_index;
        char census_name[24];

        for (census_index = 0ul; census_index < snapshot.census.slots_used &&
                                 census_index < (DWORD)V9X_D3D_CENSUS_SLOTS;
             ++census_index) {
            const V9X_D3D_CENSUS_ENTRY *entry =
                &snapshot.census.entries[census_index];

            v9x_census_name(census_name, census_index, "Cmd");
            v9x_write_hex(census_name, entry->command);
            v9x_census_name(census_name, census_index, "Draws");
            v9x_write_uint(census_name, entry->draws);
            v9x_census_name(census_name, census_index, "SizeMask");
            v9x_write_hex(census_name, entry->size_mask);
            v9x_census_name(census_name, census_index, "TexOffset");
            v9x_write_hex(census_name, entry->tex_offset);
            v9x_census_name(census_name, census_index, "TexCaps");
            v9x_write_hex(census_name, entry->tex_caps);
        }
    }
    {
        DWORD tail_index;
        char tail_name[16];

        for (tail_index = 0ul; tail_index < 16ul; ++tail_index) {
            tail_name[0] = 'D'; tail_name[1] = '3'; tail_name[2] = 'd';
            tail_name[3] = 'L'; tail_name[4] = 'c'; tail_name[5] = 'l';
            tail_name[6] = 'T'; tail_name[7] = 'a'; tail_name[8] = 'i';
            tail_name[9] = 'l';
            tail_name[10] = (char)('0' + (tail_index / 10ul));
            tail_name[11] = (char)('0' + (tail_index % 10ul));
            tail_name[12] = 0;
            v9x_write_hex(tail_name, snapshot.d3d.lcl_tail_raw[tail_index]);
        }
    }
    v9x_write_uint("D3dDepthReject", snapshot.d3d.depth_reject);
    v9x_write_text("D3dDepthRejectName",
                   v9x_depth_reject_name(snapshot.d3d.depth_reject));
    v9x_write_hex("D3dDepthCaps", snapshot.d3d.depth_caps);
    v9x_write_hex("D3dDepthOffset", snapshot.d3d.depth_offset);
    v9x_write_uint("D3dDepthPitch", snapshot.d3d.depth_pitch);
    /* The render target as the engine last programmed it - DEST_BASE and the
     * high half of DEST_SRC_STRIDE - so it can be held against the surface's
     * own address and pitch. */
    v9x_write_hex("D3dTargetOffset", snapshot.d3d.target_offset);
    v9x_write_uint("VirgeDrawsFlipPending",
                   snapshot.d3d.virge_draws_flip_pending);
    v9x_write_uint("DrawsIntoPresented", snapshot.d3d.draws_into_presented);
    v9x_write_uint("FlipDoneFirstPoll", snapshot.d3d.flip_done_first_poll);
    v9x_write_uint("FlipArmedInBlank", snapshot.d3d.flip_armed_in_blank);
    v9x_write_uint("VblankSamples", snapshot.d3d.vblank_samples);
    v9x_write_uint("VblankInBlank", snapshot.d3d.vblank_in_blank);
    v9x_write_hex("Ecoskpd", snapshot.d3d.ecoskpd);
    v9x_write_uint("FlipIssueLineLast", snapshot.d3d.flip_issue_line_last);
    v9x_write_uint("FlipIssueLineMin", snapshot.d3d.flip_issue_line_min);
    v9x_write_uint("FlipIssueLineMax", snapshot.d3d.flip_issue_line_max);
    v9x_write_uint("FlipIssueVactive", snapshot.d3d.flip_issue_vactive);
    v9x_write_uint("FlipIssueDeltaLast", snapshot.d3d.flip_issue_delta_last);
    v9x_write_uint("FlipIssueDeltaMax", snapshot.d3d.flip_issue_delta_max);
    v9x_write_hex("PipestatAOr", snapshot.d3d.pipestat_a_or);
    v9x_write_hex("PipestatBOr", snapshot.d3d.pipestat_b_or);
    v9x_write_hex("PipestatAFirst", snapshot.d3d.pipestat_a_first);
    v9x_write_hex("PipestatBFirst", snapshot.d3d.pipestat_b_first);
    v9x_write_uint("PipestatCleared", snapshot.d3d.pipestat_cleared);
    v9x_write_hex("FwBlc", snapshot.d3d.fw_blc);
    v9x_write_hex("FwBlc2", snapshot.d3d.fw_blc2);
    v9x_write_hex("FwBlcSelf", snapshot.d3d.fw_blc_self);
    /*
     * The per-mode watermark log. Written flat rather than through a name
     * assembler: four entries of four fields is few enough to spell out,
     * and each line reads as one mode's answer.
     */
    v9x_write_uint("WmLogCount", snapshot.d3d.wm_log_count);
    {
        DWORD index;

        for (index = 0ul; index < (DWORD)V9X_D3D_WM_LOG; ++index) {
            char key[24];

            v9x_wm_name(key, index, "Src");
            v9x_write_hex(key, snapshot.d3d.wm_log_pipesrc[index]);
            v9x_wm_name(key, index, "Blc");
            v9x_write_hex(key, snapshot.d3d.wm_log_fw_blc[index]);
            v9x_wm_name(key, index, "Blc2");
            v9x_write_hex(key, snapshot.d3d.wm_log_fw_blc2[index]);
            v9x_wm_name(key, index, "Self");
            v9x_write_hex(key, snapshot.d3d.wm_log_fw_blc_self[index]);
            v9x_wm_name(key, index, "Dsparb");
            v9x_write_hex(key, snapshot.d3d.wm_log_dsparb[index]);
            v9x_wm_name(key, index, "Want");
            v9x_write_hex(key, snapshot.d3d.wm_log_computed[index]);
            v9x_wm_name(key, index, "RateKhz");
            v9x_write_uint(key, snapshot.d3d.wm_log_rate_khz[index]);
        }
    }
    v9x_write_uint("BltFlipPending", snapshot.d3d.blt_flip_pending);
    v9x_write_uint("LockFlipPending", snapshot.d3d.lock_flip_pending);
    v9x_write_uint("VirgeIdleFalseSettle",
                   snapshot.d3d.virge_idle_false_settle);
    v9x_write_uint("VirgeFlipIdleFalse",
                   snapshot.d3d.virge_flip_idle_false);
    /*
     * The present trace, oldest of the kept records first. The ring holds
     * the last V9X_D3D_PRESENT_TRACE; PresentTraceCount is every record
     * written, so a count above that says older ones were dropped.
     */
    {
        DWORD total = snapshot.d3d.present_trace_count;
        DWORD kept = (DWORD)V9X_D3D_PRESENT_TRACE;
        DWORD shown = total < kept ? total : kept;
        DWORD first = total - shown;
        DWORD index;
        char key[24];

        v9x_write_uint("PresentTraceCount", total);
        for (index = 0ul; index < shown; ++index) {
            DWORD slot = (first + index) % kept;

            v9x_present_name(key, index, "Kind");
            v9x_write_uint(key, snapshot.d3d.present_trace_kind[slot]);
            v9x_present_name(key, index, "Context");
            v9x_write_hex(key, snapshot.d3d.present_trace_context[slot]);
            v9x_present_name(key, index, "Offset");
            v9x_write_hex(key, snapshot.d3d.present_trace_offset[slot]);
            v9x_present_name(key, index, "Seq");
            v9x_write_uint(key, snapshot.d3d.present_trace_seq[slot]);
        }
    }
    v9x_write_uint("D3dTargetPitch", snapshot.d3d.target_pitch);
    v9x_write_uint("D3dTargetWidth", snapshot.d3d.target_width);
    v9x_write_uint("D3dTargetHeight", snapshot.d3d.target_height);
    /*
     * Stage markers, so the file says how far the tool got.
     *
     * They exist because this tool faults in KRNL386 on the final flush after
     * a DirectDraw run (docs/issues/2026-08-30-trace-dump-krnl386-flush-gpf.md),
     * which made "wrote nothing" and "wrote everything and died on the last
     * call" indistinguishable. Markers rather than intermediate flushes: the
     * flush is the call that faults, so adding more of them truncates the
     * output instead of preserving it, and ordinary key writes reach the file
     * without one.
     */
    v9x_write_uint("StageScalars", 1ul);
    v9x_write_uint("TraceEvents", snapshot.trace.seq);
    v9x_write_text("LastEnter", v9x_trace_name(
        (WORD)snapshot.trace.last_enter_id));
    v9x_write_hex("LastEnterDetail", snapshot.trace.last_enter_detail);
    v9x_write_text("LastExit", v9x_trace_name(
        (WORD)snapshot.trace.last_exit_id));
    v9x_write_hex("LastExitResult", snapshot.trace.last_exit_result);
    v9x_write_uint("StageLast", 1ul);
    v9x_write_counters(&snapshot.trace);
    v9x_write_uint("StageCounters", 1ul);
    v9x_write_ring(&snapshot.trace);
    v9x_write_uint("StageRing", 1ul);
    /* Kept despite the fault above: it is the documented way to force the
     * cached tail out, the fault costs only the exit code, and every key is
     * already on disk by the time it runs. */
    WritePrivateProfileStringA(0, 0, 0, V9X_RESULT_PATH);
    ExitProcess(0ul);
}
