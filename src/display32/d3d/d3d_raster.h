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
    v9x_s32 red;
    v9x_s32 green;
    v9x_s32 blue;
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
} V9X_D3D_RASTER_TARGET;

/* Pack three 0..255 channels into RGB565. Values above 255 saturate. */
v9x_u16 v9x_d3d_raster_rgb565(v9x_s32 red, v9x_s32 green, v9x_s32 blue);

/*
 * Whether a target is one this rasterizer will write to: non-null, a non-zero
 * extent within V9X_D3D_RASTER_DIMENSION_MAX, and a pitch wide enough for the
 * row it claims.
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
 * Rasterize one triangle - exactly three vertices - into the target, testing
 * and updating `depth` if it is not null.
 *
 * Returns non-zero when the triangle was processed, which includes a
 * degenerate one that covers no pixel centre. Returns zero only when the
 * arguments are ones it refuses: a target that fails the check above, a
 * non-null depth buffer that fails its own, or a coordinate outside
 * [0, V9X_D3D_RASTER_COORD_MAX]. The caller clips and clamps; a refusal here
 * means the caller did not, and drawing anyway would write outside a surface
 * that on these cards is also the desktop.
 *
 * Coverage is pixel centres with half-open intervals: a pixel belongs to the
 * triangle when its centre - (x + 0.5, y + 0.5) - is inside, with the low edge
 * of each interval included and the high edge excluded. Two triangles sharing
 * an edge therefore write every pixel along it exactly once, with neither a
 * seam nor a doubled blend.
 */
int v9x_d3d_raster_triangle(const V9X_D3D_RASTER_TARGET *target,
                            const V9X_D3D_RASTER_DEPTH *depth,
                            const V9X_D3D_RASTER_VERTEX *vertices);

#endif
