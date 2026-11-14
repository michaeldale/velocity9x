/*
 * See d3d_i9xx_target.h for why this is a leaf translation unit.
 *
 * The bounds arithmetic below is the whole content, and the overflow checks in
 * it are not defensive. These are application-supplied numbers: a surface
 * whose footprint wraps a 32-bit product passes a bounds test BY BEING SMALL,
 * and the surface it described would then be written wherever the wrap landed.
 * The same hazard the blit builder's own bounds check exists for, at the other
 * end of the driver.
 */
#include "d3d_i9xx_target.h"

#include "velocity9x/intel_gma.h"
#include "velocity9x/intel_gen3_3d.h"

/*
 * The widest surface this accepts, in pixels.
 *
 * Not the part's architectural maximum, which nothing has exercised. It bounds
 * the products below so their overflow checks have something to be checked
 * against, and it is restated in the engine's limits table where the core
 * reads it.
 */
#define V9X_I9XX_TARGET_DIMENSION_MAX ((v9x_u32)2048ul)

/*
 * The footprint arithmetic, shared by every surface this engine binds.
 *
 * Extracted when textures and depth buffers arrived rather than copied: the
 * last-row overflow reasoning below is the memory-safety argument for the
 * whole runtime path, and three copies of it is three places for it to drift.
 * The differences between the surfaces are their alignment and dimension
 * rules, which stay with their callers.
 *
 * Returns V9X_TRUE when every byte of the surface lies inside the aperture.
 */
static v9x_u16 v9x_d3d_i9xx_footprint_fits(
    v9x_u32 offset, v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
    v9x_u32 bytes_per_pixel, v9x_u32 aperture_bytes)
{
    v9x_u32 row_bytes;
    v9x_u32 last_row;

    if (width == 0ul || height == 0ul || pitch == 0ul ||
        aperture_bytes == 0ul) {
        return V9X_FALSE;
    }
    /* A row must hold its own pixels: two bytes each for every texture and
     * depth buffer and a 16 bpp target, four for an XRGB8888 target. */
    row_bytes = width * bytes_per_pixel;
    if (pitch < row_bytes) {
        return V9X_FALSE;
    }
    /*
     * The last row's last byte must be inside the aperture.
     *
     * (height - 1) rows plus one row, not height rows: a surface whose final
     * row ends exactly at the aperture's end is legal, and the simpler
     * arithmetic would refuse it. Refusing a legal surface is how a driver
     * comes to work only on the modes somebody happened to try.
     */
    if ((height - 1ul) > (0xfffffffful / pitch)) {
        return V9X_FALSE;
    }
    last_row = (height - 1ul) * pitch;
    if (last_row > 0xfffffffful - row_bytes) {
        return V9X_FALSE;
    }
    if (offset > 0xfffffffful - last_row - row_bytes) {
        return V9X_FALSE;
    }
    return (offset + last_row + row_bytes <= aperture_bytes)
        ? V9X_TRUE : V9X_FALSE;
}

/*
 * Binding an application surface as a TEXTURE MAP.
 *
 * The same footprint question as a render target and three different
 * constraints, each MAP_STATE's rather than BUF_INFO's:
 *
 *  - The address is PAGE aligned. i9xx_texture.c requires it and the packet
 *    audit records that as this driver's choice rather than a databook
 *    requirement, since neither reference emitter states one. It is kept
 *    because relaxing an unmeasured choice on the strength of wanting more
 *    textures to work is how a driver acquires a fault nobody can explain.
 *  - The pitch is encoded in dwords, so it is a multiple of four.
 *  - Width and height go in fields one less than the extent, so both are
 *    bounded by the field rather than by the target's dimension limit.
 *
 * Refuses rather than clamps, for the reason bind_target does.
 */
v9x_u16 v9x_d3d_i9xx_bind_map(
    v9x_u32 offset, v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
    v9x_u32 aperture_bytes, v9x_u32 *address_out)
{
    return v9x_d3d_i9xx_bind_map_at(offset, pitch, width, height, 2ul,
                                    aperture_bytes, address_out);
}

