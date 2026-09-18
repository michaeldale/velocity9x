/* Intel Gen3 policy and read-only MMIO fingerprint decoding. */
#ifndef VELOCITY9X_INTEL_GMA_H
#define VELOCITY9X_INTEL_GMA_H

#include "velocity9x/backend.h"
#include "velocity9x/intel16.h"

#define V9X_PCI_VENDOR_INTEL             ((v9x_u16)0x8086u)
#define V9X_PCI_DEVICE_GMA950_945GSE     ((v9x_u16)0x27aeu)

#define V9X_I9XX_MMIO_BYTES              ((v9x_u32)0x00080000ul)
#define V9X_I9XX_GTT_BYTES               ((v9x_u32)0x00040000ul)
#define V9X_I9XX_GTT_ENTRY_COUNT         ((v9x_u32)65536ul)
#define V9X_I9XX_GTT_PAGE_BYTES          ((v9x_u32)4096ul)
/*
 * The reserve is carved from the TOP of the VBE-reported region, so growing it
 * moves its base downward into memory the Phase 2 inventory already proved
 * backed and linear - it does not extend past anything. It went from 128 KiB
 * to 1 MiB at Phase 5, because a true 640x480x16 render target does not fit in
 * 128 KiB (docs\plans\intel-gma950-phase5.md step 2). No PTE is written.
 *
 * Changing this moves every offset below it AND the literals in
 * src\minivdd32\loader.asm that pin the single tested machine's addresses,
 * including the Phase 4 execution CRC, whose BLT destination is inside the
 * reserve. Those are asserted against this constant by the host tests and by
 * check-intel-ring-plan.ps1; none of them derives it independently.
 */
#define V9X_I9XX_GTT_RESERVE_BYTES       ((v9x_u32)0x00100000ul)
#define V9X_I9XX_RING_BYTES              ((v9x_u32)0x00010000ul)
#define V9X_I9XX_SANDBOX_PAGE_BYTES      ((v9x_u32)0x00001000ul)
#define V9X_I9XX_RING_GUARD_BYTES        ((v9x_u32)8ul)
/*
 * Where Phase 5 and Phase 6 stage their streams, 4 KiB into the ring and
 * clear of the ten dwords Phase 4 stages at zero.
 *
 * loader.asm carries the same value as V9X_I9XX_P5_RING_OFFSET and
 * check-tree asserts the two agree. It is needed on this side because the
 * submission BOUNDARIES are computed here, and whether one is qword aligned
 * depends on this base as much as on the dword count - a test that checked
 * only the count would keep passing if this moved to an odd multiple of four.
 */
#define V9X_I9XX_P5_RING_OFFSET          ((v9x_u32)0x00001000ul)

/*
 * Phase 5's render target: 640x480 at 16 bpp, RGB565, linear.
 *
 * The pitch is bytes per row and must be a multiple of 4, because
 * _3DSTATE_BUF_INFO encodes it as BUF_3D_PITCH(x) = ((x)/4)<<2 and the low two
 * bits are discarded (docs\decisions\2026-09-14-intel-gen3-3d-packet-audit.md
 * section 4). 1280 satisfies that with no padding.
 */
#define V9X_I9XX_TARGET_WIDTH            ((v9x_u32)640ul)
#define V9X_I9XX_TARGET_HEIGHT           ((v9x_u32)480ul)
#define V9X_I9XX_TARGET_PITCH            ((v9x_u32)1280ul)
#define V9X_I9XX_TARGET_BYTES            ((v9x_u32)0x00096000ul)

/*
 * The Phase 6 texture: 32x32 RGB565, linear, one page.
 *
 * Four texels would be the smallest thing that tests addressing in both
 * axes, but the texture is painted by the GPU and XY_COLOR_BLT works in
 * dwords - it cannot usefully paint a single texel. So it is a 2x2 grid of
 * 16x16 BLOCKS, which is the same experiment at a size the blitter can
 * express: four quadrants, four colours, and a probe in each says whether
 * (u,v) reached the texel it names.
 *
 * 32 rows of 64 bytes is 2048, so it fits one page with room, and the four
 * quadrant blits between them cover it exactly - the last one ends on the
 * final byte, which makes the builder's bounds check tight rather than
 * generous.
 */
#define V9X_I9XX_TEXTURE_WIDTH           ((v9x_u32)32ul)
#define V9X_I9XX_TEXTURE_HEIGHT          ((v9x_u32)32ul)
#define V9X_I9XX_TEXTURE_PITCH           ((v9x_u32)64ul)
#define V9X_I9XX_TEXTURE_BYTES           ((v9x_u32)2048ul)

/*
 * The DEPTH buffer: 640 wide, 256 tall, at the render target's pitch.
 *
 * Not the target's height, and the reason is arithmetic rather than taste.
 * The reserve is 1 MiB and the ring, HWS, scratch, target, texture and their
 * guards leave 0x55000 bytes. A full 640x480 16-bit depth buffer is 0x96000.
 * It does not fit, and growing the reserve would move a boundary that two
 * cold-boot GTT captures and the loader all agree on.
 *
 * The PITCH matches the target's, deliberately. Depth is addressed as
 * base + y * pitch + x * 2, so at this pitch any x inside the drawing
 * rectangle is inside a row and only Y needs bounding - one invariant, on
 * one coordinate, which the builder and the decoder can both state. A
 * narrower pitch would have made it two.
 *
 * Every depth scene's geometry must therefore stay above y = 256. The
 * builder refuses a vertex below it rather than trusting the scene table.
 */
