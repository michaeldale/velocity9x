/*
 * Intel Gen4 (GM965, GMA X3100) Direct3D engine.
 *
 * docs\plans\intel-gen4-gen5-bringup.md, Phase 2. Every command and state
 * word comes from the host-tested builders in src\chipsets\intel\g4_3d.c
 * and every kernel from g4_eu.c; this file resolves surfaces, maps the
 * neutral draw onto that state, and submits.
 *
 * The ring is the Gen3 engine's: the GM965 keeps the Gen3 engine type for
 * its ring and blitter (gma950_hw16.c), and the I9XX_GEN4 bit routes
 * Direct3D here. Each draw is a batch in the reserve, started from the ring
 * and sealed with the Gen3 breadcrumb by v9x_d3d_i9xx_submit_g4_batch, so
 * Flip, Lock and Blt wait for 3D work through the drain they already use.
 *
 * Two slots, as on Haswell: each has its own state block, batch and vertex
 * area, and a draw rewrites a slot only once the breadcrumb of the batch
 * last submitted from it has landed, so the CPU builds one draw while the
 * GPU runs the other. The kernels are shared and are rewritten only after
 * a full drain. A draw waits for its own breadcrumb only while GDI may be
 * drawing on the screen.
 *
 * Mapping policy is Haswell's, through the pure helpers in d3d_hsw_map.c:
 * Intel's surface format numbers are the same on Gen4 and Gen7.5 (Vol 1
 * "Surface Format" in each). Depth is linear on Gen4 - 965 PRM Vol 2,
 * 3DSTATE_DEPTH_BUFFER: tile walk "is ignored when the surface is linear" -
 * so Z buffers come from DirectDraw's heap and clears are the core's.
 *
 * Nothing in this file has executed on hardware.
 */
#include "d3d_internal.h"
#include "d3d_hsw_map.h"
#include "d3d_state.h"
#include "velocity9x/intel_g4_3d.h"
#include "velocity9x/intel_g4_eu.h"
#include "velocity9x/intel_gma.h"
#include "velocity9x/intel_hsw_3d.h"

/* Graphics addresses inside the reserve: the ring starts it, at the end of
 * DirectDraw's heap (d3d_i9xx.c's breadcrumb offset rests on the same). */
#define V9X_D3D_G4_GFX(offset) (v9x_hal->fb.vram_bytes + (offset))

/* The framebuffer is at aperture offset 0, as the Gen3 engine has it. */
#define V9X_D3D_G4_APERTURE_MASK 0x0ffffffful

/* Triangles a draw's vertex area holds. */
#define V9X_D3D_G4_MAX_TRIANGLES \
    (V9X_G4_VERTEX_BYTES / (3ul * V9X_G4_VB_PITCH))

/* Kernel build space, in cached memory (see d3d_hsw.c on why). */
#define V9X_D3D_G4_KERNEL_INSTS (V9X_G4_WM_KERNEL_BYTES / 16ul)

/* Signature words: the SF kernel's mark, then four per WM slot. */
/* The kernels' revision, in every signature: the reserve survives a warm
 * reset, so a driver whose generator changed must not trust a mark the
 * last one left. Bump it with any change to g4_eu.c's output. */
#define V9X_D3D_G4_KERNEL_REV 0x02ul
#define V9X_D3D_G4_SF_MARK  (0x47345300ul | V9X_D3D_G4_KERNEL_REV)
#define V9X_D3D_G4_SIG_WORDS 4ul

/* A slot's last sequence is looked for this many status reads, about a
 * microsecond each, before the draw falls back to draining everything. */
#define V9X_D3D_G4_SLOT_POLLS 0x00200000ul

/* Refusal reasons, in the shared policy counters (V9XTRACE M64PolicyNN):
 * Haswell's numbering where the meaning is the same. */
#define V9X_D3D_G4_REFUSE_NONE      0ul
#define V9X_D3D_G4_REFUSE_TARGET    1ul
#define V9X_D3D_G4_REFUSE_DEPTH     2ul
#define V9X_D3D_G4_REFUSE_TEXTURE   3ul
#define V9X_D3D_G4_REFUSE_TEXOP     4ul
#define V9X_D3D_G4_REFUSE_BLEND     5ul
#define V9X_D3D_G4_REFUSE_ADDRESS   6ul
#define V9X_D3D_G4_REFUSE_KEY       7ul
#define V9X_D3D_G4_REFUSE_ALPHA     8ul
#define V9X_D3D_G4_REFUSE_NOT_READY 11ul
#define V9X_D3D_G4_REFUSE_IDLE      12ul
#define V9X_D3D_G4_REFUSE_STATE     13ul
#define V9X_D3D_G4_REFUSE_BATCH     15ul
#define V9X_D3D_G4_REFUSE_SUBMIT    16ul

/* A Y-major tile is 128 bytes by 32 rows (965 PRM Vol 1, "Tiled Memory");
 * a depth buffer is placed in whole ones. See v9x_d3d_g4_create_depth. */
#define V9X_D3D_G4_DEPTH_PITCH_ALIGN 128ul
#define V9X_D3D_G4_DEPTH_ROW_ALIGN   32ul

/* Blocks this engine places (mip trees, Z buffers) start on a page, which
 * d3d_place.c's release rule and the tiles both take. */
#define V9X_D3D_G4_PLACE_ALIGN    4096ul

/* Texture Coordinate Mode for all three axes. */
#define V9X_D3D_G4_TCM_WRAP   0u
#define V9X_D3D_G4_TCM_MIRROR 1u
#define V9X_D3D_G4_TCM_CLAMP  2u

static const V9X_D3D_ENGINE_LIMITS v9x_d3d_g4_limits = {
    16ul,           /* target_bits_per_pixel */
    16384ul,        /* target_pitch_max */
    4ul,            /* target_pitch_align */
    4096ul,         /* target_dimension_max */
    1ul,            /* texture_size_min */
    2048ul,         /* texture_size_max */
    8192.0f,        /* coordinate_limit */
    16ul,           /* depth_bits_per_pixel: D16_UNORM */
    16ul,           /* texture_align */
    1ul,            /* clip_in_core: CLIP is passed through */
    1ul,            /* depth_pitch_own: 3DSTATE_DEPTH_BUFFER's own pitch */
    0ul,            /* depth_fill_shift */
    2ul,            /* texture_units: the render interface's step 4 */
    /* SF origin bias 0.5 (Mesa and xf86 alike): the GL pixel centre. */
    1ul,            /* pixel_centre_half */
    1ul,            /* depth_tiled: Y tiles (v9x_d3d_g4_create_depth) */
    16384ul,        /* target_pitch_max_xrgb8888 */
    /* A 32-bit Z buffer is D24_UNORM_X8, Y-tiled. A linear one hung the 3D
     * pipe on TOMMOHP (boot 8, V9XD32 /z24: ACTHD at the batch's closing
     * MI_FLUSH, HEAD short of TAIL), where linear D16 ran every check. */
    24ul            /* depth32_bits */
};

typedef struct v9x_d3d_g4_texture {
    struct v9x_g4_sampler sampler;
    v9x_u16 format;
    v9x_u32 address;
    v9x_u16 width;
    v9x_u16 height;
    v9x_u32 pitch;
    /* Mip levels the sampler may read: 1, or a chain create_surface laid
     * out (v9x_d3d_g4_mip_levels). */
    v9x_u8 levels;
} V9X_D3D_G4_TEXTURE;

typedef struct v9x_d3d_g4_state {
    struct v9x_g4_ps_desc desc;
    struct v9x_g4_cc cc;
    v9x_u16 target_format;
    v9x_u8 write_disable;
    V9X_D3D_G4_TEXTURE texture;
    V9X_D3D_G4_TEXTURE texture1;
    int textured;           /* units sampled: 0, 1 or 2 */
    /* 3DSTATE_CHROMA_KEY's range when unit 0's sampler keys. */
    v9x_u32 chroma_low;
    v9x_u32 chroma_high;
    int depth;
    v9x_u32 depth_address;
    v9x_u32 depth_pitch;
    v9x_u8 depth_format;
    v9x_u32 scissor[4];
    int scissor_empty;
} V9X_D3D_G4_STATE;

/* Set on a timeout; nothing is submitted again in this process. */
static int v9x_d3d_g4_quarantined = 0;

static struct v9x_g4_inst v9x_d3d_g4_kernel_words[V9X_D3D_G4_KERNEL_INSTS];
static v9x_u32 v9x_d3d_g4_batch_words[V9X_G4_BATCH_BYTES / 4ul];

static v9x_u32 *v9x_d3d_g4_reserve(DWORD offset)
{
    return (v9x_u32 *)(v9x_hal->engine.ring_linear_base + offset);
}

static DWORD v9x_d3d_g4_vram(DWORD offset)
{
    return (v9x_hal->fb.physical_base & V9X_D3D_G4_APERTURE_MASK) + offset;
}

static void v9x_d3d_g4_count_refusal(DWORD reason)
{
    V9X_D3D_DIAGNOSTICS *diagnostics = &v9x_hal->d3d_diagnostics;

    diagnostics->m64_policy_last = reason;
    if (reason < 20ul) {
        ++diagnostics->m64_policy_counts[reason];
    }
}

/* The encodings to build for: Gen5's on Ironlake (I9XX_GEN5), Gen4's
 * otherwise. Kernels, state and batch all ask this. */
static v9x_u8 v9x_d3d_g4_gen(void)
{
    return (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_I9XX_GEN5) != 0ul
               ? V9X_G4_GEN5
               : V9X_G4_GEN4;
}

