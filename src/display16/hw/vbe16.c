/*
 * VBE BIOS services. Ported from V9xSetVbeMode and V9xSetMatroxScanLinePitch
 * in runtime.asm with no behavioural change.
 *
 * INT 10h goes through #pragma aux rather than an assembly helper so each call
 * stays one near function. Only 16-bit registers are touched, which matters:
 * the C here is compiled for 8086 while runtime.asm is .386p, so anything
 * needing a 32-bit register has to stay in assembly.
 */
#include "velocity9x/vbe16.h"

/* 4F02h. Returns AX; 004Fh is success. BX carries the mode plus the family's
 * flags, so the no-clear (8000h) and linear-framebuffer (4000h) bits are data
 * rather than a build-time choice. */
static unsigned short v9x_int10_set_mode(unsigned short ax, unsigned short bx);
#pragma aux v9x_int10_set_mode =        \
    "int 10h"                           \
    parm [ax] [bx]                      \
    value [ax]                          \
    modify [ax bx cx dx si di es]

unsigned short v9x_vbe_set_mode(unsigned short mode, unsigned short mode_flags)
{
    return v9x_int10_set_mode(0x4f02u, (unsigned short)(mode | mode_flags))
               == 0x004fu ? 1u : 0u;
}

/*
 * 4F06h, the scan line length. Registers only, no buffer, so unlike 4F00h and
 * 4F01h these are a plain int 10h from here.
 *
 * BL=1 asks what the card is currently using and BL=0 sets it, in pixels. The
 * reply puts the resulting length in BX as bytes, CX as pixels and DX as the
 * number of scan lines that then fit.
 *
 * These matter because a BIOS can accept a mode set and then scan the surface
 * out at a stride of its own choosing. The mode information from 4F01h does not
 * necessarily say so - it reports what the mode is defined as, not what the
 * CRTC was left programmed with - and a driver that draws at one stride while
 * the card scans at another produces a picture that looks shredded while every
 * check inside the driver agrees with itself. The Millennium II family has
 * always forced this; tier-0 learned it the same way, on a Mach64.
 */
static unsigned short v9x_int10_scan_line(unsigned short ax, unsigned short bx,
                                          unsigned short cx);
#pragma aux v9x_int10_scan_line =       \
    "int 10h"                           \
    "mov v9x_vbe_scan_bytes, bx"        \
    "mov v9x_vbe_scan_pixels, cx"       \
    "mov v9x_vbe_scan_lines, dx"        \
    parm [ax] [bx] [cx]                 \
    value [ax]                          \
    modify [ax bx cx dx]

/*
 * What the last 4F06h call reported. Kept in DGROUP rather than returned
 * through pointers: the pragma above can name these directly, and a far pointer
 * store inside a #pragma aux is more ways to be wrong than this is worth.
 */
unsigned short v9x_vbe_scan_bytes = 0u;
unsigned short v9x_vbe_scan_pixels = 0u;
unsigned short v9x_vbe_scan_lines = 0u;

/* Non-zero on success; the answer lands in v9x_vbe_scan_*. */
unsigned short v9x_vbe_get_scan_line(void)
{
    return v9x_int10_scan_line(0x4f06u, 0x0001u, 0u) == 0x004fu ? 1u : 0u;
}

/* Ask for a stride of exactly this many pixels. Non-zero on success, with what
 * the card settled on in v9x_vbe_scan_* - which is not always what was asked
 * for, so the caller has to look rather than assume. */
unsigned short v9x_vbe_set_scan_line_pixels(unsigned short pixels)
{
    return v9x_int10_scan_line(0x4f06u, 0x0000u, pixels) == 0x004fu ? 1u : 0u;
}

/*
 * Only the mode set lives here, and 4F00h/4F01h deliberately do not.
 *
 * Those two take a buffer in ES:DI, and a plain int 10h from 16-bit protected
 * mode would hand the V86 BIOS a selector to interpret as a paragraph address.
 * Two ring-3 ways round that were built here and both failed on hardware: the
 * DPMI host refuses to allocate a DOS block (function 0100h), and given a
 * buffer that does work, its simulated interrupt (0300h) faults the machine.
 * That code is gone rather than kept behind a flag - it is linked into every
 * family image, and dead code that once bluescreened a guest is not worth
 * carrying.
 *
 * The mini-VDD does those two calls now, at ring 0 during its own init, where
 * the VMM allocates the V86 scratch and nested execution runs the interrupt
 * with no DPMI host involved. enable16.c reaches its cache through the API
 * callers in runtime.asm. See src\minivdd32\loader.asm and D4 in
 * docs\issues\2026-08-16-tier0-defects-deferred.md.
 *
 * VBE 4F06h is not here either, for an unrelated reason: exactly one family
 * uses it and this object is linked into all of them, so its signature here
 * would stop the cross-family audit telling the Millennium II path from the S3
 * one. It lives in that family's own chip module.
 */

