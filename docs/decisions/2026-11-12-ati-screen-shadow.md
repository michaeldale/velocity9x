# The screen shadow on Carrizo: GDI reads 30 to 77 times faster, windowed GL unchanged once read-backs are deferred and the direction flag is cleared

Date: 2026-11-12

Status: **measured, one machine.** M-XP7053's Radeon R7E (`1002:9874`,
Carrizo), remote help, 1024x768x16, write-combined, all builds
`5326aec6-dirty`, in this order:

| Boot | Build | Shadow |
|---|---|---|
| 52 | boot 52's (write-combining, no shadow) | not built in |
| 53 | shadow, engine blits read back at once ("eager") | on |
| 54 | the same files, `NOSHADOW.TXT` | off |
| 56 | read-backs deferred to GDI's next touch ("deferred") | on |
| 57 | deferred, cursor-exclusion brackets told apart ("split") | on |
| 58 | split, with the event log | on |
| 59, 60 | split, the direction flag cleared on entry ("final"; 60 counts it) | on |

Evidence: [`../probe/carrizo-shadow-2026-11-12/`](../probe/carrizo-shadow-2026-11-12/)
(probe reports per build in `./`, `deferred/`, `split/`, `eventlog/`,
`cld/` and `final/`; each windowed Quake 3 run with `qconsole.log`; every
frame-buffer comparison, with its event log, in `fbcmp/`), and dispbench
`docs/results/` `carrizo-main-wc/` (off), `carrizo-main-shadow/` (eager),
`carrizo-main-shadow-deferred/` (split) and `carrizo-main-shadow-final/`.

## Why

The performance review of 2026-11-12 found every GDI read on Carrizo at
8 MB/s: GDI has no engine on the Evergreen, VI and CIK chips
(`GdiAcceleration=none`), so screen-to-screen copies, scrolling and ROPs
that read the destination all read the frame buffer over the bus. The
vbe family's shadow fixed exactly that on the same AMD silicon
([phase 1](2026-11-11-vbe-screen-shadow-phase1.md),
[open items](2026-11-11-vbe-screen-shadow-open-items.md)). Michael's
call: "do shadow if it is going to be better than GDI/SDMA". SDMA could
take plain copies only; the shadow covers every read.

## The change

- **Per chip.** `V9X_HW16_DEVICE.screen_shadow` (appended). The vbe row,
  the five Evergreen rows, Carrizo and Mullins set it; Mach64 and the
  Rages draw GDI on their engines, which would bypass the shadow, and
  leave it clear (`ScreenShadow=off-chip`).
- **Segment.** The ati family builds with `V9X_SCREEN_SHADOW`. Its
  `_TEXT` went to 61,155 bytes against the 57,344 budget, so `edid.c`,
  `mtrr.c`, `shadow.c` and `shadow16.c` moved to a new code segment,
  LEAFCODE (`velocity9x/leaf16.h`): `_TEXT` 56,639, LEAFCODE 6,780 at the
  end. Entries called from `_TEXT` are `V9X_LEAF_FAR`. The runtime's
  `__U4M`/`__U4D` live in `_TEXT` and cannot be called from another
  segment (wlink E2052, as for I9XXCODE), so the four 32-bit multiplies
  became inline 16 x 16 `MUL`s (`V9X_LEAF_MUL16`) and MTRR's power-of-two
  remainder a mask. `v9x_shadow_span` refuses a pitch over 16 bits.
- **Stale rectangle, not a read-back.** An engine fill onto the visible
  screen is repeated in the shadow, and so is a screen-to-screen copy
  whose source is current there. A copy from off-screen memory (a
  windowed present) and an Unlock of the primary only add their
  rectangle to `V9X_DD_SHARED.shadow_stale`; the frame buffer stays the
  truth there. The 16-bit side reads stale pixels back when GDI touches
  them: a drawing access that touches the stale rectangle reads all of it
  back and clears it (`v9x_shadow_stale_take`), so no later access in
  the same call reads over drawing not yet copied out; a union about to
  be copied out does the same first; the cursor reads back only its own
  overlap (`v9x_shadow_stale_overlap`), from new hooks the cursor thunks
  call before the DIB Engine. `V9xShadowFetch` (runtime.asm) is the copy.
