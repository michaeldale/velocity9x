/*
 * The matrox family table.
 *
 * One chip, and no family-wide hook: the VBE 4F02h mode set programs the
 * card, 4F01h reports where the framebuffer landed and 4F00h its size. The
 * chip's own engine hook (mga2064w_hw16.c) describes the drawing engine to
 * DirectDraw.
 */
#include "velocity9x/hw16.h"

/* enable16.c, filled in by the tier-0 stage-3 default from VBE 4F00h. */
extern unsigned long v9x_vbe_vram_reported;

extern const V9X_HW16_DEVICE v9x_mga2064w_device;

static const V9X_HW16_DEVICE * const v9x_mga_devices[] = {
    &v9x_mga2064w_device
};

/*
 * The VESA-standard 8 and 16 bpp rows. Every mode number is in this BIOS's
 * own list with a linear framebuffer, measured by emulated int10 on
 * 2026-09-11 (docs\probe\matrox-2064w-full-2026-09-11\vbe.ndjson). The
 * boot-time BIOS merge adds the rest - 1280x1024, 1600x1200 and the 32 bpp
 * modes - from the mini-VDD's collection.
 *
 * The BIOS pads both 800-wide rows: 960 bytes per scan line for 0103h and
 * 1920 for 0114h, where these rows ask for packed 800 and 1600. Stage 9
 * asks 4F06h for the packed width in pixels, the form this BIOS honours
 * (its byte form returns success and changes nothing, same record), and
 * refuses the mode if the stride does not follow. Which of the two happens
 * is not measured.
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
    /* 8 MiB, the BAR1 window Configuration Manager assigns this card and
     * the 2064W's maximum, so also the ceiling believed from 4F00h. */
    0x007fu, 0xffffu,
    v9x_mga_publish_diagnostics,
    0,
    /* NULL: the mode set is sufficient at tier-0; stage 9 checks the
     * stride. */
    0,
    /* NULL: ask the BIOS through 4F01h. */
    0,
    /* NULL: the size comes from 4F00h, clamped to the mapping above. The
     * 2026-09-11 alias probe measured 8 MiB on the BringupKit card, and
     * that card's 4F00h said the same; MGAPDX64 gives DirectDraw 7.1 MiB
     * of off-screen memory on this one at 800x600x16. */
    0,
    /* NULL: CreateDIBPDevice builds the screen PDEVICE. */
    0,
    /* Strict, for the reason ati_hw16.c gives. */
    0u
};
