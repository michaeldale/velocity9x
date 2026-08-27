/*
 * Trio64 calling-context probe: the same solid-rectangle fill the drivers
 * issue, done by hand with raw port I/O from a plain application, in both
 * bitnesses from one source file.
 *
 * RESOLVED 2026-08-27: this tool found the answer, and it was none of the
 * four outcomes below - both bitnesses pass on a clean machine and both fail
 * after DOS-box/VDD activity clears ADVFUNC_CNTL (4AE8H) bit 0, whereupon the
 * engine executes commands and discards every write. The apparent bitness
 * split was run ordering. The 'ge'/'af' cure arms told the two write-only
 * suspects apart, and Rb4AE8 documents the 0x008B/0x008A working/broken pair.
 * Kept as the instrument for any future engine-vs-GDI coherence question.
 *
 * Why this exists (docs\handoffs\2026-08-27-gdi-accel-trio64-hardware-handoff.md):
 * on physical Trio64 silicon the 16-bit display driver's accelerated fill
 * executes but its pixels never reach the displayed framebuffer, while the
 * 32-bit DirectDraw HAL's identical register sequence works. The 640x480
 * confound is closed - the 16-bit probe fails at the exact mode where the
 * 32-bit HAL passes - so what remains is the *calling context*. This tool
 * strips every other layer away: no display driver, no DIB Engine, no
 * DirectDraw, just OUT instructions from ring-3 application code. Built as
 * V9XTC32.EXE (wcc386, Win32) and V9XTC16.EXE (wcc, Win16) from this one
 * file, so the two arms cannot drift apart.
 *
 * It also reads the engine's latched state straight back off the silicon,
 * which the driver never could until the databook settled how (DB014-B):
 *   - CUR_X (86E8h), CUR_Y (82E8h), MAJ_AXIS_PCNT (96E8h), FRGD_COLOR
 *     (A6E8h), FRGD_MIX (BAE8h) and WRT_MASK (AAE8h) are Read/Write.
 *   - The BEE8h multifunc set reads back through Read Register Select
 *     (BEE8h index 0Fh): select 0-4 are MIN_AXIS_PCNT and the four
 *     scissors, 5 is PIX_CNTL (index 0Ah), 6 is MULT_MISC (index 0Eh),
 *     7 is 9AE8h, 8 is 42E8h, 9 is 46E8h, 0Ah is MULT_MISC2 (index 0Dh).
 *   - BEE8h writes are pipelined: a NOP drawing command (9AE8h with bits
 *     15-13 = 000b) must be issued before the readback is meaningful.
 *
 * So each arm answers two questions at once: do the setup writes latch
 * (readback), and do the pixels land (GetPixel through GDI). Four outcomes,
 * all informative:
 *   32 works, 16 fails  - the ring-3 I/O route differs by bitness (VMM/VxD)
 *   both work           - the driver's own context is the variable, not
 *                         application bitness
 *   both fail           - whatever DirectDraw's session establishes is the
 *                         enabler, and the readback says whether the writes
 *                         at least latched
 *   16 works, 32 fails  - would contradict the HAL result; suspect the tool
 *
 * Run with GdiAccel=0 so this tool is the only engine user. It draws two
 * small rectangles on the live desktop; a repaint clears them.
 *
 * Results land in C:\V9XTC32.INI / C:\V9XTC16.INI, section [Velocity9xTrioCtx].
 */
#include <windows.h>

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#ifdef __386__
#define V9X_INI      "C:\\V9XTC32.INI"
#define V9X_ARM      "win32"
#else
#define V9X_INI      "C:\\V9XTC16.INI"
#define V9X_ARM      "win16"
#endif
#define V9X_SEC      "Velocity9xTrioCtx"

/* The 8514/A-compatible engine ports, as include\velocity9x\s3_engine_regs.h
 * defines them for the drivers. Restated literally rather than included, so
 * this tool is a second, independent transcription of the databook - a shared
 * header would make a transcription error invisible to this experiment. */
#define V9X_CUR_Y          0x82e8u
#define V9X_CUR_X          0x86e8u
#define V9X_MAJ_AXIS       0x96e8u
#define V9X_CMD_STATUS     0x9ae8u
#define V9X_FRGD_COLOR     0xa6e8u
#define V9X_WRT_MASK       0xaae8u
#define V9X_FRGD_MIX       0xbae8u
#define V9X_MULTIFUNC      0xbee8u

