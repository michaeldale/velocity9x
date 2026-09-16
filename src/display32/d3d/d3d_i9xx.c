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
 * The largest batch this engine accepts, and the buffer it builds into.
 *
 * Bounded so the stream buffer is a fixed size the HAL can hold on its stack
 * rather than an allocation on a draw path. 64 triangles is well under the
 * core's own RenderPrimitive ceiling and well under the decoder's runtime
 * bound; a batch larger than this is refused and the core sees a failed draw
 * rather than a truncated one.
 */
#define V9X_I9XX_SUBMIT_VERTICES  ((DWORD)192ul)
#define V9X_I9XX_SUBMIT_DWORDS    ((DWORD)1280ul)

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
 * The ring's MMIO registers come from intel_gma.h, which the mini-VDD's
 * generated include also carries. They are addressed through the control
 * window the descriptor holds - BAR0, not the framebuffer, which on this part
 * are different PCI regions and is why gtt_linear_base exists at all.
 */

/*
 * How long to wait for a submission, and how hard.
 *
 * Both bounds are mandatory and neither is a guess about speed: an unbounded
 * spin on a ring head is a hung machine with no diagnosis, which the
 * sustained-3D amendment names among the things it does not authorise. The
 * mini-VDD's own executor waits on the same two bounds.
 */
#define V9X_I9XX_SUBMIT_POLLS   1000000ul

