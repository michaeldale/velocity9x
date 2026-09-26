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
 * The second is that edge stepping carries the exact division remainder.
 * There is no rounded reciprocal slope and no accumulated precision loss:
 * each crossing equals floor(from + delta * offset / span), including falling
 * gradients. Divisions occur at edge setup, not on every scanline.
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
 * floor(from + (to - from) * numerator / denominator), divided before it is
 * multiplied so that the interpolated quantity's range does not bound it.
 *
 * The obvious form is (from * (d - n) + to * n) / d, and this file used it
 * until texture wrapping needed a coordinate wider than one repeat. Its
 * numerator is bounded by max(from, to) * d, so with d up to 32752 subpixels
 * of triangle height, anything it carries has to stay under 65566 - which is
 * where the one-repeat texture coordinate came from, and it was an arithmetic
 * limit rather than a design choice
 * (docs\decisions\2026-09-01-software-textures-and-caps.md).
 *
 * Splitting the quotient removes that. With q and r the floored quotient and
 * remainder of delta by d, delta * n / d is exactly q * n + r * n / d, and
 * because q * n is a whole number the floor of the sum is q * n plus the floor
 * of the second term. Neither part is large: 0 <= r < d bounds r * n by d * d,
 * at most 1,072,693,504, and n <= d bounds q * n by |delta|. So the largest
 * intermediate no longer depends on the value being interpolated at all.
 *
 * Edge setup also splits the sixteen-subpixel step into quotient/remainder.
 * Advancing then needs only additions and a carry when the error reaches d.
 *
 * Floor and not truncation, and the sign fix-up is what makes it so. C89
 * leaves both / and % implementation defined for a negative operand, and the
 * old form rounded down because its numerator was never negative. A quotient
 * that truncated toward zero instead would round descending gradients the
 * other way - one level, on every gradient that falls rather than rises, which
 * is precisely the kind of difference that shows up as a host test and a
 * driver disagreeing by a pixel.
 *
 * Callers guarantee denominator > 0 and 0 <= numerator <= denominator.
 */
static v9x_s32 v9x_d3d_raster_edge_component(v9x_s32 from, v9x_s32 to,
                                           v9x_s32 numerator, v9x_s32 denominator,
                                           v9x_s32 *step, v9x_s32 *remainder,
                                           v9x_s32 *error)
{
    v9x_s32 delta = to - from;
    v9x_s32 whole = delta / denominator;
    v9x_s32 part = delta % denominator;

    if (part < 0l) {
        whole -= 1l;
        part += denominator;
    }

    *step = whole * V9X_D3D_RASTER_SUBPIXEL_ONE +
            (part * V9X_D3D_RASTER_SUBPIXEL_ONE) / denominator;
    *remainder = (part * V9X_D3D_RASTER_SUBPIXEL_ONE) % denominator;
    *error = (part * numerator) % denominator;
    return from + whole * numerator + (part * numerator) / denominator;
}

/* Five bits to eight, replicating the high bits into the low ones so that 31
 * reaches 255 rather than 248. Four bits to eight is exact - 17 is 255/15 - so
 * it needs no equivalent. */
static v9x_s32 v9x_d3d_raster_expand5(v9x_u32 value)
{
    return (v9x_s32)((value << 3) | (value >> 2));
}

/* Six bits to eight, on the same replication rule: 63 must reach 255, not
 * 252. RGB565's green channel and nothing else needs it. */
static v9x_s32 v9x_d3d_raster_expand6(v9x_u32 value)
{
    return (v9x_s32)((value << 2) | (value >> 4));
}

/*
 * A channel saturated to 0..255, and value / 255 without the divide, both as
 * macros.
 *
 * Macros because the span loop below runs them per pixel and this tree's
 * Watcom command line asks for no optimization at all - the HAL is built with
 * `-bt=nt -bd -zq -wx -we -zl -s` and the benchmark executable matches it - so
 * a static helper is a real CALL on every pixel and not a hint. That is not a
 * guess: the first form of this change used functions, and the 86Box A/B
 * measured the untextured scenes 13 per cent *slower* while the textured ones
 * gained, which is three calls per pixel and nothing else
 * (docs\decisions\2026-09-10-rasterizer-scalar-fixes.md).
 *
 * V9X_D3D_RASTER_CLAMP255 takes an lvalue and names it three times, in the
 * manner of V9X_EDGE_NEXT further down; every use below passes a plain local.
 * The unsigned compare is the trick: a negative value converts to something
 * far above 255, so one test catches both ends, and on the pixels of a
 * well-formed span it is the test that passes. The usual branchless form -
 * masks built from `value >> 31` - is not available here, because C89 leaves
 * the right shift of a negative signed value implementation defined and the
 * sampler already refuses to rely on that.
 *
 * V9X_D3D_RASTER_DIV255 evaluates its argument once. (value * 0x8081) >> 23
 * equals value / 255 for every value from 0 to 66298 and differs from it at
 * 66299 - checked exhaustively rather than derived. The modulate numerator is
 * at most 255 * 255 + 127, which is 65152, so the identity covers it with
 * about a thousand to spare; a margin that thin is worth an assert rather
 * than a comment. The multiply is unsigned because the product is what needs
 * the room: 65152 * 0x8081 is 2,143,305,344, four million short of what a
 * signed 32-bit integer holds. It fits, and leaving the next caller to find
 * that margin by overflowing it is the wrong trade - unsigned takes the bound
 * to 2^32, and the operand is non-negative by construction, both factors
 * having been clamped to 0..255 first. A P5's integer divide is about 46
 * cycles against a pipelined 10 for its multiply, so this removes three
 * divides from every modulated pixel, bit-identically.
 */
#define V9X_D3D_RASTER_CLAMP255(channel) do { \
    if ((v9x_u32)(channel) > 255ul) { \
        (channel) = ((channel) < 0l) ? 0l : 255l; \
    } \
} while (0)

#define V9X_D3D_RASTER_DIV255(value) \
    ((v9x_s32)(((v9x_u32)(value) * 0x8081ul) >> 23))

#define V9X_D3D_RASTER_DIV255_MAX 66298l

typedef char v9x_assert_raster_div255[
    (255l * 255l + 127l <= V9X_D3D_RASTER_DIV255_MAX) ? 1 : -1];

/*
 * The two packers' arithmetic, on channels the caller has already saturated.
 *
 * Split from the exported functions so that the span loop, which clamps its
 * own channels before the texture stage needs them clamped, does not pay for
 * the same three tests twice. The exported forms below are the same routines
 * with the guard back on.
 */
static v9x_u16 v9x_d3d_raster_pack565(v9x_s32 red, v9x_s32 green,
                                      v9x_s32 blue)
{
    return (v9x_u16)(((v9x_u16)(red & 0xf8l) << 8) |
                     ((v9x_u16)(green & 0xfcl) << 3) |
                     ((v9x_u16)(blue & 0xf8l) >> 3));
}

static v9x_u16 v9x_d3d_raster_pack1555(v9x_s32 red, v9x_s32 green,
                                       v9x_s32 blue)
{
    return (v9x_u16)(((v9x_u16)(red & 0xf8l) << 7) |
                     ((v9x_u16)(green & 0xf8l) << 2) |
                     ((v9x_u16)(blue & 0xf8l) >> 3));
}

v9x_u16 v9x_d3d_raster_rgb565(v9x_s32 red, v9x_s32 green, v9x_s32 blue)
{
    /* Saturating at both ends, because the span interpolator's last pixel can
     * land a fraction of a level outside the endpoint colours and a wrapped
     * channel is a bright speck at the end of every span. */
    V9X_D3D_RASTER_CLAMP255(red);
    V9X_D3D_RASTER_CLAMP255(green);
    V9X_D3D_RASTER_CLAMP255(blue);
    return v9x_d3d_raster_pack565(red, green, blue);
}

v9x_u16 v9x_d3d_raster_xrgb1555(v9x_s32 red, v9x_s32 green, v9x_s32 blue)
{
    /* Saturating on the same argument as the 565 packer above. */
    V9X_D3D_RASTER_CLAMP255(red);
    V9X_D3D_RASTER_CLAMP255(green);
    V9X_D3D_RASTER_CLAMP255(blue);
    return v9x_d3d_raster_pack1555(red, green, blue);
}

/*
 * The target's pixel format, resolved once per span instead of per pixel.
 *
 * The format is a property of the surface and cannot change between two
 * pixels of one scanline, so the test that chose an arm belongs outside the
 * loop. A pointer rather than a flag because a flag still costs a branch per
 * pixel, and it replaces a pair of calls - the old per-pixel dispatcher
 * called the packer - with one indirect call.
 *
 * The unpackers stay in this file's own decode terms - expand5 and expand6
 * replicate high bits into low ones so that a full-scale field reaches 255,
 * which a plain shift does not.
 */
typedef v9x_u16 (*V9X_D3D_RASTER_PACK)(v9x_s32 red, v9x_s32 green,
                                       v9x_s32 blue);
typedef void (*V9X_D3D_RASTER_UNPACK)(v9x_u16 pixel, v9x_s32 *red,
                                      v9x_s32 *green, v9x_s32 *blue);

static void v9x_d3d_raster_unpack565(v9x_u16 pixel, v9x_s32 *red,
                                     v9x_s32 *green, v9x_s32 *blue)
{
    *red = v9x_d3d_raster_expand5(((v9x_u32)pixel >> 11) & 0x1ful);
    *green = v9x_d3d_raster_expand6(((v9x_u32)pixel >> 5) & 0x3ful);
    *blue = v9x_d3d_raster_expand5((v9x_u32)pixel & 0x1ful);
}

static void v9x_d3d_raster_unpack1555(v9x_u16 pixel, v9x_s32 *red,
                                      v9x_s32 *green, v9x_s32 *blue)
{
    *red = v9x_d3d_raster_expand5(((v9x_u32)pixel >> 10) & 0x1ful);
    *green = v9x_d3d_raster_expand5(((v9x_u32)pixel >> 5) & 0x1ful);
    *blue = v9x_d3d_raster_expand5((v9x_u32)pixel & 0x1ful);
}