#define V9X_I9XX_DEPTH_WIDTH             ((v9x_u32)640ul)
#define V9X_I9XX_DEPTH_HEIGHT            ((v9x_u32)256ul)
#define V9X_I9XX_DEPTH_PITCH             ((v9x_u32)1280ul)
#define V9X_I9XX_DEPTH_BYTES             ((v9x_u32)0x00050000ul)
/*
 * The clear value: FAR, so a first draw at any depth passes a LESS test.
 *
 * 0xFFFF doubled into a dword, because the clear is an XY_COLOR_BLT and the
 * blit fills in dwords - two 16-bit depth values share one. Mesa packs a
 * 16-bit depth clear exactly this way (i915_clear.c: `(packed & 0xffff) |
 * (packed << 16)`), which is the same reason the texture quadrants and the
 * render-target fill are doubled dwords.
 */
#define V9X_I9XX_DEPTH_CLEAR_VALUE       ((v9x_u32)0x0000fffful)
#define V9X_I9XX_DEPTH_CLEAR_DWORD       ((v9x_u32)0xfffffffful)
/* One quadrant, in texels. The blit takes its width in DWORDS, which is half
 * this at 16 bpp. */
#define V9X_I9XX_TEXTURE_BLOCK           ((v9x_u32)16ul)

/* Phase 4's complete command allowlist.  These values are intentionally
 * exact: the decoder rejects even a known opcode carrying unreviewed bits. */
#define V9X_I9XX_MI_NOOP                 ((v9x_u32)0x00000000ul)
#define V9X_I9XX_MI_FLUSH                ((v9x_u32)0x02000000ul)
/*
 * MI_FLUSH with bit 0, MI_READ_FLUSH in i915_reg.h (FLUSH_MAP_CACHE in
 * Mesa's i915): the render cache is written back AND the map (texture)
 * cache is invalidated. The bare flush leaves the texture cache alone, so
 * texels the CPU rewrote through the aperture can be sampled stale.
 */
#define V9X_I9XX_MI_FLUSH_READ           ((v9x_u32)0x02000001ul)
/*
 * MI_STORE_DWORD_IMM with MI_MEM_VIRTUAL (bit 22: the address is a graphics
 * address through the GTT, the 945 form), length 1: three dwords - header,
 * graphics address, data. The form igt's gem_storedw_loop emits for gen < 4.
 * The store is pipelined behind the rendering ahead of it, so a value that
 * has arrived in memory says the drawing before it is finished; the ring
 * head reaching the tail says only that the parser has consumed the
 * commands. The breadcrumb lives in the reserve's status page, which
 * nothing else uses (HWS_PGA was never programmed; intel80 reads the BIOS
 * value), at this offset from the ring start.
 */
#define V9X_I9XX_MI_STORE_DWORD_IMM      ((v9x_u32)0x10400001ul)
#define V9X_I9XX_BREADCRUMB_FROM_RING    (V9X_I9XX_RING_BYTES + 0x800ul)
/*
 * The Gen3 page flip through the ring, i915_reg.h: MI_DISPLAY_FLIP_I915 =
 * MI_INSTR(0x14, 1) = (0x14 << 23) | 1, three dwords - command, pitch,
 * base; the plane in bits 21:20. The flip pends until the display takes
 * it at the retrace, and ISR (0x020ac) holds a per-plane flip-pending bit
 * meanwhile: I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT is bit 11 and
 * PLANE_B is bit 10 (i915_reg.h v4.4 lines 1979 and 1981). The first cut
 * of this driver used bits 2 and 6, which are MI_WAIT_FOR_EVENT's operand
 * bits for the same planes and not status at all; intel71 declared every
 * flip done at once because of it. A plain DSPADDR write sets the same
 * pending bit (i915: "an MMIO update of the plane base pointer will also
 * generate a page-flip completion irq"), so both flip paths wait on it.
 * docs\decisions\2026-09-18-intel-gen3-page-flip-audit.md.
 */
#define V9X_I9XX_MI_DISPLAY_FLIP_I915    ((v9x_u32)0x0a000001ul)
#define V9X_I9XX_MI_DISPLAY_FLIP_PLANE_SHIFT 20
#define V9X_I9XX_REG_ISR                 ((v9x_u32)0x000020acul)
#define V9X_I9XX_ISR_PLANE_A_FLIP_PENDING ((v9x_u32)0x00000800ul)
#define V9X_I9XX_ISR_PLANE_B_FLIP_PENDING ((v9x_u32)0x00000400ul)
/* Command, pitch, base, and a NOOP so the tail stays qword aligned. */
#define V9X_I9XX_FLIP_STREAM_DWORDS      ((v9x_u32)4ul)
#define V9X_I9XX_XY_COLOR_BLT            ((v9x_u32)0x54300004ul)
#define V9X_I9XX_BLT_ROP_PATCOPY         ((v9x_u32)0x00f00000ul)
#define V9X_I9XX_BLT_DEPTH_32            ((v9x_u32)0x03000000ul)
#define V9X_I9XX_PHASE4                  ((v9x_u16)4u)
/*
 * Phase 5 is a separate arm phase, not a flag on Phase 4. The whole
 * purpose is that a token authorising a blitter fill cannot authorise a
 * 3D draw: the 2026-09-13 risk decision opens the errata gate for Phase 4
 * specifically, on an argument about minimal processor-to-graphics
 * interaction that does not transfer to a triangle.
 */
