/*
 * V9XTIME.EXE: the display timing the card is scanning out right now.
 *
 * Publishes C:\V9XDIAG\V9XTIME.INI. Read-only and driver-agnostic, so the
 * same capture can be taken under Velocity9x and under a vendor's driver and
 * compared: the CRTC registers CR00-CR18, Miscellaneous Output, and with
 * /mga the Matrox CRTCEXT0-5 that carry the high bits of every count
 * (MGA-1064SG Developer Specification, CRTCEXT0-CRTCEXT5, p.4-127..4-135).
 * Then it times the vertical retrace (Input Status 1 bit 3) for two seconds,
 * which with the decoded totals gives the refresh, line rate and pixel clock.
 *
 * Why: a VGA-to-HDMI converter samples the analog signal at a clock it
 * derives from the line rate and the horizontal total it assumes for the
 * detected mode. A mode whose total differs from that assumption is sampled
 * between pixels across the whole line, which looks soft on the capture and
 * fine on a CRT. This measures what the card is doing; it does not judge.
 *
 * Every index port is read first and written back last. Nothing else is
 * written.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_TIME_PATH    V9X_DIAG_TIME_INI
#define V9X_TIME_SECTION "Velocity9xTiming"

#define V9X_MISC_READ      0x03ccu
#define V9X_CRTC_INDEX     0x03d4u
#define V9X_CRTC_DATA      0x03d5u
#define V9X_INPUT_STATUS_1 0x03dau
#define V9X_CRTCEXT_INDEX  0x03deu
#define V9X_CRTCEXT_DATA   0x03dfu

#define V9X_CRTC_COUNT     0x19u
#define V9X_CRTCEXT_COUNT  6u

/* Input Status 1 bit 3: vertical retrace active. */
#define V9X_VRETRACE       0x08u
#define V9X_MEASURE_MS     2000ul

static unsigned char v9x_in(unsigned short port);
#pragma aux v9x_in = "in al,dx" parm [dx] value [al] modify exact [al]

static void v9x_out(unsigned short port, unsigned char value);
#pragma aux v9x_out = "out dx,al" parm [dx] [al] modify exact []

static char *v9x_append_uint(char *cursor, char *end, DWORD value)
{
    char reverse[12];
    unsigned count = 0u;

    do {
        reverse[count++] = (char)('0' + value % 10ul);
        value /= 10ul;
    } while (value != 0ul);
    while (count != 0u && cursor < end) {
        *cursor++ = reverse[--count];
    }
    return cursor;
}

static void v9x_write_uint(const char *key, DWORD value)
{
    char text[12];
    char *end = v9x_append_uint(text, text + 11, value);

    *end = '\0';
    WritePrivateProfileStringA(V9X_TIME_SECTION, key, text, V9X_TIME_PATH);
}

static void v9x_write_hex_bytes(const char *key, const unsigned char *bytes,
                                unsigned count)
{
    static const char hex[] = "0123456789ABCDEF";
    char text[3u * 32u + 1u];
    unsigned index;
    unsigned at = 0u;

    for (index = 0u; index < count && index < 32u; ++index) {
        if (index != 0u) {
            text[at++] = ' ';
        }
        text[at++] = hex[(bytes[index] >> 4) & 0x0fu];
        text[at++] = hex[bytes[index] & 0x0fu];
    }
    text[at] = '\0';
    WritePrivateProfileStringA(V9X_TIME_SECTION, key, text, V9X_TIME_PATH);
}

static int v9x_has_switch(const char *line, const char *name)
{
    while (line != 0 && *line != '\0') {
        const char *a = line;
        const char *b = name;

        while (*b != '\0' && (*a == *b || (*a ^ 0x20) == *b)) {
            ++a;
            ++b;
        }
        if (*b == '\0') {
            return 1;
        }
        ++line;
    }
    return 0;
}