v9x_u16 v9x_d3d_i9xx_bind_map_at(
    v9x_u32 offset, v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
    v9x_u32 texel_bytes, v9x_u32 aperture_bytes, v9x_u32 *address_out)
{
    if (address_out == 0) {
        return V9X_FALSE;
    }
    *address_out = 0ul;

    if (width == 0ul || height == 0ul ||
        width > V9X_I9XX_MAP_DIMENSION_MAX ||
        height > V9X_I9XX_MAP_DIMENSION_MAX ||
        (texel_bytes != 2ul && texel_bytes != 4ul)) {
        return V9X_FALSE;
    }
    if (pitch == 0ul || (pitch & 3ul) != 0ul ||
        pitch > V9X_I9XX_MAP_PITCH_MAX) {
        return V9X_FALSE;
    }
    if ((offset & (V9X_I9XX_SANDBOX_PAGE_BYTES - 1ul)) != 0ul) {
        return V9X_FALSE;
    }
    if (v9x_d3d_i9xx_footprint_fits(offset, pitch, width, height,
                                    texel_bytes,
                                    aperture_bytes) == V9X_FALSE) {
        return V9X_FALSE;
    }
    *address_out = offset;
    return V9X_TRUE;
}

/*
 * Binding an application surface as the DEPTH buffer.
 *
 * BUF_INFO's constraints, exactly the target's - the two are the same packet
 * with a different identity - so this shares the target's rules rather than
 * restating them, and differs only in not producing a colour identity.
 */
v9x_u16 v9x_d3d_i9xx_bind_depth(
    v9x_u32 offset, v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
    v9x_u32 aperture_bytes, v9x_u32 *address_out)
{
    return v9x_d3d_i9xx_bind_depth_at(offset, pitch, width, height, 2ul,
                                      aperture_bytes, address_out);
}

v9x_u16 v9x_d3d_i9xx_bind_depth_at(
    v9x_u32 offset, v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
    v9x_u32 bytes_per_pixel, v9x_u32 aperture_bytes, v9x_u32 *address_out)
{
    if (address_out == 0) {
        return V9X_FALSE;
    }
    *address_out = 0ul;

    if (bytes_per_pixel != 2ul && bytes_per_pixel != 4ul) {
        return V9X_FALSE;
    }
    if (width == 0ul || height == 0ul ||
        width > V9X_I9XX_TARGET_DIMENSION_MAX ||
        height > V9X_I9XX_TARGET_DIMENSION_MAX) {
        return V9X_FALSE;
    }
    if (pitch == 0ul || (pitch & 3ul) != 0ul ||
        pitch > V9X_I9XX_BUF_3D_PITCH_MASK || (offset & 3ul) != 0ul) {
        return V9X_FALSE;
    }
    /*
     * Address zero is refused, and not only because it is a degenerate
     * offset: it is inside the aperture and is not ours, and it is the exact
     * value the removed Phase 5 depth binding carried. The builder and the
     * decoder both refuse it, and this makes three.
     */
    if (offset == 0ul) {
        return V9X_FALSE;
    }
    if (v9x_d3d_i9xx_footprint_fits(offset, pitch, width, height,
                                    bytes_per_pixel,
                                    aperture_bytes) == V9X_FALSE) {
        return V9X_FALSE;
    }
    *address_out = offset;
    return V9X_TRUE;
}

/*
 * The mip-tree alignment units and the pitch granule. Mesa 20.3 classic
 * intel_tex_layout.c:40-110 cites the Gen3 spec's "Alignment Unit Size"
 * section for 4 texels across and 2 rows down for every uncompressed format;
 * gallium i915_resource_texture.c:465 states the same pair and aligns the
 * pitch to 64 bytes at :497.
 */
#define V9X_I9XX_MIP_ALIGN_TEXELS    4ul
#define V9X_I9XX_MIP_ALIGN_ROWS      2ul
#define V9X_I9XX_MIP_PITCH_ALIGN     64ul

