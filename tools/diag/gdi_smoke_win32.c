#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/win9x_ddraw_abi.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_TEST_MESSAGE (WM_APP + 1u)

static const char v9x_class_name[] = "Velocity9xGdiSmokeWindow";
static const char v9x_title[] = "Velocity9x GDI framebuffer test";
static const char v9x_full_title[] =
    "Velocity9x GDI framebuffer test - " V9X_BUILD_ID;
static int v9x_test_posted;
static int v9x_auto_mode;
static DWORD v9x_exit_code;

static int v9x_ascii_equal_ci(char left, char right)
{
    if (left >= 'A' && left <= 'Z') {
        left = (char)(left + ('a' - 'A'));
    }
    if (right >= 'A' && right <= 'Z') {
        right = (char)(right + ('a' - 'A'));
    }
    return left == right;
}

static int v9x_has_switch(const char *command_line, const char *option)
{
    int offset;
    int index;

    for (offset = 0; command_line[offset] != '\0'; ++offset) {
        for (index = 0; option[index] != '\0'; ++index) {
            if (command_line[offset + index] == '\0' ||
                !v9x_ascii_equal_ci(command_line[offset + index],
                                    option[index])) {
                break;
            }
        }
        if (option[index] == '\0') {
            return 1;
        }
    }
    return 0;
}

static int v9x_has_auto_switch(const char *command_line)
{
    return v9x_has_switch(command_line, "/auto");
}

