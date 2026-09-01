/*
 * Gouraud triangle rasterisation, integer only.
 *
 * See d3d_raster.h for why this is a leaf translation unit and why nothing
 * here is a float.
 *
 * The shape is the ordinary one: sort the three vertices by y, walk the
 * scanlines whose centre falls inside the triangle, and for each one find
 * where the long edge and the relevant short edge cross that centre. Two
 * things about it are worth stating, because both are places a rasterizer
 * usually goes wrong quietly.
 *
 * The first is the coverage rule. A pixel belongs to the triangle when its
 * centre - (x + 0.5, y + 0.5) - is inside, and both the vertical and the
 * horizontal interval are half-open: the low end included, the high end
 * excluded. That is what makes two triangles sharing an edge cover every pixel
 * along it exactly once. Rounding the extents outwards instead draws the seam
 * twice, which is invisible until something blends; rounding inwards leaves a
 * one-pixel crack, which is visible immediately and looks like a maths bug
 * somewhere else entirely.
 *
 * The second is that every edge crossing is computed from the edge's two
 * endpoints rather than stepped from the previous scanline. A stepped
 * rasterizer needs a reciprocal slope, which needs either a wider intermediate
 * than 32 bits or a precision compromise that accumulates down the triangle.
 * Recomputing costs two divides per scanline and has no error to accumulate,
 * and mode 2 is the mode whose stated trade is "slow, correct".
 */
#include "d3d_raster.h"

/*
 * The colour interpolators carry 16 fractional bits. Chosen against the range
 * rather than by habit: a channel is 0..255, so a 16.16 value fits in
 * 24 bits and every product formed from one below has eight bits of headroom.
 */
#define V9X_D3D_RASTER_COLOUR_BITS 16

/*
 * The first pixel index whose centre is at or beyond a 28.4 coordinate.
 *
 * Pixel i covers [i, i+1) and its centre sits at (i << 4) + 8, so this is
 * ceil((edge - 8) / 16), written as a shift. The shift is only correct on a
 * non-negative operand, which is why v9x_d3d_raster_triangle refuses a
 * negative coordinate before anything reaches here: edge + 7 is then at worst
 * 7, and every value derived from an edge below is an interpolation between
 * two non-negative endpoints.
 */
static v9x_s32 v9x_d3d_raster_first_centre(v9x_s32 edge)
{
    return (edge + (V9X_D3D_RASTER_SUBPIXEL_ONE - 1l -
                    V9X_D3D_RASTER_SUBPIXEL_HALF)) >>
           V9X_D3D_RASTER_SUBPIXEL_BITS;
}

/*
 * from + (to - from) * numerator / denominator, with no negative intermediate.
 *
 * The weighted form rather than the difference form, and not for style: C89
 * leaves the direction of division with a negative operand implementation
 * defined, and a colour or a coordinate that rounds the other way on a
 * different compiler is exactly the kind of difference that shows up as a
 * one-pixel disagreement between the host test and the driver. Both weights
 * are non-negative here and they sum to the denominator, so the numerator is
 * bounded by max(from, to) * denominator - at most 32752 * 32752, which is
 * 1,072,693,504 and fits.
 *
 * Callers guarantee denominator > 0 and 0 <= numerator <= denominator.
 */
static v9x_s32 v9x_d3d_raster_lerp(v9x_s32 from, v9x_s32 to,
                                   v9x_s32 numerator, v9x_s32 denominator)
{
    return (from * (denominator - numerator) + to * numerator) / denominator;
}

v9x_u16 v9x_d3d_raster_rgb565(v9x_s32 red, v9x_s32 green, v9x_s32 blue)
{
    /* Saturating at both ends, because the span interpolator's last pixel can
     * land a fraction of a level outside the endpoint colours and a wrapped
     * channel is a bright speck at the end of every span. */
    if (red < 0l) {
        red = 0l;
    }
    if (red > 255l) {
        red = 255l;
    }
    if (green < 0l) {
        green = 0l;
    }
    if (green > 255l) {
        green = 255l;
    }
    if (blue < 0l) {
        blue = 0l;
    }
    if (blue > 255l) {
        blue = 255l;
    }
    return (v9x_u16)(((v9x_u16)(red & 0xf8l) << 8) |
                     ((v9x_u16)(green & 0xfcl) << 3) |
                     ((v9x_u16)(blue & 0xf8l) >> 3));
}

