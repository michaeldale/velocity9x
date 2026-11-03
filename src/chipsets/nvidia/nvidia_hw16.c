/*
 * The NVIDIA family table.
 *
 * One chip at tier-0, so there is nothing to dispatch on and no hook to
 * supply: the VBE 4F02h mode set programs the card, 4F01h reports where the
 * framebuffer landed, 4F00h reports its size, and the CPU draws
 * (docs\plans\nvidia-riva128.md, Phase 1). The family names the hardware it
 * claims so that it can grow native hooks - the 2D engine first - without
 * disturbing the generic vbe fallback.
 *
 * The one register this table writes is the DAC's 5:6:5 select, so that
 * 16 bpp desktops scan out as the 5:5:5 the engine draws
 * (docs\plans\nvidia-riva128-d3d.md, step 3).
 */
#include "velocity9x/hw16.h"
#include "velocity9x/vbe_modes.h"

/* enable16.c, filled in by the tier-0 stage-3 default from VBE 4F00h. */
extern unsigned long v9x_vbe_vram_reported;

/* ddi.c and modes16.c: the mode just set, and the 16 bpp layout decided
 * for it. */
extern unsigned short v9x_selected_mode_geometry(
    unsigned short *width, unsigned short *height,
    unsigned short *bpp, unsigned short *pitch);
extern unsigned short v9x_modes16_highcolor(void);

extern const V9X_HW16_DEVICE v9x_riva128_device;

/* runtime.asm, nvidia family only: BAR0 through the engine selector. */
extern unsigned short __far __pascal V9xNv3Selector(unsigned long linear);
extern unsigned long __far __pascal V9xNv3Read(unsigned long offset);
extern void __far __pascal V9xNv3Write(unsigned long offset,
                                       unsigned long value);

/*
 * PRAMDAC general control, and its 5:6:5 select. 86Box's vid_nv3.h names
 * bit 12 NV3_PRAMDAC_GENERAL_CONTROL_565_MODE; this BIOS's 16 bpp modes
 * leave the register at 00101100h, and rivafb writes 00100100h on NV3,
 * whose 16 bpp is always 5:5:5
 * (docs\decisions\2026-11-02-riva128-register-probe.md).
 */
#define V9X_NVIDIA_PRAMDAC_GENERAL 0x00680600ul
#define V9X_NVIDIA_GENERAL_565     0x00001000ul

/* The register before and after the last switch, for V9XHW.INI; 0 until
 * the hook has reached the card. */
static unsigned long v9x_nvidia_general_before = 0ul;
static unsigned long v9x_nvidia_general_after = 0ul;

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
    v9x_nvidia_format_u32(number, v9x_nvidia_general_before);
    write("PramdacGeneralBefore555", number);
    v9x_nvidia_format_u32(number, v9x_nvidia_general_after);
    write("PramdacGeneralAfter555", number);
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

/*
 * The 16 bpp scan-out to 5:5:5, because the engine draws nothing else
 * (docs\decisions\2026-11-02-riva128-2d-engine-writes.md) and the BIOS
 * lists no 15 bpp mode to switch to instead. BAR0 through the device's own
 * engine window: no window, as off the measured memory size, and the card
 * keeps the BIOS's 5:6:5.
 */
static void v9x_nvidia_scan_out_555(void)
{
    const V9X_HW16_DEVICE *device = v9x_hw16_active_device();
    unsigned long control = 0ul;
    unsigned long aperture = 0ul;
    unsigned long type = 0ul;
    unsigned long caps = 0ul;
    unsigned long gtt = 0ul;
    unsigned long ring = 0ul;
    unsigned long ring_bytes = 0ul;
    unsigned long general;

    if (device == 0 || device->fill_engine_descriptor == 0) {
        return;
    }
    device->fill_engine_descriptor(0ul, &control, &aperture, &type, &caps,
                                   &gtt, &ring, &ring_bytes);
    if (control == 0ul || V9xNv3Selector(control) == 0u) {
        return;
    }

    general = V9xNv3Read(V9X_NVIDIA_PRAMDAC_GENERAL);
    v9x_nvidia_general_before = general;
    V9xNv3Write(V9X_NVIDIA_PRAMDAC_GENERAL, general & ~V9X_NVIDIA_GENERAL_565);
    v9x_nvidia_general_after = V9xNv3Read(V9X_NVIDIA_PRAMDAC_GENERAL);
}

/*
 * The desktop starts at the start of memory; this hook is here for the
 * other half of its job, programming how the desktop scans out. It runs
 * after every mode set and pitch step, on Enable and on the return from a
 * full-screen DOS box, which is where the DAC's layout has to be set again.
 * A 16 bpp row is 5:5:5 exactly when the layout is, since every row here
 * is its own 5:5:5 sibling (modes16.c).
 */
static unsigned long v9x_nvidia_place_desktop(unsigned short pitch_bytes)
{
    unsigned short width;
    unsigned short height;
    unsigned short bpp;
    unsigned short pitch;

    (void)pitch_bytes;
    if (v9x_selected_mode_geometry(&width, &height, &bpp, &pitch) != 0u &&
        bpp == 16u && v9x_modes16_highcolor() == V9X_HIGHCOLOR_555) {
        v9x_nvidia_scan_out_555();
    }
    return 0ul;
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
    v9x_nvidia_reserve_video_memory,
    /* unalias_pitch: the BIOS's stride. */
    0u,
    /* NULL: 4F00h's memory size is right on the measured card. */
    0,
    v9x_nvidia_place_desktop
};
