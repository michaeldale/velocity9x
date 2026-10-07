/*
 * DX5 DrawPrimitive reproducer for the DDI 6 call rate
 * (docs\plans\ddi6-drawprimitives2.md, Part C).
 *
 * One IDirect3DDevice2 on a 256x256 offscreen target, windowed, and twelve
 * phases of the same 200 quads, each phase changing one thing: primitive
 * type, indexed or not, D3DDP_DONOTCLIP, a texture change or a render
 * state change between draws, the size of the vertex array an indexed draw
 * hands over. Run under Direct3DDdi=6 with Direct3DDdiProbe=32, the
 * DrawPrimitives2 capture (src\display32\d3d\d3d_dp2_ring.h) then shows how
 * many calls each phase became and what each call carried; the phases are
 * separated in the capture by a Lock of a small surface (tools\diag\
 * dp2ring.py --phases). Each phase is also timed here, so a run at DDI 5
 * gives the same per-draw cost for comparison.
 *
 * Results: C:\V9XDIAG\V9XDP2P.INI. Built by hand, not by the package
 * scripts (an instrument):
 *   wcl386 -bt=nt -l=nt_win -zq -fe=DP2REPRO.EXE dp2_repro_win32.c
 * COM through raw vtable slots, numbered from the DirectX 5 headers, as
 * tools\diag\ddraw_probe_win32.c declares them.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define RESULT_PATH "C:\\V9XDIAG\\V9XDP2P.INI"
#define DRAWS 200

typedef HRESULT (WINAPI *DDCREATE)(GUID *, void **, void *);
typedef unsigned long (__stdcall *SLOTFN)();
#define VT(o) (*(SLOTFN **)(o))

typedef struct { DWORD dwSize, dwFlags, dwFourCC, dwRGBBitCount, r, g, b, a; } PIXFMT;
typedef struct { DWORD lo, hi; } CKEY;
typedef struct {
    DWORD dwSize, dwFlags, dwHeight, dwWidth;
    LONG lPitch;
    DWORD dwBackBufferCount, dwMipMapCount, dwAlphaBitDepth, dwReserved;
    void *lpSurface;
    CKEY ck[4];
    PIXFMT ddpf;
    DWORD dwCaps;
} SURFDESC;
typedef struct {
    DWORD dwSize, dwX, dwY, dwWidth, dwHeight;
    float clipX, clipY, clipW, clipH, minZ, maxZ;
} VIEWPORT2;
typedef struct { float sx, sy, sz, rhw; DWORD color, specular; float tu, tv; } TLV;

#define DDSD_CAPS 0x1ul
#define DDSD_HEIGHT 0x2ul
#define DDSD_WIDTH 0x4ul
#define DDSD_PIXELFORMAT 0x1000ul
#define DDSCAPS_OFFSCREENPLAIN 0x40ul
#define DDSCAPS_TEXTURE 0x1000ul
#define DDSCAPS_3DDEVICE 0x2000ul
#define DDSCAPS_VIDEOMEMORY 0x4000ul
#define DDPF_RGB 0x40ul
#define DDSCL_NORMAL 0x8ul
#define DDLOCK_WAIT 0x1ul

#define D3DPT_TRIANGLELIST 4ul
#define D3DPT_TRIANGLEFAN 6ul
#define D3DVT_TLVERTEX 3ul
#define D3DDP_DONOTCLIP 4ul
#define RS_TEXTUREHANDLE 1ul
#define RS_ALPHATESTENABLE 15ul
#define RS_ALPHABLENDENABLE 27ul

static const GUID iid_d3d2 = { 0x6aae1ec1ul, 0x662a, 0x11d0,
    { 0x88, 0x9d, 0x00, 0xaa, 0x00, 0xbb, 0xb7, 0x6a } };
static const GUID iid_hal = { 0x84e63de0ul, 0x46aa, 0x11cf,
    { 0x81, 0x6f, 0x00, 0x00, 0xc0, 0x20, 0x15, 0x6e } };
static const GUID iid_d3d3 = { 0xbb223240ul, 0xe72b, 0x11d0,
    { 0xa9, 0xb4, 0x00, 0xaa, 0x00, 0xc0, 0x99, 0x3e } };
static const GUID iid_dds4 = { 0x0b2b8630ul, 0xad35, 0x11d0,
    { 0x8e, 0xa6, 0x00, 0x60, 0x97, 0x97, 0xea, 0x5b } };
static const GUID iid_tex2 = { 0x93281502ul, 0x8cf8, 0x11d0,
    { 0x89, 0xab, 0x00, 0xa0, 0xc9, 0x05, 0x41, 0x29 } };

static FILE *out;
static void *dev;
static void *marker;
static DWORD tex[2];
static TLV quad_list[6], quad_fan[4], big[64];
static WORD quad_index[6] = { 0, 1, 2, 0, 2, 3 };
static WORD vb_index[6];
static void *dev3;
static void *vb;
static void *vp3_used;
#define VB_VERTICES 32768ul
#define D3DFVF_TLVERTEX 0x1c4ul
#define DDLOCK_WRITEONLY 0x20ul
#define DDLOCK_NOOVERWRITE 0x1000ul

/* IDirectDrawSurface slots: 25 Lock, 32 Unlock. */
static void phase_marker(void)
{
    SURFDESC d;

    if (marker == 0) {
        return;
    }
    memset(&d, 0, sizeof(d));
    d.dwSize = sizeof(d);
    if (((HRESULT (__stdcall *)(void *, RECT *, SURFDESC *, DWORD, HANDLE))
             VT(marker)[25])(marker, 0, &d, DDLOCK_WAIT, 0) == 0) {
        ((HRESULT (__stdcall *)(void *, void *))VT(marker)[32])(marker, 0);
    }
}

