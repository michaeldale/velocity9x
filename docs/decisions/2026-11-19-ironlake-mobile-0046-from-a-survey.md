# Ironlake mobile HD Graphics (8086:0046, Acer Aspire 4740) joins intel-gma on one DOS survey, as an alias of the desktop 0042

Date: 2026-11-19
Machine: no Ironlake mobile (Arrandale) is in the fleet and nothing below has
run on one. The desktop Ironlake path last ran on W9QI2F4F, which holds a
Quadro FX 550 since 2026-11-05.
Evidence: the V9XSURV report attached to GitHub issue 28 by its reporter (an
Acer Aspire 4740), committed as
[`acer-aspire-4740-ironlake-m-vgasurv-2026-11-18-b0a945a5.ini`](../probe/references/acer-aspire-4740-ironlake-m-vgasurv-2026-11-18-b0a945a5.ini)
with its 4,000 `Rom.` lines removed (the video BIOS image is Intel's code);
everything else is as sent. Survey build `b0a945a5` (0.56.0), run from DOS
7.10 with `/rom`.

## Why

The desktop 0042 has been in `intel-gma` since 0.32.0: DirectDraw on the
render ring, flips on the live plane, Direct3D on the Gen4 engine with Gen5
encodings, and the Direct3D, OpenGL and Glide 2 probes passed on W9QI2F4F.
Arrandale's IGD is the same Gen5 graphics as Clarkdale's, packaged with a
mobile CPU. An alias costs one descriptor and the id lists, as the 0166 did
for the HD 4000 (`aa70a3a9`), and the reporter's first boot is the only
measurement available.

## What the survey establishes

| | Aspire 4740 (0046) | W9QI2F4F (0042) |
|---|---|---|
| Function 0 | `8086:0046` rev 02, subsystem `1025:0318` | `8086:0042` |
| Host bridge | `8086:0044` | `8086:0040` |
| BAR0 (GTTMMADR) | `B0000004`, 64-bit, high dword 0 | 64-bit |
| BAR2 (GMADR) | `A000000C`, 64-bit prefetchable, high dword 0 | 64-bit prefetchable |
| BAR4 | `3051` (I/O) | |
| GGC (52h) | `0380`: GMS 8, GGMS 3 | `0380` |
| VBE | 3.0, TotalMemory64K 2047 (128 MiB less 64 KiB) | 128 MiB reported |
| VBE OEM string | "Intel(R)Ironlake Mobile Graphics Chipset Accelerated VGA BIOS" | |
| LFB (every described mode) | `A0000000`, BAR2's base | in BAR2 |

- The BARs have the form `V9xPciReadIntelGttConfig`'s Gen4 branch validates
  (both 64-bit, zero high dwords, BAR2 256 MiB aligned), and BAR0 is 4 MiB
  aligned as the + 2 MiB GTT branch requires. `0046` joins `0042` at both
  comparisons there.
- GGC is the desktop's value exactly, so `intel_gtt16.c`'s Ironlake handling
  of GMS 8 (the BIOS's 128 MiB, clamped to the 16 MiB the driver maps)
  applies unchanged. The GTT's place (BAR0 + 2 MiB) is taken from the desktop
  measurement and Linux `intel-gtt.c`, which treats both ids alike; it was not
  read on this machine.
- The descriptor is the desktop's: `v9x_ilk_fill_engine`, framebuffer BAR 2,
  write-combined.

## The panel

EDID (DDC level 1, one block): a BOE panel, `HB140WX1-200`, preferred
timing 1366x768 at 70.70 MHz. VBE describes nine modes, 640x480, 800x600 and
1024x768 at 8, 16 and 32 bpp (`0101 0103 0105 0111 0112 0114 0115 0117
0118`), and lists 27 more whose
mode information returns attributes 0, among them `0160`-`0171`, the slots
Intel mobile BIOSes keep for the panel. So VBE offers no 1366x768, and this
driver, which publishes the modes its VBE scan finds, will offer the same
three sizes the reporter's VBE driver does: the desktop scaled on the panel.

The reporter makes 1366x768 appear by patching the BIOS at each boot. This
driver's own panel patch (`vbios_panel.c`, `intel_panel16.c`) runs on the
GM965 only. It rewrites the timing of a slot whose header already names the
panel's size; whether this BIOS's undescribed slots can be filled the same
way, and where Arrandale's shadow-RAM PAM registers are (they are not in
the 945/965 host bridge's 90h-96h on every Nehalem-era part), are not known.
Left open.

The VBIOS checksum residue (`03`) matches the GM45 survey's and is common on
Intel IGD BIOSes whose image is patched at POST; nothing here depends on it.

## What a first boot should show

- `V9XHW.INI`: `Intel HD Graphics (Ironlake mobile)`, engine Gen3 type with
  the I9XX_GEN5 cap.
- `INTELGTT.TXT`: a GTT read at BAR0 + 2 MiB, entries linear from BSM. A
  `CONFIG-FAILED` is the first place to look.
- Flips: the panel is most likely on pipe B. `i9xx_scanout.c` reads which
  pipe is enabled and which plane feeds it, and flips that plane; on the
  desktop the CRT was on pipe A, so the pipe B case has run on the GMA 950
  and GM965 only, never on Gen5.

## Not checked

Everything at run time. The release that carries this was checked on other
`intel-gma` machines, which exercises the shared code but not this id.