/* One register through an index/data pair. The caller restores the index. */
static unsigned char v9x_indexed(unsigned short index_port,
                                 unsigned short data_port,
                                 unsigned char index)
{
    v9x_out(index_port, index);
    return v9x_in(data_port);
}

/*
 * Vertical retrace starts counted over a fixed interval, timed by the
 * performance counter. Returns millihertz, or 0 when no retrace was seen.
 *
 * 32-bit arithmetic only: the tool links no C runtime, so no 64-bit divide
 * and no floating point. The counter's low dword covers the interval at the
 * 1.19 MHz rate Windows 98 gives it, and frequency and interval are scaled
 * to thousands before multiplying, which costs well under 0.1%.
 */
static DWORD v9x_measure_refresh_mhz(void)
{
    LARGE_INTEGER frequency;
    LARGE_INTEGER now;
    DWORD first = 0ul;
    DWORD last = 0ul;
    DWORD started = GetTickCount();
    DWORD frames = 0ul;
    DWORD delta_k;
    DWORD frequency_k;
    int was_in = (v9x_in(V9X_INPUT_STATUS_1) & V9X_VRETRACE) != 0;

    if (!QueryPerformanceFrequency(&frequency) ||
        frequency.u.HighPart != 0 || frequency.u.LowPart < 1000000ul) {
        return 0ul;
    }
    while (GetTickCount() - started < V9X_MEASURE_MS) {
        int in_retrace = (v9x_in(V9X_INPUT_STATUS_1) & V9X_VRETRACE) != 0;

        if (in_retrace && !was_in) {
            QueryPerformanceCounter(&now);
            if (frames == 0ul) {
                first = now.u.LowPart;
            }
            last = now.u.LowPart;
            ++frames;
        }
        was_in = in_retrace;
    }
    if (frames < 2ul) {
        return 0ul;
    }
    /* Frames between the first and last edge, over the time between them. */
    delta_k = (last - first) / 1000ul;
    frequency_k = frequency.u.LowPart / 1000ul;
    if (delta_k == 0ul) {
        return 0ul;
    }
    return (frames - 1ul) * 1000ul * frequency_k / delta_k;
}

