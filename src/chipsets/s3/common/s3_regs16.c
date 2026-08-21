/*
 * S3 register access shared by every S3 family binary.
 *
 * Lifted verbatim out of src\display16\ddi.c, where it sat behind
 * "#ifndef V9X_TARGET_MATROX_MILLENNIUM2". The behaviour, the register
 * sequence and the published key order are unchanged; only the location is.
 *
 * Port I/O is done through the #pragma aux pattern already proven in ddi.c
 * rather than by calling into assembly, so this stays one translation unit
 * with no new far calls.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/s3_regs16.h"
#include "velocity9x/s3_virge.h"
#include "velocity9x/status.h"

static unsigned char v9x_port_in(unsigned short port);
#pragma aux v9x_port_in = "in al,dx" parm [dx] value [al] modify exact [al]

static void v9x_port_out(unsigned short port, unsigned char value);
#pragma aux v9x_port_out = "out dx,al" parm [dx] [al] modify exact []

void v9x_format_u32(char *text, unsigned long value)
{
    char reverse[11];
    unsigned short length = 0u;
    unsigned short index;

    do {
        reverse[length++] = (char)('0' + (value % 10ul));
        value /= 10ul;
    } while (value != 0ul && length < 10u);
    for (index = 0u; index < length; ++index) {
        text[index] = reverse[length - index - 1u];
    }
    text[length] = '\0';
}

static unsigned char v9x_s3_read_sequencer(unsigned char index)
{
    v9x_port_out(0x03c4u, index);
    return v9x_port_in(0x03c5u);
}

/* The CRTC pair follows the MISC output register's colour/mono bit. */
static unsigned short v9x_crtc_index_port(void)
{
    return (v9x_port_in(0x03ccu) & 0x01u) != 0u ? 0x03d4u : 0x03b4u;
}

static unsigned char v9x_s3_read_crtc(unsigned short index_port,
                                      unsigned char index)
{
    v9x_port_out(index_port, index);
    return v9x_port_in((unsigned short)(index_port + 1u));
}

static void v9x_s3_write_crtc(unsigned short index_port, unsigned char index,
                              unsigned char value)
{
    v9x_port_out(index_port, index);
    v9x_port_out((unsigned short)(index_port + 1u), value);
}

/*
 * Installed video memory from CRTC register 36h.
 *
 * CR36 sits behind the S3 extended-register locks, so CR38 and CR39 are
 * unlocked around the read and restored afterwards. Returns 0 when the code
 * is one this driver does not decode, which the caller reports as unavailable
 * rather than guessing a size.
 *
 * Two callers now: the diagnostics block below, which reports it, and the
 * family's read_video_memory hook, which sizes the DirectDraw heap with it.
 * Both want the same reading, so it stays one function.
 */
unsigned long v9x_s3_read_video_memory(void)
{
    unsigned short index_port = v9x_crtc_index_port();
    unsigned char saved_index = v9x_port_in(index_port);
    unsigned char saved_lock1;
    unsigned char saved_lock2;
    unsigned char cr36;
    unsigned long bytes = 0ul;

    saved_lock1 = v9x_s3_read_crtc(index_port, 0x38u);
    saved_lock2 = v9x_s3_read_crtc(index_port, 0x39u);
    v9x_s3_write_crtc(index_port, 0x38u, 0x48u);
    v9x_s3_write_crtc(index_port, 0x39u, 0xa5u);
    cr36 = v9x_s3_read_crtc(index_port, 0x36u);
    v9x_s3_write_crtc(index_port, 0x39u, saved_lock2);
    v9x_s3_write_crtc(index_port, 0x38u, saved_lock1);
    v9x_port_out(index_port, saved_index);

    if (v9x_s3_virge_decode_memory_size(cr36, &bytes) != V9X_STATUS_OK) {
        return 0ul;
    }
    return bytes;
}

