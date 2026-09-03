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
#define V9X_RESULT_PATH  V9X_DIAG_SNAP_INI

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
    v9x_write_uint("D3dDepthReject", snapshot.d3d.depth_reject);
    v9x_write_text("D3dDepthRejectName",
                   v9x_depth_reject_name(snapshot.d3d.depth_reject));
    v9x_write_hex("D3dDepthCaps", snapshot.d3d.depth_caps);
    v9x_write_hex("D3dDepthOffset", snapshot.d3d.depth_offset);
    v9x_write_uint("D3dDepthPitch", snapshot.d3d.depth_pitch);
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
