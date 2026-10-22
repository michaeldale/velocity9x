# Ironlake read-only MMIO: the GTT is at BAR0 + 2 MiB, and a DOS VM puts the display on the VGA plane

Date: 2026-10-22
Status: **measured, nothing written to the device.** Step 3 of the Order
in `docs\plans\intel-gen4-gen5-bringup.md`, for Gen5, on W9QI2F4F
(remote help `M-J0F0SJ`), running tier-0 `vbe` at `1cc25342` since boot 43
(`2026-10-22-ironlake-w9qi2f4f-tier0-install.md`). Six captures in one
boot. The machine was fine after each.

Captures: `docs\probe\w9qi2f4f-ironlake-gen5-mmio-2026-10-22\`, numbered
in the order taken.

## Instrument

New: `tools\diag\gen5_mmio_probe.asm` (G5MM.VXD) and
`gen5_mmio_probe_win32.c` (V9XG5MM.EXE), built by
`scripts\build-gen5-mmio-probe.ps1`. It is V9XG4MM with Ironlake's layout:

- **The GTT is mapped at GTTMMADR + 2 MiB**, sized from GGC's GGMS field
  on the host bridge (Linux 6.6 `intel-gtt.c`, `i9xx_setup()` case 5, and
  `i965_gtt_total_entries()`). The Ironlake PRM gives neither the BAR
  layout nor GGC (Vol 1 Part 2 3.6.1 defers both to the device specs).
- **1 MiB of MMIO is mapped**, because the PCH display is at C0000h-FFFFFh
  (Vol 3 Part 3). Gen4's windows at 6000h and 61100h are not used: on
  Ironlake the DPLLs, ports and panel registers are in the PCH.
- **47 allowlisted windows**, each citing the PRM page or, where the PRM is
  silent, i915. None can name a pipe's scan line, scan line compare, frame
  or flip counter, time stamp or live-surface register. Two captures, as
  on Gen4: the second reads timings only for pipes and PCH transcoders
  whose CONF has both the enable and the state bit set, and plane
  registers only for enabled planes.
- Device list `0042` and `0046`.

## Stolen memory and the GTT

Capture 3, at the 1024x768x32 desktop:

| | |
|---|---|
| GGC (host 52h) | `0380`: GMS 8 = 128 MiB, GGMS 3 = 2 MiB of GTT |
| BSM (IGD 5Ch) | `C8000000` |
| TOLUD (host B0h) | `D0000000`; BSM + 128 MiB = TOLUD |
| Host A8h | `C7E00000` = BSM - 2 MiB, the size GGMS gives the table |
| GTT | 512 Ki entries at BAR0 + 2 MiB, 409,600 (`0x64000`) valid |
| Entries 0-4095 (the copy) | every one valid, type 0 (uncached), linear from `C8000000`; no 35:32 bits, no MBZ bits |
| PGTBL_CTL (2020h) | `00000000` |

**The GTT is where i915 puts it.** A table of valid entries mapping
stolen memory page by page from BSM is self-identifying; register space
would not read that way. The 965 PTE layout decodes it with nothing left
over.

PGTBL_CTL reads 0. That fits the PRM's statement that the register no
longer holds the table's base. i915 writes the size field there from GGMS;
the BIOS has not.

The copy holds the first 16 MiB of graphics address only, so where the
stolen run ends, and what the other 393,216 valid entries map, is not
recorded. Host A8h is read as the table's stolen base by arithmetic alone:
it is BSM less exactly the table's size, as Ivy Bridge's BGSM is.

## The engine as the BIOS leaves it

The render and BSD rings are off: TAIL, HEAD, START and CTL all `0`,
ACTHD `0`. EIR `0`, ESR `0`, EMR `FFFFFF3F`, IMR `FFFEDFFF`. INSTDONE
`FFFFFFFE`, INSTDONE1 `FFFFFFFF`. MI_MODE `00000200` on both rings.
**HWS_PGA reads `1FFFF000` on both rings**, the same value the GM965's
BIOS left (`2026-10-20-gm965-tommohp-tier0-and-mmio.md`). A driver gives
the status page its own memory either way.

## The display, desktop live

Capture 3, taken with no DOS VM since the last mode set:

| Register | Value | Reading |
|---|---|---|
| VGACNTRL 41000h | `8020298E` | bit 31 set: VGA plane disabled |
| PIPEACONF / PIPEBCONF | `C0000000` / `00000000` | pipe A enabled and running; B off |
| DSPACNTR | `98000000` | plane A on, format `0110` (32-bit BGRX), pipe A |
| DSPASTRIDE / DSPASURF / DSPALINOFF | `1000` / `0` / `0` | 4096-byte pitch at graphics address 0 |
| HTOTAL_A / HSYNC_A | `053F03FF` / `049F0417` | 1024 of 1344; sync 1048-1184 |
| VTOTAL_A / VSYNC_A | `032502FF` / `03080302` | 768 of 806; sync 771-777 |
| PIPEASRC | `03FF02FF` | 1024x768 |
| PF_CTRL_1_A / PF_WIN_SZ_A | `80800000` / `04000300` | fitter on, window 1024x768 (1:1) |
| TRANSACONF | `C0000000` | PCH transcoder A enabled and running |
| TRANS timings A | equal to pipe A's | |
| FDI_RXA_CTL | `B0182050` | FDI receiver A enabled |
| DPLLA_CTRL / FPA0 | `C4100010` / `00010C09` | PCH DPLL A on |
| ADPA E1100h | `80C00000` | CRT port enabled |
| HDMIC / DPC | `0000001C` | bit 2 set; HDMIB, HDMID, DPB, DPD read `18` |

So **the monitor is on the CRT**, through CPU pipe A, FDI A, PCH
transcoder A and the CRT DAC. The timing is the VESA 1024x768 at 60 Hz
geometry. The plane scans out graphics address 0, the start of stolen
memory, at the driver's pitch.

Bit 2 of HDMIC and DPC is i915's "port detected" (`SDVO_DETECTED`), a
strap. Port C is the one HDMI connector the VBT lists
(`2026-10-22-ironlake-w9qi2f4f-audit.md`). Nothing is connected to it in
this capture; that is a reading, not a test.

The BIOS mode set reprograms the PCH DPLL: `DPLLA_CTRL` / `FPA0` read
`C4800080` / `00030D07` in the text-mode state below, and `C4100010` /
`00010C09` here.

## A DOS VM puts the display on the VGA plane

Captures 1 and 2 came first, each after several agent `shell` commands (a
hidden `COMMAND.COM /C`). They showed something else while the driver
reported 1024x768x32, then 800x600x32:

| Register | After a DOS VM | Desktop live |
|---|---|---|
| VGACNTRL | `0020298E` (VGA plane on) | `8020298E` |
| DSPACNTR | `18000000` (plane off) | `98000000` |
| PIPEASRC | `02CF018F`: 720x400 | `03FF02FF` |
| HTOTAL_A / VTOTAL_A | 640 of 800 / 480 of 525 | 1024 of 1344 / 768 of 806 |
| PF_WIN_SZ_A | `028001E0`: 640x480 | `04000300` |

The test, in one boot:

1. `V9XMSW /set:1024x768x32` then V9XG5MM, both by `exec`, no DOS VM:
   desktop live (capture 3).
2. One `shell VER`, then V9XG5MM by `exec`: VGA plane on, plane A off,
   720x400 (capture 4).
3. 20 seconds later, nothing else run: unchanged (capture 5).
4. `V9XMSW /set:800x600x32` then `/set:1024x768x32`: desktop live again
   (capture 6).

**Starting a DOS VM, even a hidden one, switches Ironlake's scanout to the
VGA plane in a 720x400 text geometry through the panel fitter, and it
stays there after the VM exits. A mode set repairs it.** Windows and the
driver see nothing: GDI, V9XGDI and the agent's screenshots read the
framebuffer, which is intact.

This is the netbook's fault, register for register
(`2026-09-24-a-dos-vm-switches-the-netbook-to-the-vga-plane.md`,
`docs\issues\2026-09-12-netbook-dos-box-return-hardlock.md`): VGA plane
enabled, our plane disabled, 720x400 source, panel fitter on. Two things
are new:

- **It happens under the generic `vbe` family**, which carries no Intel
  code at all. So the intel-gma family is not what writes these registers.
  The suspects left are the video BIOS's mode set from the new VM, and how
  the VDD and the `vbe` mini-VDD let it reach the hardware.
- **It happens on Gen5**, three generations after the 945.

What the owner saw on the CRT was not checked in either state; nobody was
asked.

The agent's earlier screenshots of the 4 bpp `vga.drv` desktop in the
audit were noise. Those were taken before any `shell` command in that
session, so this does not explain them.

## Not established

- Which write turns the VGA plane on: the VBIOS's INT 10h in the new VM,
  reaching the hardware through ports the VDD does not trap, is the
  hypothesis that fits both machines. Nothing here observes the write.
- Whether the GM965, Haswell and Ivy Bridge machines do the same. Their
  read-only captures show the VGA plane disabled, but none records whether
  a DOS VM ran before it.
- What the 393,216 valid GTT entries beyond the copy map.

## Next

- Do not use the agent's `shell` verb on this machine when the display
  state matters. Use `exec` with full paths, and `get` for files.
- Step 4 of the plan, identity, GTT, ring and blits for Gen5, can stand on
  this capture. It needs the Gen4 code's differences above: the GTT at
  + 2 MiB sized from GGMS, and the display behind FDI and the PCH.
