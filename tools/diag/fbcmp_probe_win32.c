/* Frame-buffer comparison probe: does the screen show what GDI holds?
 *
 * With the screen shadow (src\display16\shadow16.c) GDI draws into a copy
 * of the desktop in system memory and the driver copies what changed to
 * the frame buffer. GDI's own reads come from the copy, so an agent
 * screenshot cannot see a frame buffer that has fallen behind it: only the
 * monitor would. This reads both and counts the pixels that differ:
 *
 *  - GDI: BitBlt from the screen DC into a DIB section of the screen's own
 *    format. On a shadow chip that reads the shadow.
 *  - The frame buffer: a DirectDraw Lock of the primary, normal
 *    cooperative level, read only. The HAL hands out the frame buffer, not
 *    the shadow.
 *
 * GDI is read first and the primary locked after, never GDI inside the
 * lock (DirectDraw holds the Win16 lock across it). The software cursor is
 * in the frame buffer and lifted from what GDI's BitBlt reads, so pixels
 * within 64 of the cursor's position are counted apart. Anything that
 * draws between the two reads (a clock, a caret) shows as a difference:
 * run it with the desktop still.
 *
 * Writes C:\V9XDIAG\V9XFBCMP.INI: the mode, the counts, the box of the
 * differences away from the cursor, and Result=PASS when there are none,
 * FAIL when there are, or FAIL-<step> when a step could not run. 16 and
 * 32 bpp only.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include "velocity9x/build.h"
#include "velocity9x/win9x_ddraw_abi.h"

#define V9X_FBC_PATH "C:\\V9XDIAG\\V9XFBCMP.INI"
#define V9X_FBC_SECTION "FbCompare"
/* Pixels this close to the cursor's hot spot, either way, are the
 * cursor's: its shape is at most 64 square on Windows 9x. */
#define V9X_FBC_CURSOR_REACH 64l

typedef HRESULT (WINAPI *V9X_FBC_CREATE)(GUID *, LPDIRECTDRAW *, IUnknown *);

static void v9x_fbc_text(const char *key, const char *value)
{
    WritePrivateProfileStringA(V9X_FBC_SECTION, key, value, V9X_FBC_PATH);
}

static void v9x_fbc_uint(const char *key, DWORD value)
{
    char text[16];

    wsprintfA(text, "%lu", value);
    v9x_fbc_text(key, text);
}

/* ZeroMemory is memset, and this probe links no C runtime. */
static void v9x_fbc_zero(void *block, DWORD bytes)
{
    volatile BYTE *at = (volatile BYTE *)block;

    while (bytes-- != 0ul) {
        *at++ = 0u;
    }
}

/* The shadow's event log as it stood before this probe drew anything
 * (V9X_DD_SHARED.shadow_log), and how many events had been written. */
static struct v9x_shadow_event v9x_fbc_log[V9X_SHADOW_LOG_EVENTS];
static DWORD v9x_fbc_log_count;
static DWORD v9x_fbc_direction_set;
static int v9x_fbc_log_taken;

/* Copy the log out of the shared block, found as the other probes find
 * it: the escape that names the HAL hands back the block's address. */
static void v9x_fbc_take_log(void)
{
    V9X_DCICMD command;
    V9X_DD32BITDRIVERDATA driver;
    const V9X_DD_SHARED *shared;
    HDC dc;
    int result;
    DWORD index;

    v9x_fbc_zero(&command, sizeof(command));
    v9x_fbc_zero(&driver, sizeof(driver));
    command.dwCommand = V9X_DDGET32BITDRIVERNAME;
    command.dwVersion = V9X_DD_VERSION;
    dc = GetDC(0);
    result = dc != 0 ? ExtEscape(dc, V9X_DCICOMMAND, sizeof(command),
                                 (LPCSTR)&command, sizeof(driver),
                                 (LPSTR)&driver) : 0;
    if (dc != 0) {
        ReleaseDC(0, dc);
    }
    shared = (const V9X_DD_SHARED *)driver.dwContext;
    if (result <= 0 || shared == 0 ||
        IsBadReadPtr(shared, sizeof(*shared)) ||
        shared->dwSize != sizeof(*shared) ||
        shared->abi != V9X_DD_SHARED_ABI) {
        return;
    }
    v9x_fbc_log_count = shared->shadow_log_count;
    v9x_fbc_direction_set = shared->shadow_direction_set;
    for (index = 0ul; index < V9X_SHADOW_LOG_EVENTS; ++index) {
        v9x_fbc_log[index] = shared->shadow_log[index];
    }
    v9x_fbc_log_taken = 1;
}

