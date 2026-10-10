/*
 * Installed video memory, measured: the decision half. See vram_probe.h.
 * Pure arithmetic, no I/O.
 */
#include "velocity9x/vram_probe.h"

v9x_u32 v9x_vram_probe_points(v9x_u32 mapped_bytes)
{
    v9x_u32 points = mapped_bytes / V9X_VRAM_PROBE_STEP;

    return points > V9X_VRAM_PROBE_MAX_POINTS ? V9X_VRAM_PROBE_MAX_POINTS
                                              : points;
}

v9x_u32 v9x_vram_probe_offset(v9x_u32 index)
{
    return index * V9X_VRAM_PROBE_STEP + V9X_VRAM_PROBE_INNER;
}

v9x_u32 v9x_vram_probe_signature(v9x_u32 index)
{
    return V9X_VRAM_PROBE_SIGNATURE | index;
}

v9x_u32 v9x_vram_probe_size(const v9x_u32 *readback, v9x_u32 count)
{
    v9x_u32 held = 0ul;

    if (readback == 0) {
        return 0ul;
    }
    while (held < count && held < V9X_VRAM_PROBE_MAX_POINTS &&
           readback[held] == v9x_vram_probe_signature(held)) {
        ++held;
    }
    return held * V9X_VRAM_PROBE_STEP;
}

v9x_u32 v9x_vram_probe_accept(v9x_u32 measured, v9x_u32 reported,
                              v9x_u32 mapped_bytes)
{
    if (measured == 0ul || measured > mapped_bytes) {
        return reported;
    }
    if (reported != 0ul && measured < reported) {
        return reported;
    }
    return measured;
}