#define V9X_I9XX_PHASE5                  ((v9x_u16)5u)
/*
 * Phase 6 is a separate arm phase again, for the reason Phase 5 was separate
 * from Phase 4: a token authorising ONE draw cannot authorise five. The
 * 2026-09-16 errata amendment bounds a boot at five independent draws, and
 * that bound is a different risk assessment from the one that opened Phase 5.
 */
#define V9X_I9XX_PHASE6                  ((v9x_u16)6u)
#define V9X_I9XX_ARM_TOKEN_MAX           ((v9x_u16)63u)

#define V9X_I9XX_ARM_REJECT_NONE         ((v9x_u16)0u)
#define V9X_I9XX_ARM_REJECT_DISABLED     ((v9x_u16)1u)
#define V9X_I9XX_ARM_REJECT_SAFE_MODE    ((v9x_u16)2u)
#define V9X_I9XX_ARM_REJECT_ERRATA       ((v9x_u16)3u)
#define V9X_I9XX_ARM_REJECT_IDENTITY     ((v9x_u16)4u)
#define V9X_I9XX_ARM_REJECT_PHASE        ((v9x_u16)5u)
#define V9X_I9XX_ARM_REJECT_TOKEN        ((v9x_u16)6u)
#define V9X_I9XX_ARM_REJECT_CRC          ((v9x_u16)7u)
/* Pure Phase 4 sequence gate. A step is recorded only after its work and
 * diagnostic flush complete; timeout/misordering permanently poisons it. */
#define V9X_I9XX_P4_PREFLIGHT            ((v9x_u16)1u)
#define V9X_I9XX_P4_INTENT               ((v9x_u16)2u)
#define V9X_I9XX_P4_STAGE                ((v9x_u16)3u)
#define V9X_I9XX_P4_PRE_SNAPSHOT         ((v9x_u16)4u)
#define V9X_I9XX_P4_PROGRAM              ((v9x_u16)5u)
#define V9X_I9XX_P4_PROBE_DRAINED        ((v9x_u16)6u)
#define V9X_I9XX_P4_WRAP_DRAINED         ((v9x_u16)7u)
#define V9X_I9XX_P4_REPROBE_DRAINED      ((v9x_u16)8u)
#define V9X_I9XX_P4_BLT_DRAINED          ((v9x_u16)9u)
#define V9X_I9XX_P4_VERIFY              ((v9x_u16)10u)
#define V9X_I9XX_P4_TEARDOWN            ((v9x_u16)11u)
#define V9X_I9XX_P4_POST_SNAPSHOT        ((v9x_u16)12u)
#define V9X_I9XX_PIPE_COUNT              ((v9x_u16)2u)
#define V9X_I9XX_PIPE_NONE               ((v9x_u16)0xffffu)
#define V9X_I9XX_SNAPSHOT_DWORDS         ((v9x_u16)20u)

#define V9X_I9XX_PIPECONF_ENABLE         ((v9x_u32)0x80000000ul)
#define V9X_I9XX_DSPCNTR_ENABLE          ((v9x_u32)0x80000000ul)
#define V9X_I9XX_DSPCNTR_FORMAT_MASK     ((v9x_u32)0x3c000000ul)
#define V9X_I9XX_DSPCNTR_PIPE_MASK       ((v9x_u32)0x03000000ul)
#define V9X_I9XX_RING_CTL_VALID          ((v9x_u32)0x00000001ul)
#define V9X_I9XX_RING_POINTER_MASK       ((v9x_u32)0x001ffff8ul)

#define V9X_I9XX_FP_STABLE               ((v9x_u16)0x0001u)
#define V9X_I9XX_FP_NONTRIVIAL           ((v9x_u16)0x0002u)
#define V9X_I9XX_FP_LIVE_PIPE            ((v9x_u16)0x0004u)
#define V9X_I9XX_FP_TIMING_VALID         ((v9x_u16)0x0008u)
#define V9X_I9XX_FP_SOURCE_MATCH         ((v9x_u16)0x0010u)
#define V9X_I9XX_FP_PLANE_MATCH          ((v9x_u16)0x0020u)
#define V9X_I9XX_FP_RING_QUIESCENT       ((v9x_u16)0x0040u)
#define V9X_I9XX_FP_PHASE1_REQUIRED      ((v9x_u16)0x003fu)

#define V9X_I9XX_PTE_VALID               ((v9x_u32)0x00000001ul)
#define V9X_I9XX_PTE_LOCAL               ((v9x_u32)0x00000002ul)
#define V9X_I9XX_PTE_CACHE_MASK          ((v9x_u32)0x00000006ul)
#define V9X_I9XX_PTE_SYSTEM_CACHED       ((v9x_u32)0x00000006ul)
#define V9X_I9XX_PTE_ADDRESS_MASK        ((v9x_u32)0xfffff000ul)
#define V9X_I9XX_PTE_ATTRIBUTE_MASK      ((v9x_u32)0x00000ffful)

