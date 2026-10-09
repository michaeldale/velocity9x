/*
 * Matrox MGA-2064W drawing engine: register values for solid fill, screen
 * copy and monochrome expansion (text).
 *
 * Pure policy, no I/O; see mga_engine.h. Every value is from the MGA-1064SG
 * Developer Specification (1997), whose 2D core is the 2064W's (p.1-2),
 * cross-checked against FreeBE/AF's Millennium driver; page numbers are the
 * document's own. docs\specifications\mga2064w-2d-engine.md has the field
 * decodes. The points no source settles for this chip - whether AR0 takes a
 * full address, the inclusive right edge of a blit - are inferences until
 * the guarded write probe measures them on the card.
 *
 * The destination of every operation is addressed in xy form with the
 * hardware linearising it: PITCH and YDSTORG are written per operation, so
 * any surface whose pitch the linearizer supports can be drawn into. The
 * copy source is addressed linearly through AR0, AR3 and AR5, which places
 * no constraint on its pitch.
 */
#include "velocity9x/mga_engine.h"

/*
 * DWGCTL (p.4-49..4-55). A fill is TRAP with solid, arzero, sgnzero and
 * shftzero set, atype RPL and bop C (source, i.e. the foreground colour):
 * the RECT row of section 5.5.5.2, p.5-35. RPL rather than BLK, because
 * block mode on the 1064SG is an SGRAM write that needs OPTION hardpwmsk
 * (p.4-50), and the 2064W has WRAM and a different OPTION register; no
 * source here says what BLK does on it.
 *
 * A copy is BITBLT with bltmod BFCOL, shftzero, atype RPL, bop C, and
 * sgnzero clear so SGN can set the scan direction: the XY row of section
 * 5.5.6.2, p.5-41. With sgnzero set it is p.5-58's 040C6008h.
 */
#define V9X_MGA_DWGCTL_FILL 0x000c7804ul
#define V9X_MGA_DWGCTL_COPY 0x040c4008ul

/* MACCESS pwidth<1:0> (p.4-64): 00 8 bpp, 01 16, 10 32. memreset and the
 * other bits stay 0, which p.4-64 asks for outside the reset sequence. */
#define V9X_MGA_PWIDTH_8  0ul
#define V9X_MGA_PWIDTH_16 1ul
#define V9X_MGA_PWIDTH_32 2ul

/* SGN (p.4-71): scanleft<0> right to left, sdy<2> bottom to top. */
#define V9X_MGA_SGN_SCANLEFT 0x00000001ul
#define V9X_MGA_SGN_SDY      0x00000004ul

/* CXBNDRY cxright<26:16>, cxleft<10:0>, both inclusive: the clip window is
 * never off (p.4-28), so it is opened to the 11-bit maximum. YTOP and YBOT
 * are linear pixel addresses that must be multiples of 32 (p.4-79, 4-83);
 * YBOT is the largest such in its 23 bits. */
#define V9X_MGA_X_LIMIT 2048ul
#define V9X_MGA_CXBNDRY_OPEN (((V9X_MGA_X_LIMIT - 1ul) << 16) | 0ul)
#define V9X_MGA_YBOT_OPEN 0x007fffe0ul

/* FXBNDRY's two edges and YDSTLEN's y are 16-bit fields (p.4-58, 4-81);
 * y is kept below the sign bit. */
#define V9X_MGA_Y_LIMIT 0x00008000ul
#define V9X_MGA_LENGTH_LIMIT 0x00010000ul

/* AR3 is 24 bits of pixel address (p.4-23); AR5 an 18-bit signed pitch
 * (p.4-25). */
#define V9X_MGA_AR3_LIMIT 0x01000000ul
#define V9X_MGA_AR5_LIMIT 0x00020000ul