#define V9X_STATUS_BUSY    0x0200u
#define V9X_MIX_NEW        0x0027u
#define V9X_PIX_CNTL_FRGD  0xa000u
#define V9X_CMD_RECT_SOLID 0x40b1u
#define V9X_CMD_NOP        0x0000u
#define V9X_MULT_MISC2     0xd000u
#define V9X_MULT_MISC      0xe000u
#define V9X_READ_SEL       0xf000u

#define V9X_IDLE_SPINS     0x00200000ul

/* Green in RGB565, distinctive against the desktop and against the driver
 * probe's magenta. The /alt arm fills red instead, so that a readback of
 * FRGD_COLOR can tell this run's write from a value another run left latched -
 * two arms writing the same colour cannot be told apart on the readback. */
#define V9X_FILL_565       0x07e0u
#define V9X_FILL_ALT_565   0xf800u

static unsigned short v9x_fill_565 = V9X_FILL_565;

static unsigned short v9x_inpw(unsigned short port);
#pragma aux v9x_inpw = "in ax,dx" parm [dx] value [ax] modify exact [ax]

static void v9x_outpw(unsigned short port, unsigned short value);
#pragma aux v9x_outpw = "out dx,ax" parm [dx] [ax] modify exact []

static void v9x_outpb(unsigned short port, unsigned char value);
#pragma aux v9x_outpb = "out dx,al" parm [dx] [al] modify exact []

static unsigned char v9x_inpb(unsigned short port);
#pragma aux v9x_inpb = "in al,dx" parm [dx] value [al] modify exact [al]

/*
 * How engine register words go out. 0 is a plain word OUT. 1 and 2 split the
 * word into two byte OUTs (1: low byte to the port then high byte to port+1;
 * 2: the reverse), which is what VMM's I/O emulation does to a trapped word
 * access. If a split mode reproduces the Win16 arm's failure from the working
 * Win32 arm, the 16-bit path's word writes are being decomposed in transit.
 */
static int v9x_split_mode;
static int v9x_flood_mode;
static int v9x_no_vga_dump;
/* Cure candidates for the displaced state, applied just before the first
 * fill. 'ge' writes SUBSYS_CNTL (42E8h) GE-RST = 01b (Graphics Engine
 * enabled); 'af' writes ADVFUNC_CNTL (4AE8h) enhanced-functions enable with
 * the MIO bit the readback shows set. Both registers are write-only or
 * side-effectful, so each is a separate arm. */
static int v9x_cure_ge;
static int v9x_cure_af;

static void v9x_outreg(unsigned short port, unsigned short value)
{
    if (v9x_split_mode == 1) {
        v9x_outpb(port, (unsigned char)(value & 0xffu));
        v9x_outpb((unsigned short)(port + 1u),
                  (unsigned char)(value >> 8));
    } else if (v9x_split_mode == 2) {
        v9x_outpb((unsigned short)(port + 1u),
                  (unsigned char)(value >> 8));
        v9x_outpb(port, (unsigned char)(value & 0xffu));
    } else {
        v9x_outpw(port, value);
    }
}

static void v9x_write_text(const char *key, const char *value)
{
    WritePrivateProfileString(V9X_SEC, key, value, V9X_INI);
}

/* Win9x caches profile writes; a NULL-section write is the documented flush.
 * Without it a fetch right after process exit reads a truncated file. */
static void v9x_flush(void)
{
    WritePrivateProfileString(0, 0, 0, V9X_INI);
}

static void v9x_write_uint(const char *key, DWORD value)
{
    char text[16];

    wsprintf(text, "%lu", value);
    v9x_write_text(key, text);
}

static void v9x_write_hex(const char *key, DWORD value)
{
    char text[16];

    wsprintf(text, "0x%04lX", value);
    v9x_write_text(key, text);
}

/*
 * Key = prefix + one trailing character, built by hand: wsprintf's %s takes a
 * far string in Win16 and a near literal reaches it as garbage, which the
 * first emulated run demonstrated by writing keys named "308". lstrcpy
 * converts through its prototype, so it does not have the problem.
 */
