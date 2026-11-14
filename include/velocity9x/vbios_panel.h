/*
 * A panel's own mode in an Intel video BIOS's mode table.
 *
 * Intel mobile BIOSes carry mode-table slots for the panel - VBE 160h and up
 * - whose header names the panel's size but whose timing an OEM may leave as
 * a placeholder. TOMMOHP's GM965 BIOS heads one slot 1280x800 and fills it
 * with 1024x768 at 65 MHz, so VBE describes the panel's own mode as
 * 1024x768 and the desktop is scaled
 * (docs\issues\2026-11-06-intel-vbios-mode-table-lacks-panel-modes.md).
 * Writing the panel's EDID detailed timing into that slot in shadow RAM, as
 * Linux's 915resolution does, makes the BIOS set the real mode.
 *
 * Split as the MTRR decision is (velocity9x\mtrr.h): this file reads a ROM
 * image and an EDID block and decides; it does no I/O. The 16-bit driver runs
 * it at Enable and stores the plan in SYSTEM.INI; the mini-VDD applies it at
 * the next Device_Init, before it reads the mode list, and only while the
 * bytes it would replace are still the ones planned against.
 */
#ifndef VELOCITY9X_VBIOS_PANEL_H
#define VELOCITY9X_VBIOS_PANEL_H

#include "velocity9x/types.h"
#include "velocity9x/leaf16.h"

/* An EDID detailed timing descriptor, and the slot's copy of one. */
#define V9X_VBIOS_DTD_BYTES ((v9x_u16)18u)

/*
 * The mode table, as 915resolution finds it: 5-byte entries of mode number,
 * bits per pixel, a 16-bit offset to a resolution block and one byte, ended
 * by mode FFh, recognised by three entries numbered 30h, 32h and 34h.
 */
#define V9X_VBIOS_MODE_ENTRY_BYTES ((v9x_u16)5u)
#define V9X_VBIOS_MODE_END ((v9x_u8)0xFFu)
#define V9X_VBIOS_MODE_TABLE_MAX ((v9x_u16)96u)

/*
 * Panel slots: modes 60h and up, each pointing at a block of an 8-byte header
 * (width at 2, height at 4, little-endian) and one descriptor at 8. Measured
 * on TOMMOHP's BIOS only; the other bytes of the header are not understood.
 */
#define V9X_VBIOS_PANEL_MODE_FIRST ((v9x_u8)0x60u)
#define V9X_VBIOS_PANEL_HEADER_BYTES ((v9x_u16)8u)
#define V9X_VBIOS_PANEL_BLOCK_BYTES \
    ((v9x_u16)(V9X_VBIOS_PANEL_HEADER_BYTES + V9X_VBIOS_DTD_BYTES))

/*
 * Shadow RAM for C0000h-CFFFFh is unlocked through the host bridge's PAM
 * registers, device 0:0.0: PAM1 (91h) covers C0000h-C7FFFh and PAM2 (92h)
 * C8000h-CFFFFh, each byte two 16 KiB halves, bits 1:0 the low and 5:4 the
 * high, 11b read-write (945 datasheet 307502 and 965 datasheet 313053,
 * "PAM0-PAM6"; the G45 keeps them at 90h-96h too). The caller decides which
 * chipsets that holds for.
 */
#define V9X_VBIOS_PAM1 ((v9x_u8)0x91u)
#define V9X_VBIOS_PAM_LOW_RW ((v9x_u8)0x03u)
#define V9X_VBIOS_PAM_HIGH_RW ((v9x_u8)0x30u)
#define V9X_VBIOS_PAM_HALF_BYTES ((v9x_u16)0x4000u)

/* What v9x_vbios_panel_plan found. */
#define V9X_VBIOS_PANEL_PLANNED  ((v9x_u16)1u)  /* a patch to apply */
#define V9X_VBIOS_PANEL_ALREADY  ((v9x_u16)2u)  /* the slot holds it already */
#define V9X_VBIOS_PANEL_NO_EDID  ((v9x_u16)3u)  /* no usable preferred timing */
#define V9X_VBIOS_PANEL_NOT_ROM  ((v9x_u16)4u)  /* no 55AAh, or no length */
#define V9X_VBIOS_PANEL_NO_TABLE ((v9x_u16)5u)
#define V9X_VBIOS_PANEL_TABLES   ((v9x_u16)6u)  /* more than one table */
#define V9X_VBIOS_PANEL_NO_SLOT  ((v9x_u16)7u)  /* no slot headed that size */
#define V9X_VBIOS_PANEL_SLOTS    ((v9x_u16)8u)  /* two blocks headed that size */
#define V9X_VBIOS_PANEL_SPLIT    ((v9x_u16)9u)  /* crosses a 16 KiB PAM half */

struct v9x_vbios_patch {
    v9x_u16 rom_offset;     /* of the descriptor, from C0000h */
    v9x_u8 pam_register;    /* 0:0.0 configuration offset */
    v9x_u8 pam_rw;          /* the bits that open that half read-write */
    v9x_u16 width;          /* the panel's, for the boot record */
    v9x_u16 height;
    v9x_u8 old_bytes[18];   /* what the slot holds now */
    v9x_u8 new_bytes[18];   /* the panel's descriptor */
};

/*
 * Plan the patch from the shadowed ROM (rom_bytes readable from rom[0]) and
 * a 128-byte EDID base block. Returns a V9X_VBIOS_PANEL_ code; the patch is
 * filled for PLANNED and ALREADY, zeroed otherwise.
 */
v9x_u16 V9X_LEAF_FAR v9x_vbios_panel_plan(const v9x_u8 *rom,
                                          v9x_u16 rom_bytes,
                                          const v9x_u8 *edid,
                                          struct v9x_vbios_patch *patch);

/*
 * The SYSTEM.INI record the mini-VDD parses, fixed width so the assembly
 * reads it by position: "OOOO PP MM " then the old and the new descriptor as
 * 36 hex digits each, separated by a space. Upper-case hex.
 */
#define V9X_VBIOS_PATCH_TEXT_BYTES ((v9x_u16)85u)
#define V9X_VBIOS_PATCH_SECTION "Velocity9x"
#define V9X_VBIOS_PATCH_KEY "VbiosPanelPatch"

void V9X_LEAF_FAR v9x_vbios_panel_format(const struct v9x_vbios_patch *patch,
                                         char *text);

#endif
