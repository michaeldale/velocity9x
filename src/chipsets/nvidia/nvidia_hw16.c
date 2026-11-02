/*
 * The NVIDIA family table.
 *
 * One chip at tier-0, so there is nothing to dispatch on and no hook to
 * supply: the VBE 4F02h mode set programs the card, 4F01h reports where the
 * framebuffer landed, 4F00h reports its size, and the CPU draws
 * (docs\plans\nvidia-riva128.md, Phase 1). The family names the hardware it
 * claims so that it can grow native hooks - the 2D engine first - without
 * disturbing the generic vbe fallback.
 */
#include "velocity9x/hw16.h"

/* enable16.c, filled in by the tier-0 stage-3 default from VBE 4F00h. */
extern unsigned long v9x_vbe_vram_reported;

extern const V9X_HW16_DEVICE v9x_riva128_device;

static const V9X_HW16_DEVICE * const v9x_nvidia_devices[] = {
    &v9x_riva128_device
};

/*
 * The VESA-standard rows. Every mode number is in the STB Velocity 128 ZXV
 * BIOS's 46-entry list, each with attributes 0F9Fh (linear framebuffer
 * supported), and the vbe package set every one of them linear on that card
 * (docs\decisions\2026-11-02-m-7v636c-riva128-first-look.md). The 16 bpp
 * rows are 5:6:5 on this BIOS.
 *
 * The 32 bpp modes (112h/115h/118h) and the BIOS's own numbers above 12Fh
 * are left to the boot-time BIOS merge rather than claimed here.
 *
 * 640x400 sits after the other 8-bpp rows so this list runs in the order of
 * the MODES registry key GDI enumerates, as in every other family.
 */
static const V9X_HW16_MODE v9x_nvidia_modes[] = {
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
static void v9x_nvidia_format_u32(char *text, unsigned long value)
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
static const char *v9x_nvidia_word(const char *value, const char *fallback)
{
    return value != 0 ? value : fallback;
}

/*
 * Key order is the diagnostic contract; see the note in s3_regs16.c. No
 * NVIDIA register is read here, so the clock key says what is true.
 */
static void v9x_nvidia_publish_diagnostics(const V9X_HW16_DEVICE *device,
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
    write("Acceleration", v9x_nvidia_word(device->acceleration, "none"));
    write("Direct3D", v9x_nvidia_word(device->direct3d, "not-advertised"));
    if (v9x_vbe_vram_reported != 0ul) {
        v9x_nvidia_format_u32(number, v9x_vbe_vram_reported);
        write("VbeVramBytes", number);
    } else {
        write("VbeVramBytes", "unavailable");
    }
}

/*
 * The DirectDraw heap ends below RAMIN, the top 32 KiB of video memory
 * (docs\decisions\2026-11-02-riva128-register-probe.md), which the 2D
 * engine's bring-up writes after every mode set. Kept off the heap at
 * every depth, engine claimed or not, so a surface never sits where the
 * engine's tables go.
 */
#define V9X_NVIDIA_RAMIN_BYTES 0x00008000ul

static unsigned long v9x_nvidia_reserve_video_memory(
    unsigned long usable_bytes, unsigned long visible_bytes)
{
    if (usable_bytes < visible_bytes + V9X_NVIDIA_RAMIN_BYTES) {
        return usable_bytes;
    }
    return usable_bytes - V9X_NVIDIA_RAMIN_BYTES;
}

const V9X_HW16_OPS v9x_hw16 = {
    "nvidia",
    v9x_nvidia_devices,
    (unsigned short)(sizeof(v9x_nvidia_devices) /
                     sizeof(v9x_nvidia_devices[0])),
    v9x_nvidia_modes,
    (unsigned short)(sizeof(v9x_nvidia_modes) / sizeof(v9x_nvidia_modes[0])),
    /* The generic linear-framebuffer bit only; the measured BIOS honours it
     * for every mode the vbe package set. */
    V9X_HW16_VBE_LINEAR,
    /* 8 MiB, the RIVA 128ZX's maximum and what the measured card reports
     * through 4F00h, so also the ceiling this family believes. The RIVA
     * 128's RAMIN lives at the top of video memory (Linux fbdev.c,
     * rivafb_probe), which matters once the engine is used, not at tier-0. */
    0x007fu, 0xffffu,
    v9x_nvidia_publish_diagnostics,
    0,
    /* NULL: the mode set is sufficient at tier-0. */
    0,
    /* NULL: ask the BIOS through 4F01h. */
    0,
    /* NULL: PFB_BOOT_0 (BAR0 + 100000h) gives the size, but BAR0 is not
     * mapped at tier-0. The size comes from 4F00h, clamped to the mapping
     * above. */
    0,
    /* NULL: CreateDIBPDevice builds the screen PDEVICE. */
    0,
    /* Strict, for the reason ati_hw16.c gives: a vendor package stays bound
     * to the hardware it names; "my card is not listed" is the vbe
     * package's job. */
    0u,
    /* NULL: identify through PCI only. */
    0,
    v9x_nvidia_reserve_video_memory
};
