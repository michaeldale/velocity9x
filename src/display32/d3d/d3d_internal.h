/*
 * The Direct3D core/engine boundary.
 *
 * src\display32\d3d\d3d_core.c holds everything about this driver's Direct3D
 * that is not about a particular chip: the context pool, the texture handle
 * table, render-state bookkeeping, the software clipper, and the DDHAL entry
 * points DDRAW calls. src\display32\d3d\d3d_virge.c holds the ViRGE S3D
 * engine behind V9X_D3D_ENGINE_OPS below, and is the only file in the D3D
 * path that writes a hardware register.
 *
 * This split mirrors what src\display32\engines already does for 2D:
 * ddhal_core.c's v9x_engine32() picks a V9X_ENGINE32_OPS on
 * engine.engine_type, and v9x_d3d_engine() picks a V9X_D3D_ENGINE_OPS the
 * same way. One HAL binary carries every engine and every family links it, so
 * the selector is what keeps a non-D3D chip out of code written for a chip it
 * is not.
 *
 * docs\decisions\2026-08-29-d3d-core-engine-split.md records why the seam is
 * where it is, the two rules a second engine has to hold to, and the evidence
 * that the split changed no behaviour.
 */
#ifndef VELOCITY9X_D3D_INTERNAL_H
#define VELOCITY9X_D3D_INTERNAL_H

#include "ddhal_internal.h"

#define V9X_D3D_CONTEXT_COUNT 16u
#define V9X_D3D_TEXTURE_COUNT 256u

/*
 * The largest triangle count the core will accept in one RenderPrimitive
 * instruction. A core limit, not an engine one: it bounds what an execute
 * buffer can ask for before any engine is consulted.
 */
/*
 * The most triangles one RenderPrimitive instruction can carry: wCount is a
 * WORD, so this is the runtime's own ceiling and the core no longer tests it
 * - an empty instruction is the only count it refuses. The previous value,
 * 256, bounded nothing - the loop indexes the triangle records by bSize and
 * clips each on its own - and quietly dropped every instruction above it.
 * Final Reality's 3D scene on the emulated ViRGE lost its larger meshes whole
 * to that on 2026-09-03, drawn as the clear colour, with no reject in the
 * trace because none was pushed. Kept as documentation of the bound.
 */
#define V9X_D3D_MAX_BATCH_TRIANGLES 65535u

/*
 * The most triangles one clipped input triangle can fan into. The clipper
 * emits at most eight vertices against four edges, and a fan of n vertices is
 * n - 2 triangles, so six is the bound and the core's batch buffer is sized
 * to it.
 */
#define V9X_D3D_MAX_FAN_TRIANGLES 6u

/*
 * The most triangles the core hands an engine in one call.
 *
 * Every path that converts a primitive into a triangle list chunks at this:
 * the indexed path, DrawOnePrimitive's long lists, and the fan gather in
 * DrawPrimitives. A record-shaped list is bounded separately by
 * DrawPrimitives' own 192-vertex cap, which is the same 64 triangles.
 *
 * IT IS NOT A FREE PARAMETER. The Gen3 engine refuses a batch past
 * V9X_I9XX_RUNTIME_MAX_TRIANGLES, which is also 64, and its vertex arrays
 * hold 192. Raising this without raising those makes every Intel batch
 * refuse - and since 2026-09-20 a refused batch reports DD_OK, so it would
 * show up as geometry quietly missing from the picture rather than as an
 * error. d3d_i9xx.c asserts the relationship at compile time so that cannot
 * happen silently; this comment is here so the next person knows why the
 * assertion exists before they trip it.
 */
#define V9X_D3D_INDEXED_BATCH 64u

