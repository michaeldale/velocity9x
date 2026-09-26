/*
 * Render interface probe (docs\plans\opengl-1.1-icd.md, Phase 3).
 *
 * Calls V9XHAL.DLL's V9xRenderInterface from an ordinary application, the
 * way the OpenGL ICD will, and records what each entry answers:
 *
 *   - whether the export is found in this process once DirectDraw has
 *     loaded the HAL, and whether version 1 negotiates;
 *   - describe: engine, generation, target formats, renderer string;
 *   - clear: a 64x64 target to red and its depth buffer to 0xFFFF, read
 *     back through DirectDraw Lock;
 *   - draw: a green quad at depth 0.5 over the whole target, a blue one at
 *     0.75 over the left half (rejected by LESSEQUAL), and a blue one at
 *     0.25 over the right half (accepted), then finish; the three pixels
 *     and depth words are read back and compared with exact values;
 *   - the refusals: a stale generation, a request built for another size,
 *     an oversized batch, a pointer that is not a surface, and a CPU
 *     texture, each of which must be refused with its own code and leave
 *     the target as it was.
 *
 * Every read is another Lock, so each step crosses the HAL's shared drain.
 * Writes C:\V9XDIAG\V9XR3DP.INI. Imports KERNEL32 and USER32 only.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"
#include "velocity9x/r3d_abi.h"

#define V9X_R3DP_SECTION "RenderInterfaceProbe"
#define V9X_R3DP_EDGE    64ul

typedef HRESULT (WINAPI *V9X_R3DP_DDRAW_CREATE)(GUID *, LPDIRECTDRAW *,
                                                IUnknown *);

static void v9x_r3dp_text(const char *key, const char *value)
{
    WritePrivateProfileStringA(V9X_R3DP_SECTION, key, value,
                               V9X_DIAG_R3DP_INI);
}

static void v9x_r3dp_hex(const char *key, DWORD value)
{
    char text[16];

    wsprintfA(text, "0x%08lX", value);
    v9x_r3dp_text(key, text);
}

static void v9x_r3dp_uint(const char *key, DWORD value)
{
    char text[16];

    wsprintfA(text, "%lu", value);
    v9x_r3dp_text(key, text);
}

static void v9x_r3dp_zero(void *block, DWORD bytes)
{
    DWORD i;

    for (i = 0ul; i < bytes; ++i) {
        ((BYTE *)block)[i] = 0u;
    }
}

/* A colour as the target stores it, from its channel masks. */
static DWORD v9x_r3dp_pack(const DDPIXELFORMAT *format, DWORD red,
                           DWORD green, DWORD blue)
{
    if (format->dwGBitMask == 0x000007e0ul) {
        return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3);
    }
    return ((red >> 3) << 10) | ((green >> 3) << 5) | (blue >> 3);
}

/* One 16-bit word at (x, y), read under Lock; 0xFFFFFFFF when it fails. */
static DWORD v9x_r3dp_read(LPDIRECTDRAWSURFACE surface, DWORD x, DWORD y)
{
    DDSURFACEDESC desc;
    DWORD value = 0xfffffffful;

    v9x_r3dp_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    if (IDirectDrawSurface_Lock(surface, 0, &desc, DDLOCK_WAIT, 0) != DD_OK) {
        return value;
    }
    value = ((const WORD *)((const BYTE *)desc.lpSurface +
                            y * (DWORD)desc.lPitch))[x];
    IDirectDrawSurface_Unlock(surface, 0);
    return value;
}

static void v9x_r3dp_vertex(V9X_R3D_ABI_VERTEX *vertex, float x, float y,
                            float z, DWORD color)
{
    vertex->sx = x;
    vertex->sy = y;
    vertex->sz = z;
    vertex->rhw = 1.0f;
    vertex->color = color;
    vertex->specular = 0xff000000ul;    /* unfogged, no specular */
    vertex->tu = 0.0f;
    vertex->tv = 0.0f;
}

/* Two triangles over [x0, x1) x [0, edge) at one depth and colour. */
static void v9x_r3dp_quad(V9X_R3D_ABI_VERTEX *vertices, float x0, float x1,
                          float z, DWORD color)
{
    float bottom = (float)V9X_R3DP_EDGE;

    v9x_r3dp_vertex(&vertices[0], x0, 0.0f, z, color);
    v9x_r3dp_vertex(&vertices[1], x1, 0.0f, z, color);
    v9x_r3dp_vertex(&vertices[2], x0, bottom, z, color);
    v9x_r3dp_vertex(&vertices[3], x1, 0.0f, z, color);
    v9x_r3dp_vertex(&vertices[4], x1, bottom, z, color);
    v9x_r3dp_vertex(&vertices[5], x0, bottom, z, color);
}