/*
 * The depth comparison as a three-bit relation mask, resolved once per span.
 *
 * Bit 0 admits a fragment nearer than the stored value, bit 1 an equal one,
 * bit 2 a farther one. D3DCMP's numbering is already that mask offset by one
 * - NEVER is 1 and admits nothing, LESS 2, EQUAL 3, LESSEQUAL 4 which is
 * less-or-equal, on to ALWAYS at 8 which admits all three - so the conversion
 * is a subtraction. What licenses it is the header's note that these
 * constants are deliberately the D3DCMP_* values and that d3d_soft.c asserts
 * the equality at compile time; the mask below would be a silent lie if
 * either changed, so it is asserted here too.
 *
 * An unrecognised function becomes ALWAYS rather than NEVER. That is the same
 * decision `v9x_d3d_z_compare` makes in the ViRGE engine, for the same
 * reason: NEVER is a legal value at the low end of the range, so a function
 * that fell through to it would discard every pixel and render black with
 * nothing anywhere to say why. Drawing something wrong is a bug someone can
 * see; drawing nothing looks like a different bug entirely.
 *
 * What this removes is the per-pixel switch. The two comparisons that place
 * the fragment against the stored value remain, because they are the test
 * itself.
 */
#define V9X_D3D_RASTER_RELATION_LESS    1l
#define V9X_D3D_RASTER_RELATION_EQUAL   2l
#define V9X_D3D_RASTER_RELATION_GREATER 4l
#define V9X_D3D_RASTER_RELATION_ANY \
    (V9X_D3D_RASTER_RELATION_LESS | V9X_D3D_RASTER_RELATION_EQUAL | \
     V9X_D3D_RASTER_RELATION_GREATER)

typedef char v9x_assert_raster_relation[
    (V9X_D3D_RASTER_CMP_NEVER == 1ul &&
     V9X_D3D_RASTER_CMP_LESS - V9X_D3D_RASTER_CMP_NEVER ==
         (v9x_u32)V9X_D3D_RASTER_RELATION_LESS &&
     V9X_D3D_RASTER_CMP_EQUAL - V9X_D3D_RASTER_CMP_NEVER ==
         (v9x_u32)V9X_D3D_RASTER_RELATION_EQUAL &&
     V9X_D3D_RASTER_CMP_GREATER - V9X_D3D_RASTER_CMP_NEVER ==
         (v9x_u32)V9X_D3D_RASTER_RELATION_GREATER &&
     V9X_D3D_RASTER_CMP_LESSEQUAL - V9X_D3D_RASTER_CMP_NEVER ==
         (v9x_u32)(V9X_D3D_RASTER_RELATION_LESS |
                   V9X_D3D_RASTER_RELATION_EQUAL) &&
     V9X_D3D_RASTER_CMP_NOTEQUAL - V9X_D3D_RASTER_CMP_NEVER ==
         (v9x_u32)(V9X_D3D_RASTER_RELATION_LESS |
                   V9X_D3D_RASTER_RELATION_GREATER) &&
     V9X_D3D_RASTER_CMP_GREATEREQUAL - V9X_D3D_RASTER_CMP_NEVER ==
         (v9x_u32)(V9X_D3D_RASTER_RELATION_EQUAL |
                   V9X_D3D_RASTER_RELATION_GREATER) &&
     V9X_D3D_RASTER_CMP_ALWAYS - V9X_D3D_RASTER_CMP_NEVER ==
         (v9x_u32)V9X_D3D_RASTER_RELATION_ANY) ? 1 : -1];

static v9x_s32 v9x_d3d_raster_depth_mask(v9x_u32 compare)
{
    if (compare < V9X_D3D_RASTER_CMP_NEVER ||
        compare > V9X_D3D_RASTER_CMP_ALWAYS) {
        return V9X_D3D_RASTER_RELATION_ANY;
    }
    return (v9x_s32)(compare - V9X_D3D_RASTER_CMP_NEVER);
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
    if (target->format != V9X_D3D_RASTER_PIXFMT_RGB565 &&
        target->format != V9X_D3D_RASTER_PIXFMT_XRGB1555) {
        return 0;
    }
    /* The scissor inside the target, and not inverted. An empty rectangle
     * is legal and draws nothing. */
    if (target->clip_left > target->clip_right ||
        target->clip_top > target->clip_bottom ||
        target->clip_right > target->width ||
        target->clip_bottom > target->height) {
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
        texture->format != V9X_D3D_RASTER_TEXFMT_ARGB4444 &&
        texture->format != V9X_D3D_RASTER_TEXFMT_RGB565) {
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
    if (texture->address != V9X_D3D_RASTER_ADDRESS_WRAP &&
        texture->address != V9X_D3D_RASTER_ADDRESS_CLAMP) {
        return 0;
    }
    if (texture->alpha != V9X_D3D_RASTER_TEXALPHA_IGNORE &&
        texture->alpha != V9X_D3D_RASTER_TEXALPHA_REPLACE &&
        texture->alpha != V9X_D3D_RASTER_TEXALPHA_MODULATE) {
        return 0;
    }
    /* Each dimension a power of two within the bounds, checked rather than
     * assumed: the sampler wraps each texel index with (extent - 1) as a
     * mask, and on a non-power-of-two extent that mask indexes outside the
     * surface instead of looking wrong. */
    {
        v9x_u32 extents[2];
        unsigned int axis;

        extents[0] = texture->width;
        extents[1] = texture->height;
        for (axis = 0u; axis < 2u; ++axis) {
            if (extents[axis] < V9X_D3D_RASTER_TEXTURE_SIZE_MIN ||
                extents[axis] > V9X_D3D_RASTER_TEXTURE_SIZE_MAX) {
                return 0;
            }
            bit = 1ul;
            while (bit < extents[axis]) {
                bit <<= 1;
            }
            if (bit != extents[axis]) {
                return 0;
            }
        }
    }
    if (texture->pitch < texture->width * 2ul) {
        return 0;
    }
    if (texture->mip != V9X_D3D_RASTER_MIP_NONE &&
        texture->mip != V9X_D3D_RASTER_MIP_POINT &&
        texture->mip != V9X_D3D_RASTER_MIP_LINEAR) {
        return 0;
    }
    if (texture->mip_count > V9X_D3D_RASTER_MIPS_MAX) {
        return 0;
    }
    if (texture->mip_count == 0ul) {
        return texture->mip == V9X_D3D_RASTER_MIP_NONE;
    }
    /* The chain: every level present and each extent max(1, half the one
     * before). The sampler's masks rely on that as much as on the powers of
     * two above, and a chain that skipped a level would be sampled a level
     * off, which looks like a blur rather than a fault. */
    if (texture->mips == 0) {
        return 0;
    }
    {
        v9x_u32 width = texture->width;
        v9x_u32 height = texture->height;
        v9x_u32 level;

        for (level = 0ul; level < texture->mip_count; ++level) {
            const V9X_D3D_RASTER_LEVEL *mip = &texture->mips[level];

            width = width > 1ul ? width / 2ul : 1ul;
            height = height > 1ul ? height / 2ul : 1ul;
            if (mip->pixels == 0 || mip->width != width ||
                mip->height != height || mip->pitch < width * 2ul) {
                return 0;
            }
        }
    }
    return 1;
}

int v9x_d3d_raster_alpha_test_valid(const V9X_D3D_RASTER_ALPHA_TEST *test)
{
    if (test == 0) {
        return 0;
    }
    if (test->compare < V9X_D3D_RASTER_CMP_NEVER ||
        test->compare > V9X_D3D_RASTER_CMP_ALWAYS) {
        return 0;
    }
    if (test->reference < 0l || test->reference > 255l) {
        return 0;
    }
    return 1;
}

int v9x_d3d_raster_alpha_valid(const V9X_D3D_RASTER_ALPHA *alpha)
{
    if (alpha == 0) {
        return 0;
    }
    /* Either side takes any of the eleven; the two D3D shorthands above
     * that, and zero, are refused. */
    if (alpha->src < V9X_D3D_RASTER_FACTOR_ZERO ||
        alpha->src > V9X_D3D_RASTER_FACTOR_SRCALPHASAT) {
        return 0;
    }
    if (alpha->dst < V9X_D3D_RASTER_FACTOR_ZERO ||
        alpha->dst > V9X_D3D_RASTER_FACTOR_SRCALPHASAT) {
        return 0;
    }
    return 1;
}

/*
 * A 0..255 alpha as a 0..256 weight, so that both ends of the range are exact.
 *
 * Dividing by 255 per channel per pixel is three divides in the inner loop and
 * this engine already costs enough; shifting by eight instead needs 255 to map
 * to 256 rather than to 255, or a fully opaque fragment would come out one
 * two-hundred-and-fifty-sixth short of the source colour and a large flat
 * blended surface would be visibly dimmed. `value >> 7` is 1 only at 254 and
 * 255, which is exactly where the correction is needed, and the mapping stays
 * monotonic everywhere else.
 */
static v9x_s32 v9x_d3d_raster_weight(v9x_s32 value)
{
    V9X_D3D_RASTER_CLAMP255(value);
    return value + (value >> 7);
}

/*
 * source * source_factor + destination * destination_factor, in 0..255.
 *
 * The two factors are passed already resolved to 0..256 weights because they
 * are the same for every channel and every pixel of a span; resolving them
 * here would repeat the branch three times a pixel. The product is at most
 * 255 * 256 twice, which is 130,560 - nowhere near the edge of anything.
 */
/*
 * One blend factor of the general path, resolved per channel to 0..255.
 *
 * The colour factors differ per channel; the alpha ones and the constants
 * are the same in all three. The destination-alpha readings are the ones the
 * header explains: no target here has an alpha plane, so it reads as 1.
 * A function rather than a macro, like the blend helper below it: an
 * eleven-way choice has no macro form worth reading, and it runs twice a
 * pixel on this path only.
 */
static void v9x_d3d_raster_factor(v9x_u32 kind,
                                  v9x_s32 src_red, v9x_s32 src_green,
                                  v9x_s32 src_blue,
                                  v9x_s32 dst_red, v9x_s32 dst_green,
                                  v9x_s32 dst_blue,
                                  v9x_s32 src_alpha,
                                  v9x_s32 *red, v9x_s32 *green, v9x_s32 *blue)
{
    v9x_s32 scalar;

    if (kind == V9X_D3D_RASTER_FACTOR_SRCCOLOR) {
        *red = src_red;
        *green = src_green;
        *blue = src_blue;
        return;
    }
    if (kind == V9X_D3D_RASTER_FACTOR_INVSRCCOLOR) {
        *red = 255l - src_red;
        *green = 255l - src_green;
        *blue = 255l - src_blue;
        return;
    }
    if (kind == V9X_D3D_RASTER_FACTOR_DESTCOLOR) {
        *red = dst_red;
        *green = dst_green;
        *blue = dst_blue;
        return;
    }
    if (kind == V9X_D3D_RASTER_FACTOR_INVDESTCOLOR) {
        *red = 255l - dst_red;
        *green = 255l - dst_green;
        *blue = 255l - dst_blue;
        return;
    }

    if (kind == V9X_D3D_RASTER_FACTOR_ONE ||
        kind == V9X_D3D_RASTER_FACTOR_DESTALPHA) {
        scalar = 255l;
    } else if (kind == V9X_D3D_RASTER_FACTOR_SRCALPHA) {
        scalar = src_alpha;
    } else if (kind == V9X_D3D_RASTER_FACTOR_INVSRCALPHA) {
        scalar = 255l - src_alpha;
    } else {
        /* ZERO, INVDESTALPHA and SRCALPHASAT. */
        scalar = 0l;
    }
    *red = scalar;
    *green = scalar;
    *blue = scalar;
}

/*
 * source * source_factor + destination * destination_factor on the general
 * path, both factors 0..255, each product divided exactly and the sum
 * saturated. Two divides rather than one over the sum because the sum of
 * two full products is past V9X_D3D_RASTER_DIV255_MAX; the rounding of each
 * term is exact at 0 and 255, which is where identity has to hold.
 */
#define V9X_D3D_RASTER_BLEND_GENERAL(channel, source, destination, \
                                     source_factor, destination_factor) \
    do { \
        (channel) = V9X_D3D_RASTER_DIV255((source) * (source_factor) + 127l) + \
                    V9X_D3D_RASTER_DIV255((destination) * (destination_factor) + \
                                          127l); \
        V9X_D3D_RASTER_CLAMP255(channel); \
    } while (0)

