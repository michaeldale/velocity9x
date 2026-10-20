# GM965 on TOMMOHP: Windows 98 runs, tier-0 enable-ok, and the Gen4 GTT is where the datasheet says

Date: 2026-10-20
Status: **measured once each, no DOS, nothing written to the device.**
Steps 1-3 of the Order in `docs\plans\intel-gen4-gen5-bringup.md`: a
Windows 98 SE install on Gen4, the `vbe` family on it, and the read-only
Gen4 probe. The probe answers every Phase 1 question the plan asks of
Gen4.

Captures: `docs\probe\tommohp-gm965-tier0-2026-10-20\`.

| | |
|---|---|
| Machine | Tom Rossell's TOMMOHP, remote help `M-KYHT4Q` (agent 0.12.0) |
| Board | HP, SUBSYS `103C:30C0`; system BIOS "Compaq" 12/01/11, KBC 71.2E |
| CPU | Core 2 Duo T7250, 2.0 GHz |
| Memory | 2 GiB; Windows reports 2,135,052,288 bytes |
| IGD | `8086:2A02` rev `0C` (GM965, GMA X3100); function 1 `2A03` |
| Host bridge | `8086:2A00` rev `0C` |
| VBIOS | Intel "Crestline PCI Accelerated SVGA BIOS", build 1585; VBT signature `$VBT CRESTLINE`, BDB version 140 |
| Panel | 1280x800 LVDS (EDID preferred mode and VBT panel type 2) |
| OS | Windows 98 4.10.2222 A (Second Edition), DirectX 4.09.00.0904 |
| Display driver before | "Modern SVGA 1024x768 16M Large" (Windows 3.x driver) on both functions |

The plan's first gate - does Windows 98 install and boot on a Gen4 or
Gen5 machine at all - is passed for Gen4 by this machine. The owner had
already installed it.

## Config space (V9XHSW.EXE, `V9XHSW.INI`)

The tool's `[Decode]` section is the Haswell layout and does not apply.
The raw words, against datasheet 316273:

| Register | Value | Meaning |
|---|---|---|
| GGC (host 52h) / MGGC (IGD 52h) | `0030` / `0030` | GMS 3 = 8 MiB stolen; IVD 0 |
| BSM (IGD 5Ch) | `7F800000` | TOLUD (`B0h` = `8000`, 2 GiB) less 8 MiB |
| GTTMMADR (IGD 10h) | `E4200004` / `00000000` | 64-bit, 1 MiB |
| GMADR (IGD 18h) | `D000000C` / `00000000` | 64-bit prefetchable |
| BAR4 IOBAR | `00004001` | I/O at 4000h |
| MSAC (IGD 66h) | `02` | 256 MiB aperture |
| GCFGC (IGD F0h) | `0205` | not decoded |
| ASLS (IGD FCh) | `7F7C5400` | OpRegion present |

This is the topology the X61 survey showed: no GTTADR BAR, BAR0 and BAR2
each two dwords. GGC is the 945 register with the 945 table, as the
datasheet says.

## Tier-0

`build\win98se-vbe` at `48ba167f`, pushed to `C:\V9XPKG` and installed
through Device Manager, Update Driver, Have Disk, on function 0. Function 0
is the entry whose Resources tab shows the VGA ranges. Setup's
`WININIT.INI` held two plain `dest=src` renames for the Glide DLLs and no
`NUL=` line. One agent `reboot`: `BootCounter` 4 to 5, reconnected, desktop
ready at 640x480x8.

- **`Stage=enable-ok`.** The desktop draws (`desktop-after-install.png`).
- **VBE 3.0, `mem=119`: 7,798,784 bytes, `0x770000`.** 8 MiB stolen less
  576 KiB. Where the 576 KiB goes is answered by the GTT below.
- **36 modes listed, 36 queried, 15 described.** Nothing above 1024x768.
  `0160`-`0165` duplicate the standard 1024x768 modes. 800x600x8 has an
  832-byte pitch, as on every Intel BIOS measured so far.
- **EDID** v1.3, preferred 1280x800. The BIOS describes no 1280x800 mode,
  so the driver recommends it (`reason=edid-unpublished`) without being
  able to set it.
- MTRRs: 8 variable ranges, none on the framebuffer, `r=0`.

## The Gen4 probe (V9XG4MM.EXE + G4MM.VXD, `V9XG4MM.INI`)

New with this record: `tools\diag\gen4_mmio_probe.asm` and
`gen4_mmio_probe_win32.c`, built by `scripts\build-gen4-mmio-probe.ps1`.
It is the Haswell probe's design reduced to reads. It maps BAR0's 512 KiB
of MMIO and the GTT at BAR0 + 512 KiB, sized from PGTBL_CTL. Offsets
outside an allowlist are refused, and the allowlist cannot name a pipe's
line or frame counter. It takes two captures. The second reads timings,
DPLL and plane registers only for pipes and planes the first found
enabled, under the 945 rule
(`2026-10-15-a-disabled-pipes-counters-hang-the-945.md`). The device list
is 965-family only; a G45 keeps its GTT at +2 MiB. Run twice; the
machine was fine both times.

### The GTT

| | |
|---|---|
| PGTBL_CTL (2020h) | `7FF80001`: enabled, size code 0 = 512 KiB, base `7FF80000`, 35:32 zero |
| Table | 128 Ki entries, 512 MiB of graphics address; 98,304 (`0x18000`) valid |
| Placement | **inside stolen, at +`0x780000`: the last 512 KiB** |
| Entries `0`-`0x77E` | linear over stolen from BSM, `7F800000`-`7FF7E000` |
| Entries `0x77F` on (in the 4096 copied) | all `7FF7E001`: the page at `0x77E` repeated, 2,177 times |
| Mapping type | 0 (uncached) on every valid entry; no 35:32 extension, no MBZ bits |

So stolen memory is: 0 to `0x77F000` mapped (with `0x77E000` doubling as
the fill page for the rest of the table), one page at `0x77F000` mapped
by nothing, and the GTT in the top 512 KiB. VBE's `0x770000` stops 60 KiB
short of the end of the mapping.

Against the research notes (`claude/personal/intel driver research/
gm965-research-notes.md`):

- **GTT at BAR0 + 512 KiB:** confirmed by reading a table of valid,
  linear entries there.
- **Size from PGTBL_CTL, and 512 KiB:** confirmed. The Gen3 code's
  256 KiB (`V9X_I9XX_GTT_BYTES`) is wrong here.
- **PTE layout:** every entry reads as the 965 PRM layout with the
  extension zero. The Gen3 `known_attributes` set (0, V, V|LOCAL,
  V|SYSTEM_CACHED) would accept this table as it stands. That says nothing
  about what an encoder may write: `LOCAL` is reserved on Gen4.
- **Stolen memory decode:** GGC's 8 MiB agrees with BSM, TOLUD and the
  GTT's place at the top of stolen. Two independent numbers agree.

### The display

| Register | Value | Reading |
|---|---|---|
| PIPEACONF / PIPEBCONF | `00000000` / `C0000000` | pipe A off; pipe B enabled, active |
| DSPACNTR / DSPBCNTR | `00000000` / `C9000000` | plane A off; plane B on, gamma, 8 bpp indexed, pipe B |
| DSPBSTRIDE / DSPBSURF / DSPBLINOFF / DSPBTILEOFF | `280` / `0` / `0` / `0` | 640-byte pitch at graphics address 0 |
| HTOTAL_B / HSYNC_B | `059F04FF` / `054F052F` | 1280 active, 1440 total; sync 1328-1360 |
| VTOTAL_B / VSYNC_B | `0336031F` / `03280322` | 800 active, 823 total; sync 803-809 |
| PIPEBSRC | `027F01DF` | **640x480 source** |
| PFIT_CONTROL | `A8000000` | enabled, pipe B, scaling mode 2 = pillarbox |
| LVDS | `C2308300` | port on, pipe B |
| ADPA | `40008C18` | CRT off |
| PP_STATUS / PP_CONTROL | `C0000008` / `00000003` | panel on |
| BLC_PWM_CTL | `90A790A7` | duty = maximum |
| DPLL_B / FPB0 | `98046A00` / `00021407` | see below |
| VGACNTRL | `A2C4008E` | VGA plane disabled |

**The BIOS runs the panel at native timing and scales the VBE mode with
the panel fitter.** Pipe B's timing equals the VBT's panel-2 DTD field
for field: 71.11 MHz, 1280+160, 800+23, the same sync positions. Only
PIPEBSRC and the plane carry the mode. So a native 1280x800 desktop needs
no DPLL programming: a source size, a stride and the fitter off, on a
pipe the BIOS already clocks. That is a hypothesis about the hardware,
not a measurement; nothing has been written.

The clock, decoded with i915's i9xx formula (`intel_dpll.c`): N 2, M1 20,
M2 7, so m = 5(20+2)+(7+2) = 119; P1 one-hot `04` = 3; LVDS P2 14 (bit 24
clear); reference select `3` = SSC. The VBT's general-features block has
`enable_ssc` 1 and the alternate SSC frequency bit 1, which on Gen4 is
100 MHz (`intel_bios_ssc_frequency`). 100 x 119 / 4 / 42 = 70.83 MHz,
59.8 Hz - the closest this PLL reaches to the DTD's 71.11 MHz.

### The engine, as the BIOS leaves it

The ring is off: PRB0 START, HEAD, TAIL, CTL all `0`. ACTHD (2074h) `0`.
EIR `0`, ESR `0`, EMR `FFFFFFDF`. INSTDONE `FFE5FAFE`, INSTDONE1
`000FFFFF`. MI_MODE `00000200`. **HWS_PGA reads `1FFFF000`**, a physical
page at 512 MiB less 4 KiB, in ordinary RAM. Whether the BIOS set it and
what it holds are not known; a driver gives HWS_PGA its own page.
FW_BLC, FW_BLC2 and FW_BLC_SELF are `0`. DSPARB `00001D9C`, DSPFW1-3
`3F8F0F0F`, `00000F0F`, `0`. MI_ARB_STATE `00000044`. Clock gating:
DSPCLK_GATE_D `00001000`, everything else `0`, which is not what i915's
`i965gm_init_clock_gating` would write (RENCLK_GATE_D1 =
`I965_RCC_CLOCK_GATE_DISABLE`). Whether that matters before the 3D
engine runs is open.

## What this does not answer

- Whether the Gen3 ring, MI and blit commands run here. The ACTHD move
  and the batch-buffer security bit (bit 8 of the header on Gen4) are
  documented differences; none has executed.
- Whether a DSPSURF write flips, and whether the 128 KiB linear alignment
  the PRM states is enforced.
- Why the 4096-entry copy has 2,177 repeats of the fill page while the
  whole table has 98,304 valid entries. The copy covers 16 MiB; the rest
  of the table was counted, not looked at.

## Next

Step 4 of the plan's Order: identity, GTT, ring and blits in the
`intel-gma` family, against the differences above. Each first write gets
its own decision record, as Gen3's did.
