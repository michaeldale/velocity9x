#include "velocity9x/nv50_crtc.h"

/*
 * The VBIOS's own split (C000:5326): the dword address's low word to
 * CR0C (high byte) and CR0D (low byte), bits 23:16 to CR35, bits 29:24
 * to CR34 masked to 3Fh. A byte offset off a dword would need the
 * attribute controller's pan register as well; nothing DirectDraw flips
 * to is placed off a dword, so it is refused rather than rounded.
 */
v9x_status v9x_nv50_build_crtc_start(v9x_u32 byte_offset, v9x_u32 vram_bytes,
                                     struct v9x_nv50_crtc_start *start)
{
    v9x_u32 dwords = byte_offset >> 2;

    if (start == 0 || (byte_offset & 3ul) != 0ul ||
        byte_offset >= vram_bytes || dwords >= V9X_NV50_CRTC_START_LIMIT) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    start->cr0d = (v9x_u8)(dwords & 0xfful);
    start->cr0c = (v9x_u8)((dwords >> 8) & 0xfful);
    start->cr35 = (v9x_u8)((dwords >> 16) & 0xfful);
    start->cr34 = (v9x_u8)((dwords >> 24) & V9X_NV50_CR34_START_MASK);
    return V9X_STATUS_OK;
}
