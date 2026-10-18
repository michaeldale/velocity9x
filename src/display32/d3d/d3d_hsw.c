/*
 * Intel Gen7.5 (Haswell) Direct3D engine.
 *
 * Phase F2a of docs\plans\intel-haswell-direct3d.md. Every command and state
 * word comes from the host-tested builders in src\chipsets\intel\hsw_3d.c
 * and every pixel shader from the generator in hsw_eu.c; the batches are
 * the ones Phases C to F1a drew pixel-exact on the i5-4570
 * (docs\decisions\2026-10-17-haswell-phase-*). This file resolves surfaces,
 * maps the neutral draw onto that state, and submits.
 *
 * The render ring was opened by the mini-VDD at reserve + 2000h beside the
 * blitter's, under the forcewake it holds (HSW_ENGINE_OPEN). The first
 * process to find the status page without the render-initialised mark runs
 * the initialising batch Phase C measured.
 *
 * Synchronous: each draw waits for the blitter, writes its state into the
 * reserve, submits one batch with a sequence store after it, and waits for
 * the store. So the RCS is idle whenever anything else runs, which is what
 * lets the blitter, the CPU and other processes ignore it. No state lives
 * in this DLL across draws: it is mapped into every Direct3D process, and
 * every batch carries all of its state.
 *
 * Depth is F2b's: D16_UNORM Z buffers, which Gen7 always reads Y-tiled, so
 * this file places them in whole tiles and fills them by drawing - a
 * depth-only quad with colour writes masked, the shape the 4570 measured
 * (docs\decisions\2026-10-17-haswell-phase-f2b-depth.md). A Lock of a Z
 * buffer returns the tiled bytes; detiling is not done.
 */
#include "d3d_internal.h"
#include "d3d_hsw_map.h"
#include "d3d_state.h"
#include "velocity9x/intel_hsw.h"
#include "velocity9x/intel_hsw_3d.h"
#include "velocity9x/intel_hsw_eu.h"

/* Graphics addresses inside the reserve (velocity9x\intel_hsw.h). */
#define V9X_D3D_HSW_GFX(offset) (V9X_INTEL_HSW_RESERVE_OFFSET + (offset))
#define V9X_D3D_HSW_STATE_BASE  V9X_D3D_HSW_GFX(V9X_INTEL_HSW_STATE_OFF)
#define V9X_D3D_HSW_RCS_RING    V9X_D3D_HSW_GFX(V9X_INTEL_HSW_RCS_RING_OFF)
#define V9X_D3D_HSW_STATUS_GFX  V9X_D3D_HSW_GFX(V9X_INTEL_HSW_RING_BYTES)
/* The batch's own PIPE_CONTROL breadcrumb, a qword after the sequences. */
#define V9X_D3D_HSW_CRUMB_GFX   (V9X_D3D_HSW_STATUS_GFX + 0x30ul)

/* Each draw writes its surfaces, sampler, blend and CC state at E's
 * offsets (intel_hsw_3d.h); the border colour E's sampler names is the
 * zeroed run after the CC state. */
#define V9X_D3D_HSW_BORDER_BYTES 0x40ul
#define V9X_D3D_HSW_TEXTURE_OFF  V9X_HSW_SHADE_TEX_SURFACE(0)

/* Each draw's DEPTH_STENCIL_STATE, in the slot E's layout gave the first. */
#define V9X_D3D_HSW_DS_OFF V9X_HSW_SHADE_DS_ALWAYS

/* A Y tile is 128 bytes by 32 rows in 4 KB (Vol 1c), and the depth buffer
 * is always Y-tiled on Gen7 (Vol 2b 3DSTATE_DEPTH_BUFFER Tiled Surface):
 * a Z buffer's pitch, rows and base are whole tiles. */
#define V9X_D3D_HSW_TILE_PITCH 128ul
#define V9X_D3D_HSW_TILE_ROWS  32ul
#define V9X_D3D_HSW_TILE_BYTES 4096ul

/* A depth fill is two triangles. */
#define V9X_D3D_HSW_FILL_VERTICES 6ul

/* The vertex area holds this many triangles of 96 bytes. */
#define V9X_D3D_HSW_MAX_TRIANGLES \
    (V9X_INTEL_HSW_VERTEX_BYTES / (3ul * V9X_HSW_TL_VERTEX_BYTES))
/* And this many of 120, with unit 1's coordinates after each vertex. */
#define V9X_D3D_HSW_MAX_TRIANGLES2 \
    (V9X_INTEL_HSW_VERTEX_BYTES / (3ul * V9X_HSW_TL2_VERTEX_BYTES))

/* Ring submission: MI_BATCH_BUFFER_START and the sequence store. */
#define V9X_D3D_HSW_SUBMIT_DWORDS 6ul

/* Status reads at about a microsecond, as the blitter's (eng_hsw.c). */
#define V9X_D3D_HSW_SPINS 0x00400000ul

/* The framebuffer is at aperture offset 0, as eng_hsw.c has it. */
#define V9X_D3D_HSW_APERTURE_MASK 0x0ffffffful

/* Fault-trace tag: "HSW3". */
#define V9X_D3D_HSW_FAULT_TAG 0x48535733ul

/* Refusal reasons, in the shared policy counters (V9XTRACE M64PolicyNN). */
#define V9X_D3D_HSW_REFUSE_NONE      0ul
#define V9X_D3D_HSW_REFUSE_TARGET    1ul
#define V9X_D3D_HSW_REFUSE_DEPTH     2ul
#define V9X_D3D_HSW_REFUSE_TEXTURE   3ul
#define V9X_D3D_HSW_REFUSE_TEXOP     4ul
#define V9X_D3D_HSW_REFUSE_BLEND     5ul
#define V9X_D3D_HSW_REFUSE_ADDRESS   6ul
#define V9X_D3D_HSW_REFUSE_KEY       7ul
#define V9X_D3D_HSW_REFUSE_ALPHA     8ul

/* SAMPLER_STATE DW3 address modes for all three axes (Vol 2a/2d). */
#define V9X_D3D_HSW_TC_WRAP   0ul
#define V9X_D3D_HSW_TC_MIRROR ((1ul << 6) | (1ul << 3) | 1ul)
#define V9X_D3D_HSW_TC_CLAMP  ((2ul << 6) | (2ul << 3) | 2ul)

static const V9X_D3D_ENGINE_LIMITS v9x_d3d_hsw_limits = {
    16ul,           /* target_bits_per_pixel */
    16384ul,        /* target_pitch_max */
    4ul,            /* target_pitch_align */
    4096ul,         /* target_dimension_max */
    1ul,            /* texture_size_min */
    2048ul,         /* texture_size_max */
    8192.0f,        /* coordinate_limit */
    16ul,           /* depth_bits_per_pixel: D16_UNORM */
    16ul,           /* texture_align */
    1ul,            /* clip_in_core: the CLIP stage is off */
    /* 3DSTATE_DEPTH_BUFFER carries the Z buffer's own pitch, whole tiles
     * wider than its row. */
    1ul,            /* depth_pitch_own */
    0ul,            /* depth_fill_shift */
    2ul,            /* texture_units: step 4, render interface only */
    /* 3DSTATE_MULTISAMPLE Pixel Location is the pixel centre (Vol 2b), and
     * Phase D's coverage matched a reference sampling at x + 0.5. */
    1ul,            /* pixel_centre_half */
    1ul,            /* depth_tiled: Y tiles */
    /* XRGB8888 on a 32 bpp desktop as B8G8R8A8 with alpha writes off
     * (v9x_d3d_hsw_target); the pitch bound is the 16 bpp one. */
    16384ul         /* target_pitch_max_xrgb8888 */
};

/* One texture unit as the sampler reads it. */
typedef struct v9x_d3d_hsw_texture {
    struct v9x_hsw_sampler sampler;
    v9x_u16 format;
    v9x_u32 address;
    v9x_u16 width;
    v9x_u16 height;
    v9x_u32 pitch;
    /* Mip levels the sampler may read: 1, or a chain create_surface laid
     * out (v9x_d3d_hsw_mip_levels). */
    v9x_u8 levels;
} V9X_D3D_HSW_TEXTURE;

