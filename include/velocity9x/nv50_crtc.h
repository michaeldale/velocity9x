/*
 * The GT218's display start, as the VGA CRTC emulation takes it in the
 * modes the VBE sets. Pure arithmetic, shared by the HAL (eng_nv50.c) and
 * the host tests.
 *
 * The VBE's mode leaves the head scanning out through the VGA emulation,
 * not an EVO channel: MEASURED on W9QI0DAD (V9XN5MM /startmove and
 * /starthigh, 2026-11-16, picture captured by OBS) - CR0C/CR0D = 2000h
 * moved the desktop 16 rows at a 2048-byte pitch (the unit is a dword),
 * and CR35 = 01h moved it 128 rows (256 KiB). The register layout is the
 * card's own VBIOS's (VBE 4F07, BL=02h, at C000:6ACE; writer C000:5326):
 * dword bits 15:0 in CR0C (high byte) and CR0D (low byte), 23:16 in CR35,
 * 29:24 in CR34 bits 5:0, with the extended registers unlocked by CR3F =
 * 57h and the old CR3F written back after. RIVA's CR19 and lock CR1F do
 * nothing on this chip (read 0, the picture did not move).
 */
#ifndef VELOCITY9X_NV50_CRTC_H
#define VELOCITY9X_NV50_CRTC_H

#include "velocity9x/types.h"
#include "velocity9x/status.h"

#define V9X_NV50_CR_START_HIGH   0x0cu
#define V9X_NV50_CR_START_LOW    0x0du
#define V9X_NV50_CR_START_TOP    0x34u
#define V9X_NV50_CR_START_MID    0x35u
#define V9X_NV50_CR_LOCK         0x3fu
#define V9X_NV50_CR_UNLOCK       0x57u
/* CR34's start bits; the VBIOS keeps bits 7:6 clear. */
#define V9X_NV50_CR34_START_MASK 0x3fu
/* 30 bits of dwords. */
#define V9X_NV50_CRTC_START_LIMIT 0x40000000ul

struct v9x_nv50_crtc_start {
    v9x_u8 cr0c;
    v9x_u8 cr0d;
    v9x_u8 cr35;
    v9x_u8 cr34;
};

/*
 * The four registers for a scanout at byte_offset into video memory.
 * Refused: a null start, an offset not on a dword (the attribute
 * controller's pan register, index 33h in the VBIOS, is not used), or
 * one at or past vram_bytes.
 */
v9x_status v9x_nv50_build_crtc_start(v9x_u32 byte_offset, v9x_u32 vram_bytes,
                                     struct v9x_nv50_crtc_start *start);

#endif