typedef struct v9x_d3d_context {
    DWORD active;
    DWORD pid;
    V9X_DD_SURFACE_LCL *target;
    V9X_DD_SURFACE_LCL *zbuffer;
    DWORD target_offset;
    DWORD pitch;
    DWORD width;
    DWORD height;
    /*
     * The render target's channel layout, as one of the two values below.
     *
     * Recorded rather than assumed because a 16 bpp desktop can be 5:6:5 or
     * 5:5:5, and on S3D silicon the difference decides whether hardware
     * Direct3D puts its colours in the right channels at all - the triangle
     * engine has no RGB565 destination format
     * (docs\decisions\2026-09-02-s3d-writes-1555-because-it-can-only-write-1555.md).
     * The software engine reads it; the ViRGE engine cannot act on it and does
     * not look.
     */
    DWORD target_format;
    /*
     * The depth surface, once validated. depth_offset and depth_pitch were
     * computed and thrown away before the Z path existed; the engine needs
     * both, and neither is recoverable from the surface pointer without
     * repeating the validation.
     *
     * z_enable and z_write start life from whether a Z surface was attached
     * at all, matching the DDK's SetRenderTarget32 (D3DCB2.C:57-66), and are
     * then owned by the render states.
     */
    DWORD depth_offset;
    DWORD depth_pitch;
    DWORD z_enable;
    DWORD z_write;
    DWORD z_func;
    DWORD specular_enable;
    DWORD fog_enable;
    DWORD fog_color;
    DWORD alpha_blend_enable;
    DWORD src_blend;
    DWORD dest_blend;
    DWORD texture_handle;
    DWORD texture_min;
    DWORD texture_mag;
    DWORD texture_blend;
    DWORD texture_wrap;
    /*
     * D3DTADDRESS_*, from D3DRENDERSTATE_TEXTUREADDRESS and its two per-axis
     * forms, and distinct from texture_wrap above - which carries
     * D3DRENDERSTATE_WRAPU/WRAPV, a different render state about how a
     * coordinate is interpolated rather than about what happens past its end.
     * One field for both axes: D3DPTADDRESSCAPS_INDEPENDENTUV is claimed by
     * neither engine, so the last of the two to be set wins, which is what a
     * driver without independent axes must do.
     */
    DWORD texture_address;
    DWORD texture_border;
    /*
     * D3DRENDERSTATE_WRAPU and WRAPV, one field each. texture_wrap above is
     * the ViRGE's "either is set" and stays as it was; an engine that can wrap
     * each axis on its own - the Intel S3 wrap-shortest bits - needs to know
     * which. Direct3D's default for both is FALSE.
     */
    DWORD wrap_u;
    DWORD wrap_v;
    /*
     * D3DRENDERSTATE_SHADEMODE. FLAT is honoured in the core, before any
     * engine sees the triangle, by copying the first vertex's colour and
     * specular to the other two - which is Direct3D's definition (the first
     * vertex provokes) and needs no engine to know a provoking-vertex
     * register. PHONG is drawn as GOURAUD, which is what every Direct3D
     * runtime did too.
     */
    DWORD shade_mode;
    /*
     * D3DRENDERSTATE_CULLMODE, one of the V9X_D3DCULL values in d3d_cull.h.
     * Like FLAT it is honoured in the core, before any engine sees the
     * triangle - but only for an engine that advertises the matching
     * D3DPMISCCAPS bit (v9x_d3d_cull_honoured). Direct3D's default is CCW.
     */
    DWORD cull_mode;
    /*
     * D3DRENDERSTATE_COLORKEYENABLE. The key itself lives in the HAL's
     * per-surface table (v9x_d3d_color_key_find); this only says whether the
     * application wants it applied.
     */
    DWORD color_key_enable;
    /*
     * V9X_D3DRENDERSTATE_V9X_ALPHAFORCE: the instrument that puts all four
     * encodings of the command word's alpha field through one draw. Zero -
     * what every application leaves it at - means the engine chooses, which
     * is the shipping behaviour.
     */
    DWORD alpha_force;
    /*
     * D3DRENDERSTATE_ALPHATESTENABLE, ALPHAFUNC and ALPHAREF, recorded so
     * an engine can see them. The S3D unit has no alpha test and does not
     * draw one; it counts the triangles that asked. Direct3D's defaults:
     * off, ALWAYS, 0.
     */
    DWORD alpha_test_enable;
    DWORD alpha_func;
    DWORD alpha_ref;
} V9X_D3D_CONTEXT;

