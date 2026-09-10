/*
 * A compact view of the driver's counters for the DirectDraw probe.
 *
 * The probe wants to know, per cell, whether the driver refused a texture,
 * skipped a blend, reset the engine or missed the 3D-done bit while that cell
 * drew - beside the pixel, not as a run-level total read afterwards. The full
 * trace snapshot (V9X_DDGETTRACE) would tell it, but its type lives in
 * win9x_ddraw_abi.h, which the probe cannot include: the probe carries its own
 * DirectDraw definitions under the same names, fifty of them, because it is
 * built to run against any driver and must not depend on ours.
 *
 * So this header is shared by the 16-bit driver and the probe and nothing
 * else, defines nothing DirectDraw does, and is append-only: dwSize says how
 * much the driver filled, and a probe built against a longer struct reads the
 * fields it has and leaves the rest at zero.
 */
#ifndef VELOCITY9X_PROBE_COUNTS_H
#define VELOCITY9X_PROBE_COUNTS_H

/* DCICOMMAND dwCommand: 'V9PC'. Output is one V9X_PROBE_COUNTS. */
#define V9X_DDGETCOUNTS 0x56395043ul

typedef struct v9x_probe_counts {
    unsigned long dwSize;                 /* bytes the driver filled        */
    unsigned long render_primitive_calls;
    unsigned long texture_refused;        /* format + shape + other          */
    unsigned long blend_skipped;
    unsigned long engine_resets;
    unsigned long engine_idle_timeouts;
    unsigned long engine_fifo_timeouts;
    unsigned long done_missing;
    unsigned long texture_green_draws;
    unsigned long color_key_draws;
    unsigned long texture_alpha_draws;
    /*
     * Appended 2026-09-10, for the render-target switch that is accepted and
     * ignored. A probe reading only surfaces cannot separate the two faults
     * that produce its pixels - a runtime that never calls the driver, and a
     * driver that takes the call and draws on the old target anyway - and
     * these do.
     *
     * `set_render_target_calls` is the trace ring's per-id enter count for
     * V9xD3dSetRenderTarget rather than a counter of its own: two more DWORDs
     * in the diagnostics block took the shared block past the 4096 bytes the
     * 16-bit side allocates. `depth_offered` and `depth_accepted` say whether
     * the driver's set_target ran and what it made of the attached Z surface,
     * and the last two are not counters at all but the render target the
     * engine last programmed - which is the other half of the question, since
     * a driver that took the call and a driver that draws where it is
     * pointed are separate claims.
     */
    unsigned long set_render_target_calls;
    unsigned long depth_offered;
    unsigned long depth_accepted;
    unsigned long context_creates;
    unsigned long context_destroys;
    unsigned long target_offset;
    unsigned long target_pitch;
    /*
     * Appended 2026-09-10, later the same day. The chain rung's draws land
     * untextured after a render-target switch, because the driver keys a
     * texture record by (handle, context) and drops every one of them when
     * the runtime destroys the context - which is how this runtime performs
     * the switch. Whether the fix belongs in ContextDestroy or in the handle
     * turns on whether the runtime re-creates its textures afterwards, and
     * these two say.
     */
    unsigned long texture_creates;
    unsigned long texture_destroys;
} V9X_PROBE_COUNTS;

#endif