static void v9x_write_hex_suffixed(const char *prefix, char suffix,
                                   DWORD value)
{
    char key[24];
    int length;

    lstrcpy(key, prefix);
    length = lstrlen(key);
    key[length] = suffix;
    key[length + 1] = '\0';
    v9x_write_hex(key, value);
}

/*
 * Dump the S3 extended CRTC file, CR30-CR70, one key per index. The engine's
 * own readable registers were byte-identical between a failing and a passing
 * run, so whatever displaces the failing fill lives in the address-translation
 * state the CRTC extensions hold - memory configuration, banking, the linear
 * address window - and this is the instrument that finds the differing bit.
 * One key, "CrNN", per register; a diff of two runs' INIs does the rest.
 */
static void v9x_dump_crtc(const char *prefix)
{
    unsigned char index;
    char key[24];
    char hexdigits[17] = "0123456789ABCDEF";
    int length;

    for (index = 0x30u; index <= 0x70u; ++index) {
        unsigned char value;

        v9x_outpb(0x3d4u, index);
        value = v9x_inpb(0x3d5u);
        lstrcpy(key, prefix);
        length = lstrlen(key);
        key[length] = hexdigits[(index >> 4) & 0x0fu];
        key[length + 1] = hexdigits[index & 0x0fu];
        key[length + 2] = '\0';
        v9x_write_hex(key, (DWORD)value);
    }
}

/* Dump an indexed VGA register file (index port / data port), same key
 * scheme as the CRTC dump. */
static void v9x_dump_indexed(const char *prefix, unsigned short index_port,
                             unsigned short data_port,
                             unsigned char first, unsigned char last)
{
    unsigned char index;
    char key[24];
    char hexdigits[17] = "0123456789ABCDEF";
    int length;

    for (index = first; index <= last; ++index) {
        unsigned char value;

        v9x_outpb(index_port, index);
        value = v9x_inpb(data_port);
        lstrcpy(key, prefix);
        length = lstrlen(key);
        key[length] = hexdigits[(index >> 4) & 0x0fu];
        key[length + 1] = hexdigits[index & 0x0fu];
        key[length + 2] = '\0';
        v9x_write_hex(key, (DWORD)value);
    }
}

static int v9x_wait_idle(void)
{
    DWORD spins = V9X_IDLE_SPINS;

    do {
        if ((v9x_inpw(V9X_CMD_STATUS) & V9X_STATUS_BUSY) == 0u) {
            return 1;
        }
    } while (--spins != 0ul);
    return 0;
}

/* Does a GetPixel COLORREF match this run's fill colour? 565 green 0x07E0
 * expands to (0, 255, 0) and 565 red 0xF800 to (255, 0, 0); allow a rounding
 * wobble but nothing that could be desktop content. */
static int v9x_is_fill_color(COLORREF pixel)
{
    int red = (int)(pixel & 0xfful);
    int green = (int)((pixel >> 8) & 0xfful);
    int blue = (int)((pixel >> 16) & 0xfful);

    if (v9x_fill_565 == V9X_FILL_ALT_565) {
        return red >= 224 && green <= 24 && blue <= 24;
    }
    return red <= 24 && green >= 224 && blue <= 24;
}

/*
 * The five sample points of a fill rectangle: the four corners (inclusive)
 * and the centre. GetPixel goes through GDI's own readback, the same
 * instrument the driver-side probe trusts.
 */
static void v9x_sample_into(HDC screen, const char *prefix, int x, int y,
                            int w, int h, int *green_count,
                            COLORREF *pixels)
{
    int index;
    int sx[5];
    int sy[5];

    sx[0] = x;             sy[0] = y;
    sx[1] = x + w - 1;     sy[1] = y;
    sx[2] = x;             sy[2] = y + h - 1;
    sx[3] = x + w - 1;     sy[3] = y + h - 1;
    sx[4] = x + w / 2;     sy[4] = y + h / 2;
    if (green_count != 0) {
        *green_count = 0;
    }
    for (index = 0; index < 5; ++index) {
        COLORREF pixel = GetPixel(screen, sx[index], sy[index]);

        if (pixels != 0) {
            pixels[index] = pixel;
        }
        v9x_write_hex_suffixed(prefix, (char)('0' + index), (DWORD)pixel);
        if (green_count != 0 && v9x_is_fill_color(pixel)) {
            ++(*green_count);
        }
    }
}

