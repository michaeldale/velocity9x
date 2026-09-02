/*
 * The CPU rasterizer's arithmetic: one Gouraud triangle into a caller-supplied
 * 16 bpp surface.
 *
 * This is a leaf translation unit by design. It includes nothing from the
 * DDHAL side, holds no state, touches no register and knows nothing about
 * DirectDraw, so scripts\build-host.ps1 compiles it and
 * tests\host\test_d3d_raster.c holds it to a pixel table on the host. That is
 * the whole point of the split: the one part of mode 2 that is arithmetic
 * rather than plumbing is the one part that can be tested without a guest, and
 * this project's Direct3D history says the plumbing is what actually breaks.
 * d3d_zfixed.h makes the same argument at greater length.
 *
 * It stays under src\display32\d3d rather than src\common because it is the
 * software engine's half of the D3D core/engine split
 * (docs\decisions\2026-08-29-d3d-core-engine-split.md), the same as the ViRGE
 * depth encoding next to it. The host build reaching in is a build-list entry,
 * not a reason to move the file.
 *
 * docs\plans\s3-trio64-voodoo2-hybrid-3d.md, mode 2, work-order step 6.
 */
#ifndef VELOCITY9X_D3D_RASTER_H
#define VELOCITY9X_D3D_RASTER_H

#include "velocity9x/types.h"

/*
 * Screen coordinates are 28.4 fixed point - whole pixels in the top bits, a
 * sixteenth of a pixel in the bottom four. The caller converts; nothing in
 * this file is a float.
 *
 * Integer-only is not an aesthetic choice. Open Watcom lowers a float-to-int
 * cast to a call to its runtime helper __CHP, the HAL links with `option
 * nodefaultlibs`, and the only way round it in this tree is a #pragma aux
 * fistp - which is Watcom assembly and would make this file uncompilable by
 * the MSVC host pass. Keeping the conversion in the engine leaves the
 * arithmetic portable, and the arithmetic is what the tests are for.
 */
#define V9X_D3D_RASTER_SUBPIXEL_BITS 4
#define V9X_D3D_RASTER_SUBPIXEL_ONE  16l
#define V9X_D3D_RASTER_SUBPIXEL_HALF 8l

/*
 * The largest render target this rasterizer accepts, in pixels, and the
 * largest coordinate that follows from it.
 *
 * This is an overflow bound, not a taste. Every product formed below is
 * bounded by coordinate * coordinate, and 32752 * 32752 is 1,072,693,504 -
 * inside a signed 32-bit integer with room to spare. Raising the dimension to
 * 4096 would make the same product 4,292,870,400 and every span in the driver
 * would go wrong silently, on large modes only. The software engine's
 * target_dimension_max must not exceed this; d3d_soft.c asserts that at
 * compile time.
 */
#define V9X_D3D_RASTER_DIMENSION_MAX 2048ul
#define V9X_D3D_RASTER_COORD_MAX \
    ((((v9x_s32)V9X_D3D_RASTER_DIMENSION_MAX) - 1l) << V9X_D3D_RASTER_SUBPIXEL_BITS)

/*
 * The depth buffer is 16 bits, so a depth value is 0..65535 and the caller
 * scales into that range. Sixteen because it is what the ViRGE's depth unit
 * carries and what `V9X_D3D_ENGINE_LIMITS.depth_bits_per_pixel` already says
 * this driver allocates; the software engine gains nothing from being
 * different and loses interchangeability.
 */
#define V9X_D3D_RASTER_DEPTH_MAX 65535l

/*
 * Fractional bits in the span's depth interpolator - eight, where colour uses
 * sixteen, and the difference is arithmetic rather than taste. A colour
 * channel is 0..255 and shifting it by sixteen leaves eight bits spare; a
 * depth is 0..65535 and shifting it by sixteen would need thirty-two, so the
 * step alone would overflow before a single pixel was drawn. Eight leaves the
 * same eight bits of headroom, and a 1/256th of a level per subpixel is far
 * finer than the buffer can record.
 */
#define V9X_D3D_RASTER_DEPTH_BITS 8

/*
 * The eight comparison functions, numbered as Direct3D numbers them.
 *
 * The values are deliberately `D3DCMP_*`: the engine holds the render state as
 * the runtime gave it and passes it straight through, so a translation table
 * here would be a second place to get the order wrong. `d3d_soft.c` asserts
 * the equality at compile time rather than trusting this comment. The ViRGE
 * engine does need a table, because the S3D unit's own encoding differs from
 * `D3DCMP - 1` in six of the eight - see `v9x_d3d_z_compare` there.
 */
