/*
 * S3 register access shared by the S3 family binaries (src\chipsets\s3\common).
 *
 * Function declarations only. The S3 engine register constants that
 * docs\plans\gdi-acceleration.md needs land separately as
 * include\velocity9x\regs\s3.h.
 */
#ifndef VELOCITY9X_S3_REGS16_H
#define VELOCITY9X_S3_REGS16_H

#include "velocity9x/hw16.h"

/* Decimal text for an unsigned 32-bit value. text must hold 11 bytes. */
void v9x_format_u32(char *text, unsigned long value);

/* Publishes the shared S3 C:\V9XHW.INI block for one chip. Reads SR10/SR11
 * for the PLL and CR36 for installed memory. */
void v9x_s3_publish_diagnostics(const V9X_HW16_DEVICE *device,
                                v9x_hw16_write_fn write);

#endif /* VELOCITY9X_S3_REGS16_H */
