/*
 * Whether a video-memory texture's pixel format is one the 3D engine
 * publishes, for CanCreateSurface.
 *
 * V9XD32's T22 drew black on the Rage 128 Pro, SiS 6326, Voodoo4, Rage XL
 * and RIVA 128 (docs\issues\2026-11-17 to 2026-11-27, "draws black"): in
 * every case the 32-bit layout that drew black is one the engine does not
 * publish, and CanCreateSurface accepted it anyway, so the texture was
 * created, given a handle and sampled as nothing. Engines that draw a
 * 32-bit layout publish it. Refusing an unpublished 32-bit RGB texture
 * makes the creation fail, as DirectDraw expects of a format the driver
 * did not list, instead of drawing black.
 *
 * Only 32-bit RGB layouts are judged: a 16-bit layout an engine samples
 * without publishing (the SiS's RGB555) is left as it was, and FourCC,
 * palette and luminance formats are not this rule's. Pure arithmetic.
 */
#ifndef VELOCITY9X_TEXFMT_H
#define VELOCITY9X_TEXFMT_H

#include "velocity9x/types.h"

/* The fields of a DDPIXELFORMAT the rule reads. */
struct v9x_texfmt {
    v9x_u32 flags;      /* DDPF_* */
    v9x_u32 bits;       /* dwRGBBitCount */
    v9x_u32 red;
    v9x_u32 green;
    v9x_u32 blue;
    v9x_u32 alpha;      /* dwRGBAlphaBitMask */
};

/*
 * Non-zero when want is a 32-bit RGB layout that matches none of the
 * count published formats. Zero when count is 0 (no 3D engine has
 * published a list, so nothing is judged) or want is anything else.
 */
v9x_u32 v9x_texfmt_unpublished_32(const struct v9x_texfmt *want,
                                  const struct v9x_texfmt *published,
                                  v9x_u32 count);

#endif