#define V9X_D3D_RASTER_CMP_NEVER        1ul
#define V9X_D3D_RASTER_CMP_LESS         2ul
#define V9X_D3D_RASTER_CMP_EQUAL        3ul
#define V9X_D3D_RASTER_CMP_LESSEQUAL    4ul
#define V9X_D3D_RASTER_CMP_GREATER      5ul
#define V9X_D3D_RASTER_CMP_NOTEQUAL     6ul
#define V9X_D3D_RASTER_CMP_GREATEREQUAL 7ul
#define V9X_D3D_RASTER_CMP_ALWAYS       8ul

/*
 * Texture coordinates in units of one 65536th of a texture, so 65536 is one
 * whole repeat and the coordinate tiles rather than describing a single pass.
 *
 * It described a single pass until 2026-09-02, and that was an arithmetic
 * limit rather than a design preference: the edge interpolator formed
 * max(from, to) * denominator, the denominator being the triangle's height in
 * subpixels and at most 32752, so anything it carried had to stay under 65566.
 * v9x_d3d_raster_lerp now divides before it multiplies and its largest
 * intermediate no longer depends on the value being interpolated, which is
 * what makes a wider coordinate possible at all.
 *
 * What bounds it now is the sampler. v9x_d3d_raster_sample forms u * size with
 * size at most 512, and the bilinear arm adds a whole texture of bias on top,
 * so the coordinate has to satisfy (max * 512) + (512 << 16) < 2^31. At
 * thirty-three repeats that is 1,140,850,688, which leaves most of a bit
 * spare.
 *
 * Thirty-three and not thirty-two because the engine normalises: it clamps a
 * float coordinate to plus or minus sixteen repeats, so a triangle can span
 * thirty-two of them, and shifting the smallest corner into the first repeat
 * leaves the largest just short of the thirty-third. See
 * V9X_D3D_SOFT_TEXCOORD_LIMIT in d3d_soft.c, which is the other half of this
 * number.
 */
#define V9X_D3D_RASTER_TEXCOORD_ONE     65536l
#define V9X_D3D_RASTER_TEXCOORD_REPEATS 33l
#define V9X_D3D_RASTER_TEXCOORD_MAX \
    (V9X_D3D_RASTER_TEXCOORD_ONE * V9X_D3D_RASTER_TEXCOORD_REPEATS - 1l)

/*
 * What happens to a coordinate outside the first repeat, numbered as
 * D3DTADDRESS_* numbers it.
 *
 * MIRROR is absent because the sampler wraps with a mask, which mirrors
 * nothing, and describe_caps advertises neither it nor BORDER.
 */
#define V9X_D3D_RASTER_ADDRESS_WRAP  1ul
#define V9X_D3D_RASTER_ADDRESS_CLAMP 3ul

/*
 * The two texture formats, numbered as this file likes: unlike the comparison
 * functions below, these correspond to no Direct3D constant - a D3D texture
 * format is a whole DDPIXELFORMAT - so the engine classifies the surface and
 * hands one of these over. They are the same two the ViRGE's sampler accepts,
 * deliberately: the two engines are meant to be interchangeable, and a
 * software path that took formats the hardware path refuses would be a
 * difference nobody asked for.
 */
#define V9X_D3D_RASTER_TEXFMT_ARGB1555 1ul
#define V9X_D3D_RASTER_TEXFMT_ARGB4444 2ul
/*
 * RGB565, and the software engine's alone. The S3D texture unit's format
 * field carries ARGB8888, ARGB4444 and ARGB1555 and nothing else, so this
 * engine accepts a texel layout the hardware path must keep refusing - the
 * one place the two deliberately differ.
 */
#define V9X_D3D_RASTER_TEXFMT_RGB565   3ul

/*
 * Filter and blend, numbered as D3DFILTER_* and D3DTBLEND_* number them, for
 * the same reason the comparison functions are: the engine passes the render
 * state through and asserts the equality at compile time. Only these values
 * are implemented - the mip filters have no meaning here, because nothing
 * selects a mip level, and `describe_caps` advertises none of them.
 */
/*
 * The blend factors, numbered as D3DBLEND_* numbers them, and only the four
 * the engine implements.
 *
 * Four rather than eleven, and the four are not an arbitrary subset: they are
 * the ones S3's own ViRGE driver publishes on this generation of silicon -
 * D3DPBLENDCAPS_ONE | SRCALPHA for source, ZERO | INVSRCALPHA for destination
 * (98DDK D3DDRV.C:239-242). ONE with ZERO is opaque, SRCALPHA with INVSRCALPHA
 * is ordinary transparency, and between them they are what a period
 * application actually asks for. Anything else is refused rather than
 * approximated, and describe_caps advertises exactly these.
 */
#define V9X_D3D_RASTER_BLEND_SRC_ONE         2ul
#define V9X_D3D_RASTER_BLEND_SRC_SRCALPHA    5ul
#define V9X_D3D_RASTER_BLEND_DST_ZERO        1ul
#define V9X_D3D_RASTER_BLEND_DST_INVSRCALPHA 6ul