static v9x_s32 v9x_d3d_raster_blend(v9x_s32 source, v9x_s32 destination,
                                    v9x_s32 source_weight,
                                    v9x_s32 destination_weight)
{
    return (source * source_weight + destination * destination_weight) >> 8;
}

/*
 * Half a texel, in the sampler's 16.16 texel-unit coordinate.
 *
 * The bilinear arm samples about the texel centre, so it steps back half a
 * texel before it splits the coordinate into an index and a fraction.
 */
#define V9X_D3D_RASTER_TEXEL_HALF 32768l

/*
 * The sampler's per-draw constants, resolved once and read per pixel.
 *
 * Everything here is derived from the bound texture and cannot change while
 * one triangle is drawn, and all of it used to be recomputed per pixel: the
 * size, the wrap mask, the bilinear bias, and a chain of format tests inside
 * a function called once per *texel* - four times over for a bilinear pixel,
 * each call also multiplying its own row offset by the pitch. Nothing in this
 * build inlines, so those were four calls, four tests and four multiplies
 * that a bilinear pixel did not need
 * (docs\decisions\2026-09-10-rasterizer-scalar-fixes.md).
 *
 * `shift` is log2 of the texture edge, so the caller scales a texture
 * coordinate into texel units with a shift rather than a multiply. The size
 * is a power of two, which v9x_d3d_raster_texture_valid checks rather than
 * assumes.
 *
 * The three channel descriptors are what replace the format tests. All three
 * texel formats are a field of w bits replicated up to eight -
 * `(field << (8 - w)) | (field >> (2w - 8))` - which is what expand5 and
 * expand6 do for five and six bits, and what the multiply by 17 does for
 * four, since 17 * v is (v << 4) | v. One decode path with a per-channel
 * shift, mask and pair of replication shifts therefore covers every format
 * exactly, with no branch left in the pixel path.
 *
 * Alpha is described by nothing, as before. The sampler has no alpha blending
 * behind it and describe_caps advertises no texture alpha to match; a channel
 * decoded and discarded would be the beginning of exactly the
 * advertise-then-ignore pattern this driver has paid for twice.
 *
 * `clamp` and `modulate` are the two render states the span loop reads, held
 * here so that the loop needs no second pointer to the texture.
 */
typedef struct V9X_D3D_RASTER_FIELD {
    v9x_s32 shift;
    v9x_u32 mask;
    v9x_s32 left;
    v9x_s32 right;
} V9X_D3D_RASTER_FIELD;

/* One level's addressing. Per axis: the texel scale, the wrap mask and the
 * bilinear bias are each the axis's own extent, since a texture need not be
 * square. */
typedef struct V9X_D3D_RASTER_SAMPLER_LEVEL {
    const v9x_u8 *pixels;
    v9x_u32 pitch;
    v9x_s32 shift_u;
    v9x_s32 shift_v;
    v9x_s32 mask_u;
    v9x_s32 mask_v;
    v9x_s32 bias_u;
    v9x_s32 bias_v;
} V9X_D3D_RASTER_SAMPLER_LEVEL;

typedef struct V9X_D3D_RASTER_SAMPLER {
    /* Level 0 first, then the chain; at least one. */
    V9X_D3D_RASTER_SAMPLER_LEVEL levels[V9X_D3D_RASTER_MIPS_MAX + 1ul];
    v9x_u32 level_count;
    /* One of V9X_D3D_RASTER_MIP_*, and whether it selects at all: NONE, or
     * a chain of one level, samples level 0 and costs the span nothing. */
    v9x_u32 mip;
    int select;
    int linear;
    int clamp;
    int modulate;
    V9X_D3D_RASTER_FIELD red;
    V9X_D3D_RASTER_FIELD green;
    V9X_D3D_RASTER_FIELD blue;
    /* The texel's alpha: none (opaque), one bit at 15, or four bits at 12;
     * and what the span does with it, one of V9X_D3D_RASTER_TEXALPHA_*. */
    v9x_u32 alpha_bits;
    v9x_u32 alpha_op;
} V9X_D3D_RASTER_SAMPLER;

/*
 * A texel's alpha as 0..255. One bit is all or nothing; four bits replicate
 * as the colour fields do (17 * v); a format with none is opaque. Not a
 * V9X_D3D_RASTER_FIELD because the one-bit case's replication shift would
 * be negative in that formula.
 */
static v9x_s32 v9x_d3d_raster_texel_alpha(const V9X_D3D_RASTER_SAMPLER *sampler,
                                          v9x_u32 word)
{
    if (sampler->alpha_bits == 1ul) {
        return (word & 0x8000ul) != 0ul ? 255l : 0l;
    }
    if (sampler->alpha_bits == 4ul) {
        return (v9x_s32)(((word >> 12) & 0xful) * 17ul);
    }
    return 255l;
}

static void v9x_d3d_raster_field(V9X_D3D_RASTER_FIELD *field, v9x_s32 shift,
                                 v9x_s32 width)
{
    field->shift = shift;
    field->mask = (1ul << width) - 1ul;
    field->left = 8l - width;
    field->right = 2l * width - 8l;
}

static void v9x_d3d_raster_sampler_level(V9X_D3D_RASTER_SAMPLER_LEVEL *level,
                                         const void *pixels, v9x_u32 pitch,
                                         v9x_u32 width, v9x_u32 height)
{
    v9x_s32 shift_u = 0l;
    v9x_s32 shift_v = 0l;

    while ((1ul << shift_u) < width) {
        ++shift_u;
    }
    while ((1ul << shift_v) < height) {
        ++shift_v;
    }
    level->pixels = (const v9x_u8 *)pixels;
    level->pitch = pitch;
    level->shift_u = shift_u;
    level->shift_v = shift_v;
    level->mask_u = (v9x_s32)width - 1l;
    level->mask_v = (v9x_s32)height - 1l;
    level->bias_u = (v9x_s32)width << 16;
    level->bias_v = (v9x_s32)height << 16;
}

static void v9x_d3d_raster_sampler_start(
    const V9X_D3D_RASTER_TEXTURE *texture, V9X_D3D_RASTER_SAMPLER *sampler)
{
    v9x_u32 level;

    v9x_d3d_raster_sampler_level(&sampler->levels[0], texture->pixels,
                                 texture->pitch, texture->width,
                                 texture->height);
    for (level = 0ul; level < texture->mip_count; ++level) {
        v9x_d3d_raster_sampler_level(&sampler->levels[level + 1ul],
                                     texture->mips[level].pixels,
                                     texture->mips[level].pitch,
                                     texture->mips[level].width,
                                     texture->mips[level].height);
    }
    sampler->level_count = texture->mip_count + 1ul;
    sampler->mip = texture->mip;
    sampler->select = texture->mip != V9X_D3D_RASTER_MIP_NONE &&
                      texture->mip_count != 0ul;
    sampler->linear = texture->filter == V9X_D3D_RASTER_FILTER_LINEAR;
    sampler->clamp = texture->address == V9X_D3D_RASTER_ADDRESS_CLAMP;
    sampler->modulate = texture->blend == V9X_D3D_RASTER_BLEND_MODULATE;
    sampler->alpha_op = texture->alpha;
    sampler->alpha_bits = texture->format == V9X_D3D_RASTER_TEXFMT_ARGB1555 ? 1ul
        : (texture->format == V9X_D3D_RASTER_TEXFMT_ARGB4444 ? 4ul : 0ul);

    if (texture->format == V9X_D3D_RASTER_TEXFMT_ARGB4444) {
        v9x_d3d_raster_field(&sampler->red, 8l, 4l);
        v9x_d3d_raster_field(&sampler->green, 4l, 4l);
        v9x_d3d_raster_field(&sampler->blue, 0l, 4l);
        return;
    }
    if (texture->format == V9X_D3D_RASTER_TEXFMT_RGB565) {
        v9x_d3d_raster_field(&sampler->red, 11l, 5l);
        v9x_d3d_raster_field(&sampler->green, 5l, 6l);
        v9x_d3d_raster_field(&sampler->blue, 0l, 5l);
        return;
    }
    v9x_d3d_raster_field(&sampler->red, 10l, 5l);
    v9x_d3d_raster_field(&sampler->green, 5l, 5l);
    v9x_d3d_raster_field(&sampler->blue, 0l, 5l);
}

