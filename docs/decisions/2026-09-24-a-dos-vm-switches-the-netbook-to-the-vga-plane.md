# A DOS VM switches the netbook's display to the VGA plane

2026-09-24, MICHAEL-NETBOOK (945GSE / GMA 950, `8086:27AE`), hard-disk
Win98 SE, v9x-remote-agent 0.6.2 at 10.0.1.254:9869. Driver 0.8.0
`intel-gma`, build `890c828`, boot 2 after a fresh install. Desktop at
1024x576x16. Capture only: nothing here changes the driver.

Issue: `docs\issues\2026-09-12-netbook-dos-box-return-hardlock.md`, whose
2026-09-24 section establishes the trigger - starting any DOS VM, even a
hidden `COMMAND.COM /C VER` from the agent's `shell` verb, leaves a
corrupt panel at 8 and 16 bpp while GDI readback stays correct, and a mode
set repairs it.

## Instrument

`V9XTRACE.EXE` gained two things, both read-only (build `d88213f-dirty`,
deployed as `C:\V9XPKG\V9XTRAC2.EXE`, the packaged tool left in place):

- **`LiveReg00`..`LiveReg23`**: the 24 display-layout registers the HAL
  samples at a flip (`i9xx_scanout.c`, review H4), read by the tool itself
  through the BAR0 window in the engine descriptor, at the moment it runs.
  Every earlier capture of these registers was taken at an enable, a mode
  switch or a flip - the first two are what repair this fault, the third
  needs a DirectDraw session.
- **`-dd`**: holds a `DirectDrawCreate` object open across the snapshot,
  because a mode switch leaves `control_linear_base` zero until the next
  DirectDraw session stamps it (first attempt without it:
  `LiveRegError=no-control-window`). No cooperative level is set, so no mode
  change; the operator confirmed the panel unchanged after the baseline run.

## Sequence

Operator watching the panel throughout.

1. Panel correct. `V9XTRAC2 -dd` via agent `exec` (Win32, no DOS VM) →
   `2026-09-24-netbook-live-display-correct-V9XSNA2.ini`. Panel still
   correct.
2. Agent `shell VER`. Panel corrupt.
3. `V9XTRAC2 -dd` → `2026-09-24-netbook-live-display-after-dos-vm-V9XSNA3.ini`.
   Photograph: `2026-09-24-netbook-panel-after-dos-vm-16bpp.jpg`.

Both runs report `DdHold=open`, `LiveControlBase=0xE6CE1000`, and the
driver's own mode record 1024x576x16 pitch 2048 - Windows believes nothing
changed.

## What changed

Every register that differs between the two captures:

| reg | offset | correct | after DOS VM |
|---|---|---|---|
| VGACNTRL | 0x71400 | `A1C4008E` | `22C4008E` |
| DSPBCNTR | 0x71180 | `95000000` | `15000000` |
| PIPEBSRC | 0x6101C | `03FF023F` | `027F018F` |
| DSPBSIZE | 0x71190 | `023F03FF` | `018F02CF` |
| PFIT_CONTROL | 0x61230 | `00002668` | `80002668` |
| LVDS | 0x61180 | `C0308300` | `C0300300` |
| PIPEB_DSL | 0x71000 | `000000B7` | `00000250` |
| PIPEB_FRAMEHIGH | 0x71040 | `00000082` | `00000000` |

Unchanged: pipe B timings (`HTOTAL 053F03FF`, `VTOTAL 029F023F`, the panel's
1344x672 totals), `PIPEBCONF 80000000`, `DSPBADDR 0`, `DSPBSTRIDE 0x800`, and
all of pipe A and plane A (pipe A disabled, plane A off, in both).

Read against i915's gen3 bit definitions:

- **VGACNTRL bit 31 (VGA display disable) went from set to clear**: the VGA
  plane is on. Bit 29 (VGA on pipe B) is set in both. Bits 24 and 25 also
  differ (`A1` → `22` in the top byte); their meaning is not established
  here.
- **DSPBCNTR bit 31 (plane enable) went from set to clear**: our plane is
  off. The format and pipe-select bits (`15000000`: 16 bpp 565, pipe B)
  are untouched.
- **PIPEBSRC is 640x400 and DSPBSIZE 720x400** - the VGA text geometry -
  with **the panel fitter enabled** to stretch it onto the 1024x576 panel.
- LVDS bit 15 cleared. Not interpreted.
- DSL and FRAMEHIGH are counters; FRAMEHIGH returning to zero is consistent
  with the pipe having been reprogrammed, but that is the weakest reading
  in the table.

## Conclusion

After a DOS VM starts, **the panel is showing the VGA plane in a 720x400
mode through the panel fitter, not plane B and not our framebuffer.** That
is why GDI readback, the agent's screenshots and `V9XGDI /auto` all stay
correct: the framebuffer and plane B's address, stride and format are
intact, and nothing reads the VGA plane. It is why a mode set repairs it:
the VBE set turns the VGA plane off and plane B back on.

The photograph fits it: fine vertical striping at about one line per
1/80 of the panel width, which is a 9-dot character cell of an 80-column
text mode, and a handful of coloured blocks - text cells decoded from memory
that was never a text page. This is a reading of the picture, not a
measurement.

## What this kills

- **Wrong palette.** The desktop's layout is absent from the panel, and our
  plane is disabled.
- **FIFO underrun / watermark.** The plane is not underrunning; it is off.
- **A bad plane stride, base or format.** All three are what they were.
- **GTT or display-TLB staleness.** The panel is not fetching plane B at all.
- **V9XGDI, and depth.** Already killed by the 2026-09-24 issue section;
  this capture is at 16 bpp.

## Not established

- **Who wrote these registers.** The pattern is what a VBIOS INT 10h text
  mode set does to this pipe, which suggests the new VM's video BIOS call
  reached the hardware instead of being virtualised - but no capture here
  observes the write, and the VDD, the mini-VDD and the VBIOS are all
  candidates.
- **Whether a windowed DOS box behaves the same.** Only hidden VMs from the
  agent's `shell` verb were measured, plus the 2026-09-12 report of
  `command` corrupting on start.
- **Why the 0.6.1 `V9XDOSBX` windowed trial "survived".**

The obvious next capture is the same pair with the mini-VDD's VM-creation
callbacks traced, to see which of them runs between the two readings.
