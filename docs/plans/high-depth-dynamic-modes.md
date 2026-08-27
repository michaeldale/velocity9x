# Velocity9x: 24/32-bit colour + dynamic mode discovery (widescreen included)

Status: partially superseded (2026-08-26) — the dynamic-discovery and EDID/DDC
portions were re-planned and shipped via
[dynamic-vbe-pipeline.md](dynamic-vbe-pipeline.md); the completed high-depth
(24/32-bpp) work and its hardware evidence remain valid.

## Context

Velocity9x currently offers only 8/16bpp at 640x400/640x480/800x600/1024x768,
from a static 7-row mode table hand-duplicated in five places (family C table,
family.psd1 manifest, DirectDraw HAL table, mini-VDD VBE cache, INF MODES
keys). This plan adds 24- and 32-bit colour, and makes **every mode the card's
BIOS actually supports an option** — which, since widescreen and
most extra resolutions have no standard VESA numbers, means dynamic scanning
of the BIOS 4F00h mode list rather than a bigger static table. Decisions
already made: dynamic BIOS discovery (not QEMU-dispi, not static-only);
baseline 24/32 depths are per-family, probe-verified.

## Architecture (one line)

BIOS 4F00h/4F01h → mini-VDD full mode-list walk at Device_Init (ring 0 — the
only place buffered VBE works; ring-3 failures measured in enable16.c:189-205)
→ locked-data cache (cap 64, now incl. RGB mask bytes) → INT 2Fh mini-VDD API
v2 enumeration (`FN_MODE_AT`/`FN_MODE_MASKS`) → 16-bit driver builds a runtime
mode table at LibMain (baseline static rows + scanned rows, host-tested
filter/merge in new `src/common/vbe_modes.c`) → consumed by GDI, by the DD
shared block, and dumped to `C:\V9XMODES.INI` at first enable-ok →
`rundll32 v9xsetp.dll,V9xSyncModes` on a persistent HKLM Run line writes/prunes
`MODES\<bpp>\<w>,<h>` registry keys each boot → Display Properties shows them.

## Key design decisions

1. **Depth gates**: `src/common/mode.c:33-36` accepts 24/32. DIBENG PDEVICE
   flag fork at `src/display16/ddi.c:659-664` becomes three-way: 8 →
   `V9X_DE_PALETTIZED`, 16 → `V9X_DE_FIVE6FIVE`, 24/32 → neither (DIBENG
   derives layout from `biBitCount`; verify against `C:\98DDK` dibeng.inc
   before coding that no dedicated 24/32 flag exists).
2. **Baseline static rows stay probe-verified per family** (Phase 0 below
   measures whether each BIOS implements 0x0112/0x0115/0x0118 etc. as packed
   24 or 32bpp before any row is committed). Scanned rows take whatever 4F01h
   reports — bpp and RGB masks from the ModeInfoBlock (offsets 31-38, parsed
   into six new `v9x_vbe_mode_summary` fields in `src/common/vbe_parse.c`).
3. **Registry writer = settings DLL per boot.** `V9xSyncModes` export in
   `tools/diag/settings_propsheet.c` (it already does HKLM writes,
   settings_propsheet.c:468-483, and rundll32-from-INF is the shipped
   pattern, inf.ps1:177-178). Keys it creates carry a `V9xScanned=1` marker;
   pruning only deletes marked keys, so INF baseline keys and `MODES\4` are
   structurally unprunable. Missing INI → no-op (non-scanning family /
   pre-first-boot: current behaviour). Enable runs before the shell processes
   Run; Run runs before Display Properties can open, so first post-install
   boot already shows the full list, and each boot rescans (card/VRAM change
   self-corrects).