/* What the mapping makes of a draw. */
typedef struct v9x_d3d_hsw_state {
    struct v9x_hsw_ps_desc desc;
    struct v9x_hsw_blend blend;
    v9x_u16 target_format;
    V9X_D3D_HSW_TEXTURE texture[2];
    v9x_u8 alpha_ref;
    /* Texture units sampled: 0, 1, or 2 for a render-interface draw with
     * texcoords1 (step 4). */
    int textured;
    int depth;
    v9x_u32 depth_address;
    v9x_u32 depth_pitch;
    v9x_u8 depth_function;
    v9x_u8 depth_write;
    /* Render interface step 2: the drawing rectangle, inclusive, and
     * whether any pixel of the target is inside it. */
    v9x_u32 scissor[4];
    int scissor_empty;
} V9X_D3D_HSW_STATE;

/* Once per process: the ring was found where the mini-VDD put it. */
static int v9x_d3d_hsw_ring_checked = 0;
/* Set on a timeout; nothing is submitted again in this process. */
static int v9x_d3d_hsw_quarantined = 0;

static DWORD v9x_d3d_hsw_read32(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

static void v9x_d3d_hsw_write32(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) = value;
}

static v9x_u32 *v9x_d3d_hsw_reserve(DWORD offset)
{
    return (v9x_u32 *)(v9x_hal->engine.ring_linear_base + offset);
}

static volatile DWORD *v9x_d3d_hsw_status(void)
{
    return (volatile DWORD *)v9x_d3d_hsw_reserve(V9X_INTEL_HSW_RING_BYTES);
}

static DWORD v9x_d3d_hsw_vram(DWORD offset)
{
    return (v9x_hal->fb.physical_base & V9X_D3D_HSW_APERTURE_MASK) + offset;
}

static void v9x_d3d_hsw_quarantine(void)
{
    if (v9x_hal->engine.idle_timeouts++ == 0ul) {
        v9x_trace_flush_fault(V9X_D3D_HSW_FAULT_TAG,
                              v9x_d3d_hsw_read32(V9X_INTEL_HSW_RCS_HEAD));
    }
    v9x_d3d_hsw_quarantined = 1;
    v9x_hal->engine.flags &= ~V9X_DD_ENGINE_VALID;
}

/* Every submitted render sequence has been stored back. */
static int v9x_d3d_hsw_wait_rcs(void)
{
    volatile DWORD *status = v9x_d3d_hsw_status();
    DWORD spins;
    DWORD started = V9X_TIME_BEGIN();

    /* Timed as the render drain: the GPU's share of a synchronous draw. */
    for (spins = 0ul; spins < V9X_D3D_HSW_SPINS; ++spins) {
        if (status[V9X_INTEL_HSW_STATUS_RCS_DONE] ==
            status[V9X_INTEL_HSW_STATUS_RCS_SENT]) {
            V9X_TIME_END(V9X_TIME_RENDER_DRAIN, started);
            return 1;
        }
    }
    V9X_TIME_END(V9X_TIME_RENDER_DRAIN, started);
    v9x_d3d_hsw_quarantine();
    return 0;
}

/*
 * The batch at the batch area, then the sequence store, written at the
 * hardware's tail; a ring too short for them is filled with MI_NOOP and
 * they start at 0, as eng_hsw.c does. Called idle, so HEAD equals TAIL.
 */
static int v9x_d3d_hsw_submit(void)
{
    volatile DWORD *ring =
        (volatile DWORD *)v9x_d3d_hsw_reserve(V9X_INTEL_HSW_RCS_RING_OFF);
    volatile DWORD *status = v9x_d3d_hsw_status();
    v9x_u32 dw[V9X_D3D_HSW_SUBMIT_DWORDS];
    struct v9x_hsw_batch batch;
    DWORD sequence;
    DWORD tail;
    DWORD index;

    sequence = status[V9X_INTEL_HSW_STATUS_RCS_SENT] + 1ul;
    v9x_intel_hsw_batch_init(&batch, dw, V9X_D3D_HSW_SUBMIT_DWORDS);
    v9x_intel_hsw_mi_batch_buffer_start(
        &batch, V9X_D3D_HSW_GFX(V9X_INTEL_HSW_BATCH_OFF));
    v9x_intel_hsw_mi_store_data_imm(
        &batch, V9X_D3D_HSW_STATUS_GFX + V9X_INTEL_HSW_STATUS_RCS_DONE * 4ul,
        sequence);
    if (batch.overflow != 0u) {
        return 0;
    }

    tail = v9x_d3d_hsw_read32(V9X_INTEL_HSW_RCS_TAIL) &
           V9X_INTEL_HSW_TAIL_MASK;
    if (tail + V9X_D3D_HSW_SUBMIT_DWORDS * 4ul > V9X_INTEL_HSW_RING_BYTES) {
        for (index = tail / 4ul; index < V9X_INTEL_HSW_RING_BYTES / 4ul;
             ++index) {
            ring[index] = 0ul;
        }
        tail = 0ul;
    }
    for (index = 0ul; index < V9X_D3D_HSW_SUBMIT_DWORDS; ++index) {
        ring[tail / 4ul + index] = dw[index];
    }
    status[V9X_INTEL_HSW_STATUS_RCS_SENT] = sequence;
    tail += V9X_D3D_HSW_SUBMIT_DWORDS * 4ul;
    if (tail >= V9X_INTEL_HSW_RING_BYTES) {
        tail = 0ul;
    }
    v9x_d3d_hsw_write32(V9X_INTEL_HSW_RCS_TAIL, tail);
    (void)v9x_d3d_hsw_read32(V9X_INTEL_HSW_RCS_TAIL);
    return v9x_d3d_hsw_wait_rcs();
}

/* The initialising batch (Phase C), once per ring open: the reserve is
 * zeroed at open, which removes the mark. */
static int v9x_d3d_hsw_initialise(void)
{
    volatile DWORD *status = v9x_d3d_hsw_status();
    struct v9x_hsw_batch batch;

    if (status[V9X_INTEL_HSW_STATUS_RCS_INIT] ==
        V9X_INTEL_HSW_RCS_INIT_MARK) {
        return 1;
    }
    if (v9x_intel_hsw_init_state(
            v9x_d3d_hsw_reserve(V9X_INTEL_HSW_STATE_OFF),
            V9X_HSW_INIT_STATE_BYTES / 4ul) != V9X_STATUS_OK) {
        return 0;
    }
    v9x_intel_hsw_batch_init(&batch,
                             v9x_d3d_hsw_reserve(V9X_INTEL_HSW_BATCH_OFF),
                             V9X_INTEL_HSW_BATCH_BYTES / 4ul);
    if (v9x_intel_hsw_init_workload(&batch, V9X_D3D_HSW_STATE_BASE,
                                    V9X_INTEL_HSW_STATE_BYTES,
                                    V9X_D3D_HSW_CRUMB_GFX, 1ul) !=
            V9X_STATUS_OK ||
        !v9x_d3d_hsw_submit()) {
        return 0;
    }
    status[V9X_INTEL_HSW_STATUS_RCS_INIT] = V9X_INTEL_HSW_RCS_INIT_MARK;
    return 1;
}

/* The blitter's conditions, the render ring the mini-VDD opened (CAP_D3D),
 * still on where it was put, and initialised. */
static int v9x_d3d_hsw_ready(void)
{
    if (v9x_hal == 0 || v9x_d3d_hsw_quarantined ||
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) == 0ul ||
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) == 0ul ||
        v9x_hal->engine.engine_type != V9X_DD_ENGINE_TYPE_INTEL_GEN75 ||
        (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_D3D) == 0ul ||
        v9x_hal->engine.control_linear_base == 0ul ||
        v9x_hal->engine.ring_linear_base == 0ul) {
        return 0;
    }
    if (!v9x_d3d_hsw_ring_checked) {
        if ((v9x_d3d_hsw_read32(V9X_INTEL_HSW_RCS_CTL) &
             V9X_INTEL_HSW_CTL_ENABLE) == 0ul ||
            v9x_d3d_hsw_read32(V9X_INTEL_HSW_RCS_START) !=
                V9X_D3D_HSW_RCS_RING) {
            return 0;
        }
        v9x_d3d_hsw_ring_checked = 1;
    }
    return v9x_d3d_hsw_initialise();
}