void __stdcall V9xTimingEntry(void)
{
    unsigned char crtc[V9X_CRTC_COUNT];
    unsigned char ext[V9X_CRTCEXT_COUNT];
    unsigned char saved_crtc;
    unsigned char saved_ext = 0u;
    unsigned char misc;
    int mga = v9x_has_switch(GetCommandLineA(), "/MGA");
    unsigned index;
    DWORD htotal_chars;
    DWORD hdisplay_chars;
    DWORD vtotal;
    DWORD vdisplay;
    DWORD vsync_start;
    DWORD hsync_start_chars;
    DWORD refresh_mhz;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    WritePrivateProfileStringA(V9X_TIME_SECTION, 0, 0, V9X_TIME_PATH);
    WritePrivateProfileStringA(V9X_TIME_SECTION, "Build", V9X_BUILD_ID,
                               V9X_TIME_PATH);
    WritePrivateProfileStringA(V9X_TIME_SECTION, "Result", "INCOMPLETE",
                               V9X_TIME_PATH);

    misc = v9x_in(V9X_MISC_READ);
    saved_crtc = v9x_in(V9X_CRTC_INDEX);
    for (index = 0u; index < V9X_CRTC_COUNT; ++index) {
        crtc[index] = v9x_indexed(V9X_CRTC_INDEX, V9X_CRTC_DATA,
                                  (unsigned char)index);
    }
    v9x_out(V9X_CRTC_INDEX, saved_crtc);
    for (index = 0u; index < V9X_CRTCEXT_COUNT; ++index) {
        ext[index] = 0u;
    }
    if (mga) {
        saved_ext = v9x_in(V9X_CRTCEXT_INDEX);
        for (index = 0u; index < V9X_CRTCEXT_COUNT; ++index) {
            ext[index] = v9x_indexed(V9X_CRTCEXT_INDEX, V9X_CRTCEXT_DATA,
                                     (unsigned char)index);
        }
        v9x_out(V9X_CRTCEXT_INDEX, saved_ext);
    }

    v9x_write_uint("Mga", mga ? 1ul : 0ul);
    v9x_write_hex_bytes("Misc", &misc, 1u);
    v9x_write_hex_bytes("Crtc", crtc, V9X_CRTC_COUNT);
    v9x_write_hex_bytes("CrtcExt", ext, V9X_CRTCEXT_COUNT);

    /*
     * The counts, with the VGA overflow bits (CR07) and, with /mga, the
     * CRTCEXT1/2 extensions (p.4-131, 4-132). Horizontal values are in
     * character clocks; htotal is programmed as total - 5, hdisplay as
     * displayed - 1, vtotal as total - 2, vdisplay as displayed - 1.
     */
    htotal_chars = (DWORD)crtc[0] |
        (mga ? ((DWORD)(ext[1] & 0x01u) << 8) : 0ul);
    htotal_chars += 5ul;
    hdisplay_chars = (DWORD)crtc[1] + 1ul;
    hsync_start_chars = (DWORD)crtc[4] |
        (mga ? ((DWORD)((ext[1] >> 2) & 0x01u) << 8) : 0ul);
    vtotal = (DWORD)crtc[6] | ((DWORD)(crtc[7] & 0x01u) << 8) |
        ((DWORD)((crtc[7] >> 5) & 0x01u) << 9) |
        (mga ? ((DWORD)(ext[2] & 0x03u) << 10) : 0ul);
    vtotal += 2ul;
    vdisplay = (DWORD)crtc[0x12] | ((DWORD)((crtc[7] >> 1) & 0x01u) << 8) |
        ((DWORD)((crtc[7] >> 6) & 0x01u) << 9) |
        (mga ? ((DWORD)((ext[2] >> 2) & 0x01u) << 10) : 0ul);
    vdisplay += 1ul;
    vsync_start = (DWORD)crtc[0x10] | ((DWORD)((crtc[7] >> 2) & 0x01u) << 8) |
        ((DWORD)((crtc[7] >> 7) & 0x01u) << 9) |
        (mga ? ((DWORD)((ext[2] >> 3) & 0x03u) << 10) : 0ul);

    v9x_write_uint("HTotalChars", htotal_chars);
    v9x_write_uint("HDisplayChars", hdisplay_chars);
    v9x_write_uint("HSyncStartChars", hsync_start_chars);
    v9x_write_uint("VTotal", vtotal);
    v9x_write_uint("VDisplay", vdisplay);
    v9x_write_uint("VSyncStart", vsync_start);
    /* Misc Output bits 7:6, sync polarities (1 = negative). */
    v9x_write_uint("HSyncNegative", (misc >> 6) & 0x01u);
    v9x_write_uint("VSyncNegative", (misc >> 7) & 0x01u);

    refresh_mhz = v9x_measure_refresh_mhz();
    v9x_write_uint("RefreshMilliHz", refresh_mhz);
    if (refresh_mhz != 0ul) {
        DWORD line_hz = vtotal * refresh_mhz / 1000ul;

        v9x_write_uint("LineRateHz", line_hz);
        /* Eight pixels per character clock: this is the pixel clock if the
         * card runs 8-dot characters, which HDisplayChars x 8 against the
         * mode's width confirms or refutes. */
        v9x_write_uint("PixelClockKHz8Dot", line_hz * htotal_chars * 8ul / 1000ul);
    }

    WritePrivateProfileStringA(V9X_TIME_SECTION, "Result", "COMPLETE",
                               V9X_TIME_PATH);
    WritePrivateProfileStringA(0, 0, 0, V9X_TIME_PATH);
    ExitProcess(0u);
}