/* IDirect3DDevice2 slots: 10 BeginScene, 11 EndScene, 23 SetRenderState,
 * 29 DrawPrimitive, 30 DrawIndexedPrimitive. */
static HRESULT set_state(DWORD state, DWORD value)
{
    return ((HRESULT (__stdcall *)(void *, DWORD, DWORD))VT(dev)[23])(
        dev, state, value);
}

static HRESULT draw(DWORD type, TLV *v, DWORD count, DWORD flags)
{
    return ((HRESULT (__stdcall *)(void *, DWORD, DWORD, void *, DWORD,
                                   DWORD))VT(dev)[29])(
        dev, type, D3DVT_TLVERTEX, v, count, flags);
}

static HRESULT draw_indexed(TLV *v, DWORD count, DWORD flags)
{
    return ((HRESULT (__stdcall *)(void *, DWORD, DWORD, void *, DWORD,
                                   WORD *, DWORD, DWORD))VT(dev)[30])(
        dev, D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, v, count, quad_index, 6ul,
        flags);
}

static void make_quad(TLV *v, float x, float y, float s)
{
    int i;

    for (i = 0; i < 4; ++i) {
        v[i].sx = x + ((i == 1 || i == 2) ? s : 0.0f);
        v[i].sy = y + (i >= 2 ? s : 0.0f);
        v[i].sz = 0.5f;
        v[i].rhw = 1.0f;
        v[i].color = 0xff8080ffu;
        v[i].specular = 0xff000000u;
        v[i].tu = (i == 1 || i == 2) ? 1.0f : 0.0f;
        v[i].tv = i >= 2 ? 1.0f : 0.0f;
    }
}

/* IDirect3DDevice3 slots: 9 BeginScene, 10 EndScene, 29
 * DrawIndexedPrimitive, 35 DrawIndexedPrimitiveVB. IDirect3DVertexBuffer: 3
 * Lock, 4 Unlock. */
