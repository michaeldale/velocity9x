# GeForce2 Go (NV11) in a Dell Inspiron 2650 from a VOGONS survey: plain VBE 3.0, no family covers it, and the report cannot explain the stutter

Date: 2026-11-04
Machine: none in the fleet. A Dell Inspiron 2650 surveyed by VOGONS member
Duffman; nothing below has run on it or on any NV1x part.
Evidence: one V9XSURV report (build d9d7aeb9, `query-only`, 2026-11-04
17:38), posted at <https://www.vogons.org/viewtopic.php?p=1454892#p1454892>
and copied in unmodified as
[`dell-inspiron2650-geforce2go-nv11-vogons-vgasurv-2026-11-04-d9d7aeb9.ini`](../probe/references/dell-inspiron2650-geforce2go-nv11-vogons-vgasurv-2026-11-04-d9d7aeb9.ini)
(it carries the machine's service tag; see "Survey tool notes").

The reporter's own words: the laptop freezes "for a few seconds" every 10-15
seconds; it boots from an SSD, so a failing disk is ruled out. They may be
able to put it online for remote testing.

## What the machine is

| | Value | Report section |
|---|---|---|
| Display | NVIDIA GeForce2 Go (NV11), `10DE:0112` rev B2, subsystem `1028:00F3` | `[PciDevice.0]` |
| VRAM | 16 MiB (VBE `TotalMemory64K=256`) | `[VBE]` |
| Video BIOS | "NVIDIA GeForce2 GO VGA BIOS" 3.11.01.52.A8, 07/24/02, 56 KiB at C000, checksum good | `[VideoBios]` |
| System BIOS | Phoenix, 09/10/2002, Inspiron 2650 rev A0 | `[OptionRom.1]` strings |
| CPU | GenuineIntel family 15 model 2 stepping 7: Northwood, so a Mobile P4-M or mobile Celeron | `[Platform]` |
| Host bridge | `8086:1A30` 845 MCH, AGP bridge `8086:1A31` (display on bus 1) | `[PciInventory]` |
| South bridge | ICH3-M: LPC `248C`, IDE `248A`, USB `2482`/`2484`, SMBus `2483`, AC'97 audio `2485` (STAC9700 codec), modem `2486` | `[PciInventory]`, DMI strings |
| Other | 3Com `10B7:9200` (3C905C-TX class) with PXE ROM at CE00; O2Micro `1217:6972` CardBus | `[PciInventory]`, `[OptionRom.0]` |
| RAM | 384 MiB (E820 type 1 to 17F80000h; ACPI tables at 17EF0000h, NVS at 17EFF000h) | `[Platform]` |
| OS at survey | DOS 7.10, no Windows running, HIMEM loaded, no EMM386 | `[System]`, `[Platform]` |

No mini-PCI wireless device is on the bus. Any Wi-Fi would be a PC Card
behind the CardBus bridge, which the survey does not enumerate.

## Findings

1. **Two BARs, the usual NV layout.** BAR0 = `E0000000` (memory,
   non-prefetchable: the register space); BAR1 = `F0000008` (prefetchable:
   the framebuffer). Every LFB mode's PhysBasePtr is `F0000000`. BAR sizes
   were not read: `query-only` does not size BARs.

2. **AGP 2.0, 1x/2x/4x, disabled at survey.** Capability chain 60h (power
   management) to 44h (AGP, revision 2.0). AGP status at 48h is `1F000007`:
   rates 1x/2x/4x, request queue 32, no sideband, no fast writes. The AGP
   command register at 4Ch is zero, as expected in DOS with no AGP driver.
   Config 40h mirrors the subsystem id (`1028:00F3`); 50h-5Fh is
   NVIDIA-specific and was not decoded.

3. **IRQ 10, pin A.** Bus master off (`Command=0003`), expected in DOS.

4. **VBE 3.0, 33 modes, 8-bit DAC.** `Capabilities=00000001` (DAC
   switchable). LFB modes: 8 bpp to 1600x1200, 16 bpp (5:6:5) to 1600x1200,
   32 bpp to 1024x768 only. No 15 bpp and no packed 24 bpp modes. No
   1280x1024x32 (`011B`) although 5 MiB fits in 16 MiB; that limit is the
   BIOS's. Also present: the 320x200/240/400 low-resolution set
   (`0130`-`0136`) and 640x400 at 16/32 bpp.

5. **No panel information through VBE.** `4F11` is unsupported and `4F15`
   returned `014F`. The panel resolution is not in the report. The
   Inspiron 2650 shipped with more than one panel; if this one is SXGA+
   (1400x1050) there is no VBE mode for it, and the BIOS list tops out at
   modes the panel would have to scale.

6. **DPMS `0710`** (VBE/PM 1.0: standby, suspend, off); protected-mode
   interface present (701 bytes).

7. **Extended VGA state is locked.** The sequencer dump repeats every 8
   bytes (only 3 index bits decode with the extensions locked); CRTC 20h-3Fh
   reads as zero except two `20` bytes. Nothing about NV11 extended state is
   in the report, which is what `query-only` promises.

## The stutter

The report cannot show it. The survey ran once, in DOS, and records
configuration, not time. Nothing in it is abnormal for this laptop: no
resource clash it can see, a good ROM checksum, sane E820, a normal AGP
capability.

Candidates, each with the test that would kill it. None is measured.

| Hypothesis | Fits a multi-second freeze every 10-15 s? | Test |
|---|---|---|
| Firmware SMI work (battery polling, thermal/fan handling) | Dell laptops of this period are known for SMI stalls, but those are usually short hiccups, not seconds | Does it freeze in plain DOS too (a game or a timing loop)? Run on AC with the battery removed. |
| Win9x CD-ROM auto-insert polling of a missing or faulty drive | Yes: a periodic multi-second hang is the classic symptom | Device Manager, CD-ROM, Settings: clear "Auto insert notification"; or run with the bay empty. |
| SSD behind an IDE adapter timing out and resetting the channel | Yes, if the freezes coincide with disk activity | Check whether DMA is on for the drive; watch the disk LED during a freeze; try another adapter or a known-good spinning disk. |
| NVIDIA display driver | Possible, unproven | Safe Mode, or the Standard VGA / Velocity9x VBE driver: if the freezes stop, the display driver is implicated. |
| PC Card Wi-Fi scanning | Only if one is fitted | Remove the card. |
| P4-M thermal throttling | No: TM1 halves the clock, it does not stop it | Not worth a run. |

The first test splits the list in two: a freeze in plain DOS rules out every
Windows-side cause.

## Survey tool notes

- `[OptionRom.1]` at DC00 is not an option ROM: it is the Phoenix DMI/SMBIOS
  string area (no PCIR, checksum mismatch, residue 3Ah). The scanner picks
  it up because it starts with `55AA`. It contains the machine's Dell
  service tag (`String.05` and `String.09`), a unique identifier. It is
  already public in the forum post, so the reference copy keeps it
  unmodified. The scanner could skip images without a PCIR.
- `[System] CpuClass=286-or-later` is the tool's floor; `[Platform]` has the
  real CPUID.
- `[OptionRom.0]` (3Com PXE): `PcirImageLength=58880` against 2 KiB resident
  is a PXE ROM that shrank itself after POST; the image-checksum mismatch and
  odd PCIR class follow from that and are not a tool fault.
- `String.07`-`String.63` of the video BIOS are the font read as text and
  used up the string budget (`StringsTruncated=1`), as on other NVIDIA ROMs.

## What it means for Velocity9x

- The `nvidia` family today covers only the RIVA 128/128ZX (`12D2:0018`).
  `10DE:0112` matches nothing specific; the `vbe` family would take it
  through `PCI\CC_0300`: BIOS mode sets, the LFB at `F0000000`, 8/16/32 bpp,
  CPU drawing, no acceleration.
- Native support would be a second generation (NV1x, "Celsius") in the
  `nvidia` family, alongside NV3 as [the RIVA 128 plan](../plans/nvidia-riva128.md)
  describes for the TNT and later: a 2D engine for
  DirectDraw and GDI, then the NV10-class 3D engine. Nothing of NV3 carries
  over beyond the family plumbing.
- The VBE driver doubles as a stutter test (row 4 above), so it is useful to
  the reporter before any NV11 code exists.
- No NV11 work starts unless the reporter puts the machine online for test
  builds; no fleet machine has an NV1x part.