/* The 16-bit formats the sampler takes, by mask, as the 3dfx engine reads
 * them; the texture's alpha decides MODULATE's alpha. */
static int v9x_d3d_hsw_texture_format_of(const V9X_DD_SURFACE_LCL *surface,
                                         v9x_u16 *format, int *has_alpha)
{
    const V9X_DDPIXELFORMAT *pf;

    if (surface == 0 || surface->lpGbl == 0) {
        return 0;
    }
    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul) {
        pf = &surface->lpGbl->ddpfSurface;
    } else {
        pf = &v9x_hal->info.vmiData.ddpfDisplay;
    }
    if ((pf->dwFlags & V9X_DDPF_RGB) == 0ul || pf->dwRGBBitCount != 16ul) {
        return 0;
    }
    if ((pf->dwFlags & V9X_DDPF_ALPHAPIXELS) == 0ul) {
        if (pf->dwRBitMask == 0x0000f800ul && pf->dwGBitMask == 0x000007e0ul &&
            pf->dwBBitMask == 0x0000001ful) {
            *format = V9X_HSW_FMT_B5G6R5_UNORM;
            *has_alpha = 0;
            return 1;
        }
        return 0;
    }
    *has_alpha = 1;
    if (pf->dwRBitMask == 0x00007c00ul && pf->dwGBitMask == 0x000003e0ul &&
        pf->dwBBitMask == 0x0000001ful &&
        pf->dwRGBAlphaBitMask == 0x00008000ul) {
        *format = V9X_HSW_FMT_B5G5R5A1_UNORM;
        return 1;
    }
    if (pf->dwRBitMask == 0x00000f00ul && pf->dwGBitMask == 0x000000f0ul &&
        pf->dwBBitMask == 0x0000000ful &&
        pf->dwRGBAlphaBitMask == 0x0000f000ul) {
        *format = V9X_HSW_FMT_B4G4R4A4_UNORM;
        return 1;
    }
    return 0;
}

static int v9x_d3d_hsw_texture_format(const V9X_DD_SURFACE_LCL *surface,
                                      DWORD *format_out)
{
    v9x_u16 format = 0u;
    int has_alpha = 0;

    if (format_out == 0) {
        return 0;
    }
    *format_out = 0ul;
    if (v9x_hal == 0 ||
        !v9x_d3d_hsw_texture_format_of(surface, &format, &has_alpha)) {
        return 0;
    }
    *format_out = format;
    return 1;
}

/*
 * A Z buffer this file placed: video memory, a pitch of whole tiles, on a
 * tile boundary, and carrying the placement's signature (v9x_d3d_place_block
 * leaves lpVidMemHeap null and the block in dwReserved1). One DirectDraw's
 * heap placed has none of the padding rows the last row of tiles reaches
 * into, so the engine would write past it; it is refused.
 */
static int v9x_d3d_hsw_depth_surface(const V9X_DD_SURFACE_LCL *surface,
                                     DWORD *offset)
{
    const V9X_DD_SURFACE_GBL *global;

    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_ZBUFFER) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
        return 0;
    }
    global = surface->lpGbl;
    if (global->lPitch <= 0l ||
        ((DWORD)global->lPitch & (V9X_D3D_HSW_TILE_PITCH - 1ul)) != 0ul ||
        global->dwBlockSizeX != 0ul || global->dwReserved1 == 0ul ||
        global->wWidth == 0u || global->wHeight == 0u ||
        (DWORD)global->wWidth > v9x_d3d_hsw_limits.target_dimension_max ||
        (DWORD)global->wHeight > v9x_d3d_hsw_limits.target_dimension_max) {
        return 0;
    }
    *offset = v9x_surface_offset(surface);
    return *offset != 0xfffffffful &&
           (*offset & (V9X_D3D_HSW_TILE_BYTES - 1ul)) == 0ul;
}

/* Direct3D's blend factors (V9X_R3D_BLEND_*) as Gen's (Vol 2a
 * 3D_Color_Buffer_Blend_Factor); 0 for one this engine does not take. */
static v9x_u8 v9x_d3d_hsw_factor(v9x_u32 factor)
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

/* Direct3D's comparisons 1-8 against Gen's (Vol 2a 3D_Compare_Function):
 * NEVER..GEQUAL keep their numbers, ALWAYS is 0. */
static v9x_u8 v9x_d3d_hsw_compare(v9x_u32 function)
{
    if (function == V9X_R3D_CMP_ALWAYS) {
        return 0u;
    }
    return (v9x_u8)(function & 7ul);
}

/* MAPFILTER_LINEAR for the filters whose texels blend within a level; the
 * part between levels is v9x_d3d_hsw_mip_filter's. The DirectX 5 names
 * read as d3d_i9xx.c's v9x_d3d_i9xx_filter settles them: LINEAR after
 * MIP is within a level, before it between levels. */
static v9x_u8 v9x_d3d_hsw_filter(v9x_u32 filter)
{
    return filter == V9X_R3D_FILTER_LINEAR ||
                   filter == V9X_R3D_FILTER_MIPLINEAR ||
                   filter == V9X_R3D_FILTER_LINEARMIPLINEAR
               ? 1u : 0u;
}

/* SAMPLER_STATE DW0 21:20, Mip Mode Filter (Vol 2d): MIPFILTER_NONE 0,
 * _NEAREST 1, _LINEAR 3. */
#define V9X_D3D_HSW_MIPFILTER_NONE    0u
#define V9X_D3D_HSW_MIPFILTER_NEAREST 1u
#define V9X_D3D_HSW_MIPFILTER_LINEAR  3u

static v9x_u8 v9x_d3d_hsw_mip_filter(v9x_u32 filter)
{
    if (filter == V9X_R3D_FILTER_MIPNEAREST ||
        filter == V9X_R3D_FILTER_MIPLINEAR) {
        return V9X_D3D_HSW_MIPFILTER_NEAREST;
    }
    if (filter == V9X_R3D_FILTER_LINEARMIPNEAREST ||
        filter == V9X_R3D_FILTER_LINEARMIPLINEAR) {
        return V9X_D3D_HSW_MIPFILTER_LINEAR;
    }
    return V9X_D3D_HSW_MIPFILTER_NONE;
}

/*
 * How many levels of a texture's chain the sampler may read: one when there
 * is no chain or its levels are not where v9x_intel_hsw_miptree puts them,
 * and each level that is, in order, otherwise.
 *
 * RENDER_SURFACE_STATE has one base and one pitch for every level, so only
 * a chain create_surface placed as one tree can be sampled past level 0.
 * One the heap placed - because create_surface declined it, or an
 * application attached the levels itself - is sampled at level 0 with no
 * mip filter, as every texture was before, and is counted in the mip_gap
 * counters so a capture tells the two apart. Gen3's v9x_d3d_i9xx_mip_levels
 * makes the same walk for its own layout.
 */
