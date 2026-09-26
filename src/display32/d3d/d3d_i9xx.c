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
#include "velocity9x/i9xx_depth.h"
#include "velocity9x/intel_gma.h"
#include "velocity9x/intel_gen3_3d.h"
/* The render-target binding is a LEAF unit so the host suite can reach it;
 * this file cannot be, because it includes the DDHAL headers. */
#include "d3d_i9xx_target.h"

/*
 * The largest batch this engine accepts, and the buffer it builds into.
 *
 * Bounded so the stream buffer is a fixed size the HAL can hold statically
 * rather than an allocation on a draw path. 64 triangles is well under the
 * core's own RenderPrimitive ceiling and well under the decoder's runtime
 * bound; a batch larger than this is refused and the core sees a failed draw
 * rather than a truncated one.
 */
#define V9X_I9XX_SUBMIT_VERTICES  ((DWORD)192ul)

/*
 * The core's batch size against this engine's limits, checked by the
 * compiler rather than by a capture.
 *
 * v9x_d3d_i9xx_draw_triangles refuses anything past either bound, and since
 * 2026-09-20 a refused batch is reported to the application as DD_OK and
 * counted, not as an error - so a mismatch here would not crash, or warn, or
 * appear in a log. It would take geometry out of the picture and leave
 * batches_engine_refused as the only trace. That is precisely the class of
 * fault this project keeps finding by accident weeks later, so it is made
 * impossible to build instead.
 */
typedef char v9x_assert_batch_fits_runtime[
    (V9X_D3D_INDEXED_BATCH <= V9X_I9XX_RUNTIME_MAX_TRIANGLES) ? 1 : -1];
typedef char v9x_assert_batch_fits_vertices[
    (V9X_D3D_INDEXED_BATCH * 3u <= 192u) ? 1 : -1];
/* Sized for the largest batch: a textured, depth-bound state block, the
 * modulate program, and 64 triangles of seven-dword vertices. */
#define V9X_I9XX_SUBMIT_DWORDS    ((DWORD)1536ul)

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
/*
 * The smallest map placement, the bind and accepts take: one texel. The
 * published floor (texture_size_min below, the Direct3D caps) stays 8;
 * its reason is the pitch of a map laid out at its own width, and every
 * map this engine places is laid out by v9x_d3d_i9xx_layout_miptree,
 * whose pitch is padded to 64 bytes whatever the width. Measured with
 * V9XGLP's small-texture scene (2026-09-26).
 */
#define V9X_D3D_I9XX_SAMPLER_SIZE_MIN 1ul