/*
 * The pitches, in pixels, the linearizer supports with ylin = 0 (PITCH,
 * p.4-68). 512, 832 and 1664 are in the 1064SG's table and left out here:
 * FreeBE's Millennium pitch list omits all three, and this chip is the
 * Millennium.
 */
static const v9x_u32 v9x_mga_pitches[] = {
    640ul, 768ul, 800ul, 960ul, 1024ul, 1152ul, 1280ul, 1600ul, 1920ul,
    2048ul
};

/* PITCH and YDSTORG must be multiples of 64 pixels at 8 bpp and of 32 at 16
 * and 32 bpp (p.4-68, 4-82; stated as a block-mode restriction and obeyed
 * here anyway, since both pages say "must"). */
static v9x_u32 v9x_mga_origin_alignment(v9x_u32 bytes_per_pixel)
{
    return bytes_per_pixel == 1ul ? 64ul : 32ul;
}

static int v9x_mga_depth_ok(v9x_u32 bytes_per_pixel)
{
    return bytes_per_pixel == 1ul || bytes_per_pixel == 2ul ||
        bytes_per_pixel == 4ul;
}

static int v9x_mga_pitch_ok(v9x_u32 pitch_pixels, v9x_u32 bytes_per_pixel)
{
    v9x_u32 index;

    if ((pitch_pixels % v9x_mga_origin_alignment(bytes_per_pixel)) != 0ul) {
        return 0;
    }
    for (index = 0ul;
         index < (v9x_u32)(sizeof(v9x_mga_pitches) / sizeof(v9x_mga_pitches[0]));
         ++index) {
        if (v9x_mga_pitches[index] == pitch_pixels) {
            return 1;
        }
    }
    return 0;
}

/* FCOL holds the colour replicated across the dword at 8 and 16 bpp
 * (p.4-56). */
static v9x_u32 v9x_mga_color_field(v9x_u32 color, v9x_u32 bytes_per_pixel)
{
    if (bytes_per_pixel == 1ul) {
        color &= 0xfful;
        return color | (color << 8) | (color << 16) | (color << 24);
    }
    if (bytes_per_pixel == 2ul) {
        color &= 0xfffful;
        return color | (color << 16);
    }
    return color;
}

static void v9x_mga_put(struct v9x_mga_writes *writes, v9x_u32 offset,
                        v9x_u32 value)
{
    writes->offsets[writes->count] = offset;
    writes->values[writes->count] = value;
    ++writes->count;
}

/*
 * A rectangle inside one surface, and its byte extent inside VRAM. Returns
 * OK, INVALID_ARGUMENT for a rectangle the surface does not hold, or
 * UNSUPPORTED for one the engine's fields cannot express. Sizes are bounded
 * before anything is multiplied, so nothing overflows.
 */
