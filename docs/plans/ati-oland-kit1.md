# Radeon R7 240 test kit 1: a read-only register snapshot

For an owner of an AMD Radeon R7 240 (or another Oland card) running
Windows 98, who is helping without remote access. The plan this serves is
[ati-oland-gfx6.md](ati-oland-gfx6.md).

## What it does, and what it does not

`V9XSIMM.EXE` finds the card on the PCI bus and reads about 180 of its
registers, each twice, through a small driver (`SIMM.VXD`) that loads while
the program runs and unloads when it exits. It **writes nothing to the
card** and installs nothing. It writes one report file:
`C:\V9XDIAG\V9XSIMM.INI`.

It has not yet been run on an R7 240. If it fails, the report says how.
A smoke test on an emulated ATI Mach64 (2026-11-12, below) showed that
the program starts, loads its driver and refuses a card that is not an
Oland, without trouble.

## Before you start

- Windows 98 or 98 SE booting normally on the R7 240. Any display driver
  will do. The Velocity9x `vbe` package is preferred: its own report files
  (below) are then part of the kit.
- The R7 240 is the card the monitor is connected to.

## Steps

1. Make a folder, for example `C:\V9XSIMM`, and copy both files into it:
   `V9XSIMM.EXE` and `SIMM.VXD`. They must be in the same folder.
2. Open the folder in Explorer and double-click `V9XSIMM.EXE`. No window
   appears; it finishes in under a second.
3. Open `C:\V9XDIAG\V9XSIMM.INI` in Notepad and check that the `[Report]`
   section has `Result=COMPLETE`.

## What to send back

- `C:\V9XDIAG\V9XSIMM.INI`, whatever its `Result=` says.
- If the Velocity9x `vbe` package is installed: `C:\V9XDIAG\V9XBOOT.INI`,
  `C:\V9XDIAG\V9XHW.INI` and `C:\V9XDIAG\V9XMODES.INI`.
- The card's make and model as printed on the box or the card, and how much
  memory it has.

## If it goes wrong

- `Result=NO-OLAND`: the card's PCI id is not one Linux lists for an Oland.
  Send the file anyway, and the id Device Manager shows for the display
  adapter (Properties, then the Details or Resources tab).
- `Result=VXD-LOAD-FAILED`: `SIMM.VXD` was not in the same folder as the
  program, or the program was started from a different working folder. Run
  it by double-clicking it in the folder that holds both files.
- Any other `Result=`, or none: send the file; the `Status=` line says which
  check refused the card.
- The machine stops responding: a read-only probe is not expected to do
  this. Power it off, start it again, and tell us; nothing was changed on
  disk except the report.

## For the maintainers: reading the report

`[Decode]` answers the plan's open questions directly:
`Bar0AtVramStart` (the ring window's assumption), `VmContext0Enabled`
(the DMA engine's MC addresses), `Dma0HeldInReset`/`Dma0RingEnabled` (the
state the BIOS left), `GbAddrConfig` and `CcGcShaderArrayConfig` (the GPU's
shape, for gpu_init), and `MemoryMiB`/`MemoryGddr5`. `[D1]` shows what the
VBE mode set programmed. A result goes into a dated
`docs/decisions/` record with the INI files, as every register probe has.

### Smoke test, 2026-11-12

On the 86Box guest `Win98SE-Mach64VT2` (port 9873, boot 158, agent
0.16.0, an emulated Mach64 VT2 `1002:5654` on the ati driver), from
`ae7a5a20-dirty` (the same probe source as `cf2600ef`). Captures in
[`docs/probe/oland-simm-smoke-2026-11-12/`](../probe/oland-simm-smoke-2026-11-12/).

- `V9XSIMM.EXE` exited 1 with `Result=NO-OLAND`: the scan does not take a
  non-Oland ATI card.
- A scratch copy of the publisher that takes any ATI device (not in the
  tree: the scan's id check removed, the report renamed `V9XSIMMX.INI`)
  handed the Mach64's address `80006000` to `SIMM.VXD`. The VxD loaded
  from the program's folder, accepted all 176 requested offsets (no
  `RefusedOffset`, status bit 10h clear), read the id through CF8h, and
  returned `Status=00000081` (ATI, not an Oland) in a full-size result
  with the right magic. Exit 2, as designed.
- The guest stayed up and shut down cleanly afterwards.

Not covered, because no emulator has an Oland: the BAR2 checks, the
mapping, the `CONFIG_MEMSIZE` identity check, the register reads and the
whole `[Decode]` section.

Build the kit with `scripts\build-si-mmio-probe.ps1`; the two files are in
`build\si-mmio-probe\` (`simm.vxd`, `v9xsimm.exe`), to be renamed in
capitals for the owner.