int v9x_d3d_raster_target_valid(const V9X_D3D_RASTER_TARGET *target)
{
    if (target == 0 || target->pixels == 0) {
        return 0;
    }
    if (target->width == 0ul || target->height == 0ul) {
        return 0;
    }
    if (target->width > V9X_D3D_RASTER_DIMENSION_MAX ||
        target->height > V9X_D3D_RASTER_DIMENSION_MAX) {
        return 0;
    }
    /* Two bytes per pixel, and the row has to fit in the pitch. A pitch
     * narrower than the width is the one target defect that would corrupt the
     * next row rather than fault. */
    if (target->pitch < target->width * 2ul) {
        return 0;
    }
    return 1;
}

/*
 * Where an edge crosses a scanline centre, and what colour it is there.
 *
 * Returns zero for a horizontal edge - nothing crosses it - which is also what
 * keeps the divide in the interpolator from being handed a zero denominator.
 * The caller's sort makes that unreachable for the edges it asks about, but a
 * division by zero here is a fault inside a display driver's draw path, so it
 * is answered rather than assumed.
 */
static int v9x_d3d_raster_edge_at(const V9X_D3D_RASTER_VERTEX *from,
                                  const V9X_D3D_RASTER_VERTEX *to,
                                  v9x_s32 sample,
                                  V9X_D3D_RASTER_VERTEX *result)
{
    v9x_s32 span = to->y - from->y;
    v9x_s32 offset = sample - from->y;

    if (span <= 0l) {
        return 0;
    }
    if (offset < 0l) {
        offset = 0l;
    }
    if (offset > span) {
        offset = span;
    }

    result->y = sample;
    result->x = v9x_d3d_raster_lerp(from->x, to->x, offset, span);
    result->red = v9x_d3d_raster_lerp(from->red, to->red, offset, span);
    result->green = v9x_d3d_raster_lerp(from->green, to->green, offset, span);
    result->blue = v9x_d3d_raster_lerp(from->blue, to->blue, offset, span);
    return 1;
}

/*
 * Fill one scanline between two edge crossings.
 *
 * The colour gradient is per subpixel and the walk is per pixel, which is the
 * arrangement that keeps every intermediate inside 32 bits. The step is
 * (delta << 16) / width, so a narrow span makes it large - but the number of
 * pixel centres inside that span shrinks in exactly the same proportion,
 * because centres are sixteen subpixels apart and all of them lie within the
 * span. Their product is therefore bounded by delta << 16, at most 255 << 16,
 * whatever the width is. The same argument covers the initial partial step:
 * when the first centre is more than the span's width past its left edge there
 * are no centres inside it at all, and the early return below has already
 * taken it.
 */
