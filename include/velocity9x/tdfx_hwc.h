/*
 * 3dfx Glide's EXT_HWC escape: decode a request, build the reply.
 *
 * Pure policy, no I/O. The 16-bit Control handler copies Glide's 64-byte
 * request in, does whatever mapping the answer names, and copies the reply
 * out. Layouts and the meaning of each sub-function:
 * docs\specifications\vsa100-ext-hwc.md section 3; the order 3dfx's shipped
 * Glide uses them in: docs\decisions\2026-10-11-shipped-glide-asks-
 * querydevnode-before-ext-hwc.md.
 */
#ifndef VELOCITY9X_TDFX_HWC_H
#define VELOCITY9X_TDFX_HWC_H

#include "velocity9x/status.h"

/* hwcExtRequest_t is 64 bytes, hwcExtResult_t 44. */
#define V9X_TDFX_HWC_REQUEST_DWORDS     16u
#define V9X_TDFX_HWC_REPLY_DWORDS       11u

/* Request DWORD 1 is the sub-function; DWORD 2 onward its data. */
#define V9X_TDFX_HWC_WHICH              1u
#define V9X_TDFX_HWC_DATA               2u

#define V9X_TDFX_HWC_GETDEVICECONFIG    0x02ul
#define V9X_TDFX_HWC_GETLINEARADDR      0x03ul
#define V9X_TDFX_HWC_SETEXCLUSIVE       0x08ul
#define V9X_TDFX_HWC_RLSEXCLUSIVE       0x09ul
#define V9X_TDFX_HWC_SHARE_CONTEXT_DWORD 0x15ul
#define V9X_TDFX_HWC_UNMAP_MEMORY       0x16ul
#define V9X_TDFX_HWC_PCI_OP             0x18ul

/* What the caller must do besides copying the reply. */
#define V9X_TDFX_HWC_ACTION_NONE        0ul
#define V9X_TDFX_HWC_ACTION_TAKE        1ul
#define V9X_TDFX_HWC_ACTION_RELEASE     2ul

/* The board as the driver knows it. A zero linear address means that
 * aperture is not mapped, and GETLINEARADDR is answered as a failure. */
struct v9x_tdfx_hwc_device {
    v9x_u32 vendor_id;
    v9x_u32 device_id;
    v9x_u32 revision;
    v9x_u32 fb_bytes;
    v9x_u32 bar0_linear;
    v9x_u32 bar1_linear;
};

struct v9x_tdfx_hwc_answer {
    v9x_u32 reply[V9X_TDFX_HWC_REPLY_DWORDS];
    /* DWORDs of reply to copy out; 0 when declined. */
    v9x_u32 reply_dwords;
    /* What ExtEscape returns: 1 handled, 0 declined, -1 the "no" Glide
     * takes from SHARE_CONTEXT_DWORD. */
    v9x_s32 escape_return;
    v9x_u32 action;
};

/* The sub-function a request names, or V9X_STATUS_INVALID_ARGUMENT for one
 * too short to name it. */
v9x_status v9x_tdfx_hwc_answer(const v9x_u32 *request, v9x_u32 request_dwords,
                               const struct v9x_tdfx_hwc_device *device,
                               struct v9x_tdfx_hwc_answer *answer);

#endif