- **Cursor exclusions.** A `deBeginAccess` without `FB_ACCESS` (DIBENG.INC:
  124-127) only excludes the cursor: DirectDraw's around a blit or Lock of
  the primary (`deCursorExclude` is this entry), and the DDK samples' own
  engine drawing (`s3v\RECT.ASM`, `framebuf\SPECIAL.ASM`). For those only
  the cursor's rectangle is read back and copied out.
- **V9XFBCMP**, a new probe (`tools\diag\fbcmp_probe_win32.c`): reads the
  screen through GDI (the shadow) and through a DirectDraw Lock of the
  primary (the frame buffer) and counts the pixels that differ, with
  images of the box that does, and the shadow's event log (below). An
  agent screenshot reads through GDI and so cannot see a frame buffer
  behind the shadow. `/delay` waits 15 s first.
- **Event log and direction flag**: see the two sections after Windowed
  OpenGL.

Host tests written first and seen failing: the pitch refusal, and the
two stale functions (nothing stale, disjoint, partial, covering, empty;
the cursor's overlap). Gates: `build-host.ps1` and `run-checks.ps1`
green on each build, binary audits included. V9XD32 (32 and 16 bpp),
V9XGLP, V9XGLIDP (0 failures) and V9XR3DP pass on boots 53, 56, 57, 58,
59 and 60.

## GDI (dispbench, off on boot 52, on on boot 53)

| Measure | Off | On |
|---|---:|---:|
| ScrollDC | 8.2 MB/s | 632.5 MB/s |
| ScrollBitBlt | 8.1 | 638.7 |
| Screen to screen, 256x256 | 8.1 | 242.1 |
| Screen to screen overlapping, 256x256 | 7.8 | 499.2 |
| Screen to bitmap, 1024x768 | 8.1 | 620.6 |
| SRCAND / SRCINVERT, 64x64 | 4.1 | 65.9 |
| StretchBlt down, 128x128 | 1.7 | 10.3 |
| FillRect 1024x768 | 2,165 | 751 |
| Bitmap to screen, 1024x768 | 1,235 | 602 |
| ExtTextOut, fixed opaque | 51,251 cyc | 92,307 cyc |
| Line, diagonal | 29,576 cyc | 57,292 cyc |

Reads 30 to 80 times faster at 64x64 and up, 8x8 copies about even;
write-only drawing at 0.35 to 0.8 of its speed, text 1.25 to 1.8 times
the cost, lines 1.4 to 1.9: the shape the vbe machines showed. The split
build on boot 57 gives the same within a few per cent (ScrollDC 631.6,
screen to screen 241.2, SRCAND 61.9; text 4 % and lines 9 % dearer than
boot 53), but that run overlapped two V9XFBCMP runs by mistake, so its
small differences are not taken as measured. DirectDraw's rows
(exclusive mode, which the shadow skips) are within 6 % of boot 52's on
every build.

## Windowed OpenGL

The Quake 3 demo timedemo, as at boots 38 and 52 but with
`+set r_fullscreen 0` (640x480 in a window on the 1024x768x16 desktop),
the owner's `q3config.cfg` restored after every run (SHA-256
`FD8C261A...` each time):

| Boot | Build | Result | `CL_InitCGame` |
|---|---|---|---|
| 54 | shadow off | 1346 frames, 72.1 s: **18.7 fps** | 4.88 s |
| 53 | eager | 528.5 s: **2.5 fps** | 15.96 s |
| 56 | deferred | 268.9 s: **5.0 fps** | 9.59 s |
| 57 | split | 72.0 s and 72.1 s: **18.7 fps** (two runs) | 6.31 s |
| 59, 60 | final | 72.0 s, 71.9 s, 72.0 s, 72.1 s: **18.7 fps** | - |

All of them drew every batch on the engine. Eager read each present
back, 340 ms a frame. Deferred still cost 146 ms a frame: DirectDraw
brackets every blit to the primary with a cursor exclusion, and treated
as drawing, each one read the whole stale window back and copied it out
again. With exclusions told apart the shadow costs windowed GL nothing
measurable. Fullscreen Quake 3 is exclusive mode and does not reach any
of this; it was not rerun.

## A tray icon that differed: the probe, not the shadow