/*
 * The render-target layouts the core recognises.
 *
 * The core's vocabulary and not the software rasterizer's, because
 * d3d_raster.h is that engine's private leaf header and this file is shared
 * with an engine that has no rasterizer at all. The values are chosen equal to
 * V9X_D3D_RASTER_PIXFMT_*, so d3d_soft.c passes the field through untranslated
 * and asserts the equality at compile time - the same arrangement the filter,
 * blend and comparison constants already use.
 */
#define V9X_D3D_TARGET_FORMAT_RGB565   1ul
#define V9X_D3D_TARGET_FORMAT_XRGB1555 2ul

/*
 * A texture handle is the address of one of these, and it is valid for the
 * context that created it and no other.
 *
 * That pairing was suspected of losing every texture at a render-target
 * switch, because this runtime performs the switch by destroying the context.
 * It does not: the runtime destroys its texture handles along with the
 * context and re-creates them on the next GetHandle, so the pairing is
 * exactly right and an application that caches handle values across a switch
 * is holding values the runtime has retired
 * (docs\issues\2026-09-10-a-target-switch-loses-every-texture.md).
 */
typedef struct v9x_d3d_texture {
    DWORD active;
    DWORD context;
    void *surface;
    /*
     * The surface's LOCAL half, resolved ONCE when the texture is created.
     *
     * `surface` is the interface wrapper the runtime handed over, and until
     * 2026-09-17 the only way to get from it to the local object was to
     * dereference it - which every scan over this table did, long after the
     * surface it names may have been destroyed. intel55 measured that: two
     * pointer refusals, both from the teardown scan and from nowhere else,
     * on a machine where the guard had just been added to stop the HAL dying
     * on them.
     *
     * The consequence was worse than the fault. The comparison in
     * v9x_d3d_textures_forget_surface reads the wrapper to decide whether
     * this entry names the surface being destroyed; a wrapper that is gone
     * answers "no", so the entry is NEVER cleared and the stale pointer
     * stays in the table for the next scan to read again.
     *
     * Resolved at creation and compared as a value afterwards, so a scan
     * dereferences nothing. A stale value compares unequal, which is
     * harmless; a stale pointer dereferenced is not.
     */
    V9X_DD_SURFACE_LCL *lcl;
} V9X_D3D_TEXTURE;

/*
 * The engine's constraints, as data.
 *
 * Every one of these was a literal inside a core routine before the split,
 * and every one of them is a property of the S3D unit rather than of Direct3D
 * - the render target must be 16 bpp, its pitch is an 8-byte-aligned field
 * with a 0FF8h ceiling, coordinates go through a 12.20 fixed-point converter
 * that overflows outside the guard band, and the sampler takes only square
 * power-of-two textures in a bounded size range. A second engine changes
 * these numbers and nothing else, which is why they are a struct and not four
 * more function pointers.
 */
