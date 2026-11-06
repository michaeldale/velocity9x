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

## Acceptance

The netbook offers and sets 1024x600 (its panel), recorded with the BIOS
dump before and after, and no change on a machine whose table is not
recognised.