static HRESULT vb_draw(int quad)
{
    int k;

    for (k = 0; k < 6; ++k) {
        vb_index[k] = (WORD)(quad * 4 + quad_index[k]);
    }
    return ((HRESULT (__stdcall *)(void *, DWORD, void *, WORD *, DWORD,
                                   DWORD))VT(dev3)[35])(
        dev3, D3DPT_TRIANGLELIST, vb, vb_index, 6ul, 0ul);
}

static HRESULT vb_write(int quad, DWORD flags)
{
    void *p = 0;
    DWORD size = 0;
    HRESULT hr = ((HRESULT (__stdcall *)(void *, DWORD, void **, DWORD *))
                      VT(vb)[3])(vb, flags, &p, &size);

    if (hr == 0 && p != 0) {
        memcpy((TLV *)p + quad * 4, quad_fan, sizeof(quad_fan));
        ((HRESULT (__stdcall *)(void *))VT(vb)[4])(vb);
    }
    return hr;
}

static void run_phase3(int number, const char *name)
{
    LARGE_INTEGER t0, t1;
    HRESULT hr = 0, last = 0;
    int i;

    if (dev3 == 0 || vb == 0) {
        fprintf(out, "Phase%d=%s skipped\n", number, name);
        return;
    }
    phase_marker();
    ((HRESULT (__stdcall *)(void *))VT(dev3)[9])(dev3);
    QueryPerformanceCounter(&t0);
    for (i = 0; i < DRAWS; ++i) {
        switch (number) {
        case 20:
            hr = vb_draw(i);
            break;
        case 21:
            vb_write(i, DDLOCK_WAIT | DDLOCK_WRITEONLY | DDLOCK_NOOVERWRITE);
            hr = vb_draw(i);
            break;
        case 22:
            vb_write(i, DDLOCK_WAIT | DDLOCK_WRITEONLY);
            hr = vb_draw(i);
            break;
        case 23:
            /* DrawIndexedPrimitive on a Device3, FVF, user memory. */
            hr = ((HRESULT (__stdcall *)(void *, DWORD, DWORD, void *, DWORD,
                                         WORD *, DWORD, DWORD))VT(dev3)[29])(
                dev3, D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, big, 64ul,
                quad_index, 6ul, 0ul);
            break;
        }
        if (hr != 0) {
            last = hr;
        }
    }
    QueryPerformanceCounter(&t1);
    ((HRESULT (__stdcall *)(void *))VT(dev3)[10])(dev3);
    fprintf(out, "Phase%d=%s ticks=%lu lasterr=%08lx\n", number, name,
            (unsigned long)(t1.QuadPart - t0.QuadPart), (unsigned long)last);
}