/*
 * One texel word decoded into three 0..255 channels, and one whole texel.
 *
 * Macros for the reason the clamp above is one: a bilinear pixel decodes four
 * texels, and in a build that inlines nothing each of these would otherwise
 * be a call. V9X_D3D_RASTER_DECODE names `sampler` implicitly, in the manner
 * of V9X_EDGE_START below.
 */
#define V9X_D3D_RASTER_FIELD_DECODE(out, field, word) do { \
    v9x_u32 field_value = ((word) >> (field).shift) & (field).mask; \
    (out) = (v9x_s32)((field_value << (field).left) | \
                      (field_value >> (field).right)); \
} while (0)

#define V9X_D3D_RASTER_DECODE(word, out_red, out_green, out_blue) do { \
    V9X_D3D_RASTER_FIELD_DECODE(out_red, sampler->red, (word)); \
    V9X_D3D_RASTER_FIELD_DECODE(out_green, sampler->green, (word)); \
    V9X_D3D_RASTER_FIELD_DECODE(out_blue, sampler->blue, (word)); \
} while (0)

/*
 * Sample the texture at a texel-unit coordinate, point or bilinear.
 *
 * The coordinate arrives in 16.16 texel units - whole texels in the top bits
 * - because the caller has already folded the texture size in with a shift.
 * That is also what bounds everything here: the caller wraps or clamps into
 * the first repeat first, so the coordinate is at most 65535 << 9, about 33.5
 * million, where it used to be a texture coordinate times a size and reached
 * 1.1 billion. The bilinear arm still adds a whole texture's worth of bias
 * before it shifts, because an arithmetic right shift of a negative value is
 * implementation defined and the wrap mask would then index backwards off the
 * surface. The weighted sum of four texels is at most 255 * 256 * 256, which
 * is 16,711,680.
 *
 * The four weights come from one multiply rather than four. w11 is fu * fv;
 * the other three follow by subtraction, since (256 - fu) * (256 - fv) is
 * 65536 - 256fu - 256fv + fu*fv. Every term is an exact integer, so the sum
 * is the same sum - which is the reason the plan's nested-lerp form was *not*
 * taken: lerping the rows and then the column costs one multiply per channel
 * instead of four, but each step truncates, and the pixel table holds this
 * function to the untruncated result.
 *
 * The `& mask` on every texel index is what performs the wrap, and it was
 * already here when the coordinate could only express one repeat, where it
 * was a no-op guarding against a coordinate that had drifted a fraction past
 * the end. Tiling did not need a second code path in the sampler; it needed a
 * coordinate wide enough to reach one.
 */
