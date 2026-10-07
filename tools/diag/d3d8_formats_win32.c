/*
 * What Direct3D 8 offers a game on this driver: display modes by format,
 * whether a HAL device can be made for 16- and 32-bit back buffers,
 * windowed and fullscreen, and which texture, render-target and depth
 * formats CheckDeviceFormat accepts. Results to C:\V9XDIAG\D3D8FMT.TXT.
 *
 * An instrument for docs/plans/ddi6-drawprimitives2.md. Built by hand
 * (GUI subsystem, because the netbook's agent refuses console programs):
 *   wcl386 -bt=nt -l=nt_win -zq -fe=D3D8FMT.EXE d3d8_formats_win32.c
 * IDirect3D8 through raw vtable slots: 4 GetAdapterCount, 6
 * GetAdapterModeCount, 7 EnumAdapterModes, 8 GetAdapterDisplayMode, 9
 * CheckDeviceType, 10 CheckDeviceFormat, 13 GetDeviceCaps.
 */
#include <windows.h>
#include <stdio.h>

typedef struct mode { UINT w, h, rate, format; } MODE;
typedef void *(WINAPI *CREATE8)(UINT);
typedef unsigned long (__stdcall *FN)();
#define SLOT(o, n) (((FN *)*(void **)(o))[n])

/* D3DFORMAT values, d3d8types.h. */
static const struct { UINT f; const char *n; } formats[] = {
    { 20, "R8G8B8" }, { 21, "A8R8G8B8" }, { 22, "X8R8G8B8" },
    { 23, "R5G6B5" }, { 24, "X1R5G5B5" }, { 25, "A1R5G5B5" },
    { 26, "A4R4G4B4" }, { 28, "A8" }, { 30, "X4R4G4B4" }, { 41, "P8" },
    { 50, "L8" }, { 51, "A8L8" },
    { 0x31545844, "DXT1" }, { 0x33545844, "DXT3" }, { 0x35545844, "DXT5" },
};
static const struct { UINT f; const char *n; } depths[] = {
    { 70, "D16_LOCKABLE" }, { 71, "D32" }, { 73, "D15S1" },
    { 75, "D24S8" }, { 77, "D24X8" }, { 79, "D24X4S4" }, { 80, "D16" },
};

#define USAGE_RENDERTARGET 1ul
#define USAGE_DEPTHSTENCIL 2ul
#define RTYPE_SURFACE 1ul
#define RTYPE_TEXTURE 3ul

static unsigned long check_format(void *d3d, UINT adapter_format,
                                  DWORD usage, DWORD rtype, UINT format)
{
    return ((unsigned long (__stdcall *)(void *, UINT, UINT, UINT, DWORD,
                                         DWORD, UINT))SLOT(d3d, 10))(
        d3d, 0, 1, adapter_format, usage, rtype, format);
}

static unsigned long check_type(void *d3d, UINT display, UINT back,
                                BOOL windowed)
{
    return ((unsigned long (__stdcall *)(void *, UINT, UINT, UINT, UINT,
                                         BOOL))SLOT(d3d, 9))(
        d3d, 0, 1, display, back, windowed);
}

int WINAPI WinMain(HINSTANCE i, HINSTANCE p, LPSTR c, int s)
{
    HMODULE lib = LoadLibrary("d3d8.dll");
    CREATE8 create;
    void *d3d;
    FILE *out = fopen("C:\\V9XDIAG\\D3D8FMT.TXT", "w");
    MODE m;
    UINT count, k, by16 = 0, by32 = 0, other = 0, display;
    unsigned long hr;

    (void)i; (void)p; (void)c; (void)s;
    if (out == 0) return 1;
    create = lib != 0 ? (CREATE8)GetProcAddress(lib, "Direct3DCreate8") : 0;
    d3d = create != 0 ? create(220) : 0;
    if (d3d == 0 && create != 0) d3d = create(120);
    if (d3d == 0) { fprintf(out, "Direct3DCreate8 failed\n"); return 3; }

    hr = ((unsigned long (__stdcall *)(void *, UINT, MODE *))SLOT(d3d, 8))(
        d3d, 0, &m);
    display = m.format;
    fprintf(out, "Desktop %ux%u format %u hr=%08lx\n", m.w, m.h, m.format, hr);
    count = ((UINT (__stdcall *)(void *, UINT))SLOT(d3d, 6))(d3d, 0);
    for (k = 0; k < count; ++k) {
        ((unsigned long (__stdcall *)(void *, UINT, UINT, MODE *))
             SLOT(d3d, 7))(d3d, 0, k, &m);
        if (m.format == 23 || m.format == 24) ++by16;
        else if (m.format == 22 || m.format == 21) ++by32;
        else ++other;
    }
    fprintf(out, "Modes %u: 16-bit %u, 32-bit %u, other %u\n", count, by16,
            by32, other);

    fprintf(out, "CheckDeviceType HAL (display, back, windowed):\n");
    fprintf(out, "  565/565 full %08lx  565/565 win %08lx\n",
            check_type(d3d, 23, 23, FALSE), check_type(d3d, 23, 23, TRUE));
    fprintf(out, "  X888/X888 full %08lx  X888/A888 full %08lx\n",
            check_type(d3d, 22, 22, FALSE), check_type(d3d, 22, 21, FALSE));
    fprintf(out, "  desktop(%u)/same win %08lx\n", display,
            check_type(d3d, display, display, TRUE));

    for (k = 0; k < sizeof(formats) / sizeof(formats[0]); ++k) {
        fprintf(out, "Texture %-9s on 565 %08lx  RenderTarget %08lx\n",
                formats[k].n,
                check_format(d3d, 23, 0ul, RTYPE_TEXTURE, formats[k].f),
                check_format(d3d, 23, USAGE_RENDERTARGET, RTYPE_SURFACE,
                             formats[k].f));
    }
    for (k = 0; k < sizeof(depths) / sizeof(depths[0]); ++k) {
        fprintf(out, "Depth %-12s on 565 %08lx\n", depths[k].n,
                check_format(d3d, 23, USAGE_DEPTHSTENCIL, RTYPE_SURFACE,
                             depths[k].f));
    }
    ((unsigned long (__stdcall *)(void *))SLOT(d3d, 2))(d3d);
    fclose(out);
    return 0;
}
