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

/*
 * Does this fragment survive the depth test?
 *
 * The default arm is ALWAYS rather than a fallthrough, and it is the same
 * decision `v9x_d3d_z_compare` makes in the ViRGE engine for the same reason:
 * NEVER is a legal value at the low end of the range, so an unrecognised
 * function that fell through to it would discard every pixel and render black
 * with nothing anywhere to say why. Drawing something wrong is a bug someone
 * can see; drawing nothing looks like a different bug entirely.
 */
static int v9x_d3d_raster_depth_passes(v9x_u32 compare, v9x_s32 value,
                                       v9x_s32 stored)
{
    switch (compare) {
    case V9X_D3D_RASTER_CMP_NEVER:
        return 0;
    case V9X_D3D_RASTER_CMP_LESS:
        return value < stored;
    case V9X_D3D_RASTER_CMP_EQUAL:
        return value == stored;
    case V9X_D3D_RASTER_CMP_LESSEQUAL:
        return value <= stored;
    case V9X_D3D_RASTER_CMP_GREATER:
        return value > stored;
    case V9X_D3D_RASTER_CMP_NOTEQUAL:
        return value != stored;
    case V9X_D3D_RASTER_CMP_GREATEREQUAL:
        return value >= stored;
    default:
        return 1;
    }
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

int v9x_d3d_raster_depth_valid(const V9X_D3D_RASTER_DEPTH *depth,
                               const V9X_D3D_RASTER_TARGET *target)
{
    if (depth == 0 || depth->pixels == 0) {
        return 0;
    }
    if (!v9x_d3d_raster_target_valid(target)) {
        return 0;
    }
    /* The depth buffer covers the same pixels as the colour buffer, so its
     * row is the colour buffer's width - not its own, which it does not
     * carry. A pitch short of that walks into the next scanline. */
    if (depth->pitch < target->width * 2ul) {
        return 0;
    }
    return 1;
}

int v9x_d3d_raster_texture_valid(const V9X_D3D_RASTER_TEXTURE *texture)
{
    v9x_u32 bit;

    if (texture == 0 || texture->pixels == 0) {
        return 0;
    }
    if (texture->format != V9X_D3D_RASTER_TEXFMT_ARGB1555 &&
        texture->format != V9X_D3D_RASTER_TEXFMT_ARGB4444) {
        return 0;
    }
    if (texture->filter != V9X_D3D_RASTER_FILTER_POINT &&
        texture->filter != V9X_D3D_RASTER_FILTER_LINEAR) {
        return 0;
    }
    if (texture->blend != V9X_D3D_RASTER_BLEND_DECAL &&
        texture->blend != V9X_D3D_RASTER_BLEND_MODULATE) {
        return 0;
    }
    if (texture->size < V9X_D3D_RASTER_TEXTURE_SIZE_MIN ||
        texture->size > V9X_D3D_RASTER_TEXTURE_SIZE_MAX) {
        return 0;
    }
    /* Power of two, checked rather than assumed: the sampler wraps its texel
     * index with size - 1 as a mask, and on a non-power-of-two size that mask
     * indexes outside the surface instead of looking wrong. */
    bit = 1ul;
    while (bit < texture->size) {
        bit <<= 1;
    }
    if (bit != texture->size) {
        return 0;
    }
    if (texture->pitch < texture->size * 2ul) {
        return 0;
    }
    return 1;
}

/* Five bits to eight, replicating the high bits into the low ones so that 31
 * reaches 255 rather than 248. Four bits to eight is exact - 17 is 255/15 - so
 * it needs no equivalent. */
static v9x_s32 v9x_d3d_raster_expand5(v9x_u32 value)
{
    return (v9x_s32)((value << 3) | (value >> 2));
}

/*
 * One texel, decoded to three 0..255 channels.
 *
 * Alpha is read by neither format's arm. The sampler has no alpha blending
 * behind it, and describe_caps advertises no texture alpha to match - a
 * channel decoded and discarded would be the beginning of exactly the
 * advertise-then-ignore pattern this driver has paid for twice.
 */
static void v9x_d3d_raster_texel(const V9X_D3D_RASTER_TEXTURE *texture,
                                 v9x_s32 x, v9x_s32 y,
                                 v9x_s32 *red, v9x_s32 *green, v9x_s32 *blue)
{
    const v9x_u16 *row = (const v9x_u16 *)((const v9x_u8 *)texture->pixels +
                                           (v9x_u32)y * texture->pitch);
    v9x_u32 texel = (v9x_u32)row[x];

    if (texture->format == V9X_D3D_RASTER_TEXFMT_ARGB4444) {
        *red = (v9x_s32)(((texel >> 8) & 0x0ful) * 17ul);
        *green = (v9x_s32)(((texel >> 4) & 0x0ful) * 17ul);
        *blue = (v9x_s32)((texel & 0x0ful) * 17ul);
        return;
    }
    *red = v9x_d3d_raster_expand5((texel >> 10) & 0x1ful);
    *green = v9x_d3d_raster_expand5((texel >> 5) & 0x1ful);
    *blue = v9x_d3d_raster_expand5(texel & 0x1ful);
}

/*
 * Sample the texture at a normalised coordinate, point or bilinear.
 *
 * Every product here is bounded and the bounds are worth stating, because they
 * are the reason the coordinate range is what it is. u is at most 65535 and
 * size at most 512, so u * size is at most 33,553,920. The bilinear arm adds
 * a whole texture's worth of bias to keep the half-texel offset from going
 * negative - an arithmetic right shift of a negative value is implementation
 * defined, and the wrap mask below would then index backwards off the surface
 * - which doubles it to 67,107,840. The weighted sum of four texels is at most
 * 255 * 256 * 256, which is 16,711,680.
 */
static void v9x_d3d_raster_sample(const V9X_D3D_RASTER_TEXTURE *texture,
                                  v9x_s32 u, v9x_s32 v,
                                  v9x_s32 *red, v9x_s32 *green, v9x_s32 *blue)
{
    v9x_s32 size = (v9x_s32)texture->size;
    v9x_s32 mask = size - 1l;
    v9x_s32 su = u * size;
    v9x_s32 sv = v * size;

    if (texture->filter == V9X_D3D_RASTER_FILTER_LINEAR) {
        v9x_s32 bias = size << 16;
        v9x_s32 bu = su + bias - 32768l;
        v9x_s32 bv = sv + bias - 32768l;
        v9x_s32 x0 = (bu >> 16) & mask;
        v9x_s32 y0 = (bv >> 16) & mask;
        v9x_s32 x1 = (x0 + 1l) & mask;
        v9x_s32 y1 = (y0 + 1l) & mask;
        v9x_s32 fu = (bu >> 8) & 0xffl;
        v9x_s32 fv = (bv >> 8) & 0xffl;
        v9x_s32 w00 = (256l - fu) * (256l - fv);
        v9x_s32 w10 = fu * (256l - fv);
        v9x_s32 w01 = (256l - fu) * fv;
        v9x_s32 w11 = fu * fv;
        v9x_s32 r00, g00, b00, r10, g10, b10, r01, g01, b01, r11, g11, b11;

        v9x_d3d_raster_texel(texture, x0, y0, &r00, &g00, &b00);
        v9x_d3d_raster_texel(texture, x1, y0, &r10, &g10, &b10);
        v9x_d3d_raster_texel(texture, x0, y1, &r01, &g01, &b01);
        v9x_d3d_raster_texel(texture, x1, y1, &r11, &g11, &b11);
        *red = (r00 * w00 + r10 * w10 + r01 * w01 + r11 * w11) >> 16;
        *green = (g00 * w00 + g10 * w10 + g01 * w01 + g11 * w11) >> 16;
        *blue = (b00 * w00 + b10 * w10 + b01 * w01 + b11 * w11) >> 16;
        return;
    }

    v9x_d3d_raster_texel(texture, (su >> 16) & mask, (sv >> 16) & mask,
                         red, green, blue);
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
    /* Depth is the widest thing interpolated here and the one that decides
     * the lerp's headroom: 65535 * 32752 is 2,146,631,520, which is 852,127
     * short of the largest signed 32-bit integer. That margin is the reason
     * V9X_D3D_RASTER_DIMENSION_MAX is 2048 and not 4096, and a target one
     * pixel wider than the cap would exhaust it here rather than in the
     * coordinate arithmetic that motivated the cap in the first place. */
    result->z = v9x_d3d_raster_lerp(from->z, to->z, offset, span);
    result->u = v9x_d3d_raster_lerp(from->u, to->u, offset, span);
    result->v = v9x_d3d_raster_lerp(from->v, to->v, offset, span);
    result->red = v9x_d3d_raster_lerp(from->red, to->red, offset, span);
    result->green = v9x_d3d_raster_lerp(from->green, to->green, offset, span);
    result->blue = v9x_d3d_raster_lerp(from->blue, to->blue, offset, span);
    return 1;
}

/*
 * Fill one scanline between two edge crossings.
 *
 * Every gradient is per subpixel and the walk is per pixel, which is the
 * arrangement that keeps every intermediate inside 32 bits. A step is
 * (delta << bits) / width, so a narrow span makes it large - but the number of
 * pixel centres inside that span shrinks in exactly the same proportion,
 * because centres are sixteen subpixels apart and all of them lie within the
 * span. The product is therefore bounded by delta << bits whatever the width
 * is: 255 << 16 for a colour channel, 65535 << 8 for depth, and those two are
 * the same number for the same reason the two shift counts differ. The same
 * argument covers the initial partial step: when the first centre is more than
 * the span's width past its left edge there are no centres inside it at all,
 * and the early return below has already taken it.
 */
static void v9x_d3d_raster_span(const V9X_D3D_RASTER_TARGET *target,
                                const V9X_D3D_RASTER_DEPTH *depth,
                                const V9X_D3D_RASTER_TEXTURE *texture,
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
    v9x_s32 z_step = 0l;
    v9x_s32 u_step = 0l;
    v9x_s32 v_step = 0l;
    v9x_s32 red;
    v9x_s32 green;
    v9x_s32 blue;
    v9x_s32 z;
    v9x_s32 u;
    v9x_s32 v;
    v9x_s32 offset;
    v9x_s32 column;
    v9x_u16 *pixels;
    v9x_u16 *depths = 0;

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
        z_step = ((right->z - left->z) << V9X_D3D_RASTER_DEPTH_BITS) / width;
        /* Texture coordinates share the depth interpolator's eight fractional
         * bits, and for the same reason: both run to 65535, and sixteen would
         * put the step alone outside a signed 32-bit integer. */
        u_step = ((right->u - left->u) << V9X_D3D_RASTER_DEPTH_BITS) / width;
        v_step = ((right->v - left->v) << V9X_D3D_RASTER_DEPTH_BITS) / width;
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
    z = (left->z << V9X_D3D_RASTER_DEPTH_BITS) + z_step * offset;
    u = (left->u << V9X_D3D_RASTER_DEPTH_BITS) + u_step * offset;
    v = (left->v << V9X_D3D_RASTER_DEPTH_BITS) + v_step * offset;
    red_step <<= V9X_D3D_RASTER_SUBPIXEL_BITS;
    green_step <<= V9X_D3D_RASTER_SUBPIXEL_BITS;
    blue_step <<= V9X_D3D_RASTER_SUBPIXEL_BITS;
    z_step <<= V9X_D3D_RASTER_SUBPIXEL_BITS;
    u_step <<= V9X_D3D_RASTER_SUBPIXEL_BITS;
    v_step <<= V9X_D3D_RASTER_SUBPIXEL_BITS;

    pixels = (v9x_u16 *)((v9x_u8 *)target->pixels +
                         (v9x_u32)row * target->pitch);
    if (depth != 0) {
        depths = (v9x_u16 *)((v9x_u8 *)depth->pixels +
                             (v9x_u32)row * depth->pitch);
    }
    for (column = first; column < last; ++column) {
        int visible = 1;

        if (depths != 0) {
            /* Clamped before the comparison, not after: the interpolator can
             * land a fraction of a level outside the endpoints, and a depth
             * that wrapped would compare against the wrong end of the buffer
             * rather than merely being one level out. */
            v9x_s32 fragment = z >> V9X_D3D_RASTER_DEPTH_BITS;

            if (fragment < 0l) {
                fragment = 0l;
            }
            if (fragment > V9X_D3D_RASTER_DEPTH_MAX) {
                fragment = V9X_D3D_RASTER_DEPTH_MAX;
            }
            visible = v9x_d3d_raster_depth_passes(depth->compare, fragment,
                                                  (v9x_s32)depths[column]);
            if (visible && depth->write != 0ul) {
                depths[column] = (v9x_u16)fragment;
            }
        }

        if (visible) {
            v9x_s32 out_red = red >> V9X_D3D_RASTER_COLOUR_BITS;
            v9x_s32 out_green = green >> V9X_D3D_RASTER_COLOUR_BITS;
            v9x_s32 out_blue = blue >> V9X_D3D_RASTER_COLOUR_BITS;

            if (texture != 0) {
                v9x_s32 tex_red;
                v9x_s32 tex_green;
                v9x_s32 tex_blue;
                v9x_s32 texel_u = u >> V9X_D3D_RASTER_DEPTH_BITS;
                v9x_s32 texel_v = v >> V9X_D3D_RASTER_DEPTH_BITS;

                /* Clamped, not wrapped, and the caps say so. One repeat is all
                 * the coordinate range holds - see V9X_D3D_RASTER_TEXCOORD_MAX
                 * - so a coordinate that has drifted a fraction past either end
                 * takes the edge texel rather than reappearing at the far
                 * side. */
                if (texel_u < 0l) {
                    texel_u = 0l;
                }
                if (texel_u > V9X_D3D_RASTER_TEXCOORD_MAX) {
                    texel_u = V9X_D3D_RASTER_TEXCOORD_MAX;
                }
                if (texel_v < 0l) {
                    texel_v = 0l;
                }
                if (texel_v > V9X_D3D_RASTER_TEXCOORD_MAX) {
                    texel_v = V9X_D3D_RASTER_TEXCOORD_MAX;
                }
                v9x_d3d_raster_sample(texture, texel_u, texel_v,
                                      &tex_red, &tex_green, &tex_blue);
                if (texture->blend == V9X_D3D_RASTER_BLEND_MODULATE) {
                    /* Clamped first: the interpolator's endpoints can sit a
                     * fraction outside 0..255, and a negative factor here
                     * would brighten rather than darken. */
                    if (out_red < 0l) {
                        out_red = 0l;
                    }
                    if (out_red > 255l) {
                        out_red = 255l;
                    }
                    if (out_green < 0l) {
                        out_green = 0l;
                    }
                    if (out_green > 255l) {
                        out_green = 255l;
                    }
                    if (out_blue < 0l) {
                        out_blue = 0l;
                    }
                    if (out_blue > 255l) {
                        out_blue = 255l;
                    }
                    out_red = (tex_red * out_red + 127l) / 255l;
                    out_green = (tex_green * out_green + 127l) / 255l;
                    out_blue = (tex_blue * out_blue + 127l) / 255l;
                } else {
                    out_red = tex_red;
                    out_green = tex_green;
                    out_blue = tex_blue;
                }
            }

            pixels[column] = v9x_d3d_raster_rgb565(out_red, out_green,
                                                   out_blue);
        }

        /* Stepped for every pixel, drawn or not. A failed depth test skips
         * the write, not the interpolation - advancing only on visible pixels
         * would tilt the gradient behind anything occluding the span. */
        red += red_step;
        green += green_step;
        blue += blue_step;
        z += z_step;
        u += u_step;
        v += v_step;
    }
}

int v9x_d3d_raster_triangle(const V9X_D3D_RASTER_TARGET *target,
                            const V9X_D3D_RASTER_DEPTH *depth,
                            const V9X_D3D_RASTER_TEXTURE *texture,
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
    /* A null depth pointer is "no depth"; a non-null one that fails its own
     * check is a caller error and is refused. Those must not look alike -
     * silently dropping the depth test would render the scene in submission
     * order, which is the exact defect this driver already shipped once with
     * the capability advertised. */
    if (depth != 0 && !v9x_d3d_raster_depth_valid(depth, target)) {
        return 0;
    }
    if (texture != 0 && !v9x_d3d_raster_texture_valid(texture)) {
        return 0;
    }
    for (index = 0ul; index < 3ul; ++index) {
        if (vertices[index].x < 0l ||
            vertices[index].x > V9X_D3D_RASTER_COORD_MAX ||
            vertices[index].y < 0l ||
            vertices[index].y > V9X_D3D_RASTER_COORD_MAX ||
            vertices[index].z < 0l ||
            vertices[index].z > V9X_D3D_RASTER_DEPTH_MAX ||
            vertices[index].u < 0l ||
            vertices[index].u > V9X_D3D_RASTER_TEXCOORD_MAX ||
            vertices[index].v < 0l ||
            vertices[index].v > V9X_D3D_RASTER_TEXCOORD_MAX) {
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
            v9x_d3d_raster_span(target, depth, texture, row, &along, &across);
        } else {
            v9x_d3d_raster_span(target, depth, texture, row, &across, &along);
        }
    }
    return 1;
}