#define V9X_I9XX_GTT_COMPLETE            ((v9x_u16)0x0001u)
#define V9X_I9XX_GTT_STABLE              ((v9x_u16)0x0002u)
#define V9X_I9XX_GTT_NONTRIVIAL          ((v9x_u16)0x0004u)
#define V9X_I9XX_GTT_BSM_START           ((v9x_u16)0x0008u)
#define V9X_I9XX_GTT_VBE_BACKED          ((v9x_u16)0x0010u)
#define V9X_I9XX_GTT_RESERVE_BACKED      ((v9x_u16)0x0020u)
#define V9X_I9XX_GTT_PGTBL_MATCH         ((v9x_u16)0x0040u)
#define V9X_I9XX_GTT_PHASE2_REQUIRED     ((v9x_u16)0x007fu)

#define V9X_I9XX_EVENT_MAX               ((v9x_u16)32u)
#define V9X_I9XX_EVENT_DWORDS            ((v9x_u16)20u)
#define V9X_I9XX_EVENT_COMPLETE          ((v9x_u16)0x0001u)
#define V9X_I9XX_EVENT_MMIO_STABLE       ((v9x_u16)0x0002u)
#define V9X_I9XX_EVENT_GTT_STABLE        ((v9x_u16)0x0004u)
#define V9X_I9XX_EVENT_RING_IDLE         ((v9x_u16)0x0008u)
#define V9X_I9XX_EVENT_RING_DISABLED     ((v9x_u16)0x0010u)
#define V9X_I9XX_EVENT_PGTBL_VALID       ((v9x_u16)0x0020u)
#define V9X_I9XX_EVENT_REQUIRED          ((v9x_u16)0x003fu)

/* Fixed dword layout of one Phase 3 event record. */
#define V9X_I9XX_EVENT_SEQUENCE          ((v9x_u16)0u)
#define V9X_I9XX_EVENT_KIND              ((v9x_u16)1u)
#define V9X_I9XX_EVENT_CONTEXT           ((v9x_u16)2u)
#define V9X_I9XX_EVENT_FLAGS             ((v9x_u16)3u)
#define V9X_I9XX_EVENT_PGTBL_CTL         ((v9x_u16)4u)
#define V9X_I9XX_EVENT_RING_TAIL         ((v9x_u16)5u)
#define V9X_I9XX_EVENT_RING_HEAD         ((v9x_u16)6u)
#define V9X_I9XX_EVENT_RING_START        ((v9x_u16)7u)
#define V9X_I9XX_EVENT_RING_CTL          ((v9x_u16)8u)
#define V9X_I9XX_EVENT_HWS_PGA           ((v9x_u16)9u)
#define V9X_I9XX_EVENT_FENCE0            ((v9x_u16)10u)
#define V9X_I9XX_EVENT_GTT_HASH_A        ((v9x_u16)18u)
#define V9X_I9XX_EVENT_GTT_HASH_B        ((v9x_u16)19u)

/* Offsets are documentation-derived and deliberately centralized. */
#define V9X_I9XX_REG_PGTBL_CTL           ((v9x_u32)0x00002020ul)
#define V9X_I9XX_REG_RING_TAIL           ((v9x_u32)0x00002030ul)
#define V9X_I9XX_REG_RING_HEAD           ((v9x_u32)0x00002034ul)
#define V9X_I9XX_REG_RING_START          ((v9x_u32)0x00002038ul)
#define V9X_I9XX_REG_RING_CTL            ((v9x_u32)0x0000203cul)
#define V9X_I9XX_REG_HWS_PGA             ((v9x_u32)0x00002080ul)
#define V9X_I9XX_REG_PIPEA_HTOTAL        ((v9x_u32)0x00060000ul)
#define V9X_I9XX_REG_PIPEA_VTOTAL        ((v9x_u32)0x0006000cul)
#define V9X_I9XX_REG_PIPEA_SRC           ((v9x_u32)0x0006001cul)
#define V9X_I9XX_REG_PIPEA_CONF          ((v9x_u32)0x00070008ul)
#define V9X_I9XX_REG_DSPA_CNTR           ((v9x_u32)0x00070180ul)
#define V9X_I9XX_REG_DSPA_ADDR           ((v9x_u32)0x00070184ul)
#define V9X_I9XX_REG_DSPA_STRIDE         ((v9x_u32)0x00070188ul)
#define V9X_I9XX_REG_PIPEB_HTOTAL        ((v9x_u32)0x00061000ul)
#define V9X_I9XX_REG_PIPEB_VTOTAL        ((v9x_u32)0x0006100cul)
#define V9X_I9XX_REG_PIPEB_SRC           ((v9x_u32)0x0006101cul)
#define V9X_I9XX_REG_PIPEB_CONF          ((v9x_u32)0x00071008ul)
#define V9X_I9XX_REG_DSPB_CNTR           ((v9x_u32)0x00071180ul)
#define V9X_I9XX_REG_DSPB_ADDR           ((v9x_u32)0x00071184ul)
#define V9X_I9XX_REG_DSPB_STRIDE         ((v9x_u32)0x00071188ul)
/*
 * The scanout's position, read-only, per pipe.
 *
 * DSL is the current display line in bits 11:0 (Linux i915_reg.h: _PIPEADSL
 * 0x70000, DSL_LINEMASK_GEN3 0x00000fff). The frame counter on Gen3 is split
 * across two registers: the high 16 bits in FRAMEHIGH (_PIPEAFRAMEHIGH
 * 0x70040, PIPE_FRAME_HIGH_MASK 0x0000ffff) and the low 8 bits in the top
 * byte of FRAMEPIXEL (_PIPEAFRAMEPIXEL 0x70044, PIPE_FRAME_LOW_MASK
 * 0xff000000), whose low 24 bits are the pixel count within the frame.
 * Pipe B is the same layout at +0x1000.
 *
 * Read so that a flip path has a vblank source it has SEEN move rather than
 * one assumed from the VGA status port. Added 2026-09-17; UNMEASURED on the
 * 945GSE until a capture shows the line counter sweep and the frame counter
 * advance.
 */
