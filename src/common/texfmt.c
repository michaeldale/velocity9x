/*
 * Unpublished 32-bit texture layouts: see include\velocity9x\texfmt.h.
 */
#include "velocity9x/texfmt.h"

/* DDPIXELFORMAT flags (ddraw.h). */
#define V9X_TEXFMT_ALPHAPIXELS  0x00000001ul
#define V9X_TEXFMT_FOURCC       0x00000004ul
/* PALETTEINDEXED4 8h, TO8 10h, 8 20h, 1 800h, 2 1000h. */
#define V9X_TEXFMT_PALETTE      0x00001838ul
#define V9X_TEXFMT_RGB          0x00000040ul
#define V9X_TEXFMT_LUMINANCE    0x00020000ul
#define V9X_TEXFMT_BUMP         0x00080000ul /* BUMPDUDV */
#define V9X_TEXFMT_NOT_RGB      (V9X_TEXFMT_FOURCC | V9X_TEXFMT_PALETTE | \
                                 V9X_TEXFMT_LUMINANCE | V9X_TEXFMT_BUMP)
#define V9X_TEXFMT_BITS_32      32ul

/* The same layout: bit count, the three colour masks, the alpha mask and
 * whether alpha is present. A published ARGB8888 is not an XRGB8888. */
static v9x_u32 v9x_texfmt_same(const struct v9x_texfmt *a,
                               const struct v9x_texfmt *b)
{
    return a->bits == b->bits && a->red == b->red &&
           a->green == b->green && a->blue == b->blue &&
           (a->flags & V9X_TEXFMT_ALPHAPIXELS) ==
               (b->flags & V9X_TEXFMT_ALPHAPIXELS) &&
           ((a->flags & V9X_TEXFMT_ALPHAPIXELS) == 0ul ||
            a->alpha == b->alpha);
}

v9x_u32 v9x_texfmt_unpublished_32(const struct v9x_texfmt *want,
                                  const struct v9x_texfmt *published,
                                  v9x_u32 count)
{
    v9x_u32 index;

    if (want == 0 || published == 0 || count == 0ul) {
        return 0ul;
    }
    if ((want->flags & V9X_TEXFMT_RGB) == 0ul ||
        (want->flags & V9X_TEXFMT_NOT_RGB) != 0ul ||
        want->bits != V9X_TEXFMT_BITS_32) {
        return 0ul;
    }
    for (index = 0ul; index < count; ++index) {
        if (v9x_texfmt_same(want, &published[index])) {
            return 0ul;
        }
    }
    return 1ul;
}
