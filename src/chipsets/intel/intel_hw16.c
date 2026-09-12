#include "velocity9x/hw16.h"

extern unsigned long v9x_vbe_vram_reported;
extern const V9X_HW16_DEVICE v9x_gma950_device;

static const V9X_HW16_DEVICE * const v9x_intel_devices[] = {
    &v9x_gma950_device
};

/* Measured on the 1024x576 LVDS target; 0160/0161 are this VBIOS's OEM rows. */
static const V9X_HW16_MODE v9x_intel_modes[] = {
    {  640u, 480u,  8u,  640u, 0x0101u, 254, 127 },
    { 1024u, 576u,  8u, 1024u, 0x0160u, 407, 203 },
    {  640u, 480u, 16u, 1280u, 0x0111u, 254, 127 },
    { 1024u, 576u, 16u, 2048u, 0x0161u, 407, 203 }
};

static void v9x_intel_format_u32(char *text, unsigned long value)
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

static void v9x_intel_publish_diagnostics(const V9X_HW16_DEVICE *device,
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
        v9x_intel_format_u32(number, v9x_vbe_vram_reported);
        write("VbeVramBytes", number);
    } else {
        write("VbeVramBytes", "unavailable");
    }
}

const V9X_HW16_OPS v9x_hw16 = {
    "intel-gma",
    v9x_intel_devices,
    (unsigned short)(sizeof(v9x_intel_devices) / sizeof(v9x_intel_devices[0])),
    v9x_intel_modes,
    (unsigned short)(sizeof(v9x_intel_modes) / sizeof(v9x_intel_modes[0])),
    V9X_HW16_VBE_LINEAR,
    /* Map only 16 MiB of GMADR; VBE's usable-memory answer clamps further. */
    0x00ffu, 0xffffu,
    v9x_intel_publish_diagnostics,
    0,
    0,
    0,
    0,
    /* Strict PCI identity: only 8086:27AE is claimed. */
    0u,
    0
};