static int v9x_d3d_g4_ready(void)
{
    return v9x_hal != 0 && !v9x_d3d_g4_quarantined &&
           (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
           (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
           v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_INTEL_GEN3 &&
           (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAPS_I9XX_G4_3D) !=
               0ul &&
           (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_D3D) != 0ul &&
           v9x_hal->engine.control_linear_base != 0ul &&
           v9x_hal->engine.ring_linear_base != 0ul &&
           v9x_hal->engine.ring_bytes != 0ul &&
           (v9x_hal->fb.vram_bytes & 0xffful) == 0ul;
}

/* The surface's own layout, or the display's, through Haswell's pure
 * mapping (the format numbers are the same). bytes may be null. */
static int v9x_d3d_g4_texture_format_of(const V9X_DD_SURFACE_LCL *surface,
                                        v9x_u16 *format, int *has_alpha,
                                        v9x_u32 *bytes)
{
    const V9X_DDPIXELFORMAT *pf;
    v9x_u32 texel_bytes;

    if (surface == 0 || surface->lpGbl == 0) {
        return 0;
    }
    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul) {
        pf = &surface->lpGbl->ddpfSurface;
    } else {
        pf = &v9x_hal->info.vmiData.ddpfDisplay;
    }
    if ((pf->dwFlags & V9X_DDPF_RGB) == 0ul ||
        !v9x_d3d_hsw_texel_format(pf->dwRGBBitCount,
                                  (pf->dwFlags & V9X_DDPF_ALPHAPIXELS) != 0ul,
                                  pf->dwRBitMask, pf->dwGBitMask,
                                  pf->dwBBitMask, pf->dwRGBAlphaBitMask,
                                  format, has_alpha, &texel_bytes)) {
        return 0;
    }
    if (bytes != 0) {
        *bytes = texel_bytes;
    }
    return 1;
}

static int v9x_d3d_g4_texture_format(const V9X_DD_SURFACE_LCL *surface,
                                     DWORD *format_out)
{
    v9x_u16 format = 0u;
    int has_alpha = 0;

    if (format_out == 0) {
        return 0;
    }
    *format_out = 0ul;
    if (v9x_hal == 0 ||
        !v9x_d3d_g4_texture_format_of(surface, &format, &has_alpha, 0)) {
        return 0;
    }
    *format_out = format;
    return 1;
}

/* Direct3D's blend factors as genxml's 3D_Color_Buffer_Blend_Factor; 0 for
 * one this engine does not take. */
static v9x_u8 v9x_d3d_g4_factor(v9x_u32 factor)
{
    switch (factor) {
    case V9X_R3D_BLEND_ZERO:
        return 0x11u;
    case V9X_R3D_BLEND_ONE:
        return 0x01u;
    case V9X_R3D_BLEND_SRCCOLOR:
        return 0x02u;
    case V9X_R3D_BLEND_INVSRCCOLOR:
        return 0x12u;
    case V9X_R3D_BLEND_SRCALPHA:
        return 0x03u;
    case V9X_R3D_BLEND_INVSRCALPHA:
        return 0x13u;
    case V9X_R3D_BLEND_DESTCOLOR:
        return 0x05u;
    case V9X_R3D_BLEND_INVDESTCOLOR:
        return 0x15u;
    case V9X_R3D_BLEND_SRCALPHASAT:
        return 0x06u;
    default:
        return 0u;
    }
}

/* Direct3D's comparisons against genxml's 3D_Compare_Function: NEVER to
 * GEQUAL keep their numbers, ALWAYS is 0. */
static v9x_u8 v9x_d3d_g4_compare(v9x_u32 function)
{
    if (function == V9X_R3D_CMP_ALWAYS) {
        return 0u;
    }
    return (v9x_u8)(function & 7ul);
}

static v9x_u8 v9x_d3d_g4_filter(v9x_u32 filter)
{
    return filter == V9X_R3D_FILTER_LINEAR ||
                   filter == V9X_R3D_FILTER_MIPLINEAR ||
                   filter == V9X_R3D_FILTER_LINEARMIPLINEAR
               ? 1u : 0u;
}

/* SAMPLER_STATE DW0 21:20, Mip Mode Filter (965 PRM Vol 4): MIPFILTER_NONE
 * 0, NEAREST 1, LINEAR 3. The DirectX 5 names put the within-level filter
 * after MIP and the between-levels one before it, as on Haswell. */
#define V9X_D3D_G4_MIPFILTER_NONE    0u
#define V9X_D3D_G4_MIPFILTER_NEAREST 1u
#define V9X_D3D_G4_MIPFILTER_LINEAR  3u

static v9x_u8 v9x_d3d_g4_mip_filter(v9x_u32 filter)
{
    if (filter == V9X_R3D_FILTER_MIPNEAREST ||
        filter == V9X_R3D_FILTER_MIPLINEAR) {
        return V9X_D3D_G4_MIPFILTER_NEAREST;
    }
    if (filter == V9X_R3D_FILTER_LINEARMIPNEAREST ||
        filter == V9X_R3D_FILTER_LINEARMIPLINEAR) {
        return V9X_D3D_G4_MIPFILTER_LINEAR;
    }
    return V9X_D3D_G4_MIPFILTER_NONE;
}

/*
 * How many levels of a texture's chain the sampler may read: one when there
 * is no chain or its levels are not where v9x_intel_hsw_miptree puts them,
 * and each level that is, in order, otherwise.
 *
 * Haswell's layout is Gen4's: 965 PRM Vol 1 6.17.3 places LOD 1 below LOD 0
 * and LOD 2 to the right of LOD 1 (MIPLAYOUT_BELOW, SURFACE_STATE DW0 bit
 * 10 left 0), every later level below the one before, with the alignment
 * unit i = 4, j = 2 of Table 6-23 for every uncompressed format - the
 * HALIGN 4 / VALIGN 2 v9x_intel_hsw_miptree computes with. Mesa lays out
 * Gen4 to Gen7 with the one brw_miptree_layout_2d for the same reason.
 *
 * SURFACE_STATE has one base and one pitch for every level, so only a chain
 * create_surface placed as one tree is sampled past level 0. One the heap
 * placed is sampled at level 0 with no mip filter, as before, and counted
 * in the mip_gap counters. The walk is d3d_hsw.c's.
 */
static v9x_u8 v9x_d3d_g4_mip_levels(const V9X_DD_SURFACE_LCL *top)
{
    const V9X_DD_SURFACE_LCL *chain[V9X_HSW_MIP_LEVELS_MAX];
    const V9X_DD_SURFACE_LCL *level = top;
    struct v9x_hsw_miptree tree;
    DWORD width = (DWORD)top->lpGbl->wWidth;
    DWORD height = (DWORD)top->lpGbl->wHeight;
    DWORD count = 1ul;
    DWORD verified;
    v9x_u16 format;
    int has_alpha;
    v9x_u32 bytes;

    if ((top->ddsCaps & V9X_DDSCAPS_MIPMAP) == 0ul) {
        return 1u;
    }
    ++v9x_hal->d3d_diagnostics.mip_chain_checks;
    chain[0] = top;

    /* Down the attachments: the next level is the attached surface that is
     * itself a mip level, each edge half the one before. */
    while (count < V9X_HSW_MIP_LEVELS_MAX) {
        const V9X_DD_ATTACH_NODE *node =
            (const V9X_DD_ATTACH_NODE *)level->lpAttachList;
        const V9X_DD_SURFACE_LCL *next = 0;
        DWORD next_width = width >> count;
        DWORD next_height = height >> count;

        while (node != 0) {
            if (node->object != 0 && node->object != level &&
                (node->object->ddsCaps & V9X_DDSCAPS_MIPMAP) != 0ul) {
                next = node->object;
                break;
            }
            node = node->next;
        }
        if (next == 0) {
            break;
        }
        if (next_width == 0ul) {
            next_width = 1ul;
        }
        if (next_height == 0ul) {
            next_height = 1ul;
        }
        if (next->lpGbl == 0 ||
            (DWORD)next->lpGbl->wWidth != next_width ||
            (DWORD)next->lpGbl->wHeight != next_height) {
            ++v9x_hal->d3d_diagnostics.mip_gap_shape;
            break;
        }
        chain[count++] = next;
        level = next;
    }
    if (count == 1ul) {
        return 1u;
    }
    /* The tree is laid out in the texel size create_miptree placed it at. */
    if (!v9x_d3d_g4_texture_format_of(top, &format, &has_alpha, &bytes) ||
        v9x_intel_hsw_miptree((v9x_u16)width, (v9x_u16)height,
                              (v9x_u8)count, (v9x_u8)bytes, &tree) !=
            V9X_STATUS_OK) {
        ++v9x_hal->d3d_diagnostics.mip_gap_shape;
        ++v9x_hal->d3d_diagnostics.mip_chain_gaps;
        return 1u;
    }

    /* Every level at the tree's pitch and exactly at its place; the first
     * that is not ends the usable chain. A shorter chain's levels sit where
     * the longer tree put them, because each level's place depends only on
     * the levels above it. */
    verified = 1ul;
    if ((DWORD)top->lpGbl->lPitch == tree.pitch) {
        while (verified < count &&
               (DWORD)chain[verified]->lpGbl->lPitch == tree.pitch &&
               chain[verified]->lpGbl->fpVidMem ==
                   top->lpGbl->fpVidMem + tree.level_offset[verified]) {
            ++verified;
        }
    }
    if (verified < count) {
        ++v9x_hal->d3d_diagnostics.mip_chain_gaps;
        ++v9x_hal->d3d_diagnostics.mip_gap_offset;
        v9x_hal->d3d_diagnostics.mip_gap_expected =
            top->lpGbl->fpVidMem + tree.level_offset[verified];
        v9x_hal->d3d_diagnostics.mip_gap_actual =
            chain[verified]->lpGbl->fpVidMem;
    }
    v9x_hal->d3d_diagnostics.mip_chain_levels = verified - 1ul;
    if (verified - 1ul > v9x_hal->d3d_diagnostics.mip_levels_max) {
        v9x_hal->d3d_diagnostics.mip_levels_max = verified - 1ul;
    }
    return (v9x_u8)verified;
}

static float v9x_d3d_g4_unit(v9x_u32 byte)
{
    return (float)byte / 255.0f;
}

/* A texture surface as this engine samples it: linear, its levels from a
 * tree create_surface placed, or level 0 from DirectDraw's heap. */
static DWORD v9x_d3d_g4_texture_unit(const V9X_R3D_TEXTURE *texture,
                                     V9X_D3D_G4_TEXTURE *unit,
                                     int *has_alpha)
{
    V9X_DD_SURFACE_LCL *surface = (V9X_DD_SURFACE_LCL *)texture->object;
    DWORD offset;

    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul ||
        surface->lpGbl->lPitch <= 0l ||
        !v9x_d3d_g4_texture_format_of(surface, &unit->format, has_alpha,
                                      0)) {
        return V9X_D3D_G4_REFUSE_TEXTURE;
    }
    offset = v9x_surface_offset(surface);
    if (offset == 0xfffffffful) {
        return V9X_D3D_G4_REFUSE_TEXTURE;
    }
    if (texture->wrap_u != 0ul || texture->wrap_v != 0ul) {
        return V9X_D3D_G4_REFUSE_ADDRESS;
    }
    unit->levels = v9x_d3d_g4_mip_levels(surface);
    unit->sampler.mip_filter = V9X_D3D_G4_MIPFILTER_NONE;
    unit->sampler.max_lod = (v9x_u8)(unit->levels - 1u);
    if (unit->levels > 1u) {
        unit->sampler.mip_filter = v9x_d3d_g4_mip_filter(texture->min_filter);
    }
    unit->sampler.mag_filter = v9x_d3d_g4_filter(texture->mag_filter);
    unit->sampler.min_filter = v9x_d3d_g4_filter(texture->min_filter);
    switch (texture->address) {
    case V9X_R3D_ADDRESS_WRAP:
        unit->sampler.address = V9X_D3D_G4_TCM_WRAP;
        break;
    case V9X_R3D_ADDRESS_MIRROR:
        unit->sampler.address = V9X_D3D_G4_TCM_MIRROR;
        break;
    case V9X_R3D_ADDRESS_CLAMP:
        unit->sampler.address = V9X_D3D_G4_TCM_CLAMP;
        break;
    default:
        return V9X_D3D_G4_REFUSE_ADDRESS;
    }
    unit->address = v9x_d3d_g4_vram(offset);
    unit->width = surface->lpGbl->wWidth;
    unit->height = surface->lpGbl->wHeight;
    unit->pitch = (v9x_u32)surface->lpGbl->lPitch;
    if (unit->width == 0u || unit->height == 0u ||
        unit->width > v9x_d3d_g4_limits.texture_size_max ||
        unit->height > v9x_d3d_g4_limits.texture_size_max) {
        return V9X_D3D_G4_REFUSE_TEXTURE;
    }
    return V9X_D3D_G4_REFUSE_NONE;
}

