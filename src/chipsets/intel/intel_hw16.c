#include "velocity9x/hw16.h"
#include "velocity9x/intel16.h"

extern unsigned long v9x_vbe_vram_reported;
extern const V9X_HW16_DEVICE v9x_gma950_device;
extern unsigned long v9x_gma950_reserve_video_memory(
    unsigned long usable_bytes, unsigned long visible_bytes);

/*
 * The hook tables are near and live in _TEXT, by the four-family convention
 * (docs\plans\multi-chip-restructure.md). v9x_intel_publish_event lives in
 * the I9XXCODE segment, so its address cannot go in the near publish_event
 * slot - a near pointer to it would be an offset into the wrong segment. The
 * slot holds this forwarder instead, which makes the far call. See
 * velocity9x/intel16.h for the rule this is the one instance of.
 */
static void v9x_intel_publish_event_near(unsigned short kind,
                                         unsigned short context)
{
    v9x_intel_publish_event(kind, context);
}

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

/* The chip's own word, or the family default when it carries none. Both keys
 * were literals here, which is how a chip that had gained an engine went on
 * publishing "not-advertised" from a second copy nobody updated. */
static const char *v9x_intel_word(const char *value, const char *fallback)
{
    return value != 0 ? value : fallback;
}

extern const char *v9x_gma950_engine_status_text(void);

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
    write("Acceleration", v9x_intel_word(device->acceleration, "none"));
    /*
     * THE CHIP'S WORD, not a literal.
     *
     * This wrote "not-advertised" unconditionally while v9x_gma950_device
     * carried "hardware-gen3" one file away, so the settings page - which
     * reads this key - offered no Hardware entry on the card the Gen3 engine
     * was written for. Photographed on the netbook 2026-09-16, build a2b9c27,
     * after the chip word had already been fixed and check-tree had already
     * been taught to compare the manifest against it: the rule asked whether
     * the string was PRESENT in the chip's source, and it was. It was simply
     * not the string that reached the file.
     *
     * The s3 publisher has written device->direct3d since it was written.
     * Three of the five families duplicated the literal instead.
     */
    write("Direct3D", v9x_intel_word(device->direct3d, "not-advertised"));
    /*
     * And why the engine descriptor answered as it did, which nothing
     * published before: a capture showing Direct3DMode=none could not say
     * whether the mapping, the permission or the ring was what stopped it.
     *
     * NAMED FOR ITS MOMENT. This file is written at Enable, and on this family
     * that is before the mini-VDD's capture paths have mapped BAR0 and BAR3 -
     * so "map-refused" here is the NORMAL reading on a first enable and says
     * nothing about whether the engine came up. It was called EngineStatus,
     * which invited exactly that misreading, and did: intel50 was read as a
     * failure on the strength of it while the event capture showed the ring
     * enabled. The answer that matters is EngineStamp=, written by dd16.c when
     * the shared block is stamped.
     */
    write("EngineStatusEnable", v9x_gma950_engine_status_text());
    if (v9x_vbe_vram_reported != 0ul) {
        v9x_intel_format_u32(number, v9x_vbe_vram_reported);
        write("VbeVramBytes", number);
    } else {
        write("VbeVramBytes", "unavailable");
    }
    v9x_intel_publish_mmio_fingerprint();
    v9x_intel_publish_gtt_inventory();
    v9x_intel_publish_ring_plan();
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
    v9x_intel_publish_event_near,
    0,
    0,
    0,
    0,
    /* Strict PCI identity: only 8086:27AE is claimed. */
    0u,
    0,
    v9x_gma950_reserve_video_memory
};
