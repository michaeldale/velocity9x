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
/* The mask-source flags a parsed summary reports are the same V9X_VBE_RF_*
 * bits the mini-VDD stores in a cache record, so they are defined once, beside
 * the rest of that contract. */
#include "velocity9x/vbe_cache.h"

/* What each call may write. The caller owns the storage. */
#define V9X_VBE_CONTROLLER_BLOCK_BYTES ((v9x_u16)512u)
#define V9X_VBE_MODE_BLOCK_BYTES       ((v9x_u16)256u)

/*
 * VbeInfoBlock Capabilities bits, as the 2.0 and 3.0 specifications define
 * them. Reported rather than acted on: the driver has no path that changes
 * behaviour on one of these, and inventing one from a bit no BIOS in this
 * project's evidence has been checked against would be a guess.
 *
 * They are carried because they are cheap and because they answer questions
 * the DOS conformance corpus says get asked after the fact - whether the DAC
 * can be switched to 8 bits per channel (the `6bitDAC` / `+badpaldac` defect
 * class), and whether the controller claims to be VGA-compatible at all, which
 * is the assumption every text-mode restore and Safe Mode fallback rests on.
 * See docs\specifications\dos-vbe-conformance.md.
 */
#define V9X_VBE_CAP_DAC_SWITCHABLE  ((v9x_u32)0x00000001ul)
#define V9X_VBE_CAP_NOT_VGA_COMPAT  ((v9x_u32)0x00000002ul)
#define V9X_VBE_CAP_RAMDAC_BLANK    ((v9x_u32)0x00000004ul)
#define V9X_VBE_CAP_STEREO_SIGNAL   ((v9x_u32)0x00000008ul)

struct v9x_vbe_controller_summary {
    v9x_u16 version;            /* BCD; 0x0200 is VBE 2.0 */
    v9x_u32 total_memory_bytes; /* TotalMemory, 64 KiB units widened to bytes */
    v9x_u32 capabilities;       /* The V9X_VBE_CAP_* bits above */
    /*
     * OemSoftwareRev: the BIOS's own revision number, a VBE 2.0 field at a
     * fixed offset.
     *
     * Worth carrying for one reason, and it is an evidence-based one: across
     * Gona's DOS conformance corpus, defects track the video BIOS revision
     * rather than the chip - a BIOS swapped between two cards took the fault
     * with it - and this driver's diagnostics have until now recorded chip
     * identity and no BIOS identity at all. A bug report from an untested card
     * could not be attributed to the one variable that predicts behaviour.
     *
     * This is the cheap half of that identity. The fuller answer is the OEM
     * strings, which are far pointers into the controller block and so need a
     * bounded ring-0 copy; see the mini-VDD contract in the dynamic-VBE plan.
     */
    v9x_u16 oem_software_rev;
};

/* Zero every field, for the same reason v9x_vbe_mode_summary_clear exists: a
 * caller filling this in from mini-VDD registers rather than from a block must
 * start defined, and adding a field here must not leave one such call site
 * reading a stack leftover. */
void v9x_vbe_controller_summary_clear(
    struct v9x_vbe_controller_summary *out);

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
    /*
     * Storage depth: the bits one pixel occupies in memory, BitsPerPixel as
     * the BIOS reports it. Kept under its VBE name because that is what every
     * caller and every trace line already says.
     */
    v9x_u16 bits_per_pixel;
    /*
     * Significant depth: how many of those bits carry colour, which is the sum
     * of the three channel widths. XRGB 8:8:8:8 is 32 storage and 24
     * significant, and packed RGB 8:8:8 is 24 and 24, so the pair distinguishes
     * two formats that BitsPerPixel alone cannot. Palettized 8 bpp is 8 and 8:
     * an index is not a channel, but all eight bits are significant.
     *
     * Derived, not read: a BIOS reports channels, not a total.
     */
    v9x_u16 significant_depth;
    /*
     * Which colour fields the layout above came from: exactly one of
     * V9X_VBE_RF_MASKS_LINEAR or V9X_VBE_RF_MASKS_LEGACY once a direct-colour
     * mode has been parsed, plus V9X_VBE_RF_LIN_STRIDE when the VBE 3 stride
     * was reported. Zero for a palettized mode, which has neither.
     *
     * Worth recording rather than inferring later: a transposed channel on a
     * VBE 3 BIOS is a different bug depending on whether the driver read the
     * linear fields or fell back to the legacy ones, and nothing downstream can
     * tell those apart from the masks alone.
     */
    v9x_u16 mask_flags;
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

/* Zero every field. Worth having as a function rather than a memset at each
 * call site: a caller that fills a summary from somewhere other than a BIOS
 * block - the mini-VDD hands its answers back in registers - must still start
 * from a defined state, and adding a field to the struct then cannot leave one
 * of those call sites reading a stack leftover. */
void v9x_vbe_mode_summary_clear(struct v9x_vbe_mode_summary *out);

/*
 * The significant depth for a summary whose channel fields are already filled
 * in: the sum of the three colour channel widths, or the storage depth itself
 * for a palettized mode. 16 bpp with all channels zero is 16, matching the
 * 5:6:5 assumption v9x_vbe_masks_to_bits documents. Zero when the summary
 * describes no depth at all.
 */
v9x_u16 v9x_vbe_summary_significant_depth(
    const struct v9x_vbe_mode_summary *summary);

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
 *
 * Colour fields follow the VBE 3 rule: the linear set (LinRedMaskSize through
 * LinRsvdFieldPosition) wins, and the legacy set is used only when all eight
 * linear bytes are zero. A VBE 2 BIOS never writes the linear set, and the
 * caller is required to zero the block before the call, so the same rule
 * covers both generations with no version test - and a caller that skips the
 * zeroing gets a VBE 2 mode described by whatever the previous call left at
 * those offsets, which is exactly the fault the rule cannot detect and the
 * reason for the requirement.
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
