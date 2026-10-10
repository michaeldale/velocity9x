/*
 * The matrox family table.
 *
 * Two chips, the Millennium and the Millennium II. The VBE 4F02h mode set
 * programs the card and 4F01h reports where the framebuffer landed. 4F00h's
 * memory size is only a starting point: the one family-wide hook walks the
 * memory once the aperture is mapped, because a 2164W BIOS was measured
 * reporting half its card. The chips' engine hook
 * (millennium\millennium_hw16.c) describes the drawing engine to DirectDraw.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/vram_probe.h"

/* enable16.c, filled in by the tier-0 stage-3 default from VBE 4F00h. */
extern unsigned long v9x_vbe_vram_reported;

/* runtime.asm, matrox family only: one locked exchange and one read at a
 * byte offset through the framebuffer selector. */
extern unsigned long __far __pascal V9xMgaScreenExchange(unsigned long offset,
                                                         unsigned long value);
extern unsigned long __far __pascal V9xMgaScreenRead(unsigned long offset);

extern const V9X_HW16_DEVICE v9x_mga2064w_device;
extern const V9X_HW16_DEVICE v9x_mga2164w_device;

static const V9X_HW16_DEVICE * const v9x_mga_devices[] = {
    &v9x_mga2064w_device,
    &v9x_mga2164w_device
};

/*
 * The VESA-standard 8 and 16 bpp rows. Every mode number is in the 2064W
 * BIOS's own list with a linear framebuffer, measured by emulated int10 on
 * 2026-09-11 (docs\probe\matrox-2064w-full-2026-09-11\vbe.ndjson); the
 * 2164W's physical sample ran 0101h, 0111h, 0114h and 0117h under the
 * guarded candidate (docs\specifications\matrox-millennium2-bringup.md). The
 * boot-time BIOS merge adds the rest - 1280x1024, 1600x1200 and the 32 bpp
 * modes - from the mini-VDD's collection.
 *
 * The 2064W BIOS pads both 800-wide rows: 960 bytes per scan line for 0103h
 * and 1920 for 0114h. The merged row carries the BIOS's stride, stage 9
 * keeps the card scanning at it, and the DIB is built at it (ddi.c), which
 * A8U4I5 runs at 800x600 (docs\decisions\2026-10-09-mga2064w-tier0-first-
 * boot.md).
 *
 * 640x400 sits after the other 8-bpp rows so this list runs in the order of
 * the MODES registry key GDI enumerates, as in every other family.
 */
static const V9X_HW16_MODE v9x_mga_modes[] = {
    {  640u, 480u,  8u,  640u, 0x0101u, 254, 127 },
    {  800u, 600u,  8u,  800u, 0x0103u, 318, 159 },
    { 1024u, 768u,  8u, 1024u, 0x0105u, 407, 203 },
    {  640u, 400u,  8u,  640u, 0x0100u, 254, 127 },
    {  640u, 480u, 16u, 1280u, 0x0111u, 254, 127 },
    {  800u, 600u, 16u, 1600u, 0x0114u, 318, 159 },
    { 1024u, 768u, 16u, 2048u, 0x0117u, 407, 203 }
};

static unsigned char v9x_mga_port_in(unsigned short port);
#pragma aux v9x_mga_port_in = "in al,dx" parm [dx] value [al] modify exact [al]

static void v9x_mga_port_out(unsigned short port, unsigned char value);
#pragma aux v9x_mga_port_out = "out dx,al" parm [dx] [al] modify exact []

/* CRTCEXT index and data, I/O 3DEh/3DFh (MGA-1064SG Developer Specification,
 * Table 3-4; the 2064W shares the core). CRTCEXT3 bit 7 is mgamode: clear,
 * the full aperture at MGABASE2 is unusable (p.4-133). */
#define V9X_MGA_CRTCEXT_INDEX 0x03deu
#define V9X_MGA_CRTCEXT_DATA  0x03dfu
#define V9X_MGA_CRTCEXT3      0x03u

/* Decimal, into a caller-owned buffer of at least 11 bytes. Local for the
 * reason ati_hw16.c gives: the shared helper lives in an object that carries
 * another vendor's register sequence. */
static void v9x_mga_format_u32(char *text, unsigned long value)
{
    char digits[11];
    unsigned short count = 0u;
    unsigned short at = 0u;

    do {
        digits[count++] = (char)('0' + (unsigned short)(value % 10ul));
        value /= 10ul;
    } while (value != 0ul && count < (unsigned short)sizeof(digits));
    while (count != 0u) {
        text[at++] = digits[--count];
    }
    text[at] = '\0';
}

/* Two hex digits, into a caller-owned buffer of at least 3 bytes. */
static void v9x_mga_format_hex8(char *text, unsigned char value)
{
    static const char hex[] = "0123456789ABCDEF";

    text[0] = hex[(value >> 4) & 0x0fu];
    text[1] = hex[value & 0x0fu];
    text[2] = '\0';
}

/* The chip's own word, or the family default when it carries none; see the
 * note in ati_hw16.c on why these are never literals. */
static const char *v9x_mga_word(const char *value, const char *fallback)
{
    return value != 0 ? value : fallback;
}

/*
 * CRTCEXT3 as the mode set left it, index restored. A read, not a write:
 * the 2026-09-11 record left open whether the BIOS sets mgamode itself when
 * it enters a linear mode, and the spec says SVGA graphics modes need it.
 * Published after Enable, so this is the value the desktop runs with.
 */