4. **Scan location = mini-VDD** (`src/minivdd32/loader.asm`). Replace the
   fixed 7-entry `V9xVbeModeList` (line 52-63) with: 4F00h → copy the mode
   numbers out of VideoModePtr FIRST (128-word staging cap; the pointer may
   target the scratch 4F01h will overwrite) → per number, 4F01h + minimal
   ring-0 admission (attributes 0x81 set, bpp in {8,16,24,32}) → cache cap
   64 entries (~1.7 KiB locked data) with mode number, linear stride, mask
   bytes. All subtle filtering stays in host-tested C. Serial markers per
   call kept. Family gating unchanged: vbe+ati scan from day one; **s3 stays
   `MiniVddVbeCollect=$false` until Stage D** (the Trio64 boot hang is fixed
   and root-caused — byte-aligned V86 alloc — so it can be re-enabled, but
   only after a soak; see docs/issues/2026-08-18-trio64-minivdd-boot-hang.md).
5. **Mini-VDD API v2** (bump `V9XMINI_API_VERSION`, equality check kept —
   mixed DRV/VXD pairs refuse cleanly, tier-0 traces `minivdd-no-api`):
   `FN_MODE_AT (0004h)` index → geometry/pitch/attr/bpp/model/number;
   `FN_MODE_MASKS (0005h)` index → RGB+reserved field pos/size;
   `FN_MODE_INFO` kept back-compatible; `FN_STATUS` ECX = scanned count.
6. **Runtime mode table**: new `src/display16/modes16.c`,
   `V9X_HW16_MODE v9x_mode_table[64]` in DGROUP (896 bytes) + count, built at
   LibMain (before `v9x_apply_mode(&modes[0])`, ddi.c:420). Merge rules (all
   in host-testable `src/common/vbe_modes.c`):
   - baseline family rows copied verbatim in order (modes[0]=640x480x8
     fallback and 640x400x8 Doom95 slot preserved, s3_hw16.c:38-40 contract);
   - scanned rows admitted by `v9x_vbe_scan_accept`: drivable (linear attr,
     packed/direct model, phys base ≥ 1 MiB), bpp in {8,16,24,32}, pitch =
     (linear stride ?: bytes/scanline) nonzero and ≤ WORD, pitch*height ≤
     VRAM from 4F00h TotalMemory×64KiB (available at build time — the 2 MB
     Trio64 never even lists oversized modes), w/h ≤ 4095;
   - dedupe on (w,h,bpp): scanned mode updates the matching baseline row in
     place with BIOS number/pitch/masks (kills stride-disagrees refusals by
     construction); non-duplicates append sorted bpp,width,height; overflow
     drops with a serial trace;
   - english values: `low = (W*254+320)/640`, `high = low/2` (reproduces
     254/318/407 and gives 508 for 1280 — verified against existing rows);
   - per-row RGB masks in a parallel array in modes16.c (chipset initializers
     untouched); canonical masks for baseline rows (8 palettized, 16 → 565,
     24/32 → 0x00FF0000/0x0000FF00/0x000000FF), BIOS-derived for scanned
     rows via new `v9x_vbe_masks_to_bits` (565 default if a 16bpp BIOS
     leaves them zero).
   - ddi.c repoints its `v9x_modes`/`V9X_MODE_COUNT` macros (ddi.c:69-70) at
     the runtime table; find/validate/select need no other edits beyond the
     ValidateMode VRAM check below.
7. **DirectDraw**: kill the hardcoded table in `ddhal_core.c:920-959`.
   `V9X_DD_MODE_COUNT` becomes capacity **32** + `DWORD mode_count`; bump
   `V9X_DD_SHARED_ABI` (win9x_ddraw_abi.h:949-950). dd16.c stamps
   `shared->modes[]` from the runtime table (subset rule: all 8bpp, all
   16bpp, then 24/32 by ascending area, cap 32; masks from modes16 per-row
   data — `v9x_dd_refresh_info` already reads per-row masks, dd16.c:249-254).
   DriverInit validates `0 < mode_count <= 32`, sets `dwNumModes`.