/*
 * The aperture registers are addressed at the fixed colour CRTC pair, not the
 * MISC-derived one the diagnostics reads use. That is what the assembly did
 * and what the S3 sample does: by the time this runs the card is in a VBE
 * graphics mode, so the mono pair cannot be the live one.
 */
#define V9X_S3_CRTC_INDEX 0x03d4u
#define V9X_S3_CRTC_DATA  0x03d5u

unsigned char v9x_s3_crtc_read(unsigned char index)
{
    v9x_port_out(V9X_S3_CRTC_INDEX, index);
    return v9x_port_in(V9X_S3_CRTC_DATA);
}

void v9x_s3_crtc_write(unsigned char index, unsigned char value)
{
    v9x_port_out(V9X_S3_CRTC_INDEX, index);
    v9x_port_out(V9X_S3_CRTC_DATA, value);
}

static void v9x_s3_unlock_extended(void)
{
    v9x_s3_crtc_write(0x38u, 0x48u);
    v9x_s3_crtc_write(0x39u, 0xa5u);
}

/*
 * The identity a VESA Local Bus card cannot publish to a bus it is not on.
 *
 * CR2D and CR2E hold the same 16-bit device id the PCI parts put in
 * configuration space - CR2D the high byte, CR2E the low - so on a machine with
 * no PCI the chip can still be asked directly. The answer is matched against
 * v9x_pci_device, the family's own list, and v9x_pci_match is set exactly as
 * V9xFindPciDevice would have set it. Nothing downstream needs to know which
 * route found the card.
 *
 * Two properties make this safe enough to gate the whole enable sequence on:
 *
 *   It reads and does not write. No unlock, so nothing is poked on a card that
 *   turns out not to be an S3. Measured on the 486 VLB Trio64 on 2026-08-21:
 *   the video BIOS leaves CR38/CR39 holding 59h/BDh rather than the unlock
 *   keys, and CR2D/CR2E read 88h/11h regardless - on these parts the locks gate
 *   writes, not reads.
 *
 *   It accepts only ids this family already names. Two values out of 65536 for
 *   the s3 binary, which is a far narrower claim than the survey's exploratory
 *   accept set needed to be. A card whose CR2D/CR2E do not spell one of ours is
 *   refused, exactly as the PCI scan refuses it.
 *
 * The colour/mono CRTC pair is derived from the MISC output register here
 * rather than assumed to be 3D4h, because this runs before any mode set - the
 * fixed pair the aperture reads use is right only once the card is in a
 * graphics mode.
 */
/*
 * The PCI identity table and the match index, both owned by
 * src\display16\ddi.c. Declared rather than included because this object is
 * chipset code and must not acquire a dependency on the display16 headers -
 * see the OS-boundary check in scripts\check-tree.ps1.
 */
extern unsigned short v9x_pci_device[];
extern unsigned short v9x_pci_count;
extern unsigned short v9x_pci_match;

unsigned short v9x_s3_identify_without_pci(void)
{
    unsigned short index_port = v9x_crtc_index_port();
    unsigned char saved_index = v9x_port_in(index_port);
    unsigned char high;
    unsigned char low;
    unsigned short device_id;
    unsigned short index;

    high = v9x_s3_read_crtc(index_port, 0x2du);
    low = v9x_s3_read_crtc(index_port, 0x2eu);
    v9x_port_out(index_port, saved_index);

    device_id = (unsigned short)(((unsigned short)high << 8) | low);
    if (device_id == 0x0000u || device_id == 0xffffu) {
        return 0xffffu;
    }

    for (index = 0u; index < v9x_pci_count; ++index) {
        if (v9x_pci_device[index] == device_id) {
            v9x_pci_match = index;
            return index;
        }
    }
    return 0xffffu;
}

