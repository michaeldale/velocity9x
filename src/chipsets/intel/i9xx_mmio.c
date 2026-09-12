#include "velocity9x/intel_gma.h"

static v9x_u16 v9x_i9xx_snapshot_equal(
    const struct v9x_i9xx_mmio_snapshot *a,
    const struct v9x_i9xx_mmio_snapshot *b)
{
    v9x_u16 pipe;

    if (a->pgtbl_ctl != b->pgtbl_ctl ||
        a->ring_tail != b->ring_tail || a->ring_head != b->ring_head ||
        a->ring_start != b->ring_start || a->ring_ctl != b->ring_ctl ||
        a->hws_pga != b->hws_pga) {
        return V9X_FALSE;
    }
    for (pipe = 0u; pipe < V9X_I9XX_PIPE_COUNT; ++pipe) {
        const struct v9x_i9xx_pipe_snapshot *pa = &a->pipe[pipe];
        const struct v9x_i9xx_pipe_snapshot *pb = &b->pipe[pipe];
        if (pa->pipe_conf != pb->pipe_conf || pa->htotal != pb->htotal ||
            pa->vtotal != pb->vtotal || pa->pipe_src != pb->pipe_src ||
            pa->plane_control != pb->plane_control ||
            pa->plane_address != pb->plane_address ||
            pa->plane_stride != pb->plane_stride) {
            return V9X_FALSE;
        }
    }
    return V9X_TRUE;
}

static v9x_u16 v9x_i9xx_snapshot_uniform(
    const struct v9x_i9xx_mmio_snapshot *snapshot,
    v9x_u32 value)
{
    v9x_u16 pipe;

    if (snapshot->pgtbl_ctl != value || snapshot->ring_tail != value ||
        snapshot->ring_head != value || snapshot->ring_start != value ||
        snapshot->ring_ctl != value || snapshot->hws_pga != value) {
        return V9X_FALSE;
    }
    for (pipe = 0u; pipe < V9X_I9XX_PIPE_COUNT; ++pipe) {
        const struct v9x_i9xx_pipe_snapshot *p = &snapshot->pipe[pipe];
        if (p->pipe_conf != value || p->htotal != value ||
            p->vtotal != value || p->pipe_src != value ||
            p->plane_control != value || p->plane_address != value ||
            p->plane_stride != value) {
            return V9X_FALSE;
        }
    }
    return V9X_TRUE;
}

static v9x_status v9x_i9xx_decode_total(v9x_u32 value,
                                         v9x_u16 *active,
                                         v9x_u16 *total)
{
    v9x_u32 active32 = (value & 0xfffful) + 1ul;
    v9x_u32 total32 = (value >> 16) + 1ul;

    if (active32 > 0xfffful || total32 > 0xfffful || active32 > total32) {
        return V9X_STATUS_INVALID_STATE;
    }
    *active = (v9x_u16)active32;
    *total = (v9x_u16)total32;
    return V9X_STATUS_OK;
}

static v9x_status v9x_i9xx_decode_source(v9x_u32 value,
                                          v9x_u16 *width,
                                          v9x_u16 *height)
{
    v9x_u32 width32 = (value >> 16) + 1ul;
    v9x_u32 height32 = (value & 0xfffful) + 1ul;

    if (width32 > 0xfffful || height32 > 0xfffful) {
        return V9X_STATUS_INVALID_STATE;
    }
    *width = (v9x_u16)width32;
    *height = (v9x_u16)height32;
    return V9X_STATUS_OK;
}

static v9x_u16 v9x_i9xx_plane_bpp(v9x_u32 control)
{
    switch ((control & V9X_I9XX_DSPCNTR_FORMAT_MASK) >> 26) {
    case 2ul: return 8u;
    case 5ul: return 16u;
    case 6ul: return 32u;
    default: return 0u;
    }
}

v9x_status v9x_i9xx_analyze_fingerprint(
    const struct v9x_i9xx_mmio_snapshot *first,
    const struct v9x_i9xx_mmio_snapshot *second,
    const struct v9x_i9xx_mode_expectation *expected,
    struct v9x_i9xx_fingerprint *result)
{
    v9x_u16 pipe;
    v9x_u16 plane;
    v9x_u16 candidates = 0u;

