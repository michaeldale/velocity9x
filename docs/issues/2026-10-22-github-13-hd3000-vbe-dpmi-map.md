# The VBE family's framebuffer mapping is refused on an HD 3000 laptop (GitHub issue 13)

Date: 2026-10-22. Reported by wolfdreamYT: Acer Aspire E1-571, Core
i5-2450M, Intel HD Graphics 3000, Windows 98 SE, the generic VBE family,
"0.25.0" by the report:
[GitHub issue 13](https://github.com/michaeldale/velocity9x/issues/13).
Status: a smaller-mapping retry and a recorded DPMI error are in source,
untested on any machine; the cause of the refusal is unknown.

## Reported

- 256 colours selected in Display Properties produces an error. The
  reporter wants 8 bpp only, for GTA3.
- `V9XBOOT.INI`:
  `Stage=fail-hardware-dpmi-map`, `ApertureStage=post`, `VbeDetail=ok`,
  `Aperture=m=none bar=0 pci=1 b=b0000000`,
  `VbeController=v=0300 mem=2047 caps=00000001 rev=0100`, and twelve
  modes from 640x480x8 to 1366x768x32, all linear at `b0000000`.
- `V9XSYNC.INI`: `Build=0de6b49a` (v0.22.0-35, before 0.23.0), which
  disagrees with the reported version. It may be a stale file.

## Reading of the record

- No Velocity9x family claims Sandy Bridge (0102/0106/0112/0116/0122/
  0126); the intel-gma family's newest parts start at Ivy Bridge. The VBE
  family is the correct path for this machine.
- Stages 2 (mode set), 9 (pitch) and 3 (aperture) passed. Stage 5 is
  `V9XMAPAPERTURE`'s DPMI 0800h: a selector was allocated, then the host
  refused to map 16 MiB (`vbe_hw16.c`'s map size) at `0xB0000000`.
  Nothing chip-specific has failed.
- The largest mode, 1366x768x32 at 5504 bytes a line, needs 4227072
  bytes; the 16 MiB request is mostly DirectDraw heap.
- Whether the record is from the boot or from a live switch to 8 bpp is
  not known. A live switch reuses the boot's selector and should not reach
  stage 5.

## Hypotheses, none tested

- The shared arena is short of linear space: the E1-571 shipped with 4 GB
  or more, which Win98 SE only runs on with a memory patch or
  `MaxPhysPage`, and either shapes the space 0800h maps into.
- The host refuses the address itself. Nothing seen supports this; the
  recorded AX below would separate the two.

## Changed in source (untested)

- `V9XMAPAPERTURE` keeps AX from a refused 0800h in
  `v9x_dpmi_map_error`, and every Enable writes a `DpmiMap` key:
  `<bytes hex>:ok`, or one `<bytes hex>:<AX>` entry per attempt.
- On a stage-5 refusal a tier-0 family (no `read_aperture` hook) maps
  again, halving the request down to the largest committed mode rounded
  up to 64 KiB (`v9x_vbe_map_retry_bytes`, `v9x_vbe_map_floor_bytes`,
  host-tested), and clamps the DirectDraw heap to the mapping granted.
  For this machine's table the ladder is 16 MiB, 8 MiB, 0x410000.
- Native families are not retried: their mapping can cover registers
  past the framebuffer.

## Asked of the reporter

- Installed RAM, and any RAM patch or `MaxPhysPage` in `SYSTEM.INI`.
- Whether Windows starts on Velocity9x at 16 or 32 bpp and fails only on
  the switch to 256 colours.
- The version the settings page reports.
- A boot record from a build with the change above.
