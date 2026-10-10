/*
 * The neutral draw description to MGA-2164W trapezoid state; see
 * d3d_mga_map.h. Pure, host-tested.
 */
#include "d3d_mga_map.h"

/* ZORG's alignment and reach (2164W spec 3-91, measured in the depth
 * record). */
#define V9X_D3D_MGA_ZORG_ALIGN 0x00000200ul
#define V9X_D3D_MGA_ZORG_LIMIT 0x01000000ul

/* TEXTRANS with no key: a zero mask and a non-zero key, which no texel
 * can match. A zero TEXTRANS keys every texel out (textures record). */
#define V9X_D3D_MGA_NO_KEY      1ul
#define V9X_D3D_MGA_NO_KEY_MASK 0ul
/* 1555's alpha bit, keyed to drop the texels whose alpha is 0. */
#define V9X_D3D_MGA_ALPHA_BIT   0x8000ul
/* TEXTRANS's key and mask are 16 bits, one texel. */
#define V9X_D3D_MGA_ALL_KEY_MASK 0xfffful

/* The Z buffer's units for Direct3D's 0..1 depth. */
#define V9X_D3D_MGA_TARGET_BYTES 2ul

static v9x_u32 v9x_d3d_mga_zmode(v9x_u32 func, v9x_u32 *skip)
{
    *skip = 0ul;
    switch (func) {
    case V9X_R3D_CMP_NEVER:
        *skip = 1ul;
        return V9X_MGA3D_ZMODE_NOZCMP;
    case V9X_R3D_CMP_LESS:
        return V9X_MGA3D_ZMODE_ZLT;
    case V9X_R3D_CMP_EQUAL:
        return V9X_MGA3D_ZMODE_ZE;
    case V9X_R3D_CMP_LESSEQUAL:
        return V9X_MGA3D_ZMODE_ZLTE;
    case V9X_R3D_CMP_GREATER:
        return V9X_MGA3D_ZMODE_ZGT;
    case V9X_R3D_CMP_NOTEQUAL:
        return V9X_MGA3D_ZMODE_ZNE;
    case V9X_R3D_CMP_GREATEREQUAL:
        return V9X_MGA3D_ZMODE_ZGTE;
    default:
        return V9X_MGA3D_ZMODE_NOZCMP;
    }
}

/* Whether Direct3D's alpha test passes alpha `alpha` against `ref`. */
static int v9x_d3d_mga_alpha_passes(v9x_u32 func, v9x_u32 ref,
                                    v9x_u32 alpha)
{
    switch (func) {
    case V9X_R3D_CMP_NEVER:
        return 0;
    case V9X_R3D_CMP_LESS:
        return alpha < ref;
    case V9X_R3D_CMP_EQUAL:
        return alpha == ref;
    case V9X_R3D_CMP_LESSEQUAL:
        return alpha <= ref;
    case V9X_R3D_CMP_GREATER:
        return alpha > ref;
    case V9X_R3D_CMP_NOTEQUAL:
        return alpha != ref;
    case V9X_R3D_CMP_GREATEREQUAL:
        return alpha >= ref;
    default:
        return 1;
    }
}

v9x_u32 v9x_d3d_mga_stipple(v9x_u32 alpha)
{
    /* Nearest of 16, 8, 4 and 2 sixteenths, or none (patterns 0000, 0001,
     * 0011, 0111, 1111). */
    if (alpha >= 192ul) {
        return 0ul;
    }
    if (alpha >= 96ul) {
        return 1ul;
    }
    if (alpha >= 48ul) {
        return 3ul;
    }
    if (alpha >= 16ul) {
        return 7ul;
    }
    return V9X_D3D_MGA_STIPPLE_NONE;
}

/*
 * The combine: decal shows the texel where (alpha & tamask) != takey, so
 * tamask 0 and takey 1 show every texel; 1555's DECALALPHA shows the
 * Gouraud colour where its alpha bit is 0 (tamask 1, takey 0), which is
 * Direct3D's blend by texel alpha for a one-bit alpha.
 */