static void v9x_d3d_raster_span(const V9X_D3D_RASTER_TARGET *target,
                                v9x_s32 row,
                                const V9X_D3D_RASTER_VERTEX *left,
                                const V9X_D3D_RASTER_VERTEX *right)
{
    v9x_s32 width = right->x - left->x;
    v9x_s32 first = v9x_d3d_raster_first_centre(left->x);
    v9x_s32 last = v9x_d3d_raster_first_centre(right->x);
    v9x_s32 red_step = 0l;
    v9x_s32 green_step = 0l;
    v9x_s32 blue_step = 0l;
    v9x_s32 red;
    v9x_s32 green;
    v9x_s32 blue;
    v9x_s32 offset;
    v9x_s32 column;
    v9x_u16 *pixels;

    if (first < 0l) {
        first = 0l;
    }
    if (last > (v9x_s32)target->width) {
        last = (v9x_s32)target->width;
    }
    if (first >= last) {
        return;
    }

    if (width > 0l) {
        red_step = ((right->red - left->red) << V9X_D3D_RASTER_COLOUR_BITS) /
                   width;
        green_step = ((right->green - left->green) <<
                      V9X_D3D_RASTER_COLOUR_BITS) / width;
        blue_step = ((right->blue - left->blue) <<
                     V9X_D3D_RASTER_COLOUR_BITS) / width;
    }

    /* The colour at the first pixel centre, then one whole pixel per step. */
    offset = ((first << V9X_D3D_RASTER_SUBPIXEL_BITS) +
              V9X_D3D_RASTER_SUBPIXEL_HALF) - left->x;
    if (offset < 0l) {
        offset = 0l;
    }
    red = (left->red << V9X_D3D_RASTER_COLOUR_BITS) + red_step * offset;
    green = (left->green << V9X_D3D_RASTER_COLOUR_BITS) + green_step * offset;
    blue = (left->blue << V9X_D3D_RASTER_COLOUR_BITS) + blue_step * offset;
    red_step <<= V9X_D3D_RASTER_SUBPIXEL_BITS;
    green_step <<= V9X_D3D_RASTER_SUBPIXEL_BITS;
    blue_step <<= V9X_D3D_RASTER_SUBPIXEL_BITS;

    pixels = (v9x_u16 *)((v9x_u8 *)target->pixels +
                         (v9x_u32)row * target->pitch);
    for (column = first; column < last; ++column) {
        pixels[column] = v9x_d3d_raster_rgb565(
            red >> V9X_D3D_RASTER_COLOUR_BITS,
            green >> V9X_D3D_RASTER_COLOUR_BITS,
            blue >> V9X_D3D_RASTER_COLOUR_BITS);
        red += red_step;
        green += green_step;
        blue += blue_step;
    }
}

int v9x_d3d_raster_triangle(const V9X_D3D_RASTER_TARGET *target,
                            const V9X_D3D_RASTER_VERTEX *vertices)
{
    const V9X_D3D_RASTER_VERTEX *top;
    const V9X_D3D_RASTER_VERTEX *middle;
    const V9X_D3D_RASTER_VERTEX *bottom;
    const V9X_D3D_RASTER_VERTEX *swap;
    v9x_s32 first_row;
    v9x_s32 last_row;
    v9x_s32 row;
    v9x_u32 index;

    if (!v9x_d3d_raster_target_valid(target) || vertices == 0) {
        return 0;
    }
    for (index = 0ul; index < 3ul; ++index) {
        if (vertices[index].x < 0l ||
            vertices[index].x > V9X_D3D_RASTER_COORD_MAX ||
            vertices[index].y < 0l ||
            vertices[index].y > V9X_D3D_RASTER_COORD_MAX) {
            return 0;
        }
    }

    top = &vertices[0];
    middle = &vertices[1];
    bottom = &vertices[2];
    if (top->y > middle->y) {
        swap = top;
        top = middle;
        middle = swap;
    }
    if (middle->y > bottom->y) {
        swap = middle;
        middle = bottom;
        bottom = swap;
    }
    if (top->y > middle->y) {
        swap = top;
        top = middle;
        middle = swap;
    }

    first_row = v9x_d3d_raster_first_centre(top->y);
    last_row = v9x_d3d_raster_first_centre(bottom->y);
    if (first_row < 0l) {
        first_row = 0l;
    }
    if (last_row > (v9x_s32)target->height) {
        last_row = (v9x_s32)target->height;
    }

    for (row = first_row; row < last_row; ++row) {
        v9x_s32 sample = (row << V9X_D3D_RASTER_SUBPIXEL_BITS) +
                         V9X_D3D_RASTER_SUBPIXEL_HALF;
        V9X_D3D_RASTER_VERTEX along;
        V9X_D3D_RASTER_VERTEX across;

        /* The long edge spans the whole triangle; which short edge is opposite
         * it changes at the middle vertex, and the comparison is the same
         * half-open rule the scanline range uses. */
        if (!v9x_d3d_raster_edge_at(top, bottom, sample, &along)) {
            continue;
        }
        if (sample < middle->y) {
            if (!v9x_d3d_raster_edge_at(top, middle, sample, &across)) {
                continue;
            }
        } else {
            if (!v9x_d3d_raster_edge_at(middle, bottom, sample, &across)) {
                continue;
            }
        }

        if (along.x <= across.x) {
            v9x_d3d_raster_span(target, row, &along, &across);
        } else {
            v9x_d3d_raster_span(target, row, &across, &along);
        }
    }
    return 1;
}