unsigned long v9x_s3_read_aperture(void)
{
    unsigned short high;
    unsigned short low;
    unsigned long base;

    v9x_s3_unlock_extended();
    high = v9x_s3_crtc_read(0x59u);
    low = v9x_s3_crtc_read(0x5au);
    base = ((unsigned long)((high << 8) | low)) << 16;

    if (base < 0x01000000ul || base > 0xffc00000ul) {
        return 0ul;
    }
    return base;
}

unsigned short v9x_s3_enable_linear_aperture(void)
{
    unsigned char value;

    v9x_s3_unlock_extended();

    /* CR58: 4 MiB aperture in [1:0] plus linear addressing in [4]. Read back
     * and require the bits to have stuck; a card that ignores the write must
     * not be treated as mapped. */
    value = v9x_s3_crtc_read(0x58u);
    value = (unsigned char)((value & 0xfcu) | 0x13u);
    v9x_port_out(V9X_S3_CRTC_DATA, value);
    if ((v9x_port_in(V9X_S3_CRTC_DATA) & 0x13u) != 0x13u) {
        return 0u;
    }

    /* CR40[0] enables the graphics engine. Without it, engine register
     * offsets address framebuffer memory instead. */
    value = v9x_s3_crtc_read(0x40u);
    v9x_port_out(V9X_S3_CRTC_DATA, (unsigned char)(value | 0x01u));
    if ((v9x_port_in(V9X_S3_CRTC_DATA) & 0x01u) == 0u) {
        return 0u;
    }
    return 1u;
}

/*
 * The shared S3 diagnostics block.
 *
 * Key order is the diagnostic contract: C:\V9XHW.INI is written by appending,
 * and the settings page and the support instructions both read it. Do not
 * reorder these calls.
 */
void v9x_s3_publish_diagnostics(const V9X_HW16_DEVICE *device,
                                v9x_hw16_write_fn write)
{
    struct v9x_clock_info clocks;
    unsigned char saved_index = v9x_port_in(0x03c4u);
    unsigned char saved_unlock;
    unsigned char sr10;
    unsigned char sr11;
    char number[11];
    v9x_status status;

    v9x_port_out(0x03c4u, 0x08u);
    saved_unlock = v9x_port_in(0x03c5u);
    v9x_port_out(0x03c5u, 0x06u);
    sr10 = v9x_s3_read_sequencer(0x10u);
    sr11 = v9x_s3_read_sequencer(0x11u);
    v9x_port_out(0x03c4u, 0x08u);
    v9x_port_out(0x03c5u, saved_unlock);
    v9x_port_out(0x03c4u, saved_index);

    write("SchemaVersion", "1");
    write("Adapter", device->adapter);
    write("VendorId", device->vendor_text);
    write("DeviceId", device->device_text);
    write("ClockDetector", device->clock_detector);
    write("ModeSwitching", device->mode_switching);
    /* What the DirectDraw HAL actually executes on the engine. Both S3
     * targets do bounded solid fills and screen-to-screen copies; only the
     * ViRGE advertises Direct3D. */
    write("Acceleration", device->acceleration);
    write("Direct3D", device->direct3d);

    {
        unsigned long memory_bytes = v9x_s3_read_video_memory();

        if (memory_bytes != 0ul) {
            v9x_format_u32(number, memory_bytes);
            write("VideoMemoryBytes", number);
            write("VideoMemoryStatus", "valid");
        } else {
            write("VideoMemoryStatus", "unavailable");
        }
    }

    status = v9x_s3_virge_decode_clock_pll(sr10, sr11, &clocks);
    if (status != V9X_STATUS_OK) {
        write("ClockStatus", "unavailable");
        return;
    }
    write("ClockStatus", "valid");
    v9x_format_u32(number, clocks.core_clock_khz);
    write("CoreClockKHz", number);
    v9x_format_u32(number, clocks.memory_clock_khz);
    write("MemoryClockKHz", number);
    write("CoreClockRelation",
          (clocks.flags & V9X_CLOCK_CORE_SHARED_MCLK) != 0u
              ? "shared-memory-clock" : "independent");
}