static void run_phase(int number, const char *name)
{
    LARGE_INTEGER t0, t1;
    HRESULT hr = 0, last = 0;
    int i;
    DWORD flags;

    phase_marker();
    ((HRESULT (__stdcall *)(void *))VT(dev)[10])(dev);
    QueryPerformanceCounter(&t0);
    for (i = 0; i < DRAWS; ++i) {
        flags = (number == 4 || number == 5) ? D3DDP_DONOTCLIP : 0ul;
        switch (number) {
        case 1: case 5:
            hr = draw(D3DPT_TRIANGLELIST, quad_list, 6ul, flags);
            break;
        case 2:
            hr = draw(D3DPT_TRIANGLEFAN, quad_fan, 4ul, flags);
            break;
        case 3: case 4:
            hr = draw_indexed(quad_fan, 4ul, flags);
            break;
        case 6:
            set_state(RS_ALPHATESTENABLE, 0ul);
            set_state(RS_ALPHATESTENABLE, 1ul);
            hr = draw_indexed(quad_fan, 4ul, 0ul);
            break;
        case 7:
            set_state(RS_TEXTUREHANDLE, tex[i & 1]);
            hr = draw(D3DPT_TRIANGLELIST, quad_list, 6ul, 0ul);
            break;
        case 8:
            set_state(RS_TEXTUREHANDLE, tex[i & 1]);
            hr = draw_indexed(quad_fan, 4ul, 0ul);
            break;
        case 9:
            set_state(RS_ALPHABLENDENABLE, (DWORD)(i & 1));
            hr = draw(D3DPT_TRIANGLELIST, quad_list, 6ul, 0ul);
            break;
        case 10:
            set_state(RS_ALPHABLENDENABLE, (DWORD)(i & 1));
            hr = draw_indexed(quad_fan, 4ul, 0ul);
            break;
        case 11:
            /* An indexed draw handing over a 64-vertex array of which it
             * uses the first four. */
            hr = draw_indexed(big, 64ul, 0ul);
            break;
        case 13: case 14: case 15: case 16: case 17: case 18:
            /* The array-size sweep: 8, 12, 16, 24, 32, 48 vertices. */
            hr = draw_indexed(big, (DWORD)(number == 13 ? 8 : number == 14
                                           ? 12 : number == 15 ? 16
                                           : number == 16 ? 24
                                           : number == 17 ? 32 : 48), 0ul);
            break;
        case 12:
            /* The same texture set again before every draw. */
            set_state(RS_TEXTUREHANDLE, tex[0]);
            hr = draw_indexed(quad_fan, 4ul, 0ul);
            break;
        }
        if (hr != 0) {
            last = hr;
        }
    }
    QueryPerformanceCounter(&t1);
    ((HRESULT (__stdcall *)(void *))VT(dev)[11])(dev);
    set_state(RS_ALPHABLENDENABLE, 0ul);
    set_state(RS_ALPHATESTENABLE, 0ul);
    set_state(RS_TEXTUREHANDLE, tex[0]);
    fprintf(out, "Phase%d=%s ticks=%lu lasterr=%08lx\n", number, name,
            (unsigned long)(t1.QuadPart - t0.QuadPart), (unsigned long)last);
}

/*
 * Lines and points, read back. The target is cleared to black by a colour
 * fill, a horizontal line (100,150)-(200,150), a vertical one
 * (50,100)-(50,200) and a point at (220,220) are drawn in white with no
 * texture, and the target is locked and counted: lit pixels on each, and
 * on the rows or columns either side, which a one-pixel line leaves dark.
 * IDirectDrawSurface slots 5 Blt, 25 Lock, 32 Unlock.
 */
#define D3DPT_POINTLIST 1ul
#define D3DPT_LINELIST 2ul
#define DDBLT_COLORFILL 0x400ul
#define DDBLT_WAIT 0x1000000ul