/* The log, oldest first, one key an event: kind, detail (hex), rectangle. */
static void v9x_fbc_write_log(void)
{
    static const char *const names[] = {
        "?", "begin", "end", "take", "overlap", "copy", "output", "cursor",
        "flush-all", "stale"
    };
    DWORD first;
    DWORD number;
    char key[16];
    char value[64];

    if (!v9x_fbc_log_taken) {
        WritePrivateProfileStringA("ShadowLog", "Taken", "no", V9X_FBC_PATH);
        return;
    }
    wsprintfA(value, "%lu", v9x_fbc_log_count);
    WritePrivateProfileStringA("ShadowLog", "Count", value, V9X_FBC_PATH);
    wsprintfA(value, "%lu", v9x_fbc_direction_set);
    WritePrivateProfileStringA("ShadowLog", "DirectionSet", value,
                               V9X_FBC_PATH);
    first = v9x_fbc_log_count > V9X_SHADOW_LOG_EVENTS ?
            v9x_fbc_log_count - V9X_SHADOW_LOG_EVENTS : 0ul;
    for (number = first; number < v9x_fbc_log_count; ++number) {
        const struct v9x_shadow_event *event =
            &v9x_fbc_log[number & (V9X_SHADOW_LOG_EVENTS - 1u)];
        const char *name = event->kind < sizeof(names) / sizeof(names[0]) ?
                           names[event->kind] : "?";

        wsprintfA(key, "E%lu", number);
        wsprintfA(value, "%s %04X %d,%d,%d,%d", name,
                  (unsigned int)event->detail, (int)event->rect.left,
                  (int)event->rect.top, (int)event->rect.right,
                  (int)event->rect.bottom);
        WritePrivateProfileStringA("ShadowLog", key, value, V9X_FBC_PATH);
    }
}

#define V9X_FBC_ZOOM 8l

/* One pixel of an image in the screen's layout as 8-bit R, G, B. */
static void v9x_fbc_rgb(const BYTE *image, DWORD width, DWORD bytes,
                        LONG x, LONG y, BYTE *rgb)
{
    const BYTE *at = image + (width * (DWORD)y + (DWORD)x) * bytes;

    if (bytes == 2ul) {
        WORD pixel = *(const WORD *)at;

        rgb[0] = (BYTE)((pixel >> 8) & 0xf8u);
        rgb[1] = (BYTE)((pixel >> 3) & 0xfcu);
        rgb[2] = (BYTE)((pixel << 3) & 0xf8u);
        return;
    }
    rgb[0] = at[2];
    rgb[1] = at[1];
    rgb[2] = at[0];
}

/* A box of an image as a binary PPM, each pixel V9X_FBC_ZOOM square. */
static void v9x_fbc_ppm(const char *path, const BYTE *image, DWORD width,
                        DWORD bytes, LONG left, LONG top, LONG right,
                        LONG bottom)
{
    HANDLE file;
    char header[48];
    BYTE line[3 * 1024 * 8];
    DWORD written;
    LONG x;
    LONG y;
    LONG repeat;
    LONG box_width = right - left;
    int length;

    if (box_width * V9X_FBC_ZOOM * 3l > (LONG)sizeof(line)) {
        box_width = (LONG)sizeof(line) / (V9X_FBC_ZOOM * 3l);
    }
    file = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    length = wsprintfA(header, "P6\n%ld %ld\n255\n", box_width * V9X_FBC_ZOOM,
                       (bottom - top) * V9X_FBC_ZOOM);
    WriteFile(file, header, (DWORD)length, &written, 0);
    for (y = top; y < bottom; ++y) {
        for (x = 0l; x < box_width; ++x) {
            BYTE rgb[3];

            v9x_fbc_rgb(image, width, bytes, left + x, y, rgb);
            for (repeat = 0l; repeat < V9X_FBC_ZOOM; ++repeat) {
                BYTE *to = line + ((x * V9X_FBC_ZOOM) + repeat) * 3l;

                to[0] = rgb[0];
                to[1] = rgb[1];
                to[2] = rgb[2];
            }
        }
        for (repeat = 0l; repeat < V9X_FBC_ZOOM; ++repeat) {
            WriteFile(file, line, (DWORD)(box_width * V9X_FBC_ZOOM * 3l),
                      &written, 0);
        }
    }
    CloseHandle(file);
}

static void v9x_fbc_fail(const char *result)
{
    v9x_fbc_text("Result", result);
    ExitProcess(1u);
}

