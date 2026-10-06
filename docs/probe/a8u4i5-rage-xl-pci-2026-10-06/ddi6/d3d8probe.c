/* Minimal Direct3D 8 interrogation through raw vtable slots, no SDK. */
#include <windows.h>
#include <stdio.h>

typedef struct mode { UINT w, h, rate, format; } MODE;
typedef void *(WINAPI *CREATE8)(UINT);
typedef unsigned long (__stdcall *FN)();

/* IDirect3D8 slots: 4 GetAdapterCount, 6 GetAdapterModeCount,
 * 7 EnumAdapterModes, 8 GetAdapterDisplayMode, 9 CheckDeviceType,
 * 10 CheckDeviceFormat, 13 GetDeviceCaps. */
#define SLOT(o, n) (((FN *)*(void **)(o))[n])

int main(void)
{
    HMODULE lib = LoadLibrary("d3d8.dll");
    CREATE8 create;
    void *d3d;
    FILE *out = fopen("C:\\V9XDIAG\\D3D8PRB.TXT", "w");
    MODE m;
    UINT count, i;
    unsigned long hr;
    DWORD caps[64];
    int internals;

    if (out == 0) return 1;
    if (lib == 0) { fprintf(out, "no d3d8.dll\n"); return 2; }
    create = (CREATE8)GetProcAddress(lib, "Direct3DCreate8");
    d3d = create != 0 ? create(220) : 0;
    internals = d3d != 0;
    if (d3d == 0 && create != 0) {
        /* DirectX 8.0's runtime takes only its own D3D_SDK_VERSION. */
        d3d = create(120);
        fprintf(out, "SdkVersion=120 (DirectX 8.0 runtime)\n");
    }
    if (d3d == 0) { fprintf(out, "Direct3DCreate8 failed\n"); return 3; }

    fprintf(out, "Adapters=%lu\n",
            ((unsigned long (__stdcall *)(void *))SLOT(d3d, 4))(d3d));
    hr = ((unsigned long (__stdcall *)(void *, UINT, MODE *))
              SLOT(d3d, 8))(d3d, 0, &m);
    fprintf(out, "DisplayMode hr=%08lx %ux%u rate %u format %u\n",
            hr, m.w, m.h, m.rate, m.format);
    count = ((UINT (__stdcall *)(void *, UINT))SLOT(d3d, 6))(d3d, 0);
    fprintf(out, "ModeCount=%u\n", count);
    for (i = 0; i < count && i < 64; ++i) {
        hr = ((unsigned long (__stdcall *)(void *, UINT, UINT, MODE *))
                  SLOT(d3d, 7))(d3d, 0, i, &m);
        fprintf(out, "Mode%02u hr=%08lx %ux%u rate %u format %u\n",
                i, hr, m.w, m.h, m.rate, m.format);
    }
    /* D3DDEVTYPE_HAL 1; formats 23 R5G6B5, 24 X1R5G5B5. */
    hr = ((unsigned long (__stdcall *)(void *, UINT, UINT, UINT, UINT, BOOL))
              SLOT(d3d, 9))(d3d, 0, 1, 23, 23, TRUE);
    fprintf(out, "CheckDeviceType HAL 565 windowed hr=%08lx\n", hr);
    hr = ((unsigned long (__stdcall *)(void *, UINT, UINT, UINT, UINT, BOOL))
              SLOT(d3d, 9))(d3d, 0, 1, 23, 23, FALSE);
    fprintf(out, "CheckDeviceType HAL 565 fullscreen hr=%08lx\n", hr);
    memset(caps, 0, sizeof(caps));
    hr = ((unsigned long (__stdcall *)(void *, UINT, UINT, DWORD *))
              SLOT(d3d, 13))(d3d, 0, 1, caps);
    fprintf(out, "GetDeviceCaps HAL hr=%08lx DevCaps=%08lx\n", hr, caps[7]);
    hr = ((unsigned long (__stdcall *)(void *, UINT, UINT, DWORD *))
              SLOT(d3d, 13))(d3d, 0, 2, caps);
    fprintf(out, "GetDeviceCaps REF hr=%08lx\n", hr);

    /* d3d8.dll 4.09.0000.0904 internals, read out of GetDeviceCaps
     * (0x410b90) and its caps getter (0x40fcc0): adapter 0's HAL block at
     * this+0x158, format-op count at +0xfc and list at +0x100, entries of
     * 0x6c bytes with the D3DFORMAT at +0x50 and the op flags at +0x58. */
    if (internals) {
        unsigned char *self = (unsigned char *)d3d;
        unsigned char *hal = self + 0x158;
        DWORD n = *(DWORD *)(hal + 0xfc);
        unsigned char *list = *(unsigned char **)(hal + 0x100);
        DWORD k;

        fprintf(out, "HalBlock flagsF8=%08lx count=%lu list=%08lx "
                "noHal33c4=%08lx\n", *(DWORD *)(hal + 0xf8), n,
                (unsigned long)list, *(DWORD *)(self + 0x33c4));
        for (k = 0; k < 0x110; k += 16) {
            fprintf(out, "Hal+%03lx %08lx %08lx %08lx %08lx\n", k,
                    *(DWORD *)(hal + k), *(DWORD *)(hal + k + 4),
                    *(DWORD *)(hal + k + 8), *(DWORD *)(hal + k + 12));
        }
        for (k = 0; list != 0 && k < n && k < 40; ++k) {
            unsigned char *e = list + k * 0x6c;
            fprintf(out, "Fmt%02lu flags4c=%08lx format=%lu ops=%08lx\n", k,
                    *(DWORD *)(e + 0x4c), *(DWORD *)(e + 0x50),
                    *(DWORD *)(e + 0x58));
        }
    }
    ((unsigned long (__stdcall *)(void *))SLOT(d3d, 2))(d3d);
    fclose(out);
    return 0;
}

/* GUI-subsystem entry, for agents that refuse console programs. */
int WINAPI WinMain(HINSTANCE i, HINSTANCE p, LPSTR c, int s)
{
    (void)i; (void)p; (void)c; (void)s;
    return main();
}