/* Haswell's stage 0 (d3d_hsw_map.c's two-unit mapping) as this kernel's;
 * 0 for one it has no form of. */
static int v9x_d3d_g4_stage_of_hsw(v9x_u8 hsw, v9x_u8 *stage)
{
    switch (hsw) {
    case V9X_HSW_PS_TEXTURE:
        *stage = V9X_G4_PS_TEXTURE;
        return 1;
    case V9X_HSW_PS_DECAL:
        *stage = V9X_G4_PS_DECAL;
        return 1;
    case V9X_HSW_PS_MODULATE:
        *stage = V9X_G4_PS_MODULATE;
        return 1;
    case V9X_HSW_PS_MODULATE_TEXALPHA:
        *stage = V9X_G4_PS_MODULATE_TEXALPHA;
        return 1;
    default:
        return 0;
    }
}

/* Does an alpha test with this function and reference reject alpha 0? */
static int v9x_d3d_g4_test_rejects_zero(v9x_u32 function, v9x_u8 ref)
{
    switch (function) {
    case V9X_R3D_CMP_NEVER:
    case V9X_R3D_CMP_GREATER:
        return 1;
    case V9X_R3D_CMP_LESS:
    case V9X_R3D_CMP_NOTEQUAL:
        return ref == 0u;
    case V9X_R3D_CMP_EQUAL:
    case V9X_R3D_CMP_GREATEREQUAL:
        return ref != 0u;
    default:
        return 0;
    }
}

/*
 * Direct3D's COLORKEYENABLE on unit 0: the texture's DirectDraw source key
 * in the sampler's chroma key, KEYFILTER_REPLACE_BLACK.
 *
 * KILL_ON_ANY_MATCH would discard the pixel itself, but only through
 * sample+killpix, which exists in SIMD8 alone (965 PRM Vol 4 4.8.1.4); this
 * kernel samples SIMD16. REPLACE_BLACK samples a matching texel as
 * (0,0,0,0) "prior to filtering", and the PRM leaves the rest to "alpha
 * test, etc." (SAMPLER_STATE ChromaKey Mode), which the caller arranges.
 * So the stage must carry the texel's alpha to the output: TEXTURE,
 * MODULATE (diffuse alpha times a texel alpha of 1 or 0) and
 * MODULATE_TEXALPHA do; ADD and DECAL keep the diffuse alpha and are
 * refused. A texture with no source key draws unkeyed, as Direct3D does.
 */
static DWORD v9x_d3d_g4_color_key(const V9X_DD_SURFACE_LCL *surface,
                                  V9X_D3D_G4_STATE *state, int *keyed)
{
    const V9X_DDPIXELFORMAT *pf;

    *keyed = 0;
    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->dwFlags & V9X_DDRAWISURF_HASCKEYSRCBLT) == 0ul) {
        return V9X_D3D_G4_REFUSE_NONE;
    }
    if (state->desc.stage0 != V9X_G4_PS_TEXTURE &&
        state->desc.stage0 != V9X_G4_PS_MODULATE &&
        state->desc.stage0 != V9X_G4_PS_MODULATE_TEXALPHA) {
        return V9X_D3D_G4_REFUSE_KEY;
    }
    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul) {
        pf = &surface->lpGbl->ddpfSurface;
    } else {
        pf = &v9x_hal->info.vmiData.ddpfDisplay;
    }
    if (v9x_g4_chroma_key(surface->ddckCKSrcBltLow,
                          surface->ddckCKSrcBltHigh, pf->dwRBitMask,
                          pf->dwGBitMask, pf->dwBBitMask,
                          &state->chroma_low, &state->chroma_high) !=
        V9X_STATUS_OK) {
        return V9X_D3D_G4_REFUSE_KEY;
    }
    state->texture.sampler.chroma_key = 1u;
    *keyed = 1;
    return V9X_D3D_G4_REFUSE_NONE;
}

/*
 * The depth buffer format for a cell: D16_UNORM, or for a 32-bit cell
 * D24_UNORM_S8_UINT with the stencil test off, the form Mesa's Gen4 path
 * gives 24-bit depth. D24_UNORM_X8_UINT, tiled, cleared only 63% of
 * V9XD32's Z buffer on TOMMOHP and took no depth writes at all (boot 31,
 * ZClearFar32 0x2F70C of 0x4B000, ZDepth counts unchanged), where D16 in
 * the same placement covered every cell.
 */
static int v9x_d3d_g4_depth_format(DWORD cell, v9x_u8 *format)
{
    if (cell == 2ul) {
        *format = (v9x_u8)V9X_G4_DEPTH_D16_UNORM;
        return 1;
    }
    if (cell == 4ul) {
        *format = (v9x_u8)V9X_G4_DEPTH_D24_UNORM_S8;
        return 1;
    }
    return 0;
}

/*
 * A Z buffer create_surface placed in Y tiles. One from DirectDraw's heap
 * (create_depth declined it) is linear, and the depth unit would read it
 * as tiled, so it is refused. Placed is d3d_place.c's own signature, the
 * one v9x_d3d_place_release frees by: the block's start in dwReserved1,
 * no heap block size, and the surface inside the block's first alignment.
 */
static int v9x_d3d_g4_depth_surface(const V9X_DD_SURFACE_LCL *surface,
                                    DWORD *offset)
{
    const V9X_DD_SURFACE_GBL *global;

    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_ZBUFFER) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul ||
        surface->lpGbl->lPitch <= 0l ||
        ((DWORD)surface->lpGbl->lPitch &
         (V9X_D3D_G4_DEPTH_PITCH_ALIGN - 1ul)) != 0ul) {
        return 0;
    }
    global = surface->lpGbl;
    if (global->dwReserved1 == 0ul || global->dwBlockSizeX != 0ul ||
        global->fpVidMem < global->dwReserved1 ||
        global->fpVidMem - global->dwReserved1 >= V9X_D3D_G4_PLACE_ALIGN) {
        return 0;
    }
    *offset = v9x_surface_offset(surface);
    return *offset != 0xfffffffful;
}