static void line_check(void *target)
{
    DWORD fx[25];
    TLV v[4];
    SURFDESC d;
    HRESULT hr_line, hr_point, hr;
    int on_h = 0, off_h = 0, on_v = 0, off_v = 0, on_p = 0, around_p = 0;
    int x, y;

    memset(fx, 0, sizeof(fx));
    fx[0] = sizeof(fx);
    hr = ((HRESULT (__stdcall *)(void *, RECT *, void *, RECT *, DWORD,
                                 DWORD *))VT(target)[5])(
        target, 0, 0, 0, DDBLT_COLORFILL | DDBLT_WAIT, fx);
    fprintf(out, "LineClear=%08lx\n", (unsigned long)hr);
    set_state(RS_TEXTUREHANDLE, 0ul);
    ((HRESULT (__stdcall *)(void *))VT(dev)[10])(dev);
    make_quad(v, 0.0f, 0.0f, 1.0f);
    v[0].color = 0xffffffffu;
    v[0].sx = 100.0f; v[0].sy = 150.0f;
    v[1] = v[0];
    v[1].sx = 200.0f;
    hr_line = draw(D3DPT_LINELIST, v, 2ul, 0ul);
    v[0].sx = 50.0f; v[0].sy = 100.0f;
    v[1] = v[0];
    v[1].sy = 200.0f;
    hr = draw(D3DPT_LINELIST, v, 2ul, 0ul);
    if (hr != 0) {
        hr_line = hr;
    }
    v[0].sx = 220.0f; v[0].sy = 220.0f;
    hr_point = draw(D3DPT_POINTLIST, v, 1ul, 0ul);
    ((HRESULT (__stdcall *)(void *))VT(dev)[11])(dev);
    fprintf(out, "LineDrawHr=%08lx PointDrawHr=%08lx\n",
            (unsigned long)hr_line, (unsigned long)hr_point);

    memset(&d, 0, sizeof(d));
    d.dwSize = sizeof(d);
    hr = ((HRESULT (__stdcall *)(void *, RECT *, SURFDESC *, DWORD, HANDLE))
              VT(target)[25])(target, 0, &d, DDLOCK_WAIT, 0);
    fprintf(out, "LineLock=%08lx pitch=%ld\n", (unsigned long)hr,
            (long)d.lPitch);
    if (hr != 0 || d.lpSurface == 0) {
        return;
    }
#define PIX(px, py) (((WORD *)((BYTE *)d.lpSurface + (py) * d.lPitch))[px])
    for (x = 100; x < 200; ++x) {
        on_h += PIX(x, 150) != 0;
        off_h += (PIX(x, 148) != 0) + (PIX(x, 152) != 0);
    }
    for (y = 100; y < 200; ++y) {
        on_v += PIX(50, y) != 0;
        off_v += (PIX(48, y) != 0) + (PIX(52, y) != 0);
    }
    /* Every row and column near each line, so a half-pixel convention
     * shows as a shift rather than a miss. */
    for (y = 147; y <= 153; ++y) {
        int lit = 0;

        for (x = 90; x < 210; ++x) {
            lit += PIX(x, y) != 0;
        }
        fprintf(out, "Row%d=%d\n", y, lit);
    }
    for (x = 47; x <= 53; ++x) {
        int lit = 0;

        for (y = 90; y < 210; ++y) {
            lit += PIX(x, y) != 0;
        }
        fprintf(out, "Col%d=%d\n", x, lit);
    }
    /* The point's pixel, whichever of the four round (220,220) the fill
     * rule picks, and the ring two pixels out. */
    for (y = 219; y <= 220; ++y) {
        for (x = 219; x <= 220; ++x) {
            on_p += PIX(x, y) != 0;
        }
    }
    for (x = 217; x <= 222; ++x) {
        around_p += (PIX(x, 217) != 0) + (PIX(x, 222) != 0);
    }
#undef PIX
    ((HRESULT (__stdcall *)(void *, void *))VT(target)[32])(target, 0);
    fprintf(out, "LineH=%d/100 LineHBeside=%d\n", on_h, off_h);
    fprintf(out, "LineV=%d/100 LineVBeside=%d\n", on_v, off_v);
    fprintf(out, "Point=%d/4 PointAround=%d\n", on_p, around_p);
}

/*
 * A colour clear through IDirect3DViewport3::Clear2 (slot 20), which a
 * DDI 6 runtime turns into the HAL's Clear2: the target is first filled
 * black, a rectangle (10,10)-(74,42) is cleared red, and the target is read
 * back for red inside and black just outside.
 */
#define D3DCLEAR_TARGET 1ul