void __stdcall V9xFbCompareProbeEntry(void)
{
    V9X_FBC_CREATE create;
    LPDIRECTDRAW dd = 0;
    LPDIRECTDRAWSURFACE primary = 0;
    DDSURFACEDESC desc;
    HDC screen;
    HDC memory;
    HBITMAP bitmap;
    HGDIOBJ previous;
    BYTE *gdi_bits = 0;
    BYTE *gdi_after = 0;
    HBITMAP bitmap_after;
    DWORD changing = 0ul;
    BYTE *fb_copy;
    struct {
        BITMAPINFOHEADER header;
        DWORD masks[3];
    } info;
    POINT cursor;
    DWORD width;
    DWORD height;
    DWORD bpp;
    DWORD bytes;
    DWORD x;
    DWORD y;
    DWORD differ = 0ul;
    DWORD differ_cursor = 0ul;
    LONG box_left = 0x7fffffffl;
    LONG box_top = 0x7fffffffl;
    LONG box_right = -1l;
    LONG box_bottom = -1l;
    char text[64];

    /* "/delay" on the command line: wait 15 s first, so a remote agent
     * that started this has gone idle (its tray icon changes with it). */
    {
        const char *command_line = GetCommandLineA();

        while (*command_line != '\0') {
            if (command_line[0] == '/' && command_line[1] == 'd') {
                Sleep(15000ul);
                break;
            }
            ++command_line;
        }
    }
    /* The log first, before this probe's own GDI read adds to it. */
    v9x_fbc_take_log();
    CreateDirectoryA("C:\\V9XDIAG", 0);
    WritePrivateProfileStringA(V9X_FBC_SECTION, 0, 0, V9X_FBC_PATH);
    WritePrivateProfileStringA("ShadowLog", 0, 0, V9X_FBC_PATH);
    v9x_fbc_text("Build", V9X_BUILD_ID);

    screen = GetDC(0);
    width = (DWORD)GetDeviceCaps(screen, HORZRES);
    height = (DWORD)GetDeviceCaps(screen, VERTRES);
    bpp = (DWORD)GetDeviceCaps(screen, BITSPIXEL);
    v9x_fbc_uint("Width", width);
    v9x_fbc_uint("Height", height);
    v9x_fbc_uint("BitsPerPixel", bpp);
    if (bpp != 16ul && bpp != 32ul) {
        v9x_fbc_fail("FAIL-DEPTH");
    }
    bytes = bpp / 8ul;

    /* The screen's own layout: 5:6:5 or 8:8:8, top down. */
    v9x_fbc_zero(&info, sizeof(info));
    info.header.biSize = sizeof(info.header);
    info.header.biWidth = (LONG)width;
    info.header.biHeight = -(LONG)height;
    info.header.biPlanes = 1;
    info.header.biBitCount = (WORD)bpp;
    info.header.biCompression = BI_BITFIELDS;
    if (bpp == 16ul) {
        info.masks[0] = 0xf800ul;
        info.masks[1] = 0x07e0ul;
        info.masks[2] = 0x001ful;
    } else {
        info.masks[0] = 0x00ff0000ul;
        info.masks[1] = 0x0000ff00ul;
        info.masks[2] = 0x000000fful;
    }
    memory = CreateCompatibleDC(screen);
    bitmap = CreateDIBSection(screen, (BITMAPINFO *)&info, DIB_RGB_COLORS,
                              (void **)&gdi_bits, 0, 0);
    if (memory == 0 || bitmap == 0 || gdi_bits == 0) {
        v9x_fbc_fail("FAIL-DIB");
    }
    previous = SelectObject(memory, bitmap);
    GetCursorPos(&cursor);
    if (!BitBlt(memory, 0, 0, (int)width, (int)height, screen, 0, 0,
                SRCCOPY)) {
        v9x_fbc_fail("FAIL-BITBLT");
    }
    GdiFlush();
    ReleaseDC(0, screen);

    create = (V9X_FBC_CREATE)GetProcAddress(LoadLibraryA("DDRAW.DLL"),
                                            "DirectDrawCreate");
    if (create == 0 || create(0, &dd, 0) != DD_OK) {
        v9x_fbc_fail("FAIL-DDRAW");
    }
    if (IDirectDraw_SetCooperativeLevel(dd, 0, DDSCL_NORMAL) != DD_OK) {
        v9x_fbc_fail("FAIL-COOPERATIVE");
    }
    v9x_fbc_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    if (IDirectDraw_CreateSurface(dd, &desc, &primary, 0) != DD_OK) {
        v9x_fbc_fail("FAIL-PRIMARY");
    }
    v9x_fbc_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    fb_copy = (BYTE *)VirtualAlloc(0, width * bytes * height,
                                   MEM_COMMIT, PAGE_READWRITE);
    if (fb_copy == 0) {
        v9x_fbc_fail("FAIL-ALLOC");
    }
    if (IDirectDrawSurface_Lock(primary, 0, &desc,
                                DDLOCK_WAIT | DDLOCK_READONLY, 0) != DD_OK) {
        v9x_fbc_fail("FAIL-LOCK");
    }
    /* Copied out under the lock and compared after it, so the lock is held
     * only as long as the bus read takes. */
    for (y = 0ul; y < height; ++y) {
        const BYTE *row = (const BYTE *)desc.lpSurface +
                          (DWORD)desc.lPitch * y;
        BYTE *to = fb_copy + width * bytes * y;

        for (x = 0ul; x < width * bytes; x += 4ul) {
            *(DWORD *)(to + x) = *(const DWORD *)(row + x);
        }
    }
    IDirectDrawSurface_Unlock(primary, 0);

    /* GDI again, after the frame buffer: a pixel the two GDI reads disagree
     * on was changing on the screen (a blinking tray icon, 2026-11-12), and
     * the frame buffer read between them may hold either. It counts as
     * equal when it matches either read. */
    bitmap_after = CreateDIBSection(memory, (BITMAPINFO *)&info,
                                    DIB_RGB_COLORS, (void **)&gdi_after, 0, 0);
    if (bitmap_after == 0 || gdi_after == 0) {
        v9x_fbc_fail("FAIL-DIB");
    }
    SelectObject(memory, bitmap_after);
    screen = GetDC(0);
    if (!BitBlt(memory, 0, 0, (int)width, (int)height, screen, 0, 0,
                SRCCOPY)) {
        v9x_fbc_fail("FAIL-BITBLT");
    }
    GdiFlush();
    ReleaseDC(0, screen);

    for (y = 0ul; y < height; ++y) {
        const BYTE *fb = fb_copy + width * bytes * y;
        const BYTE *gdi = gdi_bits + width * bytes * y;
        const BYTE *gdi2 = gdi_after + width * bytes * y;

        for (x = 0ul; x < width; ++x) {
            DWORD fb_pixel = bytes == 2ul ? *(const WORD *)(fb + x * 2ul) :
                             *(const DWORD *)(fb + x * 4ul) & 0x00fffffful;
            DWORD gdi_pixel = bytes == 2ul ?
                              *(const WORD *)(gdi + x * 2ul) :
                              *(const DWORD *)(gdi + x * 4ul) & 0x00fffffful;
            DWORD gdi2_pixel = bytes == 2ul ?
                               *(const WORD *)(gdi2 + x * 2ul) :
                               *(const DWORD *)(gdi2 + x * 4ul) &
                                   0x00fffffful;
            LONG dx = (LONG)x - cursor.x;
            LONG dy = (LONG)y - cursor.y;

            if (gdi_pixel != gdi2_pixel) {
                ++changing;
            }
            if (fb_pixel == gdi_pixel || fb_pixel == gdi2_pixel) {
                continue;
            }
            if (dx > -V9X_FBC_CURSOR_REACH && dx < V9X_FBC_CURSOR_REACH &&
                dy > -V9X_FBC_CURSOR_REACH && dy < V9X_FBC_CURSOR_REACH) {
                ++differ_cursor;
                continue;
            }
            ++differ;
            if ((LONG)x < box_left) {
                box_left = (LONG)x;
            }
            if ((LONG)y < box_top) {
                box_top = (LONG)y;
            }
            if ((LONG)x > box_right) {
                box_right = (LONG)x;
            }
            if ((LONG)y > box_bottom) {
                box_bottom = (LONG)y;
            }
        }
    }
    IDirectDrawSurface_Release(primary);
    IDirectDraw_Release(dd);

    v9x_fbc_uint("Pitch", (DWORD)desc.lPitch);
    v9x_fbc_uint("CursorX", (DWORD)cursor.x);
    v9x_fbc_uint("CursorY", (DWORD)cursor.y);
    v9x_fbc_uint("DifferNearCursor", differ_cursor);
    v9x_fbc_uint("Changing", changing);
    v9x_fbc_uint("Differ", differ);
    if (differ != 0ul) {
        wsprintfA(text, "%ld,%ld,%ld,%ld", box_left, box_top,
                  box_right + 1l, box_bottom + 1l);
        v9x_fbc_text("DifferBox", text);
        /* The box from each side, eight times enlarged, to look at. */
        v9x_fbc_ppm("C:\\V9XDIAG\\V9XFBCFB.PPM", fb_copy, width, bytes,
                    box_left, box_top, box_right + 1l, box_bottom + 1l);
        v9x_fbc_ppm("C:\\V9XDIAG\\V9XFBCGD.PPM", gdi_bits, width, bytes,
                    box_left, box_top, box_right + 1l, box_bottom + 1l);
    }
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteObject(bitmap_after);
    DeleteDC(memory);
    v9x_fbc_write_log();
    v9x_fbc_text("Result", differ == 0ul ? "PASS" : "FAIL");
    ExitProcess(differ == 0ul ? 0u : 2u);
}
