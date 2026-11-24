/*
 * GDI screen-to-screen SRCCOPY on the Gen3 blitter, for the intel-gma
 * family's arm of gdi_accel.c's BitBlt (docs\plans\gen3-gdi-blitter.md).
 *
 * A window moved or scrolled is a copy the DIB Engine does by reading
 * uncached video memory, about 6 pixels a microsecond on the GMA 950
 * netbook, where the blitter moved the same copies 13 to 47 times faster
 * (docs\decisions\2026-11-24-gen3-gdi-copies-and-text-on-the-netbook.md).
 * gdi_accel.c runs its gates and hands a copy here at gate 8; the band plan
 * and the stream are pure C in src\chipsets\intel\i9xx_gdi.c.
 *
 * The copy is synchronous: the stream ends with a breadcrumb of this
 * driver's own (byte 0x100 of the status page, clear of the HAL's), and
 * the run waits for it before returning. Nothing this file starts is still
 * running when GDI or the DIB Engine next touches the screen, so the
 * dispatcher sets no dirty flag for it and the BeginAccess drain never
 * reaches it. The HAL writes the same ring; both producers read HEAD and
 * TAIL from the registers on every submission and run under the Win16 mutex
 * (measured for the HAL, 2026-09-26), so they are serialized.
 *
 * LEAFCODE (velocity9x\leaf16.h), with i9xx_gdi.c, i9xx_blt.c and
 * i9xx_flip.c, which it calls near: _TEXT has no room for them. Nothing
 * here calls into _TEXT; the reserve hook is read by gdi_accel.c, and
 * every multiply has 16-bit operands.
 */
#include "velocity9x/engine_abi.h"
#include "velocity9x/i9xx_gdi.h"
#include "gdi_gen3.h"

/* runtime.asm. */
extern WORD FAR PASCAL V9xEngineSelector(DWORD linear_base, WORD limit);
extern WORD FAR PASCAL V9xI9xxRingSelector(DWORD linear_base, DWORD limit);
extern DWORD FAR PASCAL V9xI9xxRingRead(DWORD offset);
extern WORD FAR PASCAL V9xI9xxRingCopy(DWORD offset, const DWORD FAR *source,
                                       WORD dwords);
extern DWORD FAR PASCAL V9xI9xxMmioRead(WORD offset);
extern WORD FAR PASCAL V9xI9xxRingKick(WORD tail_register, DWORD tail);

/* HEAD reads waiting for ring space, and status-page reads waiting for the
 * breadcrumb, before the engine is declared dead. Each read is an uncached
 * bus cycle of the order of a microsecond, so these are about a fifth of a
 * second and four seconds; a whole-screen copy took about two
 * milliseconds on the netbook. */
#define V9X_GDI_GEN3_RING_POLLS   200000ul
#define V9X_GDI_GEN3_CRUMB_POLLS  4000000ul
/* The ring window the selector covers: the ring and its status page. */
#define V9X_GDI_GEN3_RING_WINDOW  (V9X_I9XX_RING_BYTES + 0x1000ul)
#define V9X_GDI_GEN3_CRUMB_WINDOW_OFFSET \
    (V9X_I9XX_RING_BYTES + V9X_I9XX_GDI_BREADCRUMB_BYTE)

/* Where the reserve begins: the ring's graphics address, and the end of the
 * memory a copy may touch (the decoder's vram_bytes). */
static DWORD v9x_gdi_gen3_reserve;
static DWORD v9x_gdi_gen3_ring_bytes;
/* The prepared copy, its band count and its first stream. */
static struct v9x_i9xx_gdi_copy v9x_gdi_gen3_copy;
static DWORD v9x_gdi_gen3_bands;
static DWORD v9x_gdi_gen3_written;
static DWORD v9x_gdi_gen3_sequence;
static v9x_u32 v9x_gdi_gen3_stream[V9X_I9XX_GDI_STREAM_DWORDS];
/* One MI_NOOP, for the pad to the ring's end. */
static const DWORD v9x_gdi_gen3_noop = V9X_I9XX_MI_NOOP;

WORD V9X_LEAF_FAR v9x_gdi_gen3_configure(unsigned long control_base,
                                         unsigned long ring_base,
                                         unsigned long ring_size,
                                         unsigned long reserve,
                                         unsigned long vram_bytes)
{
    /* The ring is open only on a boot that permits 3D (gma950_hw16.c), so
     * V9X3D OFF from DOS turns this off with it. A reserve hook that
     * refused hands back the size unchanged. */
    if (control_base == 0ul || ring_base == 0ul ||
        ring_size != V9X_I9XX_RING_BYTES || reserve == 0ul ||
        reserve >= vram_bytes) {
        return 0u;
    }
    if (V9xEngineSelector(control_base, 0xffffu) == 0u ||
        V9xI9xxRingSelector(ring_base, V9X_GDI_GEN3_RING_WINDOW - 1ul) ==
            0u) {
        return 0u;
    }
    v9x_gdi_gen3_reserve = reserve;
    v9x_gdi_gen3_ring_bytes = ring_size;
    return 1u;
}

/* A sequence the breadcrumb does not hold now, so the wait cannot be
 * satisfied by what is already there; never zero. */
static DWORD v9x_gdi_gen3_next_sequence(void)
{
    DWORD sequence =
        V9xI9xxRingRead(V9X_GDI_GEN3_CRUMB_WINDOW_OFFSET) + 1ul;

    return sequence != 0ul ? sequence : 1ul;
}

/* The breadcrumb's graphics address: byte 0x100 of the status page. */
static DWORD v9x_gdi_gen3_crumb_address(void)
{
    return v9x_gdi_gen3_reserve + V9X_I9XX_RING_BYTES +
           V9X_I9XX_GDI_BREADCRUMB_BYTE;
}

