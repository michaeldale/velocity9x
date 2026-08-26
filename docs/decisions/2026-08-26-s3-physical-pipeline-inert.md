# The dynamic VBE pipeline is inert on physical S3 silicon

Date: 2026-08-26
Status: accepted

Target: BARRY, physical S3 Trio64 (86C764), `PCI\VEN_5333&DEV_8811`, 2 MiB VRAM,
Windows 98 SE 4.10.2222, 32 MB RAM. Reached over the remote agent at
`10.0.1.47:9869`. Driver 0.5.0, build `edd7684`, deployed over the 0.4.3 that
was installed (`docs\plans\s3-physical-pipeline-validation.md`).

This is the first time the dynamic VBE pipeline - Stages 1 to 5 of
`docs\plans\dynamic-vbe-pipeline.md` - has run on physical S3 silicon. It
records what the run proved, one pre-existing table defect it exposed, and two
comments in the tree it settles.

## 1. A VBE 2.0 S3 board, not BARRY, is the prerequisite for S3 collection

This is recorded first because it is the question the run was expected to
reopen, and a clean BARRY result must never be mistaken for clearance.

`MiniVddVbeCollect` stays `$false` for `s3`. The reason is **zero benefit, not
missing validation**, and two independent facts each make the collection
worthless on this family:

- **The cache is never consulted.** The `s3` family has a `read_aperture` hook,
  so `v9x_vbe_default_aperture` - the only reader of the 4F9Ch cache - is never
  reached (`docs\decisions\2026-08-18-minivdd-vbe-collect-gating.md` §2).
  Enabling collection buys eight `Exec_Int 10h` boot calls on the exact card
  that hung from that code path, and nothing else.
- **Two filters would refuse every mode it found.** All four measured S3
  targets are VBE 1.2 with every mode at attribute `001B` (bit 7 clear),
  `PhysBasePtr=0` and `LinBytesPerScanLine=0`
  (`docs\decisions\2026-08-20-vbe-mode-inventory.md`, finding 2). The
  controller gate requires at least `0x0200` (`src\common\vbe_parse.c:151`) and
  `v9x_vbe_scan_accept` requires the linear attribute plus a physical base at
  or above 1 MiB. Zero modes would be discovered.

BARRY reports **VBE 1.2** (`Version=0102`,
`docs\decisions\2026-08-20-vbe-inventory-barry.txt`), so it also sits on the
milder half of the conformance corpus, whose headline defects are 2.0-weighted:
the lost 8x14 font hits all four 2.0 BIOSes and none of the 1.2 ones, and the
360-wide FIFO fault hits only the two newest 2.0 BIOSes
(`docs\specifications\dos-vbe-conformance.md:98-110`). A clean run on a 1.2
board is therefore not evidence about the 2.0 boards where the corpus says the
risk lives.

**The honest prerequisite for ever enabling `MiniVddVbeCollect` on `s3` is a
VBE 2.0 S3 board, which nothing in the fleet has.** No collection-enabled
experiment build was made.

### There are no extra resolutions to be had here anyway

The obvious follow-up question is whether a scan would give this card the extra
modes the pipeline releases on tier-0 - 46 where the baseline named 7, on QEMU.
It would not, and the survey settles it by measurement rather than by argument.
Comparing the 2026-08-26 survey's mode list against the `s3` table, the sets of
distinct **resolutions** are identical:

```
BARRY's BIOS (graphics, >= 8 bpp): 640x400 640x480 800x600 1024x768 1280x1024
the s3 baseline table:             640x400 640x480 800x600 1024x768 1280x1024
```

Nothing this BIOS knows about is missing from the table. What it lists and the
table does not carry is four *depth* variants, and neither kind is a win:

| Mode | Geometry | Why not |
|---|---|---|
| `0110` | 640x480x15 | 15 bpp is refused by policy - `v9x_vbe_scan_admit` |
| `0113` | 800x600x15 | admits 8, 16 and 32 only, so a scan would drop |
| `0116` | 1024x768x15 | these even on a fully compliant BIOS |
| `0211` | 640x400x32 | the one real gain, and unreachable by construction |