#define V9X_D3D_RASTER_FILTER_POINT   1ul
#define V9X_D3D_RASTER_FILTER_LINEAR  2ul
#define V9X_D3D_RASTER_BLEND_DECAL    1ul
#define V9X_D3D_RASTER_BLEND_MODULATE 2ul

/* Square, power-of-two texture edge bounds in texels, matching the ViRGE. */
#define V9X_D3D_RASTER_TEXTURE_SIZE_MIN 4ul
#define V9X_D3D_RASTER_TEXTURE_SIZE_MAX 512ul

/*
 * One vertex, already transformed, clipped and unpacked by the caller.
 *
 * Colour is three separate 0..255 channels rather than a packed DWORD because
 * every channel is interpolated independently and packing it here would mean
 * unpacking it again per pixel. The caller owns the packed form. `z` is
 * 0..V9X_D3D_RASTER_DEPTH_MAX and is ignored entirely when the draw carries no
 * depth buffer.
 *
 * Unlike `V9X_D3D_ENGINE_LIMITS`, this struct is safe to insert a field into:
 * nothing initialises it positionally, in the driver or in the tests. That is
 * why `z` sits with the other two coordinates instead of being appended after
 * the colours, and it is worth stating, because the append-only rule elsewhere
 * in this driver exists for initialiser lists rather than for structs.
 */
typedef struct v9x_d3d_raster_vertex {
    v9x_s32 x;
    v9x_s32 y;
    v9x_s32 z;
    v9x_s32 u;
    v9x_s32 v;
    v9x_s32 red;
    v9x_s32 green;
    v9x_s32 blue;
    /* 0..255, interpolated exactly as the three colour channels are and used
     * only when the draw carries a blend. Out-of-range values are clamped
     * where they are consumed rather than refused, which is what the colour
     * channels do and for the same reason: the interpolator's endpoints can
     * sit a fraction outside the range the caller wrote. */
    v9x_s32 alpha;
} V9X_D3D_RASTER_VERTEX;

/*
 * The depth buffer and the two render states that drive it.
 *
 * Passed per draw rather than living in the target, because it is render state
 * and can change between two draws into the same surface. A null pointer for
 * the whole struct means no depth at all - no test, no write, `z` unread -
 * which is the state every draw was in before this existed.
 *
 * `compare` is one of the V9X_D3D_RASTER_CMP_* values above. `write` non-zero
 * updates the buffer for every fragment that passes.
 */
typedef struct v9x_d3d_raster_depth {
    void *pixels;
    v9x_u32 pitch;
    v9x_u32 compare;
    v9x_u32 write;
} V9X_D3D_RASTER_DEPTH;

/*
 * The bound texture and the two render states that drive sampling.
 *
 * Per draw for the same reason the depth buffer is: an application changes the
 * filter or the blend between two draws into the same target. A null pointer
 * for the whole struct means untextured, which is what every draw was before
 * this existed and what the engine passes when no handle is bound.
 *
 * `size` is the edge of a square, power-of-two texture. It is not derived from
 * the pitch, because a surface may be padded, and it is not inferred at all:
 * the engine validates the surface and states it.
 */
typedef struct v9x_d3d_raster_texture {
    void *pixels;
    v9x_u32 pitch;
    v9x_u32 size;
    v9x_u32 format;
    v9x_u32 filter;
    v9x_u32 blend;
    /* V9X_D3D_RASTER_ADDRESS_WRAP or _CLAMP. Render state like the two above
     * it, and per draw for the same reason. */
    v9x_u32 address;
} V9X_D3D_RASTER_TEXTURE;

/*
 * How a fragment combines with what is already in the target.
 *
 * Per draw, like the depth buffer and the texture, and null for the same
 * reason: it means opaque - the source replaces the destination, which is what
 * every draw did before this existed and what an application that has not
 * enabled blending expects.
 *
 * There is no `enable` field. A caller that wants blending passes the struct
 * and a caller that does not passes null; an enable flag inside would make
 * "blending on with factors nobody set" expressible, and that state has to
 * mean something.
 */
typedef struct v9x_d3d_raster_alpha {
    v9x_u32 src;
    v9x_u32 dst;
} V9X_D3D_RASTER_ALPHA;

/*
 * Where the pixels go: a pointer, a byte pitch and the extent in pixels.
 *
 * Nothing here says whether that memory is video memory or a system-memory
 * shadow, and that is deliberate - work-order step 1 of mode 2 is a
 * measurement that decides which pointer gets passed, and it must not be able
 * to change the rasterizer.
 */