/* Build the stream from first_band into v9x_gdi_gen3_stream. */
static WORD v9x_gdi_gen3_build(DWORD first_band, DWORD *bands)
{
    v9x_gdi_gen3_sequence = v9x_gdi_gen3_next_sequence();
    return v9x_i9xx_gdi_build_copy(&v9x_gdi_gen3_copy, first_band,
                                   v9x_gdi_gen3_crumb_address(),
                                   v9x_gdi_gen3_sequence,
                                   v9x_gdi_gen3_stream,
                                   V9X_I9XX_GDI_STREAM_DWORDS,
                                   &v9x_gdi_gen3_written, bands) ==
                   V9X_STATUS_OK
               ? 1u
               : 0u;
}

WORD V9X_LEAF_FAR v9x_gdi_gen3_prepare(V9X_DIB_ENGINE FAR *device,
                                       WORD destination_x,
                                       WORD destination_y,
                                       WORD source_x, WORD source_y,
                                       WORD width, WORD height)
{
    v9x_gdi_gen3_copy.bytes_per_pixel = (DWORD)(device->deBitsPixel >> 3);
    v9x_gdi_gen3_copy.vram_bytes = v9x_gdi_gen3_reserve;
    v9x_gdi_gen3_copy.base = device->deBitsOffset;
    v9x_gdi_gen3_copy.pitch = (DWORD)device->deWidthBytes;
    v9x_gdi_gen3_copy.x = destination_x;
    v9x_gdi_gen3_copy.y = destination_y;
    v9x_gdi_gen3_copy.source_x = source_x;
    v9x_gdi_gen3_copy.source_y = source_y;
    v9x_gdi_gen3_copy.width = width;
    v9x_gdi_gen3_copy.height = height;
    return v9x_gdi_gen3_build(0ul, &v9x_gdi_gen3_bands);
}

/*
 * Put the stream on the ring, start it, and wait for its breadcrumb.
 *
 * TAIL is read once and HEAD until the plan fits, as the HAL does
 * (d3d_i9xx.c, v9x_d3d_i9xx_ring_submit_mode); the plan pads to the ring's
 * end rather than split a command. 1 when the breadcrumb arrived. A ring
 * that has no space, an invalid plan or a breadcrumb that never arrives is
 * an engine this path must not feed again.
 */
static WORD v9x_gdi_gen3_submit(V9X_GDI_STATS FAR *stats)
{
    struct v9x_i9xx_ring_plan plan;
    v9x_status status;
    DWORD head;
    DWORD tail;
    DWORD polls;
    DWORD index;

    stats->last_status_issued = v9x_gdi_gen3_sequence;
    tail = V9xI9xxMmioRead((WORD)V9X_I9XX_REG_RING_TAIL) &
           V9X_I9XX_RING_HEAD_MASK;
    for (polls = 0ul;; ++polls) {
        head = V9xI9xxMmioRead((WORD)V9X_I9XX_REG_RING_HEAD) &
               V9X_I9XX_RING_HEAD_MASK;
        status = v9x_i9xx_ring_plan(head, tail, v9x_gdi_gen3_ring_bytes,
                                    v9x_gdi_gen3_written, &plan);
        if (status == V9X_STATUS_OK) {
            break;
        }
        if (status != V9X_STATUS_INSUFFICIENT_MEMORY) {
            stats->last_status_entry = head;
            return 0u;
        }
        if (polls >= V9X_GDI_GEN3_RING_POLLS) {
            ++stats->fifo_timeouts;
            return 0u;
        }
    }

    for (index = 0ul; index < plan.pad_dwords; ++index) {
        if (V9xI9xxRingCopy(tail + (index << 2), &v9x_gdi_gen3_noop, 1u) ==
            0u) {
            return 0u;
        }
    }
    if (V9xI9xxRingCopy(plan.command_tail,
                        (const DWORD FAR *)v9x_gdi_gen3_stream,
                        (WORD)v9x_gdi_gen3_written) == 0u ||
        V9xI9xxRingKick((WORD)V9X_I9XX_REG_RING_TAIL, plan.next_tail) ==
            0u) {
        return 0u;
    }

    for (polls = 0ul; polls < V9X_GDI_GEN3_CRUMB_POLLS; ++polls) {
        if (V9xI9xxRingRead(V9X_GDI_GEN3_CRUMB_WINDOW_OFFSET) ==
            v9x_gdi_gen3_sequence) {
            return 1u;
        }
    }
    ++stats->idle_timeouts;
    return 0u;
}

WORD V9X_LEAF_FAR v9x_gdi_gen3_run(V9X_GDI_STATS FAR *stats)
{
    DWORD first_band;

    if (v9x_gdi_gen3_submit(stats) == 0u) {
        return V9X_GDI_GEN3_FAILED;
    }
    /*
     * A copy of more than V9X_I9XX_GDI_MAX_BANDS bands is several streams,
     * each submitted and waited for in band order. Once one has run, the
     * source may be half moved, so the DIB Engine cannot start the copy
     * again: a later stream that will not build or run is reported done,
     * and the engine failed, which is what a timeout part-way through one
     * stream already means.
     */
    for (first_band = V9X_I9XX_GDI_MAX_BANDS;
         first_band < v9x_gdi_gen3_bands;
         first_band += V9X_I9XX_GDI_MAX_BANDS) {
        if (v9x_gdi_gen3_build(first_band, 0) == 0u ||
            v9x_gdi_gen3_submit(stats) == 0u) {
            return V9X_GDI_GEN3_DONE_FAILED;
        }
    }
    return V9X_GDI_GEN3_DONE;
}