#define V9X_I9XX_REG_PIPEA_DSL           ((v9x_u32)0x00070000ul)
#define V9X_I9XX_REG_PIPEA_FRAMEHIGH     ((v9x_u32)0x00070040ul)
#define V9X_I9XX_REG_PIPEA_FRAMEPIXEL    ((v9x_u32)0x00070044ul)
#define V9X_I9XX_REG_PIPEB_DSL           ((v9x_u32)0x00071000ul)
#define V9X_I9XX_REG_PIPEB_FRAMEHIGH     ((v9x_u32)0x00071040ul)
#define V9X_I9XX_REG_PIPEB_FRAMEPIXEL    ((v9x_u32)0x00071044ul)
#define V9X_I9XX_DSL_LINE_MASK           ((v9x_u32)0x00000ffful)
#define V9X_I9XX_FRAME_HIGH_MASK         ((v9x_u32)0x0000fffful)
#define V9X_I9XX_FRAME_LOW_SHIFT         24
/* The composed counter is 24 bits: high 16 above low 8. */
#define V9X_I9XX_FRAME_COUNT_MASK        ((v9x_u32)0x00fffffful)

/*
 * A run of scanline readings, summarised as it is fed.
 *
 * The HAL reads the registers - it is the only side with the window mapped
 * at draw time - and this is what it does with the values, kept in pure C so
 * a host test can say what a sweeping counter and a stuck one each look
 * like. `changes` counts readings that differed from the previous one; a
 * live pipe gives many, a dead register gives none, and a register that
 * reads back a constant other than zero gives none either - which is why the
 * count and not the value is the evidence.
 */
struct v9x_i9xx_scan_summary {
    v9x_u32 samples;
    v9x_u32 line_min;
    v9x_u32 line_max;
    v9x_u32 line_changes;
    v9x_u32 line_last;
    v9x_u32 frame_first;
    v9x_u32 frame_last;
    /*
     * The line the counter read on the FIRST sample whose frame count had
     * changed from the previous sample, and whether one was seen. This is
     * where the frame counter ticks, in the display-line register's own
     * units - which is the one number that says whether DSL >= vactive is
     * the blank, and intel65/66 say the flip path does not know.
     */
    v9x_u32 tick_line;
    v9x_u32 tick_seen;
};

/* src\chipsets\intel\i9xx_flip.c */
v9x_u32 v9x_i9xx_flip_stream_extent(void);
v9x_status v9x_i9xx_build_flip_stream(
    v9x_u32 plane, v9x_u32 pitch, v9x_u32 base, v9x_u32 vram_bytes,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written);
/* V9X_TRUE when the stream is exactly the flip for these arguments;
 * otherwise V9X_FALSE with the first offending dword's index. */
v9x_u16 v9x_i9xx_decode_flip_stream(
    const v9x_u32 *stream, v9x_u32 dword_count,
    v9x_u32 plane, v9x_u32 pitch, v9x_u32 base, v9x_u32 vram_bytes,
    v9x_u32 *rejected_index);
v9x_u32 v9x_i9xx_flip_pending_bit(v9x_u32 plane);

/* src\chipsets\intel\i9xx_scanline.c */
void v9x_i9xx_scan_begin(struct v9x_i9xx_scan_summary *summary);
void v9x_i9xx_scan_feed(struct v9x_i9xx_scan_summary *summary,
                        v9x_u32 dsl_raw, v9x_u32 frame_high_raw,
                        v9x_u32 frame_pixel_raw);
/* Frames elapsed between the first and last feed, modulo the 24-bit
 * counter. Zero with no samples. */
v9x_u32 v9x_i9xx_scan_frames(const struct v9x_i9xx_scan_summary *summary);
/* The 24-bit frame counter composed from its two registers; the flip path
 * reads it at issue and again to know a retrace has passed. */
v9x_u32 v9x_i9xx_frame_count(v9x_u32 frame_high_raw, v9x_u32 frame_pixel_raw);

struct v9x_i9xx_pipe_snapshot {
    v9x_u32 pipe_conf;
    v9x_u32 htotal;
    v9x_u32 vtotal;
    v9x_u32 pipe_src;
    v9x_u32 plane_control;
    v9x_u32 plane_address;
    v9x_u32 plane_stride;
};

struct v9x_i9xx_mmio_snapshot {
    v9x_u32 pgtbl_ctl;
    v9x_u32 ring_tail;
    v9x_u32 ring_head;
    v9x_u32 ring_start;
    v9x_u32 ring_ctl;
    v9x_u32 hws_pga;
    struct v9x_i9xx_pipe_snapshot pipe[V9X_I9XX_PIPE_COUNT];
};

