/*
 * Binding an arbitrary surface as a Gen3 render target.
 *
 * A LEAF translation unit, on the d3d_zfixed.c precedent and for the same
 * reason: it is pure arithmetic over numbers, it is the first thing a runtime
 * draw does, and it must be reachable by the host suite. Including
 * d3d_internal.h would drag in the DDHAL headers, which the host build does
 * not have - so this file knows nothing about DirectDraw and takes the four
 * numbers a surface amounts to.
 *
 * Every draw this project has performed renders into one page of the sandbox
 * reserve, at an address and pitch the build chose. This is where that stops
 * being true.
 */
#ifndef VELOCITY9X_D3D_I9XX_TARGET_H
#define VELOCITY9X_D3D_I9XX_TARGET_H

#include "velocity9x/types.h"

/*
 * Produce the BUF_INFO identity and address for a surface, or refuse it.
 *
 * Returns V9X_TRUE on success. On refusal both outputs are zeroed, so a caller
 * that ignores the result emits a binding to address zero - which the decoder
 * refuses - rather than one to whatever the stack held.
 *
 * The constraints are BUF_INFO's, restated against a surface rather than the
 * reserve: the pitch field DISCARDS its low two bits, so a pitch that is not a
 * multiple of four puts every row but the first at the wrong address; the
 * address is a graphics offset and is dword aligned for the same reason; and
 * the whole surface must lie inside the aperture the driver was given.
 *
 * Refuses rather than clamps. A clamped surface draws a wrong picture with no
 * error anywhere, which is the failure mode this driver is built to avoid.
 */
v9x_u16 v9x_d3d_i9xx_bind_target(
    v9x_u32 offset, v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
    v9x_u32 aperture_bytes,
    v9x_u32 *identity_out, v9x_u32 *address_out);
/* The same at 2 or 4 bytes a pixel; bind_target is this at 2. An
 * XRGB8888 target's last row is four bytes a pixel, and measuring it at
 * two would let it end past the aperture. */
v9x_u16 v9x_d3d_i9xx_bind_target_at(
    v9x_u32 offset, v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
    v9x_u32 bytes_per_pixel, v9x_u32 aperture_bytes,
    v9x_u32 *identity_out, v9x_u32 *address_out);

/*
 * The same question for a texture map and for a depth buffer.
 *
 * A map's address is PAGE aligned and its dimensions are bounded by
 * MAP_STATE's fields; a depth buffer's constraints are BUF_INFO's, which are
 * the target's, and address zero is refused outright. Both share the target's
 * footprint arithmetic, which is what keeps the aperture bound in one place.
 */
v9x_u16 v9x_d3d_i9xx_bind_map(
    v9x_u32 offset, v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
    v9x_u32 aperture_bytes, v9x_u32 *address_out);
/* The same at 2 or 4 bytes a texel; bind_map is this at 2. */
v9x_u16 v9x_d3d_i9xx_bind_map_at(
    v9x_u32 offset, v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
    v9x_u32 texel_bytes, v9x_u32 aperture_bytes, v9x_u32 *address_out);

/*
 * A surface's DirectDraw pixel format (flags, FourCC or D3DFORMAT code,
 * bit count, the four masks) as the MS3 map format the sampler reads and
 * its bytes a texel, or V9X_FALSE with nothing written. RGB565,
 * ARGB1555 and ARGB4444 by mask, as the engine has read them since
 * Phase 6; ARGB8888 and XRGB8888 by mask (the alpha bit and mask say
 * which); V8U8 and Q8W8V8U8 by D3DFORMAT or FourCC code (60, 63) or, for
 * V8U8, DirectX 7's DDPF_BUMPDUDV with du FFh, dv FF00h and no
 * luminance: Haswell's v9x_d3d_hsw_signed_format reading. The flag values
 * are ddraw.h's, restated because this file includes no DDHAL header.
 */
#define V9X_D3D_I9XX_DDPF_ALPHAPIXELS 0x00000001ul
#define V9X_D3D_I9XX_DDPF_FOURCC      0x00000004ul
#define V9X_D3D_I9XX_DDPF_RGB         0x00000040ul
#define V9X_D3D_I9XX_DDPF_BUMPDUDV    0x00080000ul
#define V9X_D3D_I9XX_DDPF_D3DFORMAT   0x00200000ul
#define V9X_D3D_I9XX_D3DFMT_V8U8      60ul
/* With a luminance, for BUMPENVMAPLUMINANCE, by code only: L6V5U5 as
 * MT_16BIT_BUMP_655LDVDU, X8L8V8U8 as MT_32BIT_XLVU8888. */
#define V9X_D3D_I9XX_D3DFMT_L6V5U5    61ul
#define V9X_D3D_I9XX_D3DFMT_X8L8V8U8  62ul
#define V9X_D3D_I9XX_D3DFMT_Q8W8V8U8  63ul

v9x_u16 v9x_d3d_i9xx_map_format(v9x_u32 flags, v9x_u32 fourcc,
                                v9x_u32 bits, v9x_u32 red, v9x_u32 green,
                                v9x_u32 blue, v9x_u32 alpha,
                                v9x_u32 *format_out, v9x_u32 *texel_bytes);
v9x_u16 v9x_d3d_i9xx_bind_depth(
    v9x_u32 offset, v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
    v9x_u32 aperture_bytes, v9x_u32 *address_out);