static void clear_check(void *target, void *vp3)
{
    DWORD fx[25];
    LONG rect[4];
    SURFDESC d;
    HRESULT hr;
    int inside = 0, outside = 0, x, y;

    if (vp3 == 0) {
        fprintf(out, "ClearCheck=skipped\n");
        return;
    }
    memset(fx, 0, sizeof(fx));
    fx[0] = sizeof(fx);
    ((HRESULT (__stdcall *)(void *, RECT *, void *, RECT *, DWORD,
                            DWORD *))VT(target)[5])(
        target, 0, 0, 0, DDBLT_COLORFILL | DDBLT_WAIT, fx);
    rect[0] = 10; rect[1] = 10; rect[2] = 74; rect[3] = 42;
    hr = ((HRESULT (__stdcall *)(void *, DWORD, LONG *, DWORD, DWORD, float,
                                 DWORD))VT(vp3)[20])(
        vp3, 1ul, rect, D3DCLEAR_TARGET, 0x00ff0000ul, 1.0f, 0ul);
    fprintf(out, "Clear2Hr=%08lx\n", (unsigned long)hr);
    memset(&d, 0, sizeof(d));
    d.dwSize = sizeof(d);
    if (((HRESULT (__stdcall *)(void *, RECT *, SURFDESC *, DWORD, HANDLE))
             VT(target)[25])(target, 0, &d, DDLOCK_WAIT, 0) != 0 ||
        d.lpSurface == 0) {
        fprintf(out, "ClearLock=failed\n");
        return;
    }
#define PIX(px, py) (((WORD *)((BYTE *)d.lpSurface + (py) * d.lPitch))[px])
    for (y = 10; y < 42; ++y) {
        for (x = 10; x < 74; ++x) {
            inside += PIX(x, y) == 0xf800u;
        }
    }
    for (x = 8; x < 76; ++x) {
        outside += (PIX(x, 9) != 0) + (PIX(x, 42) != 0);
    }
    for (y = 10; y < 42; ++y) {
        outside += (PIX(9, y) != 0) + (PIX(74, y) != 0);
    }
#undef PIX
    ((HRESULT (__stdcall *)(void *, void *))VT(target)[32])(target, 0);
    fprintf(out, "ClearInside=%d/2048 ClearOutsideLit=%d\n", inside,
            outside);
}

