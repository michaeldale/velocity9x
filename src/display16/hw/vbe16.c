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
 * Only the mode set lives here.
 *
 * VBE 4F06h is used by exactly one family, and this object is linked into all
 * of them. Putting it here would place its signature in every binary and make
 * the cross-family audit unable to tell the Millennium II path from the S3
 * one, so it lives in that family's own chip module instead.
 */