static void v9x_sample(HDC screen, const char *prefix, int x, int y,
                       int w, int h, int *green_count)
{
    v9x_sample_into(screen, prefix, x, y, w, h, green_count, 0);
}

/*
 * The coherence test: an engine screen-to-screen BitBLT of live, visible,
 * icon-rich desktop content (the top-left corner) to a visible destination,
 * per the 32-bit HAL's register sequence. A fill that vanishes has two
 * stories - "the engine writes somewhere the display does not show" and "the
 * pixels landed and the desktop repainted over them". A copy whose payload is
 * recognisable desktop content separates engine-space from GDI-space
 * directly: if the copied pixels equal the source pixels when read back
 * through GDI, the engine reads and writes the same memory GDI shows.
 */
static void v9x_copy_test(HDC screen)
{
    COLORREF source[5];
    COLORREF after[5];
    int index;
    int matches = 0;

    v9x_sample_into(screen, "CopySrc_", 0, 0, 100, 40, 0, source);
    v9x_sample(screen, "CopyBefore_", 340, 300, 100, 40, 0);
    if (!v9x_wait_idle()) {
        v9x_write_text("CopyResult", "engine-stuck");
        return;
    }
    v9x_outreg(V9X_MULTIFUNC, V9X_MULT_MISC2);
    v9x_outreg(V9X_MULTIFUNC, V9X_MULT_MISC);
    v9x_outreg(V9X_WRT_MASK, 0xffffu);
    v9x_outreg(V9X_MULTIFUNC, 0x1000u);
    v9x_outreg(V9X_MULTIFUNC, 0x2000u);
    v9x_outreg(V9X_MULTIFUNC, 0x3fffu);
    v9x_outreg(V9X_MULTIFUNC, 0x4fffu);
    v9x_outreg(V9X_FRGD_MIX, 0x0067u);        /* display-memory source, SRC */
    v9x_outreg(V9X_MULTIFUNC, V9X_PIX_CNTL_FRGD);
    v9x_outreg(V9X_CUR_X, 0u);
    v9x_outreg(V9X_CUR_Y, 0u);
    v9x_outreg(0x8ee8u, 340u);                /* DESTX_DIASTP              */
    v9x_outreg(0x8ae8u, 300u);                /* DESTY_AXSTP               */
    v9x_outreg(V9X_MAJ_AXIS, 99u);
    v9x_outreg(V9X_MULTIFUNC, 39u);           /* MIN_AXIS_PCNT, index 0     */
    v9x_outreg(V9X_CMD_STATUS, 0xc0b1u);      /* BITBLT, +X +Y, draw       */
    if (!v9x_wait_idle()) {
        v9x_write_text("CopyResult", "engine-stuck-after");
        return;
    }
    v9x_sample_into(screen, "CopyAfter_", 340, 300, 100, 40, 0, after);
    for (index = 0; index < 5; ++index) {
        if (after[index] == source[index]) {
            ++matches;
        }
    }
    v9x_write_uint("CopyMatches", (DWORD)matches);
    v9x_write_text("CopyResult", matches >= 4 ? "COHERENT" : "DISPLACED");
}

/* Program the databook 13.4.2 setup plus the 13.3.3.3 fill registers for a
 * rectangle, exactly as src\display16\gdi_accel.c does. No command yet. */
static void v9x_program_fill(int x, int y, int w, int h)
{
    v9x_outreg(V9X_MULTIFUNC, V9X_MULT_MISC2);
    v9x_outreg(V9X_MULTIFUNC, V9X_MULT_MISC);
    v9x_outreg(V9X_WRT_MASK, 0xffffu);
    v9x_outreg(V9X_MULTIFUNC, 0x1000u);            /* scissors top = 0     */
    v9x_outreg(V9X_MULTIFUNC, 0x2000u);            /* scissors left = 0    */
    v9x_outreg(V9X_MULTIFUNC, 0x3fffu);            /* scissors bottom max  */
    v9x_outreg(V9X_MULTIFUNC, 0x4fffu);            /* scissors right max   */
    v9x_outreg(V9X_FRGD_MIX, V9X_MIX_NEW);
    v9x_outreg(V9X_FRGD_COLOR, v9x_fill_565);
    v9x_outreg(V9X_MULTIFUNC, V9X_PIX_CNTL_FRGD);
    v9x_outreg(V9X_CUR_X, (unsigned short)x);
    v9x_outreg(V9X_CUR_Y, (unsigned short)y);
    v9x_outreg(V9X_MAJ_AXIS, (unsigned short)(w - 1));
    v9x_outreg(V9X_MULTIFUNC, (unsigned short)(h - 1));
}

