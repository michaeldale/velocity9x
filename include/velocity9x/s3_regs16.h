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

/*
 * Read the linear aperture base from CR59/CR5A.
 *
 * The S3 sample driver reads these rather than PCI BAR0, and the value is
 * range-checked before use: a base below 16 MiB or above FFC00000h is a read
 * that went wrong, not a card in an unusual slot. Returns 0 on refusal.
 */
unsigned long v9x_s3_read_aperture(void);

/*
 * Installed video memory in bytes from CR36, or 0 when the size code is one
 * this driver does not decode.
 *
 * This is the family's read_video_memory hook as well as the source of the
 * diagnostics figure. Both S3 chips encode the size the same way, so the
 * reading is shared; what differs between a 2 MiB and a 4 MiB card is the
 * answer, not the register.
 */
unsigned long v9x_s3_read_video_memory(void);

/*
 * Unlock the extended registers, select the 4 MiB aperture in CR58[1:0] with
 * linear addressing in CR58[4], and enable the graphics engine in CR40[0].
 *
 * This is the part both S3 families share. The ViRGE's CR53[3] new-MMIO
 * window is not here: it belongs to the ViRGE alone, and putting it in this
 * shared object would leak the signature into the Trio binary.
 *
 * Non-zero on success; the CRTC index port is left selected for a caller that
 * wants to follow up, as v9x_s3_virge_enable_new_mmio does.
 */
unsigned short v9x_s3_enable_linear_aperture(void);

/*
 * Identify an S3 from CR2D/CR2E when the PCI scan found nothing.
 *
 * The family's identify_without_pci hook. CR2D/CR2E carry the same device id
 * S3 publishes to PCI configuration space, so this reads the identity off the
 * chip and matches it against the family's own device list, setting
 * v9x_pci_match on success. Returns the matched index, or 0xFFFF.
 *
 * Reads only - no unlock, no writes. Measured on a 486 VLB Trio64 where the
 * video BIOS leaves the extended bank locked and the id reads true anyway,
 * because on these parts the CR38/CR39 locks gate writes rather than reads.
 */
unsigned short v9x_s3_identify_without_pci(void);

/* CRTC access for a chip module following up on the shared sequence. */
unsigned char v9x_s3_crtc_read(unsigned char index);
void v9x_s3_crtc_write(unsigned char index, unsigned char value);

#endif /* VELOCITY9X_S3_REGS16_H */
