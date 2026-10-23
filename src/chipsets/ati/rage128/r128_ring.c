/*
 * The Rage 128's bus-master ring set-up: see include\velocity9x\r128_ring.h
 * for the registers and the measurements behind them.
 */
#include "velocity9x/r128_ring.h"

#define V9X_R128_RING_PAGE_MASK        0x00000ffful
/* BUS_CNTL: BUS_MASTER_DIS (6), and bit 29, clear in ATI's 5033A300. */
#define V9X_R128_RING_BUS_MASTER_DIS   0x00000040ul
#define V9X_R128_RING_BUS_BIT29        0x20000000ul
/* AGP_CNTL: the aperture size code (5:0) and FIFO sensing (22:20). */
#define V9X_R128_RING_AGP_SIZE_MASK    0x0000003ful
#define V9X_R128_RING_AGP_SIZE_32M     0x00000038ul
#define V9X_R128_RING_AGP_SENSE_MASK   0x00700000ul
/* AGP_COMMAND: AGP_EN. */
#define V9X_R128_RING_AGP_EN           0x00000100ul

v9x_status v9x_r128_ring_plan_make(v9x_u32 bus_address, v9x_u32 ring_bytes,
                                   v9x_r128_ring_plan *plan)
{
    v9x_u32 agp_base;
    v9x_u32 offset;
    v9x_u32 qwords;
    v9x_u32 l2qw = 0ul;

    if (plan == 0 || bus_address == 0ul ||
        (bus_address & V9X_R128_RING_PAGE_MASK) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (ring_bytes < V9X_R128_RING_MIN_BYTES ||
        ring_bytes > V9X_R128_RING_MAX_BYTES ||
        (ring_bytes & (ring_bytes - 1ul)) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* The window the ring sits in, not the ring rounded to AGP_BASE's
     * 4 MiB granularity: the second fetched nothing on A8U4I5. */
    agp_base = bus_address & ~(V9X_R128_RING_WINDOW_BYTES - 1ul);
    offset = bus_address - agp_base;
    if (offset + ring_bytes + V9X_R128_RING_MIRROR_DWORDS * 4ul >
            V9X_R128_RING_WINDOW_BYTES) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    for (qwords = ring_bytes / 8ul; qwords > 1ul; qwords >>= 1) {
        ++l2qw;
    }
    plan->agp_base = agp_base;
    plan->buffer_offset = offset;
    plan->size_l2qw = l2qw;
    plan->dword_mask = ring_bytes / 4ul - 1ul;
    return V9X_STATUS_OK;
}

v9x_u32 v9x_r128_ring_free(v9x_u32 rptr, v9x_u32 wptr, v9x_u32 dword_mask)
{
    return (rptr - wptr - 1ul) & dword_mask;
}

v9x_u32 v9x_r128_ring_bus_cntl(v9x_u32 current)
{
    return current & ~(V9X_R128_RING_BUS_MASTER_DIS |
                       V9X_R128_RING_BUS_BIT29);
}

v9x_u32 v9x_r128_ring_agp_cntl(v9x_u32 current)
{
    return (current & ~(V9X_R128_RING_AGP_SIZE_MASK |
                        V9X_R128_RING_AGP_SENSE_MASK)) |
           V9X_R128_RING_AGP_SIZE_32M;
}

void v9x_r128_vtx_reset(v9x_r128_vtx_area *area)
{
    v9x_u32 segment;

    area->segment = 0ul;
    area->used = 0ul;
    area->last_age = 0ul;
    for (segment = 0ul; segment < V9X_R128_VTX_SEGMENTS; ++segment) {
        area->age[segment] = 0ul;
    }
}

void v9x_r128_vtx_all_done(v9x_r128_vtx_area *area)
{
    v9x_u32 segment;

    for (segment = 0ul; segment < V9X_R128_VTX_SEGMENTS; ++segment) {
        area->age[segment] = 0ul;
    }
}

int v9x_r128_vtx_reserve(v9x_r128_vtx_area *area, v9x_u32 bytes,
                         v9x_r128_vtx_grant *grant)
{
    grant->offset = 0ul;
    grant->close_age = 0ul;
    grant->wait_age = 0ul;
    if (bytes == 0ul || bytes > V9X_R128_VTX_SEGMENT_BYTES) {
        return 0;
    }
    if (area->used + bytes > V9X_R128_VTX_SEGMENT_BYTES) {
        grant->close_age = ++area->last_age;
        area->age[area->segment] = grant->close_age;
        area->segment = (area->segment + 1ul) % V9X_R128_VTX_SEGMENTS;
        area->used = 0ul;
        grant->wait_age = area->age[area->segment];
        area->age[area->segment] = 0ul;
    }
    grant->offset = area->segment * V9X_R128_VTX_SEGMENT_BYTES + area->used;
    area->used += bytes;
    return 1;
}

int v9x_r128_vtx_age_reached(v9x_u32 read_back, v9x_u32 wanted)
{
    return read_back >= wanted;
}

int v9x_r128_ring_agp_enabled(v9x_u32 agp_command)
{
    if (agp_command == 0xfffffffful) {
        return 0;
    }
    return (agp_command & V9X_R128_RING_AGP_EN) != 0ul;
}
