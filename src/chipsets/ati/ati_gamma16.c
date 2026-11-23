/*
 * The ati family's gamma hook body, in LEAFCODE (leaf16.h).
 *
 * The family's _TEXT was 43 bytes past its 57,344-byte budget once the
 * gamma hook learned which rows have the Mach64 DAC (2026-11-23,
 * docs\issues\2026-11-09-ati-gamma-hook-writes-mach64-dac-on-evergreen.md),
 * so the body moved here and ati_hw16.c keeps a near stub: the ops table's
 * hook is a near pointer and cannot point across a segment. This module
 * calls nothing.
 *
 * A gamma ramp into the DAC table through DAC_REGS (MM 0_30): byte 0 the
 * write index, byte 1 the data port, red, green, blue per entry. DAC_CNTL
 * (MM 0_31) PALETTE_ACCESS_CNTL, bit 5 on the LT Pro and later, is cleared
 * for the write so the primary table - the one the CRTC scans through - is
 * the one written, and put back after. DAC_8BIT_EN, bit 8, says whether
 * an entry takes eight bits or six; the Rage XL's read 8601010A, eight.
 * Offsets are the HAL's: block 0 at 400h of the register window.
 */
#include "velocity9x/leaf16.h"

#define V9X_ATI_DAC_REGS        0x04c0u
#define V9X_ATI_DAC_DATA        0x04c1u
#define V9X_ATI_DAC_CNTL        0x04c4u
#define V9X_ATI_DAC_PAL_SECOND  0x20u
#define V9X_ATI_DAC_8BIT_HIGH   0x01u   /* bit 8: byte 1 of DAC_CNTL, bit 0 */

unsigned short V9X_LEAF_FAR v9x_ati_load_gamma_leaf(
    volatile unsigned char *registers,
    unsigned long register_bytes,
    const unsigned char *table)
{
    unsigned char access;
    unsigned short shift;
    unsigned short index;

    if (registers == 0 || table == 0 ||
        register_bytes <= (unsigned long)V9X_ATI_DAC_CNTL + 1ul) {
        return 0u;
    }
    access = registers[V9X_ATI_DAC_CNTL];
    shift = (registers[V9X_ATI_DAC_CNTL + 1u] & V9X_ATI_DAC_8BIT_HIGH) != 0u
        ? 0u : 2u;
    if ((access & V9X_ATI_DAC_PAL_SECOND) != 0u) {
        registers[V9X_ATI_DAC_CNTL] =
            (unsigned char)(access & ~V9X_ATI_DAC_PAL_SECOND);
    }
    registers[V9X_ATI_DAC_REGS] = 0u;
    for (index = 0u; index < 256u; ++index) {
        registers[V9X_ATI_DAC_DATA] =
            (unsigned char)(table[index] >> shift);
        registers[V9X_ATI_DAC_DATA] =
            (unsigned char)(table[256u + index] >> shift);
        registers[V9X_ATI_DAC_DATA] =
            (unsigned char)(table[512u + index] >> shift);
    }
    if ((access & V9X_ATI_DAC_PAL_SECOND) != 0u) {
        registers[V9X_ATI_DAC_CNTL] = access;
    }
    return 1u;
}