typedef struct v9x_d3d_engine_limits {
    /* The only framebuffer depth the engine renders to. */
    DWORD target_bits_per_pixel;
    /* Inclusive maximum render-target pitch in bytes, and its alignment. */
    DWORD target_pitch_max;
    DWORD target_pitch_align;
    /* Inclusive maximum render-target width and height in pixels. */
    DWORD target_dimension_max;
    /* Inclusive square-texture edge bounds in texels. */
    DWORD texture_size_min;
    DWORD texture_size_max;
    /*
     * Screen coordinates outside +/- this are refused before clipping. The
     * clipper's output is fed to the engine's fixed-point conversion, so a
     * vertex beyond the converter's range has to be rejected rather than
     * wrapped.
     */
    float coordinate_limit;
    /*
     * Bits per pixel in the depth buffer. The core sizes and bounds-checks
     * the Z surface, so it needs the number - and the number is the engine's.
     * It was a literal 2 in the core's footprint arithmetic, which is a ViRGE
     * fact in the chip-neutral file that check-tree cannot catch, because that
     * rule forbids chip *names* and this was a bare constant.
     */
    DWORD depth_bits_per_pixel;
    /*
     * What a texture surface's offset must be a multiple of, which the HAL
     * publishes to DirectDraw as vmiData.dwTextureAlign so that the surfaces
     * it hands back are ones this engine can bind.
     *
     * It was a flat 8 in ddhal_core.c - the ViRGE's requirement, stated once
     * for every chip because there was one chip that sampled. Gen3's
     * MAP_STATE address is page aligned, so an 8-aligned texture was refused
     * at bind time and its triangles drew untextured: a promise DirectDraw
     * had been told it could keep, and could not.
     *
     * Zero means the engine has no requirement of its own and the core's
     * default stands, which is what an engine that samples nothing wants.
     */
    DWORD texture_align;
    /*
     * Whether the core must clip a triangle to the render target before this
     * engine sees it, on every draw path and not only the execute-buffer one.
     *
     * Non-zero for an engine that cannot draw a vertex off the target: the
     * S3D unit's emitter declines the whole triangle, and the CPU rasterizer
     * clamps the vertex, which moves the edge. Zero for an engine with
     * a hardware guard band that has been measured drawing unclipped
     * geometry correctly, where clipping in the core would only cost time.
     */
    DWORD clip_in_core;
    /*
     * APPEND ONLY, and the reason is not style. The initialisers below are
     * positional - C89 has no designated form - and every member is an
     * arithmetic type, so inserting a field in the middle silently reassigns
     * every value after it and the compiler says nothing.
     *
     * That is not hypothetical: depth_bits_per_pixel was first added here
     * above coordinate_limit while its value was appended at the end of the
     * initialiser. coordinate_limit became 16.0f, the software clipper then
     * refused every vertex beyond sixteen pixels, and all Direct3D rendering
     * went black with every HRESULT still reporting success.
     */
} V9X_D3D_ENGINE_LIMITS;

/*
 * One engine's implementation.
 *
 * draw_triangles is deliberately a batch entry point taking a triangle list,
 * never a single triangle and never anything at register level. The ViRGE is
 * an immediate-mode register engine and would be happy with either, but every
 * plausible next engine - 3dfx FIFO, ATI Rage setup, Intel ring/batch - is a
 * command-stream engine that needs to see a run of work to build one packet
 * from. Fixing the granularity here means that engine does not have to move
 * the seam.
 *
 * vertices holds triangle_count * 3 vertices, already colour-adjusted and
 * clipped by the core. The engine returns non-zero when every triangle was
 * emitted, zero on the first it could not; the core does not ask which.
 */
