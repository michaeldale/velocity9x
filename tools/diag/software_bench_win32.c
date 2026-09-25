/* CPU rasterizer and aperture timing, without installing a driver. The linked
 * rasterizer is the production translation unit; DirectDraw supplies scratch
 * surfaces only. This measures neither D3D dispatch nor presentation time. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"
#include "d3d_raster.h"

#define V9X_BENCH_WIDTH 320ul
#define V9X_BENCH_HEIGHT 240ul
#define V9X_BENCH_TEX_SIZE 64ul
#define V9X_BENCH_MS 750ul
#define V9X_BENCH_SAMPLES 3ul
#define V9X_BENCH_SECTION "SoftwareBench"

typedef HRESULT (WINAPI *V9X_BENCH_DDRAW_CREATE)(GUID *, LPDIRECTDRAW *, IUnknown *);

static void v9x_bench_uint(const char *key, DWORD value)
{
    char text[16];
    wsprintfA(text, "%lu", value);
    WritePrivateProfileStringA(V9X_BENCH_SECTION, key, text, V9X_DIAG_SOFTBENCH_INI);
}

static void v9x_bench_pump(void)
{
    MSG message;
    while (PeekMessageA(&message, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
}

static void v9x_bench_clear(const V9X_D3D_RASTER_TARGET *target,
                            const V9X_D3D_RASTER_DEPTH *depth)
{
    DWORD y;
    DWORD x;
    for (y = 0; y < target->height; ++y) {
        WORD *pixels = (WORD *)((BYTE *)target->pixels + y * target->pitch);
        WORD *z = (WORD *)((BYTE *)depth->pixels + y * depth->pitch);
        for (x = 0; x < target->width; ++x) {
            pixels[x] = 0x1234u;
            z[x] = 65535u;
        }
    }
}

static DWORD v9x_bench_hash(void *pixels, DWORD pitch, DWORD width, DWORD height)
{
    DWORD hash = 2166136261ul;
    DWORD y;
    DWORD x;
    for (y = 0; y < height; ++y) {
        WORD *row = (WORD *)((BYTE *)pixels + y * pitch);
        for (x = 0; x < width; ++x) {
            hash = (hash ^ (DWORD)row[x]) * 16777619ul;
        }
    }
    return hash;
}

static void v9x_bench_vertex(V9X_D3D_RASTER_VERTEX *v, LONG x, LONG y,
                             LONG red, LONG green, LONG blue)
{
    v->x = x;
    v->y = y;
    v->z = 32768l;
    v->u = x * 12l;
    v->v = y * 16l;
    v->red = red;
    v->green = green;
    v->blue = blue;
    v->alpha = 128l;
}

static int v9x_bench_frame(DWORD scene, const V9X_D3D_RASTER_TARGET *target,
                            const V9X_D3D_RASTER_DEPTH *depth,
                            V9X_D3D_RASTER_TEXTURE *texture)
{
    V9X_D3D_RASTER_VERTEX triangle[3];
    V9X_D3D_RASTER_ALPHA alpha;
    DWORD i;
    DWORD count = scene == 0ul ? 128ul : 1ul;
    alpha.src = V9X_D3D_RASTER_BLEND_SRC_SRCALPHA;
    alpha.dst = V9X_D3D_RASTER_BLEND_DST_INVSRCALPHA;
    texture->filter = scene == 2ul ? V9X_D3D_RASTER_FILTER_POINT :
                                    V9X_D3D_RASTER_FILTER_LINEAR;
    for (i = 0; i < count; ++i) {
        LONG x = scene == 0ul ? (LONG)(i % 16ul) * 304l : 0l;
        LONG y = scene == 0ul ? (LONG)(i / 16ul) * 448l : 0l;
        LONG width = scene == 0ul ? 192l : 5104l;
        LONG height = scene == 0ul ? 256l : 3824l;
        v9x_bench_vertex(&triangle[0], x + 3l, y + 1l, 255l, 32l, 160l);
        v9x_bench_vertex(&triangle[1], x + width, y + 9l, 16l, 240l, 64l);
        v9x_bench_vertex(&triangle[2], x + 17l, y + height, 128l, 0l, 255l);
        if (!v9x_d3d_raster_triangle(target, scene >= 4ul ? depth : 0, scene >= 2ul ? texture : 0, scene == 5ul ? &alpha : 0, 0, triangle)) {
            return 0;
        }
    }
    return 1;
}

static int v9x_bench_scenes(const char *location,
                            const V9X_D3D_RASTER_TARGET *target,
                            const V9X_D3D_RASTER_DEPTH *depth,
                            V9X_D3D_RASTER_TEXTURE *texture)
{
    static const char *names[] = {"Small", "Gouraud", "Point", "Bilinear", "Depth", "Alpha"};
    DWORD scene;
    DWORD sample;
    char key[80];
    for (scene = 0; scene < 6ul; ++scene) {
        for (sample = 0; sample < V9X_BENCH_SAMPLES; ++sample) {
            DWORD start;
            DWORD elapsed;
            DWORD frames = 0;
            v9x_bench_clear(target, depth);
            if (!v9x_bench_frame(scene, target, depth, texture)) {
                return 0;
            }
            start = GetTickCount();
            do {
                if (!v9x_bench_frame(scene, target, depth, texture)) {
                    return 0;
                }
                ++frames;
                v9x_bench_pump();
                elapsed = GetTickCount() - start;
            } while (elapsed < V9X_BENCH_MS && frames < 100000ul);
            wsprintfA(key, "%s_%s_%lu_Ms", location, names[scene], sample);
            v9x_bench_uint(key, elapsed);
            wsprintfA(key, "%s_%s_%lu_Frames", location, names[scene], sample);
            v9x_bench_uint(key, frames);
        }
        /* A single freshly cleared frame makes hashes independent of speed. */
        v9x_bench_clear(target, depth);
        if (!v9x_bench_frame(scene, target, depth, texture)) {
            return 0;
        }
        wsprintfA(key, "%s_%s_Hash", location, names[scene]);
        v9x_bench_uint(key, v9x_bench_hash(target->pixels, target->pitch,
                                          target->width, target->height));
        wsprintfA(key, "%s_%s_ZHash", location, names[scene]);
        v9x_bench_uint(key, v9x_bench_hash(depth->pixels, depth->pitch,
                                          target->width, target->height));
    }
    return 1;
}