struct v9x_i9xx_mode_expectation {
    v9x_u16 width;
    v9x_u16 height;
    v9x_u16 bits_per_pixel;
    v9x_u16 pitch_bytes;
    v9x_u32 gmadr_aperture_bytes;
};

struct v9x_i9xx_fingerprint {
    v9x_u16 flags;
    v9x_u16 live_pipe;
    v9x_u16 live_plane;
    v9x_u16 timing_width;
    v9x_u16 timing_height;
    v9x_u16 total_width;
    v9x_u16 total_height;
    v9x_u16 source_width;
    v9x_u16 source_height;
    v9x_u16 plane_bits_per_pixel;
    v9x_u16 plane_stride;
    v9x_u32 plane_address;
};

struct v9x_i9xx_pte {
    v9x_u32 raw;
    v9x_u32 physical_page;
    v9x_u16 present;
    v9x_u16 cache_bits;
    v9x_u16 known_attributes;
};

/* The first-write sandbox borrows the top 128 KiB already proved to be a
 * linear BSM mapping in Phase 2.  DirectDraw may publish only heap_bytes. */
struct v9x_i9xx_sandbox_layout {
    v9x_u32 heap_bytes;
    v9x_u32 reserve_offset;
    v9x_u32 reserve_physical;
    v9x_u32 ring_offset;
    v9x_u32 ring_physical;
    v9x_u32 ring_bytes;
    v9x_u32 hws_offset;
    v9x_u32 hws_physical;
    v9x_u32 scratch_offset;
    v9x_u32 scratch_physical;
    v9x_u32 scratch_bytes;
    /*
     * Appended at Phase 5. Every member here must be v9x_u32: the whole struct
     * is zeroed by walking it as a v9x_u32 array, so a member of any other
     * width would leave part of itself uninitialised. There is no positional
     * initialiser of this struct anywhere, which is what makes appending safe.
     *
     * The scratch page doubles as the target's lower guard, so it has no
     * separate member; the upper guard sits immediately above the target.
     */
    v9x_u32 target_offset;
    v9x_u32 target_physical;
    v9x_u32 target_bytes;
    v9x_u32 target_pitch;
    v9x_u32 guard_upper_offset;
    v9x_u32 guard_upper_physical;
    /*
     * Appended at Phase 6, on the same append-only terms: every member is a
     * v9x_u32 because the struct is zeroed by walking it as a v9x_u32 array.
     *
     * The texture sits ABOVE the upper guard, so that guard is its lower one,
     * and it has a guard page of its own above. Same arrangement as the render
     * target, for the same reason: a write that leaves the texture is caught
     * on whichever side it leaves from.
     */
    v9x_u32 texture_offset;
    v9x_u32 texture_physical;
    v9x_u32 texture_bytes;
    v9x_u32 texture_pitch;
    v9x_u32 texture_guard_offset;
    v9x_u32 texture_guard_physical;
    /*
     * The depth buffer and its upper guard. The texture's guard page serves as
     * its lower one, on the same pattern the target and texture already use.
     */
    v9x_u32 depth_offset;
    v9x_u32 depth_physical;
    v9x_u32 depth_bytes;
    v9x_u32 depth_pitch;
    v9x_u32 depth_guard_offset;
    v9x_u32 depth_guard_physical;
};

/* A command is never split across the physical end of the ring.  pad_dwords
 * are MI_NOOPs to the end; command_tail is then zero. */
struct v9x_i9xx_ring_plan {
    v9x_u32 command_tail;
    v9x_u32 next_tail;
    v9x_u32 pad_dwords;
    v9x_u32 command_dwords;
    v9x_u32 consumed_bytes;
};

struct v9x_i9xx_arm_request {
    const char *token;
    const char *in_flight;
    v9x_u32 configured_crc;
    v9x_u32 packet_crc;
    v9x_u16 enable_this_boot;
    v9x_u16 safe_mode;
    v9x_u16 errata_gate;
    v9x_u16 vendor_id;
    v9x_u16 device_id;
    v9x_u16 revision;
    v9x_u16 phase;
    /*
     * What the CALLER is arming, as distinct from what the token claims.
     * v9x_i9xx_arm_evaluate used to hardcode Phase 4 on both sides, so a
     * Phase 5 caller could not express the question at all. The two must
     * match, which is what stops a Phase 4 token reaching Phase 5.
     */
    v9x_u16 expected_phase;
};

/*
 * One sequence machine, parameterised by a step range, so Phase 4 and
 * Phase 5 can use disjoint step numbers and a log line is never
 * ambiguous about which phase produced it. Phase 5 numbers from 20.
 *
 * The struct keeps its name and the Phase 4 entry points stay as thin
 * wrappers, so intel_exec16.c and its twelve existing tests are untouched.
 */
struct v9x_i9xx_phase4_sequence {
    v9x_u16 completed_step;
    v9x_u16 poisoned;
    v9x_u16 first_step;
    v9x_u16 last_step;
};

/* Phase 5 sequencer steps, numbered from 20 so a hang is attributable to
 * a phase from the step number alone. */
#define V9X_I9XX_P5_STEP_FIRST           ((v9x_u16)20u)
#define V9X_I9XX_P5_STEP_LAST            ((v9x_u16)29u)