typedef struct v9x_d3d_raster_target {
    void *pixels;
    v9x_u32 pitch;
    v9x_u32 width;
    v9x_u32 height;
    /* V9X_D3D_RASTER_PIXFMT_RGB565 or _XRGB1555. Unlike the pitch and the
     * extent this is not a property of the allocation but of the display mode
     * the surface was created in, and the caller reads it from the surface
     * rather than assuming it. */
    v9x_u32 format;
} V9X_D3D_RASTER_TARGET;

/*
 * The two 16 bpp layouts a render target can have.
 *
 * Both exist because the S3D triangle engine has no RGB565 destination format
 * and writes ZRGB1555 into any 16-bit target, so a machine running hardware
 * Direct3D on that silicon wants a 5:5:5 desktop
 * (docs\decisions\2026-09-02-s3d-writes-1555-because-it-can-only-write-1555.md).
 * The software engine has no such constraint and had assumed 565 throughout;
 * it now reads the target instead, which it should have done regardless.
 *
 * XRGB1555 and not ARGB1555: the top bit is not written and not read. This is
 * a render target, and the alpha a blend consults comes from the fragment
 * rather than from what is already on screen - no destination-alpha blend
 * factor is published by either engine.
 */
#define V9X_D3D_RASTER_PIXFMT_RGB565   1ul
#define V9X_D3D_RASTER_PIXFMT_XRGB1555 2ul

/* Pack three 0..255 channels into RGB565. Values above 255 saturate. */
v9x_u16 v9x_d3d_raster_rgb565(v9x_s32 red, v9x_s32 green, v9x_s32 blue);

/* The same into XRGB1555, with the unused top bit left clear. */
v9x_u16 v9x_d3d_raster_xrgb1555(v9x_s32 red, v9x_s32 green, v9x_s32 blue);

/*
 * Whether a target is one this rasterizer will write to: non-null, a non-zero
 * extent within V9X_D3D_RASTER_DIMENSION_MAX, a pitch wide enough for the row
 * it claims, and a known pixel format.
 */
int v9x_d3d_raster_target_valid(const V9X_D3D_RASTER_TARGET *target);

/*
 * Whether a depth buffer is one this rasterizer will test against: non-null,
 * and a pitch wide enough for the target's row at two bytes a pixel.
 *
 * The target is the second argument because the depth buffer's required width
 * is the colour buffer's width - they are the same pixels - and a depth buffer
 * narrower than that is the defect that walks off the end of a row.
 */
int v9x_d3d_raster_depth_valid(const V9X_D3D_RASTER_DEPTH *depth,
                               const V9X_D3D_RASTER_TARGET *target);

/*
 * Whether a texture is one this rasterizer will sample: non-null, a known
 * format, a known filter and blend, and a square power-of-two edge inside the
 * declared bounds with a pitch wide enough for its row.
 *
 * Power-of-two is not a formality here - the sampler wraps its texel index
 * with a mask, so a non-power-of-two size would index outside the surface
 * rather than merely look wrong.
 */
int v9x_d3d_raster_texture_valid(const V9X_D3D_RASTER_TEXTURE *texture);

/*
 * Whether a blend is one this rasterizer will apply: non-null, and a factor
 * pair drawn from the four above. An unsupported pair is refused rather than
 * approximated - a driver that silently substituted a factor would put the
 * wrong picture on screen with nothing to say so.
 */
int v9x_d3d_raster_alpha_valid(const V9X_D3D_RASTER_ALPHA *alpha);

/*
 * Rasterize one triangle - exactly three vertices - into the target, testing
 * and updating `depth` if it is not null, sampling `texture` if it is not.
 *
 * Returns non-zero when the triangle was processed, which includes a
 * degenerate one that covers no pixel centre. Returns zero only when the
 * arguments are ones it refuses: a target that fails the check above, a
 * non-null depth buffer, texture or blend that fails its own, a coordinate
 * outside
 * [0, V9X_D3D_RASTER_COORD_MAX], a depth outside
 * [0, V9X_D3D_RASTER_DEPTH_MAX] or a texture coordinate outside
 * [0, V9X_D3D_RASTER_TEXCOORD_MAX]. The caller clips and clamps; a refusal
 * here means the caller did not, and drawing anyway would write outside a
 * surface that on these cards is also the desktop.
 *
 * Coverage is pixel centres with half-open intervals: a pixel belongs to the
 * triangle when its centre - (x + 0.5, y + 0.5) - is inside, with the low edge
 * of each interval included and the high edge excluded. Two triangles sharing
 * an edge therefore write every pixel along it exactly once, with neither a
 * seam nor a doubled blend.
 */
int v9x_d3d_raster_triangle(const V9X_D3D_RASTER_TARGET *target,
                            const V9X_D3D_RASTER_DEPTH *depth,
                            const V9X_D3D_RASTER_TEXTURE *texture,
                            const V9X_D3D_RASTER_ALPHA *alpha,
                            const V9X_D3D_RASTER_VERTEX *vertices);

#endif