static v9x_u32 v9x_d3d_mga_texture_op(const V9X_R3D_DRAW *draw,
                                      const V9X_D3D_MGA_TEXTURE *texture,
                                      struct v9x_mga3d_texture *out)
{
    out->modulate = 0ul;
    out->alpha_mask = 0ul;
    out->alpha_key = 1ul;
    switch (draw->texture.op) {
    case V9X_R3D_TEXOP_DECAL:
    case V9X_R3D_TEXOP_COPY:
        return V9X_D3D_MGA_REFUSE_NONE;
    case V9X_R3D_TEXOP_MODULATE:
    case V9X_R3D_TEXOP_MODULATEALPHA:
        out->modulate = 1ul;
        return V9X_D3D_MGA_REFUSE_NONE;
    case V9X_R3D_TEXOP_DECALALPHA:
        if (texture->format == V9X_MGA3D_TEX_TW15) {
            out->alpha_mask = 1ul;
            out->alpha_key = 0ul;
        }
        return V9X_D3D_MGA_REFUSE_NONE;
    default:
        return V9X_D3D_MGA_REFUSE_TEXTURE_OP;
    }
}

v9x_u32 v9x_d3d_mga_map_draw(const V9X_R3D_DRAW *draw,
                             const V9X_D3D_MGA_TEXTURE *texture,
                             v9x_u32 vram_bytes, v9x_u32 specular_rgb,
                             V9X_D3D_MGA_MAPPED *out)
{
    struct v9x_mga3d_trap *base;
    v9x_u32 zorg;
    v9x_u32 skip;
    v9x_u32 reason;
    int textured;
    int key_alpha = 0;
    int key_color = 0;
    v9x_u32 index;
    unsigned char *bytes;

    if (draw == 0 || texture == 0 || out == 0) {
        return V9X_D3D_MGA_REFUSE_TARGET;
    }
    bytes = (unsigned char *)out;
    for (index = 0ul; index < (v9x_u32)sizeof(*out); ++index) {
        bytes[index] = 0u;
    }
    base = &out->base;

    /* The render interface states everything exactly; this engine's
     * stipple and point sampling are approximations it may not make. */
    if (draw->explicit_state != 0ul) {
        return V9X_D3D_MGA_REFUSE_EXPLICIT;
    }
    if (draw->target.format != V9X_R3D_FORMAT_RGB565 &&
        draw->target.format != V9X_R3D_FORMAT_XRGB1555) {
        return V9X_D3D_MGA_REFUSE_TARGET;
    }
    if (!v9x_mga_surface_ok(draw->target.pitch, draw->target.offset,
                            V9X_D3D_MGA_TARGET_BYTES)) {
        return V9X_D3D_MGA_REFUSE_TARGET_SHAPE;
    }
    if (draw->fog_enable != 0ul) {
        return V9X_D3D_MGA_REFUSE_FOG;
    }
    if (draw->specular_enable != 0ul && specular_rgb != 0ul) {
        return V9X_D3D_MGA_REFUSE_SPECULAR;
    }

    base->vram_bytes = vram_bytes;
    base->target_offset = draw->target.offset;
    base->pitch_bytes = draw->target.pitch;
    base->bytes_per_pixel = V9X_D3D_MGA_TARGET_BYTES;
    base->shade = V9X_MGA3D_SHADE_GOURAUD;
    base->dither_555 = draw->target.format == V9X_R3D_FORMAT_XRGB1555
        ? 1ul : 0ul;
    out->flat = draw->shade_mode == V9X_R3D_SHADE_FLAT ? 1ul : 0ul;

    /* Blending: none, ONE/ZERO (the same), or source alpha over the
     * destination, drawn as a stipple. */
    if (draw->blend_enable != 0ul &&
        !(draw->src_blend == V9X_R3D_BLEND_ONE &&
          draw->dst_blend == V9X_R3D_BLEND_ZERO)) {
        if (draw->src_blend != V9X_R3D_BLEND_SRCALPHA ||
            draw->dst_blend != V9X_R3D_BLEND_INVSRCALPHA) {
            return V9X_D3D_MGA_REFUSE_BLEND;
        }
        out->stipple = 1ul;
    }

    /* Depth: the engine's Z buffer has the target's pitch, and ZORG is the
     * Z buffer's address less twice the target origin's pixel address. */
    if (draw->depth_enable != 0ul && draw->depth.object != 0 &&
        draw->depth.pitch != 0ul) {
        if (draw->depth.pitch != draw->target.pitch ||
            draw->depth.offset < draw->target.offset) {
            return V9X_D3D_MGA_REFUSE_DEPTH;
        }
        zorg = draw->depth.offset - draw->target.offset;
        if ((zorg % V9X_D3D_MGA_ZORG_ALIGN) != 0ul ||
            zorg >= V9X_D3D_MGA_ZORG_LIMIT) {
            return V9X_D3D_MGA_REFUSE_DEPTH;
        }
        base->depth = V9X_MGA3D_DEPTH_16;
        base->zmode = v9x_d3d_mga_zmode(draw->depth_func, &skip);
        base->z_write = draw->depth_write != 0ul ? 1ul : 0ul;
        base->z_offset = draw->depth.offset;
        if (skip != 0ul) {
            out->skip = 1ul;
        }
    }

    textured = draw->texture.object != 0;
    if (textured) {
        if (texture->valid == 0ul) {
            return V9X_D3D_MGA_REFUSE_TEXTURE;
        }
        if ((draw->texture.address != V9X_R3D_ADDRESS_WRAP &&
             draw->texture.address != V9X_R3D_ADDRESS_CLAMP) ||
            draw->texture.wrap_either != 0ul) {
            return V9X_D3D_MGA_REFUSE_ADDRESS;
        }
        base->texture.enabled = 1ul;
        base->texture.offset = texture->offset;
        base->texture.format = texture->format;
        base->texture.log2_width = texture->log2_width;
        base->texture.log2_height = texture->log2_height;
        base->texture.log2_pitch = texture->log2_pitch;
        if (draw->texture.address == V9X_R3D_ADDRESS_CLAMP) {
            base->texture.clamp_u = 1ul;
            base->texture.clamp_v = 1ul;
        }
        reason = v9x_d3d_mga_texture_op(draw, texture, &base->texture);
        if (reason != V9X_D3D_MGA_REFUSE_NONE) {
            return reason;
        }
        if (draw->color_key_enable != 0ul && texture->has_color_key != 0ul) {
            key_color = 1;
        }
        /* A one-bit alpha that is 0 is a texel that is not there, under a
         * source-alpha blend as under an alpha test that 0 fails. */
        if (texture->format == V9X_MGA3D_TEX_TW15 &&
            draw->texture.op != V9X_R3D_TEXOP_DECALALPHA &&
            out->stipple != 0ul) {
            key_alpha = 1;
        }
    }

    /* The alpha test, which the chip does not have: served only where the
     * alpha is known - opaque vertices, and a texel alpha that is all or
     * nothing - by passing everything, skipping the draw, or keying out
     * the 1555 texels whose alpha bit fails. */
    if (draw->alpha_test_enable != 0ul &&
        draw->alpha_func != V9X_R3D_CMP_ALWAYS) {
        int opaque_pass = v9x_d3d_mga_alpha_passes(draw->alpha_func,
                                                   draw->alpha_ref, 255ul);
        int clear_pass = v9x_d3d_mga_alpha_passes(draw->alpha_func,
                                                  draw->alpha_ref, 0ul);

        if (draw->vertex_alpha_opaque == 0ul) {
            return V9X_D3D_MGA_REFUSE_ALPHA_TEST;
        }
        if (!opaque_pass) {
            if (clear_pass) {
                return V9X_D3D_MGA_REFUSE_ALPHA_TEST;
            }
            out->skip = 1ul;
        } else if (!clear_pass && textured &&
                   texture->format == V9X_MGA3D_TEX_TW15 &&
                   draw->texture.op != V9X_R3D_TEXOP_DECALALPHA) {
            key_alpha = 1;
        }
    }

    if (textured) {
        if (key_color && key_alpha) {
            return V9X_D3D_MGA_REFUSE_KEYS;
        }
        if (key_color) {
            /* A colour key compares colour: on 1555 the alpha bit is left
             * out, as Matrox's HAL leaves it (V9XDDP ColorKeyOk, texel
             * FC1Fh against key 7C1Fh, A8U4I5 boot 383). */
            base->texture.key_mask =
                texture->format == V9X_MGA3D_TEX_TW15
                    ? (V9X_D3D_MGA_ALL_KEY_MASK & ~V9X_D3D_MGA_ALPHA_BIT)
                    : V9X_D3D_MGA_ALL_KEY_MASK;
            base->texture.key = texture->color_key & base->texture.key_mask;
        } else if (key_alpha) {
            base->texture.key = 0ul;
            base->texture.key_mask = V9X_D3D_MGA_ALPHA_BIT;
        } else {
            base->texture.key = V9X_D3D_MGA_NO_KEY;
            base->texture.key_mask = V9X_D3D_MGA_NO_KEY_MASK;
        }
    }
    return V9X_D3D_MGA_REFUSE_NONE;
}