/* Streaming so the 16-bit diagnostic never needs a 256-KiB near array. */
struct v9x_i9xx_gtt_inventory {
    v9x_u16 flags;
    v9x_u16 reserve_ok;
    v9x_u32 expected_entries;
    v9x_u32 next_entry;
    v9x_u32 bsm;
    v9x_u32 stolen_bytes;
    v9x_u32 pgtbl_ctl;
    v9x_u32 gtt_storage_physical;
    v9x_u32 vbe_pages;
    v9x_u32 hash_stream;
    v9x_u32 hash_first;
    v9x_u32 hash_second;
    v9x_u32 present_entries;
    v9x_u32 uncached_entries;
    v9x_u32 local_entries;
    v9x_u32 cached_entries;
    v9x_u32 unknown_attribute_entries;
    v9x_u32 run_count;
    v9x_u32 backed_prefix_entries;
    v9x_u32 reserve_first_entry;
    v9x_u32 reserve_entry_count;
    v9x_u32 reserve_aperture_offset;
    v9x_u32 reserve_physical;
    v9x_u32 first_raw;
    v9x_u32 previous_raw;
    v9x_u32 previous_physical;
    v9x_u32 previous_stride;
    v9x_u32 run_length;
    v9x_u16 previous_present;
    v9x_u16 previous_attributes;
    v9x_u16 prefix_open;
    v9x_u16 all_zero;
    v9x_u16 all_ones;
};

v9x_status v9x_i9xx_analyze_fingerprint(
    const struct v9x_i9xx_mmio_snapshot *first,
    const struct v9x_i9xx_mmio_snapshot *second,
    const struct v9x_i9xx_mode_expectation *expected,
    struct v9x_i9xx_fingerprint *result);
v9x_status v9x_i9xx_decode_pte(v9x_u32 raw, struct v9x_i9xx_pte *pte);
v9x_status v9x_i9xx_gtt_inventory_begin(
    struct v9x_i9xx_gtt_inventory *inventory,
    v9x_u32 entry_count, v9x_u32 bsm, v9x_u32 stolen_bytes,
    v9x_u32 vbe_bytes, v9x_u32 pgtbl_ctl);
v9x_status v9x_i9xx_gtt_inventory_add(
    struct v9x_i9xx_gtt_inventory *inventory,
    v9x_u32 entry, v9x_u32 raw);
v9x_status v9x_i9xx_gtt_inventory_finish(
    struct v9x_i9xx_gtt_inventory *inventory,
    v9x_u32 hash_first, v9x_u32 hash_second);
/*
 * The only i9xx_* entry point called from outside the I9XXCODE segment:
 * gma950_hw16.c owns the near V9X_HW16_DEVICE table and so stays in _TEXT.
 * See velocity9x/intel16.h for why the qualifier is here.
 */
v9x_status V9X_I9XX_FAR v9x_i9xx_sandbox_calculate(
    v9x_u32 vbe_bytes, v9x_u32 bsm,
    struct v9x_i9xx_sandbox_layout *layout);
/*
 * The address field of RING_HEAD. The upper bits are a WRAP COUNT.
 *
 * This lived as a bare `and eax, 001ffffch` in loader.asm and nowhere else -
 * one number, in assembly, with nothing to compare it against. A second
 * submission path needs the same mask, and a second copy of a constant is how
 * this project's recurring defect starts. check-tree ties the two together.
 */
#define V9X_I9XX_RING_HEAD_MASK          ((v9x_u32)0x001ffffcul)

/*
 * Has the submission that ended at `tail_after` completed?
 *
 * The wrap count must be masked off before the comparison; comparing the raw
 * register would never match. Host-testable, and the predicate rather than the
 * mask is what a caller should use - a caller holding the mask is a caller
 * that can forget to apply it.
 */
v9x_u16 v9x_i9xx_ring_submission_complete(
    v9x_u32 head_register, v9x_u32 tail_after);

v9x_status v9x_i9xx_ring_free_space(
    v9x_u32 head, v9x_u32 tail, v9x_u32 ring_bytes,
    v9x_u32 *free_bytes);
v9x_status v9x_i9xx_ring_plan(
    v9x_u32 head, v9x_u32 tail, v9x_u32 ring_bytes,
    v9x_u32 command_dwords, struct v9x_i9xx_ring_plan *plan);
v9x_status v9x_i9xx_build_mi_probe(v9x_u32 *stream, v9x_u32 capacity,
                                    v9x_u32 *written);
v9x_status v9x_i9xx_build_color_blt(
    v9x_u32 destination, v9x_u16 width, v9x_u16 height,
    v9x_u16 pitch, v9x_u32 color,
    v9x_u32 scratch_offset, v9x_u32 scratch_bytes,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written);
v9x_status v9x_i9xx_decode_phase4_stream(
    const v9x_u32 *stream, v9x_u32 dword_count,
    v9x_u32 scratch_offset, v9x_u32 scratch_bytes);
v9x_u32 v9x_i9xx_crc32_dwords(const v9x_u32 *stream,
                               v9x_u32 dword_count);
/* CRC of probe, full-ring NOOP wrap, repeated probe and BLT; no large buffer. */
v9x_u32 v9x_i9xx_phase4_execution_crc(const v9x_u32 *probe,
                                        const v9x_u32 *blt);
v9x_u16 v9x_i9xx_token_valid(const char *text);
v9x_u16 v9x_i9xx_parse_crc_hex(const char *text, v9x_u32 *value);
v9x_status v9x_i9xx_arm_evaluate(
    const struct v9x_i9xx_arm_request *request, v9x_u16 *rejection);