static void v9x_r3dp_draw_reset(V9X_R3D_ABI_DRAW *draw, DWORD generation,
                                LPDIRECTDRAWSURFACE target,
                                LPDIRECTDRAWSURFACE depth,
                                const V9X_R3D_ABI_VERTEX *vertices)
{
    v9x_r3dp_zero(draw, sizeof(*draw));
    draw->struct_bytes = sizeof(*draw);
    draw->generation = generation;
    draw->target.surface = target;
    draw->depth.surface = depth;
    draw->texture.storage = V9X_R3D_ABI_TEXTURE_NONE;
    draw->state.depth_enable = 1ul;
    draw->state.depth_write = 1ul;
    draw->state.depth_func = 4ul;           /* LESSEQUAL */
    draw->state.write_mask = V9X_R3D_ABI_WRITE_RGB;
    draw->state.scissor_right = V9X_R3DP_EDGE;
    draw->state.scissor_bottom = V9X_R3DP_EDGE;
    draw->vertices = vertices;
    draw->triangle_count = 2ul;
}

static LPDIRECTDRAWSURFACE v9x_r3dp_surface(LPDIRECTDRAW dd, DWORD caps,
                                            DWORD depth_bits,
                                            const char *key)
{
    DDSURFACEDESC desc;
    LPDIRECTDRAWSURFACE surface = 0;
    HRESULT hr;

    v9x_r3dp_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = V9X_R3DP_EDGE;
    desc.dwHeight = V9X_R3DP_EDGE;
    desc.ddsCaps.dwCaps = caps;
    if (depth_bits != 0ul) {
        desc.dwFlags |= DDSD_ZBUFFERBITDEPTH;
        desc.dwZBufferBitDepth = depth_bits;
    }
    hr = IDirectDraw_CreateSurface(dd, &desc, &surface, 0);
    v9x_r3dp_hex(key, (DWORD)hr);
    return hr == DD_OK ? surface : 0;
}