On boots 56 and 57 V9XFBCMP (run detached with a 15 s delay so the agent
is idle, the cursor parked at 600,300) found 91 pixels at 946,752 to
959,762 different after windowed Quake 3 and not after a PING job, and
hovering there made them equal again. That box is the remote agent's
tray icon: a green variant in the frame buffer, white through GDI.

The event log (`V9X_DD_SHARED.shadow_log`, written by shadow16.c and the
HAL, dumped by V9XFBCMP) settled it on boot 58. The taskbar was redrawn
again and again - the Start button, then that icon's box - and every
redraw was a `FB_ACCESS` bracket whose union was copied out
(`begin 0009 944,747,975,762`, `end`, `copy 944,747,976,763`). The icon
blinks after an agent job until the pointer passes over it, and the probe
read GDI and then the frame buffer, about 100 ms apart. V9XFBCMP now
reads GDI again after the frame buffer and counts a pixel the two GDI
reads disagree on as changing, not different: on boot 58 the same box
was `Changing=91`, `Differ=0`.

## A real fault: the direction flag

While reproducing that, Quake 3 faulted on boot 58 during DirectInput's
start: a general protection fault in V9XDISP.DRV at 0003:1068, LEAFCODE,
on the near `RET` that ends `v9x_shadow_note`. The stack held the DIB
Engine's far frame into `V9xShadowEndAccess` where a near return address
should have been, and the BP chain led to `v9x_shadow_take_stale`. Watcom
copies small structures with `MOVSW` and assumes the direction flag
clear, as C code may; `v9x_shadow_widen` copies a rectangle that way into
the lowest local of its caller, so with the flag set its four `MOVSW`s
run downwards over its own return address and saved registers. The copy
loops in runtime.asm have always begun with `CLD`; the C added for the
stale rectangle is the first shadow code with string instructions
(today's objects: the 0.46.0 functions of shadow.c compile without any).

Every entry the DIB Engine or the mouse interrupt calls now saves the
flags, clears the direction flag and restores the flags on return, and
counts an entry that arrived with it set
(`V9X_DD_SHARED.shadow_direction_set`). Boot 60, in order, nothing else
running:

| After | Direction set (cumulative) | V9XFBCMP |
|---|---:|---|
| boot, idle desktop | 0 | equal (`Changing=91`, the icon) |
| windowed Quake 3: 18.7 fps | 7 | equal |
| windowed Quake 3 again: 18.7 fps | 14 | equal (`Changing=91`) |
| dispbench DBGDI and DBDD | 21,159 | equal |

So callers do enter with the flag set, which fits the fault's frame. Not
explained: dispbench on the split build (boot 57) made thousands of such
entries without faulting. The count does not say which entries they were;
the cursor hooks after a DIB Engine call use no string instructions, and
most may be those. Boot 59 (the fix without the count) ran Quake 3 twice
without a fault, as boot 60 did; before the fix one run in about four
faulted, so two clean runs are evidence, not proof.

## Final GDI numbers (boot 60, dispbench `carrizo-main-shadow-final`)

| Measure | Off (boot 52) | Final |
|---|---:|---:|
| ScrollDC | 8.2 MB/s | 631.1 MB/s |
| Screen to screen, 256x256 | 8.1 | 240.6 |
| Screen to screen overlapping, 256x256 | 7.8 | 492.1 |
| Screen to bitmap, 1024x768 | 8.1 | 621.5 |
| SRCAND, 64x64 | 4.1 | 64.1 |
| StretchBlt down, 128x128 | 1.7 | 10.3 |
| FillRect 1024x768 | 2,165 | 749 |
| Bitmap to screen, 1024x768 | 1,235 | 611 |
| ExtTextOut, fixed opaque | 51,251 cyc | 87,743 cyc |
| Line, diagonal | 29,576 cyc | 66,072 cyc |

A diagonal line costs 2.2 times its uncached-read figure here, against
1.9 on boot 53; every bracket now also runs the stale check and the log,
which is the likely cost, not measured on its own. DirectDraw's rows are
within 1 % of boot 52's.

## The other machines (afternoon, committed build 7da1bcb1)

Evidence: [`../probe/shadow-ati-vbe-2026-11-12/`](../probe/shadow-ati-vbe-2026-11-12/)
and dispbench `m-jjn1zz-r3e-v9x-ati-shadow/`, `m-jjn1zz-r3e-v9x-vbe-shadow/`.
Trinity (M-D7TFW4) and every vbe machine in the release list were
offline, so the vbe set went onto M-JJN1ZZ's Radeon R3E (Mullins) by
WININIT over its ati set, as on 2026-11-11, and the ati set went back
after (boots 36 to 39).

| M-JJN1ZZ | ati | vbe |
|---|---|---|
| `ScreenShadow` | on | on |
| V9XD32, V9XGLP, V9XGLIDP, V9XR3DP | all pass | V9XD32 FAIL (below), the rest pass |
| V9XFBCMP after the probes | equal | equal |
| V9XFBCMP after dispbench | equal (8,697 direction-set entries) | equal (47,751) |
| FbWrite | 31.7 MB/s | 2,512 MB/s |
| ScrollDC | 30.3 MB/s | 1,157 MB/s |
| Screen to screen, 256x256 | 7.8 | 382 |
| FillRect 1024x768 | 30.9 | 1,220 |
| ExtTextOut, fixed opaque | 410,017 cyc | 30,732 cyc |

- **vbe, with the shadow, against its 2026-11-07 run without it**
  (`m-jjn1zz-r3e-v9x-vbe`, write-combined): ScrollDC 4.7 to 1,157 MB/s,
  overlapping copy 4.7 to 783, SRCAND 2.5 to 100; FillRect 0.49 of its
  speed, text 1.8 and diagonal lines 3.2 times the cost.
- **ati on Mullins is not write-combined.** Its hw16 row leaves
  `write_combine` at 0 (Stage A), so the frame buffer is written
  uncached at 31.7 MB/s, and with the shadow every GDI call copies out
  to it: about 30 MB/s for everything. Carrizo was the same until
  5dfe395c. No ati run without the shadow exists on this card, so
  whether the shadow helped or hurt Mullins is not known.
- **Mullins write-combined (boot 40).** The row now opts in with
  `V9X_HW16_WC_MAPPING`, as Carrizo's: the same eng_vi.c and vi_cp.c
  drain before every SDMA and CP kick, and the plan was the one Carrizo
  had (`Mtrr=... r=0 s=5 b=e0000000 z=01000000`, `MtrrWrite=ok`). All
  five probes pass, V9XFBCMP equal after the probes and after dispbench,
  DBDD completed (`m-jjn1zz-r3e-v9x-ati-wc-shadow`):

  | Mullins, ati | uncached | write-combined | vbe |
  |---|---:|---:|---:|
  | FbWrite | 31.7 MB/s | 2,505 | 2,512 |
  | ScrollDC | 30.3 | 1,168 | 1,157 |
  | Screen to screen, 256x256 | 7.8 | 382 | 382 |
  | FillRect 1024x768 | 30.9 | 1,235 | 1,220 |
  | ExtTextOut, fixed opaque | 410,017 cyc | 28,286 cyc | 30,732 cyc |
  | DirectDraw vid-to-vid, 256x256 | 520 | 519 | 4.7 |
  | DirectDraw sys-to-vid, 512x512 | 62.4 | 2,451 | 2,467 |

  GDI under ati now matches vbe on the same card, and DirectDraw keeps
  its engine.
- **V9XD32 on vbe: T22 draws white.** Create, lock, handle and draw all
  return `0x00000000` and every T22 pixel is `0x00FFFFFF`, two failures.
  T22 dates from 2026-11-11 (Gen3 32-bit textures); no earlier vbe run
  had it. Not the shadow's: GDI only. Later the same day: the software
  engine refused the 32-bit formats and drew vertex colour; it samples
  them now, and T22 passes
  ([record](2026-11-12-vbe-soft-32bit-textures.md)).
- **DBDD died on ati** after writing its last test (`Dd.ScanLine`),
  before `[Result]`; on vbe it completed. DBDD stopped early on this
  machine on 2026-11-07 too, before any shadow (after `Dd.Flip`).
  Recorded, not rerun.

## Not covered

- Trinity and the Evergreen cards: opted in by the same design, none
  run (Trinity offline on 2026-11-12).
- 32 bpp, Windows 95, a full-screen DOS box with the shadow on this card.
- Windowed Direct3D and windowed DirectDraw programs other than the GL
  ICD's presents.
- A windowed program that GDI draws over every frame: each touch reads
  the stale window back.

M-XP7053 is left on the final build (boot 60) with the shadow on.