8. **CPU paths + accel policy**: widen accel guards `ddhal_core.c:722-723,
   787-788` to admit 24/32 (mandatory — DDCAPS_BLT means NOTHANDLED surfaces
   as DDERR_UNSUPPORTED, the CPU backstop must run); add 3-byte and 4-byte
   fill paths to `v9x_cpu_fill` (blt_cpu.c:106-160; copy path already
   byte-length safe). Both S3 engines return declined when
   bytes_per_pixel > 2 (Trio64 8514/A is depth-blind and would corrupt;
   ViRGE S3D 24bpp unverified — recorded follow-up). D3D untouched
   (already 565-gated, d3d_virge.c:373,443). Flip path: guard non-dword-
   aligned offsets before `v9x_set_display_start` (vga_scanout.c:36 programs
   dwords; candidate pitches are dword multiples so frame flips stay safe).
9. **Safety**: scan-time VRAM filter (above) + retained ValidateMode check
   (ddi.c:871): refuse a mode whose pitch*height exceeds
   `v9x_vbe_vram_reported` when known — covers static rows on non-scanning
   families and live switches on the 2 MB physical Trio64. 4F02h failure on
   a scanned mode: existing stage trace + VGA fallback (boot) / existing
   ReEnable previous-mode restore under Windows' 15-second revert applet
   (live). Per-mode quarantine list: follow-up, not built now.

## Stages (risk-ordered; each independently shippable)

### Stage 0 — Measure (no product code)
Run/refresh `tools/diag/vbe_inventory_dos.c` on 86Box ViRGE + Trio64 and
QEMU std-vga: dump 4F00h list + 4F01h detail (number, w/h, bpp, model,
stride, linear attr, masks) for 0x0107/0x0112/0x0115/0x0118/0x011A/0x011B
and any OEM (0x120+) entries. Record in a docs/decisions note: depth per
number per target; 1280x1024 availability. Gate: no baseline row for a
(number,depth) pair not seen in a dump.

### Stage A — Static 24/32 baseline (all families; zero new ring-0 code)
- mode.c depth gate; DIBENG three-way fork; ValidateMode VRAM check.
- CPU 3/4-byte fills; accel guards widened; S3 engines decline >16bpp;
  flip alignment guard.
- DD shared block: mode_count + capacity 32 + ABI bump; dd16 stamps from the
  (still static) family table; ddhal_core static table deleted.
- Baseline rows per Stage-0 probe: 24-or-32bpp at 640x480/800x600/1024x768,
  and 1280x1024 at 8/16(/24 if it fits and the BIOS lists it) — edited in
  lockstep across `src/chipsets/*/..._hw16.c`, `packaging/families/*/
  family.psd1` (Modes/ForcedModes/ModesSummary/Vm.Modes), one commit per
  family. 1280 rows need english 508/254.
- inf.ps1: drop the MODES\24 / MODES\32 prohibition (line 235).
- build-active-package.ps1: replace `ValidateRange(-1,5)` on -ForceModeIndex
  with a manifest-derived check (fixes existing off-by-one).
- New build-time cross-check script in scripts/lib: regex-parse each family's
  `V9X_HW16_MODE` rows and compare set-for-set against the manifest Modes,
  fail the package build on drift (closes the "C tables unchecked" gap).
- Tests: rewrite tests/host/test_main.c:63 (32bpp now OK; keep an odd-depth
  refusal e.g. 15bpp) and test_family_matrix.c:222 (4096x4096x32 now trips
  on memory — keep, plus an odd-bpp refusal); matrix covers new rows vs
  declared VRAM.

### Stage B — Dynamic scan, vbe family (QEMU) — widescreen appears here
- loader.asm: full-list walk + cache v2 + API v2 (decision 4/5).
- runtime.asm: `V9xMiniVbeModeAt`/`V9xMiniVbeModeMasks` callers (model on
  V9XMINIVBEMODEINFO, runtime.asm:340-388).
