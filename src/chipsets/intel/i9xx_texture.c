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
        /* A row must hold its own texels. RGB565 is two bytes each. */
        if (pitch < (v9x_u32)((v9x_u16)width * (v9x_u16)2u)) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        /* The address is a graphics offset and is page aligned - a choice the
         * audit records as a choice, since neither reference tree states a
         * requirement. */
        if ((maps[index].offset & (V9X_I9XX_SANDBOX_PAGE_BYTES - 1ul)) !=
                0ul) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }

        stream[at++] = maps[index].offset;
        /* Tiling bits deliberately absent: the texture is linear. */
        stream[at++] = V9X_I9XX_MAPSURF_16BIT_RGB565 |
                       ((height - 1ul) << V9X_I9XX_MS3_HEIGHT_SHIFT) |
                       ((width - 1ul) << V9X_I9XX_MS3_WIDTH_SHIFT);
        /* Pitch alone. See the header for the divergence between the two
         * trees over the cube-face mask, and why this is the form taken. */
        stream[at++] = ((pitch >> 2) - 1ul) << V9X_I9XX_MS4_PITCH_SHIFT;
    }

    *written = at;
    return V9X_STATUS_OK;
}

v9x_status v9x_i9xx_build_sampler_state(
    v9x_u32 count, v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 at = 0ul;
    v9x_u32 index;
    v9x_u32 extent;

    if (written != 0) { *written = 0ul; }
    if (stream == 0 || written == 0) {
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
        /* Nearest, no mips. Every filter field is zero and the constant says
         * so by name, because a forgotten field and a zero one are otherwise
         * the same dword. */
        stream[at++] = V9X_I9XX_SS2_NEAREST_NO_MIP;
        /*
         * Normalized coordinates, clamp to edge on every axis, and the MAP
         * INDEX written explicitly.
         *
         * Sampler n and map n are not implicitly paired - both trees write
         * the index - so relying on the pairing would be an assumption about
         * silicon nobody has stated.
         */
        stream[at++] = V9X_I9XX_SS3_NORMALIZED_COORDS |
                       (V9X_I9XX_TEXCOORDMODE_CLAMP_EDGE <<
                        V9X_I9XX_SS3_TCX_SHIFT) |
                       (V9X_I9XX_TEXCOORDMODE_CLAMP_EDGE <<
                        V9X_I9XX_SS3_TCY_SHIFT) |
                       (V9X_I9XX_TEXCOORDMODE_CLAMP_EDGE <<
                        V9X_I9XX_SS3_TCZ_SHIFT) |
                       (index << V9X_I9XX_SS3_MAP_INDEX_SHIFT);
        /* Border colour. Nothing samples it under clamp-to-edge. */
        stream[at++] = V9X_I9XX_SS4_BORDER_COLOR;
    }

    *written = at;
    return V9X_STATUS_OK;
}
