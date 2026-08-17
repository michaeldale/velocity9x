/*
 * The ATI Mach64 / Rage family table.
 *
 * Two chips, one binary, dispatched at run time by PCI id - the shape
 * s3_hw16.c proved at phase 8. Everything the two chips agree about lives
 * here; anything only one of them does belongs in that chip's own module, or
 * the per-object audit cannot tell them apart.
 *
 * At tier-0 they agree about everything, because neither supplies a hook. The
 * VBE 4F02h mode set programs the card, 4F01h reports where the framebuffer
 * landed, 4F00h reports its size, and the CPU draws. That is deliberately the
 * same starting point the generic vbe family occupies - the difference is that
 * this family names the hardware it claims, so it can grow native hooks
 * without disturbing the generic fallback.
 */
#include "velocity9x/hw16.h"

/* enable16.c, filled in by the tier-0 stage-3 default from VBE 4F00h. */
extern unsigned long v9x_vbe_vram_reported;

extern const V9X_HW16_DEVICE v9x_mach64_vt2_device;
extern const V9X_HW16_DEVICE v9x_rage_mobility_device;

/*
 * The emulated part first, so a run with no -ChipId lands on the target that
 * can actually be booted unattended. V9xFindPciDevice scans this list and
 * records the matched index, so order here is a default, not a restriction.
 */
static const V9X_HW16_DEVICE * const v9x_ati_devices[] = {
    &v9x_mach64_vt2_device,
    &v9x_rage_mobility_device
};

/*
 * VESA-standard mode numbers, the same seven rows the generic family carries.
 *
 * All seven were confirmed present with a linear framebuffer in the Rage's own
 * BIOS mode list, and all seven appear in the panel's per-mode timing table -
 * including 640x400, which the plan had flagged as doubtful.
 *
 * Ordering follows the established rule: 640x400 sits after the other 8-bpp
 * rows so this list runs in the same order as the MODES registry key GDI
 * enumerates, which is what lets Doom95 find it.
 *
 * A note for stage 4, not a change to make now: ddi.c applies modes[0] before
 * any DDI entry point runs, and the laptop's panel is a fixed 1024x768 with no
 * EDID, so 1024x768x16 first may light up more reliably there. It is left in
 * the conventional order for now because the emulated target has no panel and
 * because the stage-3 exit gate is an unaccelerated desktop at 640x480x8.
 * Revisit once the panel's real behaviour is known.
 */
static const V9X_HW16_MODE v9x_ati_modes[] = {
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
 * unlock sequence and so cannot be linked into this image. */
static void v9x_ati_format_u32(char *text, unsigned long value)
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
 * No Mach64 register is read here yet, so the clock and acceleration keys say
 * what is true rather than guessing.
 *
 * VbeVramBytes is the raw 4F00h answer, not the floored figure the driver
 * uses. Measured on the Rage Mobility: 4 MiB from real DOS, but 512 KiB from a
 * DOS box under Windows, where the stock driver owns the card. Reporting the
 * raw number is what makes that distinction visible in a bug report.
 */
static void v9x_ati_publish_diagnostics(const V9X_HW16_DEVICE *device,
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
    if (v9x_vbe_vram_reported != 0ul) {
        v9x_ati_format_u32(number, v9x_vbe_vram_reported);
        write("VbeVramBytes", number);
    } else {
        write("VbeVramBytes", "unavailable");
    }
}

const V9X_HW16_OPS v9x_hw16 = {
    "ati",
    v9x_ati_devices,
    (unsigned short)(sizeof(v9x_ati_devices) / sizeof(v9x_ati_devices[0])),
    v9x_ati_modes,
    (unsigned short)(sizeof(v9x_ati_modes) / sizeof(v9x_ati_modes[0])),
    /* The generic linear-framebuffer bit only; the no-clear bit is an S3 BIOS
     * quirk and means nothing here.
     *
     * Measured from real DOS on the Rage Mobility: the Mach64 BIOS honours
     * this bit. All seven modes below come back supported with a linear
     * framebuffer. The ViRGE/DX ignores it, which is the documented tier-0
     * limit and is what made this worth checking rather than assuming. */
    V9X_HW16_VBE_LINEAR,
    /* 16 MiB. The Mobility's BAR0 is a 16 MiB aperture, and this is also the
     * ceiling this family will believe from 4F00h, since the DirectDraw heap
     * has to stay inside the mapping. */
    0x00ffu, 0xffffu,
    v9x_ati_publish_diagnostics,
    /* NULL: the mode set is sufficient at tier-0. */
    0,
    /* NULL: ask the BIOS through 4F01h, and that is enough here. Measured from
     * real DOS, this card's 4F01h returns PhysBasePtr F5000000 for every mode,
     * which is exactly its BAR0 - so the native read_aperture hook once
     * contemplated as a fallback is not required. */
    0,
    /* NULL: CreateDIBPDevice builds the screen PDEVICE. */
    0,
    /*
     * Strict, even though this family is tier-0 and touches no ATI register.
     * The permissive setting belongs to the generic vbe package, which is the
     * one answer to "my card is not listed"; a vendor package staying strict
     * is what keeps its name meaningful and stops ATI identity strings being
     * published for a card that is not one. A Rage Pro owner - the id this
     * family deliberately does not claim - installs the vbe package.
     */
    0u
};
