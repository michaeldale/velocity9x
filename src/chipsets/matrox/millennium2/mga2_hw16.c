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
#include "win9x_display_abi.h"

extern void FAR PASCAL V9xDibBeginAccess(void);
extern void FAR PASCAL V9xDibEndAccess(void);

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
    v9x_mga2_build_screen_pdevice
};