static DWORD v9x_d3d_g4_map(const V9X_R3D_DRAW *draw, V9X_D3D_G4_STATE *state)
{
    V9X_DD_SURFACE_LCL *surface;
    DWORD offset;
    DWORD reason;
    int has_alpha = 0;
    int has_alpha1 = 0;
    int keyed = 0;
    v9x_u8 hsw_stage0 = 0u;
    v9x_u8 hsw_stage1 = 0u;
    v9x_u32 op;

    reason = v9x_d3d_hsw_explicit_refusal(draw);
    if (reason != V9X_D3D_G4_REFUSE_NONE) {
        return reason;
    }
    state->desc.stage0 = V9X_G4_PS_DIFFUSE;
    state->desc.stage1 = V9X_G4_PS_STAGE1_NONE;
    state->texture.sampler.chroma_key = 0u;
    state->texture1.sampler.chroma_key = 0u;
    state->chroma_low = 0ul;
    state->chroma_high = 0ul;
    state->desc.specular = 0u;
    state->desc.fog = 0u;
    state->desc.fog_rgb[0] = 0.0f;
    state->desc.fog_rgb[1] = 0.0f;
    state->desc.fog_rgb[2] = 0.0f;
    state->textured = 0;
    state->depth = 0;
    state->depth_address = 0ul;
    state->depth_pitch = 0ul;
    state->depth_format = 0u;
    state->write_disable = 0u;

    if (!v9x_d3d_hsw_target(draw->target.format, &state->target_format,
                            &state->write_disable)) {
        return V9X_D3D_G4_REFUSE_TARGET;
    }
    if (draw->target.pitch == 0ul || draw->target.width == 0ul ||
        draw->target.height == 0ul ||
        draw->target.width > v9x_d3d_g4_limits.target_dimension_max ||
        draw->target.height > v9x_d3d_g4_limits.target_dimension_max) {
        return V9X_D3D_G4_REFUSE_TARGET;
    }

    state->cc.depth_test = 0u;
    state->cc.depth_write = 0u;
    state->cc.depth_function = 0u;
    if (v9x_d3d_state_depth_active(draw->depth_enable,
                                   draw->depth.object != 0,
                                   draw->depth.pitch) != 0ul) {
        surface = (V9X_DD_SURFACE_LCL *)draw->depth.object;
        /* D16_UNORM, or D24_UNORM_X8 for a 32-bit cell, both Y-tiled. */
        if (!v9x_d3d_g4_depth_surface(surface, &offset) ||
            (DWORD)surface->lpGbl->wWidth < draw->target.width ||
            (DWORD)surface->lpGbl->wHeight < draw->target.height ||
            draw->depth_func < V9X_R3D_CMP_NEVER ||
            draw->depth_func > V9X_R3D_CMP_ALWAYS ||
            !v9x_d3d_g4_depth_format(
                draw->depth.format == V9X_R3D_DEPTH_Z24X8 ? 4ul : 2ul,
                &state->depth_format)) {
            return V9X_D3D_G4_REFUSE_DEPTH;
        }
        state->depth = 1;
        state->depth_address = v9x_d3d_g4_vram(offset);
        state->depth_pitch = (v9x_u32)surface->lpGbl->lPitch;
        state->cc.depth_test = 1u;
        state->cc.depth_function = v9x_d3d_g4_compare(draw->depth_func);
        state->cc.depth_write = draw->depth_write != 0ul ? 1u : 0u;
    }

    if (draw->texcoords1 != 0) {
        /* Two units (render interface step 4): each unit's combine is in
         * its color_op and alpha_op, `op` is not set (r3d.h). Haswell's
         * mapping, onto this kernel's stages. */
        if (draw->color_key_enable != 0ul) {
            return V9X_D3D_G4_REFUSE_KEY;
        }
        reason = v9x_d3d_g4_texture_unit(&draw->texture, &state->texture,
                                         &has_alpha);
        if (reason != V9X_D3D_G4_REFUSE_NONE) {
            return reason;
        }
        reason = v9x_d3d_g4_texture_unit(&draw->texture1, &state->texture1,
                                         &has_alpha1);
        if (reason != V9X_D3D_G4_REFUSE_NONE) {
            return reason;
        }
        if (v9x_d3d_hsw_two_unit_stages(
                draw->texture.color_op, draw->texture.alpha_op, has_alpha,
                draw->texture1.color_op, draw->texture1.alpha_op, has_alpha1,
                &hsw_stage0, &hsw_stage1) != 0ul ||
            !v9x_d3d_g4_stage_of_hsw(hsw_stage0, &state->desc.stage0)) {
            return V9X_D3D_G4_REFUSE_TEXOP;
        }
        state->desc.stage1 = V9X_G4_PS_MODULATE_TEX1;
        state->textured = 2;
    } else if (draw->texture.object != 0) {
        reason = v9x_d3d_g4_texture_unit(&draw->texture, &state->texture,
                                         &has_alpha);
        if (reason != V9X_D3D_G4_REFUSE_NONE) {
            return reason;
        }
        op = draw->texture.op;
        if (op == V9X_R3D_TEXOP_DECAL || op == V9X_R3D_TEXOP_COPY) {
            state->desc.stage0 = V9X_G4_PS_TEXTURE;
        } else if (op == V9X_R3D_TEXOP_MODULATE) {
            state->desc.stage0 = has_alpha ? V9X_G4_PS_MODULATE_TEXALPHA
                                           : V9X_G4_PS_MODULATE;
        } else if (op == V9X_R3D_TEXOP_MODULATEALPHA) {
            state->desc.stage0 = V9X_G4_PS_MODULATE;
        } else if (op == V9X_R3D_TEXOP_ADD && !has_alpha) {
            state->desc.stage0 = V9X_G4_PS_ADD;
        } else {
            return V9X_D3D_G4_REFUSE_TEXOP;
        }
        state->textured = 1;
        if (draw->color_key_enable != 0ul) {
            reason = v9x_d3d_g4_color_key(
                (const V9X_DD_SURFACE_LCL *)draw->texture.object, state,
                &keyed);
            if (reason != V9X_D3D_G4_REFUSE_NONE) {
                return reason;
            }
        }
    }

    state->desc.specular = draw->specular_enable != 0ul ? 1u : 0u;
    if (draw->fog_enable != 0ul) {
        state->desc.fog = 1u;
        state->desc.fog_rgb[0] = v9x_d3d_g4_unit((draw->fog_color >> 16) &
                                                 0xfful);
        state->desc.fog_rgb[1] = v9x_d3d_g4_unit((draw->fog_color >> 8) &
                                                 0xfful);
        state->desc.fog_rgb[2] = v9x_d3d_g4_unit(draw->fog_color & 0xfful);
    }

    if (draw->explicit_state != 0ul) {
        state->write_disable |= v9x_d3d_hsw_write_disable(draw->write_mask);
    }
    state->scissor_empty = !v9x_d3d_hsw_scissor(draw, state->scissor);
    state->cc.blend = 0u;
    state->cc.src_factor = 0x01u;
    state->cc.dst_factor = 0x11u;
    if (draw->blend_enable != 0ul) {
        state->cc.src_factor = v9x_d3d_g4_factor(draw->src_blend);
        state->cc.dst_factor = v9x_d3d_g4_factor(draw->dst_blend);
        if (state->cc.src_factor == 0u || state->cc.dst_factor == 0u) {
            return V9X_D3D_G4_REFUSE_BLEND;
        }
        state->cc.blend = 1u;
    }
    state->cc.alpha_test = 0u;
    state->cc.alpha_function = 0u;
    state->cc.alpha_ref = 0u;
    if (draw->alpha_test_enable != 0ul) {
        if (draw->alpha_func < V9X_R3D_CMP_NEVER ||
            draw->alpha_func > V9X_R3D_CMP_ALWAYS) {
            return V9X_D3D_G4_REFUSE_ALPHA;
        }
        state->cc.alpha_test = 1u;
        state->cc.alpha_function = v9x_d3d_g4_compare(draw->alpha_func);
        state->cc.alpha_ref = (v9x_u8)(draw->alpha_ref & 0xfful);
    }
    /* A keyed texel samples with alpha 0, and the alpha test is what
     * discards it: GREATER 0 of our own, or the application's test when
     * that already rejects 0. One the application set that passes 0 would
     * have to be replaced, which would change what it asked for. */
    if (keyed) {
        if (state->cc.alpha_test == 0u) {
            state->cc.alpha_test = 1u;
            state->cc.alpha_function =
                v9x_d3d_g4_compare(V9X_R3D_CMP_GREATER);
            state->cc.alpha_ref = 0u;
        } else if (!v9x_d3d_g4_test_rejects_zero(draw->alpha_func,
                                                 state->cc.alpha_ref)) {
            return V9X_D3D_G4_REFUSE_KEY;
        }
    }
    /*
     * The alpha test with depth writes. A pixel the alpha test kills must
     * not write depth, so the depth write moves after the pixel shader: 965
     * PRM Vol 2 8.4.3 ("Pixel Shader Kill Pixel" must be set for "alphatest"
     * or a chroma-keyed texture, and the non-promoted and computed cases
     * carry source depth to the render target). This kernel sends no
     * source depth, and TOMMOHP boot 16 hung the 3D pipe on the first GL
     * draw with the alpha test, a depth test and depth writes (V9XGLP;
     * BreadcrumbAbandoned=1). Until the kernel carries depth, such a draw
     * is refused and drawn by the fallback. Without depth writes the
     * depth test stays early and the result is the same.
     */
    if (state->cc.alpha_test != 0u && state->cc.depth_write != 0u) {
        return keyed ? V9X_D3D_G4_REFUSE_KEY : V9X_D3D_G4_REFUSE_ALPHA;
    }
    return V9X_D3D_G4_REFUSE_NONE;
}

static v9x_u32 v9x_d3d_g4_float_bits(float value)
{
    union {
        float f;
        v9x_u32 u;
    } convert;

    convert.f = value;
    return convert.u;
}

/* The WM slot a description takes, and the words that say a slot holds
 * it (the fog colour is in the words, not the slot). */
static DWORD v9x_d3d_g4_wm_slot(const struct v9x_g4_ps_desc *desc)
{
    return (DWORD)desc->stage1 * V9X_G4_WM_KERNEL_SLOTS +
           (DWORD)desc->stage0 * 4ul + (DWORD)desc->specular * 2ul +
           (DWORD)desc->fog;
}

/* A slot's kernel: the one-unit slots, then the two-unit ones. */
static DWORD v9x_d3d_g4_wm_offset(DWORD slot)
{
    if (slot < V9X_G4_WM_KERNEL_SLOTS) {
        return V9X_G4_WM_KERNEL_OFF + slot * V9X_G4_WM_KERNEL_BYTES;
    }
    return V9X_G4_WM_KERNEL2_OFF +
           (slot - V9X_G4_WM_KERNEL_SLOTS) * V9X_G4_WM_KERNEL_BYTES;
}

static void v9x_d3d_g4_wm_sig(const struct v9x_g4_ps_desc *desc,
                              v9x_u32 *sig)
{
    sig[0] = 0x47300000ul | ((v9x_u32)desc->stage1 << 16) |
             (V9X_D3D_G4_KERNEL_REV << 12) | ((v9x_u32)desc->stage0 << 8) |
             ((v9x_u32)desc->specular << 4) | (v9x_u32)desc->fog;
    sig[1] = v9x_d3d_g4_float_bits(desc->fog_rgb[0]);
    sig[2] = v9x_d3d_g4_float_bits(desc->fog_rgb[1]);
    sig[3] = v9x_d3d_g4_float_bits(desc->fog_rgb[2]);
}

/* Copy a built kernel into the reserve. */
static void v9x_d3d_g4_copy_kernel(DWORD offset, v9x_u32 count)
{
    v9x_u32 *target = v9x_d3d_g4_reserve(offset);
    v9x_u32 index;

    for (index = 0ul; index < count; ++index) {
        target[index * 4ul + 0ul] = v9x_d3d_g4_kernel_words[index].dw[0];
        target[index * 4ul + 1ul] = v9x_d3d_g4_kernel_words[index].dw[1];
        target[index * 4ul + 2ul] = v9x_d3d_g4_kernel_words[index].dw[2];
        target[index * 4ul + 3ul] = v9x_d3d_g4_kernel_words[index].dw[3];
    }
}

/*
 * Every earlier batch finished, before a kernel either slot's batch may be
 * running is overwritten. The ring is in order, so the newest breadcrumb
 * covers them all.
 */
static int v9x_d3d_g4_drain_all(void)
{
    return v9x_d3d_i9xx_render_drain(1) == V9X_RENDER_DRAIN_DONE;
}

/* The SF kernel, once per kernel revision: its mark is in the signature
 * area, which survives a warm reset with the rest of the reserve. */