/* A level's edge, never below one texel: the u_minify / minify rule. */
static v9x_u32 v9x_d3d_i9xx_minify(v9x_u32 edge, v9x_u32 times)
{
    while (times != 0ul && edge > 1ul) {
        edge >>= 1;
        --times;
    }
    return edge;
}

static v9x_u32 v9x_d3d_i9xx_align(v9x_u32 value, v9x_u32 unit)
{
    return (value + unit - 1ul) & ~(unit - 1ul);
}

v9x_u16 v9x_d3d_i9xx_layout_miptree(v9x_u32 width, v9x_u32 height,
                                    v9x_u32 levels, v9x_u16 stacked,
                                    struct v9x_d3d_i9xx_miptree *tree)
{
    return v9x_d3d_i9xx_layout_miptree_at(width, height, levels, stacked,
                                          2ul, tree);
}

/*
 * Mesa gallium i915_resource_texture.c's initial_offsets and step_offsets
 * for i9x5_texture_layout_cube, in Direct3D's face order (+X -X +Y -Y +Z
 * -Z, which is PIPE_TEX_FACE's too): a face's top at (x, y) times the
 * edge, and each next level at the last plus (x, y) times its own edge.
 * Signed, because three faces step left.
 */
static const long v9x_d3d_i9xx_cube_start[V9X_D3D_I9XX_CUBE_FACES][2] = {
    { 0l, 0l }, { 0l, 2l }, { 1l, 0l }, { 1l, 2l }, { 1l, 1l }, { 1l, 3l }
};
static const long v9x_d3d_i9xx_cube_step[V9X_D3D_I9XX_CUBE_FACES][2] = {
    { 0l, 2l }, { 0l, 2l }, { -1l, 2l }, { -1l, 2l }, { -1l, 1l }, { -1l, 1l }
};

/* Mesa's cube pitch is the face row doubled, aligned to 4 bytes (not the
 * 2D tree's 64). */
#define V9X_I9XX_CUBE_PITCH_ALIGN 4ul

v9x_u16 v9x_d3d_i9xx_layout_cube(v9x_u32 edge, v9x_u32 levels,
                                 v9x_u32 texel_bytes,
                                 struct v9x_d3d_i9xx_cubetree *cube)
{
    v9x_u32 top_levels = 1ul;
    v9x_u32 face;
    v9x_u32 level;
    v9x_u32 e;

    if (cube == 0) {
        return V9X_FALSE;
    }
    cube->levels = 0ul;
    cube->pitch = 0ul;
    cube->rows = 0ul;
    for (face = 0ul; face < V9X_D3D_I9XX_CUBE_FACES; ++face) {
        for (level = 0ul; level < V9X_D3D_I9XX_MIP_LEVELS_MAX; ++level) {
            cube->offset[face][level] = 0ul;
        }
    }

    if (edge == 0ul || edge > V9X_I9XX_MAP_DIMENSION_MAX ||
        (edge & (edge - 1ul)) != 0ul ||
        (texel_bytes != 2ul && texel_bytes != 4ul)) {
        return V9X_FALSE;
    }
    for (e = edge; e > 1ul; e >>= 1) {
        ++top_levels;
    }
    if (levels == 0ul || levels > top_levels ||
        levels > V9X_D3D_I9XX_MIP_LEVELS_MAX) {
        return V9X_FALSE;
    }

    cube->pitch = v9x_d3d_i9xx_align(edge * texel_bytes * 2ul,
                                     V9X_I9XX_CUBE_PITCH_ALIGN);
    cube->rows = edge * 4ul;

    /* Mesa's walk: d is the level's edge; after each level it halves and
     * the position steps by the face's step times the new d. */
    for (face = 0ul; face < V9X_D3D_I9XX_CUBE_FACES; ++face) {
        long x = v9x_d3d_i9xx_cube_start[face][0] * (long)edge;
        long y = v9x_d3d_i9xx_cube_start[face][1] * (long)edge;
        long d = (long)edge;

        for (level = 0ul; level < levels; ++level) {
            cube->offset[face][level] = (v9x_u32)y * cube->pitch +
                                        (v9x_u32)x * texel_bytes;
            d >>= 1;
            x += v9x_d3d_i9xx_cube_step[face][0] * d;
            y += v9x_d3d_i9xx_cube_step[face][1] * d;
        }
    }
    cube->levels = levels;
    return V9X_TRUE;
}