static void v9x_bench_memory(const char *location, void *memory, DWORD pitch)
{
    DWORD operation;
    DWORD sample;
    char key[80];
    for (operation = 0; operation < 2ul; ++operation) {
        for (sample = 0; sample < V9X_BENCH_SAMPLES; ++sample) {
            DWORD start = GetTickCount();
            DWORD elapsed;
            DWORD passes = 0;
            DWORD sum = 0;
            do {
                DWORD y;
                DWORD x;
                for (y = 0; y < V9X_BENCH_HEIGHT; ++y) {
                    volatile WORD *row = (volatile WORD *)((BYTE *)memory + y * pitch);
                    for (x = 0; x < V9X_BENCH_WIDTH; ++x) {
                        if (operation == 0ul) {
                            sum += row[x];
                        } else {
                            row[x] = (WORD)(x + y);
                        }
                    }
                }
                ++passes;
                elapsed = GetTickCount() - start;
            } while (elapsed < V9X_BENCH_MS && passes < 100000ul);
            wsprintfA(key, "%s_%s_%lu_Ms", location, operation ? "Write" : "Read", sample);
            v9x_bench_uint(key, elapsed);
            wsprintfA(key, "%s_%s_%lu_Passes", location, operation ? "Write" : "Read", sample);
            v9x_bench_uint(key, passes);
            wsprintfA(key, "%s_%s_%lu_Sum", location, operation ? "Write" : "Read", sample);
            v9x_bench_uint(key, sum);
        }
    }
}