static v9x_status v9x_mga_check_rect(v9x_u32 vram_bytes, v9x_u32 offset,
                                     v9x_u32 pitch_bytes,
                                     v9x_u32 bytes_per_pixel,
                                     v9x_u32 left, v9x_u32 top,
                                     v9x_u32 width, v9x_u32 height)
{
    v9x_u32 pitch_pixels;
    v9x_u32 end;

    if (width == 0ul || height == 0ul || pitch_bytes == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if ((pitch_bytes % bytes_per_pixel) != 0ul ||
        (offset % bytes_per_pixel) != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    pitch_pixels = pitch_bytes / bytes_per_pixel;
    if (left > pitch_pixels || width > pitch_pixels - left) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (left + width > V9X_MGA_X_LIMIT || top >= V9X_MGA_Y_LIMIT ||
        height >= V9X_MGA_LENGTH_LIMIT || top + height > V9X_MGA_Y_LIMIT) {
        return V9X_STATUS_UNSUPPORTED;
    }

    /* The byte after the last one touched: top + height and pitch are both
     * at most 32768 and 8192 here. */
    end = (top + height - 1ul) * pitch_bytes + (left + width) * bytes_per_pixel;
    if (offset >= vram_bytes || end > vram_bytes - offset) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    return V9X_STATUS_OK;
}

/*
 * PITCH and YDSTORG for a destination surface, or UNSUPPORTED for one the
 * linearizer cannot address. The clipper compares linear line addresses
 * against YBOT, so a last line beyond the open window would be clipped
 * without a word; that is refused here instead. The rectangle is already
 * bounded by v9x_mga_check_rect.
 */
static v9x_status v9x_mga_destination(v9x_u32 offset, v9x_u32 pitch_bytes,
                                      v9x_u32 bytes_per_pixel,
                                      v9x_u32 last_line,
                                      v9x_u32 *pitch_pixels,
                                      v9x_u32 *origin_pixels)
{
    *pitch_pixels = pitch_bytes / bytes_per_pixel;
    *origin_pixels = offset / bytes_per_pixel;
    if (!v9x_mga_pitch_ok(*pitch_pixels, bytes_per_pixel) ||
        (*origin_pixels % v9x_mga_origin_alignment(bytes_per_pixel)) != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (*origin_pixels > V9X_MGA_YBOT_OPEN ||
        last_line * *pitch_pixels > V9X_MGA_YBOT_OPEN - *origin_pixels) {
        return V9X_STATUS_UNSUPPORTED;
    }
    return V9X_STATUS_OK;
}

v9x_status v9x_mga_build_setup(v9x_u32 bytes_per_pixel,
                               struct v9x_mga_writes *writes)
{
    v9x_u32 pwidth;

    if (writes == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    writes->count = 0ul;
    if (bytes_per_pixel == 1ul) {
        pwidth = V9X_MGA_PWIDTH_8;
    } else if (bytes_per_pixel == 2ul) {
        pwidth = V9X_MGA_PWIDTH_16;
    } else if (bytes_per_pixel == 4ul) {
        pwidth = V9X_MGA_PWIDTH_32;
    } else {
        return V9X_STATUS_UNSUPPORTED;
    }

    /* The global initialisation list of section 5.5.3 (p.5-27), less PITCH
     * and YDSTORG, which every operation writes for its own destination,
     * and ZORG, which nothing here uses. */
    v9x_mga_put(writes, V9X_MGA_MACCESS, pwidth);
    v9x_mga_put(writes, V9X_MGA_PLNWT, 0xfffffffful);
    v9x_mga_put(writes, V9X_MGA_CXBNDRY, V9X_MGA_CXBNDRY_OPEN);
    v9x_mga_put(writes, V9X_MGA_YTOP, 0ul);
    v9x_mga_put(writes, V9X_MGA_YBOT, V9X_MGA_YBOT_OPEN);
    return V9X_STATUS_OK;
}

v9x_status v9x_mga_build_fill(const struct v9x_mga_fill *fill,
                              struct v9x_mga_writes *writes)
{
    v9x_u32 pitch_pixels;
    v9x_u32 origin_pixels;
    v9x_status status;

    if (fill == 0 || writes == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    writes->count = 0ul;
    if (!v9x_mga_depth_ok(fill->bytes_per_pixel)) {
        return V9X_STATUS_UNSUPPORTED;
    }
    status = v9x_mga_check_rect(fill->vram_bytes, fill->target_offset,
                                fill->pitch_bytes, fill->bytes_per_pixel,
                                fill->left, fill->top, fill->width,
                                fill->height);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    status = v9x_mga_destination(fill->target_offset, fill->pitch_bytes,
                                 fill->bytes_per_pixel,
                                 fill->top + fill->height - 1ul,
                                 &pitch_pixels, &origin_pixels);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    /*
     * The right edge is exclusive for a trapezoid: "the pixels at the top
     * and left edge are actually drawn ... while the bottom and right edges
     * exist just beyond the object's extents" (section 5.5.5.1, p.5-33).
     * YDSTLEN's y is relative to YDSTORG; its write starts the engine.
     */
    v9x_mga_put(writes, V9X_MGA_DWGCTL, V9X_MGA_DWGCTL_FILL);
    v9x_mga_put(writes, V9X_MGA_PITCH, pitch_pixels);
    v9x_mga_put(writes, V9X_MGA_YDSTORG, origin_pixels);
    v9x_mga_put(writes, V9X_MGA_FCOL,
                v9x_mga_color_field(fill->color, fill->bytes_per_pixel));
    v9x_mga_put(writes, V9X_MGA_FXBNDRY,
                ((fill->left + fill->width) << 16) | fill->left);
    v9x_mga_put(writes, V9X_MGA_YDSTLEN + V9X_MGA_GO,
                (fill->top << 16) | fill->height);
    return V9X_STATUS_OK;
}

v9x_status v9x_mga_build_copy(const struct v9x_mga_copy *copy,
                              struct v9x_mga_writes *writes)
{
    v9x_u32 bpp;
    v9x_u32 pitch_pixels;
    v9x_u32 origin_pixels;
    v9x_u32 source_pitch;
    v9x_u32 source_row;
    v9x_u32 source_top;
    v9x_u32 destination_top;
    v9x_u32 sgn = 0ul;
    v9x_u32 ar5;
    v9x_u32 last;
    int same_surface;
    v9x_status status;

    if (copy == 0 || writes == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    writes->count = 0ul;
    bpp = copy->bytes_per_pixel;
    if (!v9x_mga_depth_ok(bpp)) {
        return V9X_STATUS_UNSUPPORTED;
    }
    status = v9x_mga_check_rect(copy->vram_bytes, copy->source_offset,
                                copy->source_pitch_bytes, bpp,
                                copy->source_left, copy->source_top,
                                copy->width, copy->height);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    status = v9x_mga_check_rect(copy->vram_bytes, copy->destination_offset,
                                copy->destination_pitch_bytes, bpp,
                                copy->destination_left,
                                copy->destination_top, copy->width,
                                copy->height);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    status = v9x_mga_destination(copy->destination_offset,
                                 copy->destination_pitch_bytes, bpp,
                                 copy->destination_top + copy->height - 1ul,
                                 &pitch_pixels, &origin_pixels);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    source_pitch = copy->source_pitch_bytes / bpp;
    if (source_pitch >= V9X_MGA_AR5_LIMIT ||
        copy->source_offset / bpp >= V9X_MGA_AR3_LIMIT ||
        copy->vram_bytes / bpp > V9X_MGA_AR3_LIMIT) {
        return V9X_STATUS_UNSUPPORTED;
    }

    /*
     * Overlap. Within one surface the scan runs away from the overlap: bottom
     * to top when the destination is lower, right to left when it is on the
     * same rows and further right. Two different surfaces whose bytes
     * overlap would need the direction worked out in linear addresses; that
     * is declined rather than guessed.
     */
    same_surface = copy->source_offset == copy->destination_offset &&
        copy->source_pitch_bytes == copy->destination_pitch_bytes;
    if (!same_surface) {
        v9x_u32 s_start = copy->source_offset +
            copy->source_top * copy->source_pitch_bytes;
        v9x_u32 s_end = copy->source_offset +
            (copy->source_top + copy->height) * copy->source_pitch_bytes;
        v9x_u32 d_start = copy->destination_offset +
            copy->destination_top * copy->destination_pitch_bytes;
        v9x_u32 d_end = copy->destination_offset +
            (copy->destination_top + copy->height) *
            copy->destination_pitch_bytes;

        if (s_start < d_end && d_start < s_end) {
            return V9X_STATUS_UNSUPPORTED;
        }
    }

    source_top = copy->source_top;
    destination_top = copy->destination_top;
    ar5 = source_pitch;
    if (same_surface && copy->destination_top > copy->source_top) {
        source_top += copy->height - 1ul;
        destination_top += copy->height - 1ul;
        ar5 = (v9x_u32)(0ul - source_pitch);
        sgn |= V9X_MGA_SGN_SDY;
    }
    if (same_surface && copy->destination_top == copy->source_top &&
        copy->destination_left > copy->source_left) {
        sgn |= V9X_MGA_SGN_SCANLEFT;
    }

    /*
     * Source addresses are linear pixel addresses from the start of VRAM;
     * YDSTORG is not added to them (AR3, p.4-23). AR3 is where the scan
     * starts and AR0 the far end of the first line scanned (section
     * 5.5.6.1, p.5-40).
     */
    source_row = copy->source_offset / bpp + source_top * source_pitch;
    last = copy->width - 1ul;

    v9x_mga_put(writes, V9X_MGA_DWGCTL, V9X_MGA_DWGCTL_COPY);
    v9x_mga_put(writes, V9X_MGA_PITCH, pitch_pixels);
    v9x_mga_put(writes, V9X_MGA_YDSTORG, origin_pixels);
    v9x_mga_put(writes, V9X_MGA_SGN, sgn);
    /* The full two's-complement dword, not the documented 18 bits: FreeBE
     * and xf86-video-mga both write a negative pitch sign-extended, and
     * the masked form drew the bottom-up copy wrong in the 86Box guest
     * (V9XDDP OverlapPitchDownPixelOk=0, 2026-10-09). */
    v9x_mga_put(writes, V9X_MGA_AR5, ar5);
    if ((sgn & V9X_MGA_SGN_SCANLEFT) != 0ul) {
        v9x_mga_put(writes, V9X_MGA_AR0, source_row + copy->source_left);
        v9x_mga_put(writes, V9X_MGA_AR3,
                    source_row + copy->source_left + last);
    } else {
        v9x_mga_put(writes, V9X_MGA_AR0,
                    source_row + copy->source_left + last);
        v9x_mga_put(writes, V9X_MGA_AR3, source_row + copy->source_left);
    }
    /* A blit's right edge is inclusive, unlike a trapezoid's: FreeBE writes
     * x + w - 1 here and x + w for a fill. Unmeasured on this chip. */
    v9x_mga_put(writes, V9X_MGA_FXBNDRY,
                ((copy->destination_left + last) << 16) |
                copy->destination_left);
    v9x_mga_put(writes, V9X_MGA_YDSTLEN + V9X_MGA_GO,
                (destination_top << 16) | copy->height);
    return V9X_STATUS_OK;
}

/*
 * Expansion of a monochrome host bitmap: ILOAD with expansion (section
 * 5.5.7.3, p.5-50), the source linear (5.5.7.1, p.5-47), so the rows of
 * the DIB Engine's unpadded string bitmap run on as one stream padded only
 * at its end. The value is FreeBE's PutMonoImage for a linear source and
 * a replace mix, with RPL for its transparent case too, which p.5-50
 * permits; FreeBE uses BLK there, which this driver does not use.
 * BMONOWF is Windows bit order, the most significant bit of each byte
 * leftmost (MONO B, p.5-13), which is the DIB Engine's.
 */
#define V9X_MGA_DWGCTL_EXPAND      0x080c6089ul
#define V9X_MGA_DWGCTL_TRANSC      0x40000000ul

/* AR0 holds 18 bits (p.4-20); for a linear ILOAD it is the pixel count
 * less one. */
#define V9X_MGA_AR0_LIMIT          0x00040000ul

/* OPMODE dmamod <3:2> (p.4-66): DMA BLIT write, the mode DMAWIN data must
 * be in for an ILOAD. Reset value 0 is general purpose, in which the data
 * would be read as register indices. */
#define V9X_MGA_OPMODE_DMAMOD_MASK 0x0000000cul
#define V9X_MGA_OPMODE_DMA_BLIT    0x00000004ul

v9x_status v9x_mga_build_expand(const struct v9x_mga_expand *expand,
                                struct v9x_mga_writes *writes)
{
    v9x_u32 pitch_pixels;
    v9x_u32 origin_pixels;
    v9x_status status;

    if (expand == 0 || writes == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    writes->count = 0ul;
    if (!v9x_mga_depth_ok(expand->bytes_per_pixel)) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (expand->width == 0ul || expand->height == 0ul ||
        expand->clip_left < expand->left ||
        expand->clip_right < expand->clip_left ||
        expand->clip_right - expand->left >= expand->width) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* Both factors are bounded before they are multiplied. */
    if (expand->width > V9X_MGA_AR0_LIMIT ||
        expand->height > V9X_MGA_AR0_LIMIT ||
        expand->width * expand->height > V9X_MGA_AR0_LIMIT ||
        expand->left + expand->width > V9X_MGA_X_LIMIT) {
        return V9X_STATUS_UNSUPPORTED;
    }

    /* Only the clipped columns are written, so they are what the surface
     * must hold; the bitmap's padding beyond them is never drawn. */
    status = v9x_mga_check_rect(expand->vram_bytes, expand->target_offset,
                                expand->pitch_bytes,
                                expand->bytes_per_pixel, expand->clip_left,
                                expand->top,
                                expand->clip_right - expand->clip_left + 1ul,
                                expand->height);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    status = v9x_mga_destination(expand->target_offset, expand->pitch_bytes,
                                 expand->bytes_per_pixel,
                                 expand->top + expand->height - 1ul,
                                 &pitch_pixels, &origin_pixels);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    v9x_mga_put(writes, V9X_MGA_DWGCTL,
                V9X_MGA_DWGCTL_EXPAND |
                (expand->transparent != 0ul ? V9X_MGA_DWGCTL_TRANSC : 0ul));
    v9x_mga_put(writes, V9X_MGA_PITCH, pitch_pixels);
    v9x_mga_put(writes, V9X_MGA_YDSTORG, origin_pixels);
    v9x_mga_put(writes, V9X_MGA_FCOL,
                v9x_mga_color_field(expand->foreground,
                                    expand->bytes_per_pixel));
    v9x_mga_put(writes, V9X_MGA_BCOL,
                v9x_mga_color_field(expand->background,
                                    expand->bytes_per_pixel));
    /* AR3 and AR5 "must be 0" (5.5.7.1); a copy leaves AR5 a pitch. */
    v9x_mga_put(writes, V9X_MGA_AR0,
                expand->width * expand->height - 1ul);
    v9x_mga_put(writes, V9X_MGA_AR3, 0ul);
    v9x_mga_put(writes, V9X_MGA_AR5, 0ul);
    v9x_mga_put(writes, V9X_MGA_CXBNDRY,
                (expand->clip_right << 16) | expand->clip_left);
    /* Inclusive right edge, as FreeBE writes it for this operation. */
    v9x_mga_put(writes, V9X_MGA_FXBNDRY,
                ((expand->left + expand->width - 1ul) << 16) | expand->left);
    v9x_mga_put(writes, V9X_MGA_YDSTLEN + V9X_MGA_GO,
                (expand->top << 16) | expand->height);
    return V9X_STATUS_OK;
}

/* Total = INT((psiz * width * Nlines + 31) / 32) for a linear source,
 * psiz 1 for BMONOWF (section 5.5.7, Table 5-2). */
v9x_u32 v9x_mga_expand_dwords(const struct v9x_mga_expand *expand)
{
    if (expand == 0 || expand->width > V9X_MGA_AR0_LIMIT ||
        expand->height > V9X_MGA_AR0_LIMIT) {
        return 0ul;
    }
    return (expand->width * expand->height + 31ul) / 32ul;
}

v9x_u32 v9x_mga_opmode_for_iload(v9x_u32 opmode)
{
    return (opmode & ~V9X_MGA_OPMODE_DMAMOD_MASK) | V9X_MGA_OPMODE_DMA_BLIT;
}

int v9x_mga_surface_ok(v9x_u32 pitch_bytes, v9x_u32 offset,
                       v9x_u32 bytes_per_pixel)
{
    v9x_u32 pitch_pixels;
    v9x_u32 origin_pixels;

    if (!v9x_mga_depth_ok(bytes_per_pixel) || pitch_bytes == 0ul ||
        (pitch_bytes % bytes_per_pixel) != 0ul ||
        (offset % bytes_per_pixel) != 0ul) {
        return 0;
    }
    return v9x_mga_destination(offset, pitch_bytes, bytes_per_pixel, 0ul,
                               &pitch_pixels, &origin_pixels) ==
        V9X_STATUS_OK;
}

/*
 * CRTCEXT0 (p.4-130): startadd<19:16> in bits 3:0, the offset's bits 9:8
 * in 5:4, interlace in 7.
 */
#define V9X_MGA_EXT0_START_MASK     0x0ful
#define V9X_MGA_EXT0_OFFSET_MASK    0x30ul
#define V9X_MGA_EXT0_OFFSET_SHIFT   4u
#define V9X_MGA_EXT0_INTERLACE      0x80ul
#define V9X_MGA_START_LIMIT         0x00100000ul    /* 20 bits */

/*
 * The documents disagree about the 2064W's units, so the mode's own CRTC
 * decides. The 1064SG, whose 2D core is the 2064W's, programs the offset
 * (CRTC13 plus CRTCEXT0<5:4>) as pitch * bpp / 128, 16 bytes a unit, and
 * startadd in 8-byte units (section 5.6.5, p.5-66). FreeBE's Millennium
 * path (0519 only) uses 8 and 4. In VGA byte addressing a start unit is
 * half an offset unit under both, so whichever the BIOS set, the start
 * follows from it. A8U4I5's BIOS at 1024x768x16 set 128 for 2048 bytes,
 * the 1064SG's rule (V9XTIME, 2026-10-09). Any other ratio is refused:
 * nothing here says what it would mean.
 */
v9x_status v9x_mga_display_start(v9x_u32 byte_offset, v9x_u32 pitch_bytes,
                                 v9x_u32 crtc13, v9x_u32 crtcext0,
                                 v9x_u32 vram_bytes, v9x_u32 *start)
{
    v9x_u32 offset_units;
    v9x_u32 unit;
    v9x_u32 value;

    if (start == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *start = 0ul;
    if (byte_offset >= vram_bytes) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if ((crtcext0 & V9X_MGA_EXT0_INTERLACE) != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    offset_units = (crtc13 & 0xfful) |
        (((crtcext0 & V9X_MGA_EXT0_OFFSET_MASK) >> V9X_MGA_EXT0_OFFSET_SHIFT)
         << 8);
    if (offset_units == 0ul || (pitch_bytes % offset_units) != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    unit = pitch_bytes / offset_units;
    if (unit != 16ul && unit != 8ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    unit /= 2ul;
    if ((byte_offset % unit) != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    value = byte_offset / unit;
    if (value >= V9X_MGA_START_LIMIT) {
        return V9X_STATUS_UNSUPPORTED;
    }
    *start = value;
    return V9X_STATUS_OK;
}

v9x_u32 v9x_mga_crtcext0_with_start(v9x_u32 crtcext0, v9x_u32 start)
{
    return (crtcext0 & ~V9X_MGA_EXT0_START_MASK & 0xfful) |
        ((start >> 16) & V9X_MGA_EXT0_START_MASK);
}

v9x_u32 v9x_mga_status_busy(v9x_u32 status)
{
    return status & V9X_MGA_STATUS_BUSY;
}

v9x_u32 v9x_mga_fifo_free(v9x_u32 fifostatus)
{
    return fifostatus & V9X_MGA_FIFO_COUNT_MASK;
}
