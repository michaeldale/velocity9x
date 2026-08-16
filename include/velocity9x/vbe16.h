/*
 * VBE BIOS services for the 16-bit display driver.
 *
 * Chip-agnostic by construction: everything here is a VESA BIOS call, so a
 * family that supplies no hardware hooks at all can still set a mode. That is
 * the tier-0 path.
 *
 * Calling rule, unchanged from the assembly this replaced: a VBE mode set only
 * happens between VddPreMode and VddRegister, or from ResetHiResMode and
 * Disable. Anywhere else and the VDD's idea of the display state diverges from
 * the hardware's. The two readers below are query-only and change no state, so
 * they are safe anywhere in that window.
 */
#ifndef VELOCITY9X_VBE16_H
#define VELOCITY9X_VBE16_H

#include "velocity9x/vbe_parse.h"

/* 4F02h. mode_flags carries the family's no-clear/linear bits. Non-zero on
 * success. */
unsigned short v9x_vbe_set_mode(unsigned short mode, unsigned short mode_flags);

/* 4F00h. Non-zero when the controller answered with a credible VBE 2.0-or-later
 * block; the summary carries the version and the reported VRAM. */
unsigned short v9x_vbe_read_controller_info(
    struct v9x_vbe_controller_summary *out);

/* 4F01h for one mode number, which must be the bare mode without the family's
 * flag bits. Non-zero when the mode exists and offers a linear framebuffer;
 * the summary carries the aperture base and the stride. */
unsigned short v9x_vbe_read_mode_info(unsigned short mode,
                                      struct v9x_vbe_mode_summary *out);

#endif /* VELOCITY9X_VBE16_H */