/* Read the directly readable engine registers. */
static void v9x_read_direct(char suffix)
{
    v9x_write_hex_suffixed("RbCurX", suffix, (DWORD)v9x_inpw(V9X_CUR_X));
    v9x_write_hex_suffixed("RbCurY", suffix, (DWORD)v9x_inpw(V9X_CUR_Y));
    v9x_write_hex_suffixed("RbMajAxis", suffix,
                           (DWORD)v9x_inpw(V9X_MAJ_AXIS));
    v9x_write_hex_suffixed("RbFrgdColor", suffix,
                           (DWORD)v9x_inpw(V9X_FRGD_COLOR));
    v9x_write_hex_suffixed("RbFrgdMix", suffix,
                           (DWORD)v9x_inpw(V9X_FRGD_MIX));
    v9x_write_hex_suffixed("RbWrtMask", suffix,
                           (DWORD)v9x_inpw(V9X_WRT_MASK));
}

/* Walk Read Register Select. The select is written before every read rather
 * than trusting the auto-increment, whose width the databook gives as bits
 * 2-0 while listing eleven values. */
static void v9x_read_multifunc(void)
{
    static const char *names[11] = {
        "RbMinAxis", "RbScisT", "RbScisL", "RbScisB", "RbScisR",
        "RbPixCntl", "RbMultMisc", "Rb9AE8", "Rb42E8", "Rb46E8",
        "RbMultMisc2"
    };
    unsigned short select;

    for (select = 0u; select < 11u; ++select) {
        v9x_outpw(V9X_MULTIFUNC, (unsigned short)(V9X_READ_SEL | select));
        v9x_write_hex(names[select], (DWORD)v9x_inpw(V9X_MULTIFUNC));
    }
}

/*
 * Sweep the whole GDI screen on a 4-pixel grid and report the bounding box of
 * every pixel matching this run's fill colour. GetPixel is the readback
 * instrument the fill verdicts already trust, so this places wherever the
 * engine wrote without depending on the full-screen GetDIBits path, which the
 * agent screenshot showed misbehaving at 640x480 on the physical card.
 */
static void v9x_scan_fill_color(HDC screen, int width, int height)
{
    int x;
    int y;
    long count = 0l;
    int min_x = -1;
    int min_y = -1;
    int max_x = -1;
    int max_y = -1;

    for (y = 0; y < height; y += 4) {
        for (x = 0; x < width; x += 4) {
            if (v9x_is_fill_color(GetPixel(screen, x, y))) {
                ++count;
                if (min_x < 0 || x < min_x) { min_x = x; }
                if (max_x < 0 || x > max_x) { max_x = x; }
                if (min_y < 0 || y < min_y) { min_y = y; }
                if (max_y < 0 || y > max_y) { max_y = y; }
            }
        }
    }
    v9x_write_uint("ScanHits", (DWORD)count);
    if (count != 0l) {
        v9x_write_uint("ScanX0", (DWORD)min_x);
        v9x_write_uint("ScanY0", (DWORD)min_y);
        v9x_write_uint("ScanX1", (DWORD)max_x);
        v9x_write_uint("ScanY1", (DWORD)max_y);
    }
}

/*
 * Flood mode: fill the engine's whole 2048x2048 coordinate space, which at
 * any plausible pitch covers all 2 MiB of BARRY's VRAM, then scan what GDI
 * can see. In the displaced state the normal fills land nowhere GDI-visible;
 * a flood answers whether displaced writes reach video memory at all. If the
 * visible screen turns the fill colour, the writes land under some address
 * transform that structured fills can then map; if nothing changes, the
 * engine is discarding writes outright. The desktop is destroyed either way;
 * reboot or force a repaint afterwards.
 */
