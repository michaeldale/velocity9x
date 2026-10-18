# 3dfx VSA-100: Voodoo4 first, Voodoo5 5500 and 6000 after

Date: 2026-10-11

Status: Phase 2 first bind done: Velocity9x runs the Voodoo4 at tier-0,
enable-ok at boot 409 on A8U4I5
([2026-10-11-voodoo4-runs-velocity9x-at-tier-0-and-its-vbe-reports-half-its-memory.md](../decisions/2026-10-11-voodoo4-runs-velocity9x-at-tier-0-and-its-vbe-reports-half-its-memory.md)).
VBE reports 16 MiB of the 32 and the BIOS leaves the last 4 KiB tiled; the
memory hook, reading through BAR2, now gives the driver 32 MiB less that
page (boot 410,
[2026-10-11-voodoo4-memory-registers-read-the-same-through-the-io-window.md](../decisions/2026-10-11-voodoo4-memory-registers-read-the-same-through-the-io-window.md)).
All 18 published modes switch
([mode matrix](../probe/a8u4i5-voodoo4-velocity9x-2026-10-11/mode-matrix-boot410/README.md)),
so Phase 2's exit gate is met apart from the scanout check, not run. Phase 0
done; Phase 1 partly done (stock-driver game baseline not run). Phase 3:
the 2D engine fills and copies pixel-exact and DirectDraw runs on it
(boot 411,
[2026-10-11-voodoo4-directdraw-fills-and-copies-run-on-the-2d-engine.md](../decisions/2026-10-11-voodoo4-directdraw-fills-and-copies-run-on-the-2d-engine.md));
GDI fill and copy run on it too, `V9XGDI /accel` PASS at 640x480x8,
640x480x16 and 1024x768x16
([2026-10-11-voodoo4-gdi-fill-and-copy-on-the-engine.md](../decisions/2026-10-11-voodoo4-gdi-fill-and-copy-on-the-engine.md)).
Still open in Phase 3: the command FIFO, 32 bpp, text. Next: Phase 4. The retail Voodoo4 4500 AGP
(32 MiB) is in A8U4I5 on 3dfx 1.04.00, and its first register survey is
recorded at 640x480
([2026-10-11-voodoo4-stock-driver-tiles-its-desktop-at-the-top-of-memory.md](../decisions/2026-10-11-voodoo4-stock-driver-tiles-its-desktop-at-the-top-of-memory.md)).
Larger modes are blocked: Windows has no monitor entry for the card and
refuses 1024x768. The sibling Mini-Me's VelocityNT measurements are
imported in
[2026-10-11-what-velocitynt-measured-on-the-mini-me-vsa100.md](../decisions/2026-10-11-what-velocitynt-measured-on-the-mini-me-vsa100.md).
Phase 0 outputs: [vsa100-registers.md](../specifications/vsa100-registers.md),
[vsa100-ext-hwc.md](../specifications/vsa100-ext-hwc.md), the
`VSA100.EXE` probe, and `src/chipsets/tdfx/vsa100_decode.c` with host
tests.

## Why this plan, and how it relates to the Voodoo3 plans

[3dfx-voodoo3-family.md](3dfx-voodoo3-family.md) and
[3dfx-voodoo3-prior-work.md](3dfx-voodoo3-prior-work.md) plan the Voodoo3
(Avenger). The VSA-100 (Napalm) is the same lineage: the same BAR0 sub-space
map, the same 2D core, the same command FIFO, the same VGA/VBE BIOS ancestry.
What it adds is 32-bit colour, stencil, 2k textures, FXT1, two pixels per
clock, and multi-chip SLI/AA.

By the one-family-per-vendor rule, the VSA-100 is a chip in the `3dfx`
family (`tdfx` C prefix), not a family of its own. Whichever card is
installed first creates that family. If the VSA-100 goes first, this plan's
Phase 2 creates `3dfx` with `vsa100` as its only chip, and the Voodoo3 plan
later adds `voodoo3` beside it. The 2D engine (Phase 3) is written once for
the Avenger/Napalm 2D core and serves both chips through per-chip data. A
fix measured on one goes to both.

Voodoo Graphics and Voodoo2 stay out of this family. They are 3D-only
devices beside a host VGA card, a separate design (roadmap Track C note).

## What is already true

- **Adding a chip is manifest plus evidence**
  (`docs/specifications/family-manifest.md`, "Adding a family"). The SiS
  family (`c58dfe5`, then `afd2154` for 2D and `d3dc21f` for D3D) is the
  model for touch points.
- **`scripts/check-tree.ps1` refuses any chip with vendor `121A`.** Every
  package installs the Velocity9x `GLIDE2X.DLL` and `GLIDE3X.DLL`, which
  would replace a real Voodoo's own Glide. Phase 2 resolves this; nothing
  else can land first.
- **The Velocity9x Glide DLLs are wrappers over `V9xRenderInterface`.** They
  never touch hardware and report a Voodoo Graphics (Glide 2) or a Voodoo3
  (Glide 3). They are the wrong thing to give a game on a real VSA-100.
