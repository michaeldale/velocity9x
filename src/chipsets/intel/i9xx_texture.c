/*
 * Gen3 texture state: MAP_STATE and SAMPLER_STATE.
 *
 * Every field placement comes from
 * docs\decisions\2026-09-16-intel-gen3-texture-packet-audit.md, where each is
 * corroborated by two independently written emitters - Mesa's gallium i915
 * driver implementing OpenGL, and xf86-video-intel's UXA render path
 * implementing the X Render extension. The shared header supplied the names;
 * the two use sites supplied the evidence.
 *
 * Both packets are emitted DIRECTLY into the command stream. Neither goes
 * through _3DSTATE_LOAD_INDIRECT, which the plan had expected to need and
 * which would have meant state buffers in graphics memory and a second class
 * of GPU-readable allocation. Audit section 2.
 *
 * The dwords this produces are DERIVED AND UNVALIDATED, exactly as the
 * fragment program's were before Phase 5 ran. This unit and its host test are
 * what validation means until a capture says otherwise.
 */
#include "velocity9x/intel_gen3_3d.h"
#include "velocity9x/intel_gma.h"

/*
 * The most maps or samplers either packet will carry.
 *
 * Gen3 has eight texture units. Phase 6 uses one, and the bound is stated
 * rather than assumed because the enable mask below is built from it: a count
 * above the unit count would shift a bit off the end of the mask and enable
 * nothing, which is a stream the parser would accept and a picture nobody
 * could explain.
 */
#define V9X_I9XX_TEX_UNITS          ((v9x_u32)8ul)
/* Address, MS3, MS4 - stated by both trees as three dwords per unit. */
#define V9X_I9XX_MAP_DWORDS_EACH    ((v9x_u32)3ul)
/* SS2, SS3, SS4. */
#define V9X_I9XX_SAMPLER_DWORDS_EACH ((v9x_u32)3ul)

/*
 * Command dword plus the enable mask, then three per unit.
 *
 * The length field is the payload less one, which is the same convention
 * LOAD_STATE_IMMEDIATE_1 uses and which falls out of both trees writing
 * `command | (3 * nr)` ahead of a mask dword and 3n payload dwords.
 */
static v9x_u32 v9x_i9xx_unit_packet_extent(v9x_u32 count, v9x_u32 each)
{
    if (count == 0ul || count > V9X_I9XX_TEX_UNITS) {
        return 0ul;
    }
    /* 16-bit multiply: count is bounded by the unit count above, so the
     * product cannot leave a 16-bit register and no __U4M helper is called
     * from I9XXCODE. The same constraint the triangle run works under. */
    return 2ul + (v9x_u32)((v9x_u16)count * (v9x_u16)each);
}

v9x_u32 v9x_i9xx_map_state_extent(v9x_u32 count)
{
    return v9x_i9xx_unit_packet_extent(count, V9X_I9XX_MAP_DWORDS_EACH);
}

v9x_u32 v9x_i9xx_sampler_state_extent(v9x_u32 count)
{
    return v9x_i9xx_unit_packet_extent(count, V9X_I9XX_SAMPLER_DWORDS_EACH);
}

/* The low `count` bits set: the units this packet describes. Both trees build
 * it this way, xf86 literally as (1 << n) - 1. */
static v9x_u32 v9x_i9xx_unit_enable_mask(v9x_u32 count)
{
    return (v9x_u32)((1ul << count) - 1ul);
}

v9x_u16 v9x_i9xx_map_format_known(v9x_u32 format)
{
    if (format == V9X_I9XX_MAPSURF_16BIT_RGB565 ||
        format == V9X_I9XX_MAPSURF_16BIT_ARGB1555 ||
        format == V9X_I9XX_MAPSURF_16BIT_ARGB4444) {
        return V9X_TRUE;
    }
    return V9X_FALSE;
}