static void v9x_d3d_raster_sample(const V9X_D3D_RASTER_SAMPLER *sampler,
                                  const V9X_D3D_RASTER_SAMPLER_LEVEL *level,
                                  v9x_s32 texel_u, v9x_s32 texel_v,
                                  v9x_s32 *red, v9x_s32 *green, v9x_s32 *blue,
                                  v9x_s32 *texel_alpha)
{
    const v9x_u16 *row;
    v9x_u32 word;
    /* Into the level's texel units: a shift, since the coordinate is inside
     * one repeat and the extent is a power of two. */
    v9x_s32 su = texel_u << level->shift_u;
    v9x_s32 sv = texel_v << level->shift_v;

    if (sampler->linear) {
        v9x_s32 bu = su + level->bias_u - V9X_D3D_RASTER_TEXEL_HALF;
        v9x_s32 bv = sv + level->bias_v - V9X_D3D_RASTER_TEXEL_HALF;
        v9x_s32 x0 = (bu >> 16) & level->mask_u;
        v9x_s32 y0 = (bv >> 16) & level->mask_v;
        v9x_s32 x1 = (x0 + 1l) & level->mask_u;
        v9x_s32 y1 = (y0 + 1l) & level->mask_v;
        v9x_s32 fu = (bu >> 8) & 0xffl;
        v9x_s32 fv = (bv >> 8) & 0xffl;
        v9x_s32 w11 = fu * fv;
        v9x_s32 w10 = (fu << 8) - w11;
        v9x_s32 w01 = (fv << 8) - w11;
        v9x_s32 w00 = 65536l - (fu << 8) - (fv << 8) + w11;
        const v9x_u16 *row0 = (const v9x_u16 *)(level->pixels +
                                                (v9x_u32)y0 * level->pitch);
        const v9x_u16 *row1 = (const v9x_u16 *)(level->pixels +
                                                (v9x_u32)y1 * level->pitch);
        v9x_u32 t00 = (v9x_u32)row0[x0];
        v9x_u32 t10 = (v9x_u32)row0[x1];
        v9x_u32 t01 = (v9x_u32)row1[x0];
        v9x_u32 t11 = (v9x_u32)row1[x1];
        v9x_s32 r00, g00, b00, r10, g10, b10, r01, g01, b01, r11, g11, b11;

        V9X_D3D_RASTER_DECODE(t00, r00, g00, b00);
        V9X_D3D_RASTER_DECODE(t10, r10, g10, b10);
        V9X_D3D_RASTER_DECODE(t01, r01, g01, b01);
        V9X_D3D_RASTER_DECODE(t11, r11, g11, b11);
        *red = (r00 * w00 + r10 * w10 + r01 * w01 + r11 * w11) >> 16;
        *green = (g00 * w00 + g10 * w10 + g01 * w01 + g11 * w11) >> 16;
        *blue = (b00 * w00 + b10 * w10 + b01 * w01 + b11 * w11) >> 16;
        if (sampler->alpha_op != V9X_D3D_RASTER_TEXALPHA_IGNORE) {
            *texel_alpha =
                (v9x_d3d_raster_texel_alpha(sampler, t00) * w00 +
                 v9x_d3d_raster_texel_alpha(sampler, t10) * w10 +
                 v9x_d3d_raster_texel_alpha(sampler, t01) * w01 +
                 v9x_d3d_raster_texel_alpha(sampler, t11) * w11) >> 16;
        }
        return;
    }

    row = (const v9x_u16 *)(level->pixels +
                            (v9x_u32)((sv >> 16) & level->mask_v) *
                                level->pitch);
    word = (v9x_u32)row[(su >> 16) & level->mask_u];
    V9X_D3D_RASTER_DECODE(word, *red, *green, *blue);
    if (sampler->alpha_op != V9X_D3D_RASTER_TEXALPHA_IGNORE) {
        *texel_alpha = v9x_d3d_raster_texel_alpha(sampler, word);
    }
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
/*
 * u * q at a vertex, 16.16 in and out, for the perspective path.
 *
 * Both are 16.16 and the plain product is 32.32, so the coordinate is split
 * at its repeat: whole repeats times q stays inside 2^21 (thirty-three of
 * them by at most 2^16), and the fraction times q is under 2^32, unsigned.
 * The result is at most the coordinate itself, since q is at most one.
 */
static v9x_s32 v9x_d3d_raster_scale_by_q(v9x_s32 coordinate, v9x_s32 q)
{
    v9x_s32 whole = coordinate / V9X_D3D_RASTER_TEXCOORD_ONE;
    v9x_u32 fraction = (v9x_u32)(coordinate % V9X_D3D_RASTER_TEXCOORD_ONE);

    return whole * q +
           (v9x_s32)((fraction * (v9x_u32)q) >> V9X_D3D_RASTER_Q_BITS);
}

/*
 * The per-pixel divide of the perspective path: (u * q) / q back to 16.16,
 * given the reciprocal 2^30 / q the span formed once for both axes.
 *
 * The product scaled * reciprocal is the coordinate times 2^14 and so at
 * most 2^35, past 32 bits, and this build has no wider integer. It is
 * formed in four pieces, scaled split at bit 11 and the reciprocal at bit
 * 15 - a = ah * 2^11 + al, b = bh * 2^15 + bl, so a * b is ah * bh * 2^26
 * + ah * bl * 2^11 + al * bh * 2^15 + al * bl - each partial product under
 * 2^27 and each shifted into place by its own power; the two cross terms
 * do not share a shift, which the first draft got wrong and the column-2
 * check in the tests caught. The three truncations lose under three units
 * of 16.16 between them. Every piece is bounded by the true product
 * because the correct coordinate lies between the triangle's vertex
 * coordinates, which the entry has checked.
 *
 * The reciprocal's own truncation costs up to 1 in 16384 of the coordinate
 * at the near end of a triangle, a texel at 33 repeats on a 512 texture
 * and 1/32 of one at a single repeat. That is the precision the contract
 * document states.
 */
#define V9X_D3D_RASTER_Q_RECIPROCAL_ONE (1l << 30)

static v9x_s32 v9x_d3d_raster_divide_by_q(v9x_s32 scaled, v9x_s32 reciprocal)
{
    v9x_s32 scaled_high;
    v9x_s32 scaled_low;
    v9x_s32 reciprocal_high = reciprocal >> 15;
    v9x_s32 reciprocal_low = reciprocal & 0x7ffful;

    /* A negative u * q is an interpolator that has drifted a fraction below
     * zero between two non-negative endpoints. The span clamps the result
     * to zero anyway, and the arithmetic shift below must not be asked
     * about a negative value - the same rule the sampler's lower bound
     * states. */
    if (scaled < 0l) {
        return 0l;
    }
    scaled_high = scaled >> 11;
    scaled_low = scaled & 0x7ffl;
    return ((scaled_high * reciprocal_high) << 12) +
           ((scaled_high * reciprocal_low) >> 3) +
           ((scaled_low * reciprocal_high) << 1) +
           ((scaled_low * reciprocal_low) >> 14);
}

/*
 * log2 of a 16.16 magnitude as 16.16, negative below one: the leading bit
 * gives the whole part and a sixteen-entry table of log2(1 + i/16) the
 * fraction, from the four bits under the leading one. Good to about a
 * fiftieth of a level, which is finer than a blend between two levels can
 * show on 16-bit pixels.
 */
/*
 * |value|, saturated at the widest coordinate the arithmetic carries. The
 * level-of-detail helpers take derivatives that a sub-pixel span or a
 * one-pixel plane can make far larger than any texture coordinate, and a
 * derivative past thirty-three repeats per pixel is past every chain's
 * last level anyway; saturating there keeps every product below inside
 * the bounds its caller states.
 */
static v9x_s32 v9x_d3d_raster_bounded(v9x_s32 value, v9x_s32 limit)
{
    if (value > limit) {
        return limit;
    }
    if (value < -limit) {
        return -limit;
    }
    return value;
}

static v9x_s32 v9x_d3d_raster_magnitude(v9x_s32 value)
{
    value = v9x_d3d_raster_bounded(value, V9X_D3D_RASTER_TEXCOORD_MAX);
    return value < 0l ? -value : value;
}

static v9x_s32 v9x_d3d_raster_log2(v9x_u32 value)
{
    static const v9x_u16 fraction[16] = {
        0u, 5732u, 11136u, 16248u, 21098u, 25711u, 30109u, 34312u,
        38336u, 42196u, 45904u, 49472u, 52911u, 56229u, 59435u, 62537u
    };
    v9x_s32 top = 31l;
    v9x_u32 index;

    if (value == 0ul) {
        return -(16l << 16);
    }
    while ((value >> top) == 0ul) {
        --top;
    }
    index = top >= 4l ? (value >> (top - 4l)) & 15ul
                      : (value << (4l - top)) & 15ul;
    return ((top - 16l) << 16) + (v9x_s32)fraction[index];
}

/*
 * The level of detail from the four derivatives of the texture coordinates
 * (16.16 repeats per pixel, any sign), scaled by level 0's extents into
 * texels per pixel. The scale factor is the largest of the four absolute
 * values, which OpenGL 1.1 permits as the bound on the exact one (3.8.5);
 * lambda is its log2, in 16.16.
 */
static v9x_s32 v9x_d3d_raster_lod(const V9X_D3D_RASTER_SAMPLER *sampler,
                                  v9x_s32 du_dx, v9x_s32 dv_dx,
                                  v9x_s32 du_dy, v9x_s32 dv_dy)
{
    const V9X_D3D_RASTER_SAMPLER_LEVEL *base = &sampler->levels[0];
    v9x_u32 rho = 0ul;
    v9x_u32 scale;

    /* Each magnitude is at most 2^21 after saturation and the shift at
     * most nine, so the scaled value stays under 2^31. */
    scale = (v9x_u32)v9x_d3d_raster_magnitude(du_dx) << base->shift_u;
    if (scale > rho) {
        rho = scale;
    }
    scale = (v9x_u32)v9x_d3d_raster_magnitude(du_dy) << base->shift_u;
    if (scale > rho) {
        rho = scale;
    }
    scale = (v9x_u32)v9x_d3d_raster_magnitude(dv_dx) << base->shift_v;
    if (scale > rho) {
        rho = scale;
    }
    scale = (v9x_u32)v9x_d3d_raster_magnitude(dv_dy) << base->shift_v;
    if (scale > rho) {
        rho = scale;
    }
    return v9x_d3d_raster_log2(rho);
}

/*
 * The magnitude of a coordinate's derivative on the perspective path, where
 * u is (u * q) / q and so d(u)/dx is (d(u * q)/dx - u * dq/dx) / q, every
 * term 16.16 per pixel. Saturated at thirty-three repeats per pixel, which
 * is past any chain's last level and keeps the split multiply inside the
 * bound its comment states.
 */
static v9x_s32 v9x_d3d_raster_derivative_q(v9x_s32 scaled_step,
                                           v9x_s32 coordinate,
                                           v9x_s32 q_step, v9x_s32 q16,
                                           v9x_s32 reciprocal)
{
    /* Both terms bounded, signs kept, before the product and the
     * difference: the coordinate is inside 2^21 by validation, the step of
     * q per pixel is saturated at one whole unit either way (q itself is at
     * most one), and the scaled step at the coordinate bound; (2^13)(2^8)
     * and the difference then fit with room. */
    v9x_s32 q_rate = v9x_d3d_raster_bounded(q_step, V9X_D3D_RASTER_Q_ONE);
    v9x_s32 numerator =
        v9x_d3d_raster_bounded(scaled_step, V9X_D3D_RASTER_TEXCOORD_MAX) -
        (coordinate / 256l) * (q_rate / 256l);

    if (numerator < 0l) {
        numerator = -numerator;
    }
    if (numerator > (q16 << 5)) {
        return V9X_D3D_RASTER_TEXCOORD_MAX;
    }
    return v9x_d3d_raster_divide_by_q(numerator, reciprocal);
}

/*
 * Sample at a level of detail: POINT takes the nearest level, LINEAR the two
 * either side blended by the fraction between them, both clamped to the
 * chain. A lambda at or below zero is level 0 alone on either.
 */
static void v9x_d3d_raster_sample_mip(const V9X_D3D_RASTER_SAMPLER *sampler,
                                      v9x_s32 lambda,
                                      v9x_s32 texel_u, v9x_s32 texel_v,
                                      v9x_s32 *red, v9x_s32 *green,
                                      v9x_s32 *blue, v9x_s32 *texel_alpha)
{
    v9x_s32 last = (v9x_s32)sampler->level_count - 1l;
    v9x_s32 level;
    v9x_s32 fraction;
    v9x_s32 red1, green1, blue1;
    v9x_s32 alpha0 = 255l;
    v9x_s32 alpha1 = 255l;

    if (sampler->mip == V9X_D3D_RASTER_MIP_POINT) {
        level = (lambda + 32768l) / 65536l;
        if (level < 0l) {
            level = 0l;
        }
        if (level > last) {
            level = last;
        }
        v9x_d3d_raster_sample(sampler, &sampler->levels[level], texel_u,
                              texel_v, red, green, blue, texel_alpha);
        return;
    }
    if (lambda <= 0l) {
        v9x_d3d_raster_sample(sampler, &sampler->levels[0], texel_u, texel_v,
                              red, green, blue, texel_alpha);
        return;
    }
    level = lambda / 65536l;
    if (level >= last) {
        v9x_d3d_raster_sample(sampler, &sampler->levels[last], texel_u,
                              texel_v, red, green, blue, texel_alpha);
        return;
    }
    fraction = (lambda & 0xffffl) >> 8;
    v9x_d3d_raster_sample(sampler, &sampler->levels[level], texel_u, texel_v,
                          red, green, blue, &alpha0);
    v9x_d3d_raster_sample(sampler, &sampler->levels[level + 1l], texel_u,
                          texel_v, &red1, &green1, &blue1, &alpha1);
    *red = (*red * (256l - fraction) + red1 * fraction) >> 8;
    *green = (*green * (256l - fraction) + green1 * fraction) >> 8;
    *blue = (*blue * (256l - fraction) + blue1 * fraction) >> 8;
    if (sampler->alpha_op != V9X_D3D_RASTER_TEXALPHA_IGNORE) {
        *texel_alpha = (alpha0 * (256l - fraction) + alpha1 * fraction) >> 8;
    }
}

/*
 * The screen-space gradient along y of one vertex attribute over a
 * triangle, 16.16 per pixel, from the plane through the three vertices. In
 * whole pixels and with the attribute at twelve fractional bits so that
 * every product fits 32 bits: (2^11)(2^17) and (2^11)^2. Zero for a
 * degenerate triangle, which draws nothing anyway.
 */
typedef struct V9X_D3D_RASTER_GRADIENTS {
    v9x_s32 du_dy;
    v9x_s32 dv_dy;
    v9x_s32 dq_dy;
} V9X_D3D_RASTER_GRADIENTS;

static v9x_s32 v9x_d3d_raster_gradient_y(const V9X_D3D_RASTER_VERTEX *a,
                                         const V9X_D3D_RASTER_VERTEX *b,
                                         const V9X_D3D_RASTER_VERTEX *c,
                                         v9x_s32 va, v9x_s32 vb, v9x_s32 vc)
{
    v9x_s32 x1 = (b->x - a->x) / V9X_D3D_RASTER_SUBPIXEL_ONE;
    v9x_s32 x2 = (c->x - a->x) / V9X_D3D_RASTER_SUBPIXEL_ONE;
    v9x_s32 y1 = (b->y - a->y) / V9X_D3D_RASTER_SUBPIXEL_ONE;
    v9x_s32 y2 = (c->y - a->y) / V9X_D3D_RASTER_SUBPIXEL_ONE;
    v9x_s32 a1 = (vb - va) / 16l;
    v9x_s32 a2 = (vc - va) / 16l;
    v9x_s32 denominator = y1 * x2 - y2 * x1;
    v9x_s32 gradient;

    if (denominator == 0l) {
        return 0l;
    }
    /* Saturated before the return to sixteen fractional bits: a one-pixel
     * denominator under a full-width attribute would otherwise carry the
     * product past 32 bits, and a gradient past the coordinate bound is
     * past every chain's last level regardless. */
    gradient = (a1 * x2 - a2 * x1) / denominator;
    if (gradient > V9X_D3D_RASTER_TEXCOORD_MAX / 16l) {
        gradient = V9X_D3D_RASTER_TEXCOORD_MAX / 16l;
    }
    if (gradient < -(V9X_D3D_RASTER_TEXCOORD_MAX / 16l)) {
        gradient = -(V9X_D3D_RASTER_TEXCOORD_MAX / 16l);
    }
    return gradient * 16l;
}

typedef struct V9X_D3D_RASTER_EDGE {
    V9X_D3D_RASTER_VERTEX value;
    V9X_D3D_RASTER_VERTEX step;
    V9X_D3D_RASTER_VERTEX remainder;
    V9X_D3D_RASTER_VERTEX error;
    v9x_s32 span;
} V9X_D3D_RASTER_EDGE;

static int v9x_d3d_raster_edge_start(const V9X_D3D_RASTER_VERTEX *from,
                                  const V9X_D3D_RASTER_VERTEX *to,
                                  v9x_s32 sample,
                                  V9X_D3D_RASTER_EDGE *edge)
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

    edge->span = span;
    edge->value.y = sample;
#define V9X_EDGE_START(member) \
    edge->value.member = v9x_d3d_raster_edge_component( \
        from->member, to->member, offset, span, &edge->step.member, \
        &edge->remainder.member, &edge->error.member)
    V9X_EDGE_START(x);
    V9X_EDGE_START(z);
    V9X_EDGE_START(u);
    V9X_EDGE_START(v);
    V9X_EDGE_START(red);
    V9X_EDGE_START(green);
    V9X_EDGE_START(blue);
    V9X_EDGE_START(alpha);
    V9X_EDGE_START(q);
#undef V9X_EDGE_START
    return 1;
}