static const V9X_D3D_ENGINE_LIMITS v9x_d3d_i9xx_limits = {
    16ul,                       /* target_bits_per_pixel  */
    V9X_I9XX_BUF_3D_PITCH_MASK, /* target_pitch_max       */
    4ul,                        /* target_pitch_align     */
    2048ul,                     /* target_dimension_max   */
    /*
     * 8 to 256, square and a power of two.
     *
     * GENERALISED from one measured size, and that is stated rather than
     * implied: intel45 sampled a 32x32 map and nothing has sampled another.
     * MAP_STATE's fields hold any dimension to 2048 and the builder is
     * parameterised, so the packet is not the limit - what is unmeasured is
     * whether the sampler behaves the same at every size.
     *
     * Generalising is defensible here because the memory-safety argument does
     * not rest on it: v9x_d3d_i9xx_bind_map proves the footprint lies inside
     * the aperture whatever the size, so a size this part dislikes is a wrong
     * picture, not a write outside the surface. A claim that cost safety
     * rather than accuracy would not be worth making.
     *
     * The ceiling is 256 rather than 2048 because a 2048-square map is 8 MiB
     * and this part's stolen memory is 8. The floor is 8 because below it the
     * pitch of a square map stops being a multiple of four.
     */
    8ul,                        /* texture_size_min       */
    256ul,                      /* texture_size_max       */
    4096.0f,                    /* coordinate_limit       */
    16ul,                       /* depth_bits_per_pixel   */
    /*
     * PAGE aligned, which is what v9x_i9xx_build_map_state requires and what
     * DirectDraw is now told, so the texture surfaces it allocates are ones
     * this engine can bind. It is this driver's choice rather than a databook
     * requirement - neither reference emitter states one - and the choice is
     * kept rather than relaxed because relaxing it is an unmeasured claim
     * about where the hardware will fetch texels from, and being wrong about
     * that is a read outside the allocation.
     *
     * The cost is a page per texture surface. On a part with eight megabytes
     * of stolen memory that is affordable; if it ever stops being, the answer
     * is to measure the real alignment rather than to guess a smaller one.
     */
    V9X_I9XX_SANDBOX_PAGE_BYTES, /* texture_align         */
    /* Gen3 clips against its own drawing rectangle inside the 4096 guard
     * band above, and 3DMark99 ran on the netbook with the DX5 paths
     * unclipped; that measured behaviour is kept. */
    0ul,                        /* clip_in_core           */
    /* BUF_INFO carries the depth pitch, so a padded Z surface is drawn at
     * its own pitch (v9x_d3d_i9xx_create_surface). */
    1ul                         /* depth_pitch_own        */
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
 * How many times the scanout registers are read, once per boot.
 *
 * A 60 Hz frame is about 16.7 ms and a mapped MMIO read on this class of
 * part is on the order of a microsecond, so 4096 readings of six registers
 * should span at least one frame: enough for the frame counter to advance
 * once and for the line counter to sweep. It is a bounded loop with no
 * condition on the hardware. That bounds the LOOP; it says nothing about a
 * single read into a powered-down block, which is what the note below
 * suspects hung intel57 and intel58.
 */
#define V9X_I9XX_SCAN_SAMPLES   4096ul
/*
 * The most samples the watch takes when it is waiting for the frame counter
 * to tick. 4096 spanned 136 lines in intel61 and 137 in intel67 - a fifth
 * of a 672-line frame - and missed the tick both times, which is the one
 * reading the flip work is waiting on. 131,072 is thirty-two times that,
 * about six frames, and the loop stops at the first tick, so the usual cost
 * is under one frame; the bound is for a counter that never moves.
 */
#define V9X_I9XX_SCAN_SAMPLES_MAX 131072ul

/*
 * ON again, reading only a pipe that is enabled.
 *
 * intel57 and intel58 hung to a blank screen with the first version of this
 * watch, which read both pipes' registers with pipe A powered down
 * (PIPEA_CONF 0 in every capture). intel59 ran the same build with the
 * watch compiled out and survived, so the watch was the hang; the formats
 * it shared the boot with were not. What is untested is whether the
 * powered-down pipe was the whole cause. This version never touches a pipe
 * whose PIPECONF enable bit is clear, and its next boot is the experiment
 * that says so - the read-only step before any flip is written.
 * docs\issues\2026-09-16-final-reality-renders-black-and-the-hal-faults.md.
 */
#define V9X_I9XX_SCAN_WATCH     1

/*
 * Watch both pipes' display line and frame counter, and record what moved.
 *
 * Runs after the first submitted draw of the boot, from the draw path,
 * because that is the one moment when the display is in the application's
 * mode, the window is mapped and the engine has just been proven to accept
 * commands. Reads only. The summary is pure C with a host test; the reading
 * of the numbers - whether this driver has a vblank source on this part -
 * belongs in a capture and a record, not here.
 */
#if V9X_I9XX_SCAN_WATCH
static void v9x_d3d_i9xx_watch_scanout(void)
{
    struct v9x_i9xx_scan_summary a;
    struct v9x_i9xx_scan_summary b;
    DWORD sample;
    /* Only a pipe that is ON is read. Registers in a powered-down pipe are
     * the untested read this watch is suspected of hanging on. */
    int read_a = (*v9x_d3d_i9xx_reg(V9X_I9XX_REG_PIPEA_CONF) &
                  V9X_I9XX_PIPECONF_ENABLE) != 0ul;
    int read_b = (*v9x_d3d_i9xx_reg(V9X_I9XX_REG_PIPEB_CONF) &
                  V9X_I9XX_PIPECONF_ENABLE) != 0ul;

    v9x_i9xx_scan_begin(&a);
    v9x_i9xx_scan_begin(&b);
    for (sample = 0ul; sample < V9X_I9XX_SCAN_SAMPLES_MAX; ++sample) {
        /* At least the original window, then stop at the first tick on any
         * pipe that is being read; a pipe that is off cannot supply one. */
        if (sample >= V9X_I9XX_SCAN_SAMPLES &&
            ((read_b && b.tick_seen != 0ul) ||
             (read_a && a.tick_seen != 0ul) ||
             (!read_a && !read_b))) {
            break;
        }
        if (read_a) {
            v9x_i9xx_scan_feed(&a,
                *v9x_d3d_i9xx_reg(V9X_I9XX_REG_PIPEA_DSL),
                *v9x_d3d_i9xx_reg(V9X_I9XX_REG_PIPEA_FRAMEHIGH),
                *v9x_d3d_i9xx_reg(V9X_I9XX_REG_PIPEA_FRAMEPIXEL));
        }
        if (read_b) {
            v9x_i9xx_scan_feed(&b,
                *v9x_d3d_i9xx_reg(V9X_I9XX_REG_PIPEB_DSL),
                *v9x_d3d_i9xx_reg(V9X_I9XX_REG_PIPEB_FRAMEHIGH),
                *v9x_d3d_i9xx_reg(V9X_I9XX_REG_PIPEB_FRAMEPIXEL));
        }
    }
    /* The sample count is the loop's, so a capture can tell "pipe not
     * read" (samples set, that pipe's fields zero) from "watch never ran"
     * (samples zero) - and, now that the loop stops at a tick, how far it
     * had to go to see one. */
    v9x_hal->d3d_diagnostics.scan_samples = sample;
    v9x_hal->d3d_diagnostics.scan_a_line_min = a.line_min;
    v9x_hal->d3d_diagnostics.scan_a_line_max = a.line_max;
    v9x_hal->d3d_diagnostics.scan_a_line_changes = a.line_changes;
    v9x_hal->d3d_diagnostics.scan_a_frames = v9x_i9xx_scan_frames(&a);
    v9x_hal->d3d_diagnostics.scan_b_line_min = b.line_min;
    v9x_hal->d3d_diagnostics.scan_b_line_max = b.line_max;
    v9x_hal->d3d_diagnostics.scan_b_line_changes = b.line_changes;
    v9x_hal->d3d_diagnostics.scan_b_frames = v9x_i9xx_scan_frames(&b);
    v9x_hal->d3d_diagnostics.scan_a_tick_line = a.tick_line;
    v9x_hal->d3d_diagnostics.scan_a_tick_seen = a.tick_seen;
    v9x_hal->d3d_diagnostics.scan_b_tick_line = b.tick_line;
    v9x_hal->d3d_diagnostics.scan_b_tick_seen = b.tick_seen;
}
#endif

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
/*
 * The breadcrumb a submit waits for after the head reaches the tail. The
 * draw path sets the expected value before submitting; a submit with none
 * expected (the ring flip) waits on the head alone as before. The linear
 * address is the reserve's status page through the framebuffer mapping,
 * which is how the CPU reaches stolen memory on this part.
 */
/* Polls a submit spends on the breadcrumb after the head. A frame's worth
 * of drawing is milliseconds; this is a few hundred, so a store that never
 * lands costs a slow frame and a counter, not a machine that looks hung. */
#define V9X_I9XX_BREADCRUMB_POLLS 20000ul
/* The store-only self-test's wait: once, at first use, with nothing else
 * in flight, so a generous bound costs nothing when the store works and
 * saves thousands of per-batch waits when it does not. */
#define V9X_I9XX_SELFTEST_POLLS 200000ul

/* Polls a drain may spend, summed across calls, on one outstanding
 * sequence before the channel is declared dead (review R1): seconds, not
 * forever, and never inside one call. */
#define V9X_I9XX_DRAIN_ABANDON_POLLS 4000000ul

#define V9X_I9XX_HWS_UNTRIED 0ul
#define V9X_I9XX_HWS_READY   1ul
#define V9X_I9XX_HWS_FAILED  2ul

static DWORD v9x_d3d_i9xx_breadcrumb_expected = 0ul;
static DWORD v9x_d3d_i9xx_breadcrumb_sequence = 0ul;
/* The latest sequence issued and not yet seen in the page; 0 when none.
 * A completion the driver still owes to Flip, Lock and Blt (review R1). */
static DWORD v9x_d3d_i9xx_breadcrumb_outstanding = 0ul;
static DWORD v9x_d3d_i9xx_drain_polls_spent = 0ul;
static DWORD v9x_d3d_i9xx_hws_state = V9X_I9XX_HWS_UNTRIED;

static void v9x_d3d_i9xx_note_outstanding(DWORD sequence)
{
    v9x_d3d_i9xx_breadcrumb_outstanding = sequence;
    v9x_hal->d3d_diagnostics.breadcrumb_outstanding = sequence;
    if (sequence == 0ul) {
        v9x_d3d_i9xx_drain_polls_spent = 0ul;
    }
}

/* The breadcrumb dword as the CPU reads it: the status page is the page
 * after the ring in the reserve, which the mini-VDD's ring mapping covers
 * whole, and the MTRRs hold stolen memory uncached (intel82 V9XBOOT.INI:
 * 7F800000 type 0), so a poll here sees the GPU's write when it lands. */
static volatile DWORD *v9x_d3d_i9xx_breadcrumb_linear(void)
{
    return (volatile DWORD *)(v9x_hal->engine.ring_linear_base +
                              V9X_I9XX_RING_BYTES +
                              V9X_I9XX_HWS_BREADCRUMB_BYTE);
}

/* The fill's destination: the graphics address of the status page's
 * breadcrumb dword. The status page is RING_BYTES past the ring, and the
 * ring's offset is fb.vram_bytes (the reserve boundary the family sets). */
static DWORD v9x_d3d_i9xx_breadcrumb_offset(void)
{
    return v9x_hal->fb.vram_bytes + V9X_I9XX_RING_BYTES +
           V9X_I9XX_HWS_BREADCRUMB_BYTE;
}

/*
 * Point HWS_PGA at the reserve's status page, once, before the first batch.
 *
 * The status page is fb.vram_bytes + RING_BYTES into video memory: the
 * family's reserve_video_memory ends DirectDraw's heap where the reserve
 * begins and the ring is the first thing in it (i9xx_ring.c). HWS_PGA
 * takes a PHYSICAL address (i915 v4.4 init_phys_status_page on gen3 gives
 * it a physical page and writes HWS_PGA with its bus address), and this
 * side has no BSM - so the physical address is read from the GTT's own
 * entry for that page through BAR3, which is the hardware's statement of
 * where the page is (intel80 INTELGTT: entry i maps 7F800000 + i * 0x1000).
 * Written once and read back; the three readings go to the snapshot. The
 * BIOS value it replaces (0x1FFFF000) is a page this driver never used and
 * nothing under Windows reads.
 */
static int v9x_d3d_i9xx_hws_failed(void)
{
    v9x_d3d_i9xx_hws_state = V9X_I9XX_HWS_FAILED;
    v9x_hal->d3d_diagnostics.hws_selftest = 2ul;
    return 0;
}

/*
 * Bring the status page up, once per session, and prove it before any batch
 * depends on it (review R3, H1). Every step that can fail fails the whole
 * channel: a FAILED page means no batch carries a breadcrumb and every
 * submit waits on the head alone, as before intel81 - not thousands of
 * identical waits. Reset by DriverInit (a new session or mode).
 *
 *  1. The page's physical address, from the GTT's own entry.
 *  2. The CPU's view is real: a write to the page's second dword reads back.
 *  3. Baseline: the breadcrumb dword written to zero while nothing is in
 *     flight, so a stale value cannot match a fresh sequence.
 *  4. HWS_PGA written and read back equal.
 *  5. The round trip: a store-only stream with a sentinel, submitted and
 *     waited for with the self-test bound. The sentinel arriving is the one
 *     fact every later wait rests on; its polls are recorded.
 */
static int v9x_d3d_i9xx_hws_open(void)
{
    DWORD page;
    DWORD entry;
    DWORD physical;
    DWORD stream[V9X_I9XX_BREADCRUMB_STREAM_DWORDS];
    DWORD written = 0ul;
    volatile DWORD *crumb;
    volatile DWORD *probe;

    if (v9x_d3d_i9xx_hws_state == V9X_I9XX_HWS_READY) {
        return 1;
    }
    if (v9x_d3d_i9xx_hws_state == V9X_I9XX_HWS_FAILED) {
        return 0;
    }
    if (v9x_hal->engine.gtt_linear_base == 0ul ||
        v9x_hal->engine.ring_linear_base == 0ul) {
        return v9x_d3d_i9xx_hws_failed();
    }
    page = (v9x_hal->fb.vram_bytes + V9X_I9XX_RING_BYTES) >> 12;
    if (page >= V9X_I9XX_GTT_ENTRY_COUNT) {
        return v9x_d3d_i9xx_hws_failed();
    }
    entry = *(volatile DWORD *)(v9x_hal->engine.gtt_linear_base + page * 4ul);
    if ((entry & 1ul) == 0ul) {
        return v9x_d3d_i9xx_hws_failed();
    }
    physical = entry & 0xfffff000ul;

    crumb = v9x_d3d_i9xx_breadcrumb_linear();
    probe = crumb + 1;
    *probe = 0x5a5aa5a5ul;
    v9x_hal->d3d_diagnostics.hws_cpu_probe =
        *probe == 0x5a5aa5a5ul ? 1ul : 2ul;
    *probe = 0ul;
    if (v9x_hal->d3d_diagnostics.hws_cpu_probe != 1ul) {
        return v9x_d3d_i9xx_hws_failed();
    }
    *crumb = 0ul;
    if (*crumb != 0ul) {
        return v9x_d3d_i9xx_hws_failed();
    }

    /* HWS_PGA is read for the record and no longer written: the fill form
     * of the breadcrumb goes through the GTT like every other GPU write,
     * and the page's physical address is only recorded. */
    /* ECOSKPD, for the record: on Gen3 its bit 0 says whether this part's
     * flip-pending bit means done or merely queued. Read, never acted on
     * here; the report is where it is interpreted. */
    v9x_hal->d3d_diagnostics.ecoskpd =
        *v9x_d3d_i9xx_reg(V9X_I9XX_REG_ECOSKPD);
    v9x_hal->d3d_diagnostics.hws_pga_before =
        *v9x_d3d_i9xx_reg(V9X_I9XX_REG_HWS_PGA);
    v9x_hal->d3d_diagnostics.hws_pga_written = physical;
    v9x_hal->d3d_diagnostics.hws_pga_after =
        *v9x_d3d_i9xx_reg(V9X_I9XX_REG_HWS_PGA);

    /* The round trip. The sequence counter is not used for it: the sentinel
     * is a value no batch will ever store. */
    if (v9x_i9xx_build_breadcrumb_stream(v9x_d3d_i9xx_breadcrumb_offset(),
                                         0x600d0001ul, stream,
                                         V9X_I9XX_BREADCRUMB_STREAM_DWORDS,
                                         &written) != V9X_STATUS_OK) {
        return v9x_d3d_i9xx_hws_failed();
    }
    v9x_d3d_i9xx_breadcrumb_expected = 0ul;
    if (!v9x_d3d_i9xx_ring_submit(stream, written)) {
        return v9x_d3d_i9xx_hws_failed();
    }
    {
        DWORD polls;

        for (polls = 0ul; polls < V9X_I9XX_SELFTEST_POLLS; ++polls) {
            if (*crumb == 0x600d0001ul) {
                v9x_hal->d3d_diagnostics.hws_selftest_polls = polls;
                v9x_hal->d3d_diagnostics.hws_selftest = 1ul;
                v9x_d3d_i9xx_hws_state = V9X_I9XX_HWS_READY;
                return 1;
            }
        }
        v9x_hal->d3d_diagnostics.hws_selftest_polls = polls;
        v9x_hal->d3d_diagnostics.hws_value_last = *crumb;
    }
    return v9x_d3d_i9xx_hws_failed();
}

/*
 * Has everything the GPU was given finished? Called before a Flip, a Lock or
 * a CPU fill touches memory the GPU may still be writing (review R1).
 *
 * Nothing outstanding: yes. The outstanding sequence in the page: yes, and
 * a timed-out sequence seen now is counted once as a late arrival (R2).
 * Otherwise, with wait, one bounded poll; the polls spent on this sequence
 * accumulate across calls and past the abandon bound the channel is declared
 * dead: recorded, breadcrumbs stop, and the answer is ABANDONED because
 * completion was never observed. Without wait, or before the bound: BUSY,
 * and the caller answers WASSTILLDRAWING so DirectDraw retries -
 * the wait is DirectDraw's loop, never an unbounded one here.
 */
int v9x_d3d_i9xx_render_drain(int wait)
{
    volatile DWORD *crumb;
    DWORD polls;

    if (v9x_d3d_i9xx_breadcrumb_outstanding == 0ul) {
        return V9X_RENDER_DRAIN_DONE;
    }
    crumb = v9x_d3d_i9xx_breadcrumb_linear();
    if (*crumb == v9x_d3d_i9xx_breadcrumb_outstanding) {
        ++v9x_hal->d3d_diagnostics.breadcrumb_late;
        v9x_d3d_i9xx_note_outstanding(0ul);
        return V9X_RENDER_DRAIN_DONE;
    }
    ++v9x_hal->d3d_diagnostics.render_drain_waits;
    if (!wait) {
        ++v9x_hal->d3d_diagnostics.render_drain_stalls;
        return V9X_RENDER_DRAIN_BUSY;
    }
    for (polls = 0ul; polls < V9X_I9XX_BREADCRUMB_POLLS; ++polls) {
        if (*crumb == v9x_d3d_i9xx_breadcrumb_outstanding) {
            ++v9x_hal->d3d_diagnostics.breadcrumb_late;
            v9x_d3d_i9xx_note_outstanding(0ul);
            return V9X_RENDER_DRAIN_DONE;
        }
    }
    v9x_d3d_i9xx_drain_polls_spent += polls;
    if (v9x_d3d_i9xx_drain_polls_spent >= V9X_I9XX_DRAIN_ABANDON_POLLS) {
        ++v9x_hal->d3d_diagnostics.breadcrumb_abandoned;
        v9x_hal->d3d_diagnostics.hws_value_last = *crumb;
        v9x_d3d_i9xx_note_outstanding(0ul);
        v9x_d3d_i9xx_hws_failed();
        return V9X_RENDER_DRAIN_ABANDONED;
    }
    ++v9x_hal->d3d_diagnostics.render_drain_stalls;
    return V9X_RENDER_DRAIN_BUSY;
}

/* A new session: the page is untried again and nothing is owed. The
 * sequence counter runs on so a stale value cannot match a fresh one. */
void v9x_d3d_i9xx_reset(void)
{
    v9x_d3d_i9xx_hws_state = V9X_I9XX_HWS_UNTRIED;
    v9x_d3d_i9xx_breadcrumb_expected = 0ul;
    v9x_d3d_i9xx_breadcrumb_outstanding = 0ul;
    v9x_d3d_i9xx_drain_polls_spent = 0ul;
}

/*
 * The head wait of the latest submission, in TSC cycles, for the batch-shape
 * cells (V9X_D3D_DIAGNOSTICS.batch_cells). Zero when the latest submission
 * did not reach the tail or timing is off.
 */
static DWORD v9x_d3d_i9xx_last_head_cycles;

int v9x_d3d_i9xx_ring_submit(const DWORD *stream, DWORD dwords)
{
    struct v9x_i9xx_ring_plan plan;
    DWORD ring_linear = 0ul;
    DWORD ring_bytes = 0ul;
    DWORD head;
    DWORD tail;
    DWORD polls;
    DWORD index;
    DWORD phase_started;
    volatile DWORD *ring;

    v9x_d3d_i9xx_last_head_cycles = 0ul;
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
    phase_started = V9X_TIME_BEGIN();
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
    V9X_TIME_END(V9X_TIME_RING_WRITE, phase_started);

    phase_started = V9X_TIME_BEGIN();
    for (polls = 0ul; polls < V9X_I9XX_SUBMIT_POLLS; ++polls) {
        if (v9x_i9xx_ring_submission_complete(
                *v9x_d3d_i9xx_reg(V9X_I9XX_REG_RING_HEAD),
                plan.next_tail) != V9X_FALSE) {
            DWORD lag;

            if (V9X_TIME_ENABLED()) {
                v9x_d3d_i9xx_last_head_cycles =
                    v9x_rdtsc_low() - phase_started;
            }
            V9X_TIME_END(V9X_TIME_HEAD_WAIT, phase_started);
            phase_started = V9X_TIME_BEGIN();

            /*
             * The parser is at the tail. Is the ENGINE? ACTHD and
             * INSTDONE, raw, now and after a fixed number of polls, and
             * whether ACTHD kept changing in between. No address
             * arithmetic and no claim of completion: the register's Gen3
             * form is not validated on this part, and a value that moves
             * after the parser is done is the one fact that needs none.
             */
            {
                /* Two raw reads and nothing more: intel85 watched these for
                 * 2,000 polls a batch, saw ACTHD never move and INSTDONE
                 * never change in 82,249 submits, and paid for it with the
                 * frame rate (89 flips). Kept as a sample of the last
                 * submit only. */
                V9X_D3D_DIAGNOSTICS *d = &v9x_hal->d3d_diagnostics;

                d->acthd_at_head_last = *v9x_d3d_i9xx_reg(V9X_I9XX_REG_ACTHD);
                d->instdone_at_head_last =
                    *v9x_d3d_i9xx_reg(V9X_I9XX_REG_INSTDONE);
                d->tail_last = plan.next_tail;
            }

            if (v9x_d3d_i9xx_breadcrumb_expected == 0ul) {
                return 1;
            }
            /* The head is at the tail. Now the pixels: the store behind
             * the flush arrives when the drawing ahead of it is done. */
            for (lag = 0ul; lag < V9X_I9XX_BREADCRUMB_POLLS; ++lag) {
                if (*v9x_d3d_i9xx_breadcrumb_linear() ==
                    v9x_d3d_i9xx_breadcrumb_expected) {
                    ++v9x_hal->d3d_diagnostics.breadcrumb_submits;
                    v9x_hal->d3d_diagnostics.breadcrumb_lag_polls_total += lag;
                    if (lag > v9x_hal->d3d_diagnostics.breadcrumb_lag_polls_max) {
                        v9x_hal->d3d_diagnostics.breadcrumb_lag_polls_max = lag;
                    }
                    v9x_d3d_i9xx_note_outstanding(0ul);
                    V9X_TIME_END(V9X_TIME_CRUMB_WAIT, phase_started);
                    return 1;
                }
            }
            /*
             * Timed out. The draw is not failed - the commands are in the
             * ring and will run - but the completion is still OWED: the
             * sequence stays outstanding and Flip, Lock and Blt wait for it
             * (v9x_d3d_i9xx_render_drain) before touching what it covers.
             * The value read now says whether an older store has landed.
             */
            ++v9x_hal->d3d_diagnostics.breadcrumb_timeouts;
            v9x_hal->d3d_diagnostics.hws_value_last =
                *v9x_d3d_i9xx_breadcrumb_linear();
            v9x_d3d_i9xx_note_outstanding(v9x_d3d_i9xx_breadcrumb_expected);
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
 * A DirectDraw blit through the ring, for engines\eng_i9xx.c.
 *
 * `stream` holds the blit and its MI_FLUSH, `dwords` long, with room for
 * `capacity`. This seals it the way the draw path seals a batch - the
 * breadcrumb behind the flush, then a pad to an even count - runs the 2D
 * allowlist over the result, and submits it. Sealing is the whole of the
 * synchronisation (plan decision 5): a blit whose breadcrumb has not landed
 * is outstanding, and Flip, Lock and the CPU blits already wait for that.
 *
 * Exported rather than letting the engine build the breadcrumb because the
 * sequence and its expected value are this file's state and stay so.
 * 1 when the ring took the stream, 0 when anything refused it.
 */
int v9x_d3d_i9xx_submit_blt(DWORD *stream, DWORD dwords, DWORD capacity,
                            DWORD bytes_per_pixel)
{
    DWORD at = dwords;
    DWORD rejected = 0ul;
    DWORD produced = 0ul;
    int submitted;

    if (v9x_hal == 0 || stream == 0 || dwords == 0ul || dwords > capacity) {
        return 0;
    }
    if (v9x_d3d_i9xx_hws_open()) {
        /* As the draw path: a landed predecessor is resolved without
         * waiting, because one engine runs its commands in order. */
        (void)v9x_d3d_i9xx_render_drain(0);
        v9x_d3d_i9xx_breadcrumb_expected = ++v9x_d3d_i9xx_breadcrumb_sequence;
        if (v9x_d3d_i9xx_breadcrumb_expected == 0ul) {
            v9x_d3d_i9xx_breadcrumb_expected =
                ++v9x_d3d_i9xx_breadcrumb_sequence;
        }
        if (v9x_i9xx_build_breadcrumb_stream(
                v9x_d3d_i9xx_breadcrumb_offset(),
                v9x_d3d_i9xx_breadcrumb_expected, stream + at,
                capacity - at, &produced) != V9X_STATUS_OK) {
            v9x_d3d_i9xx_breadcrumb_expected = 0ul;
            return 0;
        }
        at += produced;
    } else {
        v9x_d3d_i9xx_breadcrumb_expected = 0ul;
    }
    if ((at & 1ul) != 0ul) {
        if (at >= capacity) {
            v9x_d3d_i9xx_breadcrumb_expected = 0ul;
            return 0;
        }
        stream[at++] = V9X_I9XX_MI_NOOP;
    }

    if (v9x_i9xx_decode_blt_stream(
            stream, at, bytes_per_pixel, v9x_hal->fb.vram_bytes,
            v9x_d3d_i9xx_breadcrumb_expected != 0ul
                ? v9x_d3d_i9xx_breadcrumb_offset() : 0ul,
            &rejected) == V9X_FALSE) {
        v9x_d3d_i9xx_breadcrumb_expected = 0ul;
        return 0;
    }
    submitted = v9x_d3d_i9xx_ring_submit(stream, at);
    v9x_d3d_i9xx_breadcrumb_expected = 0ul;
    return submitted;
}

/*
 * Why a draw was refused, for the counters. intel52 had 404 calls and about
 * eighty submissions with nothing to say what became of the rest.
 */
#define V9X_I9XX_REFUSE_NONE        0ul
#define V9X_I9XX_REFUSE_ARGUMENTS   1ul
#define V9X_I9XX_REFUSE_BATCH       2ul
#define V9X_I9XX_REFUSE_TARGET      3ul
#define V9X_I9XX_REFUSE_STATE       4ul
#define V9X_I9XX_REFUSE_PROGRAM     5ul
#define V9X_I9XX_REFUSE_VERTICES    6ul
#define V9X_I9XX_REFUSE_CAPACITY    7ul
#define V9X_I9XX_REFUSE_DECODER     8ul
#define V9X_I9XX_REFUSE_SUBMIT      9ul

static int v9x_d3d_i9xx_refuse(DWORD reason)
{
    if (v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.i9xx_draws_refused;
        v9x_hal->d3d_diagnostics.i9xx_refuse_last = reason;
    }
    return 0;
}

/*
 * Which of the three MAP_STATE formats a texture surface is, or none.
 *
 * RGB565, ARGB1555 and ARGB4444: the 16-bit types MS3 can name, and the
 * three the builder, the decoder and the published list agree on. The
 * fragment program reads the sampler's RGBA whatever the type, so the format
 * is one MAP_STATE word and nothing else in the stream changes. Anything
 * outside the three is refused here, counted, with its layout recorded -
 * which is how intel56 named Final Reality's 4:4:4:4 when only 565 was
 * accepted.
 *
 * It is also the format the desktop is in: this driver selects 5:6:5 for a
 * Gen3 machine (v9x_dd_engine_wants_555 is false for this engine), so a
 * surface with no format of its own is already the right one - which is why
 * the display's format is classified rather than treated as a refusal.
 */
static int v9x_d3d_i9xx_texture_format(const V9X_DD_SURFACE_LCL *surface,
                                       DWORD *format_out)
{
    const V9X_DDPIXELFORMAT *pixel;

    if (format_out != 0) {
        *format_out = 0ul;
    }
    if (surface == 0 || surface->lpGbl == 0) {
        return 0;
    }
    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul) {
        pixel = &surface->lpGbl->ddpfSurface;
    } else if (v9x_hal != 0) {
        /* No format of its own means the display's, and reading ddpfSurface
         * anyway would read past the allocation - the DDK allocates it only
         * in the differing case. */
        pixel = &v9x_hal->info.vmiData.ddpfDisplay;
    } else {
        return 0;
    }
    if ((pixel->dwFlags & V9X_DDPF_RGB) == 0ul ||
        pixel->dwRGBBitCount != 16ul) {
        if (v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.texture_refused_format;
            v9x_hal->d3d_diagnostics.texture_refused_last =
                (pixel->dwRGBBitCount << 24) |
                (pixel->dwRBitMask & 0x00fffffful);
        }
        return 0;
    }
    /*
     * The three 16-bit layouts MAP_STATE can name, told apart by their masks.
     * The alpha mask is checked too for the two that carry one: a surface
     * described as 1:5:5:5 with no alpha bit is 555, which the sampler has
     * no type for, and reading it as 1555 would take its top bit as alpha.
     */
    if (pixel->dwRBitMask == 0x0000f800ul &&
        pixel->dwGBitMask == 0x000007e0ul &&
        pixel->dwBBitMask == 0x0000001ful) {
        if (format_out != 0) {
            *format_out = V9X_I9XX_MAPSURF_16BIT_RGB565;
        }
        return 1;
    }
    if (pixel->dwRBitMask == 0x00007c00ul &&
        pixel->dwGBitMask == 0x000003e0ul &&
        pixel->dwBBitMask == 0x0000001ful &&
        (pixel->dwFlags & V9X_DDPF_ALPHAPIXELS) != 0ul &&
        pixel->dwRGBAlphaBitMask == 0x00008000ul) {
        if (format_out != 0) {
            *format_out = V9X_I9XX_MAPSURF_16BIT_ARGB1555;
        }
        return 1;
    }
    if (pixel->dwRBitMask == 0x00000f00ul &&
        pixel->dwGBitMask == 0x000000f0ul &&
        pixel->dwBBitMask == 0x0000000ful &&
        (pixel->dwFlags & V9X_DDPF_ALPHAPIXELS) != 0ul &&
        pixel->dwRGBAlphaBitMask == 0x0000f000ul) {
        if (format_out != 0) {
            *format_out = V9X_I9XX_MAPSURF_16BIT_ARGB4444;
        }
        return 1;
    }
    if (v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.texture_refused_format;
        v9x_hal->d3d_diagnostics.texture_refused_last =
            (pixel->dwRGBBitCount << 24) |
            (pixel->dwRBitMask & 0x00fffffful);
    }
    return 0;
}

/*
 * The bound texture as a map, or nothing.
 *
 * Returns zero when this draw samples nothing, which is NOT an error: an
 * application that bound no texture draws untextured, and one whose texture
 * this engine cannot sample draws untextured too rather than not at all.
 *
 * That second case is the ViRGE's behaviour and it is chosen for the same
 * reason - a refused draw is a hole in the frame, a refused texture is the
 * vertex colour where a texture should be, and the second is both closer to
 * what was asked for and easier to recognise. It is only defensible because
 * every refusal is counted: an uncounted silent fallback is a wrong picture
 * nobody can explain, which is what texture_refused_* exist to prevent.
 */
/*
 * A D3DFILTER value as the two things SS2 sets: bilinear within a level, and
 * how levels are chosen.
 *
 * The DirectX 5 names read backwards, and the Windows 98 DDK's own ViRGE HAL
 * is what settles them (src\display\mini\s3v\D3DRENDR.C:124-141): MIPNEAREST
 * is one texel of the nearest level, MIPLINEAR four texels of the nearest
 * level, LINEARMIPNEAREST one texel of each of two levels blended, and
 * LINEARMIPLINEAR eight - trilinear. So "LINEAR" before "MIP" is the blend
 * BETWEEN levels and "LINEAR" after it the filter WITHIN one.
 *
 * Until 2026-09-25 this engine read the within-level part the other way -
 * bilinear for LINEARMIPNEAREST, nearest for MIPLINEAR - which no map could
 * show while no map had levels.
 */
static void v9x_d3d_i9xx_filter(DWORD filter, DWORD *linear_out,
                                DWORD *mip_out)
{
    *linear_out = (filter == V9X_R3D_FILTER_LINEAR ||
                   filter == V9X_R3D_FILTER_MIPLINEAR ||
                   filter == V9X_R3D_FILTER_LINEARMIPLINEAR) ? 1ul : 0ul;
    if (filter == V9X_R3D_FILTER_MIPNEAREST ||
        filter == V9X_R3D_FILTER_MIPLINEAR) {
        *mip_out = V9X_I9XX_MIPFILTER_NEAREST;
    } else if (filter == V9X_R3D_FILTER_LINEARMIPNEAREST ||
               filter == V9X_R3D_FILTER_LINEARMIPLINEAR) {
        *mip_out = V9X_I9XX_MIPFILTER_LINEAR;
    } else {
        *mip_out = V9X_I9XX_MIPFILTER_NONE;
    }
}

/*
 * MIP TREES.
 *
 * Gen3's MAP_STATE has one address and one pitch for a whole chain, and the
 * sampler finds each level by a layout fixed in the part
 * (v9x_d3d_i9xx_layout_miptree). DirectDraw's heap creates each level as a
 * surface of its own, with its own pitch, wherever there is room - which is
 * never that layout, so a chain the heap placed can only ever be sampled at
 * its top level.
 *
 * So CreateSurface places a mip chain here: one block for the whole tree from
 * DirectDraw's own heap, through the DDHAL32_VidMemAlloc that DDRAW.DLL
 * exports "for drivers that want to handle their own memory allocation", and
 * each level's fpVidMem pointed at its place in it with the tree's pitch. It
 * is what the Windows 98 DDK's ViRGE driver does for its own chains
 * (src\display\mini\s3v\S3_DD32.C:3046-3116, built with /DMIP), with the
 * ViRGE's end-to-end layout replaced by Gen3's.
 *
 * Three choices, each for a reason:
 *
 *  - The exports are looked up at run time, not linked. A DDRAW.DLL without
 *    them - or a process where it is not loaded - declines to the heap as
 *    before, and a link-time import would instead fail to load the HAL.
 *  - The block is PAGE aligned by over-asking a page and rounding up, because
 *    MAP_STATE's address is page aligned (texture_align above) and the heap
 *    call takes no alignment. The block's own start is kept in the top
 *    level's dwReserved1, the field DDRAWI.H reserves for the display
 *    driver, because that is what has to be freed.
 *  - Every level's lpVidMemHeap is left NULL, which is what tells DirectDraw
 *    the memory is not its to free; DestroySurface frees the block when the
 *    top level goes. The DDK sample's single-heap case leaves it NULL too.
 *
 * Declines are counted with a reason, so a capture says why a chain was left
 * to the heap. UNMEASURED on this part until the probe's mip ladder runs.
 */
#define V9X_D3D_I9XX_MIPTREE_SHAPE   1ul  /* not a halving square chain  */
#define V9X_D3D_I9XX_MIPTREE_FORMAT  2ul  /* not 16 bits per texel       */
#define V9X_D3D_I9XX_MIPTREE_EXPORT  3ul  /* DDRAW.DLL lacks the exports */
#define V9X_D3D_I9XX_MIPTREE_ALLOC   4ul  /* the heap had no room        */
#define V9X_D3D_I9XX_MIPTREE_BOUNDS  5ul  /* the block is outside VRAM   */

/* DDRAWI.H's LPDDHAL_VIDMEMALLOC / _VIDMEMFREE shapes, 32-bit. */
typedef DWORD (WINAPI *V9X_D3D_I9XX_VIDMEMALLOC)(DWORD lpDD, int heap,
                                                 DWORD width, DWORD height);
typedef void (WINAPI *V9X_D3D_I9XX_VIDMEMFREE)(DWORD lpDD, int heap,
                                               DWORD memory);

/* Per process, as the DLL's data is; resolved on first use. */
static V9X_D3D_I9XX_VIDMEMALLOC v9x_d3d_i9xx_vidmem_alloc;
static V9X_D3D_I9XX_VIDMEMFREE v9x_d3d_i9xx_vidmem_free;

static int v9x_d3d_i9xx_vidmem_resolve(void)
{
    HMODULE ddraw;

    if (v9x_d3d_i9xx_vidmem_alloc != 0 && v9x_d3d_i9xx_vidmem_free != 0) {
        return 1;
    }
    ddraw = GetModuleHandleA("DDRAW.DLL");
    if (ddraw == 0) {
        return 0;
    }
    v9x_d3d_i9xx_vidmem_alloc = (V9X_D3D_I9XX_VIDMEMALLOC)GetProcAddress(
        ddraw, "DDHAL32_VidMemAlloc");
    v9x_d3d_i9xx_vidmem_free = (V9X_D3D_I9XX_VIDMEMFREE)GetProcAddress(
        ddraw, "DDHAL32_VidMemFree");
    return (v9x_d3d_i9xx_vidmem_alloc != 0 &&
            v9x_d3d_i9xx_vidmem_free != 0) ? 1 : 0;
}

static DWORD v9x_d3d_i9xx_miptree_decline(DWORD reason)
{
    ++v9x_hal->d3d_diagnostics.mip_tree_declined;
    v9x_hal->d3d_diagnostics.mip_tree_declined_last = reason;
    return V9X_DDHAL_DRIVER_NOTHANDLED;
}

/*
 * One block of pitch * rows from DirectDraw's heap, page aligned, with
 * surface n of the list pointed at base + offsets[n] at that pitch.
 *
 * Shared by the mip trees and the padded Z buffer. The block's own start is
 * kept in the first surface's dwReserved1 and every surface's lpVidMemHeap
 * left NULL, which is the signature destroy_surface frees by. Returns zero
 * and the block's graphics offset, or a V9X_D3D_I9XX_MIPTREE_* reason having
 * placed nothing.
 */
static DWORD v9x_d3d_i9xx_place_block(V9X_DDHAL_CREATESURFACEDATA *data,
                                      DWORD pitch, DWORD rows,
                                      const v9x_u32 *offsets,
                                      DWORD *base_out)
{
    V9X_DD_SURFACE_LCL **list = (V9X_DD_SURFACE_LCL **)data->lplpSList;
    DWORD index;
    DWORD block;
    DWORD base;
    DWORD footprint;
    DWORD vram;

    *base_out = 0ul;
    if (!v9x_d3d_i9xx_vidmem_resolve()) {
        return V9X_D3D_I9XX_MIPTREE_EXPORT;
    }
    if ((v9x_hal->fb.flags & V9X_DD_FB_VALID) == 0ul ||
        (v9x_hal->fb.linear_base & (V9X_I9XX_SANDBOX_PAGE_BYTES - 1ul)) !=
            0ul) {
        return V9X_D3D_I9XX_MIPTREE_BOUNDS;
    }

    /*
     * The block plus a page of rows, so it can be rounded up to a page and
     * still hold everything. Heap 0 is the one heap this driver publishes
     * (vmiData.dwNumHeaps); width is bytes, as the DDK sample passes lPitch.
     */
    block = v9x_d3d_i9xx_vidmem_alloc(
        data->lpDD, 0, pitch,
        rows + (V9X_I9XX_SANDBOX_PAGE_BYTES + pitch - 1ul) / pitch);
    if (block == 0ul) {
        return V9X_D3D_I9XX_MIPTREE_ALLOC;
    }
    vram = v9x_hal->fb.vram_bytes;
    footprint = pitch * rows;
    base = 0xfffffffful;
    if (block >= v9x_hal->fb.linear_base &&
        block - v9x_hal->fb.linear_base < vram) {
        base = (block - v9x_hal->fb.linear_base +
                V9X_I9XX_SANDBOX_PAGE_BYTES - 1ul) &
               ~(V9X_I9XX_SANDBOX_PAGE_BYTES - 1ul);
    }
    if (base == 0xfffffffful || base > vram || footprint > vram - base) {
        v9x_d3d_i9xx_vidmem_free(data->lpDD, 0, block);
        return V9X_D3D_I9XX_MIPTREE_BOUNDS;
    }

    for (index = 0ul; index < data->dwSCnt; ++index) {
        V9X_DD_SURFACE_GBL *surface = list[index]->lpGbl;

        surface->fpVidMem = v9x_hal->fb.linear_base + base + offsets[index];
        surface->lPitch = (LONG)pitch;
        /* lpVidMemHeap, in the union DDRAWI.H shares with dwBlockSizeX:
         * NULL is "not DirectDraw's to free". */
        surface->dwBlockSizeX = 0ul;
        surface->dwReserved1 = 0ul;
    }
    list[0]->lpGbl->dwReserved1 = block;
    *base_out = base;
    return 0ul;
}

/*
 * The Z buffer, padded where its row would be a power of two.
 *
 * 3DMark99 filled about ten times more slowly per pixel at 1024x576 than at
 * 640x480 on this part (2026-09-25-the-gen3-head-wait-is-pixels.md), and the
 * one layout property that separates the two modes is the 2048-byte pitch
 * the colour and depth surfaces share at 1024x576 against 1280. A
 * power-of-two pitch can put every row of both on the same cache sets or
 * DRAM banks. The scanout never reads the Z buffer, so its pitch is this
 * driver's to choose: 64 bytes more - the granule Mesa aligns linear
 * surfaces to - breaks the power of two without moving the colour side.
 * This is the EXPERIMENT that tests the hypothesis; a pitch that is not a
 * power of two is left to DirectDraw exactly as before.
 */
#define V9X_D3D_I9XX_Z_PAD_BYTES 64ul

static DWORD v9x_d3d_i9xx_create_depth(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL *surface = ((V9X_DD_SURFACE_LCL **)data->lplpSList)[0];
    v9x_u32 offsets[1];
    DWORD natural;
    DWORD pitch;
    DWORD base;

    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul &&
        surface->lpGbl->ddpfSurface.dwRGBBitCount != 16ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    natural = (((DWORD)surface->lpGbl->wWidth * 2ul) + 7ul) & ~7ul;
    if (natural == 0ul || (natural & (natural - 1ul)) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    pitch = natural + V9X_D3D_I9XX_Z_PAD_BYTES;
    if (pitch > v9x_d3d_i9xx_limits.target_pitch_max) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    offsets[0] = 0ul;
    if (v9x_d3d_i9xx_place_block(data, pitch,
                                 (DWORD)surface->lpGbl->wHeight,
                                 offsets, &base) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    ++v9x_hal->d3d_diagnostics.z_placed;
    v9x_hal->d3d_diagnostics.z_placed_pitch = pitch;
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

/*
 * A plain texture at the pitch its row needs.
 *
 * DirectDraw's heap gives every texture surface a pitch aligned to the 4 KiB
 * texture alignment this engine publishes (MipGapActual=0x1000 on the
 * netbook), so a 64-texel texture of 128 bytes a row holds 256 KB instead of
 * 8. With a 1024x576 triple-buffered chain and Z buffer that left about a
 * megabyte of a 5.66 MB heap for textures, and 3DMark99 had thousands of mip
 * chains declined and drawn untextured for want of room (2026-09-25). So a
 * texture the bind can sample - 16 bits, square, a power of two within the
 * limits - is placed in a block of its own at the tree layout's one-level
 * pitch (64-byte aligned), page aligned as MAP_STATE needs. The page of slack
 * the alignment costs is the price of not knowing where the heap will put
 * the block; it is still an order of magnitude less than the pitch it
 * replaces. Anything else is left to DirectDraw exactly as before.
 */
static DWORD v9x_d3d_i9xx_create_texture(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL *surface = ((V9X_DD_SURFACE_LCL **)data->lplpSList)[0];
    const V9X_DDPIXELFORMAT *pixel;
    struct v9x_d3d_i9xx_miptree tree;
    DWORD width = (DWORD)surface->lpGbl->wWidth;
    DWORD height = (DWORD)surface->lpGbl->wHeight;
    DWORD base;

    if (v9x_d3d_i9xx_texture_shape(width, height,
                                   V9X_D3D_I9XX_SAMPLER_SIZE_MIN,
                                   v9x_d3d_i9xx_limits.texture_size_max) ==
            V9X_FALSE) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    pixel = (surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul
        ? &surface->lpGbl->ddpfSurface
        : &v9x_hal->info.vmiData.ddpfDisplay;
    if ((pixel->dwFlags & V9X_DDPF_RGB) == 0ul ||
        pixel->dwRGBBitCount != 16ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    if (v9x_d3d_i9xx_layout_miptree(width, height, 1ul, &tree) == V9X_FALSE ||
        v9x_d3d_i9xx_place_block(data, tree.pitch, tree.rows,
                                 tree.level_offset, &base) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    ++v9x_hal->d3d_diagnostics.texture_placed;
    v9x_hal->d3d_diagnostics.texture_placed_bytes += tree.pitch * tree.rows;
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

static DWORD v9x_d3d_i9xx_create_surface(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL **list = (V9X_DD_SURFACE_LCL **)data->lplpSList;
    V9X_DD_SURFACE_LCL *top;
    const V9X_DDPIXELFORMAT *pixel;
    struct v9x_d3d_i9xx_miptree tree;
    DWORD width;
    DWORD height;
    DWORD index;
    DWORD base;
    DWORD reason;

    if (v9x_hal == 0 || list == 0 || data->dwSCnt == 0ul || list[0] == 0 ||
        list[0]->lpGbl == 0) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    /* A lone video-memory Z buffer. */
    if (data->dwSCnt == 1ul &&
        (list[0]->ddsCaps & V9X_DDSCAPS_ZBUFFER) != 0ul &&
        (list[0]->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) == 0ul) {
        return v9x_d3d_i9xx_create_depth(data);
    }
    /* A lone video-memory texture, including a MIPMAP surface of one level. */
    if (data->dwSCnt == 1ul &&
        (list[0]->ddsCaps & V9X_DDSCAPS_TEXTURE) != 0ul &&
        (list[0]->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) == 0ul) {
        return v9x_d3d_i9xx_create_texture(data);
    }
    /* A mip chain in video memory, or nothing this engine places - and not
     * counted, because every other surface arrives here too. */
    if (data->dwSCnt < 2ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    top = list[0];
    if (top == 0 || top->lpGbl == 0 ||
        (top->ddsCaps & (V9X_DDSCAPS_TEXTURE | V9X_DDSCAPS_MIPMAP)) !=
            (V9X_DDSCAPS_TEXTURE | V9X_DDSCAPS_MIPMAP) ||
        (top->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }

    /*
     * The shape the sampler can read: a top the bind would accept, and
     * every level after it each edge half the one before down to one
     * texel, in list order (a non-square chain keeps halving its longer
     * edge after the shorter reaches one). The list is DirectDraw's, top
     * first; one that is not in that order is declined rather than sorted.
     * The layout below rejects a chain longer than the larger edge allows.
     */
    width = (DWORD)top->lpGbl->wWidth;
    height = (DWORD)top->lpGbl->wHeight;
    if (data->dwSCnt > V9X_D3D_I9XX_MIP_LEVELS_MAX ||
        v9x_d3d_i9xx_texture_shape(width, height,
                                   V9X_D3D_I9XX_SAMPLER_SIZE_MIN,
                                   v9x_d3d_i9xx_limits.texture_size_max) ==
            V9X_FALSE) {
        return v9x_d3d_i9xx_miptree_decline(V9X_D3D_I9XX_MIPTREE_SHAPE);
    }
    for (index = 1ul; index < data->dwSCnt; ++index) {
        const V9X_DD_SURFACE_LCL *level = list[index];

        if (level == 0 || level->lpGbl == 0 ||
            (level->ddsCaps & V9X_DDSCAPS_MIPMAP) == 0ul ||
            (DWORD)level->lpGbl->wWidth !=
                v9x_d3d_i9xx_level_edge(width, index) ||
            (DWORD)level->lpGbl->wHeight !=
                v9x_d3d_i9xx_level_edge(height, index)) {
            return v9x_d3d_i9xx_miptree_decline(V9X_D3D_I9XX_MIPTREE_SHAPE);
        }
    }
    /* Sixteen bits a texel, which is what the layout's two bytes assume and
     * all three MAP_STATE formats are; the bind classifies which one. No
     * format of its own means the display's. */
    pixel = (top->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul
        ? &top->lpGbl->ddpfSurface
        : &v9x_hal->info.vmiData.ddpfDisplay;
    if ((pixel->dwFlags & V9X_DDPF_RGB) == 0ul ||
        pixel->dwRGBBitCount != 16ul) {
        return v9x_d3d_i9xx_miptree_decline(V9X_D3D_I9XX_MIPTREE_FORMAT);
    }
    if (v9x_d3d_i9xx_layout_miptree(width, height, data->dwSCnt, &tree) ==
            V9X_FALSE) {
        return v9x_d3d_i9xx_miptree_decline(V9X_D3D_I9XX_MIPTREE_SHAPE);
    }
    reason = v9x_d3d_i9xx_place_block(data, tree.pitch, tree.rows,
                                      tree.level_offset, &base);
    if (reason != 0ul) {
        return v9x_d3d_i9xx_miptree_decline(reason);
    }

    ++v9x_hal->d3d_diagnostics.mip_tree_allocs;
    v9x_hal->d3d_diagnostics.mip_tree_last_offset = base;
    v9x_hal->d3d_diagnostics.mip_tree_last_shape =
        (tree.pitch << 16) | (tree.rows & 0xfffful);
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

/*
 * The top level of a tree placed above, a padded Z buffer or a placed plain
 * texture is going; free the block. All three count in mip_tree_frees.
 *
 * Recognised by the signature create_surface leaves and the heap never does:
 * a mip level or Z buffer whose lpVidMemHeap is NULL, whose dwReserved1 holds a block
 * start, and whose fpVidMem lies within the page that start was rounded up
 * across. A surface DirectDraw placed has its heap set and is ignored, and
 * so is every lower level, whose dwReserved1 is zero. Cleared after the
 * free, so a second destroy of the same surface frees nothing.
 */
static void v9x_d3d_i9xx_destroy_surface(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL *surface = (V9X_DD_SURFACE_LCL *)data->lpDDSurface;
    V9X_DD_SURFACE_GBL *global;
    DWORD block;

    if (v9x_hal == 0 || surface == 0 || surface->lpGbl == 0) {
        return;
    }
    global = surface->lpGbl;
    block = global->dwReserved1;
    if ((surface->ddsCaps & (V9X_DDSCAPS_MIPMAP | V9X_DDSCAPS_ZBUFFER |
                             V9X_DDSCAPS_TEXTURE)) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul ||
        block == 0ul || global->dwBlockSizeX != 0ul ||
        global->fpVidMem < block ||
        global->fpVidMem - block >= V9X_I9XX_SANDBOX_PAGE_BYTES) {
        return;
    }
    if (!v9x_d3d_i9xx_vidmem_resolve()) {
        return;
    }
    v9x_d3d_i9xx_vidmem_free(data->lpDD, 0, block);
    global->dwReserved1 = 0ul;
    ++v9x_hal->d3d_diagnostics.mip_tree_frees;
}

/*
 * How many levels of the bound texture's chain the sampler may read: one when
 * there is no chain or when its levels are not where the layout puts them,
 * and each level that is, in order, otherwise.
 *
 * This is the check the ViRGE's v9x_d3d_mip_chain_contiguous makes for its
 * own layout, on Gen3's. A chain create_surface placed passes it by
 * construction; one the heap placed - because create_surface declined, or
 * because an application built it with AddAttachedSurface - does not, and is
 * sampled at its top level with no mip filter. That is the behaviour every
 * texture had before 2026-09-25, and it is counted in the ViRGE's mip_gap_*
 * counters so a capture tells the two apart. The levels found are laid out
 * into `tree` when there is more than one.
 */
static DWORD v9x_d3d_i9xx_mip_levels(const V9X_DD_SURFACE_LCL *top,
                                     struct v9x_d3d_i9xx_miptree *tree)
{
    const V9X_DD_SURFACE_LCL *chain[V9X_D3D_I9XX_MIP_LEVELS_MAX];
    const V9X_DD_SURFACE_LCL *level = top;
    DWORD width = (DWORD)top->lpGbl->wWidth;
    DWORD height = (DWORD)top->lpGbl->wHeight;
    DWORD count = 1ul;
    DWORD verified;

    if ((top->ddsCaps & V9X_DDSCAPS_MIPMAP) == 0ul) {
        return 1ul;
    }
    ++v9x_hal->d3d_diagnostics.mip_chain_checks;
    v9x_hal->d3d_diagnostics.mip_chain_levels = 0ul;
    v9x_hal->d3d_diagnostics.mip_chain_delta = 0xfffffffful;
    chain[0] = top;

    /* Down the attachments: the next level is the attached surface that is
     * itself a mip level, as the ViRGE walk takes it. Bounded by the tree. */
    while (count < V9X_D3D_I9XX_MIP_LEVELS_MAX) {
        const V9X_DD_ATTACH_NODE *node =
            (const V9X_DD_ATTACH_NODE *)level->lpAttachList;
        const V9X_DD_SURFACE_LCL *next = 0;

        while (node != 0) {
            if (node->object != 0 && node->object != level &&
                (node->object->ddsCaps & V9X_DDSCAPS_MIPMAP) != 0ul) {
                next = node->object;
                break;
            }
            node = node->next;
        }
        if (next == 0) {
            break;
        }
        if (next->lpGbl == 0 ||
            (DWORD)next->lpGbl->wWidth !=
                v9x_d3d_i9xx_level_edge(width, count) ||
            (DWORD)next->lpGbl->wHeight !=
                v9x_d3d_i9xx_level_edge(height, count)) {
            ++v9x_hal->d3d_diagnostics.mip_gap_shape;
            break;
        }
        chain[count++] = next;
        level = next;
    }
    if (count == 1ul) {
        return 1ul;
    }
    if (v9x_d3d_i9xx_layout_miptree(width, height, count, tree) ==
            V9X_FALSE) {
        ++v9x_hal->d3d_diagnostics.mip_gap_shape;
        ++v9x_hal->d3d_diagnostics.mip_chain_gaps;
        return 1ul;
    }
    v9x_hal->d3d_diagnostics.mip_chain_delta =
        chain[1]->lpGbl->fpVidMem - top->lpGbl->fpVidMem;

    /* Every level at the tree's pitch and exactly at its place. The first
     * that is not ends the usable chain. */
    verified = 0ul;
    if ((DWORD)top->lpGbl->lPitch == tree->pitch) {
        for (verified = 1ul; verified < count; ++verified) {
            DWORD expected = top->lpGbl->fpVidMem +
                             tree->level_offset[verified];

            if ((DWORD)chain[verified]->lpGbl->lPitch != tree->pitch ||
                chain[verified]->lpGbl->fpVidMem != expected) {
                ++v9x_hal->d3d_diagnostics.mip_gap_offset;
                v9x_hal->d3d_diagnostics.mip_gap_expected = expected;
                v9x_hal->d3d_diagnostics.mip_gap_actual =
                    chain[verified]->lpGbl->fpVidMem;
                break;
            }
        }
    } else {
        ++v9x_hal->d3d_diagnostics.mip_gap_offset;
        v9x_hal->d3d_diagnostics.mip_gap_expected = tree->pitch;
        v9x_hal->d3d_diagnostics.mip_gap_actual =
            (DWORD)top->lpGbl->lPitch;
    }
    if (verified < count) {
        ++v9x_hal->d3d_diagnostics.mip_chain_gaps;
        if (verified <= 1ul ||
            v9x_d3d_i9xx_layout_miptree(width, height, verified, tree) ==
                V9X_FALSE) {
            return 1ul;
        }
    }
    v9x_hal->d3d_diagnostics.mip_chain_levels = verified - 1ul;
    if (verified - 1ul > v9x_hal->d3d_diagnostics.mip_levels_max) {
        v9x_hal->d3d_diagnostics.mip_levels_max = verified - 1ul;
    }
    return verified;
}

static int v9x_d3d_i9xx_bind_texture(const V9X_R3D_DRAW *draw,
                                     struct v9x_i9xx_texture *map,
                                     DWORD *bytes_out)
{
    V9X_DD_SURFACE_LCL *surface = (V9X_DD_SURFACE_LCL *)draw->texture.object;
    struct v9x_d3d_i9xx_miptree tree;
    DWORD format = 0ul;
    DWORD offset;
    DWORD address = 0ul;
    DWORD levels;
    DWORD mip_filter = V9X_I9XX_MIPFILTER_NONE;
    DWORD mag_mip = V9X_I9XX_MIPFILTER_NONE;
    DWORD map_rows;
    DWORD footprint_width;

    *bytes_out = 0ul;

    if (surface == 0 || surface->lpGbl == 0) {
        return 0;
    }
    if ((surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
        ++v9x_hal->d3d_diagnostics.texture_refused_other;
        if ((surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
            ++v9x_hal->d3d_diagnostics.texture_refused_sysmem;
        } else {
            ++v9x_hal->d3d_diagnostics.texture_refused_nocap;
        }
        v9x_hal->d3d_diagnostics.texture_refused_caps = surface->ddsCaps;
        v9x_hal->d3d_diagnostics.texture_refused_vidmem =
            surface->lpGbl->fpVidMem;
        return 0;
    }
    if (!v9x_d3d_i9xx_texture_format(surface, &format)) {
        return 0;
    }
    /*
     * Powers of two within the limits, square or not
     * (v9x_d3d_i9xx_texture_shape): MAP_STATE holds width and height
     * separately. Until 2026-09-26 this refused non-square maps as the
     * sampler's rule; the Direct3D caps still say SQUAREONLY, so Direct3D
     * applications are not offered them, and the OpenGL ICD is.
     */
    if (v9x_d3d_i9xx_texture_shape((DWORD)surface->lpGbl->wWidth,
                                   (DWORD)surface->lpGbl->wHeight,
                                   V9X_D3D_I9XX_SAMPLER_SIZE_MIN,
                                   v9x_d3d_i9xx_limits.texture_size_max) ==
            V9X_FALSE) {
        ++v9x_hal->d3d_diagnostics.texture_refused_shape;
        v9x_hal->d3d_diagnostics.texture_refused_last =
            ((DWORD)surface->lpGbl->wWidth << 16) |
            ((DWORD)surface->lpGbl->lPitch & 0xfffful);
        return 0;
    }
    offset = v9x_surface_offset(surface);
    if (offset == 0xfffffffful) {
        ++v9x_hal->d3d_diagnostics.texture_refused_other;
        ++v9x_hal->d3d_diagnostics.texture_refused_bounds;
        v9x_hal->d3d_diagnostics.texture_refused_caps = surface->ddsCaps;
        v9x_hal->d3d_diagnostics.texture_refused_vidmem =
            surface->lpGbl->fpVidMem;
        return 0;
    }
    /*
     * The chain, if there is one where the sampler will look. With levels,
     * the footprint is the whole tree - every row of it and the full pitch
     * across, since level 2's column can reach past level 0's width - rather
     * than the top level alone.
     */
    levels = v9x_d3d_i9xx_mip_levels(surface, &tree);
    map_rows = (DWORD)surface->lpGbl->wHeight;
    footprint_width = (DWORD)surface->lpGbl->wWidth;
    if (levels > 1ul) {
        map_rows = tree.rows;
        footprint_width = tree.pitch / 2ul;
    }
    /*
     * The footprint, against the aperture. THIS is the memory-safety check
     * for a sampled surface: the decoder compares the stream against what
     * this produced, which cannot catch an engine that was wrong about the
     * surface, and this is what makes sure it was not.
     */
    if (v9x_d3d_i9xx_bind_map(offset, (DWORD)surface->lpGbl->lPitch,
                              footprint_width, map_rows,
                              v9x_hal->fb.vram_bytes,
                              &address) == V9X_FALSE) {
        ++v9x_hal->d3d_diagnostics.texture_refused_other;
        ++v9x_hal->d3d_diagnostics.texture_refused_bounds;
        v9x_hal->d3d_diagnostics.texture_refused_caps = surface->ddsCaps;
        v9x_hal->d3d_diagnostics.texture_refused_vidmem =
            surface->lpGbl->fpVidMem;
        return 0;
    }
    map->offset = address;
    map->width = (DWORD)surface->lpGbl->wWidth;
    map->height = (DWORD)surface->lpGbl->wHeight;
    map->pitch = (DWORD)surface->lpGbl->lPitch;
    map->format = format;
    /*
     * The sampler, from the render states the core kept. WRAP tiles, MIRROR
     * reflects on every repeat (from 2026-09-25), and CLAMP and anything else
     * - BORDER, which this driver does not publish - clamp to the edge. MIN
     * and MAG are read separately, because Direct3D sets them separately and
     * the SS2 word has a field for each; v9x_d3d_i9xx_filter says which
     * D3DFILTER values are bilinear within a level. MIN's choice between
     * levels becomes the mip filter only when the chain has levels, so a map
     * without them keeps the SS2 word it always had. MAG's has no meaning -
     * magnification is level 0 - and is dropped.
     */
    if (draw->texture.address == V9X_R3D_ADDRESS_WRAP) {
        map->wrap = V9X_I9XX_ADDRESS_WRAP;
    } else if (draw->texture.address == V9X_R3D_ADDRESS_MIRROR) {
        map->wrap = V9X_I9XX_ADDRESS_MIRROR;
    } else {
        map->wrap = V9X_I9XX_ADDRESS_CLAMP;
    }
    v9x_d3d_i9xx_filter(draw->texture.mag_filter, &map->mag_linear, &mag_mip);
    v9x_d3d_i9xx_filter(draw->texture.min_filter, &map->min_linear, &mip_filter);
    map->mip_filter = V9X_I9XX_MIPFILTER_NONE;
    map->max_lod = 0ul;
    if (levels > 1ul) {
        map->mip_filter = mip_filter;
        map->max_lod = levels - 1ul;
    }
    *bytes_out = map_rows * map->pitch;
    v9x_hal->d3d_diagnostics.texture_last_offset = address;
    v9x_hal->d3d_diagnostics.texture_last_size = map->width;
    v9x_hal->d3d_diagnostics.texture_last_caps = surface->ddsCaps;
    return 1;
}

/*
 * The bound depth buffer, or nothing.
 *
 * Zero when the application asked for no depth test, and zero WITH A COUNT
 * when it asked for one this engine cannot express. S6 carries a single
 * comparison function and this build emits LESS; an application asking for
 * GREATER would otherwise have its draws silently depth-tested the wrong way,
 * which is a wrong picture that looks like a plausible one.
 */
/*
 * Whether this draw blends, and with which of the ONE pair this engine has.
 *
 * The ViRGE's rule, reused because it is the right one: SRC_ALPHA over
 * INV_SRC_ALPHA is the measured pair and blends; ONE over ZERO is an opaque
 * draw whether or not the enable is set; anything else is drawn opaque and
 * COUNTED with the pair, so a capture says what the application wanted
 * rather than showing a wrong picture with every HRESULT reporting success.
 * Direct3D's default with ALPHABLENDENABLE set is ONE/ZERO, which is why that
 * pair has to mean "off" rather than "unsupported".
 */
static DWORD v9x_d3d_i9xx_blend_factor(DWORD d3d)
{
    if (d3d == V9X_R3D_BLEND_ZERO) { return V9X_I9XX_BLENDFACT_ZERO; }
    if (d3d == V9X_R3D_BLEND_ONE) { return V9X_I9XX_BLENDFACT_ONE; }
    if (d3d == V9X_R3D_BLEND_SRCALPHA) { return V9X_I9XX_BLENDFACT_SRC_ALPHA; }
    if (d3d == V9X_R3D_BLEND_INVSRCALPHA) {
        return V9X_I9XX_BLENDFACT_INV_SRC_ALPHA;
    }
    /* The colour factors, from 2026-09-25. */
    if (d3d == V9X_R3D_BLEND_SRCCOLOR) { return V9X_I9XX_BLENDFACT_SRC_COLR; }
    if (d3d == V9X_R3D_BLEND_INVSRCCOLOR) {
        return V9X_I9XX_BLENDFACT_INV_SRC_COLR;
    }
    if (d3d == V9X_R3D_BLEND_DESTCOLOR) { return V9X_I9XX_BLENDFACT_DST_COLR; }
    if (d3d == V9X_R3D_BLEND_INVDESTCOLOR) {
        return V9X_I9XX_BLENDFACT_INV_DST_COLR;
    }
    return 0ul;
}

/*
 * The factor pair as S6 codes, or both zero for an opaque draw. The source
 * and destination caps are published as independently selectable, which is
 * what the capability definitions mean, so every pairing of the four
 * published factors is honoured - SRCALPHA/ZERO and ONE/INVSRCALPHA are
 * legal requests and get their fields. ONE/ZERO is opaque by arithmetic and
 * is passed as off, because it is Direct3D's default with the enable set.
 * A factor outside the four draws opaque and is counted with the pair.
 */
static void v9x_d3d_i9xx_bind_blend(const V9X_R3D_DRAW *draw,
                                    DWORD *src_out, DWORD *dst_out)
{
    DWORD src;
    DWORD dst;

    *src_out = 0ul;
    *dst_out = 0ul;
    if (draw->blend_enable == 0ul) {
        return;
    }
    if (draw->src_blend == V9X_R3D_BLEND_ONE &&
        draw->dst_blend == V9X_R3D_BLEND_ZERO) {
        return;
    }
    src = v9x_d3d_i9xx_blend_factor(draw->src_blend);
    dst = v9x_d3d_i9xx_blend_factor(draw->dst_blend);
    if (src == 0ul || dst == 0ul) {
        ++v9x_hal->d3d_diagnostics.blend_skipped;
        v9x_hal->d3d_diagnostics.blend_last_pair =
            (draw->src_blend << 16) | (draw->dst_blend & 0xfffful);
        return;
    }
    *src_out = src;
    *dst_out = dst;
}

/*
 * Which textured program: MODULATEALPHA multiplies alpha; legacy MODULATE
 * takes the texture's alpha when the format has one and the vertex's when
 * it does not. Anything else the application sets is drawn as MODULATE,
 * the one operation published besides MODULATEALPHA.
 */
static DWORD v9x_d3d_i9xx_texture_program(const V9X_R3D_DRAW *draw,
                                          DWORD format)
{
    if (draw->texture.op == V9X_R3D_TEXOP_MODULATEALPHA) {
        return V9X_I9XX_TEXPROG_MODULATE_ALPHA;
    }
    /* DECAL, from 2026-09-25: the texel alone, the vertex colour unused. */
    if (draw->texture.op == V9X_R3D_TEXOP_DECAL) {
        return V9X_I9XX_TEXPROG_DECAL;
    }
    if (format == V9X_I9XX_MAPSURF_16BIT_ARGB1555 ||
        format == V9X_I9XX_MAPSURF_16BIT_ARGB4444) {
        return V9X_I9XX_TEXPROG_MODULATE_TEXALPHA;
    }
    return V9X_I9XX_TEXPROG_MODULATE_DIFFALPHA;
}

static int v9x_d3d_i9xx_bind_depth_surface(const V9X_R3D_DRAW *draw,
                                           DWORD *offset_out,
                                           DWORD *pitch_out,
                                           DWORD *writes_out,
                                           DWORD *compare_out)
{
    DWORD address = 0ul;
    DWORD compare = 0ul;

    *offset_out = 0ul;
    *pitch_out = 0ul;
    *writes_out = 0ul;
    /*
     * LESS while nothing is bound, so a caller that ignores the return
     * value cannot emit a comparison that was never resolved. It is the
     * value this engine emitted unconditionally until 2026-09-20.
     */
    *compare_out = V9X_I9XX_COMPAREFUNC_LESS;

    if (draw->depth.offset == 0ul || draw->depth_enable == 0ul) {
        return 0;
    }
    /*
     * Any comparison the part implements, which is all eight.
     *
     * This accepted D3DCMP_LESS alone and skipped the depth test for
     * everything else. intel98 measured the cost: 3DMark99 asks for
     * LESSEQUAL, so 192,069 of 224,838 draws - eighty-five per cent - went
     * to the ring with no depth test, and the netbook photograph shows one
     * correctly textured object against an empty scene, because geometry
     * without a depth test paints in submission order and what is drawn
     * late covers what came before.
     *
     * A function outside D3DCMP's range is still skipped rather than
     * guessed at, and still recorded, because the alternative is emitting
     * some other comparison into a scene that asked for one this driver did
     * not recognise.
     */
    if (v9x_i9xx_depth_func(draw->depth_func, &compare) == V9X_FALSE) {
        ++v9x_hal->d3d_diagnostics.i9xx_depth_skipped;
        v9x_hal->d3d_diagnostics.i9xx_depth_last_func = draw->depth_func;
        return 0;
    }
    if (v9x_d3d_i9xx_bind_depth(draw->depth.offset, draw->depth.pitch,
                                draw->target.width, draw->target.height,
                                v9x_hal->fb.vram_bytes,
                                &address) == V9X_FALSE) {
        ++v9x_hal->d3d_diagnostics.i9xx_depth_skipped;
        return 0;
    }
    *offset_out = address;
    *pitch_out = draw->depth.pitch;
    *compare_out = compare;
    *writes_out = draw->depth_write != 0ul ? 1ul : 0ul;
    return 1;
}

/*
 * What this engine will accept, and NOTHING it has not been built to do.
 *
 * Deliberately far narrower than the ViRGE's, which is a mature engine with
 * measured behaviour behind every bit. This one has executed no application
 * geometry at all, so what it claims is exactly what the code path supports:
 * flat and Gouraud RGB triangles into a 16-bit target, from transformed and
 * lit vertices, and nothing else.
 *
 * WHAT IS CLAIMED AND WHY, as of 2026-09-16:
 *
 *  - THREE texture formats, RGB565, ARGB1555 and ARGB4444, square and a
 *    power of two from 8 to 256. The runtime path builds MAP_STATE and
 *    SAMPLER_STATE from the application's surface and the modulate program
 *    samples it. The 565 path is measured (intel45, intel46); the two alpha
 *    types are the same packet with a different type code, from the
 *    reference trees, and are UNMEASURED until a capture samples one. What is
 *    generalised is the size range, which the limits comment above states
 *    plainly.
 *  - A 16-bit Z BUFFER with the LESS comparison and optional writes. One
 *    comparison, because S6 carries one and this build emits LESS - an
 *    application asking for another gets an un-Z'd draw and a count, which is
 *    why dwZCmpCaps says LESS alone rather than letting the runtime assume.
 *  - MODULATE texture blending only, which is what the fragment program
 *    computes. DECAL and the rest are absent because nothing builds them.
 *
 *  - ONE colour blend, SRC_ALPHA over INV_SRC_ALPHA added, which is the pair
 *    intel47 measured to the exact product, plus ONE/ZERO which is no blend
 *    at all. The runtime S6 carries it, with the IAB disable in front, when
 *    the application enables blending with those factors; any other pair
 *    draws opaque and is counted in blend_skipped with the pair recorded.
 *    Publishing the two factors each side is what tells an application which
 *    pair to ask for. From 2026-09-18, and UNMEASURED on the runtime path -
 *    the scene measured the packet, not this builder's use of it.
 *  - Texture ALPHA: the modulate program multiplies all four channels, so
 *    a 1555 or 4444 texel's alpha times the vertex alpha is what the blend
 *    sees. That matches MODULATE's alpha for an opaque vertex, which is the
 *    common case; the general case is a prediction of the program, not a
 *    measurement.
 *
 * STILL DELIBERATELY ABSENT:
 *
 *  - No fog, no lines, no specular, no colour key, no alpha test on the
 *    runtime path.
 *  - No anisotropy.
 *
 * The distinction this function has to get right is unchanged: a capability
 * is a promise about what an application can do, not a summary of what the
 * hardware has been seen to do. What moved is that the runtime path now
 * builds these, not that more has been measured.
 */
static void v9x_d3d_i9xx_describe_caps(V9X_DD_SHARED *shared)
{
    if (shared == 0) {
        return;
    }
    shared->d3d_global.dwSize = sizeof(V9X_D3DHAL_GLOBALDRIVERDATA);
    shared->d3d_global.hwCaps.dwSize = sizeof(V9X_D3DDEVICEDESC_V1);
    shared->d3d_global.hwCaps.dwFlags =
        V9X_D3DDD_COLORMODEL | V9X_D3DDD_DEVCAPS |
        V9X_D3DDD_TRICAPS | V9X_D3DDD_DEVICERENDERBITDEPTH;
    shared->d3d_global.hwCaps.dcmColorModel = V9X_D3DCOLOR_RGB;
    /*
     * EXECUTESYSTEMMEMORY is here for the reason the ViRGE's comment records:
     * a DirectX 2/3-era title renders only through execute buffers and
     * discards a HAL that does not claim it. The runtime decomposes them into
     * the RenderPrimitive calls this engine actually serves.
     */
    /*
     * TEXTUREVIDEOMEMORY: "the device can texture from device memory". This
     * engine always could, and not saying so is why Final Reality drew
     * untextured in intel56 and intel59 with every format accepted. An
     * application's ALLOCONLOAD texture is given its memory by the runtime
     * at Load, and the runtime places it where the device says it can
     * texture from: with neither TEXTURE*MEMORY bit claimed it chose system
     * memory, and the bind then refused every one of them - 1,118,317 draws
     * in intel59 refused for DDSCAPS_SYSTEMMEMORY against a heap with 5.8 MB
     * free, while the probe's explicitly video-memory texture drew. The
     * ViRGE claims this bit and the same game textures there.
     *
     * TEXTURESYSTEMMEMORY stays absent: the bind refuses system memory and a
     * claim it could sample it would be the advertise-then-ignore pattern.
     */
    shared->d3d_global.hwCaps.dwDevCaps =
        V9X_D3DDEVCAPS_FLOATTLVERTEX |
        V9X_D3DDEVCAPS_EXECUTESYSTEMMEMORY |
        V9X_D3DDEVCAPS_TLVERTEXSYSTEMMEMORY |
        V9X_D3DDEVCAPS_TEXTUREVIDEOMEMORY |
        V9X_D3DDEVCAPS_DRAWPRIMTLVERTEX;
    shared->d3d_global.hwCaps.dtcTransformCaps.dwSize =
        sizeof(V9X_D3DTRANSFORMCAPS);
    shared->d3d_global.hwCaps.dlcLightingCaps.dwSize =
        sizeof(V9X_D3DLIGHTINGCAPS);
    shared->d3d_global.hwCaps.dpcLineCaps.dwSize = sizeof(V9X_D3DPRIMCAPS);
    shared->d3d_global.hwCaps.dpcTriCaps.dwSize = sizeof(V9X_D3DPRIMCAPS);
    /*
     * All three cull modes, from 2026-09-25, and still with S4_CULLMODE_NONE.
     *
     * The hardware culls nothing: the runtime state block programs NONE and
     * the decoder holds it there. CW and CCW are done by the core, which
     * drops back faces before this engine sees them and does so only because
     * these two bits are set (r3d3d_cull.h, v9x_r3d_cull_honoured).
     *
     * CULLNONE alone was honest and cost everything: 3D WinBench 98 renders
     * every scene with CCW culling and refused all 41 quality tests before
     * creating a device, "unsupported settings: CCW Culling"
     * (docs\decisions\2026-09-24-3d-winbench-98-quality-on-the-netbook.md).
     */
    shared->d3d_global.hwCaps.dpcTriCaps.dwMiscCaps =
        V9X_D3DPMISCCAPS_CULLNONE | V9X_D3DPMISCCAPS_CULLCW |
        V9X_D3DPMISCCAPS_CULLCCW;
    shared->d3d_global.hwCaps.dpcTriCaps.dwRasterCaps =
        V9X_D3DPRASTERCAPS_SUBPIXEL | V9X_D3DPRASTERCAPS_ZTEST;
    /*
     * ALPHAGOURAUDBLEND says the device can blend with an alpha interpolated
     * from the vertices, which is what the blend path does: the fragment
     * program reads the interpolated diffuse and the blend takes its alpha.
     * It is one of the bits DirectX 5 and 6 titles test before enabling
     * blending, and its absence is a candidate for why 3DMark99 asked for no
     * texture in intel63 and intel64. Claimed because the blend it describes
     * is built and, for the measured pair, drawn (intel64: 739,862 textured
     * draws with blending on, none skipped).
     *
     * ALPHAFLATBLEND and COLORFLATRGB are true from 2026-09-18 because the
     * CORE does flat shading: under D3DSHADE_FLAT it copies the first
     * vertex's colour, alpha and specular to the other two before any
     * engine sees the triangle, so the Gouraud interpolator produces the
     * flat result and no provoking-vertex register has to be programmed or
     * measured. Both were claimed before that without being true; the
     * flat-shading issue records the interval.
     */
    shared->d3d_global.hwCaps.dpcTriCaps.dwShadeCaps =
        V9X_D3DPSHADECAPS_COLORFLATRGB |
        V9X_D3DPSHADECAPS_COLORGOURAUDRGB |
        V9X_D3DPSHADECAPS_ALPHAFLATBLEND |
        V9X_D3DPSHADECAPS_ALPHAGOURAUDBLEND;
    /*
     * LESS alone, and that is the point of publishing it rather than leaving
     * the field zero: the runtime asks what comparisons exist, and an engine
     * that claims none while accepting a Z buffer is the shape that let
     * intel52's depth test silently do nothing.
     */
    /*
     * All eight, because the part implements all eight and the engine now
     * emits what it is asked for.
     *
     * LESS alone was published and LESS alone was emitted, so an
     * application asking for anything else lost its depth test silently:
     * intel98 measured 3DMark99 asking for LESSEQUAL and 192,069 of 224,838
     * draws going to the ring untested, with the netbook photograph showing
     * one correctly textured object against an empty scene.
     *
     * Publishing the full set is not the advertise-then-ignore pattern this
     * file warns about elsewhere; the mapping is in i9xx_depth.c and every
     * arm of it is host-tested against Mesa's table.
     */
    shared->d3d_global.hwCaps.dpcTriCaps.dwZCmpCaps =
        V9X_D3DPCMPCAPS_NEVER | V9X_D3DPCMPCAPS_LESS |
        V9X_D3DPCMPCAPS_EQUAL | V9X_D3DPCMPCAPS_LESSEQUAL |
        V9X_D3DPCMPCAPS_GREATER | V9X_D3DPCMPCAPS_NOTEQUAL |
        V9X_D3DPCMPCAPS_GREATEREQUAL | V9X_D3DPCMPCAPS_ALWAYS;
    /*
     * Every factor the engine builds, on both sides, from 2026-09-25.
     *
     * This published SRCALPHA|ONE as source and INVSRCALPHA|ZERO as
     * destination - the one measured pair and the one that means "off" -
     * while bind_blend already honoured every pairing of the four codes. So
     * ONE/ONE was buildable and never offered, and 3D WinBench 98 reported
     * Add Pixel Blending unsupported. The colour factors join them; the
     * destination-alpha factors stay out, because a 565 target has no alpha.
     */
    shared->d3d_global.hwCaps.dpcTriCaps.dwSrcBlendCaps =
        V9X_D3DPBLENDCAPS_ZERO | V9X_D3DPBLENDCAPS_ONE |
        V9X_D3DPBLENDCAPS_SRCCOLOR | V9X_D3DPBLENDCAPS_INVSRCCOLOR |
        V9X_D3DPBLENDCAPS_SRCALPHA | V9X_D3DPBLENDCAPS_INVSRCALPHA |
        V9X_D3DPBLENDCAPS_DESTCOLOR | V9X_D3DPBLENDCAPS_INVDESTCOLOR;
    shared->d3d_global.hwCaps.dpcTriCaps.dwDestBlendCaps =
        shared->d3d_global.hwCaps.dpcTriCaps.dwSrcBlendCaps;
    shared->d3d_global.hwCaps.dpcTriCaps.dwTextureCaps =
        V9X_D3DPTEXTURECAPS_PERSPECTIVE | V9X_D3DPTEXTURECAPS_POW2 |
        V9X_D3DPTEXTURECAPS_SQUAREONLY | V9X_D3DPTEXTURECAPS_ALPHA;
    /*
     * NEAREST and LINEAR, WRAP and CLAMP: the two filter values and the two
     * address modes the sampler state now carries from the render states.
     * intel62's photograph of Final Reality showed the cost of publishing
     * clamp and nearest alone - tiled sky and terrain smeared into edge
     * texels, and an aliased floor - while the application had asked for
     * wrap and a linear filter and was given neither.
     *
     * The four mip filters from 2026-09-25, because a chain now has levels
     * the sampler can reach: CreateSurface lays it out where Gen3 reads it,
     * and SS2 and MS4 carry the filter and the level count. A chain placed
     * any other way is sampled at its top level and counted, which is the
     * honest degradation rather than a claim ignored.
     */
    shared->d3d_global.hwCaps.dpcTriCaps.dwTextureFilterCaps =
        V9X_D3DPTFILTERCAPS_NEAREST | V9X_D3DPTFILTERCAPS_LINEAR |
        V9X_D3DPTFILTERCAPS_MIPNEAREST | V9X_D3DPTFILTERCAPS_MIPLINEAR |
        V9X_D3DPTFILTERCAPS_LINEARMIPNEAREST |
        V9X_D3DPTFILTERCAPS_LINEARMIPLINEAR;
    /* MODULATE with Direct3D's alpha rule, MODULATEALPHA, which is the
     * original program, and from 2026-09-25 DECAL, the sampling program.
     * DECALALPHA needs a lerp program this engine does not build yet. */
    shared->d3d_global.hwCaps.dpcTriCaps.dwTextureBlendCaps =
        V9X_D3DPTBLENDCAPS_DECAL | V9X_D3DPTBLENDCAPS_MODULATE |
        V9X_D3DPTBLENDCAPS_MODULATEALPHA;
    /* MIRROR from 2026-09-25: the sampler's TEXCOORDMODE_MIRROR, one value
     * in the SS3 address fields the other two modes already use. */
    shared->d3d_global.hwCaps.dpcTriCaps.dwTextureAddressCaps =
        V9X_D3DPTADDRESSCAPS_WRAP | V9X_D3DPTADDRESSCAPS_MIRROR |
        V9X_D3DPTADDRESSCAPS_CLAMP;
    /*
     * GUID_D3DExtendedCaps, which the runtime asks for and which carries the
     * texture limits D3DDEVICEDESC_V1 has nowhere to put. These are the same
     * numbers the bind enforces, so what is published and what is accepted
     * are one statement rather than two that can drift.
     *
     * No stipple: this driver has no stippled-fill path, and zero is what a
     * device without one reports.
     */
    shared->d3d_extended_caps.dwSize =
        sizeof(V9X_D3DHAL_D3DEXTENDEDCAPS);
    shared->d3d_extended_caps.dwMinTextureWidth = v9x_d3d_i9xx_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureWidth = v9x_d3d_i9xx_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinTextureHeight = v9x_d3d_i9xx_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureHeight = v9x_d3d_i9xx_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMaxStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMinStippleHeight = 0ul;
    shared->d3d_extended_caps.dwMaxStippleHeight = 0ul;

    shared->d3d_global.hwCaps.dwDeviceRenderBitDepth = V9X_DDBD_16;
    shared->d3d_global.hwCaps.dwDeviceZBufferBitDepth = V9X_DDBD_16;
    /*
     * The three formats MAP_STATE can name at 16 bits: RGB565, the measured
     * one, and ARGB1555 and ARGB4444, which the same field selects and the
     * same program samples. Published exactly as v9x_d3d_i9xx_texture_format
     * classifies them, so nothing an application allocates from this list is
     * refused at bind - an accepted texture and an untextured draw was what
     * intel56 measured with 565 alone: Final Reality's 4:4:4:4 textures were
     * refused at creation, fell back to system memory, and 1.9 million draws
     * went untextured.
     *
     * ALPHAPIXELS only on the two that carry one. A 565 texel has none, and
     * claiming alpha over it is how an application comes to ask for blending
     * that cannot work. The alpha the two formats carry reaches the program
     * as the texel's A, and from 2026-09-18 the runtime S6 can blend on it.
     *
     * The two alpha formats are UNMEASURED on this part.
     */
    shared->texture_formats[0].dwSize = sizeof(V9X_DDSURFACEDESC);
    shared->texture_formats[0].dwFlags =
        V9X_DDSD_CAPS | V9X_DDSD_PIXELFORMAT;
    shared->texture_formats[0].ddpfPixelFormat.dwSize =
        sizeof(V9X_DDPIXELFORMAT);
    shared->texture_formats[0].ddpfPixelFormat.dwFlags = V9X_DDPF_RGB;
    shared->texture_formats[0].ddpfPixelFormat.dwRGBBitCount = 16ul;
    shared->texture_formats[0].ddpfPixelFormat.dwRBitMask = 0x0000f800ul;
    shared->texture_formats[0].ddpfPixelFormat.dwGBitMask = 0x000007e0ul;
    shared->texture_formats[0].ddpfPixelFormat.dwBBitMask = 0x0000001ful;
    shared->texture_formats[0].ddpfPixelFormat.dwRGBAlphaBitMask = 0ul;
    shared->texture_formats[0].ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;

    shared->texture_formats[1].dwSize = sizeof(V9X_DDSURFACEDESC);
    shared->texture_formats[1].dwFlags =
        V9X_DDSD_CAPS | V9X_DDSD_PIXELFORMAT;
    shared->texture_formats[1].ddpfPixelFormat.dwSize =
        sizeof(V9X_DDPIXELFORMAT);
    shared->texture_formats[1].ddpfPixelFormat.dwFlags =
        V9X_DDPF_RGB | V9X_DDPF_ALPHAPIXELS;
    shared->texture_formats[1].ddpfPixelFormat.dwRGBBitCount = 16ul;
    shared->texture_formats[1].ddpfPixelFormat.dwRBitMask = 0x00007c00ul;
    shared->texture_formats[1].ddpfPixelFormat.dwGBitMask = 0x000003e0ul;
    shared->texture_formats[1].ddpfPixelFormat.dwBBitMask = 0x0000001ful;
    shared->texture_formats[1].ddpfPixelFormat.dwRGBAlphaBitMask =
        0x00008000ul;
    shared->texture_formats[1].ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;

    shared->texture_formats[2].dwSize = sizeof(V9X_DDSURFACEDESC);
    shared->texture_formats[2].dwFlags =
        V9X_DDSD_CAPS | V9X_DDSD_PIXELFORMAT;
    shared->texture_formats[2].ddpfPixelFormat.dwSize =
        sizeof(V9X_DDPIXELFORMAT);
    shared->texture_formats[2].ddpfPixelFormat.dwFlags =
        V9X_DDPF_RGB | V9X_DDPF_ALPHAPIXELS;
    shared->texture_formats[2].ddpfPixelFormat.dwRGBBitCount = 16ul;
    shared->texture_formats[2].ddpfPixelFormat.dwRBitMask = 0x00000f00ul;
    shared->texture_formats[2].ddpfPixelFormat.dwGBitMask = 0x000000f0ul;
    shared->texture_formats[2].ddpfPixelFormat.dwBBitMask = 0x0000000ful;
    shared->texture_formats[2].ddpfPixelFormat.dwRGBAlphaBitMask =
        0x0000f000ul;
    shared->texture_formats[2].ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;

    shared->d3d_global.lpTextureFormats = &shared->texture_formats[0];
    shared->d3d_global.dwNumTextureFormats = 3ul;
    shared->d3d_global.hwCaps.dwFlags |= V9X_D3DDD_DEVICEZBUFFERBITDEPTH;
    shared->d3d_global.dwNumVertices = 0ul;
    shared->d3d_global.dwNumClipVertices = 0ul;
}

/*
 * One batch: build the stream, check it, submit it, wait for it.
 *
 * UNRUN. Every piece below is host-tested and no guest has executed one of
 * these streams; what the tests establish is that the bytes are the ones the
 * decoder accepts, not that the part draws them.
 */
/*
 * The batch's scratch, at file scope and NOT on the stack.
 *
 * As locals these four were 11,520 bytes, and wdis on d3d_i9xx.obj showed
 * the prologue as `sub esp,0x2d7c` with the first writes just under ebp and
 * the first push at the bottom of the frame. The HAL is compiled -s, so
 * there is no stack probe between the two: a frame that spans three pages
 * skips the thread's guard page whenever fewer than three pages below the
 * caller's esp are committed, and the push then lands on reserved memory.
 * That is an access violation before the first line of C - before any
 * counter here could say the function was entered.
 *
 * It is a HYPOTHESIS for intel53-55, where RenderPrimitive entered once
 * per boot, never exited, never incremented its unconditional counter and
 * left every engine counter at zero (docs\issues\2026-09-16-final-reality-
 * renders-black-and-the-hal-faults.md). The frame is a defect regardless of
 * whether it is that fault, which is why it moves without waiting.
 *
 * Statics are safe here for the reason the context and texture tables are:
 * DirectDraw serialises HAL calls under the Win16 lock, so one draw is
 * building a stream at a time.
 */
static DWORD v9x_d3d_i9xx_stream[V9X_I9XX_SUBMIT_DWORDS];
static DWORD v9x_d3d_i9xx_xyzw[V9X_I9XX_SUBMIT_VERTICES * 4ul];
static DWORD v9x_d3d_i9xx_uv[V9X_I9XX_SUBMIT_VERTICES * 2ul];
static DWORD v9x_d3d_i9xx_colors[V9X_I9XX_SUBMIT_VERTICES];

static int v9x_d3d_i9xx_draw_triangles_body(const V9X_R3D_DRAW *draw,
                                            const V9X_D3DTLVERTEX *vertices,
                                            DWORD triangle_count);

/*
 * One submitted batch's head wait, filed by the batch's shape.
 *
 * The question is whether the GPU's time per batch is a fixed cost - the
 * flushes and the state reload every submission carries - or grows with
 * triangles or with pixels, and a single total cannot say. The area is the
 * sum of the triangles' screen areas from the vertices the engine was given,
 * so overdraw counts and depth-rejected pixels count too; it is a size, not
 * a fill measurement. Thresholds are powers of ten and five around a
 * 1024x576 frame of 590K pixels.
 */
#define V9X_D3D_I9XX_BATCH_CLASSES 6ul

static void v9x_d3d_i9xx_note_batch(const V9X_D3DTLVERTEX *vertices,
                                    DWORD triangle_count, DWORD cycles)
{
    float area = 0.0f;
    DWORD triangle;
    DWORD edge;
    DWORD count_class = 0ul;
    DWORD area_class = 0ul;
    DWORD *cell;

    for (triangle = 0ul; triangle < triangle_count; ++triangle) {
        const V9X_D3DTLVERTEX *v = &vertices[triangle * 3ul];
        float twice = (v[1].sx - v[0].sx) * (v[2].sy - v[0].sy) -
                      (v[2].sx - v[0].sx) * (v[1].sy - v[0].sy);

        if (twice < 0.0f) {
            twice = -twice;
        }
        area += twice * 0.5f;
    }
    for (edge = triangle_count;
         edge > 1ul && count_class < V9X_D3D_I9XX_BATCH_CLASSES - 1ul;
         edge >>= 1) {
        ++count_class;
    }
    if (area >= 1000.0f) { area_class = 1ul; }
    if (area >= 10000.0f) { area_class = 2ul; }
    if (area >= 50000.0f) { area_class = 3ul; }
    if (area >= 200000.0f) { area_class = 4ul; }
    if (area >= 1000000.0f) { area_class = 5ul; }

    cell = &v9x_hal->d3d_diagnostics.batch_cells[
        ((count_class * V9X_D3D_I9XX_BATCH_CLASSES) + area_class) * 3ul];
    cell[0] += cycles;
    if (cell[0] < cycles) {
        ++cell[1];
    }
    ++cell[2];
}

/* The engine's whole draw, timed; the work is in the body. */
static int v9x_d3d_i9xx_draw(const V9X_R3D_DRAW *draw,
                             const V9X_R3D_VERTEX *r3d_vertices,
                             DWORD triangle_count)
{
    /* The same layout, asserted in d3d_core.c; the stream builders below
     * are written on the D3DTLVERTEX field names and stay so. */
    const V9X_D3DTLVERTEX *vertices = (const V9X_D3DTLVERTEX *)r3d_vertices;
    DWORD started = V9X_TIME_BEGIN();
    int ok;

    v9x_d3d_i9xx_last_head_cycles = 0ul;
    ok = v9x_d3d_i9xx_draw_triangles_body(draw, vertices, triangle_count);
    V9X_TIME_END(V9X_TIME_ENGINE_DRAW, started);
    if (ok && V9X_TIME_ENABLED() && v9x_d3d_i9xx_last_head_cycles != 0ul &&
        vertices != 0) {
        v9x_d3d_i9xx_note_batch(vertices, triangle_count,
                                v9x_d3d_i9xx_last_head_cycles);
    }
    return ok;
}

static int v9x_d3d_i9xx_draw_triangles_body(const V9X_R3D_DRAW *draw,
                                            const V9X_D3DTLVERTEX *vertices,
                                            DWORD triangle_count)
{
    struct v9x_i9xx_decode_limits limits;
    struct v9x_i9xx_texture map;
    DWORD *stream = v9x_d3d_i9xx_stream;
    DWORD *xyzw = v9x_d3d_i9xx_xyzw;
    DWORD *uv = v9x_d3d_i9xx_uv;
    DWORD *colors = v9x_d3d_i9xx_colors;
    DWORD identity = 0ul;
    DWORD address = 0ul;
    DWORD at = 0ul;
    DWORD produced = 0ul;
    DWORD rejected = 0ul;
    DWORD vertex;
    DWORD count;
    int textured;
    int depthed;
    DWORD blend_src;
    DWORD blend_dst;
    DWORD program = V9X_I9XX_TEXPROG_MODULATE_ALPHA;
    DWORD depth_offset = 0ul;
    DWORD depth_pitch = 0ul;
    DWORD depth_writes = 0ul;
    DWORD depth_compare = V9X_I9XX_COMPAREFUNC_LESS;
    DWORD cylinder = 0ul;
    DWORD map_bytes = 0ul;
    DWORD alpha_test = 0ul;

    if (draw == 0 || vertices == 0 || triangle_count == 0ul) {
        return v9x_d3d_i9xx_refuse(V9X_I9XX_REFUSE_ARGUMENTS);
    }
    /*
     * Both bounds, and they are different numbers for different reasons: the
     * vertex arrays hold 192, and the run builders multiply the triangle
     * count in sixteen bits and refuse anything past their own maximum. The
     * smaller wins and saying so here means a batch that would be refused
     * deeper is refused with a reason instead.
     */
    if (triangle_count > (V9X_I9XX_SUBMIT_VERTICES / 3ul) ||
        triangle_count > V9X_I9XX_RUNTIME_MAX_TRIANGLES) {
        return v9x_d3d_i9xx_refuse(V9X_I9XX_REFUSE_BATCH);
    }
    count = triangle_count * 3ul;

    /*
     * The surface, validated against the aperture BEFORE anything is built.
     * This is the memory-safety check - the decoder below compares the stream
     * against what this produced, which cannot catch an engine that was wrong
     * about the surface, and this is what makes sure it was not.
     */
    if (v9x_d3d_i9xx_bind_target(draw->target.offset, draw->target.pitch,
                                 draw->target.width, draw->target.height,
                                 v9x_hal->fb.vram_bytes,
                                 &identity, &address) == V9X_FALSE) {
        return v9x_d3d_i9xx_refuse(V9X_I9XX_REFUSE_TARGET);
    }
    /*
     * A flip still pending means the panel is still fetching the buffer
     * this batch is aimed at: Flip returned when the base was written, the
     * latch is at the next blank start, and Direct3D does not ask
     * GetFlipStatus before drawing. intel78 saw the construction on screen
     * with the write timed right, which is this gap. Wait for the flip to
     * be taken; the count says how often the wait was needed, and a wait
     * that runs out is a dead scanout, drawn anyway rather than refused
     * (the frame is lost either way; the game keeps running).
     */
    {
        int waited = v9x_flip_wait_done();

        if (waited != V9X_FLIP_WAIT_NONE) {
            ++v9x_hal->d3d_diagnostics.draws_flip_waited;
            if (waited == V9X_FLIP_WAIT_TIMEOUT) {
                ++v9x_hal->d3d_diagnostics.draws_flip_wait_timeouts;
            }
        }
    }
    /* Front or back: is this batch about to land in the buffer the display
     * is scanning right now? See the diagnostics comment (intel74). */
    {
        DWORD displayed = v9x_scanout_displayed_offset();

        /*
         * The set of targets, not just the last one. See the census comment
         * in the ABI header: a last value read after teardown says nothing
         * about where a run's draws went.
         */
        {
            DWORD slot;
            DWORD count = v9x_hal->d3d_diagnostics.draw_target_count;

            for (slot = 0ul; slot < count &&
                             slot < (DWORD)V9X_D3D_TARGET_SLOTS; ++slot) {
                if (v9x_hal->d3d_diagnostics.draw_target_offset[slot] ==
                        draw->target.offset) {
                    break;
                }
            }
            if (slot < (DWORD)V9X_D3D_TARGET_SLOTS) {
                if (slot == count) {
                    v9x_hal->d3d_diagnostics.draw_target_offset[slot] =
                        draw->target.offset;
                    ++v9x_hal->d3d_diagnostics.draw_target_count;
                }
                ++v9x_hal->d3d_diagnostics.draw_target_draws[slot];
            } else {
                ++v9x_hal->d3d_diagnostics.draw_target_other;
            }
        }

        if (displayed != 0xfffffffful) {
            v9x_hal->d3d_diagnostics.draws_target_last =
                draw->target.offset;
            v9x_hal->d3d_diagnostics.draws_displayed_last = displayed;
            v9x_hal->d3d_diagnostics.draws_pitch_last = draw->target.pitch;
            v9x_hal->d3d_diagnostics.draws_extent_last =
                (draw->target.width << 16) | (draw->target.height & 0xfffful);
            if (displayed == draw->target.offset) {
                ++v9x_hal->d3d_diagnostics.draws_to_front;
            } else {
                ++v9x_hal->d3d_diagnostics.draws_to_back;
            }
        }
    }

    /*
     * The other two surfaces. Neither refuses the draw: a texture this engine
     * cannot sample draws untextured and a depth test it cannot express draws
     * un-Z'd, both counted. A hole in the frame is worse than a wrong colour
     * in it, and the counters are what keep the fallback from being silent.
     */
    textured = v9x_d3d_i9xx_bind_texture(draw, &map, &map_bytes);
    depthed = v9x_d3d_i9xx_bind_depth_surface(draw, &depth_offset,
                                              &depth_pitch, &depth_writes,
                                              &depth_compare);
    v9x_d3d_i9xx_bind_blend(draw, &blend_src, &blend_dst);
    /*
     * ALPHATESTENABLE, ALPHAFUNC and ALPHAREF, as S6's alpha test - the
     * three fields the Phase 6 scene measured rejecting by alpha (intel47).
     * Until 2026-09-25 this engine read none of them, and Half-Life's
     * see-through fences drew their transparent texels solid black. A
     * function outside D3DCMP's range draws untested and is counted, as the
     * ViRGE counts every alpha test it cannot draw.
     */
    if (v9x_i9xx_alpha_test_bits(draw->alpha_test_enable,
                                 draw->alpha_func, draw->alpha_ref,
                                 &alpha_test) == V9X_FALSE) {
        alpha_test = 0ul;
        ++v9x_hal->d3d_diagnostics.alpha_test_unexpressed;
    }
    if (textured != 0) {
        program = v9x_d3d_i9xx_texture_program(draw, map.format);
        /*
         * WRAPU / WRAPV, by the hardware: the S3 wrap-shortest bits of
         * coordinate set 0. Only with a texture, because an untextured draw
         * has no coordinate to wrap and the builder refuses the request.
         *
         * Until 2026-09-25 this engine read neither state, and 3D WinBench
         * 98's Cylindrical Wrap u and v tests drew the seam interpolated the
         * long way round the whole cylinder
         * (docs\decisions\2026-09-24-3d-winbench-98-quality-on-the-netbook.md).
         */
        if (draw->texture.wrap_u != 0ul) {
            cylinder |= V9X_I9XX_CYLINDER_U;
        }
        if (draw->texture.wrap_v != 0ul) {
            cylinder |= V9X_I9XX_CYLINDER_V;
        }
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
        /*
         * tu and tv, which sit past color and specular in the vertex - bits[6]
         * and bits[7]. Copied only when something will sample them, so an
         * untextured draw does not depend on fields an application had no
         * reason to fill.
         */
        if (textured != 0) {
            uv[(vertex * 2ul) + 0ul] = bits[6];
            uv[(vertex * 2ul) + 1ul] = bits[7];
        }
    }

    /*
     * A flush BEFORE the batch, with the texture cache invalidated.
     *
     * The trailing flush (intel64) writes the render cache back so the flip
     * shows finished pixels. A bare leading flush (intel76) changed nothing:
     * the render cache was not the stale one. What intel76 also showed is
     * the texture traffic: 76,006 texture creates and as many destroys in
     * 650 frames, a Lock for each - Final Reality rebuilds about a hundred
     * textures a frame, so the same video memory is refilled by the CPU,
     * through the aperture, with different texels every frame. The GPU
     * samples texels through its map cache, and nothing this driver ever
     * emitted invalidated that cache. A draw can therefore sample the texels
     * of the texture that occupied the address a frame earlier; with the
     * alpha of those textures blended over the scene, that is a translucent
     * picture of an older frame laid over a correct one. MI_READ_FLUSH is
     * what i915 emits when the sampler domain is invalidated
     * (i915_gem_execbuffer / intel_ring_flush, v4.4). UNMEASURED as a fix.
     */
    if (at >= V9X_I9XX_SUBMIT_DWORDS) {
        return v9x_d3d_i9xx_refuse(V9X_I9XX_REFUSE_CAPACITY);
    }
    stream[at++] = V9X_I9XX_MI_FLUSH_READ;

    if (v9x_i9xx_build_runtime_state(draw->target.offset, draw->target.pitch,
                                     draw->target.width, draw->target.height,
                                     textured != 0 ? &map : 0,
                                     depth_offset, depth_pitch, depth_writes,
                                     depth_compare,
                                     blend_src, blend_dst, cylinder,
                                     alpha_test, stream + at,
                                     V9X_I9XX_SUBMIT_DWORDS - at,
                                     &produced) != V9X_STATUS_OK) {
        return v9x_d3d_i9xx_refuse(V9X_I9XX_REFUSE_STATE);
    }
    at += produced;
    /*
     * The program follows the state block and must agree with it: the
     * modulate program reads a texel and the plain one does not, and a
     * textured state block with the plain program samples nothing while
     * declaring a coordinate set. The decoder checks the pairing too.
     */
    if ((textured != 0
            ? v9x_i9xx_build_texture_program(program, stream + at,
                                             V9X_I9XX_SUBMIT_DWORDS - at,
                                             &produced)
            : v9x_i9xx_build_fragment_program(stream + at,
                                              V9X_I9XX_SUBMIT_DWORDS - at,
                                              &produced)) != V9X_STATUS_OK) {
        return v9x_d3d_i9xx_refuse(V9X_I9XX_REFUSE_PROGRAM);
    }
    at += produced;
    if ((textured != 0
            ? v9x_i9xx_build_textured_runtime_run(
                  xyzw, colors, uv, triangle_count,
                  draw->target.width, draw->target.height, stream + at,
                  V9X_I9XX_SUBMIT_DWORDS - at, &produced)
            : v9x_i9xx_build_runtime_run(
                  xyzw, colors, triangle_count,
                  draw->target.width, draw->target.height, stream + at,
                  V9X_I9XX_SUBMIT_DWORDS - at, &produced))
            != V9X_STATUS_OK) {
        return v9x_d3d_i9xx_refuse(V9X_I9XX_REFUSE_VERTICES);
    }
    at += produced;
    /*
     * An MI_FLUSH after the draw, so the wait below means "drawn" and not
     * only "parsed".
     *
     * The submit waits for RING_HEAD to reach the tail, which is the parser
     * having consumed the commands; the pixels of the last primitive can
     * still be in the render cache when a Flip then moves the scanout to
     * them. intel64 measured the flip itself clean - 738 Flips handled, none
     * declined or abandoned - and the operator still saw a little flicker,
     * which is the shape of frames presented a few pixels short. The scene
     * streams put an MI_FLUSH after every blit for the same reason; the
     * decoder already accepts the dword anywhere. UNMEASURED as a fix.
     */
    if (at >= V9X_I9XX_SUBMIT_DWORDS) {
        return v9x_d3d_i9xx_refuse(V9X_I9XX_REFUSE_CAPACITY);
    }
    stream[at++] = V9X_I9XX_MI_FLUSH;
    /*
     * The breadcrumb, behind the flush: a sequence number stored to the
     * status page when everything ahead of it has drawn. The submit waits
     * for it after the head; see the diagnostics comment (intel80).
     */
    if (at + V9X_I9XX_BREADCRUMB_STREAM_DWORDS > V9X_I9XX_SUBMIT_DWORDS) {
        return v9x_d3d_i9xx_refuse(V9X_I9XX_REFUSE_CAPACITY);
    }
    if (v9x_d3d_i9xx_hws_open()) {
        /* A timed-out predecessor that has landed since is resolved here,
         * without waiting: drawing behind unfinished drawing is in order on
         * one engine. Only the CPU and the flip have to wait. */
        (void)v9x_d3d_i9xx_render_drain(0);
        v9x_d3d_i9xx_breadcrumb_expected = ++v9x_d3d_i9xx_breadcrumb_sequence;
        if (v9x_d3d_i9xx_breadcrumb_expected == 0ul) {
            v9x_d3d_i9xx_breadcrumb_expected =
                ++v9x_d3d_i9xx_breadcrumb_sequence;
        }
        {
            DWORD produced_crumb = 0ul;

            if (v9x_i9xx_build_breadcrumb_stream(
                    v9x_d3d_i9xx_breadcrumb_offset(),
                    v9x_d3d_i9xx_breadcrumb_expected, stream + at,
                    V9X_I9XX_SUBMIT_DWORDS - at,
                    &produced_crumb) != V9X_STATUS_OK) {
                v9x_d3d_i9xx_breadcrumb_expected = 0ul;
                return v9x_d3d_i9xx_refuse(V9X_I9XX_REFUSE_CAPACITY);
            }
            at += produced_crumb;
        }
    } else {
        /* No status page: the batch goes without a breadcrumb and the
         * submit waits on the head alone, as every build before intel81. */
        v9x_d3d_i9xx_breadcrumb_expected = 0ul;
    }
    /* The ring tail must land qword aligned, and the plan refuses an odd
     * count rather than padding one - so the pad is here, where the stream is
     * still being built and a NOOP is a dword nobody will miss. */
    if ((at & 1ul) != 0ul) {
        if (at >= V9X_I9XX_SUBMIT_DWORDS) {
            return v9x_d3d_i9xx_refuse(V9X_I9XX_REFUSE_CAPACITY);
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
    limits.target_offset = draw->target.offset;
    limits.target_bytes = draw->target.pitch * draw->target.height;
    limits.target_pitch = draw->target.pitch;
    limits.target_width = draw->target.width;
    limits.target_height = draw->target.height;
    limits.texture_offset = textured != 0 ? map.offset : 0ul;
    /*
     * Non-zero is what tells the decoder this stream samples, and the value is
     * the footprint bind_map already proved fits. The multiply is here rather
     * than in the decoder because the decoder is 16-bit code where a 32-bit
     * multiply calls a helper it cannot reach.
     */
    limits.texture_bytes = textured != 0 ? map_bytes : 0ul;
    limits.texture_width = textured != 0 ? map.width : 0ul;
    limits.texture_height = textured != 0 ? map.height : 0ul;
    limits.texture_pitch = textured != 0 ? map.pitch : 0ul;
    limits.texture_format = textured != 0 ? map.format : 0ul;
    limits.texture_wrap = textured != 0 ? map.wrap : 0ul;
    limits.texture_mag_linear = textured != 0 ? map.mag_linear : 0ul;
    limits.texture_min_linear = textured != 0 ? map.min_linear : 0ul;
    limits.blend_src = blend_src;
    limits.blend_dst = blend_dst;
    limits.texture_program = program;
    limits.depth_offset = depth_offset;
    limits.depth_bytes = depthed != 0 ? draw->target.height * depth_pitch : 0ul;
    limits.depth_pitch = depth_pitch;
    limits.depth_writes = depth_writes;
    limits.depth_compare = depth_compare;
    limits.alpha_test = alpha_test;
    limits.kind = V9X_I9XX_SCENE_RUNTIME;
    limits.texture_cylinder = cylinder;
    limits.texture_mip_filter = textured != 0 ? map.mip_filter : 0ul;
    limits.texture_max_lod = textured != 0 ? map.max_lod : 0ul;
    limits.breadcrumb_offset = v9x_d3d_i9xx_breadcrumb_expected != 0ul
                                   ? v9x_d3d_i9xx_breadcrumb_offset() : 0ul;
    {
        DWORD decode_started = V9X_TIME_BEGIN();
        v9x_u16 decoded = v9x_i9xx_decode_phase5_stream(stream, at, &limits,
                                                        &rejected);

        V9X_TIME_END(V9X_TIME_DECODE, decode_started);
        if (decoded != V9X_I9XX_P5_OK) {
            v9x_d3d_i9xx_breadcrumb_expected = 0ul;
            return v9x_d3d_i9xx_refuse(V9X_I9XX_REFUSE_DECODER);
        }
    }

    v9x_present_note_submission();
    if (!v9x_d3d_i9xx_ring_submit(stream, at)) {
        v9x_d3d_i9xx_breadcrumb_expected = 0ul;
        return v9x_d3d_i9xx_refuse(V9X_I9XX_REFUSE_SUBMIT);
    }
    v9x_d3d_i9xx_breadcrumb_expected = 0ul;
#if V9X_I9XX_SCAN_WATCH
    if (v9x_hal->d3d_diagnostics.i9xx_draws_submitted == 0ul) {
        v9x_d3d_i9xx_watch_scanout();
    }
#endif
    ++v9x_hal->d3d_diagnostics.i9xx_draws_submitted;
    if (textured != 0) {
        ++v9x_hal->d3d_diagnostics.i9xx_texture_draws;
        /* Successful textured submissions with these sampler states.
         * Binding alone can be followed by a refusal; neither counter
         * proves which filter the hardware used for any particular pixel. */
        if (map.mag_linear != 0ul) {
            ++v9x_hal->d3d_diagnostics.draws_mag_linear;
        }
        if (map.min_linear != 0ul) {
            ++v9x_hal->d3d_diagnostics.draws_min_linear;
        }
        if (map.max_lod != 0ul) {
            ++v9x_hal->d3d_diagnostics.mip_draws;
        }
    }
    if (depthed != 0) {
        ++v9x_hal->d3d_diagnostics.i9xx_depth_draws;
    }
    return 1;
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

/* Side-effect-free capability query. Binding still validates placement and
 * mip layout; this rejects state the command builder would approximate. */
/*
 * Whether bind_texture would take this surface: a video-memory texture,
 * sixteen bits a texel, powers of two inside the limits. The same
 * tests as the bind, without its counters - the caller only asks.
 */
static int v9x_d3d_i9xx_texture_bindable(const V9X_DD_SURFACE_LCL *surface)
{
    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
        return 0;
    }
    if (v9x_d3d_i9xx_texture_shape((DWORD)surface->lpGbl->wWidth,
                                   (DWORD)surface->lpGbl->wHeight,
                                   V9X_D3D_I9XX_SAMPLER_SIZE_MIN,
                                   v9x_d3d_i9xx_limits.texture_size_max) ==
            V9X_FALSE) {
        return 0;
    }
    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul) {
        return (surface->lpGbl->ddpfSurface.dwFlags & V9X_DDPF_RGB) != 0ul &&
               surface->lpGbl->ddpfSurface.dwRGBBitCount == 16ul;
    }
    return 1;
}

static int v9x_d3d_i9xx_accepts(const V9X_R3D_DRAW *draw)
{
    if (draw == 0 ||
        (draw->target.format != V9X_R3D_FORMAT_RGB565 &&
         draw->target.format != V9X_R3D_FORMAT_XRGB1555)) {
        return 0;
    }
    /* An explicit draw's CPU texture, scissor or channel mask: the command
     * builder here emits none of them yet (the ring could carry a scissor
     * and a write mask; neither is built). */
    if (draw->explicit_state != 0ul &&
        (draw->texture.levels != 0 || draw->write_mask != 7ul ||
         draw->scissor_left != 0ul || draw->scissor_top != 0ul ||
         draw->scissor_right != draw->target.width ||
         draw->scissor_bottom != draw->target.height)) {
        return 0;
    }
    if (draw->fog_enable != 0ul) {
        return 0;
    }
    /* The alpha test is emitted by the draw since 2026-09-25 (S6, through
     * v9x_i9xx_alpha_test_bits); refuse only what that encoder cannot say,
     * which is what the draw would count and draw untested. */
    {
        v9x_u32 alpha_test;

        if (v9x_i9xx_alpha_test_bits(draw->alpha_test_enable,
                                     draw->alpha_func, draw->alpha_ref,
                                     &alpha_test) == V9X_FALSE) {
            return 0;
        }
    }
    if (draw->blend_enable != 0ul &&
        !(draw->src_blend == V9X_R3D_BLEND_ONE &&
          draw->dst_blend == V9X_R3D_BLEND_ZERO) &&
        (v9x_d3d_i9xx_blend_factor(draw->src_blend) == 0ul ||
         v9x_d3d_i9xx_blend_factor(draw->dst_blend) == 0ul)) {
        return 0;
    }
    /* A surface texture the bind would refuse draws untextured, which
     * Direct3D counts and accepts; an explicit draw is refused instead, so
     * the render interface's caller can send its CPU copy. */
    if (draw->explicit_state != 0ul && draw->texture.object != 0 &&
        !v9x_d3d_i9xx_texture_bindable(
            (const V9X_DD_SURFACE_LCL *)draw->texture.object)) {
        return 0;
    }
    if (draw->texture.object != 0) {
        if (draw->texture.op != V9X_R3D_TEXOP_DECAL &&
            draw->texture.op != V9X_R3D_TEXOP_MODULATE &&
            draw->texture.op != V9X_R3D_TEXOP_MODULATEALPHA) {
            return 0;
        }
        if (draw->texture.address != V9X_R3D_ADDRESS_WRAP &&
            draw->texture.address != V9X_R3D_ADDRESS_MIRROR &&
            draw->texture.address != V9X_R3D_ADDRESS_CLAMP) {
            return 0;
        }
    }
    return 1;
}

/*
 * Positional; draw_triangles is null and the neutral draw entry is set
 * (Phase 1d of the OpenGL plan, 2026-09-26): this engine reads nothing
 * from V9X_D3D_CONTEXT. The texture is the object the core resolved once
 * per batch, and the mip-tree and bind checks read it as they always did.
 */
const V9X_D3D_ENGINE_OPS v9x_d3d_engine_i9xx = {
    &v9x_d3d_i9xx_limits,
    v9x_d3d_i9xx_texture_format,
    v9x_d3d_i9xx_describe_caps,
    0,
    v9x_d3d_i9xx_ready,
    v9x_d3d_i9xx_create_surface,
    v9x_d3d_i9xx_destroy_surface,
    v9x_d3d_i9xx_draw,
    v9x_d3d_i9xx_accepts
};
