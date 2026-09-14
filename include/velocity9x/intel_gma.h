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

/* Phase 4's complete command allowlist.  These values are intentionally
 * exact: the decoder rejects even a known opcode carrying unreviewed bits. */
#define V9X_I9XX_MI_NOOP                 ((v9x_u32)0x00000000ul)
#define V9X_I9XX_MI_FLUSH                ((v9x_u32)0x02000000ul)
#define V9X_I9XX_XY_COLOR_BLT            ((v9x_u32)0x54300004ul)
#define V9X_I9XX_BLT_ROP_PATCOPY         ((v9x_u32)0x00f00000ul)
#define V9X_I9XX_BLT_DEPTH_32            ((v9x_u32)0x03000000ul)
#define V9X_I9XX_PHASE4                  ((v9x_u16)4u)
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
};

struct v9x_i9xx_phase4_sequence {
    v9x_u16 completed_step;
    v9x_u16 poisoned;
};

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