- **No emulator models the VSA-100.** 86Box has Banshee and Voodoo3 only.
  The `Win98SE-BX-Voodoo3` guest (port 9874) can exercise code the two chips
  share (2D core, FIFO), and proves nothing about Napalm registers. This
  family is physical-first, as SiS was.
- **The OpenGL ICD and both Glide wrappers reach a chip only through
  `V9X_D3D_ENGINE_OPS`.** A tdfx D3D engine gives OpenGL with no extra work.
  The ICD's renderer-string rule (no "voodoo", "3dfx", "glide", because
  Quake 2 tests for them) needs revisiting on a real 3dfx card; see open
  questions.

## Sources, and what each may license

Velocity9x is GPLv3. The prior-work plan's licence table still holds; this
plan adds the VSA-100 sources.

| Source | Status | Use |
|---|---|---|
| VelocityNT M5 (the author's NT/2000/XP driver, closed for now) | Physical measurements of the Mini-Me v2 on Windows 2000, 2026-10-10/11 | Hardware facts and their evidence, imported by a decision record (Phase 0). No code. |
| Napalm Graphics Engine spec rev 1.13, 6 Oct 1999 (`3dfx driver research\sux6000-docs\voodoo45-spec.pdf`) | Marked "3dfx Confidential"; circulated with the SUX 6000 documentation | **The databook for VSA-100 register writes** (decided 2026-10-11). Cited by section and page. Where its prose and bit tables disagree, the bit table is followed and the disagreement recorded in the fact sheet (three known cases, below). |
| Avenger (a.k.a. Voodoo3) spec rev 1.0, Nov 1999, and the Banshee spec and 2D databook (bitsavers copies in `3dfx driver research\`, archived 2026-10-18) | 3dfx release; watermarked confidential | Cross-check for registers the VSA-100 shares with Avenger, and the Voodoo3 chip's authority. Read against the fact sheet 2026-10-18: each Avenger note there is now confirmed or corrected with a page. |
| Open Glide h5 (`artifacts\3dfx_glide\h5`, Koolsmoky tree) | 3DFX GLIDE Source Code General Public License | Facts only: register layouts (`h5/incsrc/h3regs.h`), the `EXT_HWC` escape protocol (`h5/minihwc/hwcext.h`), SLI setup order. No code. |
| retro-3dfx project notes (`C:\everything\retro-3dfx\*.md`, `optimized\*.md`; voidsstr, public, no licence file) | Measurements on a Voodoo5 5500 AGP and a 4-chip Voodoo5 6000 under Windows XP, with their rebuild of the leaked driver | Facts only, as hypotheses; see "Prior work: retro-3dfx". No text or code copied. Their `3dfx Driver Code\` folder is the leak and falls under the row below. |
| Mesa `tdfx` (`artifacts\MesaFx-7.4.4`) | MIT | Compatible; may be adapted with attribution. 3D only. |
| smoltdfx | GPLv3 | Compatible; Voodoo3 2D/3D/FIFO worked example. |
| Leaked 3dfx tree (`C:\everything\3dfx-driver`, and the second copy under `C:\everything\retro-3dfx\3dfx Driver Code\`) | None granted | Not read for Velocity9x. The research folder's README makes this a standing rule (`H3REGS.H`, `CSIM`, `DIAGS` named). Stricter than the prior-work plan's "understanding only"; this plan adopts the stricter rule. |

## Measured on a sibling board: the Mini-Me (VelocityNT M5)

Machine: DESKPRO, Compaq Deskpro, Pentium III ~1 GHz, Windows 2000 SP4,
PCI slot, VGA to a BenQ T2200HD. Board: Mini-Me v2, one VSA-100. Every
value is a physical measurement on that board under VelocityNT's own
miniport or under the 3dfx vendor driver, as marked.

These are measurements of the chip, not of the retail 4500. Board-level
values (subsystem, memory size, BIOS version, tile boundary, BAR sizes) are
expected to differ; chip-level semantics (register layout, selector
protocol, idle procedure, memory decode) are expected to carry. Phase 1
says which held.

### Identity and apertures

- PCI `121A:0009`, subsystem `121A:0002`, revision `01`. Agent inventory
  names the video BIOS "Voodoo4 4500 LCD" 1.18. No ROM image was dumped or
  hashed.
- **Subsystem does not identify chip count.** `0002121A` is the Voodoo5
  5500 AGP's id in the 3dfx INF, on a one-chip board. The driver counts
  chips at run time (Phase 6); it never infers it from the subsystem.
- BAR0 `0x40000000`, 128 MiB, non-prefetchable MMIO. BAR1 `0x50000008`,
  128 MiB, prefetchable. BAR2 I/O `0x1000–0x10FF`. IRQ 16. Both memory BARs
  are four times Banshee's 32 MiB; BAR1 is twice the memory size because its
  upper half is a tiled view. **BAR1's size is not VRAM size.**
- VGA registers are reachable in BAR2 (sequencer `0xC4/0xC5`, misc output
  read `0xCC`, CRTC `0xD4/0xD5`); VelocityNT read timing through them.

### Memory

- `dramInit0` = `0x607EADF9`, `dramInit1` = `0x40240031`. Decode: bit 26 part
  count (1 = 8, 0 = 4); bits 29:27 density, 8 Mbit << n; bytes = parts ×
  Mbit × 128 KiB; `dramInit1` bit 30 = SDRAM. Result: 4 × 128 Mbit =
  64 MiB SDRAM.
- A paired-page sweep wrote, verified and restored every DWORD in
  [1 MiB, 64 MiB) with two address-distinct patterns. Pages 32 MiB apart did
  not alias. 64 MiB is real.
- The vendor driver (5.00.2195.0232) reported 64 MiB and ran 1024x768x32 at
  60 Hz.

### The BIOS's linear aperture ends at 32 MiB

- After the BIOS mode set, `lfbMemoryConfig` tile control is `0x3FFF` and
  compare is `0`. That puts the first tiled page at `0x01FFF000`: the linear
  BAR1 view covers just under 32 MiB of the 64.
- `lfbMemoryConfig` (`0x0C`) multiplexes two values behind a selector:
  write `0x40000000` then read for control, `0x60000000` then read for
  compare. The spec's prose about bit 30 is wrong; its bit table is right.
- Temporarily writing compare `0x8000C000`, with desktop fetch paused by
  clearing only `vidProcCfg` bit 7, moved the boundary to 64 MiB. Restoring
  compare and `vidProcCfg` worked, and five mode/reset pairs passed after.
  VelocityNT keeps this to diagnostics; its normal driver stays inside the
  BIOS boundary.
- Consequence for tier-0: usable linear VRAM is the smaller of the decoded
  size and the BIOS boundary, so 32 MiB on this board unless Velocity9x
  takes over the boundary (a later, measured step). On a 32 MiB retail 4500
  the two may coincide; Phase 1 measures it.

### First mode and timing

- VBE `4F02h` with BX = `4111h` (mode `111h`, 640x480x16, linear) returned
  `004Fh`, set by register-only INT10.
- Post-mode registers: `vidProcCfg` `0x30040C81` (bits 18–20 = 1, RGB565;
  bit 7 desktop enable; bit 0 processor enable), `vidScreenSize`
  `0x001E0280`, `vidDesktopStartAddr` `0`, `vidDesktopOverlayStride` `0x500`
  (1280 bytes, linear), `miscInit0` `0`, `miscInit1` `0x27008001`.
- The mode runs on the fixed 25.175 MHz VGA clock (misc output `0xE3`, bits
  3:2 = 0), not the PLL; `pllCtrl0` `0x00000C01` is unused. CRTC
  `5F/4F/0B/3E/40/DF/E3`, `CR1A`/`CR1B` `00/00`, `dacMode` 0: 800x525 total,
  59.940 Hz, 31.468 kHz. The user confirmed the picture on the monitor.
- Eleven text-mode resets and console round trips passed pixel regression.
  No hang or wedge was seen.

### Status and idle

- Status at MMIO `0x0`, bit 9 is the aggregate busy bit. Idle is three
  consecutive clear reads, 10 µs apart, at most 256 polls; all-ones means the
  device is gone. Twelve reads covered one aperture transaction, no
  timeouts. The FIFO-free prose disagrees with the bit table and is unused.

### Not measured by that survey

- **VBE controller info (`4F00h`) and mode info (`4F01h`).** Windows 2000's
  `VideoPortInt10` cannot transfer buffers, so no VBE version, OEM string,
  memory figure or mode list was read. Tier-0 depends on all of them.
- The ROM image and hash, EDID, and every mode other than 640x480x16.
- Refresh rates other than the BIOS default, the PLL in use, `dacMode` 2x.
- Anything on Windows 9x, any AGP board, any 2D/3D engine use.

## Facts from documents only

Not measured anywhere. Each is a hypothesis until a Velocity9x record
confirms it.

- Subsystem ids from the 3dfx INF: `0004121A` Voodoo4 4500 AGP, `0005121A`
  Voodoo4 4500 PCI, `0002121A` Voodoo5 5500 AGP, `0003121A` Voodoo5 5500
  PCI, `0001121A` Voodoo5 6000. A stale `0005121A` registry entry on DESKPRO
  suggests a retail 4500 PCI was once in that machine.
- On multi-chip boards the chips are functions 0–3 of one PCI device; the
  master is function 0.
- **The BIOS sign-on reports memory summed across chips.** If VBE `4F00h`
  does the same, tier-0 on a 5500 would claim 64 MiB when the master scans
  out of 32. Tier-0 takes VRAM from `dramInit0`, not from VBE (Phase 2).
- The BIOS reports VBE 3.00, or 2.00 to callers that pass "VBE2" (BIOS
  source notes).
- BAR0 sub-spaces (Napalm spec p. 25 and open Glide agree): I/O `0x0`,
  CMDFIFO/AGP `0x80000`, 2D `0x100000`, 3D `0x200000`, textures from
  `0x600000`, LFB window `0x1000000`.
- PLL: f = 14.31818 MHz × (N+2) / ((M+2) × 2^K), N bits 15:8, M 7:2, K 1:0.
  Confirmed on the Voodoo4 for `pllCtrl0` and `pllCtrl1`. "M = 0 refused"
  is VelocityNT's rule, not the databook's. `miscInit0` bit 30 makes the
  `miscInit1` address alias `vip2vmiCtrl`; read `miscInit0` first. 3dfx's
  driver leaves bit 30 set.
- BAR1's tiled view starts at the tile boundary, wherever that is; "the
  upper half is tiled" holds only when the boundary is at 64 MiB.
- 2D errata: blits 16 pixels or smaller in either dimension can hang the
  chip, a NOP first avoids it; fence every 64 KiB of FIFO writes; 12 NOPs
  when leaving two-pixels-per-clock; a NOP before switching to a compressed
  texture; `vgaInit0` bit 12 stops VGA refresh (reportedly ~10% on blits).
- Host-chipset problems: 440FX/PIIX3 write-combining lockup, VIA, AMD 751.
  A8U4I5 is 440BX, not on the list.

### Glide discovery on Windows 9x

Full prediction list, from the open h5 tree:
[vsa100-ext-hwc.md](../specifications/vsa100-ext-hwc.md). In short, the
open h5 `minihwc` does not scan PCI on Windows; it asks the display driver:

1. `ExtEscape(0x3DF3, GETDEVICECONFIG)` directly, then `0xFD3` the same
   way. A non-zero return and vendor `121A` in the reply is the "yes".
   `QUERYESCMODE` (`0x8001`) is used only to find the devnode. If nothing
   answers, Glide fails.
2. A single-chip fullscreen session needs six sub-functions:
   `GETDEVICECONFIG`, `GETLINEARADDR` (BAR0 and BAR1 addresses Glide
   dereferences from ring 3 in the game's process), set and release
   exclusive, `SHARE_CONTEXT_DWORD`, `UNMAP_MEMORY` and `PCI_OP`.
   `VIDTIMING` and `GETAGPINFO` only in some cases. No driver-version check.
3. Glide builds its own command FIFO at VRAM `0x18000` and uses everything
   above it; only the first 96 KiB is left alone. The desktop comes back
   only through the display driver's mode set after release.
4. `\\.\H5VDD` (`DeviceIoControl` 6/7) is opened only on multi-chip boards
   with SLI or AA. A Voodoo4 does not need it.

So **stock 3dfx `glide2x.dll`/`glide3x.dll` for Voodoo4/5 can run on
Velocity9x only if the display driver answers `EXT_HWC`**, and multi-chip
Glide only if a VxD registers as `H5VDD`. This replaces the prior-work
plan's `\\.\CONFIGMG` hypothesis for h5; whether h3 (Voodoo3) Glide differs
is for that plan. The open tree is a community fork; whether 3dfx's
shipped DLLs behave the same is itself a prediction to test.

## Prior work: retro-3dfx

`C:\everything\retro-3dfx` rebuilds 3dfx's leaked H5 driver for Windows XP
and runs it on a Voodoo5 5500 AGP (Athlon, XP SP3) and a 4-chip Voodoo5
6000 (HiNT HB1-SE66 bridge; dual P3/440BX, then Athlon/nForce2). It never
ran Windows 9x or a Voodoo4. Researched 2026-10-11. Every item below is
their measurement on those boards under XP unless marked "from source"
(their reading of the leaked tree, which this project does not read). Each
is a hypothesis here until a Velocity9x record confirms it. Their own
fixes to the leaked tree do not touch `H5\Win9x\`.

### Bears on tier-0 and the 2D engine (Phases 2-3)

- **Bound every wait by time, not by count.** The chip can stop consuming
  the command FIFO with its read pointer frozen; the cause is unknown.
  3dfx's spin limits (50-100 M polls) took 50-100 s at about 1 µs per
  uncached MMIO read, longer than XP's ~30 s watchdog; they now cap at about
  2 M polls (~2 s). Win98 has no display watchdog, so an unbounded wait
  there is a frozen machine, not an error (`V56K-SLI-FINDINGS.md` §22,
  `VINTAGE-FIXES.md` §3).
- **Burstiness, not frame rate, moved the FIFO wedge rate** on the 6000:
  per-frame logging on was stable, off reset the box three times in three
  runs. A 64 KiB FIFO did not help and then froze the box (§21-21a).
- **The AGP command FIFO stayed off** as "the classic hard-lock vector";
  the FIFO lives in local memory (`optimized\HANDOFF-3DMARK-D3D.md`,
  `V56K-PLAN.md`). This plan already leaves `agpInit` alone.
- **FIFO bring-up rules** (from open Glide, which this plan may read):
  setup registers are written directly, never through the FIFO; a packet
  larger than one lap must be refused, or the make-room wait never ends;
  read the read pointer twice with a status read between
  (`FINDINGS.md`, FIFO bring-up entry).
- **Desktop at the top of memory:** their 6000 master put a 640x480 Glide
  surface at `01F60000` with tile stride `0A`, the values this card's
  stock driver uses for its desktop. Consistent with the tiled-stride unit
  inferred in the Voodoo4 survey.
- **`vidProcCfg` bits 28 and 29** are set on every Napalm board by 3dfx's
  W2K miniport, commented as a fix for high-resolution modes "in a heated
  environment" (from source). Both are writable at `5Ch` (measured). The
  Voodoo4 under 3dfx's Win98 driver reads them clear; the Mini-Me's BIOS
  mode had them set. Whether either matters is unknown; a candidate when
  chasing scan-out artefacts.

### Bears on Glide (Phase 4)

- **Close order:** leave exclusive, release exclusive, restore the mode,
  `UNMAP_MEMORY` (5500).
- **`SHARE_CONTEXT_DWORD` must return a valid pointer.** On the Voodoo3,
  Glide's NT path dereferenced a NULL one and crashed; the open-tree reading
  in [vsa100-ext-hwc.md](../specifications/vsa100-ext-hwc.md) says the same
  for 9x.
- **`GETLINEARADDR` before anything that needs chip registers;**
  `GET_SLAVE_REGS` returned stale zeros otherwise.
- **`grSstWinOpen` needs a real window;** a null one failed silently
  (Voodoo3).
- **A killed fullscreen Glide 2 title hangs the bus** (Voodoo3:
  half-written packet). Repeated failing `grSstOpen` calls (nGlide) froze a
  Voodoo3. Phase 4's killed-title case should expect a power cycle.
- **Quake III stalls:** one in about 20 launches hit Glide's no-context
  `int3` at `grDrawTriangle`, attributed to a focus loss tearing down the
  context; modal dialogs behind a fullscreen game look like wedges
  (`FINDINGS.md`). Relevant to the Phase 1 baseline: the Voodoo4 crashed in
  Quake III's timedemo under the stock driver.
- **Open Glide h5 lacks four fixes the h3 tree has** (TLS read, call order
  of `GETLINEARADDR`, a zero-base guard, a lost-context fallback) and froze
  the 6000 (`OPEN-STACK-ON-VSA100.md`). Phase 4 targets 3dfx's shipped DLLs;
  if the open tree is ever used, these come first.

### Bears on Direct3D and OpenGL (Phase 5)

- **Texture base addresses ignore bits 3:0:** a heap at +8 shifted every
  texture by four texels (5500, `optimized\TEXT-GARBLE-ROOTCAUSE.md`).
  Allocate textures 16-byte aligned.
- **Colour and depth buffers need matching tile parity;** a render target
  in the GDI desktop buffer hard-hung a Voodoo3 (`FINDINGS.md`).
- **Two pixels per clock is single-texture only.** Left on in a
  dual-texture pass, the world rendered wrong (5500).
- **Glide reports two TMUs on a 5500 chip** (`grNTexelFx == 2`), against a
  note that says one. Answers the TMU open question for the 5500, not yet
  for the 4500.
- **The 25-bit texture address mask is correct even at 64 MiB a chip;**
  the alternative relocation form rebooted the 6000.

### Bears on the Voodoo5 (Phases 6-7)

- Slaves answer as PCI functions 1-3 of the master's device, also behind
  the 6000's bridge. Register apertures 32 MiB apart, framebuffers twice
  the per-chip memory apart (from source).
- `CFG_SLI_RD_EN` set on every chip makes BAR1 reads through the master
  return the reassembled picture.
- Four chips always combine analog (two digital pairs); two combine
  digitally (from source).
- **Values that froze or rebooted the 6000:** `cfgSliAaMisc` `81F` (31 px
  vsync offset); an SLI band whose group height does not divide the screen
  height; master `cfgVideoCtrl0` bit 11 (no picture); slave `miscInit0`
  forced to the master's (blank); `FX_GLIDE_NUM_CHIPS=2` on four chips; an
  unchecked `vidOverlayDudx` sweep. Any Velocity9x sweep needs a liveness
  check per step.
- The 6000's clock synthesizer is bit-banged through GPIO at HiNT bridge
  config `C4h`; that code existed only in 3dfx's Win9x driver (from
  source). Registry tunables are read as strings; a DWORD is ignored.
- **The 4-way scanout skew:** bands displaced on the monitor while
  readback is bit-exact. A third-party driver on the same board shows none.
  It is the closest known analogue to this card's monitor-only lines, and
  their conclusion applies: register state and someone watching the
  monitor are the only sensors.
- 256 MiB (64 MiB per chip) mode works with no driver change.

### Methods worth adopting

A per-register recorder that writes a line on every change and flushes each
line, so a wedge leaves the last state on disk; liveness-gated sweeps that
stop at the step that breaks the machine and reboot between conditions;
"the timedemo printed its frame rate" as an unattended pass check; a
same-box control driver (3dfx's own here, as AmigaMerlin is for them).

## Phase 0 — desk (no hardware)

1. **Import record.** A dated decision record here
   (`YYYY-MM-DD-vsa100-velocitynt-evidence.md`) carrying the measured tables
   above with machine, date, VelocityNT source revision and raw values, so
   Velocity9x code can cite evidence that lives in this tree. It also states
   the provenance rule: VelocityNT supplies facts, not code.
2. **VSA-100 register fact sheet** (`docs/specifications/vsa100-registers.md`).
   Every register this plan names, cited to the Napalm spec by section and
   page, cross-checked against the Avenger (Voodoo3) spec, open Glide h5
   and the measurements. Disagreements are listed, not resolved by
   majority. Shared Avenger registers are marked as such, so the Voodoo3
   plan reuses the sheet. The Avenger cross-check was done on 2026-10-18,
   from the documents only.
3. **`EXT_HWC` prediction list.** From `hwcext.h`, the sub-functions a
   single-chip Glide 3 session calls, in order, each with what the driver
   must return. This is the Phase 4 spec.
4. **Read-only probe tool** on the SiS pattern (`SIS6326.EXE` +
   `SIS6326.VXD`, `scripts/build-sis6326-probe.ps1`): a `VSA100.EXE` and
   VxD that read PCI config space, the MMIO registers named in Phase 1, the
   VGA registers through BAR2, and the BIOS shadow. Its only writes are the
   VGA index ports and the `lfbMemoryConfig` selector, each restored; the
   selector write changes no tile data (Napalm spec bit table, measured
   harmless on the Mini-Me). DOS is not an option on A8U4I5, so this is the
   survey route.

Exit gate: the four items; the probe builds and passes host tests for its
decode functions. No driver code.

## Phase 1 — survey the retail Voodoo4 4500 on A8U4I5

A8U4I5 (P3, 440BX, AGP 2x 3.3 V and PCI, real MTRRs) takes the card in
place of whatever it carries; the 4500 AGP is AGP 2x 3.3 V and slot-powered.
After the swap, check `BitsPerPixel` before the first boot on the new card
(a past swap left it at 4, loading `vga.drv`). A8U4I5 has no DOS mode, so
everything runs from Windows.

1. **Stock driver first.** Install the 3dfx 1.04.00 Win98 driver. Run the
   probe at the stock desktop, at 16 and 32 bpp, and at 640x480 and
   1024x768. Answers: PCI ids, revision, subsystem, BAR sizes, IRQ;
   `dramInit0/1` and the decode; `lfbMemoryConfig` control and compare;
   `vidProcCfg`, `vidScreenSize`, stride, start address; `miscInit0/1`;
   `pllCtrl0`, misc output, CRTC; BIOS shadow image and hash.
2. **Mini-Me predictions.** Each chip-level fact in the measured section
   marked held, refuted or not reached. Board-level differences recorded
   as such.
3. **Stock-driver Glide and Direct3D baseline.** Diablo II, Rollcage and
   NFS II SE on 3dfx's own Glide; Half-Life on its Direct3D and OpenGL
   renderers (mwd5 timedemo) on 3dfx's D3D HAL and ICD. Record captures,
   frame rates and the Glide version each title loads. This is the first
   Voodoo reference run the Glide wrapper plans never had, and the
   comparison for Phases 4 and 5.
4. **Decision record** `YYYY-MM-DD-voodoo4-register-survey.md`: Measured /
   What the sources add / Hypotheses this kills / Not established.

The VBE mode list is not read here: the mini-VDD's boot-time collection
(`MiniVddVbeCollect`) reads it on the first tier-0 install in Phase 2, as
the SiS family did.

Exit gate: the decision record plus the archived probe output and captures.

## Phase 2 — tier-0 chip `vsa100` in family `3dfx`

1. **Glide packaging first.** Replace the check-tree `121A` refusal with a
   rule that keeps it true: a manifest field drops `GLIDE2X.DLL` and
   `GLIDE3X.DLL` from the package and from `V9XCOPY.BAT`, and check-tree
   asserts that every family claiming `121A` sets it. The card keeps
   whatever Glide the user installed.
2. **Manifest.** `packaging/families/3dfx/family.psd1` (or the `vsa100` chip
   added to it). Ids and subsystem from Phase 1, not from the 3dfx INF.
   `MiniVddVbeCollect = $true`; static mode rows only where Phase 1's ROM
   image shows them. `Vm.Emulator = 'none'` unless the Voodoo3 chip already
   brought a guest. `EngineType 'NONE'`.
3. **hw16 data.** `framebuffer_bar` = BAR1. First bind: every hook NULL,
   memory from VBE 4F00h clamped to a 32 MiB mapping (one chip's memory),
   as SiS started. Then the probe reads the tile boundary this BIOS leaves
   in a VBE mode, under our driver, and a `read_video_memory` hook takes
   usable VRAM = min(`dramInit0` decode, BIOS tile boundary) through the
   host-tested `v9x_tdfx_vsa100_usable_memory`. `dramInit0` is readable
   through BAR2 (I/O) before any mapping exists; the boundary needs the
   `lfbMemoryConfig` selector, a write, so its handling is decided from that
   measurement.
4. **Gates.** `check-tree`, `build-host`, `update-backend-registry`,
   `run-checks` green; enable gate and mode matrix on A8U4I5; the scanout
   check. The boot-time VBE table is archived with the record.
5. **Record what stock Glide does** on tier-0: expected to fail at the
   `EXT_HWC` query. A crash or a wedged desktop is a defect to fix before
   release, not an expected outcome.

Exit gate: `Stage=enable-ok`, a clean mode matrix, DirectDraw on CPU blits,
and a decision record including the stock-Glide result.

If the BIOS boundary falls short of the decoded size on this card, owning
it follows VelocityNT's measured compare-write and restore sequence. That
is a register write in display mode, so it needs its own Win98 measurement
and a restore on every mode set.

## Phase 3 — the 2D engine (`V9X_DD_ENGINE_TYPE_TDFX`)

One engine for the Avenger/Napalm 2D core; chip differences are hw16 data.

1. Direct register writes at BAR0 `0x100000`: solid fill, then
   screen-to-screen copy. No ring.
2. Bounded idle wait as measured (triple read of status bit 9). The
   ≤16-pixel hang erratum handled by the NOP workaround, with a host test on
   the size predicate.
3. DirectDraw first (the SiS `afd2154` shape: `engine_abi.h`, `ddhal_core.c`,
   `engines/eng_tdfx.c`, mini-VDD MMIO mapping), then the `gdi-accel` arm.
4. CMDFIFO as its own sub-phase once the direct path is proven, using the
   prior-work plan's ten-store bring-up order. FIFO memory type (uncached vs
   write-combined with a fence) is a measured decision.

Exit gate: pixel-correct fills and copies across a mode-set enable series on
hardware, the `gdi-accel` harness passing, and the stock-baseline Glide
behaviour unchanged.

## Phase 4 — stock 3dfx Glide on Velocity9x (`EXT_HWC`)

Most Voodoo4-era games use Glide. A driver that runs D3D and breaks every
Glide title is a regression for this card's users, so this precedes D3D
(decided 2026-10-11).

Stock Glide does not pass through OpenGL, `V9XHAL.DLL` or the render core.
Once `EXT_HWC` hands it the linear address and a command FIFO, it builds
command packets itself and the chip executes them. The display driver's
part is discovery, mapping, exclusivity and desktop restore. So the ICD's
performance (Phase 5) has no bearing on Glide on this card. (The Velocity9x
Glide wrappers do not go through OpenGL either; they are a client of
`V9xRenderInterface` beside the ICD, and are not installed on a 3dfx
family.)

1. Answer `EXT_HWC` with the six sub-functions in
   [vsa100-ext-hwc.md](../specifications/vsa100-ext-hwc.md), single chip
   only. Escape entry is in the 16-bit driver's `Control`; where each
   sub-function's work lives (16-bit glue, HAL, mini-VDD mapping of BAR0
   and BAR1 into the shared arena so the game can dereference them) is
   decided against `win9x-driver-boundaries.md` before code. First step: a
   logging stub that answers nothing and records what 3dfx's shipped Glide
   asks for, to test the open tree's predictions.
2. Exclusive take/release and desktop restore: Glide overwrites all VRAM
   above 96 KiB, so the desktop, any offscreen heap and the 2D engine's
   state come back by a full mode set and repaint after release. A killed
   title is a separate, recorded case (the Voodoo3 notes say it wedges the
   chip until power-off).
3. Gate titles: the Phase 1 baseline set on 3dfx's own Glide 3 and Glide 2.

Exit gate: each title starts, renders like its baseline capture, and
returns a usable desktop; decision record with captures.

## Phase 5 — Direct3D engine (OpenGL follows)

Detailed plan: [3dfx-vsa100-d3d.md](3dfx-vsa100-d3d.md).

Plugs into `V9X_D3D_ENGINE_OPS`. Napalm triangle setup and formats from the
fact sheet, Mesa `tdfx` as a compatible worked example. 16-bit colour and
one TMU first; 32-bit colour, stencil, 2k textures and FXT1 as separate
steps. The milestone game is chosen then and confirmed to use hardware D3D
before it becomes the gate. OpenGL then runs through the render core with no
chip work.

All three APIs are required on this card: Glide (Phase 4, native), Direct3D
and OpenGL (this phase, through the tdfx engine).

**OpenGL performance review.** On the GMA 950 and the Rage XL the ICD runs
at about half the frame rate of Direct3D (user observation, not yet in a
decision record). Because both APIs share one render core and one engine,
the gap is either in the ICD's own path or in how its draws reach the core;
which is unknown. This phase:

1. Records the gap on the existing engines first, same scene, both
   renderers: Half-Life mwd5 timedemo on its Direct3D and OpenGL renderers,
   on the netbook and the Rage XL. One decision record, frame rates and
   where the time goes.
2. Repeats it on the tdfx engine once both APIs draw, beside the Phase 1
   stock-driver numbers for the same timedemo.
3. If the gap holds on a third engine, it is in the shared path, and its fix
   is a separate plan that benefits every chip, not tdfx work. If it does
   not, the tdfx engine's caps or batching for the ICD are the suspect.

Exit gate for OpenGL on tdfx: correct rendering of the gate titles plus the
recorded comparison. Closing the gap is the separate plan's gate, not this
one's.

## Phase 6 — Voodoo5 5500

1. **Survey** as Phase 1, all functions. How does Win98 enumerate the slave
   function: a second display devnode, an unknown device? The INF must bind
   the master only and leave the slave inert.
2. **Single-chip first.** Master scans out the desktop; slave untouched. This
   is tier-0 through Phase 5 on a 5500 with one chip's memory, and should
   need only the run-time chip count and the per-chip memory clamp.
3. **SLI for 3D.** Slave discovery by PCI function, slave BAR placement,
   `sliCtrl` (render, compare and scan masks, log2 chip count, enable),
   `cfgSliLfbCtrl`, `cfgAALfbCtrl`, `cfgInitEnable`. Setup order from open
   Glide h5 as facts. Its own plan, written after the single-chip run.
4. **Stock Glide multi-chip.** An `H5VDD`-named interface answering
   `DeviceIoControl` 6/7, in the mini-VDD. AA after SLI.

## Phase 7 — Voodoo5 6000 (sketch only)

Four chips behind a PCI bridge: the user's SUX 6000 has a PLX PCI6152 with
1 MiB-granular windows that silently drop transactions outside them;
original boards used Intel or HiNT bridges. External or 6-pin power. Win98
works on the stock driver; Win2000/XP falls back to one chip because slave
BARs land outside the bridge window. Velocity9x programs no bridge until a
dedicated plan, with measurements from that card, says how. The research
folder's 6000 notes are its starting point.

## Later, independent of the phases

- Native mode set: PLL and CRTC programming for refresh-rate control, with
  the PLL arithmetic as host-tested policy.
- Hardware cursor (`vidProcCfg` bit 27), CLUT and gamma, DDC.
- Overlay and YUV.
- AGP texturing. Phases 1–5 leave `agpInit` and the GART untouched, so AGP
  and PCI boards take the same path.

## Decisions fixed

- `vsa100` is a chip in family `3dfx`; C prefix `tdfx`; one 2D engine for
  Avenger and Napalm.
- Policy (memory decode, usable-memory clamp, chip count, PLL arithmetic,
  blit-size erratum) is pure C with host tests; the backend reads registers
  and interprets nothing.
- A 3dfx family never installs the Velocity9x Glide wrappers.
- VRAM comes from `dramInit0` and the tile boundary, never VBE or BAR1 size.
- Subsystem ids identify the board for the INF only; chip count is measured.
- No bridge programming, slave relocation or AGP setup before their own
  plans.
- No INI gates: each feature is the default in the build that ships it.
- The leaked 3dfx tree is not read for Velocity9x work.
- The Napalm spec rev 1.13 is the databook cited for VSA-100 register writes
  (2026-10-11).
- Stock Glide (Phase 4) precedes Direct3D and OpenGL (Phase 5); all three
  APIs ship (2026-10-11).
- A8U4I5 with the retail 4500 is the development machine. The Mini-Me stays
  in DESKPRO for VelocityNT; moving it is a fallback, not a plan step
  (2026-10-11).

## Open questions only hardware answers

- What does VBE `4F00h` report on the 4500? Per-chip or summed on a 5500?
- Does the BIOS move the tile boundary or switch to the PLL for larger
  modes?
- Does the 4500's BIOS leave the linear view covering all 32 MiB?
- Does 3dfx's own OpenGL ICD (which runs on Glide) also work once
  `EXT_HWC` answers? Users would then have it as an alternative to ours.
- How does Win98 enumerate a 5500's slave function?
- Does the Velocity9x desktop survive a stock-Glide title after Phase 4, and
  after a killed one?
- How many TMUs does Glide report on a 4500 (retro-3dfx measured two on a
  5500 chip), and what does the D3D engine expose?
- Do `vidProcCfg` bits 28/29, clear under 3dfx's Win98 driver, change the
  lines on the A8U4I5 monitor?
- What does `miscInit0` bit 30, which 3dfx's driver sets, change, and is
  the value at `14h` then `miscInit1` or `vip2vmiCtrl`?
- 3dfx's driver leaves PCI bus mastering off (command `0003`) at the
  desktop. Does Glide or D3D turn it on?
- Should the OpenGL renderer string say "3dfx" on a real 3dfx card, so
  Quake 2 and GLQuake take their Voodoo paths, or stay neutral as on other
  chips?
- Does an AGP 4500 on a 440BX run stably with write-combined BAR1?

## Hardware

- **Development:** retail Voodoo4 4500, 32 MiB, AGP or PCI, on A8U4I5
  (10.0.1.172, Win98SE).
- **Sibling evidence:** Mini-Me v2 PCI (single VSA-100, 64 MiB, subsystem
  `0002121A`, BIOS 1.18) in DESKPRO under Windows 2000, VelocityNT's target.
  Its records are the import in Phase 0.
- **Later:** a Voodoo5 5500; the SUX 6000.
- **Emulator:** none for VSA-100. `Win98SE-BX-Voodoo3` (86Box, port 9874)
  for shared 2D/FIFO code only.