void v9x_i9xx_phase4_sequence_begin(struct v9x_i9xx_phase4_sequence *state);
void v9x_i9xx_sequence_begin_range(
    struct v9x_i9xx_phase4_sequence *state,
    v9x_u16 first_step, v9x_u16 last_step);
void v9x_i9xx_phase5_sequence_begin(
    struct v9x_i9xx_phase4_sequence *state);

/*
 * The two-phase arm transaction.
 *
 * Phase 5 cannot simply reuse the Phase 4 arm path. That path compares
 * the on-disk CRC with the Phase 4 execution CRC and, on success,
 * records the final result and clears IntelInFlight. A Phase 5 token
 * carrying a Phase 5 CRC can therefore neither pass that preflight nor
 * retain its authority until the draw.
 *
 * So a chained run is one transaction over two executions: the Phase 4
 * replay must still pass its own generated stream and CRC gate, but the
 * token is not completed and IntelInFlight is not cleared until the
 * Phase 5 result is known. A power cut between the phases therefore
 * leaves the token in flight, which is what makes the one-shot property
 * survive the chain.
 */
#define V9X_I9XX_CHAIN_OK               ((v9x_u16)0u)
#define V9X_I9XX_CHAIN_REJECT_PHASE     ((v9x_u16)1u)
#define V9X_I9XX_CHAIN_REJECT_ARM       ((v9x_u16)2u)
#define V9X_I9XX_CHAIN_REJECT_COMBINED  ((v9x_u16)3u)
#define V9X_I9XX_CHAIN_REJECT_REPLAY    ((v9x_u16)4u)
#define V9X_I9XX_CHAIN_REJECT_P4_CRC    ((v9x_u16)5u)
#define V9X_I9XX_CHAIN_REJECT_P5_CRC    ((v9x_u16)6u)

/* Where a chained run has got to. Completion is the ONLY state in which
 * the token may be retired and IntelInFlight cleared. */
#define V9X_I9XX_CHAIN_STATE_IDLE       ((v9x_u16)0u)
#define V9X_I9XX_CHAIN_STATE_ARMED      ((v9x_u16)1u)
#define V9X_I9XX_CHAIN_STATE_REPLAYED   ((v9x_u16)2u)
#define V9X_I9XX_CHAIN_STATE_DREW       ((v9x_u16)3u)
#define V9X_I9XX_CHAIN_STATE_FAILED     ((v9x_u16)4u)

struct v9x_i9xx_chain {
    v9x_u16 state;
    v9x_u16 token_retired;
    v9x_u16 in_flight_cleared;
};

/*
 * Which arm gate a consumed token must pass, decided from what the token
 * claims and what the running build actually contains.
 *
 * This is policy, so it lives here in host-testable C rather than inside the
 * Win16 executor that acts on it. The defect it exists to prevent was real:
 * the driver never read IntelArmPhase at all, so the Phase 4 boot latch was
 * the only thing gating Phase 5, and "is this token allowed to draw?" was a
 * question nothing asked.
 */
#define V9X_I9XX_GATE_NONE        ((v9x_u16)0u)
#define V9X_I9XX_GATE_STANDALONE  ((v9x_u16)1u)
#define V9X_I9XX_GATE_CHAINED     ((v9x_u16)2u)
#define V9X_I9XX_GATE_REFUSE      ((v9x_u16)3u)

v9x_u16 v9x_i9xx_arm_gate_for(v9x_u16 armed, v9x_u16 arm_phase,
                               v9x_u16 phase5_built);

v9x_u16 v9x_i9xx_chain_begin(
    struct v9x_i9xx_chain *chain,
    const struct v9x_i9xx_arm_request *request,
    v9x_u32 combined_crc, v9x_u32 phase4_crc, v9x_u32 phase5_crc);
v9x_u16 v9x_i9xx_chain_replay_done(
    struct v9x_i9xx_chain *chain, v9x_u16 replay_passed,
    v9x_u32 observed_phase4_crc, v9x_u32 expected_phase4_crc);
v9x_u16 v9x_i9xx_chain_draw_done(
    struct v9x_i9xx_chain *chain, v9x_u16 draw_passed,
    v9x_u32 observed_phase5_crc, v9x_u32 expected_phase5_crc);
v9x_u32 v9x_i9xx_combined_arm_crc(v9x_u32 phase4_crc,
                                   v9x_u32 phase5_crc);
v9x_status v9x_i9xx_phase4_sequence_commit(
    struct v9x_i9xx_phase4_sequence *state, v9x_u16 step);
void v9x_i9xx_phase4_sequence_poison(
    struct v9x_i9xx_phase4_sequence *state);

v9x_status v9x_intel_gma_probe(struct v9x_backend_state *state,
                               const struct v9x_pci_identity *pci);
v9x_status v9x_intel_gma_bind_framebuffer(
    struct v9x_backend_state *state,
    const struct v9x_pci_bar_resource *bar,
    v9x_u32 detected_vram_bytes,
    v9x_u32 override_vram_bytes);
v9x_status v9x_intel_gma_validate_mode(
    struct v9x_backend_state *state,
    const struct v9x_mode_request *request,
    struct v9x_mode_layout *layout);
const struct v9x_backend_ops *v9x_intel_gma_backend(void);

#endif