/* Called only when the next sample is still on this edge. Error stays below
 * span, and error + remainder is below 2 * span (at most 65504). */
static void v9x_d3d_raster_edge_next(V9X_D3D_RASTER_EDGE *edge)
{
#define V9X_EDGE_NEXT(member) do { \
    edge->value.member += edge->step.member; \
    edge->error.member += edge->remainder.member; \
    if (edge->error.member >= edge->span) { \
        edge->error.member -= edge->span; \
        ++edge->value.member; \
    } \
} while (0)
    V9X_EDGE_NEXT(x);
    V9X_EDGE_NEXT(z);
    V9X_EDGE_NEXT(u);
    V9X_EDGE_NEXT(v);
    V9X_EDGE_NEXT(red);
    V9X_EDGE_NEXT(green);
    V9X_EDGE_NEXT(blue);
    V9X_EDGE_NEXT(alpha);
    V9X_EDGE_NEXT(q);
#undef V9X_EDGE_NEXT
    edge->value.y += V9X_D3D_RASTER_SUBPIXEL_ONE;
}

/*
 * The span's pixel store under the colour mask. The unmasked case is the
 * plain store it always was; the masked one reads the pixel back and keeps
 * the channels the target does not write. A macro because it sits in the
 * per-pixel loop, and it names the span's own locals.
 */