typedef struct v9x_d3d_engine_ops {
    const V9X_D3D_ENGINE_LIMITS *limits;

    /*
     * Whether this engine can sample the surface, and as which of its own
     * formats. The value written to format_out is opaque to the core: it is
     * carried back to draw_triangles through the context and means whatever
     * the engine's command register means by it.
     */
    int (*texture_format)(const V9X_DD_SURFACE_LCL *surface,
                          DWORD *format_out);

    /*
     * Fill the D3D device description and the texture-format list this engine
     * publishes. The core owns the callback tables around it, because those
     * are its own entry points.
     */
    void (*describe_caps)(V9X_DD_SHARED *shared);

    int (*draw_triangles)(V9X_D3D_CONTEXT *context,
                          const V9X_D3DTLVERTEX *vertices,
                          DWORD triangle_count);

    /*
     * Is this engine in a state to draw, right now?
     *
     * APPENDED, and for the same reason V9X_D3D_ENGINE_LIMITS is append-only:
     * the initialisers below are positional, so a member inserted above this
     * one silently reassigns every function pointer after it and the compiler
     * says nothing. A mis-set function pointer here is a jump to the wrong
     * code, not a wrong number.
     *
     * This exists because the three draw entry points in d3d_core.c used to
     * ask v9x_engine_status_validated() instead - a ViRGE 2D-engine question,
     * asked in the chip-neutral file, which resolves through a literal
     * engine_type == S3_VIRGE_DX test plus a mapped MMIO aperture. A Trio64
     * failed it even though its descriptor is valid, and a software engine
     * fails it by construction: no MMIO window, no FIFO, no status register.
     * The result would have been an engine that resolved, published caps,
     * accepted every call and drew nothing, with every HRESULT reporting
     * success.
     *
     * The ViRGE's implementation validates its engine and answers that same
     * question. The software engine answers yes.
     */
    int (*ready)(void);

    /*
     * Surface placement, APPENDED 2026-09-25 under the same rule as `ready`.
     * Null for an engine that leaves every surface to DirectDraw's heap,
     * which the positional initialisers get by omission.
     *
     * An engine whose sampler derives a mip chain's levels from one address
     * cannot use chains DirectDraw placed level by level; Gen3 is that
     * engine. create_surface returns V9X_DDHAL_DRIVER_HANDLED only when it
     * has given EVERY surface of the list its memory and pitch, and
     * V9X_DDHAL_DRIVER_NOTHANDLED, touching nothing, otherwise.
     * destroy_surface releases what create_surface placed, and must ignore a
     * surface it did not.
     */
    DWORD (*create_surface)(V9X_DDHAL_CREATESURFACEDATA *data);
    void (*destroy_surface)(V9X_DDHAL_DESTROYSURFACEDATA *data);
} V9X_D3D_ENGINE_OPS;

/* The engine for the chip this HAL was handed, or null when it has none. */
const V9X_D3D_ENGINE_OPS *v9x_d3d_engine(void);

/*
 * Bytes per depth-buffer pixel for this chip, or zero when it has no D3D
 * engine and therefore no depth buffers.
 *
 * For ddhal_core.c's DDBLT_DEPTHFILL, which has to know how wide a depth
 * pixel is to fill one and is a chip-neutral file. The number belongs to the
 * engine - depth_bits_per_pixel in the limits above - and this is how the 2D
 * side asks for it rather than writing a literal 2. That exact literal, in
 * that exact file, is what the comment on depth_bits_per_pixel is about: a
 * ViRGE fact in chip-neutral code that check-tree cannot catch, because the
 * rule it enforces forbids chip *names* and a bare constant has none.
 */
DWORD v9x_d3d_depth_bytes_per_pixel(void);

/* The ViRGE S3D engine, in d3d_virge.c. */
extern const V9X_D3D_ENGINE_OPS v9x_d3d_engine_virge;

/* The CPU rasterizer, in d3d_soft.c. Selected by capability rather than by
 * chip: it serves whatever silicon it is given, including none. */
extern const V9X_D3D_ENGINE_OPS v9x_d3d_engine_soft;

/*
 * The Intel Gen3 engine, in d3d_i9xx.c. Wired and NOT READY: it publishes no
 * caps and refuses every draw, because there is no 32-bit ring submission path
 * and because sustained 3D work is outside the errata authorisation. See that
 * file's header.
 */
extern const V9X_D3D_ENGINE_OPS v9x_d3d_engine_i9xx;


/*
 * The core services an engine may use.
 *
 * Exactly one, and it exists because the texture handle table is core state
 * while what makes a texture usable is an engine question: the engine asks
 * the core which surface the context has bound, then judges it.
 */
V9X_DD_SURFACE_LCL *v9x_d3d_context_texture_surface(
    const V9X_D3D_CONTEXT *context);

#endif