static v9x_u8 v9x_d3d_hsw_mip_levels(const V9X_DD_SURFACE_LCL *top)
{
    const V9X_DD_SURFACE_LCL *chain[V9X_HSW_MIP_LEVELS_MAX];
    const V9X_DD_SURFACE_LCL *level = top;
    struct v9x_hsw_miptree tree;
    DWORD width = (DWORD)top->lpGbl->wWidth;
    DWORD height = (DWORD)top->lpGbl->wHeight;
    DWORD count = 1ul;
    DWORD verified;

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
    if (v9x_intel_hsw_miptree((v9x_u16)width, (v9x_u16)height,
                              (v9x_u8)count, 2u, &tree) != V9X_STATUS_OK) {
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

static v9x_u32 v9x_d3d_hsw_float(v9x_u32 byte)
{
    union {
        float value;
        v9x_u32 bits;
    } convert;

    convert.value = (float)byte / 255.0f;
    return convert.bits;
}

/* A texture surface as this engine samples it, or the reason it cannot be.
 * has_alpha: whether its format carries alpha. */
static DWORD v9x_d3d_hsw_texture_unit(const V9X_R3D_TEXTURE *texture,
                                      V9X_D3D_HSW_TEXTURE *unit,
                                      int *has_alpha)
{
    V9X_DD_SURFACE_LCL *surface = (V9X_DD_SURFACE_LCL *)texture->object;
    DWORD offset;

    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul ||
        surface->lpGbl->lPitch <= 0l ||
        !v9x_d3d_hsw_texture_format_of(surface, &unit->format, has_alpha)) {
        return V9X_D3D_HSW_REFUSE_TEXTURE;
    }
    offset = v9x_surface_offset(surface);
    if (offset == 0xfffffffful) {
        return V9X_D3D_HSW_REFUSE_TEXTURE;
    }
    if (texture->wrap_u != 0ul || texture->wrap_v != 0ul) {
        return V9X_D3D_HSW_REFUSE_ADDRESS;
    }
    /* MIN's choice between levels is the mip filter only when the chain
     * has levels; MAX LOD, U4.8, stops at the last (Vol 2d). MAG's has no
     * meaning, magnification being level 0. */
    unit->levels = v9x_d3d_hsw_mip_levels(surface);
    unit->sampler.mip_filter = V9X_D3D_HSW_MIPFILTER_NONE;
    unit->sampler.mag_filter = v9x_d3d_hsw_filter(texture->mag_filter);
    unit->sampler.min_filter = v9x_d3d_hsw_filter(texture->min_filter);
    unit->sampler.min_lod = 0u;
    unit->sampler.max_lod = 0u;
    if (unit->levels > 1u) {
        unit->sampler.mip_filter = v9x_d3d_hsw_mip_filter(texture->min_filter);
        unit->sampler.max_lod = (v9x_u16)((unit->levels - 1u) << 8);
    }
    unit->sampler.border_color_pointer = V9X_HSW_SHADE_BORDER_COLOR;
    switch (texture->address) {
    case V9X_R3D_ADDRESS_WRAP:
        unit->sampler.dw3 = V9X_D3D_HSW_TC_WRAP;
        break;
    case V9X_R3D_ADDRESS_MIRROR:
        unit->sampler.dw3 = V9X_D3D_HSW_TC_MIRROR;
        break;
    case V9X_R3D_ADDRESS_CLAMP:
        unit->sampler.dw3 = V9X_D3D_HSW_TC_CLAMP;
        break;
    default:
        return V9X_D3D_HSW_REFUSE_ADDRESS;
    }
    unit->address = v9x_d3d_hsw_vram(offset);
    unit->width = surface->lpGbl->wWidth;
    unit->height = surface->lpGbl->wHeight;
    unit->pitch = (v9x_u32)surface->lpGbl->lPitch;
    if (unit->width == 0u || unit->height == 0u ||
        unit->width > v9x_d3d_hsw_limits.texture_size_max ||
        unit->height > v9x_d3d_hsw_limits.texture_size_max) {
        return V9X_D3D_HSW_REFUSE_TEXTURE;
    }
    return V9X_D3D_HSW_REFUSE_NONE;
}

/* The draw as this engine's state, or the reason it cannot be. */
static DWORD v9x_d3d_hsw_map(const V9X_R3D_DRAW *draw,
                             V9X_D3D_HSW_STATE *state)
{
    V9X_DD_SURFACE_LCL *surface;
    DWORD offset;
    DWORD reason;
    int has_alpha = 0;
    int has_alpha1 = 0;
    v9x_u32 op;
    v9x_u8 target_disable = 0u;

    /* What the render interface states and this engine cannot draw exactly
     * (d3d_hsw_map.c): CPU levels. */
    reason = v9x_d3d_hsw_explicit_refusal(draw);
    if (reason != V9X_D3D_HSW_REFUSE_NONE) {
        return reason;
    }

    state->desc.stage0 = V9X_HSW_PS_DIFFUSE;
    state->desc.stage1 = V9X_HSW_PS_STAGE1_NONE;
    state->desc.fog = 0u;
    state->desc.fog_rgb[0] = 0ul;
    state->desc.fog_rgb[1] = 0ul;
    state->desc.fog_rgb[2] = 0ul;
    state->desc.specular = 0u;
    state->textured = 0;
    state->depth = 0;
    state->depth_address = 0ul;
    state->depth_pitch = 0ul;
    state->depth_function = 0u;
    state->depth_write = 0u;

    /* 565, 1555, or XRGB8888 with its alpha writes off (target_disable,
     * ORed into the blend state below). */
    if (!v9x_d3d_hsw_target(draw->target.format, &state->target_format,
                            &target_disable)) {
        return V9X_D3D_HSW_REFUSE_TARGET;
    }
    if (draw->target.pitch == 0ul || draw->target.width == 0ul ||
        draw->target.height == 0ul ||
        draw->target.width > v9x_d3d_hsw_limits.target_dimension_max ||
        draw->target.height > v9x_d3d_hsw_limits.target_dimension_max) {
        return V9X_D3D_HSW_REFUSE_TARGET;
    }
    if (v9x_d3d_state_depth_active(draw->depth_enable,
                                   draw->depth.object != 0,
                                   draw->depth.pitch) != 0ul) {
        surface = (V9X_DD_SURFACE_LCL *)draw->depth.object;
        /* The depth buffer is addressed at the target's extent (DW3), so
         * it must cover the target. */
        if (!v9x_d3d_hsw_depth_surface(surface, &offset) ||
            (DWORD)surface->lpGbl->wWidth < draw->target.width ||
            (DWORD)surface->lpGbl->wHeight < draw->target.height ||
            draw->depth_func < V9X_R3D_CMP_NEVER ||
            draw->depth_func > V9X_R3D_CMP_ALWAYS) {
            return V9X_D3D_HSW_REFUSE_DEPTH;
        }
        state->depth = 1;
        state->depth_address = v9x_d3d_hsw_vram(offset);
        state->depth_pitch = (v9x_u32)surface->lpGbl->lPitch;
        state->depth_function = v9x_d3d_hsw_compare(draw->depth_func);
        state->depth_write = draw->depth_write != 0ul ? 1u : 0u;
    }

    if (draw->texcoords1 != 0) {
        /* Two units (render interface step 4): each unit's combine is in
         * its color_op and alpha_op, `op` is not set (r3d.h). */
        if (draw->color_key_enable != 0ul) {
            return V9X_D3D_HSW_REFUSE_KEY;
        }
        reason = v9x_d3d_hsw_texture_unit(&draw->texture, &state->texture[0],
                                          &has_alpha);
        if (reason != V9X_D3D_HSW_REFUSE_NONE) {
            return reason;
        }
        reason = v9x_d3d_hsw_texture_unit(&draw->texture1,
                                          &state->texture[1], &has_alpha1);
        if (reason != V9X_D3D_HSW_REFUSE_NONE) {
            return reason;
        }
        reason = v9x_d3d_hsw_two_unit_stages(
            draw->texture.color_op, draw->texture.alpha_op, has_alpha,
            draw->texture1.color_op, draw->texture1.alpha_op, has_alpha1,
            &state->desc.stage0, &state->desc.stage1);
        if (reason != V9X_D3D_HSW_REFUSE_NONE) {
            return reason;
        }
        state->textured = 2;
    } else if (draw->texture.object != 0) {
        reason = v9x_d3d_hsw_texture_unit(&draw->texture, &state->texture[0],
                                          &has_alpha);
        if (reason != V9X_D3D_HSW_REFUSE_NONE) {
            return reason;
        }
        if (draw->color_key_enable != 0ul) {
            return V9X_D3D_HSW_REFUSE_KEY;
        }
        op = draw->texture.op;
        if (op == V9X_R3D_TEXOP_DECAL || op == V9X_R3D_TEXOP_COPY) {
            state->desc.stage0 = V9X_HSW_PS_TEXTURE;
        } else if (op == V9X_R3D_TEXOP_MODULATE) {
            /* D3D's rule: the texture's alpha when it has one. */
            state->desc.stage0 = has_alpha ? V9X_HSW_PS_MODULATE_TEXALPHA
                                           : V9X_HSW_PS_MODULATE;
        } else if (op == V9X_R3D_TEXOP_MODULATEALPHA) {
            state->desc.stage0 = V9X_HSW_PS_MODULATE;
        } else if (op == V9X_R3D_TEXOP_ADD && !has_alpha) {
            state->desc.stage0 = V9X_HSW_PS_ADD;
        } else {
            return V9X_D3D_HSW_REFUSE_TEXOP;
        }
        state->textured = 1;
    }

    state->desc.specular = draw->specular_enable != 0ul ? 1u : 0u;
    if (draw->fog_enable != 0ul) {
        state->desc.fog = 1u;
        state->desc.fog_rgb[0] =
            v9x_d3d_hsw_float((draw->fog_color >> 16) & 0xfful);
        state->desc.fog_rgb[1] =
            v9x_d3d_hsw_float((draw->fog_color >> 8) & 0xfful);
        state->desc.fog_rgb[2] = v9x_d3d_hsw_float(draw->fog_color & 0xfful);
    }

    state->blend.enable = 0u;
    state->blend.color_function = 0u;
    state->blend.src_factor = 0u;
    state->blend.dst_factor = 0u;
    state->blend.independent_alpha = 0u;
    state->blend.alpha_function = 0u;
    state->blend.src_alpha_factor = 0u;
    state->blend.dst_alpha_factor = 0u;
    state->blend.write_disable = 0u;
    state->blend.alpha_test = 0u;
    state->blend.alpha_function_test = 0u;
    /* An explicit draw's mask, as BLEND_STATE's write disables (the depth
     * fill measured all four, F2b); Direct3D's draws state no mask. */
    if (draw->explicit_state != 0ul) {
        state->blend.write_disable =
            v9x_d3d_hsw_write_disable(draw->write_mask);
    }
    state->blend.write_disable |= target_disable;
    state->scissor_empty = !v9x_d3d_hsw_scissor(draw, state->scissor);
    if (draw->blend_enable != 0ul) {
        state->blend.src_factor = v9x_d3d_hsw_factor(draw->src_blend);
        state->blend.dst_factor = v9x_d3d_hsw_factor(draw->dst_blend);
        if (state->blend.src_factor == 0u || state->blend.dst_factor == 0u) {
            return V9X_D3D_HSW_REFUSE_BLEND;
        }
        state->blend.enable = 1u;
    }
    state->alpha_ref = 0u;
    if (draw->alpha_test_enable != 0ul) {
        if (draw->alpha_func < V9X_R3D_CMP_NEVER ||
            draw->alpha_func > V9X_R3D_CMP_ALWAYS) {
            return V9X_D3D_HSW_REFUSE_ALPHA;
        }
        state->blend.alpha_test = 1u;
        state->blend.alpha_function_test =
            v9x_d3d_hsw_compare(draw->alpha_func);
        state->alpha_ref = (v9x_u8)(draw->alpha_ref & 0xfful);
    }
    return V9X_D3D_HSW_REFUSE_NONE;
}

static void v9x_d3d_hsw_count_refusal(DWORD reason)
{
    V9X_D3D_DIAGNOSTICS *diagnostics = &v9x_hal->d3d_diagnostics;

    diagnostics->m64_policy_last = reason;
    if (reason < 20ul) {
        ++diagnostics->m64_policy_counts[reason];
    }
}

/* A kernel's slot is a fixed function of its description, so every
 * process writes the same words to the same place: six stage 0s, two stage
 * 1s, specular and fog are 48 distinct slots of the 64. */
static DWORD v9x_d3d_hsw_kernel_slot(const struct v9x_hsw_ps_desc *desc)
{
    return ((DWORD)desc->stage0 * 8ul + (DWORD)desc->stage1 * 4ul +
            (DWORD)desc->specular * 2ul + (DWORD)desc->fog) %
           V9X_INTEL_HSW_KERNEL_SLOTS;
}

/* The last 128 bytes of a slot stay clear for the EU's prefetch past a
 * kernel's end (Vol 7). */
#define V9X_D3D_HSW_KERNEL_DWORDS \
    ((V9X_INTEL_HSW_KERNEL_SLOT - 128ul) / 4ul)

/*
 * Where a kernel is assembled before it goes to its slot: ordinary cached
 * memory. The EU encoder ORs each instruction's fields into its output
 * (hsw_eu.c), and built in place that is an uncached read and write of the
 * aperture per field - the aperture is UC by the BIOS's MTRR over
 * E0000000-FFFFFFFF (V9XBOOT.INI, boot 20). Measured on the 4570 (boot 21):
 * 16.1 us a batch in place, about 210 ns per dword produced, against 48 ns
 * per dword for the plain vertex copy. Per process, as the DLL's data is;
 * HAL callbacks run under the Win16 lock, so one draw at a time.
 */
static v9x_u32 v9x_d3d_hsw_kernel_words[V9X_D3D_HSW_KERNEL_DWORDS];

/* The kernel for a description, built into its slot; its offset in the
 * state. */
static int v9x_d3d_hsw_write_kernel(const struct v9x_hsw_ps_desc *desc,
                                    DWORD *kernel)
{
    v9x_u32 *image = v9x_d3d_hsw_reserve(V9X_INTEL_HSW_STATE_OFF);
    v9x_u32 *slot_words;
    DWORD slot;
    DWORD index;
    v9x_u32 used = 0ul;
    DWORD started = V9X_TIME_BEGIN();
    int built;

    slot = v9x_d3d_hsw_kernel_slot(desc);
    *kernel = V9X_INTEL_HSW_KERNEL_OFF + slot * V9X_INTEL_HSW_KERNEL_SLOT;
    built = v9x_intel_hsw_ps_kernel_build(v9x_d3d_hsw_kernel_words,
                                          V9X_D3D_HSW_KERNEL_DWORDS, desc,
                                          &used) == V9X_STATUS_OK;
    /* Only the words the builder produced, as building in place wrote. */
    if (built) {
        slot_words = image + *kernel / 4ul;
        for (index = 0ul; index < used; ++index) {
            slot_words[index] = v9x_d3d_hsw_kernel_words[index];
        }
    }
    /* Haswell times the kernel build in the Gen3 decode bucket (TimeDecode
     * in V9XTRACE): the per-batch cost split of 2026-10-18. */
    V9X_TIME_END(V9X_TIME_DECODE, started);
    return built;
}

/* The draw's state in the reserve: the target and texture surfaces, the
 * binding table, the sampler, blend, CC and depth-stencil state, the zeroed
 * border colour, and the kernel. */
static int v9x_d3d_hsw_write_state(const V9X_R3D_DRAW *draw,
                                   const V9X_D3D_HSW_STATE *state,
                                   DWORD *kernel)
{
    v9x_u32 *image = v9x_d3d_hsw_reserve(V9X_INTEL_HSW_STATE_OFF);
    v9x_u32 *table;
    const V9X_D3D_HSW_TEXTURE *texture;
    DWORD index;

    if (v9x_intel_hsw_surface_2d(image + V9X_HSW_DRAW_RT_SURFACE(0) / 4ul,
                                 state->target_format,
                                 v9x_d3d_hsw_vram(draw->target.offset),
                                 (v9x_u16)draw->target.width,
                                 (v9x_u16)draw->target.height,
                                 draw->target.pitch) != V9X_STATUS_OK) {
        return 0;
    }
    table = image + V9X_HSW_SHADE_BINDING_TABLE(0) / 4ul;
    table[0] = V9X_HSW_DRAW_RT_SURFACE(0);
    table[1] = V9X_D3D_HSW_TEXTURE_OFF;
    table[2] = V9X_D3D_HSW_TEXTURE_OFF;
    /* Unit n's surface at E's texture n, its sampler n after the first
     * (the kernel's TEX1 reads binding 2, sampler 1: hsw_eu.c). */
    for (index = 0ul; index < (DWORD)state->textured; ++index) {
        texture = &state->texture[index];
        if (v9x_intel_hsw_surface_2d(
                image + V9X_HSW_SHADE_TEX_SURFACE(index) / 4ul,
                texture->format, texture->address, texture->width,
                texture->height, texture->pitch) != V9X_STATUS_OK ||
            (texture->levels > 1u &&
             v9x_intel_hsw_surface_mips(
                 image + V9X_HSW_SHADE_TEX_SURFACE(index) / 4ul,
                 texture->levels) != V9X_STATUS_OK) ||
            v9x_intel_hsw_sampler(
                image + (V9X_HSW_SHADE_SAMPLER +
                         index * V9X_HSW_SAMPLER_BYTES) / 4ul,
                &texture->sampler) != V9X_STATUS_OK) {
            return 0;
        }
    }
    if (state->textured == 2) {
        table[2] = V9X_HSW_SHADE_TEX_SURFACE(1);
    }
    if (!state->textured) {
        /* An untextured kernel samples nothing, but the binding table still
         * names a surface: the target's. */
        table[1] = V9X_HSW_DRAW_RT_SURFACE(0);
        table[2] = V9X_HSW_DRAW_RT_SURFACE(0);
    }
    for (index = 0ul; index < V9X_D3D_HSW_BORDER_BYTES / 4ul; ++index) {
        image[V9X_HSW_SHADE_BORDER_COLOR / 4ul + index] = 0ul;
    }
    if (v9x_intel_hsw_blend(image + V9X_HSW_SHADE_BLEND_ALPHA_TEST / 4ul,
                            &state->blend) != V9X_STATUS_OK ||
        v9x_intel_hsw_cc_state(image + V9X_HSW_SHADE_CC / 4ul,
                               state->alpha_ref) != V9X_STATUS_OK) {
        return 0;
    }
    if (state->depth &&
        v9x_intel_hsw_depth_stencil(image + V9X_D3D_HSW_DS_OFF / 4ul, 1u,
                                    state->depth_function,
                                    state->depth_write) != V9X_STATUS_OK) {
        return 0;
    }
    return v9x_d3d_hsw_write_kernel(&state->desc, kernel);
}

static int v9x_d3d_hsw_accepts(const V9X_R3D_DRAW *draw)
{
    V9X_D3D_HSW_STATE state;

    if (draw == 0 || v9x_hal == 0) {
        return 0;
    }
    return v9x_d3d_hsw_map(draw, &state) == V9X_D3D_HSW_REFUSE_NONE;
}

static int v9x_d3d_hsw_draw_batch(const V9X_R3D_DRAW *draw,
                                  const V9X_R3D_VERTEX *vertices,
                                  DWORD triangle_count)
{
    V9X_D3D_HSW_STATE state;
    struct v9x_hsw_batch batch;
    struct v9x_hsw_draw d;
    DWORD reason;
    DWORD kernel = 0ul;
    DWORD done = 0ul;
    DWORD count;
    DWORD limit;
    DWORD bytes;
    DWORD index;
    DWORD vertex;
    DWORD timed;
    const v9x_u32 *source;
    const v9x_u32 *uv1;
    v9x_u32 *target;

    if (draw == 0 || vertices == 0 || triangle_count == 0ul ||
        !v9x_d3d_hsw_ready()) {
        return 0;
    }
    reason = v9x_d3d_hsw_map(draw, &state);
    if (reason != V9X_D3D_HSW_REFUSE_NONE) {
        v9x_d3d_hsw_count_refusal(reason);
        return 0;
    }
    /* A scissor with no pixel of the target in it: every triangle is drawn,
     * as nothing. */
    if (state.scissor_empty) {
        return 1;
    }
    if ((state.textured >= 1 && state.texture[0].levels > 1u) ||
        (state.textured == 2 && state.texture[1].levels > 1u)) {
        ++v9x_hal->d3d_diagnostics.mip_draws;
    }

    /* The blitter may still be writing this draw's texture or target, and
     * the previous draw must be done before its state is overwritten. */
    if (!v9x_engine32_hsw.wait_idle(1) || !v9x_d3d_hsw_wait_rcs()) {
        return 0;
    }
    /* The per-batch cost split (2026-10-18), in Gen3 buckets V9XTRACE
     * names: the whole state write in RingSpaceWait (the kernel inside it
     * also in Decode), the vertex copy in RingWrite. */
    timed = V9X_TIME_BEGIN();
    if (!v9x_d3d_hsw_write_state(draw, &state, &kernel)) {
        return 0;
    }
    V9X_TIME_END(V9X_TIME_RING_SPACE_WAIT, timed);

    d.binding_table = V9X_HSW_SHADE_BINDING_TABLE(0);
    d.kernel = kernel;
    d.vertices = V9X_D3D_HSW_GFX(V9X_INTEL_HSW_VERTEX_OFF);
    d.width = (v9x_u16)draw->target.width;
    d.height = (v9x_u16)draw->target.height;
    d.attributes = V9X_HSW_SHADE_ATTRIBUTES;
    d.blend = V9X_HSW_SHADE_BLEND_ALPHA_TEST;
    d.cc = V9X_HSW_SHADE_CC;
    d.sampler = V9X_HSW_SHADE_SAMPLER;
    /* With a depth buffer, the batch emits it after Vol 2b's stall, flush
     * and stall, and the whole draw runs under the depth-stencil state;
     * without one the buffer is NULL. */
    d.depth_buffer = state.depth ? state.depth_address : 0ul;
    d.depth_clear = 0ul;
    d.depth_test = state.depth ? V9X_D3D_HSW_DS_OFF : 0ul;
    d.tl_vertices = 1u;
    d.flat = 0u;   /* the core copies the provoking vertex */
    d.depth_format = state.depth ? V9X_HSW_DEPTH_D16_UNORM : (v9x_u8)0u;
    d.depth_pitch = state.depth_pitch;
    d.depth_only = 0u;
    d.scissor = 1u;
    d.scissor_x0 = (v9x_u16)state.scissor[0];
    d.scissor_y0 = (v9x_u16)state.scissor[1];
    d.scissor_x1 = (v9x_u16)state.scissor[2];
    d.scissor_y1 = (v9x_u16)state.scissor[3];
    d.tl_uv1 = state.textured == 2 ? 1u : 0u;
    limit = d.tl_uv1 ? V9X_D3D_HSW_MAX_TRIANGLES2 : V9X_D3D_HSW_MAX_TRIANGLES;

    while (done < triangle_count) {
        count = triangle_count - done;
        if (count > limit) {
            count = limit;
        }
        if (done != 0ul && !v9x_d3d_hsw_wait_rcs()) {
            return 0;
        }
        source = (const v9x_u32 *)(vertices + done * 3ul);
        target = v9x_d3d_hsw_reserve(V9X_INTEL_HSW_VERTEX_OFF);
        timed = V9X_TIME_BEGIN();
        if (d.tl_uv1) {
            /* Each 32-byte vertex, then its tu1 tv1: the TL2 vertex. */
            uv1 = (const v9x_u32 *)(draw->texcoords1 + done * 3ul * 2ul);
            for (vertex = 0ul; vertex < count * 3ul; ++vertex) {
                for (index = 0ul; index < V9X_HSW_TL_VERTEX_BYTES / 4ul;
                     ++index) {
                    *target++ = *source++;
                }
                *target++ = *uv1++;
                *target++ = *uv1++;
            }
        } else {
            bytes = count * 3ul * V9X_HSW_TL_VERTEX_BYTES;
            for (index = 0ul; index < bytes / 4ul; ++index) {
                target[index] = source[index];
            }
        }
        V9X_TIME_END(V9X_TIME_RING_WRITE, timed);
        d.vertex_count = count * 3ul;
        v9x_intel_hsw_batch_init(
            &batch, v9x_d3d_hsw_reserve(V9X_INTEL_HSW_BATCH_OFF),
            V9X_INTEL_HSW_BATCH_BYTES / 4ul);
        if (v9x_intel_hsw_draw_workload(&batch, V9X_D3D_HSW_STATE_BASE,
                                        V9X_INTEL_HSW_STATE_BYTES, &d,
                                        V9X_D3D_HSW_CRUMB_GFX,
                                        done + count) != V9X_STATUS_OK ||
            !v9x_d3d_hsw_submit()) {
            return 0;
        }
        done += count;
    }
    v9x_present_note_submission();
    return 1;
}

/* The whole draw timed, as d3d_mga.c's is; its RCS waits are in the render
 * drain bucket as well. */
static int v9x_d3d_hsw_draw(const V9X_R3D_DRAW *draw,
                            const V9X_R3D_VERTEX *vertices,
                            DWORD triangle_count)
{
    DWORD started = V9X_TIME_BEGIN();
    int result = v9x_d3d_hsw_draw_batch(draw, vertices, triangle_count);

    V9X_TIME_END(V9X_TIME_ENGINE_DRAW, started);
    return result;
}

/* One TL vertex (V9X_HSW_TL_VERTEX_BYTES): x, y, z, rhw 1.0, then a black
 * diffuse and specular and zero texture coordinates. */
static v9x_u32 *v9x_d3d_hsw_fill_vertex(v9x_u32 *at, float x, float y,
                                        float z)
{
    union {
        float value;
        v9x_u32 bits;
    } convert;

    convert.value = x;
    at[0] = convert.bits;
    convert.value = y;
    at[1] = convert.bits;
    convert.value = z;
    at[2] = convert.bits;
    convert.value = 1.0f;
    at[3] = convert.bits;
    at[4] = 0ul;
    at[5] = 0ul;
    at[6] = 0ul;
    at[7] = 0ul;
    return at + V9X_HSW_TL_VERTEX_BYTES / 4u;
}

/*
 * A rectangle of a Z buffer at value / 65535: the depth-only draw the
 * /depth16 probe measured on the 4570 (Phase F2b, run 3). Two triangles
 * whose edges lie on the rectangle's, so with sampling at pixel centres
 * they cover exactly its pixels, under DEPTH_STENCIL ALWAYS with write; the
 * shader runs, and the blend state masks every colour channel. Its colour
 * target is the Z buffer's own memory, named as B5G6R5 at the same extent,
 * which nothing writes. Synchronous, after the blitter is idle.
 */
int v9x_d3d_hsw_depth_fill(V9X_DD_SURFACE_LCL *surface, const LONG *rect,
                           DWORD value, int wait)
{
    struct v9x_hsw_ps_desc desc;
    struct v9x_hsw_blend blend;
    struct v9x_hsw_batch batch;
    struct v9x_hsw_draw d;
    v9x_u32 *image;
    v9x_u32 *table;
    v9x_u32 *at;
    DWORD offset = 0ul;
    DWORD kernel = 0ul;
    DWORD width;
    DWORD height;
    DWORD pitch;
    float left;
    float top;
    float right;
    float bottom;
    float z;

    if (surface == 0 || rect == 0 || !v9x_d3d_hsw_ready() ||
        !v9x_d3d_hsw_depth_surface(surface, &offset)) {
        return V9X_BLT_DECLINED;
    }
    width = (DWORD)surface->lpGbl->wWidth;
    height = (DWORD)surface->lpGbl->wHeight;
    pitch = (DWORD)surface->lpGbl->lPitch;
    if (rect[0] < 0l || rect[1] < 0l || rect[2] <= rect[0] ||
        rect[3] <= rect[1] || (DWORD)rect[2] > width ||
        (DWORD)rect[3] > height) {
        return V9X_BLT_DECLINED;
    }
    if (!v9x_engine32_hsw.wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }
    if (!v9x_d3d_hsw_wait_rcs()) {
        return V9X_BLT_DECLINED;
    }

    image = v9x_d3d_hsw_reserve(V9X_INTEL_HSW_STATE_OFF);
    if (v9x_intel_hsw_surface_2d(image + V9X_HSW_DRAW_RT_SURFACE(0) / 4ul,
                                 V9X_HSW_FMT_B5G6R5_UNORM,
                                 v9x_d3d_hsw_vram(offset), (v9x_u16)width,
                                 (v9x_u16)height, pitch) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    table = image + V9X_HSW_SHADE_BINDING_TABLE(0) / 4ul;
    table[0] = V9X_HSW_DRAW_RT_SURFACE(0);
    table[1] = V9X_HSW_DRAW_RT_SURFACE(0);
    table[2] = V9X_HSW_DRAW_RT_SURFACE(0);

    blend.enable = 0u;
    blend.color_function = 0u;
    blend.src_factor = 0u;
    blend.dst_factor = 0u;
    blend.independent_alpha = 0u;
    blend.alpha_function = 0u;
    blend.src_alpha_factor = 0u;
    blend.dst_alpha_factor = 0u;
    blend.write_disable = 0xfu;
    blend.alpha_test = 0u;
    blend.alpha_function_test = 0u;
    desc.stage0 = V9X_HSW_PS_DIFFUSE;
    desc.stage1 = V9X_HSW_PS_STAGE1_NONE;
    desc.fog = 0u;
    desc.fog_rgb[0] = 0ul;
    desc.fog_rgb[1] = 0ul;
    desc.fog_rgb[2] = 0ul;
    desc.specular = 0u;
    /* COMPAREFUNCTION_ALWAYS is 0 (Vol 2a). */
    if (v9x_intel_hsw_blend(image + V9X_HSW_SHADE_BLEND_NO_COLOUR / 4ul,
                            &blend) != V9X_STATUS_OK ||
        v9x_intel_hsw_cc_state(image + V9X_HSW_SHADE_CC / 4ul, 0u) !=
            V9X_STATUS_OK ||
        v9x_intel_hsw_depth_stencil(image + V9X_D3D_HSW_DS_OFF / 4ul, 1u,
                                    0u, 1u) != V9X_STATUS_OK ||
        !v9x_d3d_hsw_write_kernel(&desc, &kernel)) {
        return V9X_BLT_DECLINED;
    }

    /* D16_UNORM stores round(z x 65535). */
    z = (float)(value & 0xfffful) / 65535.0f;
    left = (float)rect[0];
    top = (float)rect[1];
    right = (float)rect[2];
    bottom = (float)rect[3];
    at = v9x_d3d_hsw_reserve(V9X_INTEL_HSW_VERTEX_OFF);
    at = v9x_d3d_hsw_fill_vertex(at, left, top, z);
    at = v9x_d3d_hsw_fill_vertex(at, right, top, z);
    at = v9x_d3d_hsw_fill_vertex(at, left, bottom, z);
    at = v9x_d3d_hsw_fill_vertex(at, right, top, z);
    at = v9x_d3d_hsw_fill_vertex(at, right, bottom, z);
    (void)v9x_d3d_hsw_fill_vertex(at, left, bottom, z);

    d.binding_table = V9X_HSW_SHADE_BINDING_TABLE(0);
    d.kernel = kernel;
    d.vertices = V9X_D3D_HSW_GFX(V9X_INTEL_HSW_VERTEX_OFF);
    d.width = (v9x_u16)width;
    d.height = (v9x_u16)height;
    d.attributes = V9X_HSW_SHADE_ATTRIBUTES;
    d.blend = V9X_HSW_SHADE_BLEND_NO_COLOUR;
    d.cc = V9X_HSW_SHADE_CC;
    d.sampler = V9X_HSW_SHADE_SAMPLER;
    d.depth_buffer = v9x_d3d_hsw_vram(offset);
    d.depth_clear = 0ul;
    d.depth_test = V9X_D3D_HSW_DS_OFF;
    d.tl_vertices = 1u;
    d.vertex_count = V9X_D3D_HSW_FILL_VERTICES;
    d.flat = 0u;
    d.depth_format = V9X_HSW_DEPTH_D16_UNORM;
    d.depth_pitch = pitch;
    d.depth_only = 1u;
    d.scissor = 0u;
    d.tl_uv1 = 0u;
    v9x_intel_hsw_batch_init(&batch,
                             v9x_d3d_hsw_reserve(V9X_INTEL_HSW_BATCH_OFF),
                             V9X_INTEL_HSW_BATCH_BYTES / 4ul);
    if (v9x_intel_hsw_draw_workload(&batch, V9X_D3D_HSW_STATE_BASE,
                                    V9X_INTEL_HSW_STATE_BYTES, &d,
                                    V9X_D3D_HSW_CRUMB_GFX, 1ul) !=
            V9X_STATUS_OK ||
        !v9x_d3d_hsw_submit()) {
        return V9X_BLT_DECLINED;
    }
    return V9X_BLT_DONE;
}

/*
 * A Z buffer in whole Y tiles: pitch width x 2 rounded up to 128 bytes,
 * rows rounded up to 32, on a 4 KB boundary, so the last row of tiles the
 * engine reaches is the surface's own.
 */
static DWORD v9x_d3d_hsw_create_depth(V9X_DDHAL_CREATESURFACEDATA *data,
                                      V9X_DD_SURFACE_LCL *surface)
{
    v9x_u32 offsets[1];
    DWORD pitch;
    DWORD rows;
    DWORD base;

    if (surface->lpGbl->wWidth == 0u || surface->lpGbl->wHeight == 0u ||
        (DWORD)surface->lpGbl->wWidth >
            v9x_d3d_hsw_limits.target_dimension_max ||
        (DWORD)surface->lpGbl->wHeight >
            v9x_d3d_hsw_limits.target_dimension_max ||
        ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul &&
         surface->lpGbl->ddpfSurface.dwRGBBitCount != 16ul)) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    pitch = ((DWORD)surface->lpGbl->wWidth * 2ul + V9X_D3D_HSW_TILE_PITCH -
             1ul) & ~(V9X_D3D_HSW_TILE_PITCH - 1ul);
    rows = ((DWORD)surface->lpGbl->wHeight + V9X_D3D_HSW_TILE_ROWS - 1ul) &
           ~(V9X_D3D_HSW_TILE_ROWS - 1ul);
    offsets[0] = 0ul;
    if (v9x_d3d_place_block(data, V9X_D3D_HSW_TILE_BYTES, pitch, rows,
                            offsets, &base) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    ++v9x_hal->d3d_diagnostics.z_placed;
    v9x_hal->d3d_diagnostics.z_placed_pitch = pitch;
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

/*
 * Mip trees. RENDER_SURFACE_STATE has one base and one pitch for a whole
 * chain, and the sampler finds each level by the layout v9x_intel_hsw_miptree
 * computes. DirectDraw's heap places each level as a surface of its own,
 * which is never that layout, so a chain is placed here instead: one block
 * for the tree (d3d_place.c), on the 4 KB boundary the Z buffers use so
 * one release rule frees both, and each level's fpVidMem at its place in
 * it with the tree's pitch. Gen3's create_surface does the same for its
 * own layout. Declines are counted with Gen3's reasons.
 */
#define V9X_D3D_HSW_MIPTREE_SHAPE  1ul  /* not a halving chain */
#define V9X_D3D_HSW_MIPTREE_FORMAT 2ul  /* not 16 bits per texel */

static DWORD v9x_d3d_hsw_miptree_decline(DWORD reason)
{
    ++v9x_hal->d3d_diagnostics.mip_tree_declined;
    v9x_hal->d3d_diagnostics.mip_tree_declined_last = reason;
    return V9X_DDHAL_DRIVER_NOTHANDLED;
}

static DWORD v9x_d3d_hsw_create_miptree(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL **list = (V9X_DD_SURFACE_LCL **)data->lplpSList;
    V9X_DD_SURFACE_LCL *top = list[0];
    const V9X_DDPIXELFORMAT *pixel;
    struct v9x_hsw_miptree tree;
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
        width > v9x_d3d_hsw_limits.texture_size_max ||
        height > v9x_d3d_hsw_limits.texture_size_max ||
        data->dwSCnt > V9X_HSW_MIP_LEVELS_MAX) {
        return v9x_d3d_hsw_miptree_decline(V9X_D3D_HSW_MIPTREE_SHAPE);
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
            return v9x_d3d_hsw_miptree_decline(V9X_D3D_HSW_MIPTREE_SHAPE);
        }
    }
    /* Sixteen bits a texel: every format the sampler takes here (565, 1555,
     * 4444), and the two bytes the layout is computed in. No format of its
     * own means the display's. */
    pixel = (top->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul
        ? &top->lpGbl->ddpfSurface
        : &v9x_hal->info.vmiData.ddpfDisplay;
    if ((pixel->dwFlags & V9X_DDPF_RGB) == 0ul ||
        pixel->dwRGBBitCount != 16ul) {
        return v9x_d3d_hsw_miptree_decline(V9X_D3D_HSW_MIPTREE_FORMAT);
    }
    if (v9x_intel_hsw_miptree((v9x_u16)width, (v9x_u16)height,
                              (v9x_u8)data->dwSCnt, 2u, &tree) !=
            V9X_STATUS_OK) {
        return v9x_d3d_hsw_miptree_decline(V9X_D3D_HSW_MIPTREE_SHAPE);
    }
    reason = v9x_d3d_place_block(data, V9X_D3D_HSW_TILE_BYTES, tree.pitch,
                                 tree.rows, tree.level_offset, &base);
    if (reason != 0ul) {
        return v9x_d3d_hsw_miptree_decline(reason);
    }
    ++v9x_hal->d3d_diagnostics.mip_tree_allocs;
    v9x_hal->d3d_diagnostics.mip_tree_last_offset = base;
    v9x_hal->d3d_diagnostics.mip_tree_last_shape =
        (tree.pitch << 16) | (tree.rows & 0xfffful);
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

/* A lone video-memory Z buffer, or a video-memory mip chain; every other
 * surface is DirectDraw's. */
static DWORD v9x_d3d_hsw_create_surface(V9X_DDHAL_CREATESURFACEDATA *data)
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
        return v9x_d3d_hsw_create_depth(data, surface);
    }
    if (data->dwSCnt >= 2ul &&
        (surface->ddsCaps & (V9X_DDSCAPS_TEXTURE | V9X_DDSCAPS_MIPMAP)) ==
            (V9X_DDSCAPS_TEXTURE | V9X_DDSCAPS_MIPMAP)) {
        return v9x_d3d_hsw_create_miptree(data);
    }
    return V9X_DDHAL_DRIVER_NOTHANDLED;
}

/* The top of a tree or a Z buffer placed above is going: free the block
 * (d3d_place.c ignores every other surface). */
static void v9x_d3d_hsw_destroy_surface(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
    if (v9x_d3d_place_release(data, V9X_D3D_HSW_TILE_BYTES)) {
        ++v9x_hal->d3d_diagnostics.mip_tree_frees;
    }
}

/*
 * What the mapping takes: Gouraud and the core's flat; texture DECAL, COPY,
 * MODULATE (with Direct3D's alpha rule) and MODULATEALPHA, published, and
 * ADD and SRCALPHASAT, mapped but not published (the DDK headers this tree
 * carries define neither caps bit); specular;
 * vertex fog; every blend factor but the destination alpha ones (a 16-bit
 * target has no useful alpha); the alpha test with every function; WRAP,
 * MIRROR, CLAMP; a 16-bit Z buffer with every comparison (F2b). Mip
 * filters are published; a chain create_surface placed samples its levels,
 * any other texture level 0.
 */
static void v9x_d3d_hsw_describe_caps(V9X_DD_SHARED *shared)
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
                         V9X_D3DPTEXTURECAPS_ALPHA;
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
        v9x_d3d_hsw_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureWidth =
        v9x_d3d_hsw_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinTextureHeight =
        v9x_d3d_hsw_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureHeight =
        v9x_d3d_hsw_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMaxStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMinStippleHeight = 0ul;
    shared->d3d_extended_caps.dwMaxStippleHeight = 0ul;
    /* 16 and 32 bpp targets (v9x_d3d_hsw_target); depth D16 at both. */
    shared->d3d_global.hwCaps.dwDeviceRenderBitDepth =
        V9X_DDBD_16 | V9X_DDBD_32;
    shared->d3d_global.hwCaps.dwDeviceZBufferBitDepth = V9X_DDBD_16;

    /* RGB565, ARGB1555 and ARGB4444: the shared block has three slots. */
    for (index = 0ul; index < 3ul; ++index) {
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
    shared->d3d_global.lpTextureFormats = &shared->texture_formats[0];
    shared->d3d_global.dwNumTextureFormats = 3ul;
    shared->d3d_global.dwNumVertices = 0ul;
    shared->d3d_global.dwNumClipVertices = 0ul;
}

/* Positional: V9X_D3D_ENGINE_OPS is append-only (d3d_internal.h). */
const V9X_D3D_ENGINE_OPS v9x_d3d_engine_hsw = {
    &v9x_d3d_hsw_limits,
    v9x_d3d_hsw_texture_format,
    v9x_d3d_hsw_describe_caps,
    0,                                  /* draw_triangles: draw serves */
    v9x_d3d_hsw_ready,
    v9x_d3d_hsw_create_surface,         /* Z buffers in Y tiles */
    v9x_d3d_hsw_destroy_surface,
    v9x_d3d_hsw_draw,
    v9x_d3d_hsw_accepts
};
