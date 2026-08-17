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
 * the hardware's.
 *
 * Only the mode set is here. 4F00h and 4F01h take a buffer, which ring 3 in
 * this driver has no working way to provide - see the note in vbe16.c - so the
 * mini-VDD makes those calls at ring 0 and enable16.c reads its cache through
 * the API callers in runtime.asm.
 */
#ifndef VELOCITY9X_VBE16_H
#define VELOCITY9X_VBE16_H

#include "velocity9x/vbe_parse.h"

/* 4F02h. mode_flags carries the family's no-clear/linear bits. Non-zero on
 * success. */
unsigned short v9x_vbe_set_mode(unsigned short mode, unsigned short mode_flags);

#endif /* VELOCITY9X_VBE16_H */
