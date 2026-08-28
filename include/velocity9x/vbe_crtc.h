/*
 * The VBE 3.0 CRTCInfoBlock, built from a panel's own timing.
 *
 * VBE 3.0 function 4F02h accepts a caller-supplied CRTC block when bit 11 of
 * the mode number is set. That is the only in-specification way to reach a
 * geometry the BIOS does not list, and on a panel whose native mode the video
 * BIOS declines to describe it is the difference between the desktop filling
 * the screen and not.
 *
 * The block is written as bytes at fixed offsets rather than as a struct: it
 * crosses into the video BIOS, both the 16-bit driver and the 32-bit HAL
 * include this header, and neither compiler's default packing is part of the
 * specification. Nothing here calls a BIOS or touches hardware.
 */
#ifndef VELOCITY9X_VBE_CRTC_H
#define VELOCITY9X_VBE_CRTC_H

#include "velocity9x/edid.h"
#include "velocity9x/types.h"

/* VBE 3.0 table 26: six words, a flags byte, a dword clock, a word refresh,
 * then 40 reserved bytes. */
#define V9X_VBE_CRTC_BYTES ((v9x_u16)59u)

/* Field offsets within the block, in the order the specification lists them. */
#define V9X_VBE_CRTC_HTOTAL      ((v9x_u16)0u)
#define V9X_VBE_CRTC_HSYNC_START ((v9x_u16)2u)
#define V9X_VBE_CRTC_HSYNC_END   ((v9x_u16)4u)
#define V9X_VBE_CRTC_VTOTAL      ((v9x_u16)6u)
#define V9X_VBE_CRTC_VSYNC_START ((v9x_u16)8u)
#define V9X_VBE_CRTC_VSYNC_END   ((v9x_u16)10u)
#define V9X_VBE_CRTC_FLAGS       ((v9x_u16)12u)
#define V9X_VBE_CRTC_PIXEL_CLOCK ((v9x_u16)13u)
#define V9X_VBE_CRTC_REFRESH     ((v9x_u16)17u)

/* The flags byte. Double-scan and interlace are never set: this driver has no
 * runtime table row that could describe either. */
#define V9X_VBE_CRTC_FLAG_DOUBLE_SCAN ((v9x_u8)0x01u)
#define V9X_VBE_CRTC_FLAG_INTERLACED  ((v9x_u8)0x02u)
#define V9X_VBE_CRTC_FLAG_HSYNC_NEG   ((v9x_u8)0x04u)
#define V9X_VBE_CRTC_FLAG_VSYNC_NEG   ((v9x_u8)0x08u)

/* Bit 11 of the 4F02h mode number: "use the CRTC block at ES:DI". */
#define V9X_VBE_MODE_CRTC_OVERRIDE ((v9x_u16)0x0800u)

/*
 * Build the block. `block` is V9X_VBE_CRTC_BYTES and is zeroed first, so the
 * 40 reserved bytes are zero as the specification requires.
 *
 * V9X_FALSE for a timing that cannot be expressed: no pixel clock, no active
 * geometry, a total that does not exceed its active count, or a sync that
 * runs past the total. A refused timing leaves the block zeroed rather than
 * half-written, because a partially built block handed to 4F02h is the one
 * outcome worth ruling out by construction.
 */
v9x_u16 v9x_vbe_crtc_build(const struct v9x_edid_timing *timing,
                           v9x_u8 *block);

#endif /* VELOCITY9X_VBE_CRTC_H */
