/*
 * The tier-0 hardware table.
 *
 * This file is mostly holes, and that is the design. Every hook the interface
 * allows a family to supply is NULL here, which is what selects the
 * chip-agnostic default in each case: the VBE 4F02h mode set programs the card,
 * VBE 4F01h reports where its framebuffer landed, VBE 4F00h reports how much
 * memory it has, and the DIB Engine draws into it with the CPU. Nothing here
 * knows what chip it is talking to, so nothing here has to be ported to reach a
 * new one.
 *
 * What that costs is acceleration: engine_type stays NONE, so the 32-bit HAL
 * resolves no engine vtable and every blit falls to blt_cpu.c. A card that
 * deserves better gets a native family later; this is the floor, not the
 * ceiling.
 *
 * Known limit: the ViRGE/DX BIOS ignores the generic linear-framebuffer bit,
 * so this package is not expected to drive an S3 ViRGE. It refuses cleanly at
 * stage 3 rather than rendering wrongly. See hw16.h.
 */
#include "velocity9x/hw16.h"

/* enable16.c, filled in by the tier-0 stage-3 default from VBE 4F00h. */
extern unsigned long v9x_vbe_vram_bytes;

/*
 * Not static: the per-object audit resolves this symbol by name to prove the
 * family's own table is in the image, the same way each S3 chip module exports
 * its device.
 *
 * Both per-chip hooks are NULL. There is no aperture to enable - the mode set
 * already did it - and no engine to describe.
 */
const V9X_HW16_DEVICE v9x_vbe_device = {
    0x1234u, 0x1111u,
    "QEMU/Bochs VBE (generic VESA linear framebuffer)",
    "1234", "1111",
    "vbe-generic-unavailable-v1",
    "vbe-lfb",
    0,
    0,
    0,
    0
};

static const V9X_HW16_DEVICE * const v9x_vbe_devices[] = {
    &v9x_vbe_device
};

/*
 * The VESA-standard mode numbers and their packed pitches.
 *
 * These are the same rows the S3 family publishes, and deliberately so: they
 * are VBE's own numbering rather than any vendor's, which is the whole reason
 * a generic package can carry a mode list at all. The ordering rule carries
 * over too - 640x400 sits after the other 8-bpp rows so this list runs in the
 * same order as the MODES registry key GDI enumerates, which is what lets
 * Doom95 find it.
 *
 * A BIOS that disagrees about any of these pitches is refused at stage 3
 * rather than adapted to; see v9x_vbe_default_aperture in enable16.c.
 */
static const V9X_HW16_MODE v9x_vbe_modes[] = {
    {  640u, 480u,  8u,  640u, 0x0101u, 254, 127 },
    {  800u, 600u,  8u,  800u, 0x0103u, 318, 159 },
    { 1024u, 768u,  8u, 1024u, 0x0105u, 407, 203 },
    {  640u, 400u,  8u,  640u, 0x0100u, 254, 127 },
    {  640u, 480u, 16u, 1280u, 0x0111u, 254, 127 },
    {  800u, 600u, 16u, 1600u, 0x0114u, 318, 159 },
    { 1024u, 768u, 16u, 2048u, 0x0117u, 407, 203 }
};

/* Decimal, into a caller-owned buffer of at least 11 bytes. Local rather than
 * shared: the equivalent helper lives in s3_regs16.c, which carries the S3
 * unlock sequence and so cannot be linked into a generic image. */
static void v9x_vbe_format_u32(char *text, unsigned long value)
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

/*
 * Key order is the diagnostic contract; see the note in s3_regs16.c.
 *
 * No register is read here. On an unknown card there is no register this code
 * would know how to read, which is the point of the tier - so the clock and
 * acceleration keys report what is true rather than guessing.
 *
 * VbeVramBytes is the one fact tier-0 learns at run time, and the one worth
 * having in a bug report from a card nobody has tested.
 */
static void v9x_vbe_publish_diagnostics(const V9X_HW16_DEVICE *device,
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
    write("Acceleration", "none");
    write("Direct3D", "not-advertised");
    if (v9x_vbe_vram_bytes != 0ul) {
        v9x_vbe_format_u32(number, v9x_vbe_vram_bytes);
        write("VbeVramBytes", number);
    } else {
        write("VbeVramBytes", "unavailable");
    }
}

const V9X_HW16_OPS v9x_hw16 = {
    "vbe",
    v9x_vbe_devices,
    (unsigned short)(sizeof(v9x_vbe_devices) / sizeof(v9x_vbe_devices[0])),
    v9x_vbe_modes,
    (unsigned short)(sizeof(v9x_vbe_modes) / sizeof(v9x_vbe_modes[0])),
    /* The generic linear-framebuffer bit, and only that. The no-clear bit is an
     * S3 BIOS quirk, and an unknown BIOS should start from a clean framebuffer
     * rather than inheriting the previous mode's contents. */
    V9X_HW16_VBE_LINEAR,
    /* 16 MiB: the QEMU std-vga BAR. Also the ceiling this family will believe
     * from 4F00h, since the DirectDraw heap has to stay inside the mapping. */
    0x00ffu, 0xffffu,
    v9x_vbe_publish_diagnostics,
    /* NULL: the mode set is sufficient. */
    0,
    /* NULL: ask the BIOS through 4F01h. This one hole is the family. */
    0,
    /* NULL: CreateDIBPDevice builds the screen PDEVICE. */
    0
};
