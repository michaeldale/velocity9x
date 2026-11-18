#ifndef VELOCITY9X_PALETTE16_H
#define VELOCITY9X_PALETTE16_H

/*
 * The 8 bpp palette onto the card, by the VGA DAC ports or by VBE 4F09h
 * (palette16.c says which and why).
 *
 * The three entries are far: the ati family compiles palette16.c into
 * LEAFCODE (velocity9x\leaf16.h).
 */
#include "win9x_display_abi.h"
#include "velocity9x/leaf16.h"

/* Pick the route for this Enable, from the 4F00h capabilities. */
void V9X_LEAF_FAR v9x_palette16_choose(void);

/* Load entries [start, start + count) of a 256-entry table. The caller has
 * clamped the range to the table. */
void V9X_LEAF_FAR v9x_palette16_load(const RGBQUAD FAR *table, WORD start,
                                     WORD count);

/* Palette= in V9XBOOT.INI: the route and the last VBE load's result. */
void V9X_LEAF_FAR v9x_palette16_trace(void);

#endif