`0x0211` is the interesting one and the irony is already on record: the mode
inventory called it "the only direct evidence so far that walking the list finds
modes enumerating fixed numbers cannot - and it is on the one target where the
scan can never run" (`2026-08-20-vbe-mode-inventory.md`). It stays out of reach.

**The discrepancy on this card runs the other way, which reframes what the
pipeline would be for on `s3`.** The table carries three rows BARRY's BIOS does
*not* list: `0100` (section 8's defect), and `0118` and `011A`, which the BIOS
answers `014F` to. So `s3` does not want a longer table, it wants a shorter and
more honest one - the pipeline's value here would be its **scan-contradicted
hiding**, not its merging. That does not rescue BARRY, whose scan can never be
valid enough to be trusted for hiding, but it is the right expectation to carry
into any future VBE 2.0 S3 board: pruning, not adding.

## 2. The pipeline is present and contributes nothing

Two clean boots, boot counters 44 and 45. `C:\V9XMODES.INI` was byte-identical
across both except its `Generation` counter:

```
Complete=1
Schema=1
Build=edd7684
Family=s3
Bios=unavailable
Vram=reported=0 usable=2097152
Edid=none
Recommendation=none reason=no-edid
Scan=state=3 listed=0 queried=0 cached=0 flags=0800
Table=rows=12 published=12 first=0 dropped=0
Reasons=0,0,0,0,0,0,0,0,0,0,0,0,0
```

All twelve `RowNN` entries carry `src=baseline`. There is no `HiddenNN` key and
no `src=dynamic` row. This is the real-silicon instance of Stage 2's exit gate -
that a scan-disabled build is byte-for-behaviour equivalent to the static
baseline path - which until now was proven only in host tests and on QEMU.

`state=3` is "baseline, scan invalid": `V9xMiniVbeStatus()` answered, and the
`V9X_VBE_ST_CTRL_VALID | V9X_VBE_ST_LIST_VALID` pair was absent
(`src\display16\modes16.c:226-236`), so the scan was refused before a single
record was read. `flags=0800` is exactly `V9X_VBE_ST_COLLECT_OFF`
(`include\velocity9x\vbe_cache.h:80`) and nothing else.

### Why `flags=0800` stands in for the serial capture

The plan asked for a COM1 capture across both boots carrying
`V9X-MINI vbe-collect disabled` and no `vbe-collect start`, `vbe-call fn=` or
`vbe-status ptr=` line. **That capture was not taken**: BARRY is physical, the
mini-VDD writes straight to the UART at `Device_Init`, and nothing on the
machine reads it back. Two substitutes were taken instead, and between them
they are stronger than the serial line for the "no new BIOS calls" claim:

- **Build-time, proving the code is absent.** `build-minivdd-skeleton.ps1:258-266`
  requires the `vbe-collect disabled` marker in a `-DisableVbeCollect` image and
  *forbids* it in a default one, so its presence in the shipped
  `V9XMINI.VXD` is exclusive proof that `V9X_NO_VBE_COLLECT` was defined and
  `V9xMini_Vbe_Collect` was assembled out. The build log also printed
  `no probe list (collection assembled out)`.
- **Run-time, proving the collection never ran.** `flags=0800` is the mini-VDD's
  own status word read back through 4F9Ch by the 16-bit side. `V9X_VBE_ST_COLLECT_OFF`
  is set at assembly time in the no-collect variant
  (`src\minivdd32\loader.asm:63-67`) and no other bit is set, so nothing
  collected anything.

One trap worth recording, because it makes a weaker check look like a strong
one: **the `vbe-collect start`, `vbe-collect done`, `vbe-call` and `vbe-status`
strings are present in a no-collect image.** `build-minivdd-skeleton.ps1:60-69`
emits them unconditionally; only the disabled line at 96-97 is conditional.
Grepping a VXD for `vbe-collect start` therefore proves nothing. Only the
*disabled* marker discriminates.

## 3. No synchronizer artefacts, measured against a pre-deploy baseline

A scan-disabled family's INF emits neither the `V9xFamily` marker nor the
`Run,V9xSyncModes` line, and `scripts\lib\inf.ps1:431,439` asserts both absent
at build time. The installed side was confirmed by exporting the same two
registry trees before the deploy and after the second boot:

- `HKLM\Software\Microsoft\Windows\CurrentVersion\Run` - **byte-identical**,
  and carries no `V9xSyncModes` value.
- `HKLM\System\CurrentControlSet\Services\Class\Display` - **byte-identical**,
  and carries no `V9xFamily`, no `V9xDynamic` and no `V9xSyncGeneration`
  anywhere in the tree, on either display instance.

The absences are measured, not assumed. That mattered: without the pre-deploy
export there is no way to tell a marker that was never written from one that was
written and then removed.

## 4. The `V9xSyncModes` dry run stops where the design says it should

BARRY carries a stale Cirrus `Display\0000` beside the Velocity9x
`Display\0001`, which makes it the "ambiguous devnodes" case in Stage 4's test
list - tested until now only on a clean QEMU guest with one instance.

```text
rundll32.exe v9xsetp.dll,V9xSyncModes report
```

The dry run mutated nothing and named its reason:

```
[Velocity9xSync]
Build=edd7684
DryRun=1
Status=no-op
Reason=no-marked-instance
```

`DryRun=1` confirms the substring scan for `rep` fired
(`tools\diag\settings_syncmodes.c:648-660`), so the class key was opened
`KEY_READ` only and nothing could have been written. The registry diff in
section 3 is the independent confirmation that nothing was.

**Which reason appeared was the finding, and it is the right one.**
`v9x_load_inventory()` runs before `v9x_find_marked_instance()`, so the two
outcomes are distinguishable: `inventory-missing-or-incomplete` would mean the
inventory was rejected, `no-marked-instance` means the inventory loaded and
validated - twelve rows, published count agreeing, generation present - and the
run then stopped because no display instance carries a `V9xFamily` value. That
is exactly correct for a scan-disabled family whose INF deliberately emits no
marker, and it means the stale Cirrus `Display\0000` was never a hazard here:
the walk found *zero* marked instances rather than being confused between two.

The "ambiguous devnodes" case therefore remains **partly** covered. BARRY proves
the zero-marked-instances branch on a machine that genuinely has two display
instances. It does not exercise `multiple-marked-instances`
(`settings_syncmodes.c:333`), which needs two instances that both carry the
marker and so needs a scanning family on a two-instance machine.

Two operational notes for the next run of this:

- Read the report with a guest-side `TYPE`, **not** with the agent's `get`.
  `get` returned a 43-byte file - `Build` and `DryRun` only - twice, 12 and 27
  seconds after the run, while `TYPE` on the guest showed all five lines. The
  Win9x profile-cache flush the plan warns about does not settle on a timer
  here; the directory entry stays stale and `get` faithfully copies a short
  file. `TYPE` reads current content.
- `shell` returned in 1.1 s, which is a complete run, not a truncated one - the
  dry run bails at instance matching before touching anything slow.

## 5. A real monitor: BARRY's ROM has no DDC at all

Every EDID this project had parsed before today was QEMU's synthetic block, and
even that was unstable - preferred 1280x800 in the 2026-08-23 DOS survey against
1024x768 at the Stage 5 guest boot. BARRY drives a real CRT, so
`tools\diag\vga_survey_dos.c:1448` was run against it: the 4F15h BL=00h
capability probe first, then BL=01h for block 0. Query-only, no mode set, no
driver involvement. The full report is
`docs\decisions\2026-08-26-vga-survey-barry.txt`, beside the existing inventory
dump.

```
[EDID]
Status=unsupported
Reason=vbe-4f15-capabilities-returned-4F00
```

**DDC is absent**, which is the outcome the plan called likely on a VBE 1.2 S3
ROM. The capability probe returns `4F00` rather than the `004F` a supported
function gives, so the tool stops before attempting a block read - there is no
EDID here to be had. This is now recorded on real silicon rather than assumed,
and it is consistent end to end: the driver's own inventory says `Edid=none` and
`Recommendation=none reason=no-edid`.

**No EDID host fixture is added.** That deliverable was conditional on DDC
answering and the condition was not met; `tests\host\test_edid.c` is unchanged.
The first real EDID block will have to come from another machine.

The same run re-dumped the mode list, and the 2026-08-20 fixture **holds
exactly**: 18 modes, `Version=0102`, terminated, no overflow, `0x0211`
(640x400x32) present and still unique to this target. Every graphics mode
reports attribute `001B` - bit 7 clear - with `PhysicalBase=00000000` and
linear bytes-per-scanline 0, which is the concrete form of the argument in
section 1 that a collection here would discover nothing.

Two incidental observations worth having on record:

- `OemString=S3 Incorporated. Vision864` on a card whose PCI id is
  `5333:8811` and whose devnode reads `ChipType=Trio64(86C764) Revision=C`.
  Whether that is a shared S3 BIOS string or genuinely an older ROM image, it
  fits the pattern of this target's short mode list, and it is the most likely
  explanation for why `0x0100` and `0x0118`/`0119`/`011A`/`011B` are missing
  here but present on both emulated 4 MiB S3 targets.
- The fresh dump **again has no `Mode.` entry for `0x0100`**, six days after the
  first inventory and through a different tool. That independently reconfirms
  section 8.

## 6. Text mode returns intact - but the 8x14 font was never exercised

Entering a DOS box from a freshly booted 800x600x32 desktop - a text-mode entry
after the driver's own mode set - gives a clean and complete text state:

```
VideoMode=03  TextColumns=80  TextRows=25  CharacterHeight=16
Crtc.00=5F4F50825581BF1F004F0D0E00000000
Complete=yes
```

The CRTC bytes are **byte-identical** to the earlier run in the same session,
the survey printed readable text throughout, and the whole report completed. Ten
mode sets and seventeen driver enable/disable cycles left text mode exactly where
it started.

**The 8x14 font specifically was not tested, and cannot be from here.** The
corpus defect is the loss of the 8x14 font, and Windows 98's mode 03h on this
machine runs the **8x16** font: `CharacterHeight=16`, and CRTC 09h reads `4F`,
whose low five bits give a maximum scan line of 15. The 350-line configuration
that uses 8x14 is never entered, so there is nothing here for the defect to
damage. That the font survives therefore remains **inferred** from the corpus -
BARRY is VBE 1.2 and the defect is 2.0-only
(`docs\specifications\dos-vbe-conformance.md:98-110`) - not measured. Measuring
it needs either a 350-line text mode entered deliberately or a font-plane dump,
and neither exists as a tool today. Note also that the agent's `screenshot` reads
the GDI primary, so a full-screen text screen cannot be captured for visual
comparison at all.

One anomaly observed and deliberately not investigated, recorded so it is not
rediscovered as a mystery: running the same survey in a DOS box **while the
driver held a live-switched mode with the unrepainted desktop** produced a
zero-byte report - the tool printed its banner and then stopped before its
"Collecting" line. Repeating it after a clean reboot works every time. This is
plausibly a further symptom of
`docs\issues\2026-08-20-live-mode-switch-no-repaint-barry.md` rather than
anything about the survey, but it is one observation, it is outside this plan's
scope, and no claim is made from it.

## 7. Two comments in the tree, settled

### `v9x_vbe_vram_reported` is live on `s3`, and the manifest comment was wrong

`packaging\families\s3\family.psd1` claimed that `ValidateMode`'s memory test is
inert on this family because `v9x_vbe_vram_reported` "is permanently zero" for
`s3`, on the reasoning that `enable16.c` assigns it "only on the tier-0 VBE
path". That is not what the code does, and BARRY proves the code.

`src\display16\enable16.c:719-731` assigns **both** `v9x_vbe_vram_reported` and
`v9x_vbe_vram_bytes` from the family's `read_video_memory` hook - the S3 CR36
decode at `src\chipsets\s3\s3_hw16.c:96-99` - after a successful enable,
whenever that hook exists. `v9x_vbe_vram_bytes` starts at zero and is written in
exactly two places: line 640 on the tier-0 path, dead on this family, and line
729 inside that same `if`. So the inventory's `usable=2097152` is proof that the
branch executed, and therefore that line 728 set `v9x_vbe_vram_reported` to the
CR36-decoded 2 MiB.

The inventory's `reported=0` is *not* a contradiction, and the plan's
expectation that it would show 2 MiB conflated two variables. `reported=` is
`v9x_runtime_vram_reported`, which `modes16.c:239` fills from the mini-VDD's
4F00h answer - the scan path, correctly zero here. The `ValidateMode` variable
is `v9x_vbe_vram_reported`, a different symbol, and the `s3` family's
`publish_diagnostics` does not report it, which is why the proof has to run
through `usable=`.

The refusal was then measured directly, and this is where the plan's test design
needed correcting. **A reboot into an oversized mode does not exercise
`ValidateMode` at all.** GDI calls `ValidateMode` on a mode *change*; at boot it
calls `Enable` with the registry's mode, so the Enable path's `fail-hardware-*`
stages are what report - as `src\display16\enable16.c:158-160` already says.
Rebooting into 1024x768x32 and 1280x1024x16 does refuse both, but with
`Stage=fail-hardware-vbe-mode` and a fall back to 640x480x4, which is BARRY's
BIOS declining 4F02h (it answers `014F` for `0118`/`0119`/`011A`/`011B`,
`2026-08-20-vbe-mode-inventory.md:114`) - not our memory test.

A **live** switch is the test that reaches `ValidateMode`, and it is decisive.
`V9XMSW.EXE`, which drives `ChangeDisplaySettingsA` with `CDS_UPDATEREGISTRY`
the way Display Properties does, from a 800x600x32 desktop:

| Requested | Needs | `ChangeResult` | Outcome |
|---|---|---|---|
| 1024x768x32 | 3.00 MiB | **-2** `DISP_CHANGE_BADMODE` | refused, desktop unchanged |
| 1280x1024x16 | 2.50 MiB | **-2** `DISP_CHANGE_BADMODE` | refused, desktop unchanged |
| 1024x768x16 | 1.50 MiB | **0** | accepted, switched |

The third row is the control: the same code path accepts a mode that fits and
refuses both that do not, without ever reaching the hardware. `V9X_VALMODE_NO_NOMEM`
maps to `BADMODE`, and it is deliberately indistinguishable from "not in the
table" (`src\display16\ddi.c:1086-1089`), so the fitting-mode control is what
makes this a measurement of the memory test rather than of the table.

**Verdict: the code reads live and behaves live. The manifest comment was
wrong on both counts and is corrected.** The two mechanisms are worth keeping
distinct in future testing: on the reboot path the BIOS refuses these modes, on
the live path our VRAM check refuses them first.

### The pruned mode list is not what keeps oversized rows off a 2 MiB card

The same manifest comment credited `Inf.ManualSelect`'s pruned list with keeping
1024x768x32 and 1280x1024x16 off a 2 MiB card, calling it "load-bearing rather
than a duplicate of a runtime refusal". It is not load-bearing on BARRY: the
pruned ten-row list is `[Velocity9x.Registry.Manual]`, reached only by the
VLB manual-select install, while BARRY's `PCI\VEN_5333&DEV_8811` matches
`[Velocity9x.Install.trio64]` and gets `[Velocity9x.Registry.trio64]`, which
publishes **all twelve rows** including both oversized ones. On the PnP path the
runtime refusal is the only thing standing between Display Properties and a mode
the card cannot scan out.

## 8. The defect this run found: 640x400x8 is not on BARRY's BIOS

`run-vm-mode-matrix.ps1` cannot address BARRY - the family's VM target is an
emulator port - so the twelve rows were driven by hand through the agent, one
reboot per mode, writing `Display\0001\DEFAULT\Mode` and
`Config\0001\Display\Settings` exactly as the matrix script does. Nine of the
ten rows that fit 2 MiB reached `Stage=enable-ok` with the pitch the table
declares, and the guest-side `V9XGDI.EXE` probe confirmed the depth in each
(the agent reports `BitsPerPixel=0` against this driver and is not a depth
check):

| Mode | Pitch | Stage | GDI |
|---|---|---|---|
| 640x480x8 | 640 | enable-ok | PASS |
| 800x600x8 | 800 | enable-ok | PASS |
| 1024x768x8 | 1024 | enable-ok | PASS |
| 1280x1024x8 | 1280 | enable-ok | PASS |
| 640x480x16 | 1280 | enable-ok | PASS |
| 800x600x16 | 1600 | enable-ok | PASS |
| 1024x768x16 | 2048 | enable-ok | PASS |
| 640x480x32 | 2560 | enable-ok | PASS |
| 800x600x32 | 3200 | enable-ok | PASS |
| **640x400x8** | 640 | **fail-hardware-vbe-mode** | - |

**640x400x8 does not work on this card.** Selecting it as the desktop mode gives
`Stage=fail-hardware-vbe-mode`, which is stage 2 - the
`v9x_vbe_set_mode(v9x_active_vbe_mode, v9x_vbe_mode_flags)` call itself
(`src\display16\enable16.c:668`, mapped at `src\display16\ddi.c:172`). Windows
falls back to 640x480 and raises a modal Display applet, which holds the agent's
`DesktopReady` low indefinitely and so looks like a hung guest; it is not, and
two `ESC` keystrokes clear it.

The cause is not memory and not our validation. **BARRY's ROM does not list VBE
mode `0x0100` at all.** Its 18-mode list runs `0101`..`011B` plus the extended
`0211`, and `0x0100` is one of the modes only the two 4 MiB targets add
(`docs\decisions\2026-08-20-vbe-mode-inventory.md:133`, and no `Mode0100.` key
exists in `2026-08-20-vbe-inventory-barry.txt`). So `ValidateMode` says yes - the
row is in the table and 256 KiB fits 2 MiB - `Enable` runs, and the BIOS refuses
4F02h.

That is precisely the failure mode the mode-inventory decision predicted for a
different row, in the paragraph that excluded 1600x1200x8:

> a VRAM check catches a mode too large, not a mode missing, so the row would
> pass validation and fail at 4F02h

The rule was written and then not applied to `0x0100` when the 640x400 row was
added later for Doom95. **This is a pre-existing table defect, not a 0.5.0
regression** - the 0.4.3 install on this card carried `MODES\8\640,400` too, and
nobody had ever selected it. It is exposed here only because physical silicon
was finally driven through every row.

Left unresolved deliberately, because both answers cost something and the
choice is outside this plan's scope:

- **Removing the row** costs Doom95 its mode on every S3 target whose BIOS does
  list `0x0100`, which is every other one measured. The DirectDraw consequence
  of not having it is recorded at `src\chipsets\s3\s3_hw16.c` - the game keeps
  the desktop's 16-bpp mode and writes 8-bpp frames into it.
- **Keeping it** leaves one target advertising a mode it cannot set. The
  comment at that row now records the exception rather than implying the row is
  safe everywhere.

What a fix probably wants is per-device row admission - the family already has a
per-chip `V9X_HW16_DEVICE`, so the row could be ViRGE-and-newer-Trio64 only -
but that is a table-structure change and needs its own plan. Note also that
Doom95 on BARRY was never going to work through this row: a DirectDraw
`SetDisplayMode` to 640x400 reaches the same 4F02h refusal.

## 9. What is still QEMU-only

Live mode switching. `docs\issues\2026-08-20-live-mode-switch-no-repaint-barry.md`
is open and undiagnosed and reproduces on this card, so Stage 2's "survives live
same-depth switching" gate remains emulator-only evidence. Block 2 of the plan
deliberately tested reboot transitions only; every one of the nine working modes
above was entered by reboot and was clean.

**The fault still reproduces on 0.5.0, build `edd7684`**, confirmed and not
diagnosed. A live 800x600x32 to 1024x768x16 switch reports `ChangeResult=0` and
`Result=PASS`, and the desktop then never finishes repainting: the capture shows
the 800-wide desktop's stale framebuffer read at the new 2048-byte stride, so it
appears twice side by side with a band of garbage between. Six captures over
30 seconds are pixel-identical **except the taskbar clock, which advances
5:34 PM to 5:35 PM**. That rules the capture race out conclusively and sharpens
the existing diagnosis: GDI is drawing and the driver is presenting - only the
invalidation of the rest of the screen never happens.

The serial-capture gate for the mini-VDD's boot markers also remains untaken on
physical hardware, for the reason in section 2. Closing it needs a null-modem
link from BARRY's COM1 to a capturing host; until then §2's two substitutes are
the evidence.