static int v9x_d3d_g4_write_sf(v9x_u8 *grf_used)
{
    volatile DWORD *sig = (volatile DWORD *)v9x_d3d_g4_reserve(V9X_G4_SIG_OFF);
    struct v9x_g4_program program;

    v9x_g4_program_init(&program, v9x_d3d_g4_kernel_words,
                        V9X_D3D_G4_KERNEL_INSTS);
    program.gen = v9x_d3d_g4_gen();
    if (v9x_g4_sf_kernel(&program, V9X_G4_SETUP_REGS, grf_used) !=
        V9X_STATUS_OK) {
        return 0;
    }
    if (sig[0] != V9X_D3D_G4_SF_MARK) {
        if (!v9x_d3d_g4_drain_all()) {
            return 0;
        }
        v9x_d3d_g4_copy_kernel(V9X_G4_SF_KERNEL_OFF, program.count);
        sig[0] = V9X_D3D_G4_SF_MARK;
    }
    return 1;
}

static int v9x_d3d_g4_write_wm(const struct v9x_g4_ps_desc *desc,
                               DWORD *kernel, v9x_u8 *grf_used)
{
    volatile DWORD *stored;
    struct v9x_g4_program program;
    v9x_u32 wanted[V9X_D3D_G4_SIG_WORDS];
    DWORD slot = v9x_d3d_g4_wm_slot(desc);
    DWORD index;
    int same = 1;

    if (slot >= 2ul * V9X_G4_WM_KERNEL_SLOTS) {
        return 0;
    }
    *kernel = v9x_d3d_g4_wm_offset(slot);
    v9x_g4_program_init(&program, v9x_d3d_g4_kernel_words,
                        V9X_D3D_G4_KERNEL_INSTS);
    program.gen = v9x_d3d_g4_gen();
    if (v9x_g4_wm_kernel(&program, desc, grf_used) != V9X_STATUS_OK) {
        return 0;
    }
    stored = (volatile DWORD *)v9x_d3d_g4_reserve(V9X_G4_SIG_OFF) +
             V9X_D3D_G4_SIG_WORDS * (slot + 1ul);
    v9x_d3d_g4_wm_sig(desc, wanted);
    for (index = 0ul; index < V9X_D3D_G4_SIG_WORDS; ++index) {
        if (stored[index] != wanted[index]) {
            same = 0;
        }
    }
    if (!same) {
        if (!v9x_d3d_g4_drain_all()) {
            return 0;
        }
        v9x_d3d_g4_copy_kernel(*kernel, program.count);
        for (index = 0ul; index < V9X_D3D_G4_SIG_WORDS; ++index) {
            stored[index] = wanted[index];
        }
    }
    return 1;
}

/* The draw's state block. Zero, or the reason a block would not encode. */
static DWORD v9x_d3d_g4_write_state(const V9X_R3D_DRAW *draw,
                                    const V9X_D3D_G4_STATE *state, DWORD st)
{
    v9x_u32 *image = v9x_d3d_g4_reserve(st);
    v9x_u32 *table;
    DWORD wm_kernel = 0ul;
    DWORD index;
    v9x_u8 sf_grf = 0u;
    v9x_u8 wm_grf = 0u;

    if (!v9x_d3d_g4_write_sf(&sf_grf) ||
        !v9x_d3d_g4_write_wm(&state->desc, &wm_kernel, &wm_grf)) {
        return V9X_D3D_G4_REFUSE_STATE;
    }
    v9x_g4_sf_viewport(image + V9X_G4_ST_SF_VP / 4ul);
    /* The scissor in SF_VIEWPORT's rectangle (dwords 6 and 7). */
    image[V9X_G4_ST_SF_VP / 4ul + 6ul] = state->scissor[0] |
                                         (state->scissor[1] << 16);
    image[V9X_G4_ST_SF_VP / 4ul + 7ul] = state->scissor[2] |
                                         (state->scissor[3] << 16);
    v9x_g4_cc_viewport(image + V9X_G4_ST_CC_VP / 4ul);
    for (index = 0ul; index < 16ul; ++index) {
        image[V9X_G4_ST_BORDER / 4ul + index] = 0ul;
    }
    if (v9x_g4_vs_state(image + V9X_G4_ST_VS / 4ul, v9x_d3d_g4_gen()) != V9X_STATUS_OK ||
        v9x_g4_sf_state(image + V9X_G4_ST_SF / 4ul, V9X_G4_SF_KERNEL_OFF,
                        sf_grf, st + V9X_G4_ST_SF_VP) != V9X_STATUS_OK ||
        v9x_g4_wm_state(image + V9X_G4_ST_WM / 4ul, wm_kernel, wm_grf,
                        st + V9X_G4_ST_SAMPLER, (v9x_u8)state->textured,
                        3u, v9x_d3d_g4_gen()) !=
            V9X_STATUS_OK ||
        v9x_g4_cc_state(image + V9X_G4_ST_CC / 4ul, &state->cc,
                        st + V9X_G4_ST_CC_VP, 0u) != V9X_STATUS_OK) {
        return V9X_D3D_G4_REFUSE_STATE;
    }
    /* Scissor enable, SF_STATE DW6 bit 17. */
    image[V9X_G4_ST_SF / 4ul + 6ul] |= 1ul << 17;

    if (v9x_g4_surface(image + V9X_G4_ST_RT_SURF / 4ul,
                       state->target_format,
                       v9x_d3d_g4_vram(draw->target.offset),
                       (v9x_u16)draw->target.width,
                       (v9x_u16)draw->target.height, draw->target.pitch, 1u,
                       state->write_disable, 1u) != V9X_STATUS_OK) {
        return V9X_D3D_G4_REFUSE_TARGET;
    }
    /* Entries the kernel does not sample name the target, as entry 1 did
     * before textures: every entry the WM counts is a valid surface. */
    table = image + V9X_G4_ST_BINDING / 4ul;
    table[0] = st + V9X_G4_ST_RT_SURF;
    table[1] = st + V9X_G4_ST_RT_SURF;
    table[2] = st + V9X_G4_ST_RT_SURF;
    if (state->textured == 2) {
        if (v9x_g4_surface(image + V9X_G4_ST_TEX1_SURF / 4ul,
                           state->texture1.format, state->texture1.address,
                           state->texture1.width, state->texture1.height,
                           state->texture1.pitch, 0u, 0u,
                           state->texture1.levels) != V9X_STATUS_OK ||
            v9x_g4_sampler_state(image + (V9X_G4_ST_SAMPLER +
                                          V9X_G4_SAMPLER_BYTES) / 4ul,
                                 &state->texture1.sampler,
                                 st + V9X_G4_ST_BORDER) != V9X_STATUS_OK) {
            return V9X_D3D_G4_REFUSE_TEXTURE;
        }
        table[2] = st + V9X_G4_ST_TEX1_SURF;
    }
    if (state->textured != 0) {
        if (v9x_g4_surface(image + V9X_G4_ST_TEX_SURF / 4ul,
                           state->texture.format, state->texture.address,
                           state->texture.width, state->texture.height,
                           state->texture.pitch, 0u, 0u,
                           state->texture.levels) !=
                V9X_STATUS_OK ||
            v9x_g4_sampler_state(image + V9X_G4_ST_SAMPLER / 4ul,
                                 &state->texture.sampler,
                                 st + V9X_G4_ST_BORDER) != V9X_STATUS_OK) {
            return V9X_D3D_G4_REFUSE_TEXTURE;
        }
        table[1] = st + V9X_G4_ST_TEX_SURF;
    }
    return V9X_D3D_G4_REFUSE_NONE;
}

/* One TL vertex into the buffer's layout: x y z, diffuse, specular, then
 * u, v and 1 multiplied by 1/w for the WM's perspective division, then the
 * second unit's u1 and v1 by the same 1/w (uv1 null: zero). */
static v9x_u32 *v9x_d3d_g4_vertex(v9x_u32 *at, const V9X_R3D_VERTEX *v,
                                  const float *uv1)
{
    float rhw = v->rhw > 0.0f ? v->rhw : 1.0f;

    at[0] = v9x_d3d_g4_float_bits(v->sx);
    at[1] = v9x_d3d_g4_float_bits(v->sy);
    at[2] = v9x_d3d_g4_float_bits(v->sz);
    at[3] = v->color;
    at[4] = v->specular;
    at[5] = v9x_d3d_g4_float_bits(v->tu * rhw);
    at[6] = v9x_d3d_g4_float_bits(v->tv * rhw);
    at[7] = v9x_d3d_g4_float_bits(rhw);
    at[8] = 0ul;
    at[9] = 0ul;
    if (uv1 != 0) {
        at[8] = v9x_d3d_g4_float_bits(uv1[0] * rhw);
        at[9] = v9x_d3d_g4_float_bits(uv1[1] * rhw);
    }
    at[10] = 0ul;
    return at + V9X_G4_VB_PITCH / 4ul;
}

/* The pipeline's shared words in the reserve (intel_g4_3d.h). */
static volatile DWORD *v9x_d3d_g4_pipe(void)
{
    return (volatile DWORD *)v9x_d3d_g4_reserve(V9X_G4_PIPE_OFF);
}

/* The Gen3 status page's breadcrumb dword, as d3d_i9xx.c reads it. */
static volatile DWORD *v9x_d3d_g4_crumb(void)
{
    return (volatile DWORD *)(v9x_hal->engine.ring_linear_base +
                              V9X_I9XX_RING_BYTES +
                              V9X_I9XX_HWS_BREADCRUMB_BYTE);
}

/*
 * The next slot, free: the batch last submitted from it has stored its
 * breadcrumb. A sequence not seen within the bound - a stale word the
 * reserve kept across a warm reset, or a slow batch - falls back to a full
 * drain, after which nothing can be using either slot.
 */
static int v9x_d3d_g4_take_slot(DWORD *slot)
{
    volatile DWORD *pipe = v9x_d3d_g4_pipe();
    volatile DWORD *crumb = v9x_d3d_g4_crumb();
    DWORD next = (pipe[V9X_G4_PIPE_LAST_SLOT] + 1ul) % V9X_G4_SLOTS;
    DWORD sequence = pipe[V9X_G4_PIPE_SLOT_SEQ + next];
    DWORD polls;
    DWORD started = V9X_TIME_BEGIN();

    *slot = next;
    ++v9x_hal->d3d_diagnostics.render_drain_waits;
    if (sequence == 0ul) {
        return 1;
    }
    for (polls = 0ul; polls < V9X_D3D_G4_SLOT_POLLS; ++polls) {
        if (v9x_i9xx_sequence_reached(*crumb, sequence) != V9X_FALSE) {
            if (polls != 0ul) {
                ++v9x_hal->d3d_diagnostics.render_drain_stalls;
                v9x_hal->d3d_diagnostics.breadcrumb_lag_polls_total += polls;
                if (polls > v9x_hal->d3d_diagnostics.breadcrumb_lag_polls_max) {
                    v9x_hal->d3d_diagnostics.breadcrumb_lag_polls_max = polls;
                }
            }
            V9X_TIME_END(V9X_TIME_RENDER_DRAIN, started);
            return 1;
        }
    }
    V9X_TIME_END(V9X_TIME_RENDER_DRAIN, started);
    return v9x_d3d_g4_drain_all();
}

