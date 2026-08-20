/*
 * Shared S3 scanout controls for V9XHAL.DLL.
 *
 * The CRTC index/data pair, the display-start registers and the input-status
 * vblank bit are the same on every S3 part this driver drives, and on a great
 * many VGA-compatible parts besides. They are chip-agnostic in the way the
 * drawing engines are not, so they sit apart from both engine modules rather
 * than being duplicated into each or arbitrarily assigned to one.
 */
#include "ddhal_internal.h"

unsigned char v9x_read_crtc(unsigned char index)
{
    v9x_outp(V9X_CRTC_INDEX, index);
    return v9x_inp(V9X_CRTC_DATA);
}

void v9x_write_crtc(unsigned char index, unsigned char value)
{
    v9x_outp(V9X_CRTC_INDEX, index);
    v9x_outp(V9X_CRTC_DATA, value);
}

int v9x_in_vblank(void)
{
    return (v9x_inp(V9X_INPUT_STATUS_1) & V9X_STATUS_VBLANK) != 0u;
}

/*
 * Program the S3 display start address. The offset is expressed in
 * doublewords: CR0D holds bits 7:0, CR0C bits 15:8, and the low nibble of
 * CR69 bits 19:16 (the high nibble must be preserved).
 *
 * Returns 0 without touching the CRTC when the offset is not a whole number of
 * doublewords. The registers cannot express such an address: the shift below
 * would silently round the scanout down by one or two bytes, shifting every
 * pixel on the screen and tinting the whole frame. It cannot arise at 8, 16 or
 * 32 bpp, where the pixel size divides 4, but a 24-bpp surface can start on any
 * byte. Refusing lets the caller decline the flip instead of presenting a
 * corrupt frame and reporting success.
 */
int v9x_set_display_start(DWORD byte_offset)
{
    DWORD start = byte_offset >> 2;
    unsigned char extension;

    if ((byte_offset & 3ul) != 0ul) {
        return 0;
    }
    v9x_write_crtc(0x0du, (unsigned char)(start & 0xfful));
    v9x_write_crtc(0x0cu, (unsigned char)((start >> 8) & 0xfful));
    extension = v9x_read_crtc(0x69u);
    extension = (unsigned char)((extension & 0xf0u) |
                                (unsigned char)((start >> 16) & 0x0ful));
    v9x_write_crtc(0x69u, extension);
    return 1;
}

