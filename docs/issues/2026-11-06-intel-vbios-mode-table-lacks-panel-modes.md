# Intel video BIOSes lack the panel's native mode, and nothing adds it

Date: 2026-11-06. Feature gap, found in a feature review. Status: open,
nothing written, nothing measured.

## Gap

The intel-gma family sets modes through the video BIOS, so it offers only
the modes in the BIOS's fixed mode table. Recorded consequences:

- the netbook (945GSE) gets 640x480 and 1024x576, not its panel's size
  (docs/STATUS.md);
- TOMMOHP (GM965) stops at 1024x768, scaled to its 1280x800 panel;
- the GM45 reporter's BIOS has no mode for a 1920x1080 monitor
  (decisions/2026-11-05-gm45-gl40-field-report.md);
- an Acer NAV50 whose BIOS lists no 1024x600 would stay at 800x600
  (decisions/2026-08-28-pineview-vbe-mode-list.md).

The VBE 3.0 CRTC block cannot fix this: it carries no active width or
height (same 2026-08-28 record).

## Feature

Rewrite an unused entry of the BIOS mode table in shadow RAM, before the
mode list is read, with the panel's EDID size (the technique Linux's
`915resolution` uses). Parts: 830 through G4x/GM45 and Pineview, which
covers every Gen3/Gen4 part the family claims. The change lasts until reset,
so it would run at every boot, from the mini-VDD or a boot-time step, with
the policy (which entry, which size, which table layout) in host-testable
C and the shadow-RAM write behind the backend.

Open questions: the table layouts per chipset generation, how to find the
table by content and refuse look-alike bytes, how GM45-era BIOSes store
timings, and whether shadow RAM must be unlocked through the host bridge.

## TOMMOHP's table, read offline (2026-11-12)

From the VBIOS image saved on 2026-10-20 (60,928 bytes, kept outside the
repo in `claude/personal/v9x-testers/tommohp-2026-10-20/vbios/`), with
`915resolution`'s search: three 5-byte entries whose mode numbers are 30h,
32h and 34h. One table, at offset 268h. Each entry is mode, bits per pixel,
a 16-bit offset to a resolution block, and one byte.

- Modes 30h-5Ch (the standard 640x480 to 1280x1024 at 8, 16 and 32 bpp)
  point at blocks 136h-244h of 54 bytes: three 18-byte EDID-style detailed
  timing descriptors each (for example 136h: 25.18 MHz, 640 + 144 blank,
  480).
- Modes 60h-71h (VBE 160h-171h) point at six 26-byte blocks, three modes
  (8, 16, 32 bpp) each: an 8-byte header, then one 18-byte descriptor.
  Header bytes 2-3 are the width and 4-5 the height, little-endian.

| Block | Modes | Header | Descriptor |
|---|---|---|---|
| 129Ah | 60h-62h | 1280x768 | 1024x768, 65.00 MHz |
| 12B4h | 63h-65h | **1280x800** | 1024x768, 65.00 MHz |
| 12CEh-131Ch | 66h-71h | 0x0 | zero |

So the BIOS carries a 1280x800 slot for this panel whose descriptor is a
1024x768 placeholder, which is why VBE describes 160h-165h as 1024x768
(the 2026-10-20 record). Writing the panel's own descriptor (EDID
preferred timing: 71.11 MHz, 1280 + 160, 800 + 23) into block 12B4h
before the mode list is read should make VBE 163h-165h set 1280x800. Not
tried: nothing has been written to shadow RAM, and how the GM965's PAM
registers lock C0000h is not checked.

## Acceptance

The netbook offers and sets 1024x600 (its panel), recorded with the BIOS
dump before and after, and no change on a machine whose table is not
recognised.