- vbe_parse.c mask fields + `v9x_vbe_masks_to_bits`; new vbe_modes.c
  (accept/merge/dd-subset) + tests/host/test_vbe_modes.c (QEMU-shaped list
  with OEM widescreen numbers, S3-shaped short list, pathological: no linear
  attr, 15bpp, pitch > 0xFFFF, VRAM overflow, >64 entries, missing 0xFFFF
  terminator, duplicate (w,h,bpp), 24-vs-32 baseline disagreement).
- modes16.c runtime table; ddi.c macro repoint; V9XMODES.INI producer at
  first enable-ok (WritePrivateProfileString, the API the driver already
  uses: `[Velocity9xModes] Count=N, Mode<i>=<bpp>,<w>,<h>`).
- V9xSyncModes in settings_propsheet.c (write/prune with V9xScanned marker);
  inf.ps1 persistent Run line + required-entry assertion.
- settings_status.c:354-355: supported-modes line built from V9XMODES.INI
  when present, baseline sentence otherwise.

### Stage C — ati family
Same binaries; enable and verify on the Rage targets via the cache dump.

### Stage D — s3 scan
Flip s3 `MiniVddVbeCollect=$true`; 86Box ViRGE+Trio64 soak (the many-mode
Exec_Int walk is the top risk); one physical-Trio64 (BARRY) boot pair with
serial capture before shipping. If BARRY objects, s3 stays baseline-static —
the architecture tolerates that indefinitely.

### Docs pass (with A and B)
README.md (modes table, "what it does"), docs/INSTALL.md:192, floppy script
text (build-floppy-package.ps1:181), CHANGELOG, a docs/decisions note for the
Stage-0 findings and the dynamic-discovery design.

## Explicit scope cuts
- No Bochs-dispi backend, no EDID/DDC, no CRTC/PLL timing generation, no
  refresh-rate selection (stays adapter-default 0/60).
- No hardware acceleration at 24/32 (ViRGE S3D 24bpp = recorded follow-up);
  D3D stays 16bpp-only.
- No per-mode failure quarantine list (follow-up).
- matrox-m2 untouched.

## Verification
- Host: full tests/host suite incl. new test_vbe_modes.c; C-table↔manifest
  check; inf.ps1 assertions incl. Run line.
- QEMU vbe: serial `vbe-scan n>7`; V9XMODES.INI lists OEM widescreen; after
  Run sync Display Properties offers them; pick widescreen 32bpp → enable-ok,
  gdi_smoke/palette_smoke, ddraw_probe CPU blits at 24/32, mode_switch cycles
  8↔16↔32 live; delete a scanned MODES key + reboot → key returns.
- 86Box s3 (ViRGE 9869, Trio64 9871): Stage A baseline 24bpp rows per probe;
  Doom95 640x400x8 regression; S3D declines at 24; D3D 16bpp unchanged.
  Stage D: scan output equals the vbe_inventory_dos dump.
- Physical Trio64 (2 MB): ValidateMode refuses 1024x768x24 live; forced
  attempt fails staged with trace + VGA fallback; after Stage D the mode is
  absent from the list entirely (scan-time filter).
- Mixed-ABI negatives: old DRV+new DLL → driverinit-pending; old VXD+new DRV
  → minivdd-no-api stage-3 trace; no crash either way.

## Critical files
- src/common/mode.c, src/common/vbe_parse.c, new src/common/vbe_modes.c
- src/display16/ddi.c, dd16.c, new modes16.c, runtime.asm
- src/display32/ddhal_core.c, blt_cpu.c, engines/eng_s3_trio.c,
  engines/eng_s3_virge.c, engines/vga_scanout.c
- src/minivdd32/loader.asm
- include/velocity9x/win9x_ddraw_abi.h (ABI bump), include/velocity9x/hw16.h
- src/chipsets/{s3,generic/vbe,ati}/..._hw16.c + packaging/families/*/family.psd1
- scripts/lib/inf.ps1, scripts/build-active-package.ps1, new cross-check script
- tools/diag/settings_propsheet.c, settings_status.c
- tests/host/test_main.c, test_family_matrix.c, new test_vbe_modes.c