/* The same at 2 or 4 bytes a cell; bind_depth is this at 2. */
v9x_u16 v9x_d3d_i9xx_bind_depth_at(
    v9x_u32 offset, v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
    v9x_u32 bytes_per_pixel, v9x_u32 aperture_bytes, v9x_u32 *address_out);

/*
 * Where Gen3's sampler reads each level of a square 16-bit mip chain.
 *
 * MAP_STATE carries one address and one pitch for the whole chain, and the
 * sampler derives every level from them by a layout fixed in the part - so a
 * chain is usable only if its levels sit exactly there. DirectDraw's heap
 * places each level wherever it likes with its own pitch, which never
 * matches, and that is why the HAL lays chains out itself at CreateSurface.
 *
 * The layout is Mesa's i945 one (gallium i915_resource_texture.c:462-522,
 * classic intel_tex_layout.c:121-186; the two agree): level 0 at the origin,
 * level 1 below it, level 2 to the right of level 1, each later level below
 * the one before; level widths aligned to 4 texels and heights to 2 rows; the
 * pitch widened if levels 1 and 2 side by side overrun level 0, and aligned
 * to 64 bytes. UNMEASURED on this part until the probe's mip ladder reads
 * each level's colour back.
 *
 * `stacked` asks for the 915's layout instead (gallium
 * i915_texture_layout_2d, which Mesa picks when is_i945 is false): every level
 * at the left edge, each below the one before, rows aligned to 2, the pitch
 * level 0's aligned to 64. The HAL passes it on V9X_DD_ENGINE_CAP_I9XX_MIP_
 * STACKED. Desk evidence only; no 915 has sampled a chain this driver placed.
 *
 * `size` is the top level's edge, a power of two up to MAP_STATE's 2048;
 * `levels` counts the top, from 1 to log2(size) + 1. level_offset[n] is
 * level n's byte offset from the chain's base, pitch is bytes and rows is
 * the chain's height - pitch * rows is its footprint. Returns V9X_FALSE, the
 * tree zeroed, for anything else.
 */
#define V9X_D3D_I9XX_MIP_LEVELS_MAX 12ul

struct v9x_d3d_i9xx_miptree {
    v9x_u32 levels;
    v9x_u32 pitch;
    v9x_u32 rows;
    v9x_u32 level_offset[V9X_D3D_I9XX_MIP_LEVELS_MAX];
};

/* Width and height separately (Phase 2 of the OpenGL plan): each a power of
 * two within the map bounds, and the chain as long as the larger edge
 * allows. Direct3D passes a square, which its caps still require. */
v9x_u16 v9x_d3d_i9xx_layout_miptree(v9x_u32 width, v9x_u32 height,
                                    v9x_u32 levels, v9x_u16 stacked,
                                    struct v9x_d3d_i9xx_miptree *tree);
/* The same at 2 or 4 bytes a texel; layout_miptree is this at 2. The
 * alignments are in texels and rows, so only the byte counts change. */
v9x_u16 v9x_d3d_i9xx_layout_miptree_at(v9x_u32 width, v9x_u32 height,
                                       v9x_u32 levels, v9x_u16 stacked,
                                       v9x_u32 texel_bytes,
                                       struct v9x_d3d_i9xx_miptree *tree);

/*
 * A cube map as the sampler reads one (step 5b of docs\plans\gen3-ps11-
 * pixel-shaders.md): six square faces of `levels` levels each in one
 * block, Mesa gallium's i9x5_texture_layout_cube, which the 915 and the
 * 945 both use for uncompressed formats. offset[face][level] is that
 * image's byte offset from the block's start, faces in Direct3D's order
 * (+X, -X, +Y, -Y, +Z, -Z); pitch is twice a face's row aligned to 4
 * bytes, rows four edges. Only Mesa gallium states this layout among
 * the sources here (xf86 samples no cubes), so it is single-sourced and
 * UNMEASURED until the netbook samples a cube. `edge` a power of two up
 * to MAP_STATE's 2048, `levels` 1 to log2(edge) + 1, `texel_bytes` 2 or
 * 4; anything else V9X_FALSE, the tree zeroed.
 */
#define V9X_D3D_I9XX_CUBE_FACES 6ul

struct v9x_d3d_i9xx_cubetree {
    v9x_u32 levels;
    v9x_u32 pitch;
    v9x_u32 rows;
    v9x_u32 offset[V9X_D3D_I9XX_CUBE_FACES][V9X_D3D_I9XX_MIP_LEVELS_MAX];
};

v9x_u16 v9x_d3d_i9xx_layout_cube(v9x_u32 edge, v9x_u32 levels,
                                 v9x_u32 texel_bytes,
                                 struct v9x_d3d_i9xx_cubetree *cube);

/*
 * The texture shapes the sampler is given: each edge a power of two, the
 * larger within [size_min, size_max], the smaller at least one texel. The
 * placement, the chain walk, the bind and accepts all ask this, so the
 * rule is stated once.
 */
v9x_u16 v9x_d3d_i9xx_texture_shape(v9x_u32 width, v9x_u32 height,
                                   v9x_u32 size_min, v9x_u32 size_max);
/* Level `level`'s edge of a chain whose level 0 edge is `edge`: halved per
 * level, never below one texel. */
v9x_u32 v9x_d3d_i9xx_level_edge(v9x_u32 edge, v9x_u32 level);

#endif /* VELOCITY9X_D3D_I9XX_TARGET_H */
