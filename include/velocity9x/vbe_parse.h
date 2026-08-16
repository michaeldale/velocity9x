/*
 * VBE 4F00h and 4F01h result parsing.
 *
 * Split from the BIOS call itself so the judgement — is this block credible,
 * and does it describe the mode the driver just set — is host-testable.
 * src\display16\hw\vbe16.c owns the INT 10h mechanics; this owns the decision.
 *
 * Every field is byte-composed, so one source serves both callers: the driver,
 * where the block sits in DOS memory behind a far pointer and the compiler is
 * targeting 8086, and the host suite, where it is an ordinary array. Nothing
 * here reads a 32-bit register or assumes an alignment.
 */
#ifndef VELOCITY9X_VBE_PARSE_H
#define VELOCITY9X_VBE_PARSE_H

#include "velocity9x/types.h"

/* What each call may write. The caller owns the storage. */
#define V9X_VBE_CONTROLLER_BLOCK_BYTES ((v9x_u16)512u)
#define V9X_VBE_MODE_BLOCK_BYTES       ((v9x_u16)256u)

struct v9x_vbe_controller_summary {
    v9x_u16 version;            /* BCD; 0x0200 is VBE 2.0 */
    v9x_u32 total_memory_bytes; /* TotalMemory, 64 KiB units widened to bytes */
};

struct v9x_vbe_mode_summary {
    v9x_u16 attributes;
    v9x_u16 bytes_per_scan_line;
    /*
     * VBE 3.0 reports the linear-framebuffer stride separately, and when it is
     * present it — not BytesPerScanLine — is what a mode set with the linear
     * bit actually produces. VBE 2.0 does not define the field, and the caller
     * zeroes the block before the call, so zero here means "not reported" and
     * BytesPerScanLine stands. QEMU's BIOS reports VBE 3.0, so this is the
     * common case for tier-0, not an exotic one.
     */
    v9x_u16 lin_bytes_per_scan_line;
    v9x_u16 width;
    v9x_u16 height;
    v9x_u16 bits_per_pixel; /* byte field, widened */
    v9x_u16 memory_model;   /* byte field, widened */
    v9x_u32 phys_base;
};

/*
 * 4F00h. V9X_TRUE when the block is a credible VBE 2.0-or-later answer:
 * "VESA" signature, version at least 0x0200, non-zero TotalMemory. The summary
 * is zeroed on refusal.
 */
v9x_u16 v9x_vbe_parse_controller_info(
    const v9x_u8 *block, struct v9x_vbe_controller_summary *out);

/*
 * 4F01h. V9X_TRUE when the block describes a mode this driver can drive: the
 * mode exists in hardware (attribute bit 0), a linear framebuffer is available
 * for it (attribute bit 7), the memory model is packed-pixel or direct-colour,
 * and the aperture is a real one above the first megabyte. The summary is
 * zeroed on refusal.
 */
v9x_u16 v9x_vbe_parse_mode_info(
    const v9x_u8 *block, struct v9x_vbe_mode_summary *out);

/*
 * The whitelist intersection: the family's static table row and the BIOS
 * answer must describe the same surface. Geometry must match exactly and the
 * effective linear stride must equal the table pitch — GDI and the registry
 * already agreed on that pitch, so a BIOS that reports a different one would
 * misplace every scan line, and the caller refuses the mode rather than
 * adapting to it.
 */
v9x_u16 v9x_vbe_mode_matches(const struct v9x_vbe_mode_summary *summary,
                             v9x_u16 width, v9x_u16 height,
                             v9x_u16 bits_per_pixel, v9x_u16 pitch);

#endif /* VELOCITY9X_VBE_PARSE_H */
