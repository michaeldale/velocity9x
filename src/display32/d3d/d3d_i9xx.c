/*
 * The Intel Gen3 engine seam, wired and NOT READY.
 *
 * This file exists so the plumbing around a Gen3 engine is present and tested
 * while the engine itself is not. `ready` returns zero, `describe_caps`
 * publishes nothing and `draw_triangles` refuses every batch, so nothing can
 * reach hardware through it. The intel-gma family manifest still declares
 * EngineType NONE, so nothing selects it either.
 *
 * Two things stand between here and an engine that draws, and only one of them
 * is code:
 *
 *  - There is NO 32-bit ring submission path. Every Gen3 draw this project has
 *    performed went through the mini-VDD's armed one-shot verb, staged and
 *    CRC-gated, with the driver blind to the ring. A HAL that draws on demand
 *    needs its own submission, completion and recovery, and none of that
 *    exists.
 *
 *  - Sustained 3D work is NOT AUTHORISED. The errata gate authorises five
 *    independent diagnostic draws per armed boot and names "sustained or
 *    repeated 3D work in the sense of a workload" among the things it does
 *    not cover (docs\decisions\2026-09-15-intel-phase5-errata-gate.md). An
 *    application issuing draws is exactly that. Publishing caps before that
 *    decision is taken would put the machine outside its own authorisation on
 *    the first frame.
 *
 * So what is here is the seam: the ABI value, the selector arms, the limits
 * this part is MEASURED to support, and the render-target binding every
 * runtime draw will need. The binding is real arithmetic with a host test,
 * because it is the first thing a draw does and the first thing that can be
 * wrong about an arbitrary surface rather than the diagnostic sandbox.
 *
 * docs\plans\hardware-d3d-on-intel-gma950.md phase 7.
 */
#include "d3d_internal.h"
#include "velocity9x/intel_gma.h"
#include "velocity9x/intel_gen3_3d.h"
/* The render-target binding is a LEAF unit so the host suite can reach it;
 * this file cannot be, because it includes the DDHAL headers. */
#include "d3d_i9xx_target.h"

/*
 * What this part is measured to do, and nothing wider.
 *
 * Every number here has a capture behind it. The target depth is RGB565
 * because that is the only colour buffer format any Gen3 stream this project
 * has run declared, and the only one a probe has read back. The texture bounds
 * are the one texture that has been painted and sampled - 32 by 32, square,
 * power of two - rather than the part's architectural maximum, which nothing
 * has exercised.
 *
 * Deliberately narrower than the hardware. A limit that claims more than has
 * been measured is a promise the first application collects on.
 */
static const V9X_D3D_ENGINE_LIMITS v9x_d3d_i9xx_limits = {
    16ul,                       /* target_bits_per_pixel  */
    V9X_I9XX_BUF_3D_PITCH_MASK, /* target_pitch_max       */
    4ul,                        /* target_pitch_align     */
    2048ul,                     /* target_dimension_max   */
    32ul,                       /* texture_size_min       */
    32ul,                       /* texture_size_max       */
    4096.0f,                    /* coordinate_limit       */
    16ul                        /* depth_bits_per_pixel   */
};

/*
 * No texture format is accepted.
 *
 * The engine cannot draw, so accepting a format would tell the core this
 * engine can sample something it has no way to reach.
 */
static int v9x_d3d_i9xx_texture_format(const V9X_DD_SURFACE_LCL *surface,
                                       DWORD *format_out)
{
    (void)surface;
    if (format_out != 0) {
        *format_out = 0ul;
    }
    return 0;
}

/*
 * Publishes NOTHING.
 *
 * Not a stub in the sense of unfinished work: publishing caps is the step the
 * errata gate does not cover, and this function is where that would happen. It
 * stays empty until a risk decision says otherwise, and the emptiness is the
 * decision being respected rather than a gap.
 */
static void v9x_d3d_i9xx_describe_caps(V9X_DD_SHARED *shared)
{
    (void)shared;
}

/* Refuses every batch. There is no 32-bit submission path to hand it to. */
static int v9x_d3d_i9xx_draw_triangles(V9X_D3D_CONTEXT *context,
                                       const V9X_D3DTLVERTEX *vertices,
                                       DWORD triangle_count)
{
    (void)context;
    (void)vertices;
    (void)triangle_count;
    return 0;
}

/*
 * NOT READY, and this is the load-bearing one.
 *
 * The header records why this entry point exists: without it an engine can
 * resolve, publish caps, accept every call and draw nothing, with every
 * HRESULT reporting success. That is precisely the state this engine would be
 * in if it answered yes, so it answers no.
 */
static int v9x_d3d_i9xx_ready(void)
{
    return 0;
}

const V9X_D3D_ENGINE_OPS v9x_d3d_engine_i9xx = {
    &v9x_d3d_i9xx_limits,
    v9x_d3d_i9xx_texture_format,
    v9x_d3d_i9xx_describe_caps,
    v9x_d3d_i9xx_draw_triangles,
    v9x_d3d_i9xx_ready
};
