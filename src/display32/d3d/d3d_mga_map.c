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

/* Bytes a pixel of the 16 bpp targets and of XRGB8888, and of the two Z
 * cells. */
#define V9X_D3D_MGA_TARGET_BYTES_16 2ul
#define V9X_D3D_MGA_TARGET_BYTES_32 4ul
#define V9X_D3D_MGA_Z_BYTES_16      2ul
#define V9X_D3D_MGA_Z_BYTES_32      4ul

/* The render interface's write-mask bits (r3d_abi.h V9X_R3D_ABI_WRITE_*). */
#define V9X_D3D_MGA_WRITE_RED   1ul
#define V9X_D3D_MGA_WRITE_GREEN 2ul
#define V9X_D3D_MGA_WRITE_BLUE  4ul
#define V9X_D3D_MGA_WRITE_RGB   7ul

/* One pixel's bits of a channel, by target layout. */
static v9x_u32 v9x_d3d_mga_channel_bits(v9x_u32 format, v9x_u32 channel)
{
    if (format == V9X_R3D_FORMAT_XRGB8888) {
        return channel == V9X_D3D_MGA_WRITE_RED ? 0x00ff0000ul
             : channel == V9X_D3D_MGA_WRITE_GREEN ? 0x0000ff00ul
             : 0x000000fful;
    }
    if (format == V9X_R3D_FORMAT_XRGB1555) {
        return channel == V9X_D3D_MGA_WRITE_RED ? 0x7c00ul
             : channel == V9X_D3D_MGA_WRITE_GREEN ? 0x03e0ul
             : 0x001ful;
    }
    return channel == V9X_D3D_MGA_WRITE_RED ? 0xf800ul
         : channel == V9X_D3D_MGA_WRITE_GREEN ? 0x07e0ul
         : 0x001ful;
}

/*
 * An explicit draw's scissor and write mask as the engine's clip window
 * and plane mask: CXBNDRY's columns and YTOP/YBOT's linear row addresses
 * (YDSTORG + row x PITCH) inclusive, PLNWT replicated to every pixel of
 * the 32-bit word at 16 bpp. All of it measured on 3D trapezoids by MGA2D
 * /ri. Zero when the scissor is empty: nothing would be drawn.
 */
static int v9x_d3d_mga_explicit_state(const V9X_R3D_DRAW *draw,
                                      v9x_u32 target_bytes,
                                      V9X_D3D_MGA_MAPPED *out)
{
    v9x_u32 origin = draw->target.offset / target_bytes;
    v9x_u32 pitch = draw->target.pitch / target_bytes;
    v9x_u32 mask = 0ul;
    v9x_u32 channel;

    if (draw->scissor_right <= draw->scissor_left ||
        draw->scissor_bottom <= draw->scissor_top) {
        return 0;
    }
    out->cxbndry = ((draw->scissor_right - 1ul) << 16) | draw->scissor_left;
    out->ytop = origin + draw->scissor_top * pitch;
    out->ybot = origin + (draw->scissor_bottom - 1ul) * pitch;

    if ((draw->write_mask & V9X_D3D_MGA_WRITE_RGB) == V9X_D3D_MGA_WRITE_RGB) {
        mask = 0xfffffffful;
    } else {
        for (channel = V9X_D3D_MGA_WRITE_RED;
             channel <= V9X_D3D_MGA_WRITE_BLUE; channel <<= 1) {
            if ((draw->write_mask & channel) != 0ul) {
                mask |= v9x_d3d_mga_channel_bits(draw->target.format,
                                                 channel);
            }
        }
        if (target_bytes == 2ul) {
            mask |= mask << 16;
        }
    }
    out->plnwt = mask;
    out->state_writes =
        (draw->scissor_left != 0ul || draw->scissor_top != 0ul ||
         draw->scissor_right != draw->target.width ||
         draw->scissor_bottom != draw->target.height ||
         mask != 0xfffffffful) ? 1ul : 0ul;
    return 1;
}

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

/* Source times destination: DESTCOLOR/ZERO, ZERO/SRCCOLOR, or both
 * (Direct3D's 2x modulate). */