static void *make_surface(void *dd, DWORD caps, DWORD w, DWORD h, int fmt)
{
    SURFDESC d;
    void *s = 0;
    HRESULT hr;

    memset(&d, 0, sizeof(d));
    d.dwSize = sizeof(d);
    d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    d.dwWidth = w;
    d.dwHeight = h;
    d.dwCaps = caps;
    if (fmt) {
        d.dwFlags |= DDSD_PIXELFORMAT;
        d.ddpf.dwSize = sizeof(d.ddpf);
        d.ddpf.dwFlags = DDPF_RGB;
        d.ddpf.dwRGBBitCount = 16ul;
        d.ddpf.r = 0xf800ul;
        d.ddpf.g = 0x07e0ul;
        d.ddpf.b = 0x001ful;
    }
    /* IDirectDraw slot 6: CreateSurface. */
    hr = ((HRESULT (__stdcall *)(void *, SURFDESC *, void **, void *))
              VT(dd)[6])(dd, &d, &s, 0);
    fprintf(out, "CreateSurface caps=%08lx hr=%08lx\n", (unsigned long)caps,
            (unsigned long)hr);
    return hr == 0 ? s : 0;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    HMODULE lib;
    DDCREATE create;
    void *dd = 0, *d3d = 0, *target, *vp = 0;
    WNDCLASSA wc;
    HWND wnd;
    HRESULT hr;
    VIEWPORT2 v;
    int i;

    (void)prev; (void)cmd; (void)show;
    out = fopen(RESULT_PATH, "w");
    if (out == 0) {
        return 1;
    }
    fprintf(out, "[Velocity9xDp2Repro]\n");
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = inst;
    wc.lpszClassName = "V9xDp2Repro";
    RegisterClassA(&wc);
    wnd = CreateWindowExA(0, wc.lpszClassName, "dp2repro", WS_POPUP, 0, 0,
                          64, 64, 0, 0, inst, 0);
    lib = LoadLibraryA("DDRAW.DLL");
    create = lib != 0 ? (DDCREATE)GetProcAddress(lib, "DirectDrawCreate") : 0;
    hr = create != 0 ? create(0, &dd, 0) : E_FAIL;
    fprintf(out, "DirectDrawCreate=%08lx\n", (unsigned long)hr);
    if (hr != 0) {
        fclose(out);
        return 2;
    }
    ((HRESULT (__stdcall *)(void *, HWND, DWORD))VT(dd)[20])(dd, wnd,
                                                             DDSCL_NORMAL);
    hr = ((HRESULT (__stdcall *)(void *, const GUID *, void **))VT(dd)[0])(
        dd, &iid_d3d2, &d3d);
    fprintf(out, "QueryD3D2=%08lx\n", (unsigned long)hr);
    target = make_surface(dd, DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE |
                                  DDSCAPS_VIDEOMEMORY, 256ul, 256ul, 0);
    marker = make_surface(dd, DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY,
                          8ul, 8ul, 0);
    if (d3d == 0 || target == 0) {
        fclose(out);
        return 3;
    }
    /* IDirect3D2 slot 8: CreateDevice. */
    hr = ((HRESULT (__stdcall *)(void *, const GUID *, void *, void **))
              VT(d3d)[8])(d3d, &iid_hal, target, &dev);
    fprintf(out, "CreateDevice=%08lx\n", (unsigned long)hr);
    if (hr != 0) {
        fclose(out);
        return 4;
    }
    /* IDirect3DDevice2 slot 3: GetCaps(HAL, HEL); the DX5 D3DDEVICEDESC is
     * 252 bytes with dwDevCaps at byte 12. What the runtime believes. */
    {
        DWORD hal[64], hel[64];

        memset(hal, 0, sizeof(hal));
        memset(hel, 0, sizeof(hel));
        hal[0] = 252ul;
        hel[0] = 252ul;
        hr = ((HRESULT (__stdcall *)(void *, DWORD *, DWORD *))VT(dev)[3])(
            dev, hal, hel);
        fprintf(out, "GetCaps=%08lx HalDevCaps=%08lx\n", (unsigned long)hr,
                (unsigned long)hal[3]);
    }
    /* Viewport: IDirect3D2 slot 6 CreateViewport, device slot 6
     * AddViewport and 13 SetCurrentViewport, viewport slot 17
     * SetViewport2. */
    ((HRESULT (__stdcall *)(void *, void **, void *))VT(d3d)[6])(d3d, &vp, 0);
    if (vp != 0) {
        ((HRESULT (__stdcall *)(void *, void *))VT(dev)[6])(dev, vp);
        memset(&v, 0, sizeof(v));
        v.dwSize = sizeof(v);
        v.dwWidth = 256ul;
        v.dwHeight = 256ul;
        v.clipX = -1.0f; v.clipY = 1.0f; v.clipW = 2.0f; v.clipH = 2.0f;
        v.maxZ = 1.0f;
        ((HRESULT (__stdcall *)(void *, VIEWPORT2 *))VT(vp)[17])(vp, &v);
        ((HRESULT (__stdcall *)(void *, void *))VT(dev)[13])(dev, vp);
    }
    /* Two textures: surface QueryInterface for IDirect3DTexture2, slot 3
     * GetHandle. */
    for (i = 0; i < 2; ++i) {
        void *ts = make_surface(dd, DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY,
                                16ul, 16ul, 1);
        void *t2 = 0;

        if (ts != 0 &&
            ((HRESULT (__stdcall *)(void *, const GUID *, void **))
                 VT(ts)[0])(ts, &iid_tex2, &t2) == 0 && t2 != 0) {
            ((HRESULT (__stdcall *)(void *, void *, DWORD *))VT(t2)[3])(
                t2, dev, &tex[i]);
        }
        fprintf(out, "Texture%d=%08lx\n", i, (unsigned long)tex[i]);
    }

    make_quad(quad_fan, 16.0f, 16.0f, 64.0f);
    quad_list[0] = quad_fan[0]; quad_list[1] = quad_fan[1];
    quad_list[2] = quad_fan[2]; quad_list[3] = quad_fan[0];
    quad_list[4] = quad_fan[2]; quad_list[5] = quad_fan[3];
    for (i = 0; i < 16; ++i) {
        make_quad(&big[i * 4], 16.0f, 16.0f, 64.0f);
    }
    set_state(RS_TEXTUREHANDLE, tex[0]);

    run_phase(1, "list");
    run_phase(2, "fan");
    run_phase(3, "indexed");
    run_phase(4, "indexed-donotclip");
    run_phase(5, "list-donotclip");
    run_phase(6, "indexed-alphatest-toggle");
    run_phase(7, "list-texture-alternate");
    run_phase(8, "indexed-texture-alternate");
    run_phase(9, "list-state-alternate");
    run_phase(10, "indexed-state-alternate");
    run_phase(11, "indexed-64-vertex-array");
    run_phase(12, "indexed-same-texture-reset");
    run_phase(13, "indexed-8-vertex-array");
    run_phase(14, "indexed-12-vertex-array");
    run_phase(15, "indexed-16-vertex-array");
    run_phase(16, "indexed-24-vertex-array");
    run_phase(17, "indexed-32-vertex-array");
    run_phase(18, "indexed-48-vertex-array");
    line_check(target);
    ((ULONG (__stdcall *)(void *))VT(dev)[2])(dev);
    dev = 0;

    /* DirectX 6: IDirect3D3 (slot 8 CreateDevice on an IDirectDrawSurface4,
     * slot 9 CreateVertexBuffer, slot 6 CreateViewport), the device's
     * viewport (5 AddViewport, 12 SetCurrentViewport). */
    {
        void *d3d3 = 0, *target4 = 0, *vp3 = 0;
        DWORD vbdesc[4];

        hr = ((HRESULT (__stdcall *)(void *, const GUID *, void **))
                  VT(dd)[0])(dd, &iid_d3d3, &d3d3);
        fprintf(out, "QueryD3D3=%08lx\n", (unsigned long)hr);
        if (d3d3 != 0) {
            ((HRESULT (__stdcall *)(void *, const GUID *, void **))
                 VT(target)[0])(target, &iid_dds4, &target4);
        }
        if (target4 != 0) {
            hr = ((HRESULT (__stdcall *)(void *, const GUID *, void *,
                                         void **, void *))VT(d3d3)[8])(
                d3d3, &iid_hal, target4, &dev3, 0);
            fprintf(out, "CreateDevice3=%08lx\n", (unsigned long)hr);
        }
        if (dev3 != 0) {
            ((HRESULT (__stdcall *)(void *, void **, void *))VT(d3d3)[6])(
                d3d3, &vp3, 0);
            if (vp3 != 0) {
                ((HRESULT (__stdcall *)(void *, void *))VT(dev3)[5])(dev3,
                                                                    vp3);
                ((HRESULT (__stdcall *)(void *, VIEWPORT2 *))VT(vp3)[17])(
                    vp3, &v);
                ((HRESULT (__stdcall *)(void *, void *))VT(dev3)[12])(dev3,
                                                                     vp3);
                vp3_used = vp3;
            }
            vbdesc[0] = sizeof(vbdesc);
            vbdesc[1] = 0ul;
            vbdesc[2] = D3DFVF_TLVERTEX;
            vbdesc[3] = VB_VERTICES;
            hr = ((HRESULT (__stdcall *)(void *, DWORD *, void **, DWORD,
                                         void *))VT(d3d3)[9])(
                d3d3, vbdesc, &vb, 0ul, 0);
            fprintf(out, "CreateVertexBuffer=%08lx\n", (unsigned long)hr);
            for (i = 0; vb != 0 && i < DRAWS; ++i) {
                vb_write(i, DDLOCK_WAIT | DDLOCK_WRITEONLY);
            }
        }
    }
    run_phase3(20, "vb-indexed-no-locks");
    run_phase3(21, "vb-indexed-lock-nooverwrite");
    run_phase3(22, "vb-indexed-lock");
    run_phase3(23, "device3-indexed-64-usermem");
    clear_check(target, vp3_used);
    phase_marker();
    fprintf(out, "Done=1\n");
    fclose(out);
    /* Device release destroys the context, which writes the capture. */
    if (dev3 != 0) {
        ((ULONG (__stdcall *)(void *))VT(dev3)[2])(dev3);
    }
    return 0;
}