void __stdcall V9xSoftwareBenchEntry(void)
{
    HMODULE module;
    V9X_BENCH_DDRAW_CREATE create;
    LPDIRECTDRAW dd = 0;
    LPDIRECTDRAWSURFACE surfaces[3] = {0, 0, 0};
    DDSURFACEDESC locked[3];
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_DEPTH depth;
    V9X_D3D_RASTER_TEXTURE texture;
    void *ram[3];
    DWORD i;
    DWORD location;
    DWORD x;
    DWORD y;
    DWORD locked_count = 0;
    int ok = 1;
    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_DIAG_SOFTBENCH_INI);
    WritePrivateProfileStringA(V9X_BENCH_SECTION, "Build", V9X_BUILD_ID, V9X_DIAG_SOFTBENCH_INI);
    WritePrivateProfileStringA(V9X_BENCH_SECTION, "Result", "RUNNING", V9X_DIAG_SOFTBENCH_INI);
    v9x_bench_uint("SchemaVersion", 1ul);
    v9x_bench_uint("Width", V9X_BENCH_WIDTH);
    v9x_bench_uint("Height", V9X_BENCH_HEIGHT);
    v9x_bench_uint("SampleMinimumMs", V9X_BENCH_MS);
    for (i = 0; i < 3ul; ++i) {
        ram[i] = GlobalAlloc(GPTR, V9X_BENCH_WIDTH * V9X_BENCH_HEIGHT * 2ul);
        if (ram[i] == 0) {
            ExitProcess(2u);
        }
    }
    module = LoadLibraryA("DDRAW.DLL");
    create = module ? (V9X_BENCH_DDRAW_CREATE)GetProcAddress(module, "DirectDrawCreate") : 0;
    if (create && create(0, &dd, 0) == DD_OK &&
            IDirectDraw_SetCooperativeLevel(dd, GetDesktopWindow(), DDSCL_NORMAL) == DD_OK) {
        for (i = 0; i < 3ul; ++i) {
            DDSURFACEDESC desc;
            HRESULT hr;
            {
                unsigned int byte;
                for (byte = 0; byte < sizeof(desc); ++byte) {
                    ((unsigned char *)&desc)[byte] = 0;
                }
            }
            desc.dwSize = sizeof(desc);
            desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
            desc.dwWidth = V9X_BENCH_WIDTH;
            desc.dwHeight = V9X_BENCH_HEIGHT;
            desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
            hr = IDirectDraw_CreateSurface(dd, &desc, &surfaces[i], 0);
            if (hr != DD_OK) {
                break;
            }
            {
                unsigned int byte;
                for (byte = 0; byte < sizeof(locked[i]); ++byte) {
                    ((unsigned char *)&locked[i])[byte] = 0;
                }
            }
            locked[i].dwSize = sizeof(locked[i]);
            hr = IDirectDrawSurface_Lock(surfaces[i], 0, &locked[i], DDLOCK_WAIT, 0);
            if (hr != DD_OK) {
                break;
            }
            ++locked_count;
            if (locked[i].ddpfPixelFormat.dwRGBBitCount != 16ul ||
                    !(locked[i].ddsCaps.dwCaps & DDSCAPS_VIDEOMEMORY)) {
                break;
            }
        }
    }
    v9x_bench_uint("VramAvailable", locked_count == 3ul && i == 3ul ? 1ul : 0ul);
    for (location = 0; location < 2ul; ++location) {
        const char *name = location ? "VRAM" : "RAM";
        if (location && (locked_count != 3ul || i != 3ul)) {
            break;
        }
        target.pixels = location ? locked[0].lpSurface : ram[0];
        target.pitch = location ? (DWORD)locked[0].lPitch : V9X_BENCH_WIDTH * 2ul;
        target.width = V9X_BENCH_WIDTH;
        target.height = V9X_BENCH_HEIGHT;
        target.format = V9X_D3D_RASTER_PIXFMT_RGB565;
        depth.pixels = location ? locked[1].lpSurface : ram[1];
        depth.pitch = location ? (DWORD)locked[1].lPitch : target.pitch;
        depth.compare = V9X_D3D_RASTER_CMP_LESSEQUAL;
        depth.write = 1ul;
        texture.pixels = location ? locked[2].lpSurface : ram[2];
        texture.pitch = location ? (DWORD)locked[2].lPitch : target.pitch;
        texture.width = V9X_BENCH_TEX_SIZE;
        texture.height = V9X_BENCH_TEX_SIZE;
        texture.alpha = V9X_D3D_RASTER_TEXALPHA_IGNORE;
        texture.format = V9X_D3D_RASTER_TEXFMT_RGB565;
        texture.blend = V9X_D3D_RASTER_BLEND_MODULATE;
        texture.address = V9X_D3D_RASTER_ADDRESS_WRAP;
        for (y = 0; y < texture.height; ++y) {
            WORD *row = (WORD *)((BYTE *)texture.pixels + y * texture.pitch);
            for (x = 0; x < texture.width; ++x) {
                row[x] = (WORD)((x * 977ul + y * 619ul) & 65535ul);
            }
        }
        v9x_bench_clear(&target, &depth);
        v9x_bench_memory(name, target.pixels, target.pitch);
        if (!v9x_bench_scenes(name, &target, &depth, &texture)) {
            ok = 0;
        }
    }
    for (i = 0; i < 3ul; ++i) {
        if (i < locked_count) {
            IDirectDrawSurface_Unlock(surfaces[i], 0);
        }
        if (surfaces[i]) {
            IDirectDrawSurface_Release(surfaces[i]);
        }
        GlobalFree(ram[i]);
    }
    if (dd) {
        IDirectDraw_Release(dd);
    }
    if (module) {
        FreeLibrary(module);
    }
    WritePrivateProfileStringA(V9X_BENCH_SECTION, "Result", ok ? "PASS" : "FAIL", V9X_DIAG_SOFTBENCH_INI);
    WritePrivateProfileStringA(0, 0, 0, V9X_DIAG_SOFTBENCH_INI);
    ExitProcess(ok ? 0u : 1u);
}