void __stdcall V9xRenderInterfaceProbeEntry(void)
{
    HMODULE module;
    HMODULE hal;
    V9X_R3DP_DDRAW_CREATE create;
    V9X_R3D_ENTRY_FN entry;
    const V9X_R3D_INTERFACE *iface = 0;
    LPDIRECTDRAW dd = 0;
    LPDIRECTDRAWSURFACE target = 0;
    LPDIRECTDRAWSURFACE depth = 0;
    DDSURFACEDESC desc;
    V9X_R3D_ABI_DESCRIBE describe;
    V9X_R3D_ABI_CLEAR clear;
    V9X_R3D_ABI_RECT whole;
    V9X_R3D_ABI_DRAW draw;
    V9X_R3D_ABI_OUTCOME outcome;
    V9X_R3D_ABI_LEVEL level;
    WORD texels[16];
    V9X_R3D_ABI_VERTEX quad[6];
    DWORD result;
    DWORD red, green, blue;
    DWORD before;
    int ok = 1;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_DIAG_R3DP_INI);
    v9x_r3dp_text("Build", V9X_BUILD_ID);
    v9x_r3dp_text("Result", "RUNNING");
    v9x_r3dp_uint("SchemaVersion", 1ul);

    module = LoadLibraryA("DDRAW.DLL");
    create = module ? (V9X_R3DP_DDRAW_CREATE)GetProcAddress(
                          module, "DirectDrawCreate") : 0;
    if (create == 0 || create(0, &dd, 0) != DD_OK) {
        v9x_r3dp_text("Result", "FAIL-DDRAW");
        ExitProcess(1u);
    }
    v9x_r3dp_hex("CoopHr",
                 (DWORD)IDirectDraw_SetCooperativeLevel(dd, 0, DDSCL_NORMAL));

    /*
     * Where the HAL is, from here. DirectDraw loads the 32-bit HAL DLL in
     * its helper process, so it need not be in this process's module list
     * even though its shared-arena image is callable from everywhere - the
     * first run of this probe found it absent. The fallback is the ICD's
     * own LoadLibrary, and the question it answers is whether that returns
     * the one shared instance DriverInit ran in (describe answers) or a
     * copy of its own (describe answers NOT_READY). HalBase says where it
     * landed; the image is linked at 0xB0400000.
     */
    hal = GetModuleHandleA("V9XHAL.DLL");
    v9x_r3dp_uint("HalLoaded", hal != 0 ? 1ul : 0ul);
    if (hal == 0) {
        hal = LoadLibraryA("V9XHAL.DLL");
        v9x_r3dp_uint("HalLoadLibrary", hal != 0 ? 1ul : 0ul);
    }
    v9x_r3dp_hex("HalBase", (DWORD)hal);
    entry = hal ? (V9X_R3D_ENTRY_FN)GetProcAddress(hal, "V9xRenderInterface")
                : 0;
    v9x_r3dp_uint("EntryFound", entry != 0 ? 1ul : 0ul);
    if (entry == 0) {
        v9x_r3dp_text("Result", "FAIL-NO-EXPORT");
        ExitProcess(1u);
    }
    v9x_r3dp_uint("NegotiateWrongVersion",
                  entry(V9X_R3D_ABI_VERSION + 1ul,
                        sizeof(V9X_R3D_INTERFACE)) == 0 ? 1ul : 0ul);
    v9x_r3dp_uint("NegotiateWrongSize",
                  entry(V9X_R3D_ABI_VERSION,
                        sizeof(V9X_R3D_INTERFACE) + 4ul) == 0 ? 1ul : 0ul);
    iface = entry(V9X_R3D_ABI_VERSION, sizeof(V9X_R3D_INTERFACE));
    v9x_r3dp_uint("Negotiated", iface != 0 ? 1ul : 0ul);
    if (iface == 0) {
        v9x_r3dp_text("Result", "FAIL-NEGOTIATE");
        ExitProcess(1u);
    }

    v9x_r3dp_zero(&describe, sizeof(describe));
    describe.struct_bytes = sizeof(describe);
    result = iface->describe(&describe);
    v9x_r3dp_uint("DescribeResult", result);
    v9x_r3dp_uint("DescribeEngine", describe.engine);
    v9x_r3dp_uint("DescribeGeneration", describe.generation);
    v9x_r3dp_hex("DescribeTargetFormats", describe.target_formats);
    v9x_r3dp_hex("DescribeTextureFormats", describe.texture_formats);
    v9x_r3dp_uint("DescribeTextureSizeMax", describe.texture_size_max);
    v9x_r3dp_uint("DescribeBatchMax", describe.batch_max);
    describe.renderer[sizeof(describe.renderer) - 1u] = '\0';
    v9x_r3dp_text("DescribeRenderer", describe.renderer);
    if (result != V9X_R3D_RESULT_OK) {
        v9x_r3dp_text("Result", "FAIL-DESCRIBE");
        ExitProcess(1u);
    }

    target = v9x_r3dp_surface(dd, DDSCAPS_OFFSCREENPLAIN |
                                  DDSCAPS_VIDEOMEMORY | DDSCAPS_3DDEVICE,
                              0ul, "TargetCreateHr");
    depth = v9x_r3dp_surface(dd, DDSCAPS_ZBUFFER | DDSCAPS_VIDEOMEMORY, 16ul,
                             "DepthCreateHr");
    if (target == 0 || depth == 0) {
        v9x_r3dp_text("Result", "FAIL-SURFACES");
        ExitProcess(1u);
    }
    v9x_r3dp_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    IDirectDrawSurface_GetSurfaceDesc(target, &desc);
    v9x_r3dp_hex("TargetGreenMask", desc.ddpfPixelFormat.dwGBitMask);
    red = v9x_r3dp_pack(&desc.ddpfPixelFormat, 255ul, 0ul, 0ul);
    green = v9x_r3dp_pack(&desc.ddpfPixelFormat, 0ul, 255ul, 0ul);
    blue = v9x_r3dp_pack(&desc.ddpfPixelFormat, 0ul, 0ul, 255ul);

    /* Clear: red, depth 0xFFFF, the whole target. */
    v9x_r3dp_zero(&clear, sizeof(clear));
    whole.left = 0ul;
    whole.top = 0ul;
    whole.right = V9X_R3DP_EDGE;
    whole.bottom = V9X_R3DP_EDGE;
    clear.struct_bytes = sizeof(clear);
    clear.generation = describe.generation;
    clear.target.surface = target;
    clear.depth.surface = depth;
    clear.clear_color = 1ul;
    clear.clear_depth = 1ul;
    clear.color_value = 0x00ff0000ul;
    clear.depth_value = 0xfffful;
    clear.write_mask = V9X_R3D_ABI_WRITE_RGB;
    clear.write_depth = 1ul;
    clear.rects = &whole;
    clear.rect_count = 1ul;
    result = iface->clear(&clear);
    v9x_r3dp_uint("ClearResult", result);
    v9x_r3dp_hex("ClearColorRaw", v9x_r3dp_read(target, 5ul, 5ul));
    v9x_r3dp_hex("ClearDepthRaw", v9x_r3dp_read(depth, 5ul, 5ul));
    v9x_r3dp_uint("ClearOk",
                  result == V9X_R3D_RESULT_OK &&
                  v9x_r3dp_read(target, 5ul, 5ul) == red &&
                  v9x_r3dp_read(depth, 5ul, 5ul) == 0xfffful ? 1ul : 0ul);

    /* Draw A: green at 0.5 over everything. */
    v9x_r3dp_quad(quad, 0.0f, (float)V9X_R3DP_EDGE, 0.5f, 0xff00ff00ul);
    v9x_r3dp_draw_reset(&draw, describe.generation, target, depth, quad);
    result = iface->draw(&draw, &outcome);
    v9x_r3dp_uint("DrawAResult", result);
    v9x_r3dp_uint("DrawASubmitted", outcome.submitted);
    /* Draw B: blue at 0.75 over the left half, behind A. */
    v9x_r3dp_quad(quad, 0.0f, (float)(V9X_R3DP_EDGE / 2ul), 0.75f,
                  0xff0000fful);
    result = iface->draw(&draw, &outcome);
    v9x_r3dp_uint("DrawBResult", result);
    /* Draw C: blue at 0.25 over the right half, in front of A. */
    v9x_r3dp_quad(quad, (float)(V9X_R3DP_EDGE / 2ul), (float)V9X_R3DP_EDGE,
                  0.25f, 0xff0000fful);
    result = iface->draw(&draw, &outcome);
    v9x_r3dp_uint("DrawCResult", result);
    result = iface->finish(describe.generation);
    v9x_r3dp_uint("FinishResult", result);
    v9x_r3dp_uint("FlushResult", iface->flush(describe.generation));

    v9x_r3dp_hex("LeftColorRaw", v9x_r3dp_read(target, 16ul, 32ul));
    v9x_r3dp_hex("LeftDepthRaw", v9x_r3dp_read(depth, 16ul, 32ul));
    v9x_r3dp_hex("RightColorRaw", v9x_r3dp_read(target, 48ul, 32ul));
    v9x_r3dp_hex("RightDepthRaw", v9x_r3dp_read(depth, 48ul, 32ul));
    v9x_r3dp_uint("DrawOk",
                  v9x_r3dp_read(target, 16ul, 32ul) == green &&
                  v9x_r3dp_read(target, 48ul, 32ul) == blue ? 1ul : 0ul);

    /* Refusals, each leaving the target as it was. */
    before = v9x_r3dp_read(target, 48ul, 32ul);
    v9x_r3dp_quad(quad, 0.0f, (float)V9X_R3DP_EDGE, 0.0f, 0xffffff00ul);
    v9x_r3dp_draw_reset(&draw, describe.generation + 1ul, target, depth,
                        quad);
    v9x_r3dp_uint("RefuseStale", iface->draw(&draw, &outcome));
    v9x_r3dp_draw_reset(&draw, describe.generation, target, depth, quad);
    draw.struct_bytes -= 4ul;
    v9x_r3dp_uint("RefuseAbi", iface->draw(&draw, &outcome));
    v9x_r3dp_draw_reset(&draw, describe.generation, target, depth, quad);
    draw.triangle_count = V9X_R3D_ABI_BATCH_MAX + 1ul;
    v9x_r3dp_uint("RefuseBatch", iface->draw(&draw, &outcome));
    v9x_r3dp_draw_reset(&draw, describe.generation, target, depth, quad);
    draw.target.surface = &level;           /* not a surface */
    v9x_r3dp_zero(&level, sizeof(level));
    v9x_r3dp_uint("RefuseNotSurface", iface->draw(&draw, &outcome));
    v9x_r3dp_draw_reset(&draw, describe.generation, target, depth, quad);
    {
        unsigned int i;

        for (i = 0u; i < 16u; ++i) {
            texels[i] = 0xF81Fu;        /* magenta in 565 */
        }
    }
    level.pixels = texels;
    level.bytes = sizeof(texels) - 2ul;     /* one texel short */
    level.pitch = 8ul;
    level.width = 4ul;
    level.height = 4ul;
    draw.texture.storage = V9X_R3D_ABI_TEXTURE_CPU;
    draw.texture.format = V9X_R3D_ABI_FORMAT_RGB565;
    draw.texture.levels = &level;
    draw.texture.level_count = 1ul;
    draw.texture.min_filter = V9X_R3D_ABI_FILTER_NEAREST;
    draw.texture.mag_filter = V9X_R3D_ABI_FILTER_NEAREST;
    draw.texture.mip = V9X_R3D_ABI_MIP_NONE;
    draw.texture.address = V9X_R3D_ABI_ADDRESS_WRAP;
    draw.texture.color_op = V9X_R3D_ABI_COLOROP_REPLACE;
    draw.texture.alpha_op = V9X_R3D_ABI_ALPHAOP_FRAGMENT;
    v9x_r3dp_uint("RefuseCpuTextureExtent", iface->draw(&draw, &outcome));
    v9x_r3dp_uint("RefusalsLeftTarget",
                  v9x_r3dp_read(target, 48ul, 32ul) == before ? 1ul : 0ul);

    /*
     * Explicit draws: a CPU texture, a scissor, a channel mask. The
     * software engine draws them; a hardware engine refuses them as
     * UNSUPPORTED in accepts() and leaves the target alone.
     *
     * D: the magenta texture, REPLACE, over the whole target at depth 0,
     * depth test off, scissored to the top half (surface rows 0..31): the
     * top turns magenta, the bottom keeps the blue of draw C.
     */
    level.bytes = sizeof(texels);
    draw.state.depth_enable = 0ul;
    draw.state.scissor_bottom = V9X_R3DP_EDGE / 2ul;
    result = iface->draw(&draw, &outcome);
    v9x_r3dp_uint("DrawTexturedScissorResult", result);
    v9x_r3dp_hex("TexturedTopRaw", v9x_r3dp_read(target, 48ul, 10ul));
    v9x_r3dp_hex("TexturedBottomRaw", v9x_r3dp_read(target, 48ul, 50ul));
    v9x_r3dp_uint("TexturedScissorOk",
                  result == V9X_R3D_RESULT_OK &&
                  v9x_r3dp_read(target, 48ul, 10ul) ==
                      v9x_r3dp_pack(&desc.ddpfPixelFormat, 255ul, 0ul, 255ul) &&
                  v9x_r3dp_read(target, 48ul, 50ul) == blue ? 1ul : 0ul);

    /* E: untextured white over everything, depth off, only red writable:
     * the green of draw A at (16, 50) becomes yellow. */
    v9x_r3dp_quad(quad, 0.0f, (float)V9X_R3DP_EDGE, 0.0f, 0xfffffffful);
    v9x_r3dp_draw_reset(&draw, describe.generation, target, depth, quad);
    draw.state.depth_enable = 0ul;
    draw.state.write_mask = V9X_R3D_ABI_WRITE_RED;
    result = iface->draw(&draw, &outcome);
    v9x_r3dp_uint("DrawMaskedResult", result);
    v9x_r3dp_hex("MaskedRaw", v9x_r3dp_read(target, 16ul, 50ul));
    v9x_r3dp_uint("MaskedOk",
                  result == V9X_R3D_RESULT_OK &&
                  v9x_r3dp_read(target, 16ul, 50ul) ==
                      v9x_r3dp_pack(&desc.ddpfPixelFormat, 255ul, 255ul, 0ul)
                      ? 1ul : 0ul);
    (void)iface->finish(describe.generation);

    IDirectDrawSurface_Release(depth);
    IDirectDrawSurface_Release(target);
    IDirectDraw_Release(dd);
    FreeLibrary(module);
    v9x_r3dp_text("Result", ok ? "COMPLETE" : "FAIL");
    ExitProcess(0u);
}
