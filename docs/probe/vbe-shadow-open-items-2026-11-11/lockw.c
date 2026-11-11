/* Scratch: write the primary through a DirectDraw Lock in normal
 * cooperative mode, the way a windowed program drawing straight to the
 * screen does, then hold. One Lock with a rectangle (a gradient in the
 * window), one without (a stripe across the screen). Results in
 * C:\V9XDIAG\LOCKW.TXT. Not part of the repository. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define DDSD_CAPS 0x00000001ul
#define DDSCAPS_PRIMARYSURFACE 0x00000200ul
#define DDSCL_NORMAL 0x00000008ul
#define DDLOCK_WAIT 0x00000001ul
#define DD_CREATE_SURFACE 6
#define DD_SET_COOPERATIVE_LEVEL 20
#define DDS_LOCK 25
#define DDS_UNLOCK 32

typedef struct lw_desc {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwHeight;
    DWORD dwWidth;
    LONG lPitch;
    DWORD rest[4];
    void *lpSurface;
    DWORD more[17];
} LW_DESC;
typedef char lw_desc_is_108[sizeof(LW_DESC) == 108 ? 1 : -1];

typedef struct lw_object { void **vtbl; } LW_OBJECT;
typedef HRESULT (WINAPI *lw_create_fn)(void *, LW_OBJECT **, void *);
typedef HRESULT (WINAPI *lw_create_surface_fn)(LW_OBJECT *, LW_DESC *,
    LW_OBJECT **, void *);
typedef HRESULT (WINAPI *lw_coop_fn)(LW_OBJECT *, HWND, DWORD);
typedef HRESULT (WINAPI *lw_lock_fn)(LW_OBJECT *, RECT *, LW_DESC *, DWORD,
    HANDLE);
typedef HRESULT (WINAPI *lw_unlock_fn)(LW_OBJECT *, void *);

static HANDLE lw_file = INVALID_HANDLE_VALUE;

static void lw_line(const char *key, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char text[64];
    DWORD written;
    int at = 0;
    int index;

    while (key[at] != '\0') {
        text[at] = key[at];
        ++at;
    }
    text[at++] = '=';
    for (index = 0; index < 8; ++index) {
        text[at++] = digits[(value >> ((7 - index) * 4)) & 15u];
    }
    text[at++] = '\r';
    text[at++] = '\n';
    WriteFile(lw_file, text, (DWORD)at, &written, 0);
}

static void lw_zero(void *p, DWORD n)
{
    BYTE *b = (BYTE *)p;
    while (n-- != 0ul) {
        *b++ = 0;
    }
}

/* One pixel of value v at depth bpp. */
static void lw_put(BYTE *row, DWORD x, DWORD bpp, DWORD v)
{
    if (bpp == 8ul) {
        row[x] = (BYTE)v;
    } else if (bpp == 16ul) {
        ((WORD *)row)[x] = (WORD)v;
    } else if (bpp == 24ul) {
        row[3 * x] = (BYTE)v;
        row[3 * x + 1] = (BYTE)(v >> 8);
        row[3 * x + 2] = (BYTE)(v >> 16);
    } else {
        ((DWORD *)row)[x] = v;
    }
}

static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_TIMER) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, w, l);
}

void __stdcall LockwEntry(void)
{
    WNDCLASSA wc;
    HWND window;
    MSG msg;
    HMODULE ddraw_module;
    lw_create_fn create;
    LW_OBJECT *ddraw = 0;
    LW_OBJECT *primary = 0;
    LW_DESC desc;
    RECT area;
    HRESULT hr;
    HDC screen;
    DWORD bpp;
    DWORD x;
    DWORD y;
    HINSTANCE instance = GetModuleHandleA(0);

    lw_file = CreateFileA("C:\\V9XDIAG\\LOCKW.TXT", GENERIC_WRITE, 0, 0,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    lw_zero(&wc, sizeof(wc));
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = "V9xLockw";
    wc.hCursor = LoadCursorA(0, IDC_ARROW);
    RegisterClassA(&wc);
    window = CreateWindowA("V9xLockw", "lockw", WS_POPUP | WS_VISIBLE,
                           200, 150, 400, 300, 0, 0, instance, 0);
    UpdateWindow(window);
    SetTimer(window, 1, 45000, 0);

    screen = GetDC(0);
    bpp = (DWORD)GetDeviceCaps(screen, BITSPIXEL);
    ReleaseDC(0, screen);
    lw_line("Bpp", bpp);

    ddraw_module = LoadLibraryA("DDRAW.DLL");
    create = ddraw_module != 0
        ? (lw_create_fn)GetProcAddress(ddraw_module, "DirectDrawCreate") : 0;
    hr = create != 0 ? create(0, &ddraw, 0) : (HRESULT)-1;
    lw_line("CreateHr", (DWORD)hr);
    if (hr == 0) {
        hr = ((lw_coop_fn)ddraw->vtbl[DD_SET_COOPERATIVE_LEVEL])(
            ddraw, window, DDSCL_NORMAL);
        lw_line("CoopHr", (DWORD)hr);
        lw_zero(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS;
        desc.more[16] = DDSCAPS_PRIMARYSURFACE;
        hr = ((lw_create_surface_fn)ddraw->vtbl[DD_CREATE_SURFACE])(
            ddraw, &desc, &primary, 0);
        lw_line("PrimaryHr", (DWORD)hr);
    }
    if (hr == 0) {
        /* A rectangle: a gradient over the window's middle. */
        area.left = 240;
        area.top = 190;
        area.right = 560;
        area.bottom = 410;
        lw_zero(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        hr = ((lw_lock_fn)primary->vtbl[DDS_LOCK])(primary, &area, &desc,
                                                  DDLOCK_WAIT, 0);
        lw_line("RectLockHr", (DWORD)hr);
        if (hr == 0) {
            for (y = 0ul; y < 220ul; ++y) {
                BYTE *row = (BYTE *)desc.lpSurface + y * (DWORD)desc.lPitch;
                for (x = 0ul; x < 320ul; ++x) {
                    lw_put(row, x, bpp, (x * 0x01010101ul) ^ (y << 3) ^
                           ((x & 16ul) != 0ul ? 0x00ff00fful : 0ul));
                }
            }
            hr = ((lw_unlock_fn)primary->vtbl[DDS_UNLOCK])(primary, 0);
            lw_line("RectUnlockHr", (DWORD)hr);
        }

        /* No rectangle: a stripe across the screen at rows 520-539. */
        lw_zero(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        hr = ((lw_lock_fn)primary->vtbl[DDS_LOCK])(primary, 0, &desc,
                                                  DDLOCK_WAIT, 0);
        lw_line("WholeLockHr", (DWORD)hr);
        if (hr == 0) {
            for (y = 520ul; y < 540ul; ++y) {
                BYTE *row = (BYTE *)desc.lpSurface + y * (DWORD)desc.lPitch;
                for (x = 0ul; x < desc.dwWidth; ++x) {
                    lw_put(row, x, bpp, ((x >> 3) & 1ul) != 0ul ?
                           0xfffffffful : 0x0000001ful);
                }
            }
            hr = ((lw_unlock_fn)primary->vtbl[DDS_UNLOCK])(primary, 0);
            lw_line("WholeUnlockHr", (DWORD)hr);
        }
    }
    CloseHandle(lw_file);
    while (GetMessageA(&msg, 0, 0, 0)) {
        DispatchMessageA(&msg);
    }
    ExitProcess(0);
}