static void v9x_flood(HDC screen, int width, int height)
{
    if (!v9x_wait_idle()) {
        v9x_write_text("FloodResult", "engine-stuck");
        return;
    }
    v9x_program_fill(0, 0, 2048, 2048);
    v9x_outreg(V9X_CMD_STATUS, V9X_CMD_RECT_SOLID);
    if (!v9x_wait_idle()) {
        v9x_write_text("FloodResult", "engine-stuck-after");
        return;
    }
    v9x_scan_fill_color(screen, width, height);
    v9x_write_text("FloodResult", "scanned");
    v9x_flush();
}

static void v9x_run(int alt)
{
    HDC screen = GetDC(0);
    int width;
    int height;
    int bpp;
    int green1 = 0;
    int green2 = 0;
    int x = 120;
    int y = 80;
    int w = 96;
    int h = 32;

    if (alt) {
        v9x_fill_565 = V9X_FILL_ALT_565;
        x = 340;
    }

    WritePrivateProfileString(V9X_SEC, 0, 0, V9X_INI);
    v9x_write_text("Build", V9X_BUILD_ID);
    v9x_write_text("Arm", V9X_ARM);
    v9x_write_text("Variant", alt ? "alt-red" : "green");
    v9x_write_hex("FillColor", (DWORD)v9x_fill_565);
    v9x_write_uint("SplitMode", (DWORD)v9x_split_mode);
    if (screen == 0) {
        v9x_write_text("Result", "no-screen-dc");
        return;
    }
    width = GetSystemMetrics(SM_CXSCREEN);
    height = GetSystemMetrics(SM_CYSCREEN);
    bpp = GetDeviceCaps(screen, BITSPIXEL) * GetDeviceCaps(screen, PLANES);
    v9x_write_uint("Width", (DWORD)width);
    v9x_write_uint("Height", (DWORD)height);
    v9x_write_uint("Bpp", (DWORD)bpp);
    if (bpp != 16 || width < x + w || height < y + h * 2 + 24) {
        v9x_write_text("Result", "wrong-mode");
        ReleaseDC(0, screen);
        return;
    }

    if (v9x_flood_mode) {
        v9x_flood(screen, width, height);
        ReleaseDC(0, screen);
        return;
    }

    v9x_sample(screen, "Before1_", x, y, w, h, 0);
    v9x_dump_crtc("Cr");
    if (!v9x_no_vga_dump) {
        /* The VGA core's memory-path state: sequencer (incl. the S3 extended
         * SR08-1F behind the SR08 unlock, read-only here), graphics
         * controller (memory map, odd/even, chain), and the misc output
         * register. Skippable ('nod') because the boot-96 TC16 build, which
         * did not touch 3C4/3CE, failed at boot state, and the first build
         * that did touch them passed - the touch itself may be the variable. */
        v9x_dump_indexed("Sr", 0x3c4u, 0x3c5u, 0x00u, 0x1fu);
        v9x_dump_indexed("Gr", 0x3ceu, 0x3cfu, 0x00u, 0x08u);
        v9x_write_hex("MiscOut", (DWORD)v9x_inpb(0x3ccu));
    }

    if (v9x_cure_ge) {
        v9x_outpw(0x42e8u, 0x4000u);      /* GE-RST = 01b: engine enabled */
        v9x_write_text("Cure", "ge-enable");
    }
    if (v9x_cure_af) {
        v9x_outpw(0x4ae8u, 0x0021u);      /* ENB EHFC + MIO               */
        v9x_write_text("Cure", "advfunc-enable");
    }

    /* ADVFUNC_CNTL (4AE8h) is Read/Write: bit 0 clear means the chip is in
     * VGA/planar mode and every enhanced-engine write is discarded, which is
     * the displaced state this tool hunted. Read it before any cure. */
    v9x_write_hex("Rb4AE8", (DWORD)v9x_inpw(0x4ae8u));

    /* Arm 1: full setup, then the readback with its documented NOP flush,
     * then the command. */
    v9x_write_hex("StatusEntry", (DWORD)v9x_inpw(V9X_CMD_STATUS));
    if (!v9x_wait_idle()) {
        v9x_write_text("Result", "engine-stuck-on-entry");
        v9x_flush();
        ReleaseDC(0, screen);
        return;
    }
    v9x_program_fill(x, y, w, h);
    v9x_read_direct('0');
    v9x_outreg(V9X_CMD_STATUS, V9X_CMD_NOP);
    if (!v9x_wait_idle()) {
        v9x_write_text("Result", "engine-stuck-after-nop");
        v9x_flush();
        ReleaseDC(0, screen);
        return;
    }
    v9x_read_direct('1');
    v9x_read_multifunc();
    /* The readback walk moved no drawing state, but re-establish the pixel
     * control in case a read disturbed the pipelined index, then draw. */
    v9x_outreg(V9X_MULTIFUNC, V9X_PIX_CNTL_FRGD);
    v9x_outreg(V9X_CMD_STATUS, V9X_CMD_RECT_SOLID);
    v9x_write_hex("StatusIssued", (DWORD)v9x_inpw(V9X_CMD_STATUS));
    if (!v9x_wait_idle()) {
        v9x_write_text("Result", "engine-stuck-after-fill");
        v9x_flush();
        ReleaseDC(0, screen);
        return;
    }
    v9x_write_hex("StatusAfter", (DWORD)v9x_inpw(V9X_CMD_STATUS));
    v9x_sample(screen, "After1_", x, y, w, h, &green1);
    v9x_write_uint("Fill1Green", (DWORD)green1);

    /* Arm 2: the plain databook sequence with no NOP and no readback in the
     * middle, at a second rectangle, in case the diagnostics themselves
     * perturb the drawing. */
    y = y + h + 16;
    v9x_sample(screen, "Before2_", x, y, w, h, 0);
    v9x_program_fill(x, y, w, h);
    v9x_outreg(V9X_CMD_STATUS, V9X_CMD_RECT_SOLID);
    if (!v9x_wait_idle()) {
        v9x_write_text("Result", "engine-stuck-after-fill2");
        v9x_flush();
        ReleaseDC(0, screen);
        return;
    }
    v9x_sample(screen, "After2_", x, y, w, h, &green2);
    v9x_write_uint("Fill2Green", (DWORD)green2);

    /* Where, if anywhere, this run's colour actually is on the GDI screen. */
    v9x_scan_fill_color(screen, width, height);

    /* Engine-space vs GDI-space coherence, independent of the fill path. */
    v9x_copy_test(screen);

    v9x_write_text("Result",
                   (green1 == 5 && green2 == 5) ? "PASS"
                   : (green1 == 0 && green2 == 0) ? "FAIL-NOTHING-LANDED"
                                                  : "FAIL-PARTIAL");
    v9x_flush();
    ReleaseDC(0, screen);
}