static void v9x_uint_text(char *text, UINT value)
{
    char reverse[12];
    int count = 0;
    int index;

    do {
        reverse[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    for (index = 0; index < count; ++index) {
        text[index] = reverse[count - index - 1];
    }
    text[count] = '\0';
}

static void v9x_write_auto_result(HDC display, int passed)
{
    char number[12];
    POINT cursor;
    const char result_path[] = "C:\\V9XGDI.INI";

    WritePrivateProfileStringA("Velocity9xGDI", 0, 0, result_path);
    WritePrivateProfileStringA("Velocity9xGDI", "Result",
                               passed ? "PASS" : "FAIL", result_path);
    WritePrivateProfileStringA("Velocity9xGDI", "Build", V9X_BUILD_ID,
                               result_path);
    v9x_uint_text(number, (UINT)GetDeviceCaps(display, HORZRES));
    WritePrivateProfileStringA("Velocity9xGDI", "Width", number, result_path);
    v9x_uint_text(number, (UINT)GetDeviceCaps(display, VERTRES));
    WritePrivateProfileStringA("Velocity9xGDI", "Height", number, result_path);
    v9x_uint_text(number, (UINT)(GetDeviceCaps(display, BITSPIXEL) *
                                GetDeviceCaps(display, PLANES)));
    WritePrivateProfileStringA("Velocity9xGDI", "BitsPerPixel", number,
                               result_path);
    v9x_uint_text(number, (UINT)GetPixel(display, 28, 72));
    WritePrivateProfileStringA("Velocity9xGDI", "BlackPixel", number,
                               result_path);
    v9x_uint_text(number, (UINT)GetPixel(display, 82, 72));
    WritePrivateProfileStringA("Velocity9xGDI", "WhitePixel", number,
                               result_path);
    v9x_uint_text(number, (UINT)GetPixel(display, 136, 72));
    WritePrivateProfileStringA("Velocity9xGDI", "RedPixel", number,
                               result_path);
    v9x_uint_text(number, (UINT)GetPixel(display, 28, 236));
    WritePrivateProfileStringA("Velocity9xGDI", "BltPixel", number,
                               result_path);
    v9x_uint_text(number, (UINT)GetPixel(display, 330, 250));
    WritePrivateProfileStringA("Velocity9xGDI", "SetPixel", number,
                               result_path);
    if (GetCursorPos(&cursor)) {
        v9x_uint_text(number, (UINT)cursor.x);
        WritePrivateProfileStringA("Velocity9xGDI", "CursorX", number,
                                   result_path);
        v9x_uint_text(number, (UINT)cursor.y);
        WritePrivateProfileStringA("Velocity9xGDI", "CursorY", number,
                                   result_path);
    }
    WritePrivateProfileStringA(0, 0, 0, result_path);
}

static int v9x_string_length(const char *text)
{
    int length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static int v9x_channel_near(BYTE actual, BYTE expected)
{
    int difference = (int)actual - (int)expected;
    if (difference < 0) {
        difference = -difference;
    }
    return difference <= 64;
}

static int v9x_color_near(COLORREF actual, COLORREF expected)
{
    return actual != CLR_INVALID &&
           v9x_channel_near(GetRValue(actual), GetRValue(expected)) &&
           v9x_channel_near(GetGValue(actual), GetGValue(expected)) &&
           v9x_channel_near(GetBValue(actual), GetBValue(expected));
}

static void v9x_paint_pattern(HWND window, HDC display)
{
    static const COLORREF colors[] = {
        RGB(0, 0, 0), RGB(255, 255, 255), RGB(255, 0, 0),
        RGB(0, 255, 0), RGB(0, 0, 255), RGB(255, 255, 0),
        RGB(0, 255, 255), RGB(255, 0, 255)
    };
    RECT client;
    RECT area;
    HBRUSH brush;
    HPEN pen;
    HPEN old_pen;
    HDC memory;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    WORD index;

    GetClientRect(window, &client);
    FillRect(display, &client, (HBRUSH)(COLOR_WINDOW + 1));
    SetBkMode(display, TRANSPARENT);
    SetTextColor(display, RGB(0, 0, 0));
    TextOutA(display, 18, 16,
        "DIB Engine screen-path test: colors, lines, text and blits",
        v9x_string_length(
          "DIB Engine screen-path test: colors, lines, text and blits"));

    for (index = 0u; index < 8u; ++index) {
        area.left = 18 + (int)index * 54;
        area.top = 48;
        area.right = area.left + 48;
        area.bottom = 112;
        brush = CreateSolidBrush(colors[index]);
        FillRect(display, &area, brush);
        DeleteObject(brush);
    }

    pen = CreatePen(PS_SOLID, 2, RGB(0, 0, 128));
    old_pen = (HPEN)SelectObject(display, pen);
    MoveToEx(display, 18, 136, 0);
    LineTo(display, 450, 136);
    MoveToEx(display, 18, 145, 0);
    LineTo(display, 450, 205);
    SelectObject(display, old_pen);
    DeleteObject(pen);

    brush = CreateSolidBrush(RGB(0, 0, 255));
    memory = CreateCompatibleDC(display);
    bitmap = CreateCompatibleBitmap(display, 96, 64);
    old_bitmap = (HBITMAP)SelectObject(memory, bitmap);
    area.left = 0;
    area.top = 0;
    area.right = 96;
    area.bottom = 64;
    FillRect(memory, &area, brush);
    DeleteObject(brush);
    brush = CreateSolidBrush(RGB(255, 255, 255));
    area.left = 38;
    area.right = 58;
    FillRect(memory, &area, brush);
    area.left = 0;
    area.top = 22;
    area.right = 96;
    area.bottom = 42;
    FillRect(memory, &area, brush);
    DeleteObject(brush);

    BitBlt(display, 18, 226, 96, 64, memory, 0, 0, SRCCOPY);
    StretchBlt(display, 142, 216, 144, 84, memory, 0, 0, 96, 64, SRCCOPY);
    SelectObject(memory, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);

    SetPixel(display, 330, 250, RGB(255, 0, 0));
    SetPixel(display, 331, 250, RGB(255, 0, 0));
    SetPixel(display, 330, 251, RGB(255, 0, 0));
    SetPixel(display, 331, 251, RGB(255, 0, 0));
    TextOutA(display, 18, 315,
        "Leave this window visible while checking the software cursor.",
        v9x_string_length(
          "Leave this window visible while checking the software cursor."));

    if (!v9x_test_posted) {
        v9x_test_posted = 1;
        PostMessageA(window, V9X_TEST_MESSAGE, 0, 0);
    }
}

static void v9x_check_pixels(HWND window)
{
    HDC display = GetDC(window);
    int passed;

    passed = v9x_color_near(GetPixel(display, 28, 72), RGB(0, 0, 0)) &&
             v9x_color_near(GetPixel(display, 82, 72), RGB(255, 255, 255)) &&
             v9x_color_near(GetPixel(display, 136, 72), RGB(255, 0, 0)) &&
             v9x_color_near(GetPixel(display, 28, 236), RGB(0, 0, 255)) &&
             v9x_color_near(GetPixel(display, 330, 250), RGB(255, 0, 0));
    if (v9x_auto_mode) {
        v9x_write_auto_result(display, passed);
    }
    ReleaseDC(window, display);

    if (v9x_auto_mode) {
        v9x_exit_code = passed ? 0ul : 3ul;
        DestroyWindow(window);
        return;
    }

    MessageBoxA(window,
        passed ?
          "PASS: display writes, BitBlt and pixel readback are coherent. "
          "Move the cursor around the pattern, then close the window." :
          "FAIL: one or more framebuffer pixels did not read back as drawn. "
          "Keep the window visible and report the color corruption.",
        v9x_title, MB_OK | (passed ? MB_ICONINFORMATION : MB_ICONERROR));
}

static LRESULT CALLBACK v9x_window_proc(HWND window,
                                        UINT message,
                                        WPARAM wparam,
                                        LPARAM lparam)
{
    (void)wparam;
    (void)lparam;
    switch (message) {
    case WM_PAINT:
        {
            PAINTSTRUCT paint;
            HDC display = BeginPaint(window, &paint);
            v9x_paint_pattern(window, display);
            EndPaint(window, &paint);
        }
        return 0;
    case V9X_TEST_MESSAGE:
        v9x_check_pixels(window);
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage((int)v9x_exit_code);
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

/* ------------------------------------------------------------------------
 * /accel: the phase that can fail.
 *
 * The existing smoke path above is deliberately untouched, so Result=PASS in
 * C:\V9XGDI.INI keeps exactly the meaning the mode matrix already relies on.
 * This phase writes its own file.
 *
 * What it is for: a seeded stream of operations drawn on the screen and
 * mirrored into a reference DC, compared periodically, and then checked
 * against the driver's own counters. The comparison alone is not enough and
 * that is the whole point - a comparison harness that silently exercised the
 * decline path on every operation would pass perfectly and prove nothing,
 * which is exactly how the ati package shipped unable to enable for a release
 * (docs\issues\2026-08-26-ati-package-cannot-enable.md). Every check it had to
 * pass was a check it could pass without working.
 *
 * So the counters are read, and the run fails when a primitive is advertised
 * and enabled and its counter is nonetheless zero. On a build with nothing
 * enabled - which is every default build 000 - that check cannot fire, and the
 * one that can is Calls: if the dispatcher were not actually wired to ordinal
 * 1, no blit would have reached it at all.
 *
 * Colours come from a fixed table of 16 static system-palette entries. That is
 * not decoration either: on an 8-bpp desktop an arbitrary RGB is mapped to the
 * nearest palette entry, and comparing a screen readback against a reference
 * DC only means something if both map the same way. Static entries map to
 * themselves at every depth this driver offers.
 * ------------------------------------------------------------------------ */

#define V9X_ACCEL_PATH        "C:\\V9XACCE.INI"
#define V9X_ACCEL_SECTION     "Velocity9xAccel"
#define V9X_ACCEL_CLASS       "Velocity9xAccelWindow"
#define V9X_ACCEL_OPERATIONS  500
#define V9X_ACCEL_COMPARE_EVERY 25
#define V9X_ACCEL_WIDTH       320
#define V9X_ACCEL_HEIGHT      240
#define V9X_ACCEL_MARGIN        8

static const COLORREF v9x_accel_colors[16] = {
    RGB(0, 0, 0),       RGB(128, 0, 0),     RGB(0, 128, 0),
    RGB(128, 128, 0),   RGB(0, 0, 128),     RGB(128, 0, 128),
    RGB(0, 128, 128),   RGB(192, 192, 192), RGB(128, 128, 128),
    RGB(255, 0, 0),     RGB(0, 255, 0),     RGB(255, 255, 0),
    RGB(0, 0, 255),     RGB(255, 0, 255),   RGB(0, 255, 255),
    RGB(255, 255, 255)
};

/* The eight overlap directions a same-surface copy has to get right, plus the
 * two axis-aligned zero cases that make it eight rather than four. */
static const int v9x_accel_shift_x[8] = { -1, 0, 1, 1, 1, 0, -1, -1 };
static const int v9x_accel_shift_y[8] = { -1, -1, -1, 0, 1, 1, 1, 0 };

static DWORD v9x_accel_seed = 0x13579bdful;

static DWORD v9x_accel_random(DWORD limit)
{
    v9x_accel_seed = v9x_accel_seed * 1103515245ul + 12345ul;
    if (limit == 0ul) {
        return 0ul;
    }
    return ((v9x_accel_seed >> 16) & 0x7ffful) % limit;
}

static void v9x_accel_write_text(const char *key, const char *value)
{
    WritePrivateProfileStringA(V9X_ACCEL_SECTION, key, value, V9X_ACCEL_PATH);
}

static void v9x_accel_write_uint(const char *key, DWORD value)
{
    char text[12];

    v9x_uint_text(text, (UINT)value);
    v9x_accel_write_text(key, text);
}

/*
 * The reference and the screen are COMPARED in 24-bpp RGB, but they are DRAWN
 * at the screen's own depth. The distinction is not cosmetic, and getting it
 * wrong is what the first run of this phase found:
 *
 * The reference bitmap was created as a 24-bpp DIBSection, and the comparison
 * failed identically on the ViRGE and on the engine-less ATI guest - same
 * operation, same byte - on a build where every operation declined and the DIB
 * Engine therefore drew both sides. The cause was PATINVERT, the decline-noise
 * operation: it XORs the destination, and on an 8-bpp desktop that XORs palette
 * *indices* while a 24-bpp reference XORs RGB *bytes*. Two different answers,
 * neither wrong, compared against each other.
 *
 * So the reference is a compatible bitmap - same format as the screen - and
 * only the readback is 24 bpp. Both sides then go through GetDIBits with the
 * same request, so the conversion is the same conversion, and one code path
 * covers 8 and 16 bpp. Any destination-dependent ROP is comparable again.
 */
static DWORD v9x_accel_dib_bytes(void)
{
    return (DWORD)(((V9X_ACCEL_WIDTH * 3 + 3) & ~3) * V9X_ACCEL_HEIGHT);
}

static void v9x_accel_fill_header(BITMAPINFO *info)
{
    unsigned index;
    unsigned char *bytes = (unsigned char *)info;

    for (index = 0u; index < sizeof(BITMAPINFO); ++index) {
        bytes[index] = 0u;
    }
    info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info->bmiHeader.biWidth = V9X_ACCEL_WIDTH;
    /* Negative height would be a top-down DIB; either orientation compares
     * equal as long as both sides ask for the same one. */
    info->bmiHeader.biHeight = V9X_ACCEL_HEIGHT;
    info->bmiHeader.biPlanes = 1;
    info->bmiHeader.biBitCount = 24;
    info->bmiHeader.biCompression = BI_RGB;
}

static int v9x_accel_capture(HDC source, HBITMAP bitmap, unsigned char *bits)
{
    BITMAPINFO info;

    v9x_accel_fill_header(&info);
    return GetDIBits(source, bitmap, 0, V9X_ACCEL_HEIGHT, bits, &info,
                     DIB_RGB_COLORS) == V9X_ACCEL_HEIGHT;
}

/* First differing byte, or -1. Hand-rolled: this tool links no C runtime. */
static long v9x_accel_first_difference(const unsigned char *left,
                                       const unsigned char *right,
                                       DWORD bytes)
{
    DWORD index;

    for (index = 0ul; index < bytes; ++index) {
        if (left[index] != right[index]) {
            return (long)index;
        }
    }
    return -1l;
}

static DWORD v9x_accel_difference_count(const unsigned char *left,
                                        const unsigned char *right,
                                        DWORD bytes)
{
    DWORD index;
    DWORD count = 0ul;

    for (index = 0ul; index < bytes; ++index) {
        if (left[index] != right[index]) {
            ++count;
        }
    }
    return count;
}

typedef struct v9x_accel_state {
    HDC screen;          /* the window's client DC - the real framebuffer   */
    /* Memory DC every operation is mirrored into, at the screen's own pixel
     * format. This is the software reference: a memory bitmap is drawn by the
     * DIB Engine, which is precisely what the accelerated path has to agree
     * with. */
    HDC reference;
    HBITMAP reference_bitmap;
    HDC capture;         /* where the screen region is read back to         */
    HBITMAP capture_bitmap;
    unsigned char *screen_bits;
    unsigned char *reference_bits;
    int origin_x;
    int origin_y;
    HWND window;
} V9X_ACCEL_STATE;

/*
 * Enough about a mismatch to tell the failure modes apart without another
 * round trip to a guest. A wrong colour puts a specific value on the screen
 * side; a stale read leaves the screen holding what the previous operation
 * put there; a wrong rectangle disagrees over a shape rather than a hue. The
 * count and the coordinates are what separate those three.
 */
static void v9x_accel_report_mismatch(const V9X_ACCEL_STATE *state,
                                      long difference)
{
    DWORD stride = (DWORD)((V9X_ACCEL_WIDTH * 3 + 3) & ~3);
    DWORD offset = (DWORD)difference;
    DWORD row = offset / stride;
    DWORD column = (offset - row * stride) / 3ul;

    v9x_accel_write_uint("MismatchByte", offset);
    /* The DIB is bottom-up, so row 0 is the bottom scan line. */
    v9x_accel_write_uint("MismatchX", column);
    v9x_accel_write_uint("MismatchY", (DWORD)(V9X_ACCEL_HEIGHT - 1) - row);
    v9x_accel_write_uint("MismatchBytes",
                         v9x_accel_difference_count(state->screen_bits,
                                                    state->reference_bits,
                                                    v9x_accel_dib_bytes()));
    v9x_accel_write_uint("MismatchScreenB", state->screen_bits[offset]);
    v9x_accel_write_uint("MismatchRefB", state->reference_bits[offset]);
    if (offset + 2ul < v9x_accel_dib_bytes()) {
        v9x_accel_write_uint("MismatchScreenG", state->screen_bits[offset + 1]);
        v9x_accel_write_uint("MismatchRefG", state->reference_bits[offset + 1]);
        v9x_accel_write_uint("MismatchScreenR", state->screen_bits[offset + 2]);
        v9x_accel_write_uint("MismatchRefR", state->reference_bits[offset + 2]);
    }
}

/*
 * One operation, issued twice: once against the screen and once against the
 * reference DC, with identical coordinates. Anything the driver declines still
 * has to produce the same pixels, which is what makes the decline-noise
 * operations worth issuing.
 */
static void v9x_accel_operation(V9X_ACCEL_STATE *state, int index,
                                DWORD *fills, DWORD *copies, DWORD *overlaps,
                                DWORD *noise)
{
    int kind = (int)v9x_accel_random(10ul);
    int width;
    int height;
    int x;
    int y;
    HBRUSH brush;
    HBRUSH previous_screen;
    HBRUSH previous_reference;
    COLORREF color = v9x_accel_colors[v9x_accel_random(16ul)];

    if (kind <= 4) {
        /* Solid fill. One in five is deliberately below the driver's
         * accelerated pixel threshold, so the gate that rejects a small
         * rectangle is exercised rather than assumed. */
        int small = (index % 5) == 0;

        width = small ? 4 + (int)v9x_accel_random(12ul)
                      : 40 + (int)v9x_accel_random(140ul);
        height = small ? 4 + (int)v9x_accel_random(12ul)
                       : 40 + (int)v9x_accel_random(120ul);
        x = (int)v9x_accel_random((DWORD)(V9X_ACCEL_WIDTH - width));
        y = (int)v9x_accel_random((DWORD)(V9X_ACCEL_HEIGHT - height));
        brush = CreateSolidBrush(color);
        previous_screen = (HBRUSH)SelectObject(state->screen, brush);
        previous_reference = (HBRUSH)SelectObject(state->reference, brush);
        if (kind == 0) {
            PatBlt(state->screen, state->origin_x + x, state->origin_y + y,
                   width, height, BLACKNESS);
            PatBlt(state->reference, x, y, width, height, BLACKNESS);
        } else if (kind == 1) {
            PatBlt(state->screen, state->origin_x + x, state->origin_y + y,
                   width, height, WHITENESS);
            PatBlt(state->reference, x, y, width, height, WHITENESS);
        } else {
            PatBlt(state->screen, state->origin_x + x, state->origin_y + y,
                   width, height, PATCOPY);
            PatBlt(state->reference, x, y, width, height, PATCOPY);
        }
        SelectObject(state->screen, previous_screen);
        SelectObject(state->reference, previous_reference);
        DeleteObject(brush);
        ++*fills;
        return;
    }
    if (kind <= 6) {
        /*
         * Non-overlapping screen-to-screen SRCCOPY - what build 002 turns on.
         *
         * Disjointness is constructed rather than hoped for: the source lies
         * entirely in the left half of the test area and the destination
         * entirely in the right, so they cannot intersect on x whatever the y
         * offsets are. The y offsets still vary, which exercises the engine's
         * scan direction on a copy where direction cannot affect the result.
         *
         * The smallest rectangle here is 32x32 = 1024 pixels, which is exactly
         * the accelerated threshold and therefore lands on its boundary - the
         * gate declines below it, so this is the smallest copy that should be
         * accepted.
         */
        int half = V9X_ACCEL_WIDTH / 2;
        int source_x;
        int source_y;

        width = 32 + (int)v9x_accel_random(64ul);
        height = 32 + (int)v9x_accel_random(48ul);
        source_x = (int)v9x_accel_random((DWORD)(half - width));
        source_y = (int)v9x_accel_random((DWORD)(V9X_ACCEL_HEIGHT - height));
        x = half + (int)v9x_accel_random((DWORD)(half - width));
        y = (int)v9x_accel_random((DWORD)(V9X_ACCEL_HEIGHT - height));
        BitBlt(state->screen, state->origin_x + x, state->origin_y + y,
               width, height, state->screen,
               state->origin_x + source_x, state->origin_y + source_y,
               SRCCOPY);
        BitBlt(state->reference, x, y, width, height, state->reference,
               source_x, source_y, SRCCOPY);
        ++*copies;
        return;
    }
    if (kind == 7) {
        /*
         * Overlapping screen-to-screen SRCCOPY. The shift table walks all eight
         * directions in turn and the shift is always smaller than the
         * rectangle, so the two really do overlap.
         *
         * Until build 003 these must all DECLINE, and the run checks that they
         * did - an overlapping copy accelerated by an engine walking from the
         * wrong corner produces a smeared rectangle, so "we declined it" is a
         * claim worth verifying rather than assuming.
         */
        int direction = index % 8;
        int shift = 8 + (int)v9x_accel_random(24ul);
        int source_x;
        int source_y;

        width = 48 + (int)v9x_accel_random(120ul);
        height = 48 + (int)v9x_accel_random(100ul);
        source_x = 40 + (int)v9x_accel_random(
            (DWORD)(V9X_ACCEL_WIDTH - width - 80));
        source_y = 40 + (int)v9x_accel_random(
            (DWORD)(V9X_ACCEL_HEIGHT - height - 80));
        x = source_x + v9x_accel_shift_x[direction] * shift;
        y = source_y + v9x_accel_shift_y[direction] * shift;
        BitBlt(state->screen, state->origin_x + x, state->origin_y + y,
               width, height, state->screen,
               state->origin_x + source_x, state->origin_y + source_y,
               SRCCOPY);
        BitBlt(state->reference, x, y, width, height, state->reference,
               source_x, source_y, SRCCOPY);
        ++*overlaps;
        return;
    }
    /*
     * Decline noise. PATINVERT is not among the ROPs any build accepts, and a
     * memory-source blit has a source the engine cannot read, so both must go
     * to the DIB Engine - and must still land the same pixels on both sides.
     */
    width = 32 + (int)v9x_accel_random(96ul);
    height = 32 + (int)v9x_accel_random(80ul);
    x = (int)v9x_accel_random((DWORD)(V9X_ACCEL_WIDTH - width));
    y = (int)v9x_accel_random((DWORD)(V9X_ACCEL_HEIGHT - height));
    brush = CreateSolidBrush(color);
    previous_screen = (HBRUSH)SelectObject(state->screen, brush);
    previous_reference = (HBRUSH)SelectObject(state->reference, brush);
    PatBlt(state->screen, state->origin_x + x, state->origin_y + y,
           width, height, PATINVERT);
    PatBlt(state->reference, x, y, width, height, PATINVERT);
    SelectObject(state->screen, previous_screen);
    SelectObject(state->reference, previous_reference);
    DeleteObject(brush);
    ++*noise;
}

static int v9x_accel_compare(V9X_ACCEL_STATE *state, long *difference)
{
    BitBlt(state->capture, 0, 0, V9X_ACCEL_WIDTH, V9X_ACCEL_HEIGHT,
           state->screen, state->origin_x, state->origin_y, SRCCOPY);
    if (!v9x_accel_capture(state->capture, state->capture_bitmap,
                           state->screen_bits) ||
        !v9x_accel_capture(state->reference, state->reference_bitmap,
                           state->reference_bits)) {
        *difference = -2l;
        return 0;
    }
    *difference = v9x_accel_first_difference(state->screen_bits,
                                            state->reference_bits,
                                            v9x_accel_dib_bytes());
    return *difference < 0l;
}

static int v9x_accel_read_stats(HDC screen, V9X_GDI_STATS *stats)
{
    V9X_DCICMD command;
    unsigned char *bytes = (unsigned char *)stats;
    unsigned index;

    for (index = 0u; index < sizeof(V9X_GDI_STATS); ++index) {
        bytes[index] = 0u;
    }
    command.dwCommand = V9X_GDIGETSTATS;
    command.dwParam1 = 0ul;
    command.dwParam2 = 0ul;
    command.dwVersion = V9X_DD_VERSION;
    command.dwReserved = 0ul;
    if (ExtEscape(screen, V9X_DCICOMMAND, sizeof(command),
                  (LPCSTR)&command, sizeof(V9X_GDI_STATS),
                  (LPSTR)stats) <= 0) {
        return 0;
    }
    return stats->dwSize == sizeof(V9X_GDI_STATS);
}

static int v9x_accel_arm_fault(HDC screen, DWORD count)
{
    V9X_DCICMD command;

    command.dwCommand = V9X_GDIFAULTINJECT;
    command.dwParam1 = count;
    command.dwParam2 = 0ul;
    command.dwVersion = V9X_DD_VERSION;
    command.dwReserved = 0ul;
    return ExtEscape(screen, V9X_DCICOMMAND, sizeof(command),
                     (LPCSTR)&command, 0, 0) > 0;
}

static void v9x_accel_report_stats(const V9X_GDI_STATS *stats)
{
    v9x_accel_write_uint("Advertised", stats->advertised);
    v9x_accel_write_uint("Enabled", stats->enabled);
    v9x_accel_write_uint("EngineType", stats->engine_type);
    v9x_accel_write_uint("Threshold", stats->threshold);
    v9x_accel_write_uint("Calls", stats->calls);
    v9x_accel_write_uint("Declines", stats->declines);
    v9x_accel_write_uint("Fills", stats->fills);
    v9x_accel_write_uint("Copies", stats->copies);
    v9x_accel_write_uint("IdleTimeouts", stats->idle_timeouts);
    v9x_accel_write_uint("FifoTimeouts", stats->fifo_timeouts);
    v9x_accel_write_uint("Resets", stats->resets);
    v9x_accel_write_uint("Poisoned", stats->poisoned);
    v9x_accel_write_uint("FaultInjectRemaining", stats->fault_inject);
    v9x_accel_write_uint("Drains", stats->drains);
    v9x_accel_write_uint("DeclineDisabled", stats->decline_disabled);
    v9x_accel_write_uint("DeclinePoisoned", stats->decline_poisoned);
    v9x_accel_write_uint("DeclineNotScreen", stats->decline_not_screen);
    v9x_accel_write_uint("DeclineBusy", stats->decline_busy);
    v9x_accel_write_uint("DeclinePaletteXlat", stats->decline_palette_xlat);
    v9x_accel_write_uint("DeclineDepth", stats->decline_depth);
    v9x_accel_write_uint("DeclineRop", stats->decline_rop);
    v9x_accel_write_uint("DeclineGeometry", stats->decline_geometry);
    v9x_accel_write_uint("DeclineOverlap", stats->decline_overlap);
    v9x_accel_write_uint("DeclineThreshold", stats->decline_threshold);
    v9x_accel_write_uint("DeclineEngine", stats->decline_engine);
    v9x_accel_write_uint("LastRop256", stats->last_rop256);
    v9x_accel_write_uint("LastColor", stats->last_color);
    v9x_accel_write_uint("LastBrushFlags", stats->last_brush_flags);
    v9x_accel_write_uint("LastBrushBpp", stats->last_brush_bpp);
    v9x_accel_write_uint("LastBrushStyle", stats->last_brush_style);
    v9x_accel_write_uint("LastBpp", stats->last_bpp);
}

/* ------------------------------------------------------------------------
 * /soak: a window drag and scroll pass.
 *
 * What this is NOT, and why, because the gap matters more than the coverage:
 *
 * A copy makes the engine READ the framebuffer, so a software cursor sitting
 * over the source rectangle is part of the pixels it copies, and an exclusion
 * covering only the destination leaves that cursor to be duplicated into the
 * destination. The reference driver keeps a separate B_SWCursorExcludeUnion for
 * exactly this (98DDK\src\display\mini\s3v\S3BLT.ASM), and this driver had
 * only the destination form until build 002 fixed it.
 *
 * An automated check for that was written, and then measured to be vacuous: a
 * driver deliberately built to exclude only the destination passed it 40 out of
 * 40 iterations. It was removed rather than kept, and the reason it cannot work
 * is worth recording, because it is a property of the cursor contract rather
 * than a shortage of effort:
 *
 *   Every way a Win32 application can read the screen goes through GDI, GDI
 *   goes through the DIB Engine, and the DIB Engine announces framebuffer
 *   access through deBeginAccess - which lifts the software cursor before the
 *   read happens. The readback therefore never contains the cursor, whatever
 *   the driver did or did not exclude, so no comparison built on a readback can
 *   tell a correct exclusion from a missing one.
 *
 * So the cursor-over-source case has no automated coverage in this project. It
 * rests on the reference driver's authority for the shape of the fix, and on
 * somebody looking at a screen for confirmation. Saying so is better than a
 * green check that means nothing.
 *
 * What remains here is the soak the rollout plan asks for: volume through
 * USER's own screen-to-screen copy paths with the pointer kept on the window
 * being moved, checked for survival and for the desktop still rendering
 * afterwards.
 * ------------------------------------------------------------------------ */

#define V9X_SOAK_DRAGS              60

static void v9x_soak_drag_window(HWND window, DWORD drags)
{
    int screen_width = GetSystemMetrics(SM_CXSCREEN);
    int screen_height = GetSystemMetrics(SM_CYSCREEN);
    int width = 320;
    int height = 240;
    DWORD index;

    if (screen_width < width + 8 || screen_height < height + 8) {
        return;
    }
    for (index = 0ul; index < drags; ++index) {
        int x = (int)v9x_accel_random((DWORD)(screen_width - width));
        int y = (int)v9x_accel_random((DWORD)(screen_height - height));
        HDC client;

        /* Keep the pointer on the window being moved, which is what a real
         * drag does and what puts the cursor over the source of USER's copy. */
        SetCursorPos(x + width / 2, y + height / 2);
        SetWindowPos(window, HWND_TOP, x, y, width, height, SWP_NOZORDER);
        client = GetDC(window);
        if (client != 0) {
            ScrollWindow(window, 0, 16, 0, 0);
            ScrollWindow(window, 12, 0, 0, 0);
            ReleaseDC(window, client);
        }
    }
}

static DWORD v9x_accel_run(HWND window)
{
    V9X_ACCEL_STATE state;
    V9X_GDI_STATS stats;
    V9X_GDI_STATS before;
    RECT client;
    DWORD fills = 0ul;
    DWORD copies = 0ul;
    DWORD overlaps = 0ul;
    DWORD noise = 0ul;
    DWORD compares = 0ul;
    long difference = -1l;
    int screen_bpp;
    int accelerated_depth;
    int index;
    int stats_ok;
    int compared_ok = 1;
    const char *error = 0;

    for (index = 0; index < (int)sizeof(state); ++index) {
        ((unsigned char *)&state)[index] = 0u;
    }
    GetClientRect(window, &client);
    if (client.right < V9X_ACCEL_WIDTH + V9X_ACCEL_MARGIN * 2 ||
        client.bottom < V9X_ACCEL_HEIGHT + V9X_ACCEL_MARGIN * 2) {
        v9x_accel_write_text("Result", "FAIL");
        v9x_accel_write_text("Error", "client-too-small");
        v9x_accel_write_uint("ClientWidth", (DWORD)client.right);
        v9x_accel_write_uint("ClientHeight", (DWORD)client.bottom);
        return 4ul;
    }
    state.origin_x = V9X_ACCEL_MARGIN;
    state.origin_y = V9X_ACCEL_MARGIN;
    state.window = window;
    state.screen = GetDC(window);
    if (state.screen == 0) {
        v9x_accel_write_text("Result", "FAIL");
        v9x_accel_write_text("Error", "no-window-dc");
        return 4ul;
    }
    screen_bpp = GetDeviceCaps(state.screen, BITSPIXEL) *
                 GetDeviceCaps(state.screen, PLANES);
    v9x_accel_write_uint("ScreenBpp", (DWORD)screen_bpp);

    state.reference = CreateCompatibleDC(state.screen);
    state.reference_bitmap = CreateCompatibleBitmap(state.screen,
                                                   V9X_ACCEL_WIDTH,
                                                   V9X_ACCEL_HEIGHT);
    state.capture = CreateCompatibleDC(state.screen);
    state.capture_bitmap = CreateCompatibleBitmap(state.screen,
                                                 V9X_ACCEL_WIDTH,
                                                 V9X_ACCEL_HEIGHT);
    state.screen_bits = (unsigned char *)VirtualAlloc(
        0, v9x_accel_dib_bytes(), MEM_COMMIT, PAGE_READWRITE);
    state.reference_bits = (unsigned char *)VirtualAlloc(
        0, v9x_accel_dib_bytes(), MEM_COMMIT, PAGE_READWRITE);
    if (state.reference == 0 || state.reference_bitmap == 0 ||
        state.capture == 0 || state.capture_bitmap == 0 ||
        state.screen_bits == 0 || state.reference_bits == 0) {
        error = "setup-failed";
    } else {
        SelectObject(state.reference, state.reference_bitmap);
        SelectObject(state.capture, state.capture_bitmap);
    }

    if (error == 0) {
        /*
         * Both sides start from the same known contents, so a mismatch is
         * always attributable to an operation in this run and not to whatever
         * was on the desktop.
         */
        PatBlt(state.screen, state.origin_x, state.origin_y, V9X_ACCEL_WIDTH,
               V9X_ACCEL_HEIGHT, BLACKNESS);
        PatBlt(state.reference, 0, 0, V9X_ACCEL_WIDTH, V9X_ACCEL_HEIGHT,
               BLACKNESS);
        stats_ok = v9x_accel_read_stats(state.screen, &before);
        if (!stats_ok) {
            error = "escape-rejected";
        }
    }

    if (error == 0) {
        for (index = 0; index < V9X_ACCEL_OPERATIONS; ++index) {
            v9x_accel_operation(&state, index, &fills, &copies, &overlaps,
                                &noise);
            if ((index + 1) % V9X_ACCEL_COMPARE_EVERY == 0) {
                ++compares;
                if (!v9x_accel_compare(&state, &difference)) {
                    compared_ok = 0;
                    v9x_accel_write_uint("MismatchOperation",
                                         (DWORD)(index + 1));
                    break;
                }
            }
        }
    }

    if (error == 0 && !v9x_accel_read_stats(state.screen, &stats)) {
        error = "escape-rejected-after";
    }

    if (error == 0) {
        v9x_accel_report_stats(&stats);
        /*
         * The driver's counters are DGROUP statics with no per-Enable reset,
         * so they accumulate over a whole session - a second /accel run in one
         * boot, or a live mode switch between two runs, reads the first run's
         * totals as well as its own. The absolutes above are what the
         * zero-counter check uses (0 against non-0 is unambiguous either way);
         * these deltas are what a human comparing two runs needs.
         */
        v9x_accel_write_uint("CallsDelta", stats.calls - before.calls);
        v9x_accel_write_uint("FillsDelta", stats.fills - before.fills);
        v9x_accel_write_uint("CopiesDelta", stats.copies - before.copies);
        v9x_accel_write_uint("DeclinesDelta",
                             stats.declines - before.declines);
        v9x_accel_write_uint("DrainsDelta", stats.drains - before.drains);
        v9x_accel_write_uint("Operations", (DWORD)index);
        v9x_accel_write_uint("FillOperations", fills);
        v9x_accel_write_uint("CopyOperations", copies);
        v9x_accel_write_uint("OverlapOperations", overlaps);
        v9x_accel_write_uint("NoiseOperations", noise);
        v9x_accel_write_uint("Comparisons", compares);
        v9x_accel_write_text("Compared", compared_ok ? "PASS" : "FAIL");
        if (!compared_ok) {
            if (difference >= 0l) {
                v9x_accel_report_mismatch(&state, difference);
            }
            error = difference == -2l ? "getdibits-failed" : "pixel-mismatch";
        }
    }

    /*
     * The anti-vacuous-pass check, and the single most important line here.
     *
     * A primitive that the build advertises and the configuration enabled must
     * have fired. Without this the whole comparison above can pass while every
     * operation quietly took the decline path - the failure mode this file
     * exists to make impossible.
     *
     * Conditioned on the depth, and that condition was measured rather than
     * anticipated. Run with GdiAccelFill=1 on a 1024x768x32 desktop this check
     * fired and the run failed, on a driver that was behaving perfectly: the
     * S3 primitives serve 8 and 16 bpp only, so at 32 bpp every fill is
     * declined at the depth gate by design (DeclineDepth=669 said so plainly).
     * Left unconditioned, every 32-bpp mode in the mode matrix would fail from
     * build 001 onward.
     *
     * The check is not weakened by this: it still runs in every mode the
     * primitives can actually serve, which is eight of the S3 family's eleven.
     * What it must never do is skip silently, so the depth it decided on is
     * reported either way.
     */
    /*
     * One predicate for both of the checks below, because they depend on the
     * same thing and gating them differently produced two false positives at
     * 32 bpp on a driver that was behaving perfectly. The S3 primitives serve
     * 8 and 16 bpp only; above that every operation is declined at the depth
     * gate, so no primitive can fire and no bounded wait can run.
     */
    accelerated_depth = screen_bpp == 8 || screen_bpp == 16;
    if (error == 0) {
        int serviceable = accelerated_depth;

        v9x_accel_write_uint("ZeroCounterChecked", serviceable ? 1ul : 0ul);
        if (!serviceable) {
            v9x_accel_write_text("ZeroCounterSkipped",
                                 "depth-not-accelerated");
        } else if ((stats.enabled & V9X_GDI_PRIM_FILL) != 0ul &&
                   stats.fills == before.fills) {
            error = "fill-enabled-but-never-fired";
        } else if ((stats.enabled & V9X_GDI_PRIM_COPY) != 0ul &&
                   stats.copies == before.copies) {
            error = "copy-enabled-but-never-fired";
        } else if ((stats.enabled & V9X_GDI_PRIM_COPY) != 0ul &&
                   (stats.enabled & V9X_GDI_PRIM_OVERLAP) == 0ul &&
                   stats.decline_overlap == before.decline_overlap) {
            /*
             * The mirror of the check above, and it matters for the same
             * reason. A build with copy on and overlap off claims two things:
             * that disjoint copies are accelerated, and that overlapping ones
             * are not. This run issues both deliberately, so if no overlapping
             * copy was declined then either the generator stopped producing
             * them or the gate stopped catching them - and the second would
             * mean an overlapping copy running on an engine that has not been
             * told which corner to start from.
             */
            error = "overlap-declines-never-exercised";
        }
    }

    /*
     * A poison this run did not ask for is a failure, and nothing was checking.
     *
     * The driver's counters do not reset per Enable, and the mode matrix runs
     * the original V9XGDI smoke phase in the same boot before this one - so
     * `stats.fills` can be non-zero because of work that happened before this
     * run started. Comparing against `before` above is what makes the
     * zero-counter check about *this* run; comparing against zero let the
     * earlier phase satisfy it.
     *
     * That is not hypothetical. On the Trio64 at its three largest modes a real
     * uninjected bounded wait expired during the smoke phase, latched the
     * poison, and turned acceleration off for the rest of the boot. This run
     * then performed 500 operations with every one of them declining at the
     * first gate, and passed - because `fills` was non-zero from before, and
     * because being poisoned is a legitimate state nobody had asserted against.
     * Two holes, one of which hid a driver defect.
     */
    if (error == 0 && before.poisoned != 0ul) {
        error = "poisoned-before-run";
    }
    if (error == 0 && stats.poisoned != 0ul) {
        error = "poisoned-during-run";
    }
    /*
     * And the check that still means something when nothing is enabled, which
     * is every default build 000: the dispatcher has to have been reached. If
     * ordinal 1 were not actually ours, five hundred blits would have produced
     * no calls at all and the pixel comparison would still be perfect.
     */
    if (error == 0 && stats.calls <= before.calls) {
        error = "dispatcher-never-called";
    }

    /*
     * Fault injection, gated on the same condition as the zero-counter check -
     * and it has to be the *same* condition, not merely a similar one.
     *
     * An armed injection is only ever consumed by an operation that actually
     * reaches a bounded wait. Two things can stop that: no primitive enabled
     * (every default build 000, where no GDI wait ever runs), or a depth no
     * primitive serves (every 32-bpp mode, where the depth gate declines
     * before any wait). In both cases Poisoned stays 0 honestly, and asserting
     * otherwise fails a healthy driver. Gating on `enabled` alone got the first
     * and missed the second.
     */
    v9x_accel_write_uint("InjectionChecked",
                         (stats.enabled != 0ul && accelerated_depth) ? 1ul
                                                                     : 0ul);
    if (error == 0 && stats.enabled == 0ul) {
        v9x_accel_write_text("InjectionSkipped", "no-primitive-enabled");
    } else if (error == 0 && !accelerated_depth) {
        v9x_accel_write_text("InjectionSkipped", "depth-not-accelerated");
    } else if (error == 0) {
        V9X_GDI_STATS injected;
        HBRUSH brush = CreateSolidBrush(v9x_accel_colors[9]);
        /*
         * Into BOTH DCs. Selecting it into the screen alone left the reference
         * holding its default white brush, so this step compared a red screen
         * against a white reference and reported a pixel mismatch on a driver
         * that had just filled 197 rectangles correctly - two of every three
         * bytes differing, which is exactly red against white.
         */
        HBRUSH previous_screen = (HBRUSH)SelectObject(state.screen, brush);
        HBRUSH previous_reference = (HBRUSH)SelectObject(state.reference,
                                                        brush);

        v9x_accel_write_uint("InjectArmed",
                             v9x_accel_arm_fault(state.screen, 1ul) ? 1ul
                                                                    : 0ul);
        PatBlt(state.screen, state.origin_x, state.origin_y,
               V9X_ACCEL_WIDTH, V9X_ACCEL_HEIGHT, PATCOPY);
        PatBlt(state.reference, 0, 0, V9X_ACCEL_WIDTH, V9X_ACCEL_HEIGHT,
               PATCOPY);
        SelectObject(state.screen, previous_screen);
        SelectObject(state.reference, previous_reference);
        DeleteObject(brush);
        if (!v9x_accel_read_stats(state.screen, &injected)) {
            error = "escape-rejected-injected";
        } else {
            v9x_accel_write_uint("PoisonedAfterInject", injected.poisoned);
            v9x_accel_write_uint("IdleTimeoutsAfterInject",
                                 injected.idle_timeouts);
            v9x_accel_write_uint("FifoTimeoutsAfterInject",
                                 injected.fifo_timeouts);
            if (injected.poisoned == 0ul) {
                error = "injection-not-consumed";
            } else if (!v9x_accel_compare(&state, &difference)) {
                /* The forced timeout must leave the pixels correct: the
                 * operation the engine did not perform is still performed by
                 * the DIB Engine. */
                if (difference >= 0l) {
                    v9x_accel_report_mismatch(&state, difference);
                }
                error = "pixel-mismatch-after-inject";
            }
        }
    }

    if (state.screen_bits != 0) {
        VirtualFree(state.screen_bits, 0, MEM_RELEASE);
    }
    if (state.reference_bits != 0) {
        VirtualFree(state.reference_bits, 0, MEM_RELEASE);
    }
    if (state.capture_bitmap != 0) {
        DeleteObject(state.capture_bitmap);
    }
    if (state.capture != 0) {
        DeleteDC(state.capture);
    }
    if (state.reference_bitmap != 0) {
        DeleteObject(state.reference_bitmap);
    }
    if (state.reference != 0) {
        DeleteDC(state.reference);
    }
    ReleaseDC(window, state.screen);

    if (error != 0) {
        v9x_accel_write_text("Error", error);
        v9x_accel_write_text("Result", "FAIL");
        return 5ul;
    }
    v9x_accel_write_text("Result", "PASS");
    return 0ul;
}

static LRESULT CALLBACK v9x_accel_window_proc(HWND window, UINT message,
                                             WPARAM wparam, LPARAM lparam)
{
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

/*
 * Parse /inject:N. Returns 0 when the switch is absent.
 *
 * It exists because fault injection and the poison latch are properties of the
 * bounded-wait and recovery paths, not of any primitive's pixel output, and
 * /accel's own injection step is gated behind a passing comparison. With a
 * primitive that fires but paints the wrong colour - which is where build 000
 * left the fill - that gate is unreachable, and the recovery paths would have
 * gone untested for the wrong reason. Arming from outside separates the two
 * claims.
 */
static DWORD v9x_accel_parse_inject(const char *command_line)
{
    static const char option[] = "/inject:";
    int offset;
    int index;
    DWORD value;

    for (offset = 0; command_line[offset] != ' '; ++offset) {
        for (index = 0; option[index] != ' '; ++index) {
            if (!v9x_ascii_equal_ci(command_line[offset + index],
                                    option[index])) {
                break;
            }
        }
        if (option[index] != ' ') {
            continue;
        }
        offset += index;
        value = 0ul;
        while (command_line[offset] >= '0' && command_line[offset] <= '9') {
            value = value * 10ul + (DWORD)(command_line[offset] - '0');
            ++offset;
        }
        return value;
    }
    return 0ul;
}

/*
 * Arm the GDI fault injector and report the driver's counters, without drawing
 * anything. Also the way to read the counters back after a live mode switch,
 * which is what shows the poison latch surviving one.
 */
static DWORD v9x_accel_inject_phase(DWORD count)
{
    V9X_GDI_STATS stats;
    HDC screen = GetDC(0);
    int armed;

    WritePrivateProfileStringA(V9X_ACCEL_SECTION, 0, 0, V9X_ACCEL_PATH);
    v9x_accel_write_text("Build", V9X_BUILD_ID);
    v9x_accel_write_text("Phase", "inject");
    v9x_accel_write_uint("InjectRequested", count);
    if (screen == 0) {
        v9x_accel_write_text("Result", "FAIL");
        v9x_accel_write_text("Error", "no-screen-dc");
        return 1ul;
    }
    v9x_accel_write_uint("ScreenBpp",
                         (DWORD)(GetDeviceCaps(screen, BITSPIXEL) *
                                 GetDeviceCaps(screen, PLANES)));
    armed = count != 0ul ? v9x_accel_arm_fault(screen, count) : 1;
    v9x_accel_write_uint("InjectArmed", armed ? 1ul : 0ul);
    if (!v9x_accel_read_stats(screen, &stats)) {
        ReleaseDC(0, screen);
        v9x_accel_write_text("Result", "FAIL");
        v9x_accel_write_text("Error", "escape-rejected");
        return 2ul;
    }
    ReleaseDC(0, screen);
    v9x_accel_report_stats(&stats);
    v9x_accel_write_text("Result", armed ? "PASS" : "FAIL");
    WritePrivateProfileStringA(0, 0, 0, V9X_ACCEL_PATH);
    return armed ? 0ul : 3ul;
}

static DWORD v9x_accel_phase(HINSTANCE instance)
{
    WNDCLASSA window_class;
    HWND window;
    DWORD result;
    int screen_width = GetSystemMetrics(SM_CXSCREEN);
    int screen_height = GetSystemMetrics(SM_CYSCREEN);
    unsigned index;

    WritePrivateProfileStringA(V9X_ACCEL_SECTION, 0, 0, V9X_ACCEL_PATH);
    v9x_accel_write_text("Build", V9X_BUILD_ID);
    v9x_accel_write_uint("Width", (DWORD)screen_width);
    v9x_accel_write_uint("Height", (DWORD)screen_height);

    for (index = 0u; index < sizeof(window_class); ++index) {
        ((unsigned char *)&window_class)[index] = 0u;
    }
    window_class.lpfnWndProc = v9x_accel_window_proc;
    window_class.hInstance = instance;
    window_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    window_class.lpszClassName = V9X_ACCEL_CLASS;
    /*
     * A class cursor, and it is load bearing rather than cosmetic.
     *
     * This was zero, and with no class cursor USER draws no cursor at all over
     * the window - so there was no cursor image anywhere in the framebuffer
     * while this phase ran. The cursor-over-source check was therefore
     * vacuous, and it was measured vacuous: a driver built to exclude only the
     * destination rectangle passed it 40 times out of 40. An assertion never
     * observed failing is not known to work, and this one was not working.
     *
     * It also means the pointer-parking below was belt-and-braces over a window
     * that showed no pointer.
     */
    window_class.hCursor = LoadCursorA(0, IDC_ARROW);
    if (!RegisterClassA(&window_class)) {
        v9x_accel_write_text("Result", "FAIL");
        v9x_accel_write_text("Error", "register-class");
        return 1ul;
    }
    /*
     * A borderless full-screen topmost window, because the comparison reads
     * pixels back out of the framebuffer: anything overlapping the sampled
     * region would be read as a mismatch. The pointer is parked in the far
     * corner for the same reason - DIBENG's cursor exclusion should keep a
     * software cursor out of the readback, but not relying on that costs one
     * call.
     */
    window = CreateWindowExA(WS_EX_TOPMOST, V9X_ACCEL_CLASS,
                             "Velocity9x GDI acceleration test",
                             WS_POPUP, 0, 0, screen_width, screen_height,
                             0, 0, instance, 0);
    if (window == 0) {
        v9x_accel_write_text("Result", "FAIL");
        v9x_accel_write_text("Error", "create-window");
        return 2ul;
    }
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    SetCursorPos(screen_width - 1, screen_height - 1);
    result = v9x_accel_run(window);
    /*
     * The window drag and scroll soak the rollout plan asks for: volume through
     * USER's own screen-to-screen copy paths, with the pointer kept on the
     * window being moved. Checked for survival rather than for pixels - USER
     * may or may not blit content on a move on this platform, so a pixel
     * failure here would not be attributable, and the attributable version of
     * that test is the cursor-over-source check above.
     */
    v9x_soak_drag_window(window, V9X_SOAK_DRAGS);

    /*
     * Prove the desktop still renders after everything above, including a
     * forced engine timeout: a driver that poisoned itself into drawing
     * nothing would still have written PASS for the pixels it compared before
     * the injection.
     */
    {
        HDC screen = GetDC(window);
        COLORREF probe = CLR_INVALID;

        if (screen != 0) {
            HBRUSH brush = CreateSolidBrush(v9x_accel_colors[15]);
            HBRUSH previous = (HBRUSH)SelectObject(screen, brush);

            PatBlt(screen, 4, 4, 64, 64, PATCOPY);
            SelectObject(screen, previous);
            DeleteObject(brush);
            probe = GetPixel(screen, 32, 32);
            ReleaseDC(window, screen);
        }
        v9x_accel_write_uint("DesktopRenders",
                             v9x_color_near(probe, RGB(255, 255, 255)) ? 1ul
                                                                       : 0ul);
        if (result == 0ul && !v9x_color_near(probe, RGB(255, 255, 255))) {
            v9x_accel_write_text("Error", "desktop-stopped-rendering");
            v9x_accel_write_text("Result", "FAIL");
            result = 6ul;
        }
    }
    DestroyWindow(window);
    WritePrivateProfileStringA(0, 0, 0, V9X_ACCEL_PATH);
    return result;
}

void WINAPI V9xGdiSmokeEntry(void)
{
    HINSTANCE instance = GetModuleHandleA(0);
    const char *command_line = GetCommandLineA();
    WNDCLASSA window_class;
    HWND window;
    MSG message;

    if (v9x_has_switch(command_line, "/inject:")) {
        ExitProcess(v9x_accel_inject_phase(
            v9x_accel_parse_inject(command_line)));
    }
    if (v9x_has_switch(command_line, "/stats")) {
        ExitProcess(v9x_accel_inject_phase(0ul));
    }
    if (v9x_has_switch(command_line, "/accel")) {
        ExitProcess(v9x_accel_phase(instance));
    }

    v9x_auto_mode = v9x_has_auto_switch(command_line);

    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = v9x_window_proc;
    window_class.cbClsExtra = 0;
    window_class.cbWndExtra = 0;
    window_class.hInstance = instance;
    window_class.hIcon = LoadIconA(0, IDI_APPLICATION);
    window_class.hCursor = LoadCursorA(0, IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    window_class.lpszMenuName = 0;
    window_class.lpszClassName = v9x_class_name;
    if (!RegisterClassA(&window_class)) {
        ExitProcess(1ul);
    }

    window = CreateWindowExA(WS_EX_DLGMODALFRAME, v9x_class_name,
        v9x_full_title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        60, 40, 490, 390, 0, 0, instance, 0);
    if (window == 0) {
        ExitProcess(2ul);
    }
    ShowWindow(window, SW_SHOWNORMAL);
    /* Pixel readback is meaningful only while the sampled client area is
     * visible.  Boot-time utilities such as PowerStrip can leave a modal
     * dialog over the test window, causing GetPixel to return CLR_INVALID.
     * Keep only unattended runs above incidental desktop popups. */
    if (v9x_auto_mode) {
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    }
    UpdateWindow(window);
    while (GetMessageA(&message, 0, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    ExitProcess((DWORD)message.wParam);
}