v9x_u16 v9x_d3d_i9xx_layout_miptree_at(v9x_u32 width, v9x_u32 height,
                                       v9x_u32 levels, v9x_u16 stacked,
                                       v9x_u32 texel_bytes,
                                       struct v9x_d3d_i9xx_miptree *tree)
{
    v9x_u32 level;
    v9x_u32 edge;
    v9x_u32 top_levels = 1ul;
    v9x_u32 x = 0ul;
    v9x_u32 y = 0ul;
    v9x_u32 pitch_texels;

    if (tree == 0) {
        return V9X_FALSE;
    }
    tree->levels = 0ul;
    tree->pitch = 0ul;
    tree->rows = 0ul;
    for (level = 0ul; level < V9X_D3D_I9XX_MIP_LEVELS_MAX; ++level) {
        tree->level_offset[level] = 0ul;
    }

    if (width == 0ul || width > V9X_I9XX_MAP_DIMENSION_MAX ||
        (width & (width - 1ul)) != 0ul ||
        height == 0ul || height > V9X_I9XX_MAP_DIMENSION_MAX ||
        (height & (height - 1ul)) != 0ul ||
        (texel_bytes != 2ul && texel_bytes != 4ul)) {
        return V9X_FALSE;
    }
    /* The chain runs until both edges are one, so its length is the larger
     * edge's; the smaller stops halving at one texel (minify). */
    for (edge = width > height ? width : height; edge > 1ul; edge >>= 1) {
        ++top_levels;
    }
    if (levels == 0ul || levels > top_levels) {
        return V9X_FALSE;
    }

    /*
     * The pitch: level 0's row, widened when level 1 (aligned) and level 2
     * side by side are wider - which happens only for a top of four texels
     * or fewer - then aligned to the granule. Widths only: the rows do not
     * enter it, which is what lets a tall map keep a narrow pitch.
     */
    pitch_texels = width;
    if (levels > 1ul && stacked == V9X_FALSE) {
        v9x_u32 mip1 =
            v9x_d3d_i9xx_align(v9x_d3d_i9xx_minify(width, 1ul),
                               V9X_I9XX_MIP_ALIGN_TEXELS) +
            v9x_d3d_i9xx_minify(width, 2ul);

        if (mip1 > pitch_texels) {
            pitch_texels = mip1;
        }
    }
    tree->pitch = v9x_d3d_i9xx_align(pitch_texels * texel_bytes,
                                     V9X_I9XX_MIP_PITCH_ALIGN);

    /*
     * Down the chain. After level 1 the position steps RIGHT by level 1's
     * aligned width instead of down, which puts level 2 and everything
     * after it in a column beside level 1 - Mesa's "Layout_below: step
     * right after second mipmap". The step across is level 1's WIDTH and
     * each step down its own level's HEIGHT, which for a square were the
     * same number (i945_miptree_layout_2d).
     *
     * Stacked, the 915's: every level at x = 0, each below the one before,
     * its rows aligned to two (gallium i915_texture_layout_2d). Level 1 then
     * steps down like the rest, and the pitch stays level 0's.
     */
    for (level = 0ul; level < levels; ++level) {
        v9x_u32 level_width = v9x_d3d_i9xx_minify(width, level);
        v9x_u32 rows = v9x_d3d_i9xx_align(v9x_d3d_i9xx_minify(height, level),
                                          V9X_I9XX_MIP_ALIGN_ROWS);

        tree->level_offset[level] = y * tree->pitch + x * texel_bytes;
        if (y + rows > tree->rows) {
            tree->rows = y + rows;
        }
        if (level == 1ul && stacked == V9X_FALSE) {
            x += v9x_d3d_i9xx_align(level_width, V9X_I9XX_MIP_ALIGN_TEXELS);
        } else {
            y += rows;
        }
    }
    tree->levels = levels;
    return V9X_TRUE;
}