#define V9X_D3D_RASTER_STORE(out_red, out_green, out_blue) do { \
    if (write_mask == 0xfffful) { \
        pixels[column] = pack(out_red, out_green, out_blue); \
    } else { \
        pixels[column] = (v9x_u16)(((v9x_u32)pixels[column] & ~write_mask) | \
                                   ((v9x_u32)pack(out_red, out_green, \
                                                  out_blue) & write_mask)); \
    } \
} while (0)

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
                                const V9X_D3D_RASTER_SAMPLER *sampler,
                                const V9X_D3D_RASTER_ALPHA *alpha,
                                const V9X_D3D_RASTER_ALPHA_TEST *alpha_test,
                                int perspective,
                                const V9X_D3D_RASTER_GRADIENTS *slopes,
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
    v9x_s32 alpha_step = 0l;
    v9x_s32 z_step = 0l;
    v9x_s32 u_step = 0l;
    v9x_s32 v_step = 0l;
    v9x_s32 q_step = 0l;
    /* The level of detail, 16.16; on the affine path once per span, on the
     * perspective path per pixel from there. Only read when `slopes` is
     * set, which is when the sampler selects a level. */
    v9x_s32 lod = 0l;
    v9x_s32 red;
    v9x_s32 green;
    v9x_s32 blue;
    v9x_s32 fragment_alpha;
    v9x_s32 z;
    v9x_s32 u;
    v9x_s32 v;
    /* q, on the perspective path only; u and v are then u * q and v * q. */
    v9x_s32 q;
    v9x_s32 offset;
    v9x_s32 column;
    v9x_u16 *pixels;
    v9x_u16 *depths = 0;
    /* Both factors resolved once per span. A source factor of ONE is a
     * constant 256 whatever the fragment's alpha is, so only SRCALPHA has to
     * be recomputed per pixel, and the flag says which. */
    int alpha_varies = 0;
    /* The general per-channel blend path, for any pair outside the five the
     * weight arithmetic below was written for. */
    int blend_general = 0;
    /* DESTCOLOR is not a weight. It scales each channel by that channel's own
     * stored value, so it cannot join the pair below and is carried as its
     * own per-span flag; the weights then apply to the product. */
    int modulate_destination = 0;
    v9x_s32 source_weight = 256l;
    v9x_s32 destination_weight = 0l;
    /* The three per-pixel dispatches the loop used to make, made once here.
     * Target format is validated to one of the two, so 565 is the remaining
     * case rather than a guess at an unknown one. */
    V9X_D3D_RASTER_PACK pack = v9x_d3d_raster_pack565;
    V9X_D3D_RASTER_UNPACK unpack = v9x_d3d_raster_unpack565;
    v9x_s32 depth_mask = 0l;
    v9x_s32 alpha_test_mask = 0l;
    /* Whether the fragment's alpha is consumed at all this span: by the
     * blend, by the alpha test, or by a texel alpha op. When it is not, the
     * per-pixel clamp and combine are skipped as they always were. */
    int alpha_used = 0;
    /* The bits of a packed pixel this span may change: all of them unless
     * the target masks a channel, in which case the store below keeps the
     * rest of what was there. */
    v9x_u32 write_mask = 0xfffful;

    if (first < 0l) {
        first = 0l;
    }
    if (last > (v9x_s32)target->width) {
        last = (v9x_s32)target->width;
    }
    /* The scissor's columns, inside the target by validation. The stepping
     * below counts from the span's own left edge, so a clipped-off start is
     * walked over by the initial offset like any other, and the gradients
     * are unaffected. */
    if (first < (v9x_s32)target->clip_left) {
        first = (v9x_s32)target->clip_left;
    }
    if (last > (v9x_s32)target->clip_right) {
        last = (v9x_s32)target->clip_right;
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
        alpha_step = ((right->alpha - left->alpha) <<
                      V9X_D3D_RASTER_COLOUR_BITS) / width;
        z_step = ((right->z - left->z) << V9X_D3D_RASTER_DEPTH_BITS) / width;
        /* Texture coordinates share the depth interpolator's eight fractional
         * bits, and for the same reason: both run to 65535, and sixteen would
         * put the step alone outside a signed 32-bit integer. */
        u_step = ((right->u - left->u) << V9X_D3D_RASTER_DEPTH_BITS) / width;
        v_step = ((right->v - left->v) << V9X_D3D_RASTER_DEPTH_BITS) / width;
        q_step = ((right->q - left->q) << V9X_D3D_RASTER_DEPTH_BITS) / width;
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
    fragment_alpha = (left->alpha << V9X_D3D_RASTER_COLOUR_BITS) +
                     alpha_step * offset;
    z = (left->z << V9X_D3D_RASTER_DEPTH_BITS) + z_step * offset;
    u = (left->u << V9X_D3D_RASTER_DEPTH_BITS) + u_step * offset;
    v = (left->v << V9X_D3D_RASTER_DEPTH_BITS) + v_step * offset;
    q = (left->q << V9X_D3D_RASTER_DEPTH_BITS) + q_step * offset;
    red_step <<= V9X_D3D_RASTER_SUBPIXEL_BITS;
    green_step <<= V9X_D3D_RASTER_SUBPIXEL_BITS;
    blue_step <<= V9X_D3D_RASTER_SUBPIXEL_BITS;
    alpha_step <<= V9X_D3D_RASTER_SUBPIXEL_BITS;
    z_step <<= V9X_D3D_RASTER_SUBPIXEL_BITS;
    u_step <<= V9X_D3D_RASTER_SUBPIXEL_BITS;
    v_step <<= V9X_D3D_RASTER_SUBPIXEL_BITS;
    q_step <<= V9X_D3D_RASTER_SUBPIXEL_BITS;

    if (slopes != 0 && !perspective) {
        /* Affine: the derivatives are the same at every pixel of the span,
         * so the level is chosen once. The steps are 8.24 per pixel here. */
        lod = v9x_d3d_raster_lod(sampler, u_step / 256l, v_step / 256l,
                                 slopes->du_dy, slopes->dv_dy);
    }

    if (alpha != 0) {
        int legacy = (alpha->src == V9X_D3D_RASTER_BLEND_SRC_ONE ||
                      alpha->src == V9X_D3D_RASTER_BLEND_SRC_SRCALPHA ||
                      alpha->src == V9X_D3D_RASTER_BLEND_SRC_DESTCOLOR) &&
                     (alpha->dst == V9X_D3D_RASTER_BLEND_DST_ZERO ||
                      alpha->dst == V9X_D3D_RASTER_BLEND_DST_INVSRCALPHA);

        blend_general = !legacy;
        alpha_varies = alpha->src == V9X_D3D_RASTER_FACTOR_SRCALPHA ||
                       alpha->src == V9X_D3D_RASTER_FACTOR_INVSRCALPHA ||
                       alpha->dst == V9X_D3D_RASTER_FACTOR_SRCALPHA ||
                       alpha->dst == V9X_D3D_RASTER_FACTOR_INVSRCALPHA;
        modulate_destination =
            legacy && alpha->src == V9X_D3D_RASTER_BLEND_SRC_DESTCOLOR;
    }

    if (alpha_test != 0) {
        alpha_test_mask = v9x_d3d_raster_depth_mask(alpha_test->compare);
    }
    alpha_used = alpha_varies || alpha_test != 0 ||
                 (sampler != 0 &&
                  sampler->alpha_op != V9X_D3D_RASTER_TEXALPHA_IGNORE);

    if (target->format == V9X_D3D_RASTER_PIXFMT_XRGB1555) {
        pack = v9x_d3d_raster_pack1555;
        unpack = v9x_d3d_raster_unpack1555;
    }
    if (target->write_red == 0ul || target->write_green == 0ul ||
        target->write_blue == 0ul) {
        int fifteen = target->format == V9X_D3D_RASTER_PIXFMT_XRGB1555;

        write_mask = 0ul;
        if (target->write_red != 0ul) {
            write_mask |= fifteen ? 0x7c00ul : 0xf800ul;
        }
        if (target->write_green != 0ul) {
            write_mask |= fifteen ? 0x03e0ul : 0x07e0ul;
        }
        if (target->write_blue != 0ul) {
            write_mask |= 0x001ful;
        }
    }

    pixels = (v9x_u16 *)((v9x_u8 *)target->pixels +
                         (v9x_u32)row * target->pitch);
    if (depth != 0) {
        depths = (v9x_u16 *)((v9x_u8 *)depth->pixels +
                             (v9x_u32)row * depth->pitch);
        depth_mask = v9x_d3d_raster_depth_mask(depth->compare);
    }
    for (column = first; column < last; ++column) {
        int visible = 1;
        /* The depth the fragment will store, held until the alpha test has
         * had its say: a discarded fragment writes neither colour nor depth,
         * which is the pipeline's order in both APIs. */
        v9x_s32 depth_fragment = 0l;

        if (depths != 0) {
            /* Clamped before the comparison, not after: the interpolator can
             * land a fraction of a level outside the endpoints, and a depth
             * that wrapped would compare against the wrong end of the buffer
             * rather than merely being one level out. This clamp stays a pair
             * of compares against its own bound - it is not a colour channel,
             * and 0..65535 is the buffer's whole range rather than 0..255. */
            v9x_s32 fragment = z >> V9X_D3D_RASTER_DEPTH_BITS;
            v9x_s32 stored;
            v9x_s32 relation;

            if (fragment < 0l) {
                fragment = 0l;
            }
            if (fragment > V9X_D3D_RASTER_DEPTH_MAX) {
                fragment = V9X_D3D_RASTER_DEPTH_MAX;
            }
            stored = (v9x_s32)depths[column];
            relation = V9X_D3D_RASTER_RELATION_EQUAL;
            if (fragment < stored) {
                relation = V9X_D3D_RASTER_RELATION_LESS;
            } else if (fragment > stored) {
                relation = V9X_D3D_RASTER_RELATION_GREATER;
            }
            visible = (depth_mask & relation) != 0l;
            depth_fragment = fragment;
        }

        if (visible) {
            /*
             * Clamped once, here, and the position is load-bearing: it has to
             * be before the texture stage, because modulate multiplies by
             * this channel and a negative factor would brighten rather than
             * darken - the reason the clamp was written where the modulate
             * arm used to hold it.
             *
             * Doing it here instead is what lets the blend arm's copy and the
             * packer's go. A modulated channel is already inside 0..255 by
             * the time it reaches the blend, a decalled one is a decoded
             * texel, and an untextured one has been clamped on these three
             * lines, so neither stage had anything left to correct. The
             * exported packers keep the guard for callers that are not this
             * loop.
             */
            v9x_s32 out_red = red >> V9X_D3D_RASTER_COLOUR_BITS;
            v9x_s32 out_green = green >> V9X_D3D_RASTER_COLOUR_BITS;
            v9x_s32 out_blue = blue >> V9X_D3D_RASTER_COLOUR_BITS;
            /* The fragment's alpha, resolved only when something consumes
             * it: the vertex's, then the texel's if the draw says so. */
            v9x_s32 out_alpha = 255l;
            v9x_s32 tex_alpha = 255l;

            V9X_D3D_RASTER_CLAMP255(out_red);
            V9X_D3D_RASTER_CLAMP255(out_green);
            V9X_D3D_RASTER_CLAMP255(out_blue);

            if (sampler != 0) {
                v9x_s32 tex_red;
                v9x_s32 tex_green;
                v9x_s32 tex_blue;
                v9x_s32 texel_u;
                v9x_s32 texel_v;
                v9x_s32 lambda = lod;
                v9x_s32 q16 = 0l;
                v9x_s32 reciprocal = 0l;

                if (perspective) {
                    /* q back to 1.16 - never below 1, since it is
                     * interpolated between vertices the entry checked - and
                     * one reciprocal for both axes. */
                    q16 = q >> V9X_D3D_RASTER_DEPTH_BITS;
                    if (q16 < 1l) {
                        q16 = 1l;
                    }
                    reciprocal = V9X_D3D_RASTER_Q_RECIPROCAL_ONE / q16;
                    texel_u = v9x_d3d_raster_divide_by_q(
                        u >> V9X_D3D_RASTER_DEPTH_BITS, reciprocal);
                    texel_v = v9x_d3d_raster_divide_by_q(
                        v >> V9X_D3D_RASTER_DEPTH_BITS, reciprocal);
                    if (slopes != 0) {
                        /* The derivatives of (u * q) / q change along the
                         * span, so the level is chosen per pixel - four
                         * divides, the price of the plan's correctness-first
                         * rule; the subdivided variants are its listed
                         * follow-up. */
                        lambda = v9x_d3d_raster_lod(
                            sampler,
                            v9x_d3d_raster_derivative_q(
                                u_step / 256l, texel_u, q_step / 256l, q16,
                                reciprocal),
                            v9x_d3d_raster_derivative_q(
                                v_step / 256l, texel_v, q_step / 256l, q16,
                                reciprocal),
                            v9x_d3d_raster_derivative_q(
                                slopes->du_dy, texel_u, slopes->dq_dy, q16,
                                reciprocal),
                            v9x_d3d_raster_derivative_q(
                                slopes->dv_dy, texel_v, slopes->dq_dy, q16,
                                reciprocal));
                    }
                } else {
                    texel_u = u >> V9X_D3D_RASTER_DEPTH_BITS;
                    texel_v = v >> V9X_D3D_RASTER_DEPTH_BITS;
                }

                /*
                 * Under CLAMP a coordinate past the end takes the edge texel;
                 * under WRAP it is folded into the first repeat.
                 *
                 * The lower bound is applied either way and it is not the
                 * address mode talking. The sampler shifts right to find a
                 * texel index, an arithmetic shift of a negative value is
                 * implementation defined, and the mask would then index
                 * backwards off the surface - so a negative coordinate is a
                 * memory-safety question rather than a wrapping one.
                 * v9x_d3d_raster_triangle already refuses a negative vertex
                 * coordinate; this catches an interpolator that has drifted a
                 * fraction below zero between two non-negative endpoints.
                 */
                if (texel_u < 0l) {
                    texel_u = 0l;
                }
                if (texel_v < 0l) {
                    texel_v = 0l;
                }
                if (sampler->clamp) {
                    /* To the last texel of the FIRST repeat, which is what
                     * clamping means - not to TEXCOORD_MAX, which is the
                     * widest coordinate the arithmetic carries and is
                     * thirty-three repeats away. Clamping there would let a
                     * clamped draw tile, which is the defect this arm exists
                     * to prevent, and a host test caught exactly that. */
                    if (texel_u > V9X_D3D_RASTER_TEXCOORD_ONE - 1l) {
                        texel_u = V9X_D3D_RASTER_TEXCOORD_ONE - 1l;
                    }
                    if (texel_v > V9X_D3D_RASTER_TEXCOORD_ONE - 1l) {
                        texel_v = V9X_D3D_RASTER_TEXCOORD_ONE - 1l;
                    }
                } else {
                    /*
                     * Folded unconditionally, where this used to test against
                     * TEXCOORD_MAX first and fold only past it. The two are
                     * the same picture: the sampler's `& mask` already
                     * discards everything above the first repeat, so dropping
                     * those bits here cannot change which texel is read, and
                     * it cannot change a bilinear fraction either - the bits
                     * removed are a multiple of a whole texture, which is a
                     * multiple of both 65536 and 256.
                     *
                     * What it buys is the bound. The coordinate handed to the
                     * sampler is now under one repeat rather than under
                     * thirty-three, so scaling it into texel units is a shift
                     * that cannot overflow rather than a multiply sized
                     * against TEXCOORD_MAX.
                     */
                    texel_u &= V9X_D3D_RASTER_TEXCOORD_ONE - 1l;
                    texel_v &= V9X_D3D_RASTER_TEXCOORD_ONE - 1l;
                }
                if (!sampler->select) {
                    v9x_d3d_raster_sample(sampler, &sampler->levels[0],
                                          texel_u, texel_v, &tex_red,
                                          &tex_green, &tex_blue, &tex_alpha);
                } else {
                    v9x_d3d_raster_sample_mip(sampler, lambda, texel_u,
                                              texel_v, &tex_red, &tex_green,
                                              &tex_blue, &tex_alpha);
                }
                if (sampler->modulate) {
                    /* Both factors are 0..255 - the texel by decode, the
                     * interpolant by the clamp above - which is what puts the
                     * rounded product inside the exact divide's range. */
                    out_red = V9X_D3D_RASTER_DIV255(tex_red * out_red + 127l);
                    out_green =
                        V9X_D3D_RASTER_DIV255(tex_green * out_green + 127l);
                    out_blue =
                        V9X_D3D_RASTER_DIV255(tex_blue * out_blue + 127l);
                } else {
                    out_red = tex_red;
                    out_green = tex_green;
                    out_blue = tex_blue;
                }
            }

            if (alpha_used) {
                out_alpha = fragment_alpha >> V9X_D3D_RASTER_COLOUR_BITS;
                V9X_D3D_RASTER_CLAMP255(out_alpha);
                if (sampler != 0) {
                    if (sampler->alpha_op == V9X_D3D_RASTER_TEXALPHA_REPLACE) {
                        out_alpha = tex_alpha;
                    } else if (sampler->alpha_op ==
                               V9X_D3D_RASTER_TEXALPHA_MODULATE) {
                        out_alpha = V9X_D3D_RASTER_DIV255(tex_alpha * out_alpha +
                                                          127l);
                    }
                }
                if (alpha_test != 0) {
                    v9x_s32 relation = V9X_D3D_RASTER_RELATION_EQUAL;

                    if (out_alpha < alpha_test->reference) {
                        relation = V9X_D3D_RASTER_RELATION_LESS;
                    } else if (out_alpha > alpha_test->reference) {
                        relation = V9X_D3D_RASTER_RELATION_GREATER;
                    }
                    if ((alpha_test_mask & relation) == 0l) {
                        /* Discarded: no colour, no depth, and the
                         * interpolants still step below. */
                        red += red_step;
                        green += green_step;
                        blue += blue_step;
                        fragment_alpha += alpha_step;
                        z += z_step;
                        u += u_step;
                        v += v_step;
                        q += q_step;
                        continue;
                    }
                }
            }
            if (depths != 0 && depth->write != 0ul) {
                depths[column] = (v9x_u16)depth_fragment;
            }

            if (alpha != 0) {
                v9x_u16 stored = pixels[column];
                v9x_s32 dst_red;
                v9x_s32 dst_green;
                v9x_s32 dst_blue;

                /* No clamp here any more. A source channel outside 0..255
                 * would subtract from the destination instead of adding to
                 * it - a dark fringe along the edge of every blended triangle
                 * rather than an obviously wrong colour - and that is still
                 * true; it is the span's own clamp, above, that now
                 * guarantees the range on every path into this arm. */
                unpack(stored, &dst_red, &dst_green, &dst_blue);
                if (blend_general) {
                    v9x_s32 src_factor_red;
                    v9x_s32 src_factor_green;
                    v9x_s32 src_factor_blue;
                    v9x_s32 dst_factor_red;
                    v9x_s32 dst_factor_green;
                    v9x_s32 dst_factor_blue;

                    v9x_d3d_raster_factor(alpha->src, out_red, out_green,
                                          out_blue, dst_red, dst_green,
                                          dst_blue, out_alpha,
                                          &src_factor_red, &src_factor_green,
                                          &src_factor_blue);
                    v9x_d3d_raster_factor(alpha->dst, out_red, out_green,
                                          out_blue, dst_red, dst_green,
                                          dst_blue, out_alpha,
                                          &dst_factor_red, &dst_factor_green,
                                          &dst_factor_blue);
                    V9X_D3D_RASTER_BLEND_GENERAL(out_red, out_red, dst_red,
                                                 src_factor_red,
                                                 dst_factor_red);
                    V9X_D3D_RASTER_BLEND_GENERAL(out_green, out_green,
                                                 dst_green, src_factor_green,
                                                 dst_factor_green);
                    V9X_D3D_RASTER_BLEND_GENERAL(out_blue, out_blue, dst_blue,
                                                 src_factor_blue,
                                                 dst_factor_blue);
                    V9X_D3D_RASTER_STORE(out_red, out_green, out_blue);
                    red += red_step;
                    green += green_step;
                    blue += blue_step;
                    fragment_alpha += alpha_step;
                    z += z_step;
                    u += u_step;
                    v += v_step;
                    q += q_step;
                    continue;
                }
                if (alpha_varies) {
                    v9x_s32 weight = v9x_d3d_raster_weight(out_alpha);

                    if (alpha->src == V9X_D3D_RASTER_BLEND_SRC_SRCALPHA) {
                        source_weight = weight;
                    }
                    if (alpha->dst ==
                        V9X_D3D_RASTER_BLEND_DST_INVSRCALPHA) {
                        destination_weight = 256l - weight;
                    }
                }
                if (modulate_destination) {
                    /* Both factors are 0..255 - the source by the span clamp
                     * above, the destination by expand5/expand6 - so the
                     * rounded product is inside the exact divide's range.
                     * The divide has to be exact rather than a shift by
                     * eight: a lightmap that lights nothing multiplies by
                     * white, and an approximate divide would return the
                     * frame one level darker everywhere it passed. */
                    out_red =
                        V9X_D3D_RASTER_DIV255(out_red * dst_red + 127l);
                    out_green =
                        V9X_D3D_RASTER_DIV255(out_green * dst_green + 127l);
                    out_blue =
                        V9X_D3D_RASTER_DIV255(out_blue * dst_blue + 127l);
                }
                out_red = v9x_d3d_raster_blend(out_red, dst_red,
                                               source_weight,
                                               destination_weight);
                out_green = v9x_d3d_raster_blend(out_green, dst_green,
                                                 source_weight,
                                                 destination_weight);
                out_blue = v9x_d3d_raster_blend(out_blue, dst_blue,
                                                source_weight,
                                                destination_weight);
            }

            V9X_D3D_RASTER_STORE(out_red, out_green, out_blue);
        }

        /* Stepped for every pixel, drawn or not. A failed depth test skips
         * the write, not the interpolation - advancing only on visible pixels
         * would tilt the gradient behind anything occluding the span. */
        red += red_step;
        green += green_step;
        blue += blue_step;
        fragment_alpha += alpha_step;
        z += z_step;
        u += u_step;
        v += v_step;
        q += q_step;
    }
}