static int v9x_d3d_mga_multiplies(const V9X_R3D_DRAW *draw)
{
    return (draw->src_blend == V9X_R3D_BLEND_DESTCOLOR &&
            (draw->dst_blend == V9X_R3D_BLEND_ZERO ||
             draw->dst_blend == V9X_R3D_BLEND_SRCCOLOR)) ||
        (draw->src_blend == V9X_R3D_BLEND_ZERO &&
         draw->dst_blend == V9X_R3D_BLEND_SRCCOLOR);
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
    int key_black = 0;
    int explicit_draw;
    v9x_u32 index;
    v9x_u32 target_bytes;
    v9x_u32 z_bytes;
    unsigned char *bytes;

    if (draw == 0 || texture == 0 || out == 0) {
        return V9X_D3D_MGA_REFUSE_TARGET;
    }
    bytes = (unsigned char *)out;
    for (index = 0ul; index < (v9x_u32)sizeof(*out); ++index) {
        bytes[index] = 0u;
    }
    base = &out->base;

    /* The render interface states everything exactly. CPU levels and a
     * second unit have no sampler here; the approximations below are
     * refused one by one. */
    explicit_draw = draw->explicit_state != 0ul;
    if (explicit_draw &&
        (draw->texture.levels != 0 || draw->texture1.object != 0 ||
         draw->texture1.levels != 0 || draw->texcoords1 != 0)) {
        return V9X_D3D_MGA_REFUSE_EXPLICIT;
    }
    /* XRGB8888 at PWIDTH 32: the X byte is FCOL's top byte (mga_3d.h),
     * which the map leaves zero. */
    if (draw->target.format == V9X_R3D_FORMAT_XRGB8888) {
        target_bytes = V9X_D3D_MGA_TARGET_BYTES_32;
    } else if (draw->target.format == V9X_R3D_FORMAT_RGB565 ||
               draw->target.format == V9X_R3D_FORMAT_XRGB1555) {
        target_bytes = V9X_D3D_MGA_TARGET_BYTES_16;
    } else {
        return V9X_D3D_MGA_REFUSE_TARGET;
    }
    if (!v9x_mga_surface_ok(draw->target.pitch, draw->target.offset,
                            target_bytes)) {
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
    base->bytes_per_pixel = target_bytes;
    base->shade = V9X_MGA3D_SHADE_GOURAUD;
    base->dither_555 = draw->target.format == V9X_R3D_FORMAT_XRGB1555
        ? 1ul : 0ul;
    out->flat = draw->shade_mode == V9X_R3D_SHADE_FLAT ? 1ul : 0ul;

    if (explicit_draw) {
        /* Undithered, as the software engine draws: dithering moves even
         * an exact level at 16 bpp (F8 drawn as F0, MGA2D /ri). */
        base->no_dither = 1ul;
        if (!v9x_d3d_mga_explicit_state(draw, target_bytes, out)) {
            out->skip = 1ul;
            return V9X_D3D_MGA_REFUSE_NONE;
        }
        /* No blender: the stipple, the additive key and the multiply
         * below are Direct3D's approximations. */
        if (draw->blend_enable != 0ul &&
            !(draw->src_blend == V9X_R3D_BLEND_ONE &&
              draw->dst_blend == V9X_R3D_BLEND_ZERO)) {
            return V9X_D3D_MGA_REFUSE_BLEND;
        }
    }

    /*
     * Blending: none, ONE/ZERO (the same), source alpha over the
     * destination drawn as a stipple, or additive.
     *
     * Additive (ONE/ONE, or SRCALPHA/ONE at the stipple density of the
     * alpha) has no counterpart on the chip. On a texture, black adds
     * nothing, so its texels are keyed out (below) and the rest drawn over
     * the destination: exact for black-backed sprites such as Half-Life's
     * HUD, which boot 388 drew none of (33,449 ONE/ONE draws refused);
     * elsewhere a colour replaces what it should add to. Matrox's HAL draws
     * the same HUD with the black boxes left in (boot 389). Untextured,
     * there is nothing to key, and it is refused.
     */
    if (draw->blend_enable != 0ul &&
        !(draw->src_blend == V9X_R3D_BLEND_ONE &&
          draw->dst_blend == V9X_R3D_BLEND_ZERO)) {
        if (draw->dst_blend == V9X_R3D_BLEND_ONE &&
            (draw->src_blend == V9X_R3D_BLEND_ONE ||
             draw->src_blend == V9X_R3D_BLEND_SRCALPHA)) {
            if (draw->texture.object == 0) {
                return V9X_D3D_MGA_REFUSE_BLEND;
            }
            key_black = 1;
            if (draw->src_blend == V9X_R3D_BLEND_SRCALPHA) {
                out->stipple = 1ul;
            }
        } else if (draw->src_blend == V9X_R3D_BLEND_SRCALPHA &&
                   draw->dst_blend == V9X_R3D_BLEND_INVSRCALPHA) {
            out->stipple = 1ul;
        } else if (v9x_d3d_mga_multiplies(draw)) {
            /* Multiplying into the frame needs the destination, which the
             * 3D engine never reads. The source is drawn through the half
             * stipple instead: light and shade show as every other pixel
             * of the lightmap. Half-Life's lighting is DESTCOLOR/SRCCOLOR
             * (boot 391, all 28,728 blend refusals); refused, its world
             * was unlit. */
            base->trans = V9X_D3D_MGA_STIPPLE_HALF;
        } else {
            return V9X_D3D_MGA_REFUSE_BLEND;
        }
    }

    /*
     * Depth: the engine's Z buffer has the target's pitch in pixels, 2 or
     * 4 bytes each by MACCESS.zwidth, and ZORG is the Z buffer's address
     * less the target origin's pixel address times the Z cell (2164W spec
     * 3-91; both widths measured in the depth record). The core's 32-bit
     * cell is Z24X8 by name; here all 32 bits are depth.
     */
    if (draw->depth_enable != 0ul && draw->depth.object != 0 &&
        draw->depth.pitch != 0ul) {
        z_bytes = draw->depth.format == V9X_R3D_DEPTH_Z24X8
            ? V9X_D3D_MGA_Z_BYTES_32 : V9X_D3D_MGA_Z_BYTES_16;
        /* target_offset / target_bytes is below 2^22, so the product
         * fits. */
        if (draw->depth.pitch / z_bytes !=
                draw->target.pitch / target_bytes ||
            (draw->depth.pitch % z_bytes) != 0ul ||
            draw->depth.offset <
                draw->target.offset / target_bytes * z_bytes) {
            return V9X_D3D_MGA_REFUSE_DEPTH;
        }
        zorg = draw->depth.offset -
            draw->target.offset / target_bytes * z_bytes;
        if ((zorg % V9X_D3D_MGA_ZORG_ALIGN) != 0ul ||
            zorg >= V9X_D3D_MGA_ZORG_LIMIT) {
            return V9X_D3D_MGA_REFUSE_DEPTH;
        }
        base->depth = z_bytes == V9X_D3D_MGA_Z_BYTES_32
            ? V9X_MGA3D_DEPTH_32 : V9X_MGA3D_DEPTH_16;
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
        /* The sampler is nearest on the top level only. */
        if (explicit_draw &&
            (draw->texture.min_filter != V9X_R3D_FILTER_NEAREST ||
             draw->texture.mag_filter != V9X_R3D_FILTER_NEAREST)) {
            return V9X_D3D_MGA_REFUSE_FILTER;
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
        /* TEXTRANS holds one key. */
        if (key_color + key_alpha + key_black > 1) {
            return V9X_D3D_MGA_REFUSE_KEYS;
        }
        if (key_color || key_black) {
            /* A colour key compares colour: on 1555 the alpha bit is left
             * out, as Matrox's HAL leaves it (V9XDDP ColorKeyOk, texel
             * FC1Fh against key 7C1Fh, A8U4I5 boot 383). */
            base->texture.key_mask =
                texture->format == V9X_MGA3D_TEX_TW15
                    ? (V9X_D3D_MGA_ALL_KEY_MASK & ~V9X_D3D_MGA_ALPHA_BIT)
                    : V9X_D3D_MGA_ALL_KEY_MASK;
            base->texture.key = key_black
                ? 0ul : texture->color_key & base->texture.key_mask;
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