v9x_u16 v9x_d3d_i9xx_bind_target(
    v9x_u32 offset, v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
    v9x_u32 aperture_bytes,
    v9x_u32 *identity_out, v9x_u32 *address_out)
{
    return v9x_d3d_i9xx_bind_target_at(offset, pitch, width, height, 2ul,
                                       aperture_bytes, identity_out,
                                       address_out);
}

v9x_u16 v9x_d3d_i9xx_bind_target_at(
    v9x_u32 offset, v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
    v9x_u32 bytes_per_pixel, v9x_u32 aperture_bytes,
    v9x_u32 *identity_out, v9x_u32 *address_out)
{
    if (identity_out == 0 || address_out == 0) {
        return V9X_FALSE;
    }
    *identity_out = 0ul;
    *address_out = 0ul;

    if (width == 0ul || height == 0ul || pitch == 0ul ||
        aperture_bytes == 0ul ||
        (bytes_per_pixel != 2ul && bytes_per_pixel != 4ul)) {
        return V9X_FALSE;
    }
    if (width > V9X_I9XX_TARGET_DIMENSION_MAX ||
        height > V9X_I9XX_TARGET_DIMENSION_MAX) {
        return V9X_FALSE;
    }

    /*
     * The pitch must survive the encoding. BUF_INFO carries it in a masked
     * field whose low two bits are discarded, so a pitch of 1282 - a plausible
     * thing for an application to ask for - would place every row but the
     * first two bytes early. A sheared picture, and no error anywhere.
     */
    if ((pitch & 3ul) != 0ul || pitch > V9X_I9XX_BUF_3D_PITCH_MASK) {
        return V9X_FALSE;
    }
    if ((offset & 3ul) != 0ul) {
        return V9X_FALSE;
    }
    if (v9x_d3d_i9xx_footprint_fits(offset, pitch, width, height,
                                    bytes_per_pixel,
                                    aperture_bytes) == V9X_FALSE) {
        return V9X_FALSE;
    }

    *identity_out = V9X_I9XX_BUF_3D_ID_COLOR_BACK |
                    (pitch & V9X_I9XX_BUF_3D_PITCH_MASK);
    *address_out = offset;
    return V9X_TRUE;
}

v9x_u16 v9x_d3d_i9xx_texture_shape(v9x_u32 width, v9x_u32 height,
                                   v9x_u32 size_min, v9x_u32 size_max)
{
    v9x_u32 larger = width > height ? width : height;

    /* MAP_STATE carries width and height in separate fields (i915
     * MS3), so a map need not be square; the sampler normalises each
     * axis by its own size. Powers of two because the mip chain and the
     * WRAP address arithmetic assume them. */
    if (width == 0ul || height == 0ul ||
        (width & (width - 1ul)) != 0ul ||
        (height & (height - 1ul)) != 0ul) {
        return V9X_FALSE;
    }
    return larger >= size_min && larger <= size_max ? V9X_TRUE
                                                    : V9X_FALSE;
}

v9x_u32 v9x_d3d_i9xx_level_edge(v9x_u32 edge, v9x_u32 level)
{
    return v9x_d3d_i9xx_minify(edge, level);
}

/* DDPF_BUMPDUDV's du and dv masks for V8U8, which ddraw.h keeps where
 * the red and green masks are. */
#define V9X_D3D_I9XX_V8U8_DU 0x000000fful
#define V9X_D3D_I9XX_V8U8_DV 0x0000ff00ul