int v9x_d3d_raster_triangle(const V9X_D3D_RASTER_TARGET *target,
                            const V9X_D3D_RASTER_DEPTH *depth,
                            const V9X_D3D_RASTER_TEXTURE *texture,
                            const V9X_D3D_RASTER_ALPHA *alpha,
                            const V9X_D3D_RASTER_ALPHA_TEST *alpha_test,
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

    V9X_D3D_RASTER_EDGE along;
    V9X_D3D_RASTER_EDGE across;
    int lower = 0;
    /* The sampler is built once for the whole triangle rather than per row,
     * because nothing in it depends on the row. `bound` is what the spans
     * receive, and a null one is what untextured means. */
    V9X_D3D_RASTER_SAMPLER sampler;
    const V9X_D3D_RASTER_SAMPLER *bound = 0;
    /* The perspective path walks a copy of the vertices with u and v
     * already multiplied by q; the affine path walks the caller's. */
    V9X_D3D_RASTER_VERTEX carried[3];
    const V9X_D3D_RASTER_VERTEX *walked = vertices;
    int perspective = 0;
    /* The plane gradients along y, formed once per triangle when a level
     * has to be chosen; null otherwise, and the span never reads them. */
    V9X_D3D_RASTER_GRADIENTS gradients;
    const V9X_D3D_RASTER_GRADIENTS *slopes = 0;

    if (!v9x_d3d_raster_target_valid(target) || vertices == 0) {
        return 0;
    }
    /* Like the blend: null is off, and a non-null test that fails its own
     * check is a caller error, refused rather than skipped. */
    if (alpha_test != 0 && !v9x_d3d_raster_alpha_test_valid(alpha_test)) {
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
    if (alpha != 0 && !v9x_d3d_raster_alpha_valid(alpha)) {
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
            vertices[index].v > V9X_D3D_RASTER_TEXCOORD_MAX ||
            vertices[index].q < 1l ||
            vertices[index].q > V9X_D3D_RASTER_Q_ONE) {
            return 0;
        }
    }

    /* After validation, never before: the sampler reads the size, format and
     * render states as facts. */
    if (texture != 0) {
        v9x_d3d_raster_sampler_start(texture, &sampler);
        bound = &sampler;
    }

    /* Perspective when the three q differ (see V9X_D3D_RASTER_VERTEX.q).
     * Equal q, whatever the value, leaves the caller's coordinates and every
     * line below exactly as they were. */
    if (vertices[0].q != vertices[1].q || vertices[1].q != vertices[2].q) {
        for (index = 0ul; index < 3ul; ++index) {
            carried[index] = vertices[index];
            carried[index].u = v9x_d3d_raster_scale_by_q(vertices[index].u,
                                                         vertices[index].q);
            carried[index].v = v9x_d3d_raster_scale_by_q(vertices[index].v,
                                                         vertices[index].q);
        }
        walked = carried;
        perspective = 1;
    }

    if (bound != 0 && bound->select) {
        gradients.du_dy = v9x_d3d_raster_gradient_y(
            &walked[0], &walked[1], &walked[2],
            walked[0].u, walked[1].u, walked[2].u);
        gradients.dv_dy = v9x_d3d_raster_gradient_y(
            &walked[0], &walked[1], &walked[2],
            walked[0].v, walked[1].v, walked[2].v);
        gradients.dq_dy = perspective ? v9x_d3d_raster_gradient_y(
            &walked[0], &walked[1], &walked[2],
            walked[0].q, walked[1].q, walked[2].q) : 0l;
        slopes = &gradients;
    }

    top = &walked[0];
    middle = &walked[1];
    bottom = &walked[2];
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
    /* The scissor's rows. The validator has put the rectangle inside the
     * target, so these only ever narrow the two clamps above. */
    if (first_row < (v9x_s32)target->clip_top) {
        first_row = (v9x_s32)target->clip_top;
    }
    if (last_row > (v9x_s32)target->clip_bottom) {
        last_row = (v9x_s32)target->clip_bottom;
    }

    if (first_row >= last_row) {
        return 1;
    }
    {
        v9x_s32 sample = (first_row << V9X_D3D_RASTER_SUBPIXEL_BITS) +
                         V9X_D3D_RASTER_SUBPIXEL_HALF;
        lower = sample >= middle->y;
        if (!v9x_d3d_raster_edge_start(top, bottom, sample, &along) ||
            !v9x_d3d_raster_edge_start(lower ? middle : top,
                                       lower ? bottom : middle, sample, &across)) {
            return 1;
        }
    }
    for (row = first_row; row < last_row; ++row) {
        if (along.value.x <= across.value.x) {
            v9x_d3d_raster_span(target, depth, bound, alpha, alpha_test,
                                perspective, slopes, row,
                                &along.value, &across.value);
        } else {
            v9x_d3d_raster_span(target, depth, bound, alpha, alpha_test,
                                perspective, slopes, row,
                                &across.value, &along.value);
        }
        if (row + 1l < last_row) {
            v9x_d3d_raster_edge_next(&along);
            /* Reinitialize exactly at the first sample on the lower edge;
             * stepping the upper edge past the middle would extrapolate. */
            if (!lower && along.value.y >= middle->y) {
                lower = 1;
                if (!v9x_d3d_raster_edge_start(middle, bottom, along.value.y, &across)) {
                    return 1;
                }
            } else {
                v9x_d3d_raster_edge_next(&across);
            }
        }
    }
    return 1;
}
