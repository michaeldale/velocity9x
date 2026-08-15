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
 */
static unsigned long v9x_s3_detect_video_memory(void)
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
        unsigned long memory_bytes = v9x_s3_detect_video_memory();

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