    if (first == 0 || second == 0 || expected == 0 || result == 0 ||
        expected->width == 0u || expected->height == 0u ||
        expected->pitch_bytes == 0u || expected->gmadr_aperture_bytes == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    result->flags = 0u;
    result->live_pipe = V9X_I9XX_PIPE_NONE;
    result->live_plane = V9X_I9XX_PIPE_NONE;
    result->timing_width = 0u;
    result->timing_height = 0u;
    result->total_width = 0u;
    result->total_height = 0u;
    result->source_width = 0u;
    result->source_height = 0u;
    result->plane_bits_per_pixel = 0u;
    result->plane_stride = 0u;
    result->plane_address = 0ul;

    if (v9x_i9xx_snapshot_equal(first, second) != V9X_FALSE) {
        result->flags |= V9X_I9XX_FP_STABLE;
    }
    if (v9x_i9xx_snapshot_uniform(first, 0ul) == V9X_FALSE &&
        v9x_i9xx_snapshot_uniform(first, 0xfffffffful) == V9X_FALSE) {
        result->flags |= V9X_I9XX_FP_NONTRIVIAL;
    }
    if ((first->ring_ctl & V9X_I9XX_RING_CTL_VALID) == 0ul &&
        (first->ring_head & V9X_I9XX_RING_POINTER_MASK) ==
        (first->ring_tail & V9X_I9XX_RING_POINTER_MASK)) {
        result->flags |= V9X_I9XX_FP_RING_QUIESCENT;
    }

    /* Gen3 has a free plane-to-pipe mapping: DSPxCNTR bits 25:24 select the
     * pipe a plane feeds, and mobile VBIOS commonly scans the LVDS on pipe B
     * through plane A. So the live pipe is found from PIPECONF alone, and the
     * live plane is whichever enabled plane selects it, in any pairing. Either
     * side being other than exactly one is ambiguous and decodes nothing. */
    for (pipe = 0u; pipe < V9X_I9XX_PIPE_COUNT; ++pipe) {
        if ((first->pipe[pipe].pipe_conf & V9X_I9XX_PIPECONF_ENABLE) != 0ul) {
            result->live_pipe = pipe;
            ++candidates;
        }
    }
    if (candidates != 1u) {
        result->live_pipe = V9X_I9XX_PIPE_NONE;
        return V9X_STATUS_OK;
    }

    candidates = 0u;
    for (plane = 0u; plane < V9X_I9XX_PIPE_COUNT; ++plane) {
        v9x_u32 control = first->pipe[plane].plane_control;
        v9x_u16 selected = (v9x_u16)
            ((control & V9X_I9XX_DSPCNTR_PIPE_MASK) >> 24);
        if ((control & V9X_I9XX_DSPCNTR_ENABLE) != 0ul &&
            selected == result->live_pipe) {
            result->live_plane = plane;
            ++candidates;
        }
    }
    if (candidates != 1u) {
        result->live_pipe = V9X_I9XX_PIPE_NONE;
        result->live_plane = V9X_I9XX_PIPE_NONE;
        return V9X_STATUS_OK;
    }

    result->flags |= V9X_I9XX_FP_LIVE_PIPE;
    {
        const struct v9x_i9xx_pipe_snapshot *p =
            &first->pipe[result->live_pipe];
        const struct v9x_i9xx_pipe_snapshot *pl =
            &first->pipe[result->live_plane];
        v9x_status hstatus = v9x_i9xx_decode_total(
            p->htotal, &result->timing_width, &result->total_width);
        v9x_status vstatus = v9x_i9xx_decode_total(
            p->vtotal, &result->timing_height, &result->total_height);
        v9x_status sstatus = v9x_i9xx_decode_source(
            p->pipe_src, &result->source_width, &result->source_height);

        result->plane_bits_per_pixel = v9x_i9xx_plane_bpp(pl->plane_control);
        result->plane_stride = (v9x_u16)(pl->plane_stride & 0xfffful);
        result->plane_address = pl->plane_address;

        if (hstatus == V9X_STATUS_OK && vstatus == V9X_STATUS_OK) {
            result->flags |= V9X_I9XX_FP_TIMING_VALID;
        }
        if (sstatus == V9X_STATUS_OK &&
            result->source_width == expected->width &&
            result->source_height == expected->height) {
            result->flags |= V9X_I9XX_FP_SOURCE_MATCH;
        }
        if (result->plane_bits_per_pixel == expected->bits_per_pixel &&
            result->plane_stride == expected->pitch_bytes &&
            result->plane_address < expected->gmadr_aperture_bytes) {
            result->flags |= V9X_I9XX_FP_PLANE_MATCH;
        }
    }
    return V9X_STATUS_OK;
}
