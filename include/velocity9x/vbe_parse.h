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
    /*
     * Direct-colour channel layout, byte fields widened. Size is the channel's
     * width in bits and position its lowest bit within the pixel, so red
     * 8@16 / green 8@8 / blue 8@0 is the ordinary 32-bpp arrangement and
     * 5@11 / 6@5 / 5@0 is 5:6:5.
     *
     * The reserved channel is what distinguishes a 32-bpp mode from a packed
     * 24-bpp one when both report bits_per_pixel = 32, and is where an alpha or
     * unused byte lives. A palettized mode leaves all six zero.
     */
    v9x_u16 red_mask_size;
    v9x_u16 red_field_position;
    v9x_u16 green_mask_size;
    v9x_u16 green_field_position;
    v9x_u16 blue_mask_size;
    v9x_u16 blue_field_position;
    v9x_u16 rsvd_mask_size;
    v9x_u16 rsvd_field_position;
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
 * Is this a mode this driver can drive at all?
 *
 * The same judgement v9x_vbe_parse_mode_info applies once it has read a block,
 * split out so a caller holding a summary from somewhere other than a raw BIOS
 * block can apply it too - the mini-VDD hands its answers back in registers,
 * not as 256 bytes. One source of truth for the rule, two ways in.
 */
v9x_u16 v9x_vbe_mode_summary_is_drivable(
    const struct v9x_vbe_mode_summary *summary);

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

/*
 * The channel layout as the three 32-bit masks DirectDraw and the DIB engine
 * want. V9X_FALSE, with the masks zeroed, for a mode whose layout cannot be
 * expressed that way.
 *
 * Depth decides what "cannot" means:
 *
 *   - 8 bpp is palettized. There is no mask; the call refuses.
 *   - 16 bpp with all six fields zero is taken as 5:6:5 rather than refused.
 *     A BIOS reporting a packed-pixel 16-bpp mode need not fill the
 *     direct-colour fields in, and 5:6:5 is the only 16-bpp layout this driver
 *     programs, so it is the answer rather than a guess.
 *   - 24 and 32 bpp with all six zero refuse. There is no comparable single
 *     convention to fall back on, and inventing one is how a scanned mode ends
 *     up with the channels transposed.
 *
 * A size of zero for any of the three colour channels, or a channel that does
 * not fit inside the pixel, is a refusal at every depth.
 */
v9x_u16 v9x_vbe_masks_to_bits(const struct v9x_vbe_mode_summary *summary,
                              v9x_u32 *red, v9x_u32 *green, v9x_u32 *blue);

#endif /* VELOCITY9X_VBE_PARSE_H */