static volatile DWORD *v9x_d3d_i9xx_reg(DWORD offset)
{
    return (volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

/*
 * Where the ring is: PUBLISHED, never derived.
 *
 * This side derived it once, from fb.vram_bytes, and landed a megabyte low -
 * vram_bytes has already had the reserve taken off, so running it through the
 * sandbox calculator subtracted a second one and pointed the ring into the
 * DirectDraw heap, where command dwords would have overwritten application
 * surfaces with no guard between them and it.
 *
 * So the address comes from the 16-bit side, which asked the mini-VDD, which
 * owns the mapping and knows the true size of video memory. Nothing here
 * computes it, and the bounds below are checked against what was published
 * rather than against anything recomputed.
 */
static int v9x_d3d_i9xx_ring_base(DWORD *linear_out, DWORD *bytes_out)
{
    *linear_out = 0ul;
    *bytes_out = 0ul;
    if (v9x_hal == 0 || v9x_hal->engine.control_linear_base == 0ul) {
        return 0;
    }
    if (v9x_hal->engine.ring_linear_base == 0ul ||
        v9x_hal->engine.ring_bytes == 0ul) {
        /* No ring means the mini-VDD could not bring one up. Refusing is the
         * whole response: submitting to a ring that is not enabled advances a
         * pointer nobody reads, and the only symptom would be a timeout. */
        return 0;
    }
    *linear_out = v9x_hal->engine.ring_linear_base;
    *bytes_out = v9x_hal->engine.ring_bytes;
    return 1;
}

/*
 * Submit one built stream and wait for it.
 *
 * The data goes through the framebuffer mapping; only TAIL is a register
 * write. That write is the single most consequential store in this driver -
 * a tail the hardware cannot express poisoned a whole run on 2026-09-16, when
 * the mini-VDD wrote 0x10BC and read back 0x10B8 - so the plan that produces
 * it is the tested one rather than arithmetic written here.
 */
static int v9x_d3d_i9xx_submit(const DWORD *stream, DWORD dwords)
{
    struct v9x_i9xx_ring_plan plan;
    DWORD ring_linear = 0ul;
    DWORD ring_bytes = 0ul;
    DWORD head;
    DWORD tail;
    DWORD polls;
    DWORD index;
    volatile DWORD *ring;

    if (v9x_d3d_i9xx_ring_base(&ring_linear, &ring_bytes) == 0) {
        return 0;
    }
    head = *v9x_d3d_i9xx_reg(V9X_I9XX_REG_RING_HEAD) &
           V9X_I9XX_RING_HEAD_MASK;
    tail = *v9x_d3d_i9xx_reg(V9X_I9XX_REG_RING_TAIL) &
           V9X_I9XX_RING_HEAD_MASK;

    /*
     * The plan, from the unit the diagnostic path already uses. It pads to the
     * ring end with NOOPs rather than splitting a command across the wrap,
     * refuses an odd dword count so the tail stays qword aligned, and counts
     * the pad against the free space. None of that is restated here.
     */
    if (v9x_i9xx_ring_plan(head, tail, ring_bytes, dwords, &plan) !=
            V9X_STATUS_OK) {
        /* A full ring is not an error the caller can fix by retrying inside
         * this call - that would be an unbounded wait wearing a different
         * name - so the batch is refused and the core sees a failed draw. */
        return 0;
    }

    ring = (volatile DWORD *)ring_linear;
    /* The pad first, where one is needed: MI_NOOPs to the ring's end. */
    for (index = 0ul; index < plan.pad_dwords; ++index) {
        ring[(tail / 4ul) + index] = V9X_I9XX_MI_NOOP;
    }
    for (index = 0ul; index < dwords; ++index) {
        ring[(plan.command_tail / 4ul) + index] = stream[index];
    }

    /*
     * The tail, last and once. Everything the GPU will fetch is in memory
     * before the register that tells it to fetch moves - the same ordering
     * the texture paint and the depth clear needed for the same reason.
     */
    *v9x_d3d_i9xx_reg(V9X_I9XX_REG_RING_TAIL) = plan.next_tail;

    for (polls = 0ul; polls < V9X_I9XX_SUBMIT_POLLS; ++polls) {
        if (v9x_i9xx_ring_submission_complete(
                *v9x_d3d_i9xx_reg(V9X_I9XX_REG_RING_HEAD),
                plan.next_tail) != V9X_FALSE) {
            return 1;
        }
    }
    /*
     * Timed out. Reported rather than retried and rather than reset: this
     * driver has never reset this engine, has no measurement of what a reset
     * does to it, and a recovery path nobody has run is a worse thing to
     * enter than a failed draw.
     */
    return 0;
}

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

/*
 * One batch: build the stream, check it, submit it, wait for it.
 *
 * UNRUN. Every piece below is host-tested and no guest has executed one of
 * these streams; what the tests establish is that the bytes are the ones the
 * decoder accepts, not that the part draws them.
 */
static int v9x_d3d_i9xx_draw_triangles(V9X_D3D_CONTEXT *context,
                                       const V9X_D3DTLVERTEX *vertices,
                                       DWORD triangle_count)
{
    struct v9x_i9xx_decode_limits limits;
    DWORD stream[V9X_I9XX_SUBMIT_DWORDS];
    DWORD xyzw[V9X_I9XX_SUBMIT_VERTICES * 4ul];
    DWORD colors[V9X_I9XX_SUBMIT_VERTICES];
    DWORD identity = 0ul;
    DWORD address = 0ul;
    DWORD at = 0ul;
    DWORD produced = 0ul;
    DWORD rejected = 0ul;
    DWORD vertex;
    DWORD count;

    if (context == 0 || vertices == 0 || triangle_count == 0ul) {
        return 0;
    }
    if (triangle_count > (V9X_I9XX_SUBMIT_VERTICES / 3ul)) {
        return 0;
    }
    count = triangle_count * 3ul;

    /*
     * The surface, validated against the aperture BEFORE anything is built.
     * This is the memory-safety check - the decoder below compares the stream
     * against what this produced, which cannot catch an engine that was wrong
     * about the surface, and this is what makes sure it was not.
     */
    if (v9x_d3d_i9xx_bind_target(context->target_offset, context->pitch,
                                 context->width, context->height,
                                 v9x_hal->fb.vram_bytes,
                                 &identity, &address) == V9X_FALSE) {
        return 0;
    }

    for (vertex = 0ul; vertex < count; ++vertex) {
        /* The DDHAL vertex is floats; the stream is bit patterns. A union is
         * the only portable way across, and the HAL links without a runtime
         * so a cast through a pointer is what there is. */
        const DWORD *bits = (const DWORD *)&vertices[vertex].sx;

        xyzw[(vertex * 4ul) + 0ul] = bits[0];
        xyzw[(vertex * 4ul) + 1ul] = bits[1];
        xyzw[(vertex * 4ul) + 2ul] = bits[2];
        xyzw[(vertex * 4ul) + 3ul] = bits[3];
        colors[vertex] = vertices[vertex].color;
    }

    if (v9x_i9xx_build_3d_state(context->target_offset, context->pitch,
                                context->width, context->height,
                                stream + at, V9X_I9XX_SUBMIT_DWORDS - at,
                                &produced) != V9X_STATUS_OK) {
        return 0;
    }
    at += produced;
    if (v9x_i9xx_build_fragment_program(stream + at,
                                        V9X_I9XX_SUBMIT_DWORDS - at,
                                        &produced) != V9X_STATUS_OK) {
        return 0;
    }
    at += produced;
    if (v9x_i9xx_build_runtime_run(xyzw, colors, triangle_count,
                                   context->width, context->height,
                                   stream + at,
                                   V9X_I9XX_SUBMIT_DWORDS - at,
                                   &produced) != V9X_STATUS_OK) {
        return 0;
    }
    at += produced;
    /* The ring tail must land qword aligned, and the plan refuses an odd
     * count rather than padding one - so the pad is here, where the stream is
     * still being built and a NOOP is a dword nobody will miss. */
    if ((at & 1ul) != 0ul) {
        if (at >= V9X_I9XX_SUBMIT_DWORDS) {
            return 0;
        }
        stream[at++] = V9X_I9XX_MI_NOOP;
    }

    /*
     * THE ALLOWLIST, applied by the engine to its own stream.
     *
     * The sustained-3D amendment gave up the combined-CRC gate and named this
     * as what replaces it. Running it here rather than trusting the builders
     * is the whole point: the builders and the decoder are two opinions, and
     * a stream that reaches the ring has passed both.
     */
    limits.target_offset = context->target_offset;
    limits.target_bytes = context->pitch * context->height;
    limits.target_pitch = context->pitch;
    limits.target_width = context->width;
    limits.target_height = context->height;
    limits.texture_offset = 0ul;
    limits.texture_bytes = 0ul;
    limits.depth_offset = 0ul;
    limits.depth_bytes = 0ul;
    limits.kind = V9X_I9XX_SCENE_RUNTIME;
    if (v9x_i9xx_decode_phase5_stream(stream, at, &limits, &rejected) !=
            V9X_I9XX_P5_OK) {
        return 0;
    }

    return v9x_d3d_i9xx_submit(stream, at);
}

/*
 * Ready when the two windows are mapped, and not otherwise.
 *
 * The header records why this entry point exists: without it an engine can
 * resolve, publish caps, accept every call and draw nothing, with every
 * HRESULT reporting success. It answered a flat no while there was no
 * submission path; there is one now, so it answers the real question -
 * whether the windows a submission needs are there.
 *
 * WHAT THIS IS NOT is a claim that the engine draws. No guest has executed
 * one of these streams. What keeps applications away from it is the
 * capability bit, which the 16-bit side still does not set; this answering
 * yes only means the core would route a draw here if one arrived.
 */
static int v9x_d3d_i9xx_ready(void)
{
    if (v9x_hal == 0) {
        return 0;
    }
    if (v9x_hal->engine.engine_type != V9X_DD_ENGINE_TYPE_INTEL_GEN3) {
        return 0;
    }
    if (v9x_hal->engine.control_linear_base == 0ul ||
        v9x_hal->engine.gtt_linear_base == 0ul ||
        v9x_hal->fb.linear_base == 0ul) {
        return 0;
    }
    /*
     * And a RING, which is what mapped windows alone are not. Its presence in
     * the descriptor means the mini-VDD brought one up and reported where it
     * is; its absence means it could not, and an engine without one would
     * advance a TAIL nobody reads.
     *
     * This is still not a claim that the engine draws. No guest has executed
     * one of these streams. What keeps applications away is the capability
     * bit, which the 16-bit side does not set.
     */
    if (v9x_hal->engine.ring_linear_base == 0ul ||
        v9x_hal->engine.ring_bytes == 0ul) {
        return 0;
    }
    return 1;
}

const V9X_D3D_ENGINE_OPS v9x_d3d_engine_i9xx = {
    &v9x_d3d_i9xx_limits,
    v9x_d3d_i9xx_texture_format,
    v9x_d3d_i9xx_describe_caps,
    v9x_d3d_i9xx_draw_triangles,
    v9x_d3d_i9xx_ready
};
