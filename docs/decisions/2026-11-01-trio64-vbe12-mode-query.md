# The `s3` family asks a VBE 1.2 BIOS which modes it has

Date: 2026-11-01
Status: implemented, host-tested; not yet run on a guest or a card

## The defect

The `s3` table is static and shared (`src\chipsets\s3\s3_hw16.c`). `ValidateMode`
measures a row against the card's VRAM, but it cannot know whether the BIOS has
the mode: a row the BIOS lacks validates and then fails at 4F02h, with the
staged failure, the fall back to 640x480 and the modal Display applet that
`2026-08-26-s3-physical-pipeline-inert.md` section 8 records for 0x0100 on
BARRY.

The runtime merge (`2026-10-04-trio3d-8904-on-the-virge-path.md`) already hides a
baseline row that a trusted scan contradicts (`v9x_vbe_publish_rows`), and it
admits S3 records without linear attributes under
`V9X_VBE_ADMIT_FLAG_APERTURE_KNOWN`. It never ran on a Trio64 or Trio64V+: the
mini-VDD's collection stopped at its ring-0 check for VBE 2.0, and every Trio64
and Trio64V+ BIOS measured is VBE 1.2 (`2026-08-20-vbe-mode-inventory.md`).

## A common card whose BIOS lacks two rows

