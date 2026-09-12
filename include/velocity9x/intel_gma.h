/* Intel Gen3 policy and read-only MMIO fingerprint decoding. */
#ifndef VELOCITY9X_INTEL_GMA_H
#define VELOCITY9X_INTEL_GMA_H

#include "velocity9x/backend.h"

#define V9X_PCI_VENDOR_INTEL             ((v9x_u16)0x8086u)
#define V9X_PCI_DEVICE_GMA950_945GSE     ((v9x_u16)0x27aeu)

#define V9X_I9XX_MMIO_BYTES              ((v9x_u32)0x00080000ul)
#define V9X_I9XX_PIPE_COUNT              ((v9x_u16)2u)
#define V9X_I9XX_PIPE_NONE               ((v9x_u16)0xffffu)

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

v9x_status v9x_i9xx_analyze_fingerprint(
    const struct v9x_i9xx_mmio_snapshot *first,
    const struct v9x_i9xx_mmio_snapshot *second,
    const struct v9x_i9xx_mode_expectation *expected,
    struct v9x_i9xx_fingerprint *result);

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