v9x_u16 v9x_d3d_i9xx_map_format(v9x_u32 flags, v9x_u32 fourcc,
                                v9x_u32 bits, v9x_u32 red, v9x_u32 green,
                                v9x_u32 blue, v9x_u32 alpha,
                                v9x_u32 *format_out, v9x_u32 *texel_bytes)
{
    v9x_u32 has_alpha = (flags & V9X_D3D_I9XX_DDPF_ALPHAPIXELS) != 0ul;
    v9x_u32 format = 0ul;
    v9x_u32 code = 0ul;

    if (format_out == 0 || texel_bytes == 0) {
        return V9X_FALSE;
    }

    /* The signed formats, named by their Direct3D code, or V8U8 by
     * DirectX 7's bump description. */
    if ((flags & (V9X_D3D_I9XX_DDPF_D3DFORMAT |
                  V9X_D3D_I9XX_DDPF_FOURCC)) != 0ul) {
        code = fourcc;
    } else if ((flags & V9X_D3D_I9XX_DDPF_BUMPDUDV) != 0ul &&
               bits == 16ul && red == V9X_D3D_I9XX_V8U8_DU &&
               green == V9X_D3D_I9XX_V8U8_DV && blue == 0ul) {
        code = V9X_D3D_I9XX_D3DFMT_V8U8;
    } else if ((flags & V9X_D3D_I9XX_DDPF_BUMPDUDV) != 0ul &&
               bits == 32ul && red == V9X_D3D_I9XX_V8U8_DU &&
               green == V9X_D3D_I9XX_V8U8_DV && blue == 0x00ff0000ul) {
        /* The two with a luminance, as the DirectX 8 runtime hands them
         * over: BUMPDUDV and BUMPLUMINANCE with masks, no code (the
         * netbook, boot 340). */
        code = V9X_D3D_I9XX_D3DFMT_X8L8V8U8;
    } else if ((flags & V9X_D3D_I9XX_DDPF_BUMPDUDV) != 0ul &&
               bits == 16ul && red == 0x0000001ful &&
               green == 0x000003e0ul && blue == 0x0000fc00ul) {
        code = V9X_D3D_I9XX_D3DFMT_L6V5U5;
    }
    if (code == V9X_D3D_I9XX_D3DFMT_Q8W8V8U8) {
        format = V9X_I9XX_MAPSURF_32BIT_QWVU8888;
    } else if (code == V9X_D3D_I9XX_D3DFMT_V8U8) {
        format = V9X_I9XX_MAPSURF_16BIT_88DVDU;
    } else if (code == V9X_D3D_I9XX_D3DFMT_L6V5U5) {
        format = V9X_I9XX_MAPSURF_16BIT_655LDVDU;
    } else if (code == V9X_D3D_I9XX_D3DFMT_X8L8V8U8) {
        format = V9X_I9XX_MAPSURF_32BIT_XLVU8888;
    } else if ((flags & V9X_D3D_I9XX_DDPF_RGB) == 0ul) {
        return V9X_FALSE;
    } else if (bits == 16ul) {
        /* The three 16-bit layouts, told apart by their masks; the two
         * with alpha only with the alpha bit and its mask, since 1:5:5:5
         * without one is 555, which the sampler has no type for. */
        if (red == 0x0000f800ul && green == 0x000007e0ul &&
            blue == 0x0000001ful) {
            format = V9X_I9XX_MAPSURF_16BIT_RGB565;
        } else if (red == 0x00007c00ul && green == 0x000003e0ul &&
                   blue == 0x0000001ful && has_alpha &&
                   alpha == 0x00008000ul) {
            format = V9X_I9XX_MAPSURF_16BIT_ARGB1555;
        } else if (red == 0x00000f00ul && green == 0x000000f0ul &&
                   blue == 0x0000000ful && has_alpha &&
                   alpha == 0x0000f000ul) {
            format = V9X_I9XX_MAPSURF_16BIT_ARGB4444;
        }
    } else if (bits == 32ul && red == 0x00ff0000ul &&
               green == 0x0000ff00ul && blue == 0x000000fful) {
        /* With the alpha bit, the alpha must be the top byte; without
         * it, the top byte is ignored. */
        if (!has_alpha) {
            format = V9X_I9XX_MAPSURF_32BIT_XRGB8888;
        } else if (alpha == 0xff000000ul) {
            format = V9X_I9XX_MAPSURF_32BIT_ARGB8888;
        }
    }
    if (format == 0ul) {
        return V9X_FALSE;
    }
    *format_out = format;
    *texel_bytes = v9x_i9xx_map_texel_bytes(format);
    return V9X_TRUE;
}
