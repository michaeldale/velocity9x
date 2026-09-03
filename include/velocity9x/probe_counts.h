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
} V9X_PROBE_COUNTS;

#endif