static void v9x_d3d_g4_note_slot(DWORD slot, DWORD sequence)
{
    volatile DWORD *pipe = v9x_d3d_g4_pipe();

    pipe[V9X_G4_PIPE_SLOT_SEQ + slot] = sequence;
    pipe[V9X_G4_PIPE_LAST_SLOT] = slot;
}

/*
 * Submit a mapped draw: the state block, the vertices and the batch, a
 * slot at a time. The draw's target sizes the drawing rectangle.
 */
static int v9x_d3d_g4_submit(const V9X_R3D_DRAW *draw,
                             const V9X_D3D_G4_STATE *state,
                             const V9X_R3D_VERTEX *vertices,
                             DWORD triangle_count)
{
    struct v9x_g4_batch batch;
    struct v9x_g4_draw d;
    DWORD reason;
    DWORD done = 0ul;
    DWORD count;
    DWORD vertex;
    DWORD index;
    DWORD slot = 0ul;
    DWORD batch_off;
    DWORD sequence = 0ul;
    int wait_after;
    v9x_u32 *at;
    v9x_u32 *target;

    {
        int waited = v9x_flip_wait_done();

        if (waited != V9X_FLIP_WAIT_NONE) {
            ++v9x_hal->d3d_diagnostics.draws_flip_waited;
            if (waited == V9X_FLIP_WAIT_TIMEOUT) {
                ++v9x_hal->d3d_diagnostics.draws_flip_wait_timeouts;
            }
        }
    }

    d.state_base = V9X_D3D_G4_GFX(0ul);
    d.width = (v9x_u16)draw->target.width;
    d.height = (v9x_u16)draw->target.height;
    d.depth_address = state->depth ? state->depth_address : 0ul;
    d.depth_pitch = state->depth_pitch;
    d.depth_format = state->depth_format;
    d.depth_tiled = 1u;   /* every Z buffer this engine takes is Y-tiled */
    d.chroma_key = state->texture.sampler.chroma_key;
    d.chroma_low = state->chroma_low;
    d.chroma_high = state->chroma_high;
    d.gen = v9x_d3d_g4_gen();
    /* Left running, unless GDI may be drawing on the screen (Haswell's
     * rule, d3d_hsw.c v9x_d3d_hsw_draw_waits). */
    wait_after = v9x_present_gdi_may_draw();

    while (done < triangle_count) {
        count = triangle_count - done;
        if (count > V9X_D3D_G4_MAX_TRIANGLES) {
            count = V9X_D3D_G4_MAX_TRIANGLES;
        }
        /* The slot after the last one, once the batch that used it last
         * is done; the other slot's batch may still be running. */
        if (!v9x_d3d_g4_take_slot(&slot)) {
            v9x_d3d_g4_count_refusal(done == 0ul ? V9X_D3D_G4_REFUSE_IDLE
                                                 : V9X_D3D_G4_REFUSE_SUBMIT);
            return 0;
        }
        d.state_offset = V9X_G4_STATE_OFF + slot * V9X_G4_STATE_STRIDE;
        d.vertex_address = V9X_D3D_G4_GFX(V9X_G4_VERTEX_OFF +
                                          slot * V9X_G4_VERTEX_STRIDE);
        reason = v9x_d3d_g4_write_state(draw, state, d.state_offset);
        if (reason != V9X_D3D_G4_REFUSE_NONE) {
            v9x_d3d_g4_count_refusal(reason);
            return 0;
        }
        at = v9x_d3d_g4_reserve(V9X_G4_VERTEX_OFF +
                                slot * V9X_G4_VERTEX_STRIDE);
        for (vertex = 0ul; vertex < count * 3ul; ++vertex) {
            /* texcoords1 is in the vertices' order, two floats each. */
            at = v9x_d3d_g4_vertex(at, &vertices[done * 3ul + vertex],
                                   draw->texcoords1 != 0
                                       ? draw->texcoords1 +
                                             (done * 3ul + vertex) * 2ul
                                       : 0);
        }
        d.vertex_count = count * 3ul;

        v9x_g4_batch_init(&batch, v9x_d3d_g4_batch_words,
                          V9X_G4_BATCH_BYTES / 4ul);
        if (v9x_g4_draw_batch(&batch, &d) != V9X_STATUS_OK ||
            v9x_g4_batch_check(batch.dw, batch.count, d.gen) != V9X_TRUE) {
            v9x_d3d_g4_count_refusal(V9X_D3D_G4_REFUSE_BATCH);
            return 0;
        }
        batch_off = V9X_G4_BATCH_OFF + slot * V9X_G4_BATCH_STRIDE;
        target = v9x_d3d_g4_reserve(batch_off);
        for (index = 0ul; index < batch.count; ++index) {
            target[index] = batch.dw[index];
        }
        if (!v9x_d3d_i9xx_submit_g4_batch(V9X_D3D_G4_GFX(batch_off),
                                          wait_after, &sequence)) {
            /* The batch may be running: nothing is submitted again. */
            v9x_d3d_g4_quarantined = 1;
            ++v9x_hal->engine.idle_timeouts;
            v9x_d3d_g4_count_refusal(V9X_D3D_G4_REFUSE_SUBMIT);
            return 0;
        }
        v9x_d3d_g4_note_slot(slot, sequence);
        if (d.chroma_key != 0u) {
            ++v9x_hal->d3d_diagnostics.color_key_draws;
        }
        done += count;
    }
    v9x_present_note_submission();
    return 1;
}

static int v9x_d3d_g4_draw_batch(const V9X_R3D_DRAW *draw,
                                 const V9X_R3D_VERTEX *vertices,
                                 DWORD triangle_count)
{
    V9X_D3D_G4_STATE state;
    DWORD reason;

    if (draw == 0 || vertices == 0 || triangle_count == 0ul) {
        return 0;
    }
    if (!v9x_d3d_g4_ready()) {
        if (v9x_hal != 0) {
            v9x_d3d_g4_count_refusal(V9X_D3D_G4_REFUSE_NOT_READY);
        }
        return 0;
    }
    reason = v9x_d3d_g4_map(draw, &state);
    if (reason != V9X_D3D_G4_REFUSE_NONE) {
        v9x_d3d_g4_count_refusal(reason);
        return 0;
    }
    if (state.scissor_empty) {
        return 1;
    }
    return v9x_d3d_g4_submit(draw, &state, vertices, triangle_count);
}

/*
 * A depth fill drawn by the engine: a Y-tiled Z buffer is not the linear
 * rectangle the blitter or the CPU would write. Two triangles over the
 * rectangle at depth z, the depth test ALWAYS with writes on, and the
 * colour target - the Z buffer's own memory as RGB565, which only sizes the
 * draw - with every channel's write disabled. Haswell's fill the same way
 * (d3d_hsw.c v9x_d3d_hsw_depth_fill).
 */
int v9x_d3d_g4_depth_fill(V9X_DD_SURFACE_LCL *surface, const LONG *rect,
                          DWORD value, int wait)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_G4_STATE state;
    V9X_R3D_VERTEX v[6];
    BYTE *bytes;
    DWORD offset = 0ul;
    DWORD cell;
    DWORD index;
    float z;
    float left;
    float top;
    float right;
    float bottom;

    (void)wait;
    if (surface == 0 || rect == 0 || !v9x_d3d_g4_ready() ||
        !v9x_d3d_g4_depth_surface(surface, &offset)) {
        return V9X_BLT_DECLINED;
    }
    if (rect[0] < 0l || rect[1] < 0l || rect[2] <= rect[0] ||
        rect[3] <= rect[1] ||
        (DWORD)rect[2] > (DWORD)surface->lpGbl->wWidth ||
        (DWORD)rect[3] > (DWORD)surface->lpGbl->wHeight) {
        return V9X_BLT_DECLINED;
    }

    bytes = (BYTE *)&draw;
    for (index = 0ul; index < sizeof(draw); ++index) {
        bytes[index] = 0u;
    }
    bytes = (BYTE *)&state;
    for (index = 0ul; index < sizeof(state); ++index) {
        bytes[index] = 0u;
    }
    draw.target.offset = offset;
    draw.target.pitch = (DWORD)surface->lpGbl->lPitch;
    draw.target.width = (DWORD)surface->lpGbl->wWidth;
    draw.target.height = (DWORD)surface->lpGbl->wHeight;
    draw.target.format = V9X_R3D_FORMAT_RGB565;

    /* The cell sets the format, D16_UNORM or D24_UNORM_X8, and the scale
     * the value is on (v9x_d3d_hsw_fill_z). */
    cell = v9x_d3d_depth_bytes_per_pixel(surface);
    if (!v9x_d3d_g4_depth_format(cell, &state.depth_format) ||
        !v9x_d3d_hsw_target(V9X_R3D_FORMAT_RGB565, &state.target_format,
                            &state.write_disable)) {
        return V9X_BLT_DECLINED;
    }
    state.write_disable = 0xfu;
    state.depth = 1;
    state.depth_address = v9x_d3d_g4_vram(offset);
    state.depth_pitch = (v9x_u32)surface->lpGbl->lPitch;
    state.cc.depth_test = 1u;
    state.cc.depth_function = v9x_d3d_g4_compare(V9X_R3D_CMP_ALWAYS);
    state.cc.depth_write = 1u;
    state.cc.src_factor = 0x01u;
    state.cc.dst_factor = 0x11u;
    (void)v9x_d3d_hsw_scissor(&draw, state.scissor);

    z = v9x_d3d_hsw_fill_z(value, cell);
    left = (float)rect[0];
    top = (float)rect[1];
    right = (float)rect[2];
    bottom = (float)rect[3];
    for (index = 0ul; index < 6ul; ++index) {
        v[index].sz = z;
        v[index].rhw = 1.0f;
        v[index].color = 0ul;
        v[index].specular = 0ul;
        v[index].tu = 0.0f;
        v[index].tv = 0.0f;
    }
    v[0].sx = left;
    v[0].sy = top;
    v[1].sx = right;
    v[1].sy = top;
    v[2].sx = left;
    v[2].sy = bottom;
    v[3].sx = right;
    v[3].sy = top;
    v[4].sx = right;
    v[4].sy = bottom;
    v[5].sx = left;
    v[5].sy = bottom;
    return v9x_d3d_g4_submit(&draw, &state, v, 2ul) ? V9X_BLT_DONE
                                                     : V9X_BLT_DECLINED;
}

