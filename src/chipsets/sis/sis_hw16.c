/*
 * The SiS family table.
 *
 * One chip, and no family-wide hook: the VBE 4F02h mode set programs the
 * card, 4F01h reports where the framebuffer landed and 4F00h its size. The
 * chip's own hooks (sis6326_hw16.c) turn the 2D engine on after every mode
 * set and describe it to DirectDraw.
 */
#include "velocity9x/hw16.h"

/* enable16.c, filled in by the tier-0 stage-3 default from VBE 4F00h. */
extern unsigned long v9x_vbe_vram_reported;

extern const V9X_HW16_DEVICE v9x_sis6326_device;

static const V9X_HW16_DEVICE * const v9x_sis_devices[] = {
    &v9x_sis6326_device
};

/*
 * The VESA-standard rows. All seven mode numbers are in both measured BIOSes'
 * own lists: the 1.06 BIOS's single 33-entry list (ROM offset 7A98h) and the
 * 4 MiB list of the 1.28q BIOS's three (ROM offset 3285h, 44 entries), in
 * docs\probe\a8u4i5-sis6326-registers-2026-10-04\. Whether either BIOS
 * honours the linear-framebuffer bit, and what pitch it reports per mode, is
 * not yet measured; the first enable's V9XMODES.INI says.
 *
 * The 24 bpp modes (112h/115h/118h) are in both lists too and are left to
 * the boot-time BIOS merge rather than claimed here. This chip has no 32 bpp
 * mode (datasheet mode tables).
 *
 * 640x400 sits after the other 8-bpp rows so this list runs in the order of
 * the MODES registry key GDI enumerates, as in every other family.
 */
static const V9X_HW16_MODE v9x_sis_modes[] = {
    {  640u, 480u,  8u,  640u, 0x0101u, 254, 127 },
    {  800u, 600u,  8u,  800u, 0x0103u, 318, 159 },
    { 1024u, 768u,  8u, 1024u, 0x0105u, 407, 203 },
    {  640u, 400u,  8u,  640u, 0x0100u, 254, 127 },
    {  640u, 480u, 16u, 1280u, 0x0111u, 254, 127 },
    {  800u, 600u, 16u, 1600u, 0x0114u, 318, 159 },
    { 1024u, 768u, 16u, 2048u, 0x0117u, 407, 203 }
};

/* Decimal, into a caller-owned buffer of at least 11 bytes. Local for the
 * reason ati_hw16.c gives: the shared helper lives in an object that carries
 * another vendor's register sequence. */
static void v9x_sis_format_u32(char *text, unsigned long value)
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

/* The chip's own word, or the family default when it carries none; see the
 * note in ati_hw16.c on why these are never literals. */
static const char *v9x_sis_word(const char *value, const char *fallback)
{
    return value != 0 ? value : fallback;
}

/*
 * Key order is the diagnostic contract; see the note in s3_regs16.c. No SiS
 * register is read here, so the clock key says what is true.
 */
static void v9x_sis_publish_diagnostics(const V9X_HW16_DEVICE *device,
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
    write("Acceleration", v9x_sis_word(device->acceleration, "none"));
    write("Direct3D", v9x_sis_word(device->direct3d, "not-advertised"));
    if (v9x_vbe_vram_reported != 0ul) {
        v9x_sis_format_u32(number, v9x_vbe_vram_reported);
        write("VbeVramBytes", number);
    } else {
        write("VbeVramBytes", "unavailable");
    }
}

const V9X_HW16_OPS v9x_hw16 = {
    "sis",
    v9x_sis_devices,
    (unsigned short)(sizeof(v9x_sis_devices) / sizeof(v9x_sis_devices[0])),
    v9x_sis_modes,
    (unsigned short)(sizeof(v9x_sis_modes) / sizeof(v9x_sis_modes[0])),
    /* The generic linear-framebuffer bit only. Whether this BIOS honours it
     * is unmeasured: a BIOS that ignores it refuses at stage 3, which is the
     * documented tier-0 limit the ViRGE hit. */
    V9X_HW16_VBE_LINEAR,
    /* 4 MiB, the size of BAR0 on both measured boards and the datasheet's
     * maximum, so also the ceiling this family believes from 4F00h. */
    0x003fu, 0xffffu,
    v9x_sis_publish_diagnostics,
    0,
    /* NULL: the mode set is sufficient at tier-0. */
    0,
    /* NULL: ask the BIOS through 4F01h. */
    0,
    /* NULL: no SiS memory-size register is read at tier-0; SRC D[2:1] gives
     * it (measured on both boards), but that needs SR05 unlocked, which is a
     * write. The size comes from 4F00h, clamped to the mapping above. */
    0,
    /* NULL: CreateDIBPDevice builds the screen PDEVICE. */
    0,
    /* Strict, for the reason ati_hw16.c gives: a vendor package stays bound
     * to the hardware it names; "my card is not listed" is the vbe
     * package's job. */
    0u
};