v9x_status v9x_i9xx_build_map_state(
    const struct v9x_i9xx_texture *maps, v9x_u32 count,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 at = 0ul;
    v9x_u32 index;
    v9x_u32 extent;

    if (written != 0) { *written = 0ul; }
    if (stream == 0 || written == 0 || maps == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    extent = v9x_i9xx_map_state_extent(count);
    if (extent == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (capacity < extent) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }

    stream[at++] = V9X_I9XX_3DSTATE_MAP_STATE |
                   (v9x_u32)((v9x_u16)count *
                             (v9x_u16)V9X_I9XX_MAP_DWORDS_EACH);
    stream[at++] = v9x_i9xx_unit_enable_mask(count);

    for (index = 0ul; index < count; ++index) {
        v9x_u32 width = maps[index].width;
        v9x_u32 height = maps[index].height;
        v9x_u32 pitch = maps[index].pitch;

        /*
         * Every bound refused rather than truncated.
         *
         * A dimension that does not fit its field wraps into the field above
         * it - width into height, height off the top of the dword - and the
         * result is a texture the hardware reads at the wrong size from an
         * address that is still valid. That is a wrong picture with no error,
         * which is the failure mode this whole driver is built to avoid.
         */
        if (width == 0ul || height == 0ul ||
            width > V9X_I9XX_MAP_DIMENSION_MAX ||
            height > V9X_I9XX_MAP_DIMENSION_MAX) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        /*
         * The pitch is encoded as dwords, so it must be a multiple of four -
         * the same constraint BUF_INFO puts on the render target's pitch, and
         * for the same reason: the low bits do not survive the encoding and a
         * pitch that does not survive places every row but the first at the
         * wrong address.
         */
        if (pitch == 0ul || (pitch & 3ul) != 0ul ||
            pitch > V9X_I9XX_MAP_PITCH_MAX) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        /* A row must hold its own texels. All three formats this builder
         * emits are MAPSURF_16BIT: two bytes each. */
        if (pitch < (v9x_u32)((v9x_u16)width * (v9x_u16)2u)) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        /* The format is stated, never defaulted. A zero here is a caller
         * that did not read the surface, and a 565 sampler over a 4444
         * surface is a plausible-looking wrong picture. */
        if (v9x_i9xx_map_format_known(maps[index].format) == V9X_FALSE) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        /* The address is a graphics offset and is page aligned - a choice the
         * audit records as a choice, since neither reference tree states a
         * requirement. */
        if ((maps[index].offset & (V9X_I9XX_SANDBOX_PAGE_BYTES - 1ul)) !=
                0ul) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        /*
         * The levels below the top: no more than the field's ceiling, and no
         * more than the top has - a 16-texel map halves four times, and a
         * fifth MAX_LOD would send the sampler to a level that is not there.
         * Halved in a loop rather than shifted by a variable count, which
         * keeps a long-shift helper out of I9XXCODE for the reason the
         * multiplies above are 16-bit.
         */
        {
            v9x_u32 edge = width > height ? width : height;
            v9x_u32 level;

            if (maps[index].max_lod > V9X_I9XX_MAX_LOD_LEVELS) {
                return V9X_STATUS_INVALID_ARGUMENT;
            }
            for (level = 0ul; level < maps[index].max_lod; ++level) {
                edge >>= 1;
            }
            if (edge == 0ul) {
                return V9X_STATUS_INVALID_ARGUMENT;
            }
        }

        stream[at++] = maps[index].offset;
        /* Tiling bits deliberately absent: the texture is linear. */
        stream[at++] = maps[index].format |
                       ((height - 1ul) << V9X_I9XX_MS3_HEIGHT_SHIFT) |
                       ((width - 1ul) << V9X_I9XX_MS3_WIDTH_SHIFT);
        /* Pitch and MAX_LOD. See the header for the divergence between the
         * two trees over the cube-face mask, and why it is left out. */
        stream[at++] = (((pitch >> 2) - 1ul) << V9X_I9XX_MS4_PITCH_SHIFT) |
                       V9X_I9XX_MS4_MAX_LOD(maps[index].max_lod);
    }

    *written = at;
    return V9X_STATUS_OK;
}

/*
 * SS2 from the two filter requests. Nearest is FILTER_NEAREST (0) in both
 * fields, so the nearest/no-mip word is V9X_I9XX_SS2_NEAREST_NO_MIP by
 * construction. The mip field is not this function's: callers OR in
 * V9X_I9XX_SS2_MIP, so a map without levels keeps the word it always had. One
 * function so the builder and the decoder cannot compose it differently.
 */
v9x_u32 v9x_i9xx_sampler_filter_word(v9x_u32 min_linear, v9x_u32 mag_linear)
{
    v9x_u32 word = V9X_I9XX_SS2_NEAREST_NO_MIP;

    if (min_linear != 0ul) {
        word |= V9X_I9XX_SS2_MIN_LINEAR;
    }
    if (mag_linear != 0ul) {
        word |= V9X_I9XX_SS2_MAG_LINEAR;
    }
    return word;
}

v9x_status v9x_i9xx_build_sampler_state(
    const struct v9x_i9xx_texture *maps, v9x_u32 count,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 at = 0ul;
    v9x_u32 index;
    v9x_u32 extent;

    if (written != 0) { *written = 0ul; }
    if (stream == 0 || written == 0 || maps == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    extent = v9x_i9xx_sampler_state_extent(count);
    if (extent == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (capacity < extent) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }

    stream[at++] = V9X_I9XX_3DSTATE_SAMPLER_STATE |
                   (v9x_u32)((v9x_u16)count *
                             (v9x_u16)V9X_I9XX_SAMPLER_DWORDS_EACH);
    stream[at++] = v9x_i9xx_unit_enable_mask(count);

    for (index = 0ul; index < count; ++index) {
        if (!V9X_I9XX_ADDRESS_KNOWN(maps[index].wrap) ||
            !V9X_I9XX_MIPFILTER_KNOWN(maps[index].mip_filter)) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
    }
    for (index = 0ul; index < count; ++index) {
        /* The address mode for every axis, from the map: clamp, wrap or
         * mirror. Z has no coordinate in a 2D fetch and takes the same mode
         * so the dword has one shape. */
        v9x_u32 mode = V9X_I9XX_ADDRESS_TEXCOORDMODE(maps[index].wrap);

        /* MIN and MAG each nearest or bilinear, and the mip filter between
         * levels - NONE for a map without them. */
        stream[at++] = v9x_i9xx_sampler_filter_word(maps[index].min_linear,
                                                    maps[index].mag_linear) |
                       V9X_I9XX_SS2_MIP(maps[index].mip_filter);
        /*
         * Normalized coordinates, the address mode on every axis, and the
         * MAP INDEX written explicitly.
         *
         * Sampler n and map n are not implicitly paired - both trees write
         * the index - so relying on the pairing would be an assumption about
         * silicon nobody has stated.
         */
        stream[at++] = V9X_I9XX_SS3_NORMALIZED_COORDS |
                       (mode << V9X_I9XX_SS3_TCX_SHIFT) |
                       (mode << V9X_I9XX_SS3_TCY_SHIFT) |
                       (mode << V9X_I9XX_SS3_TCZ_SHIFT) |
                       (index << V9X_I9XX_SS3_MAP_INDEX_SHIFT);
        /* Border colour. Nothing samples it under clamp-to-edge or wrap. */
        stream[at++] = V9X_I9XX_SS4_BORDER_COLOR;
    }

    *written = at;
    return V9X_STATUS_OK;
}

/*
 * The four quadrant colours, as DWORD fill patterns.
 *
 * These are 565 BIT PATTERNS, not colours put through a conversion: the blit
 * writes the pattern into memory verbatim. The first three are nonetheless the
 * values the 2026-09-15 and 2026-09-16 captures read back from this part, so a
 * probe that reads one is reading a bit pattern the part has already been seen
 * to store and display. Each is doubled into a dword because XY_COLOR_BLT
 * fills in dwords and two 16-bit texels share one.
 *
 * Two distinctness requirements, and the second is why the fourth entry is not
 * the fill:
 *
 *  - They must all differ from EACH OTHER, or a probe could not say which
 *    quadrant it read.
 *  - None may equal the FILL. A quadrant painted 0x0842 would be read by a
 *    probe as 0x0842 whether the sampler worked or nothing drew at all, and
 *    the scene would report a pass for a draw that never happened. The fourth
 *    entry was the fill for exactly one commit.
 *
 * The host test asserts both rather than trusting the list.
 */
static const v9x_u32 v9x_i9xx_texture_quadrant[4] = {
    0x1c3e1c3eul,   /* 0xff1587f9, read back 2026-09-15 */
    0xf325f325ul,   /* 0xfff86428, read back 2026-09-15 */
    0x30383038ul,   /* 0xff2e03c8, read back 2026-09-16 */
    0x07e007e0ul    /* 565 green, distinct from the fill and from the rest */
};

v9x_u32 v9x_i9xx_texture_paint_extent(void)
{
    /* Four blits of six dwords, then one MI_FLUSH so the sampler sees them. */
    return (4ul * 6ul) + 1ul;
}

/*
 * Paint the texture with the GPU.
 *
 * XY_COLOR_BLT is the one operation this hardware is measured to perform
 * correctly, and it keeps the CPU out of the aperture entirely - which is the
 * condition the errata gate opened on, and why the render-target fill moved to
 * the GPU in the first place. A CPU-uploaded texture would be exactly the
 * access that decision avoided.
 *
 * Each blit is anchored at (0,0) of its own destination, so the quadrants are
 * addressed by ADDRESS rather than by coordinate. Quadrant n sits at
 * (n & 1) half-widths across and (n >> 1) half-heights down.
 */
v9x_status v9x_i9xx_build_texture_paint(
    const struct v9x_i9xx_texture *texture,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 at = 0ul;
    v9x_u32 quadrant;
    v9x_u32 produced = 0ul;
    v9x_u32 block_dwords;
    v9x_u32 block_bytes;

    if (written != 0) { *written = 0ul; }
    if (stream == 0 || written == 0 || texture == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (texture->width != V9X_I9XX_TEXTURE_WIDTH ||
        texture->height != V9X_I9XX_TEXTURE_HEIGHT ||
        texture->pitch != V9X_I9XX_TEXTURE_PITCH) {
        /* The quadrant arithmetic below is written for this texture. A
         * different one would need it re-derived, not re-used. */
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (capacity < v9x_i9xx_texture_paint_extent()) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }

    /* A quadrant is 16 texels wide; the blit counts dwords, and two 16-bit
     * texels share one. */
    block_dwords = V9X_I9XX_TEXTURE_BLOCK / 2ul;
    block_bytes = (v9x_u32)((v9x_u16)V9X_I9XX_TEXTURE_BLOCK * (v9x_u16)2u);

    for (quadrant = 0ul; quadrant < 4ul; ++quadrant) {
        v9x_u32 destination = texture->offset;

        if ((quadrant & 1ul) != 0ul) {
            destination += block_bytes;
        }
        if ((quadrant & 2ul) != 0ul) {
            destination += (v9x_u32)((v9x_u16)V9X_I9XX_TEXTURE_BLOCK *
                                     (v9x_u16)V9X_I9XX_TEXTURE_PITCH);
        }
        /*
         * Bounded by the texture itself, so a quadrant that computed its way
         * outside is refused by the blit builder rather than written. The
         * fourth quadrant ends on the texture's final byte, which makes that
         * check tight rather than generous.
         */
        if (v9x_i9xx_build_color_blt(
                destination, (v9x_u16)block_dwords,
                (v9x_u16)V9X_I9XX_TEXTURE_BLOCK,
                (v9x_u16)V9X_I9XX_TEXTURE_PITCH,
                v9x_i9xx_texture_quadrant[quadrant],
                texture->offset, V9X_I9XX_TEXTURE_BYTES,
                stream + at, capacity - at, &produced) != V9X_STATUS_OK) {
            return V9X_STATUS_INSUFFICIENT_MEMORY;
        }
        at += produced;
    }

    /*
     * One MI_FLUSH after all four, with every bit clear - which FLUSHES the
     * render cache rather than inhibiting it. Without it the sampler could
     * read the texture through a cache the blits had not reached.
     */
    if (capacity - at < 1ul) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    stream[at++] = V9X_I9XX_MI_FLUSH;

    *written = at;
    return V9X_STATUS_OK;
}

/* The colour a probe should read in quadrant n, as a single 565 texel. */
v9x_u32 v9x_i9xx_texture_quadrant_color(v9x_u32 quadrant)
{
    if (quadrant >= 4ul) {
        return 0ul;
    }
    return v9x_i9xx_texture_quadrant[quadrant] & 0xfffful;
}

/*
 * The depth CLEAR.
 *
 * One XY_COLOR_BLT over the whole depth buffer, then an MI_FLUSH so the values
 * are out of the render cache before the first primitive tests against them -
 * the same ordering the texture paint needs and for the same reason.
 *
 * A blit rather than _3DSTATE_CLEAR_PARAMETERS with PRIM3D_CLEAR_RECT. Mesa
 * offers both and its blitter path clears depth with i915_fill_blit; the
 * render path would add a state packet AND a primitive type, neither of which
 * can be double-sourced and neither of which this driver needs. The blit is
 * also the one operation this part is measured to perform correctly.
 *
 * docs\decisions\2026-09-16-intel-gen3-modulate-and-depth-audit.md section 5.
 */
v9x_u32 v9x_i9xx_depth_clear_extent(void)
{
    /* One six-dword blit and the flush. */
    return 6ul + 1ul;
}

v9x_status v9x_i9xx_build_depth_clear(
    v9x_u32 depth_offset, v9x_u32 depth_pitch, v9x_u32 depth_bytes,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 at = 0ul;
    v9x_u32 produced = 0ul;

    if (written != 0) { *written = 0ul; }
    if (stream == 0 || written == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (depth_pitch != V9X_I9XX_DEPTH_PITCH ||
        depth_bytes != V9X_I9XX_DEPTH_BYTES) {
        /* The rectangle below is written for this buffer. A different one
         * would need it re-derived, not re-used. */
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (capacity < v9x_i9xx_depth_clear_extent()) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }

    /*
     * The blit counts DWORDS across, and a 16-bit depth buffer puts two values
     * in each - so the width is the pitch in dwords, not in pixels. Getting
     * that backwards would clear half the buffer and leave the rest holding
     * whatever the page held, which under a LESS test is a region where
     * nothing ever draws.
     */
    if (v9x_i9xx_build_color_blt(
            depth_offset,
            (v9x_u16)(V9X_I9XX_DEPTH_PITCH / 4ul),
            (v9x_u16)V9X_I9XX_DEPTH_HEIGHT,
            (v9x_u16)V9X_I9XX_DEPTH_PITCH,
            V9X_I9XX_DEPTH_CLEAR_DWORD,
            depth_offset, depth_bytes,
            stream + at, capacity - at, &produced) != V9X_STATUS_OK) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    at += produced;
    stream[at++] = V9X_I9XX_MI_FLUSH;

    *written = at;
    return V9X_STATUS_OK;
}