The Diamond Stealth64 Graphics 2000 (Trio64,
https://theretroweb.com/expansioncards/s/diamond-stealth64-graphics-2000-trio64),
BIOS "Stealth 64 DRAM Vers. 2.05" of 04/27/95 (VBE OEM string "S3 Incorporated.
Vision864", like BARRY's), run in the trio64-vbios-tools emulator (Trio64
register model, 2 and 4 MiB, 1 November 2026):

- 4F00h: version 0102h; the list is 0x0100-0x0107, 0x0109, 0x010A,
  0x0110-0x0117 and 0x0211, the same at 2 and 4 MiB;
- 0x0118 (1024x768x32) and 0x011A (1280x1024x16) are not listed, and 4F02h
  answers 014Fh for both at 2 and 4 MiB;
- every listed mode answers 4F01h.

At 2 MiB `ValidateMode` refuses both rows for want of memory. On a 4 MiB card
they pass it and fail at 4F02h. No physical 4 MiB Diamond card ran this.

## What the change gains

- On a card whose BIOS lacks a row, the row is hidden from GDI, DirectDraw and
  Display Properties instead of failing at 4F02h: 1024x768x32 and 1280x1024x16
  on a 4 MiB Stealth64 Graphics 2000, 640x400x8 on BARRY.
- The rows stay published on every card whose BIOS has them. Section 8 of the
  2026-08-26 record left 640x400x8 unresolved because removing the row costs the
  cards that have it, and keeping it leaves BARRY offering a mode it cannot set;
  it named per-device row admission as the probable fix. The scan is that
  admission, decided by each card's own BIOS.

## The change

1. `src\minivdd32\loader.asm`: the 4F00h check accepts VBE 1.2 and later
   (`cmp ax, 0102h`). 1.2 is the first version whose ModeInfoBlock is complete
   (VBE 1.2, section 6.3: "Version 1.2 of the VBE requires that all fields of
   the ModeInfoBlock contain valid data"). Its host-tested twin,
   `v9x_vbe_parse_controller_info` in `src\common\vbe_parse.c`, moves to 1.2 with
   it, so the two still match.
2. `v9x_vbe_controller_scan_usable` (`src\common\vbe_modes.c`): 2.0 and later
   serve every family; 1.2 serves only a family with `APERTURE_KNOWN`; below 1.2
   nobody. `src\display16\modes16.c` treats a refused controller as scan state 3,
   as a missing controller was.
3. `v9x_vbe_scan_may_contradict_version` (`src\common\vbe_cache.c`): for a 1.x
   controller a refused 4F01h on a listed mode no longer withdraws the scan's
   authority to hide rows. VBE 1.2, section 6.2, says listed modes "may not be
   available due to the video board's current memory and monitor configuration"
   and leaves it to the application to verify each through 4F01h. BARRY's BIOS
   lists 0x0118-0x011B, answers 014Fh to 4F01h for them and refuses them at 4F02h
   as well (2026-08-26 record, section 7); before this, those four answers kept
   its scan from hiding 0x0100. For 2.0 and later the rule is unchanged, and
   every other completeness rule (terminated list, no overflow, no cache or query
   limit) still applies. `modes16.c` carries the same rule inline, as it did
   before: no family's 16-bit driver links `vbe_cache.c`, so a call to the
   function there does not link (found in review, PR 19), and the inline form
   is the smaller one in a 16-bit code segment that is close to its budget
   (intel-gma). The function and its host tests stay as the tested statement
   of the rule; the comment in `modes16.c` names it as the twin.

The risk this accepts: a 1.x BIOS that refuses 4F01h for a mode it would still
set with 4F02h would lose that mode's row if no other record describes its
geometry. None of the 1.2 BIOSes measured does this.

## Side effects on other families

Families on VBE 1.2 BIOSes now get the 4F01h collection at boot (one `Exec_Int`
per listed mode, as 2.0 BIOSes always did) and a valid controller record, so
`enable16.c` takes their 4F00h TotalMemory as the VRAM fallback when the family
has no `read_video_memory` hook. Their scan stays refused by
`v9x_vbe_controller_scan_usable`, so their mode tables are unchanged, and the
tier-0 aperture default still finds no linear record (its trace now reads
`4f01-mode-rejected` instead of `minivdd-no-mode`).

## Gates

- `tests\host\test_vbe_modes.c` (`test_controller_scan_usable`) and
  `tests\host\test_vbe_cache.c` (`test_may_contradict_version`) cover the two
  rules. Run here with gcc in C89 mode against a 32-bit `v9x_u32`, not with Open
  Watcom: 0 failures.
- Not run: `check-tree.ps1`, `build-host.ps1`, `run-checks.ps1`, an 86Box guest,
  a physical card.

## Amended at merge, 2026-11-04

Michael's review (2026-11-01, 86Box) and a second review at merge found
three things, fixed before merging:

- **The 16-bit driver did not link** (`modes16.c` called a `vbe_cache.c`
  function no 16-bit driver links). The rule is written out inline in
  `modes16.c` (`593caf37`); `v9x_vbe_scan_may_contradict_version` stays
  as its host-tested twin.
- **A 1.x scan no longer adds rows.** On 86Box's 4 MiB Trio64 (VBE 1.2)
  the scan added 1600x1200x8 (0x120), which that BIOS sets interlaced
  (99 Hz fields, about 50 frames a second). `v9x_vbe_scan_may_append`
  (`vbe_modes.c`, host-tested) lets a 1.x scan correct and hide the
  family's own rows but not append others. Which further modes a Trio64
  drives non-interlaced is left to a pixel-clock rule measured on real
  cards.
- **Only 014Fh counts as "not available".** Ring 0 had set
  `QUERY_FAILED` for every 4F01h answer other than 004Fh, and the 1.x
  rule waived all of them, so a BIOS that does not handle 4F01h at all
  (AL not 4Fh) would have given a trusted, empty walk. A new status bit,
  `V9X_VBE_ST_QUERY_UNAVAIL` (8000h), records an exact 014Fh; only it is
  waived below VBE 2.0. At 2.0 and later both still withdraw the scan,
  as before.

The intel-gma `_TEXT` segment, 11 bytes over budget on the PR's base,
has room on main since the Haswell layout code moved to its own segment
(`2026-11-04-gm45-gl40-from-a-survey.md`).

Gates at merge: `check-tree.ps1`, `build-host.ps1` and `run-checks.ps1`
on the merged tree. 86Box results are added below when run.