static unsigned char v9x_mga_read_crtcext3(void)
{
    unsigned char saved = v9x_mga_port_in(V9X_MGA_CRTCEXT_INDEX);
    unsigned char value;

    v9x_mga_port_out(V9X_MGA_CRTCEXT_INDEX, V9X_MGA_CRTCEXT3);
    value = v9x_mga_port_in(V9X_MGA_CRTCEXT_DATA);
    v9x_mga_port_out(V9X_MGA_CRTCEXT_INDEX, saved);
    return value;
}

/*
 * The memory walk's answer, kept for every later Enable and for the
 * diagnostics: 0 until the first walk, then the bytes the readback showed
 * (still 0 if the walk failed).
 */
static unsigned long v9x_mga_measured_bytes;
static unsigned short v9x_mga_walked;
static unsigned long v9x_mga_walk_original[V9X_VRAM_PROBE_MAX_POINTS];
static unsigned long v9x_mga_walk_readback[V9X_VRAM_PROBE_MAX_POINTS];

/*
 * Installed memory, from the memory (include\velocity9x\vram_probe.h). The
 * 2164W has no size register, and A8U4I5's BIOS reports half of what its
 * card holds; the 2064W's BIOS has agreed with its card, and the walk costs
 * nothing there.
 *
 * Signatures go in highest offset first, each exchange taking the original
 * out as it puts the signature in, so a point that aliases a lower one hands
 * back that lower point's original. They come out in the same order, so an
 * aliased pair ends holding the lower point's original, which is also the
 * higher one's. The walk runs once, at the first Enable, before anything is
 * drawn: the mode set has just cleared the screen, and nothing else uses
 * the engine or the framebuffer yet.
 */
static unsigned long v9x_mga_measure_video_memory(unsigned long mapped_bytes,
                                                  unsigned long reported_bytes)
{
    v9x_u32 points;
    v9x_u32 index;

    if (v9x_mga_walked == 0u) {
        v9x_mga_walked = 1u;
        points = v9x_vram_probe_points(mapped_bytes);
        for (index = points; index-- != 0u;) {
            v9x_mga_walk_original[index] = V9xMgaScreenExchange(
                v9x_vram_probe_offset(index), v9x_vram_probe_signature(index));
        }
        for (index = 0u; index < points; ++index) {
            v9x_mga_walk_readback[index] =
                V9xMgaScreenRead(v9x_vram_probe_offset(index));
        }
        for (index = points; index-- != 0u;) {
            (void)V9xMgaScreenExchange(v9x_vram_probe_offset(index),
                                       v9x_mga_walk_original[index]);
        }
        v9x_mga_measured_bytes = v9x_vram_probe_size(v9x_mga_walk_readback,
                                                     points);
    }
    return v9x_vram_probe_accept(v9x_mga_measured_bytes, reported_bytes,
                                 mapped_bytes);
}

/*
 * Key order is the diagnostic contract; see the note in s3_regs16.c.
 */
static void v9x_mga_publish_diagnostics(const V9X_HW16_DEVICE *device,
                                        v9x_hw16_write_fn write)
{
    char number[11];

    write("SchemaVersion", "1");
    write("Adapter", device->adapter);
    write("VendorId", device->vendor_text);
    write("DeviceId", device->device_text);
    write("ClockDetector", device->clock_detector);
    write("ClockStatus", "unavailable");
    write("ModeSwitching", device->mode_switching);
    write("Acceleration", v9x_mga_word(device->acceleration, "none"));
    write("Direct3D", v9x_mga_word(device->direct3d, "not-advertised"));
    if (v9x_vbe_vram_reported != 0ul) {
        v9x_mga_format_u32(number, v9x_vbe_vram_reported);
        write("VbeVramBytes", number);
    } else {
        write("VbeVramBytes", "unavailable");
    }
    v9x_mga_format_hex8(number, v9x_mga_read_crtcext3());
    write("MgaCrtcExt3", number);
    if (v9x_mga_walked != 0u) {
        v9x_mga_format_u32(number, v9x_mga_measured_bytes);
        write("VramMeasuredBytes", number);
    } else {
        write("VramMeasuredBytes", "not-walked");
    }
}

const V9X_HW16_OPS v9x_hw16 = {
    "matrox",
    v9x_mga_devices,
    (unsigned short)(sizeof(v9x_mga_devices) / sizeof(v9x_mga_devices[0])),
    v9x_mga_modes,
    (unsigned short)(sizeof(v9x_mga_modes) / sizeof(v9x_mga_modes[0])),
    /* The generic linear-framebuffer bit. This BIOS advertises a linear
     * framebuffer on every mode above (attributes 9Bh). */
    V9X_HW16_VBE_LINEAR,
    /* 16 MiB, the Millennium II's framebuffer window and largest memory;
     * the 2064W's is 8 MiB. This is only the ceiling believed from 4F00h,
     * which reported 8 MiB on the 2064W, so a card is mapped at its own
     * size. */
    0x00ffu, 0xffffu,
    v9x_mga_publish_diagnostics,
    0,
    /* NULL: the mode set is sufficient at tier-0; stage 9 checks the
     * stride. */
    0,
    /* NULL: ask the BIOS through 4F01h. */
    0,
    /* NULL: no size register to read before the mapping exists. The size
     * starts from 4F00h, clamped to the mapping above, and the memory walk
     * at the end of this table corrects it once the aperture is mapped. */
    0,
    /* NULL: CreateDIBPDevice builds the screen PDEVICE. */
    0,
    /* Strict, for the reason ati_hw16.c gives. */
    0u,
    /* NULL: PCI identifies both chips. */
    0,
    /* NULL: no top-of-memory reservation. */
    0,
    /* The BIOS's packed pitch. */
    0u,
    /* The memory walk: A8U4I5's 2164W BIOS reports 4 MiB of 8. */
    v9x_mga_measure_video_memory
};