static int v9x_d3d_g4_draw(const V9X_R3D_DRAW *draw,
                           const V9X_R3D_VERTEX *vertices,
                           DWORD triangle_count)
{
    DWORD started = V9X_TIME_BEGIN();
    int result = v9x_d3d_g4_draw_batch(draw, vertices, triangle_count);

    V9X_TIME_END(V9X_TIME_ENGINE_DRAW, started);
    return result;
}

static int v9x_d3d_g4_accepts(const V9X_R3D_DRAW *draw)
{
    V9X_D3D_G4_STATE state;

    if (draw == 0 || v9x_hal == 0) {
        return 0;
    }
    return v9x_d3d_g4_map(draw, &state) == V9X_D3D_G4_REFUSE_NONE;
}

/* What the mapping takes: Haswell's, minus the tiled depth placement, plus
 * the source colour key (v9x_d3d_g4_color_key). */
static void v9x_d3d_g4_describe_caps(V9X_DD_SHARED *shared)
{
    V9X_D3DPRIMCAPS *tri;
    DWORD index;

    if (shared == 0) {
        return;
    }
    shared->d3d_global.dwSize = sizeof(V9X_D3DHAL_GLOBALDRIVERDATA);
    shared->d3d_global.hwCaps.dwSize = sizeof(V9X_D3DDEVICEDESC_V1);
    shared->d3d_global.hwCaps.dwFlags =
        V9X_D3DDD_COLORMODEL | V9X_D3DDD_DEVCAPS |
        V9X_D3DDD_TRICAPS | V9X_D3DDD_DEVICERENDERBITDEPTH |
        V9X_D3DDD_DEVICEZBUFFERBITDEPTH;
    shared->d3d_global.hwCaps.dcmColorModel = V9X_D3DCOLOR_RGB;
    shared->d3d_global.hwCaps.dwDevCaps =
        V9X_D3DDEVCAPS_FLOATTLVERTEX |
        V9X_D3DDEVCAPS_EXECUTESYSTEMMEMORY |
        V9X_D3DDEVCAPS_TLVERTEXSYSTEMMEMORY |
        V9X_D3DDEVCAPS_TEXTUREVIDEOMEMORY |
        V9X_D3DDEVCAPS_DRAWPRIMTLVERTEX;
    shared->d3d_global.hwCaps.dtcTransformCaps.dwSize =
        sizeof(V9X_D3DTRANSFORMCAPS);
    shared->d3d_global.hwCaps.dlcLightingCaps.dwSize =
        sizeof(V9X_D3DLIGHTINGCAPS);
    shared->d3d_global.hwCaps.dpcLineCaps.dwSize = sizeof(V9X_D3DPRIMCAPS);

    tri = &shared->d3d_global.hwCaps.dpcTriCaps;
    tri->dwSize = sizeof(V9X_D3DPRIMCAPS);
    tri->dwMiscCaps = V9X_D3DPMISCCAPS_CULLNONE | V9X_D3DPMISCCAPS_CULLCW |
                      V9X_D3DPMISCCAPS_CULLCCW;
    tri->dwRasterCaps = V9X_D3DPRASTERCAPS_SUBPIXEL |
                        V9X_D3DPRASTERCAPS_FOGVERTEX |
                        V9X_D3DPRASTERCAPS_FOGTABLE |
                        V9X_D3DPRASTERCAPS_ZTEST;
    tri->dwZCmpCaps =
        V9X_D3DPCMPCAPS_NEVER | V9X_D3DPCMPCAPS_LESS |
        V9X_D3DPCMPCAPS_EQUAL | V9X_D3DPCMPCAPS_LESSEQUAL |
        V9X_D3DPCMPCAPS_GREATER | V9X_D3DPCMPCAPS_NOTEQUAL |
        V9X_D3DPCMPCAPS_GREATEREQUAL | V9X_D3DPCMPCAPS_ALWAYS;
    tri->dwAlphaCmpCaps = tri->dwZCmpCaps;
    tri->dwSrcBlendCaps =
        V9X_D3DPBLENDCAPS_ZERO | V9X_D3DPBLENDCAPS_ONE |
        V9X_D3DPBLENDCAPS_SRCCOLOR | V9X_D3DPBLENDCAPS_INVSRCCOLOR |
        V9X_D3DPBLENDCAPS_SRCALPHA | V9X_D3DPBLENDCAPS_INVSRCALPHA |
        V9X_D3DPBLENDCAPS_DESTCOLOR | V9X_D3DPBLENDCAPS_INVDESTCOLOR;
    tri->dwDestBlendCaps = tri->dwSrcBlendCaps;
    tri->dwShadeCaps =
        V9X_D3DPSHADECAPS_COLORFLATRGB | V9X_D3DPSHADECAPS_COLORGOURAUDRGB |
        V9X_D3DPSHADECAPS_SPECULARFLATRGB |
        V9X_D3DPSHADECAPS_SPECULARGOURAUDRGB |
        V9X_D3DPSHADECAPS_ALPHAFLATBLEND |
        V9X_D3DPSHADECAPS_ALPHAGOURAUDBLEND |
        V9X_D3DPSHADECAPS_FOGFLAT | V9X_D3DPSHADECAPS_FOGGOURAUD;
    tri->dwTextureCaps = V9X_D3DPTEXTURECAPS_PERSPECTIVE |
                         V9X_D3DPTEXTURECAPS_ALPHA |
                         V9X_D3DPTEXTURECAPS_TRANSPARENCY;
    /* Mip filters are published. A chain create_surface placed samples its
     * levels; any other texture level 0, as on Haswell. */
    tri->dwTextureFilterCaps =
        V9X_D3DPTFILTERCAPS_NEAREST | V9X_D3DPTFILTERCAPS_LINEAR |
        V9X_D3DPTFILTERCAPS_MIPNEAREST | V9X_D3DPTFILTERCAPS_MIPLINEAR |
        V9X_D3DPTFILTERCAPS_LINEARMIPNEAREST |
        V9X_D3DPTFILTERCAPS_LINEARMIPLINEAR;
    tri->dwTextureBlendCaps =
        V9X_D3DPTBLENDCAPS_DECAL | V9X_D3DPTBLENDCAPS_MODULATE |
        V9X_D3DPTBLENDCAPS_MODULATEALPHA | V9X_D3DPTBLENDCAPS_COPY;
    tri->dwTextureAddressCaps = V9X_D3DPTADDRESSCAPS_WRAP |
                                V9X_D3DPTADDRESSCAPS_MIRROR |
                                V9X_D3DPTADDRESSCAPS_CLAMP;

    shared->d3d_extended_caps.dwSize = sizeof(V9X_D3DHAL_D3DEXTENDEDCAPS);
    shared->d3d_extended_caps.dwMinTextureWidth =
        v9x_d3d_g4_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureWidth =
        v9x_d3d_g4_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinTextureHeight =
        v9x_d3d_g4_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureHeight =
        v9x_d3d_g4_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMaxStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMinStippleHeight = 0ul;
    shared->d3d_extended_caps.dwMaxStippleHeight = 0ul;
    shared->d3d_global.hwCaps.dwDeviceRenderBitDepth =
        V9X_DDBD_16 | V9X_DDBD_32;
    /* D16_UNORM, and D24_UNORM_X8 in a 32-bit cell, both Y-tiled. */
    shared->d3d_global.hwCaps.dwDeviceZBufferBitDepth =
        V9X_DDBD_16 | V9X_DDBD_32;

    for (index = 0ul; index < 5ul; ++index) {
        shared->texture_formats[index].dwSize = sizeof(V9X_DDSURFACEDESC);
        shared->texture_formats[index].dwFlags =
            V9X_DDSD_CAPS | V9X_DDSD_PIXELFORMAT;
        shared->texture_formats[index].ddpfPixelFormat.dwSize =
            sizeof(V9X_DDPIXELFORMAT);
        shared->texture_formats[index].ddpfPixelFormat.dwRGBBitCount = 16ul;
        shared->texture_formats[index].ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;
    }
    shared->texture_formats[0].ddpfPixelFormat.dwFlags = V9X_DDPF_RGB;
    shared->texture_formats[0].ddpfPixelFormat.dwRBitMask = 0x0000f800ul;
    shared->texture_formats[0].ddpfPixelFormat.dwGBitMask = 0x000007e0ul;
    shared->texture_formats[0].ddpfPixelFormat.dwBBitMask = 0x0000001ful;
    shared->texture_formats[0].ddpfPixelFormat.dwRGBAlphaBitMask = 0ul;
    shared->texture_formats[1].ddpfPixelFormat.dwFlags =
        V9X_DDPF_RGB | V9X_DDPF_ALPHAPIXELS;
    shared->texture_formats[1].ddpfPixelFormat.dwRBitMask = 0x00007c00ul;
    shared->texture_formats[1].ddpfPixelFormat.dwGBitMask = 0x000003e0ul;
    shared->texture_formats[1].ddpfPixelFormat.dwBBitMask = 0x0000001ful;
    shared->texture_formats[1].ddpfPixelFormat.dwRGBAlphaBitMask =
        0x00008000ul;
    shared->texture_formats[2].ddpfPixelFormat.dwFlags =
        V9X_DDPF_RGB | V9X_DDPF_ALPHAPIXELS;
    shared->texture_formats[2].ddpfPixelFormat.dwRBitMask = 0x00000f00ul;
    shared->texture_formats[2].ddpfPixelFormat.dwGBitMask = 0x000000f0ul;
    shared->texture_formats[2].ddpfPixelFormat.dwBBitMask = 0x0000000ful;
    shared->texture_formats[2].ddpfPixelFormat.dwRGBAlphaBitMask =
        0x0000f000ul;
    shared->texture_formats[3].ddpfPixelFormat.dwFlags =
        V9X_DDPF_RGB | V9X_DDPF_ALPHAPIXELS;
    shared->texture_formats[3].ddpfPixelFormat.dwRGBBitCount = 32ul;
    shared->texture_formats[3].ddpfPixelFormat.dwRBitMask = 0x00ff0000ul;
    shared->texture_formats[3].ddpfPixelFormat.dwGBitMask = 0x0000ff00ul;
    shared->texture_formats[3].ddpfPixelFormat.dwBBitMask = 0x000000fful;
    shared->texture_formats[3].ddpfPixelFormat.dwRGBAlphaBitMask =
        0xff000000ul;
    shared->texture_formats[4].ddpfPixelFormat.dwFlags = V9X_DDPF_RGB;
    shared->texture_formats[4].ddpfPixelFormat.dwRGBBitCount = 32ul;
    shared->texture_formats[4].ddpfPixelFormat.dwRBitMask = 0x00ff0000ul;
    shared->texture_formats[4].ddpfPixelFormat.dwGBitMask = 0x0000ff00ul;
    shared->texture_formats[4].ddpfPixelFormat.dwBBitMask = 0x000000fful;
    shared->texture_formats[4].ddpfPixelFormat.dwRGBAlphaBitMask = 0ul;
    shared->d3d_global.lpTextureFormats = &shared->texture_formats[0];
    shared->d3d_global.dwNumTextureFormats = 5ul;
    shared->d3d_global.dwNumVertices = 0ul;
    shared->d3d_global.dwNumClipVertices = 0ul;
}