/* Case-sensitive "alt" anywhere on the command line selects the red arm;
 * "bl" / "bh" select the byte-split write modes (low-then-high /
 * high-then-low). Scanned as substrings of the argument tail only, which the
 * launcher passes without the program path. */
static int v9x_has_alt(const char *command)
{
    int alt = 0;

    while (command != 0 && command[0] != '\0') {
        if (command[0] == 'a' && command[1] == 'l' && command[2] == 't') {
            alt = 1;
        }
        if (command[0] == 'b' && command[1] == 'l') {
            v9x_split_mode = 1;
        }
        if (command[0] == 'b' && command[1] == 'h') {
            v9x_split_mode = 2;
        }
        if (command[0] == 'f' && command[1] == 'l' && command[2] == 'o') {
            v9x_flood_mode = 1;
        }
        if (command[0] == 'n' && command[1] == 'o' && command[2] == 'd') {
            v9x_no_vga_dump = 1;
        }
        if (command[0] == 'g' && command[1] == 'e' &&
            (command[2] == ' ' || command[2] == ' ')) {
            v9x_cure_ge = 1;
        }
        if (command[0] == 'a' && command[1] == 'f' &&
            (command[2] == ' ' || command[2] == ' ')) {
            v9x_cure_af = 1;
        }
        ++command;
    }
    return alt;
}

#ifdef __386__
void __stdcall V9xTrioCtxEntry(void)
{
    v9x_run(v9x_has_alt(GetCommandLineA()));
    ExitProcess(0u);
}
#else
int PASCAL WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command,
                   int show)
{
    char local[64];
    int index;

    (void)instance;
    (void)previous;
    (void)show;
    /* The Win16 command tail is a far string; copy it near before scanning. */
    for (index = 0; index < 63 && command != 0 && command[index] != '\0';
         ++index) {
        local[index] = command[index];
    }
    local[index] = '\0';
    v9x_run(v9x_has_alt(local));
    return 0;
}
#endif
