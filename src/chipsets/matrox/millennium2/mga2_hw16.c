/*
 * Matrox Millennium II MGA-2164W hardware table.
 *
 * Two things set this family apart from the S3 ones, and both used to be
 * #ifdef'd into src\display16\ddi.c:
 *
 *   - it publishes a static diagnostics block, because no register read for
 *     the MGA-2164W's clocks or installed memory has been validated on the
 *     physical card;
 *   - it builds the screen PDEVICE itself instead of calling the DIB Engine's
 *     CreateDIBPDevice, because this card's BIOS-selected scan-line layout is
 *     not reconstructed reliably from the BITMAPINFO. Every surface field is
 *     therefore set explicitly and is auditable.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/vbe16.h"
#include "win9x_display_abi.h"

extern void FAR PASCAL V9xDibBeginAccess(void);
extern void FAR PASCAL V9xDibEndAccess(void);

/*
 * PCI BAR0 read, and the aperture the driver settled on.
 *
 * The configuration read stays in runtime.asm: PCI BIOS B10Ah returns its
 * dword in ECX and the masks below are 32-bit, while this file is compiled
 * for 8086. It is a chip-agnostic INT 1Ah primitive parametrized by the
 * family's device list, so it belongs there anyway.
 *
 * Returns non-zero on success and stores the validated base.
 */
extern WORD FAR PASCAL V9xPciReadBar0(DWORD FAR *base);

/* The active mode's geometry, published by ddi.c for the runtime. */
extern WORD v9x_active_width;
extern WORD v9x_active_pitch;

/* Not static: see the note in virge_hw16.c. */
const V9X_HW16_DEVICE v9x_mga2_devices[] = {
    {
        0x102bu, 0x051bu,
        "Matrox Millennium II MGA-2164W",
        "102B", "051B",
        "matrox-mga2164w-unavailable-v1",
        "single-mode",
        0,
        0
    }
};

static const V9X_HW16_MODE v9x_mga2_modes[] = {
#ifdef V9X_MATROX_16BPP
    {  640u, 480u, 16u, 1280u, 0x0111u, 254, 127 },
    {  800u, 600u, 16u, 1600u, 0x0114u, 318, 159 },
    { 1024u, 768u, 16u, 2048u, 0x0117u, 407, 203 }
#else
    /* The physical Millennium II reports a packed 640-byte pitch for 101h. */
    {  640u, 480u,  8u,  640u, 0x0101u, 254, 127 }
#endif
};

static void v9x_mga2_publish_diagnostics(const V9X_HW16_DEVICE *device,
                                         v9x_hw16_write_fn write)
{
    write("SchemaVersion", "1");
    write("Adapter", device->adapter);
    write("VendorId", device->vendor_text);
    write("DeviceId", device->device_text);
    write("ClockDetector", device->clock_detector);
    write("ClockStatus", "unavailable");
    write("ModeSwitching", device->mode_switching);
}

/*
 * VBE 4F06h subfunction 0 forces the logical scan-line length to the packed
 * pitch the audited mode table selected, and rejects a BIOS that quietly
 * chooses a different stride: reported success with a different stride would
 * misplace every scan line.
 *
 * This is deliberately here rather than in the shared vbe16 service. Only
 * this family calls 4F06h, and the shared object is linked into all of them,
 * so keeping it here is what lets the audit tell this path from the S3 one.
 *
 * Both AX and the resulting pitch in BX matter, so BX is moved into CX and the
 * pair returned as one dword.
 */
static unsigned long v9x_mga2_int10_scan_line(unsigned short ax,
                                              unsigned short bx,
                                              unsigned short cx);
#pragma aux v9x_mga2_int10_scan_line =  \
    "int 10h"                           \
    "mov cx, bx"                        \
    parm [ax] [bx] [cx]                 \
    value [cx ax]                       \
    modify [ax bx cx dx si di es]

static unsigned short v9x_mga2_post_mode_set(void)
{
    unsigned long result = v9x_mga2_int10_scan_line(0x4f06u, 0x0000u,
                                                    v9x_active_width);

    if ((unsigned short)(result & 0xfffful) != 0x004fu) {
        return 0u;
    }
    return (unsigned short)(result >> 16) == v9x_active_pitch ? 1u : 0u;
}

/*
 * BAR0 is MGABASE2, the direct framebuffer aperture. No MGA MMIO register is
 * touched during this conservative activation - the VBE mode set is the only
 * thing that has programmed the card.
 */
static unsigned long v9x_mga2_read_aperture(void)
{
    DWORD base = 0ul;

    if (V9xPciReadBar0(&base) == 0u) {
        return 0ul;
    }
    return base;
}

/*
 * The working vmdisp9x minidriver initializes the screen DIBENGINE record
 * explicitly. That is important for hardware whose BIOS-selected scan-line
 * layout is not reconstructed reliably by CreateDIBPDevice.
 *
 * The caller finalizes deBeginAccess, deEndAccess and deVersion afterwards on
 * every target; they are set here as well so the record is complete before it
 * is handed back, exactly as the previous in-line version did.
 */
static unsigned long v9x_mga2_build_screen_pdevice(void *device_info,
                                                   void *bitmap_info,
                                                   const V9X_HW16_MODE *mode,
                                                   unsigned short screen_selector,
                                                   unsigned short pdevice_flags)
{
    V9X_DIB_ENGINE FAR *pdevice = (V9X_DIB_ENGINE FAR *)device_info;

    pdevice->deType = V9X_TYPE_DIBENG;
    pdevice->deWidth = mode->width;
    pdevice->deHeight = mode->height;
    pdevice->deWidthBytes = mode->pitch;
    pdevice->dePlanes = 1u;
    pdevice->deBitsPixel = mode->bits_per_pixel;
    pdevice->deReserved1 = 0ul;
    pdevice->deDeltaScan = mode->pitch;
    pdevice->delpPDevice = 0;
    pdevice->deBitsOffset = 0ul;
    pdevice->deBitsSelector = screen_selector;
    pdevice->deFlags = pdevice_flags;
    pdevice->deVersion = V9X_DE_VERSION;
    pdevice->deBitmapInfo = (LPBITMAPINFO)bitmap_info;
    pdevice->deBeginAccess = V9xDibBeginAccess;
    pdevice->deEndAccess = V9xDibEndAccess;
    pdevice->deDriverReserved = 0ul;
    return 1ul;
}

const V9X_HW16_OPS v9x_hw16 = {
    "matrox-m2",
    v9x_mga2_devices,
    (unsigned short)(sizeof(v9x_mga2_devices) / sizeof(v9x_mga2_devices[0])),
    v9x_mga2_modes,
    (unsigned short)(sizeof(v9x_mga2_modes) / sizeof(v9x_mga2_modes[0])),
    /* Request the advertised linear framebuffer. Unlike the S3 path, do not
     * preserve the previous mode's framebuffer origin: the physical
     * Millennium II BIOS must establish a fresh origin at BAR0 offset zero. */
    V9X_HW16_VBE_LINEAR,
    0x03ffu, 0xffffu,
    v9x_mga2_publish_diagnostics,
    v9x_mga2_post_mode_set,
    v9x_mga2_read_aperture,
    /* VBE 4F02h already enabled the linear framebuffer; do not write MGA
     * control registers during this conservative first activation. */
    0,
    v9x_mga2_build_screen_pdevice,
    /* No 2D/3D engine on this conservative path. */
    0
};