/*
 * Mip trees, placed as Haswell places them (d3d_hsw.c
 * v9x_d3d_hsw_create_miptree): SURFACE_STATE has one base and one pitch for
 * a chain, and DirectDraw's heap places each level as a surface of its own,
 * which is never the tree. One block from the heap (d3d_place.c), on a 4 KB
 * boundary, holds the tree, and each level's fpVidMem is pointed at its
 * place in it with the tree's pitch. The block is whole pages, which also
 * covers 965 PRM Vol 1 6.18's padding: the sampler reads to the alignment
 * unit past the edge, and every page it can reach is the block's.
 */
#define V9X_D3D_G4_MIPTREE_SHAPE  1ul  /* not a halving chain */
#define V9X_D3D_G4_MIPTREE_FORMAT 2ul  /* not a layout the sampler takes */

static DWORD v9x_d3d_g4_miptree_decline(DWORD reason)
{
    ++v9x_hal->d3d_diagnostics.mip_tree_declined;
    v9x_hal->d3d_diagnostics.mip_tree_declined_last = reason;
    return V9X_DDHAL_DRIVER_NOTHANDLED;
}

static DWORD v9x_d3d_g4_create_miptree(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL **list = (V9X_DD_SURFACE_LCL **)data->lplpSList;
    V9X_DD_SURFACE_LCL *top = list[0];
    struct v9x_hsw_miptree tree;
    v9x_u16 format;
    int has_alpha;
    v9x_u32 bytes;
    DWORD width = (DWORD)top->lpGbl->wWidth;
    DWORD height = (DWORD)top->lpGbl->wHeight;
    DWORD index;
    DWORD edge_w;
    DWORD edge_h;
    DWORD base;
    DWORD reason;

    /* The list is DirectDraw's, top first, each edge half the one before
     * down to 1; one in any other order is declined rather than sorted. */
    if (width == 0ul || height == 0ul ||
        width > v9x_d3d_g4_limits.texture_size_max ||
        height > v9x_d3d_g4_limits.texture_size_max ||
        data->dwSCnt > V9X_HSW_MIP_LEVELS_MAX) {
        return v9x_d3d_g4_miptree_decline(V9X_D3D_G4_MIPTREE_SHAPE);
    }
    for (index = 1ul; index < data->dwSCnt; ++index) {
        const V9X_DD_SURFACE_LCL *level = list[index];

        edge_w = width >> index;
        edge_h = height >> index;
        if (edge_w == 0ul) {
            edge_w = 1ul;
        }
        if (edge_h == 0ul) {
            edge_h = 1ul;
        }
        if (level == 0 || level->lpGbl == 0 ||
            (level->ddsCaps & V9X_DDSCAPS_MIPMAP) == 0ul ||
            (DWORD)level->lpGbl->wWidth != edge_w ||
            (DWORD)level->lpGbl->wHeight != edge_h) {
            return v9x_d3d_g4_miptree_decline(V9X_D3D_G4_MIPTREE_SHAPE);
        }
    }
    if (!v9x_d3d_g4_texture_format_of(top, &format, &has_alpha, &bytes)) {
        return v9x_d3d_g4_miptree_decline(V9X_D3D_G4_MIPTREE_FORMAT);
    }
    if (v9x_intel_hsw_miptree((v9x_u16)width, (v9x_u16)height,
                              (v9x_u8)data->dwSCnt, (v9x_u8)bytes, &tree) !=
            V9X_STATUS_OK) {
        return v9x_d3d_g4_miptree_decline(V9X_D3D_G4_MIPTREE_SHAPE);
    }
    reason = v9x_d3d_place_block(data, V9X_D3D_G4_PLACE_ALIGN, tree.pitch,
                                 tree.rows, tree.level_offset, &base);
    if (reason != 0ul) {
        return v9x_d3d_g4_miptree_decline(reason);
    }
    ++v9x_hal->d3d_diagnostics.mip_tree_allocs;
    v9x_hal->d3d_diagnostics.mip_tree_last_offset = base;
    v9x_hal->d3d_diagnostics.mip_tree_last_shape =
        (tree.pitch << 16) | (tree.rows & 0xfffful);
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

/*
 * A Z buffer in whole Y-major tiles: a 16-bit D16_UNORM, or a 32-bit
 * D24_UNORM_X8, its pitch rounded up to the tile's 128 bytes and its rows
 * to the tile's 32, in a 4 KB-aligned block, as Haswell places its own.
 *
 * Linear was first. A linear depth buffer's pitch must be a multiple of 64
 * bytes (965 PRM Vol 2, 3DSTATE_DEPTH_BUFFER Surface Pitch), and before
 * this placement DirectDraw's heap gave a 400-wide one 800, which hung the
 * 3D pipe on TOMMOHP boot 14. A linear D24_UNORM_X8 hung it at any pitch
 * (boot 8), where in the boot 20 diagnostic a Y-tiled D16 drew the ICD's
 * depth-tested draws without a hang. Tiled, the CPU and the blitter cannot
 * write it: fills are drawn by the engine (v9x_d3d_g4_depth_fill), and the
 * limits say depth_tiled so a declined fill fails instead of reaching the
 * CPU.
 */
static DWORD v9x_d3d_g4_create_depth(V9X_DDHAL_CREATESURFACEDATA *data,
                                     V9X_DD_SURFACE_LCL *surface)
{
    v9x_u32 offsets[1];
    DWORD pitch;
    DWORD rows;
    DWORD base;
    DWORD bytes = 2ul;

    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul) {
        if (surface->lpGbl->ddpfSurface.dwRGBBitCount == 32ul) {
            bytes = 4ul;
        } else if (surface->lpGbl->ddpfSurface.dwRGBBitCount != 16ul) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
    }
    if (surface->lpGbl->wWidth == 0u || surface->lpGbl->wHeight == 0u ||
        (DWORD)surface->lpGbl->wWidth >
            v9x_d3d_g4_limits.target_dimension_max ||
        (DWORD)surface->lpGbl->wHeight >
            v9x_d3d_g4_limits.target_dimension_max) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    pitch = ((DWORD)surface->lpGbl->wWidth * bytes +
             V9X_D3D_G4_DEPTH_PITCH_ALIGN - 1ul) &
            ~(V9X_D3D_G4_DEPTH_PITCH_ALIGN - 1ul);
    rows = ((DWORD)surface->lpGbl->wHeight + V9X_D3D_G4_DEPTH_ROW_ALIGN -
            1ul) & ~(V9X_D3D_G4_DEPTH_ROW_ALIGN - 1ul);
    offsets[0] = 0ul;
    if (v9x_d3d_place_block(data, V9X_D3D_G4_PLACE_ALIGN, pitch, rows,
                            offsets, &base) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    ++v9x_hal->d3d_diagnostics.z_placed;
    v9x_hal->d3d_diagnostics.z_placed_pitch = pitch;
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

/* A lone video-memory Z buffer, or a video-memory mip chain; every other
 * surface is DirectDraw's. */
static DWORD v9x_d3d_g4_create_surface(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL **list;
    V9X_DD_SURFACE_LCL *surface;

    if (v9x_hal == 0 || data == 0 || data->dwSCnt == 0ul ||
        data->lplpSList == 0) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    list = (V9X_DD_SURFACE_LCL **)data->lplpSList;
    surface = list[0];
    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    if (data->dwSCnt == 1ul &&
        (surface->ddsCaps & V9X_DDSCAPS_ZBUFFER) != 0ul) {
        return v9x_d3d_g4_create_depth(data, surface);
    }
    if (data->dwSCnt >= 2ul &&
        (surface->ddsCaps & (V9X_DDSCAPS_TEXTURE | V9X_DDSCAPS_MIPMAP)) ==
            (V9X_DDSCAPS_TEXTURE | V9X_DDSCAPS_MIPMAP)) {
        return v9x_d3d_g4_create_miptree(data);
    }
    return V9X_DDHAL_DRIVER_NOTHANDLED;
}

/* The top of a tree or a Z buffer placed above is going: free the block
 * (d3d_place.c ignores every other surface). */
static void v9x_d3d_g4_destroy_surface(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
    if (v9x_d3d_place_release(data, V9X_D3D_G4_PLACE_ALIGN)) {
        ++v9x_hal->d3d_diagnostics.mip_tree_frees;
    }
}

/* Positional: V9X_D3D_ENGINE_OPS is append-only (d3d_internal.h). */
const V9X_D3D_ENGINE_OPS v9x_d3d_engine_g4 = {
    &v9x_d3d_g4_limits,
    v9x_d3d_g4_texture_format,
    v9x_d3d_g4_describe_caps,
    0,                                  /* draw_triangles: draw serves */
    v9x_d3d_g4_ready,
    v9x_d3d_g4_create_surface,
    v9x_d3d_g4_destroy_surface,
    v9x_d3d_g4_draw,
    v9x_d3d_g4_accepts
};
