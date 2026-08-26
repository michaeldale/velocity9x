# Changelog

All notable Velocity9x changes are recorded here. The project uses semantic
version numbers for product milestones; diagnostic builds retain a separate
build identifier so exact guest-tested binaries remain traceable.

## Unreleased

The dynamic VBE pipeline is verified inert on physical S3 silicon. BARRY, the
physical 2 MiB PCI Trio64, ran 0.5.0 over two clean boots with the runtime table
byte-identical to the static baseline — twelve rows, all `src=baseline`, nothing
hidden, the scan refused with `COLLECT_OFF` and no other status bit — and with
the `Run` key and the whole display class tree byte-identical either side of the
install. Full evidence and method in
[the decision note](docs/decisions/2026-08-26-s3-physical-pipeline-inert.md).
That run also settled that `ValidateMode`'s VRAM refusal is live on this family,
corrected three claims in the tree that said otherwise, and recorded that a
VBE 2.0 S3 board — not BARRY — is the prerequisite for ever enabling
`MiniVddVbeCollect` on `s3`.

Fixed: the live mode-switch path never sent `VDD_POST_MODE_CHANGE`, so the
master VDD was left believing a mode change was still in flight after every
live switch. `v9x_build_pdevice` has always opened the change with
`V9xVddPreMode` and the unchanged-mode branch beside it has always paired them.

Fixed: the display driver did not export `UserRepaintDisable` at ordinal 500,
which every Windows 98 DDK display sample exports and USER uses to tell a driver
when repaint requests may be issued. Implemented with the DDK's deferred-repaint
pattern.

Found and not fixed: the shared `s3` table's 640x400x8 row (`0x0100`) is absent
from BARRY's BIOS mode list, so it validates and then fails at 4F02h. Not a
0.5.0 regression — 0.4.3 carried the same registry row and nobody had selected
it. The row is kept, because removing it costs Doom95 its mode on every S3
target whose BIOS does list it.

Still open: the live-repaint fault on the physical Trio64
([issue](docs/issues/2026-08-20-live-mode-switch-no-repaint-barry.md)) still
reproduces. Three candidate cures were built, deployed and measured out; what is
now known is that the desktop is simply never invalidated, and that an Explorer
refresh repairs it perfectly.

## 0.5.0 - 2026-08-26

The dynamic VBE pipeline: the driver stops trusting a hand-written mode list
and starts asking the video BIOS, with every answer validated on the host
before a guest ever runs it. On the generic VESA family this releases 46
modes on QEMU where the baseline named 7, native Display Properties follows
along, and the panel's EDID chooses the fallback mode. Also in this release:
the settings report tells the runtime-table story, INSTALL.TXT is generated
per family, and the INF leads with the SUBSYS-qualified hardware id where the
family declares one.

Stages 1 and 2 of [the dynamic VBE pipeline](docs/plans/dynamic-vbe-pipeline.md)
are guest-verified. Stage 1's exit gate passed on 2026-08-25 on two Windows 98 SE
guests — the fresh local QEMU 4.2 std-vga VM and a UTM (Apple Silicon QEMU) VM —
with `C:\V9XBOOT.INI` matching the DOS inventory fixture record-for-record: all
64 cached records identical in mode, attributes, geometry, depth, memory model,
pitch, base and RGB masks, in BIOS list order, with every absent record explained
(legacy/planar, 15-bpp policy, and the 64-record cap reported in the status
flags).

Stage 2 — the runtime mode table consumed by GDI:

### Added

- **Admission reason codes** (`v9x_vbe_scan_admit`): every refusal names the
  rule it failed — unsupported, non-linear, memory model, physical base, depth,
  colour layout, stride, geometry, VRAM, duplicate, table-full and
  known-defect — so the guest inventory says why a BIOS record is absent.
  `v9x_vbe_build_mode_table_ex` adds the optional refuse-only family distrust
  predicate and a per-reason rejection tally; the original builder is now a
  wrapper over it.
- **Publication flags** (`v9x_vbe_publish_rows`): a baseline row a trustworthy
  scan contradicts keeps its storage slot and is offered to nothing. Hiding
  requires the full validity gate — a terminated list and a cache that is
  neither truncated nor overflowed — an empty cache contradicts nothing, the
  fallback row is the first *published* row, and a scan that would hide
  everything publishes everything instead. Host tests pin the GMA950-shaped
  case, the fallback movement and both publish-all paths.
- **`src/display16/modes16.c`**: the runtime table in fixed DGROUP storage
  (64 rows, masks, publication bytes), built transactionally at driver load —
  baseline committed first, mini-VDD API v2 records read into bounded staging,
  merged, validated and committed all-or-nothing, so a missing mini-VDD, an
  invalid scan or any mid-build refusal lands on exactly the static table with
  every row published. Also writes the validated mode inventory
  `C:\V9XMODES.INI` after a successful enable, with the `Complete=0/1` sentinel
  discipline, BIOS identity, raw-vs-usable VRAM, the 60 Hz-is-a-convention
  admission, scan state and counts, per-reason tallies, and one line per
  published row plus one per hidden row.

### Changed

- **`ddi.c` reads the runtime table.** `v9x_modes`/`V9X_MODE_COUNT` repoint to
  `modes16.c`'s committed table; `v9x_find_mode` skips unpublished rows, which
  makes ValidateMode, Enable, ReEnable and requested-mode selection refuse a
  hidden row through the one lookup they share; the load-time and
  requested-mode fallbacks follow publication rather than assuming row zero.
  All four families link the new objects; a non-scanning family commits a
  byte-identical baseline table.
- **The generated INF leads with the SUBSYS-qualified hardware id** when a
  chip declares `SubsystemId` (the vbe std-vga declares QEMU's `11001AF4`),
  keeping the bare id as the compatible id: Win98's Have Disk matches
  HardwareIDs, which are always SUBSYS-qualified on PCI, and refused the bare
  id on a UTM guest.

Guest-verified on the local QEMU VM: 46 rows (7 baseline + 39 dynamic), all
published, reason tally accounting for all 64 cached records; live switches
into dynamic 1152x864x16 and 1280x800x16 and back to baseline 800x600x16, with
the GDI framebuffer test passing at the dynamic mode.

Stage 3 - DirectDraw publication (2026-08-26):

- **`dd16.c` publishes the runtime table.** Rows and masks come from
  `modes16.c`'s committed table - the BIOS's own masks for a scanned row -
  published rows only, so DirectDraw's list is a subset of GDI's published
  list by construction. `v9x_vbe_dd_subset` gained a publication parameter (a
  hidden row is never selected; host-tested), and is used when more than the
  32 shared-block slots are published: every 8/16-bpp row in table order,
  then 32 bpp by ascending area. The active desktop row is guaranteed a slot,
  and the fill re-runs on every driver-object refresh so a live mode switch
  cannot strand the desktop off the list.
- Guest-verified: the DirectDraw probe completed with desktops at 8, 16 and
  32 bpp (the 32-bpp desktop on a dynamic row) - SetDisplayMode across
  depths, primary at the runtime row's exact pitch, pixel-verified blits,
  flips and RestoreDisplayMode - with the published list matching the runtime
  table order exactly and no 24-bpp row anywhere.

Stage 4 - native Display Properties synchronization (2026-08-26):

- **`V9xSyncModes` in `v9xsetp.dll`** mirrors the validated inventory
  (`C:\V9XMODES.INI`) into the display class instance's `MODES` registry
  tree, which is what the native Settings page enumerates. It refuses whole
  on any invalid inventory (torn, unknown schema, malformed or duplicate
  rows, out-of-range counts), targets only the class instance the INF marked
  with `V9xFamily` (zero or several marks, or a mark no `HKLM\Enum` devnode
  points at, is a recorded no-op), creates missing
  `MODES\<depth>\<width>,<height>` keys stamped `V9xDynamic=1` plus the
  generation, prunes only keys carrying that stamp when the current
  inventory no longer publishes their geometry - INF baseline keys,
  `MODES\4` and anything unstamped are never touched - repoints a
  `DEFAULT\Mode` naming a vanished row at a published row of the same depth,
  and writes every run's outcome (or the word "report" as a dry run) to
  `C:\V9XSYNC.INI`.
- **The generated INF carries the marker and the per-boot command**:
  `HKR,,V9xFamily` on the devnode and `Run,V9xSyncModes` beside the one-shot
  `RunOnce,V9xSettingsPage`, both under required-entry assertions so a
  scan-enabled family cannot ship without them.
- Guest-verified: first boot created all 39 dynamic keys (7 baseline keeps),
  the next boot was a pure no-change run, a planted stamped orphan was
  pruned while an unstamped neighbour survived, a `Complete=0` inventory was
  a recorded no-op, and the native Settings slider then offered the dynamic
  geometries up to 1920x1200 at High Color.

Stage 5 - EDID preferred hint (2026-08-26):

- **The mini-VDD collects EDID block 0 through `4F15h`** at init, after the
  mode scan so a hung DDC read cannot cost the mode table: a BL=00h
  capability probe, then BL=01h into the cleared V86 scratch, accepted only
  on `AX=004Fh`. The three EDID status bits (valid / no-DDC / read-failed)
  are the whole outcome and never affect mode-scan validity. The
  `EDID_CHUNK` API function now serves the cached block in register-only
  16-byte chunks, and `V9xMini_Vbe_Call` gained a client-BX input for the
  subfunction.
- **A host-tested EDID parser** (`src/common/edid.c`) accepts only a
  header-and-checksum-true 1.x block whose first detailed timing is a
  non-interlaced timing with nonzero geometry; the negative corpus covers
  the all-zero block a lying BIOS produces, a flipped checksum byte,
  version 2, descriptor-first slots and interlaced preferred timings.
- **Selection order**: the configured mode always wins when it resolves;
  only when it is absent or hidden does the published row matching the
  panel's EDID geometry at the requested depth stand in, ahead of the
  first-published-row fallback. The inventory carries `Edid=` and
  `Recommendation=` lines with reasons.
- Guest-verified: boot status gained the EDID_VALID bit, the block parsed as
  EDID 1.4 preferred 1024x768, the desktop stayed on the configured
  800x600x16, and a deliberately nonexistent configured mode fell back to
  1024x768 on the next boot.

Stage 6 - rollout and cleanup, the workstation half (2026-08-26):

- **`INSTALL.TXT` is generated per family.** The checked-in file is now a
  template; the family summary, the model-selection step (with the
  manual-select paragraph only where the family declares one), the default
  mode in the serial checkpoints and the after-first-boot mode wording all
  come from the manifest, and the dynamic-discovery/EDID paragraph is
  emitted only for scan-enabled families. Ends the S3-worded INSTALL.TXT
  shipping inside every family's package.
- README and docs/INSTALL.md describe the dynamic mode pipeline: what the
  generic VESA family discovers, where the inventory and synchronizer
  reports live, and that a broken BIOS list simply keeps the baseline.
- Recorded in the plan: no v1 compatibility path remains to remove (exact v2
  handshake, generated rescue probes, and by-mode lookup is a v2 operation),
  and ATI/Intel-GMA enablement stays hardware-blocked on its own gates
  rather than being faked here.

Stage 0 of [the dynamic VBE pipeline](docs/plans/dynamic-vbe-pipeline.md):
contracts and fixtures only. No driver behaviour changes — every image this
builds is the same image as before, which is the stage's own exit condition.

### Added

- **One shared definition of the mini-VDD API contract, in
  `include/asm/V9XMAPI.INC`.** The device id, handshake magic, contract version
  and function numbers existed twice, in `src/minivdd32/loader.asm` and
  `src/display16/runtime.asm`, with a comment in each asking the reader to keep
  them in step. Nothing detected a disagreement: a renamed function number or a
  field read from the wrong offset assembles, links, installs and then misreads
  the BIOS answers at boot, on hardware, with no diagnostic that points at the
  cause. Both files now include the one file, which is written in the subset of
  syntax MASM 6 and Open Watcom's wasm both accept — EQU definitions and
  comments, nothing else, since the mini-VDD is assembled by the DDK's MASM and
  the driver's runtime by wasm.

  The include also carries what API v2 will need: the bounds (128 listed modes,
  128 queries, 64 cached records, 16 reserved rescue probes), the packed 32-byte
  cache record's field offsets, the record and status flag bits, and the EDID
  chunk geometry. v2's numbers are defined ahead of its implementation so both
  sides can be written against one set of them; `V9XMINI_API_VERSION` still says
  v1, which is what the code implements and advertises.

  Its C mirror is `include/velocity9x/vbe_cache.h`, because the consumers of
  what comes back are C. Two files for one contract is a duplication no
  assembler-and-compiler pair here can avoid, so `scripts/check-tree.ps1` now
  asserts the numbers equal — 26 constants at present — that the packed record
  stays a power of two with every field inside it, that the cache bound cannot
  exceed the runtime table it feeds, and that neither assembly user defines any
  shared constant locally. A deliberately mismatched value was checked to fail
  the tree check.

- **The mini-VDD's rescue-probe mode list is generated from the family
  manifest.** `loader.asm` carries seven hand-written VESA mode numbers, which
  were only ever right for families whose modes happen to be the standard
  numbers — the reason the generic VBE family reaches nothing on a
  panel-filtered Intel BIOS. `build-minivdd-skeleton.ps1` now takes a `-Family`
  and emits `V9XPROBE.INC` from that manifest's distinct `VbeMode` values,
  deduplicated across chips, sorted, and bounded by the reserved capacity read
  from `V9XMAPI.INC` rather than by a number repeated in the script. It asserts
  the generated file against the manifest afterwards, because this include
  becomes BIOS calls at boot. A build with collection assembled out emits no
  probe list and deletes any stale one.

  For both scan-enabled families the generated list is byte-for-byte the seven
  numbers `loader.asm` hard-codes today, which is what lets Stage 1 swap one for
  the other with nothing else changing.

- **A DGROUP budget gate on the Win16 driver.** `audit-family-binary.ps1` reads
  the linker's own group size, adds the local heap the link file declares, and
  fails over a 32 KiB budget — half the 64 KiB an automatic data segment can
  hold, since DGROUP also holds the stack at run time. It reports the figure on
  every build, so growth is visible in the log before it is a failure. The
  driver is at 2014 bytes plus a 1024-byte heap today; the runtime mode table
  this plan adds is 1728 more (64 rows: 896 bytes of `V9X_HW16_MODE`, 768 of
  colour masks, 64 of publication flags), which is the number the gate exists to
  keep honest.

- **The VBE 3.0 linear colour fields, and the depths derived from them.** A
  parsed mode summary now carries `significant_depth` — the sum of the three
  channel widths — beside the storage depth, which is the pair that distinguishes
  XRGB 8:8:8:8 (32 and 24) from packed RGB 8:8:8 (24 and 24) when BitsPerPixel
  alone cannot. It also carries `mask_flags`, recording whether the layout came
  from the VBE 3 linear fields, from the legacy ones, and whether a linear
  stride was reported: a transposed channel is a different bug depending on
  which set was read, and nothing downstream could tell those apart from the
  masks alone.

  The rule is the linear set when any of its eight bytes is non-zero, the legacy
  set otherwise — one rule covering both BIOS generations with no version test,
  because a VBE 2 BIOS writes none of those bytes into a block the caller
  zeroed. Which is also why the caller must zero it: the new host fixtures
  include the stale-scratch case, where a 16-bpp mode's block still holds the
  previous 32-bpp mode's channels, and show the parser refusing the layout
  rather than describing the mode with channels that do not fit in its pixel.

- **Host fixtures for what the mini-VDD's counts and flags permit.** The mode-
  list walk is assembly and is exercised by fault injection in a guest, but the
  other half of the bounded-count invariant is testable here: a reported count
  is clamped to the cache bound rather than believed, and a collection that
  never ran or could not trust its list contributes nothing regardless of the
  count it came with. `v9x_vbe_scan_may_contradict` is the stricter question the
  publication rule turns on — whether the scan may hide a family baseline row —
  and it additionally requires the walk to have been complete, because admitting
  a mode wrongly offers something that fails to set while hiding one wrongly
  takes away something that works.

- **The controller contract carries BIOS identity: `Capabilities` and
  `OemSoftwareRev`.** Read into the DOS VBE conformance corpus
  (`docs/specifications/dos-vbe-conformance.md`), the strongest finding against
  this pipeline's design is that defects track the *video BIOS revision* rather
  than the chip — the corpus author swapped a BIOS between two cards and the
  fault moved with it, and Commander Keen fails on ELSA, miro, SPEA and Number
  Nine boards while Canopus and Diamond boards of the same chip pass. This
  driver recorded chip identity and no BIOS identity at all, so a report from an
  untested card could not be attributed to the one variable that predicts
  behaviour.

  Both are fixed-offset VbeInfoBlock fields, so neither costs a pointer
  dereference: the revision is the BIOS's own version number, and capability
  bit 0 is the switchable-DAC claim behind a whole class of DOS-era colour
  faults while bit 1 says the controller is *not* VGA-compatible — which is what
  every text-mode restore and Safe Mode fallback here assumes it is. Neither can
  refuse an otherwise credible controller block: a BIOS that describes itself
  sparsely is not describing itself incredibly, and losing the aperture over a
  diagnostic field would be the wrong trade. `vbe_inventory_dos` records them
  too, so the outstanding QEMU capture will carry them.

  The OEM strings would say more and are deliberately not in the v2 contract:
  they are far pointers into the controller block, so they need the same bounded
  staging copy the mode list gets — which is a second reason that block is
  copied before any `4F01h` call can overwrite it.

- **Two host fixtures built from real BIOS captures rather than invented
  values.** `test_gma950_survey` transcribes the 945GM netbook survey - attributes
  `009B`, aperture `D0000000`, both stride fields, the reported channel sizes and
  positions - and pins what the panel-filtered case does: six live modes, two of
  them updating baseline rows in place, four appended, and the five dead standard
  rows still in storage. `test_qemu_stdvga_list` does the same for the QEMU
  std-vga list captured on 2026-08-23.

  The QEMU one turned up two things no hand-built fixture had: mode `0013h` and
  mode `0146h` both describe 320x200x8, so duplicate geometry at one depth is
  real BIOS behaviour the merge has to collapse; and 48 admitted rows against 32
  DirectDraw slots means the subset fills 28 slots with 8- and 16-bpp rows and
  has four left for high colour, so an ordinary **1024x768x32 desktop is absent
  from the ordinary selection**. That makes "guarantee the current desktop row is
  present" a requirement with a test behind it rather than a sentence in a plan.

### Changed

- **24-bpp modes are refused by mode admission.** They divide into whole bytes
  and the parser describes them perfectly well, but `display16` has never drawn
  a 24-bpp surface: no family baseline table has such a row, `ddi.c` splits three
  ways on 8/16/else, and the DIB Engine has no 24/32 surface flag to set.
  Admitting one would offer a mode the blitters cannot draw, so 24-bpp support
  is its own piece of work rather than a dependency of dynamic discovery. QEMU
  std-vga publishes 24-bpp modes, which is why this had to be settled before the
  runtime table was wired to GDI rather than discovered at that gate. The
  refusal is a quiet omission from the table, tested as such.

- **The dynamic-VBE plan now carries the BIOS evidence that argues against it.**
  The plan is built on asking the BIOS what it supports and believing the
  answer, so the conformance corpus is the closest thing available to an
  adversarial review of it, and five findings changed something. *Newer is not
  safer*: on the same silicon the VBE 2.0 S3 BIOSes are worse than the 1.2
  ones — a drawing fault becomes a hang, the 8x14 font stops being restored —
  so the version gate exists because 2.0 is where the linear framebuffer is
  defined and for no other reason. *Some listed modes do not work*, so admission
  gains a known-defect reason code and an optional per-family distrust predicate
  that may only refuse, never admit; S3's 360-wide FIFO defect is the first
  entry, scoped to the family rather than written as a general geometry rule,
  because a blanket "width must be a multiple of 8" would reject the ordinary
  1366-wide panel. *Reported VRAM is wrong in both directions*, so the inventory
  publishes the raw figure beside the usable one. *A green matrix can mean
  nothing* — deferred defect D5 — so capture is the oracle for a newly admitted
  mode, not a supplement to asking GDI whether GDI is happy. And the 60 Hz
  refresh rate `dd16.c` publishes is recorded as a known falsehood this plan
  makes worse before anything fixes it, since the modes it adds are
  disproportionately the high-resolution ones where real BIOSes run 87 Hz and
  above; fixing it is deliberately out of scope, because anything ending in *and
  then set a refresh rate* can put a mode on the only monitor a machine has.

- **Stage 0's outstanding evidence is captured, and it changed a rule from
  precaution to requirement.** The plan said "some BIOSes point `VideoModePtr`
  into the controller buffer"; the first target is one of them. Two DOS programs
  run on the QEMU std-vga guest reported different mode-list pointers -
  `0E38:25F8` and `0B46:06D4`, both low DOS RAM, neither near the C000h ROM - and
  a pointer that moves with the caller is a pointer into the caller's own 4F00h
  block. Without the staging copy, the Stage 1 walk would read its mode numbers
  out of a block the first `4F01h` had already overwritten, on the package this
  rolls out to first.

  The same capture confirms 19 of the 93 listed modes are 24 bpp, which settles
  that rejection with evidence instead of reasoning; that 49 admitted rows sit
  inside the 64-row table, so the cache-full flag will not fire there; and that
  QEMU serves a valid EDID (1.4, preferred timing 1280x800, a geometry the list
  also admits), so Stage 5's exact-match path can be built without waiting on
  physical panel hardware. Captures and method are in
  `personal/v9x-qemu-stdvga/`.

  On BIOS identity the second sample agrees with the first and against the
  earlier preference: SeaBIOS reports `OemSoftwareRev=0000` where the netbook
  reports `0100`, and neither names a build. The informative source is a string
  in both cases, but a different one each time - the ROM build stamp on the Intel
  part, the VBE OEM strings on SeaBIOS - which argues for reading both eventually
  rather than preferring either now.

## 0.4.4 - 2026-08-22

The S3 driver runs on a second physical machine, and the first with no PCI bus:
a 486 with an S3 Trio64 on VESA Local Bus under Windows 95 4.00.950. Getting
there took a manual-select INF model, three real bugs and one Win95 INF-syntax
trap, all below.

### Fixed

- **The Velocity9x tab never appeared in Display Properties on Windows 95,
  because two AddReg lines were silently doing nothing.** Both key paths contain
  a space — `Controls Folder` and `Shell Extensions` — and Win95's SetupX will
  not parse an unquoted AddReg key path that does. It fails without a word: on
  the 486 every other line of the same section applied, including the `HKCR`
  ones beside them, while these two created no keys at all, so the property
  sheet handler was never registered and there was nothing to explain why. The
  `Controls Folder` key on that machine has a complete
  `Joystick\shellex\PropertySheetHandlers` structure, which is what proved the
  mechanism and the path shape were fine and the syntax was not. Both paths are
  now quoted, as Windows' own `MSDISP.INF` quotes the same key. Win98's SetupX is
  more forgiving, which is why this survived until a Win95 machine met it.

  Verified on the 486: both keys created with the right values by a clean
  install, and after a reboot the **Velocity9x tab appears** between Appearance
  and Settings, reporting `S3 Trio32/64 86C764`, `5333:8811`, 2 MB, 640x480x8,
  a 59.957 MHz clock shared with memory, and *"Driver / framebuffer: Active -
  linear aperture mapped"*.

- **The manual-select model no longer names a mini-VDD, which is what stopped
  the driver working on the 486.** A Win9x display devnode is started by the VDD
  loading the mini-VDD named in `DEFAULT\minivdd`. On Win95 4.00.950
  `v9xmini.vxd` does not load, so the devnode never reached `DN_STARTED`, Device
  Manager reported **Code 24** — "this device is not present" — Display
  Properties then offered no modes at all, and the desktop sat on the 4-bpp
  `vga.drv` fallback row. The driver underneath was fine the whole time; it was
  simply never asked to enable, which is why it presented as a driver fault for
  so long.

  `Inf.ManualSelect.MiniVdd = $false` moves `HKR,DEFAULT,minivdd` out of the
  shared registry section and into the per-chip ones. The manual model AddRegs
  only the shared section plus its own, so it gets no mini-VDD; every PCI model
  still gets one from its chip section, so the Win98SE targets are unchanged —
  verified in the emitted `ati` and `vbe` INFs as well. Done by omission rather
  than by writing an empty value afterwards, so it does not rely on SetupX
  applying `AddReg` sections left to right.

  **Measured end to end on the 486 from a clean install with no hand edits:**
  `Problem` `0x18` → `0x00000000`, `Status` `0x0EE7` → `0x0ACF` with
  `DN_STARTED` set, `V9XBOOT.INI` reading `Stage=enable-ok` with a 640x480x8
  surface, and `V9XHW.INI` written for the first time naming
  `S3 Trio32/64 86C764`, `5333:8811`, `VideoMemoryBytes=2097152` valid and the
  clock valid at 59957 kHz. So `identify_without_pci` names the chip on a bus
  with no PCI, the aperture maps from protected mode, and a mode set lands.

  What it costs is the mini-VDD's DPMS and mode-save callbacks, not the
  framebuffer — both S3 chips read their aperture directly and this family
  already builds the mini-VDD with `MiniVddVbeCollect = $false`. **Why it fails
  to load on Win95 is still undiagnosed**; a logged `BOOTLOG.TXT` would say, and
  this is the measured configuration rather than an explanation.

- **The manual-select model now declares its resources, without which Windows
  reports the device as not present.** A PCI model needs none: the bus reports
  what the card decodes and the Configuration Manager builds the devnode's
  resource list from it. A model with no hardware ID sits on a device the
  enumerator only knows exists, so with nothing declared the devnode has no
  resources at all — Device Manager gives **Code 24**, "this device is not
  present, not working properly, or does not have all the drivers installed",
  and Display Properties refuses with "your display adapter is not configured
  properly". The driver loads and runs perfectly well in that state; it is
  simply never asked to enable, so it presents as a driver fault and is not one.

  The fix is the `LogConfig` every display model in Win95's own `MSDISP.INF`
  carries — all twenty share one `VGA.LogConfig` — emitted here as
  `[Velocity9x.LogConfig]` with the standard VGA register windows and
  apertures. Only the manual-select install section references it; the PCI
  models are left alone, because on a bus that enumerates them a HARDWIRED
  config would be asserting what the bus already knows. The linear framebuffer
  is deliberately not declared, exactly as `MSDISP.INF` does not declare it for
  its own S3 models with linear apertures.

  `Assert-V9xInf` now requires the reference and a section with
  `ConfigPriority` plus at least one `IOConfig` and one `MemConfig`, and
  requires a family with no `ManualSelect` never to mention it. Note that
  `LogConfig` is applied by SetupX at install time, so changing it means
  re-running the install — swapping the driver files does not revisit the
  devnode.

- **`identify_without_pci` now unlocks the S3 extended registers before reading
  the chip id, which is what stopped the driver working on the 486.** It was the
  only S3 register accessor in `s3_regs16.c` that did not, and the comment
  justifying that cited a real measurement: on this very card the video BIOS
  leaves CR38/CR39 holding `59h/BDh` rather than the unlock keys, and CR2D/CR2E
  read `88h/11h` anyway — so "the locks gate writes, not reads". That
  generalised one lock state into a rule. Under Windows the state is different
  and the rule is false: measured from `ValidateMode` on the 486 with the
  desktop on the fallback `vga.drv`, CR38/CR39 read `96h/52h` and **CR2D/CR2E
  both read `5Ah`**. Those are read-only chip-id registers and cannot have
  changed, so the lock state was the only variable.

  The consequence was the whole failure: the id did not match, so
  `v9x_hardware_acceptable` refused, so `ValidateMode` rejected every mode, so
  GDI never called Enable and Windows fell back to the INF's 4-bpp `vga.drv`
  row — a driver that loaded cleanly and then declined everything it advertised,
  which is the D3 shape the plan set out to avoid. With the unlock in place
  `V9X16LD.EXE` reports *"passed its DIB Engine inquiry and all six mode
  validations"* on the same machine in the same state, the only change being the
  unlock.

  The cost is that identification now writes two registers on a card it has not
  yet identified. That is the bet `v9x_s3_read_video_memory` already makes in
  the same file: CRTC 38h/39h are S3 extensions plain VGA does not implement,
  and both are saved and restored.

- **A driver that refuses every mode now says why.** `ValidateMode` gates every
  mode on `v9x_hardware_acceptable`, and GDI asks it before it ever calls
  Enable — so when the answer is no, Windows is told a cleanly loaded driver
  supports nothing, falls back to the INF's 4-bpp `vga.drv` row, and the Enable
  path's ten `fail-hardware-*` stages never run to record it. On the 486 VLB
  Trio64 that produced a VGA desktop and a boot trace reading nothing but
  `libmain`, and finding out why took a hand-loaded DRV, a registry export and
  a survey re-read. `v9x_hardware_acceptable` now records which condition
  refused, and `ValidateMode` writes it: `fail-validate-no-identify-hook`,
  `fail-validate-pci-bios-present` (the hook exists but a PCI BIOS is present,
  so it was never offered), or `fail-validate-identify-declined` (no PCI BIOS,
  the hook ran and did not recognise the card). The accept path is byte-for-byte
  the same set of decisions in the same order; only the reason is new. The write
  is guarded on `v9x_ever_enabled` like the `query-ok` write, because Windows
  disables and re-enables the display during startup and a late query must not
  overwrite the marker of a driver that came up.

  On the 486 this distinguishes the two remaining candidates for the VLB
  failure on one boot, which is what it was built for.

- **A manifest comment that claimed a refusal the code cannot make.** The s3
  `Vm.Modes` note said a 2 MiB Trio64 has 1024x768x32 and 1280x1024x16 refused
  by `ValidateMode`. It does not: that test reads `v9x_vbe_vram_reported`, which
  `enable16.c` assigns only on the tier-0 VBE path, and this family has a
  `read_aperture` hook — so the figure is permanently zero for this binary and
  the test is inert. `Inf.ManualSelect`'s pruned list is the only thing keeping
  those two modes off a 2 MiB card, which makes it load-bearing rather than a
  belt-and-braces duplicate.

### Added

- **An INF model with no hardware ID, so the S3 driver can be installed on a
  486.** The driver was already capable on VESA Local Bus and still not
  installable: every model the INF advertised was a `PCI\VEN_5333&DEV_xxxx`, and
  SetupX cannot bind one of those on a bus it does not enumerate — the 486's
  Win95 guest root-enumerates the card as `*PNP0913`. A family manifest may now
  declare `Inf.ManualSelect`, which emits one more models line carrying no
  hardware ID field at all. That is Windows' own pattern for a manual-select
  display model (`MSDISP.INF` has eight of them) and the display class permits
  the override, so the entry installs over a device whose real ID nothing in the
  INF claims; `identify_without_pci` then picks the chip at Enable from the
  card's own identity registers. It deliberately does *not* bind `*PNP0913`,
  which covers every S3 801/805/928 card `DETECTS3801` finds and for which this
  driver has no code. No C source changed: the driver side was already complete
  and the runtime reads real VRAM from CR36, so the heap sizes itself.

  The model's mode list is derived rather than declared — the intersection of
  every chip's modes, narrowed to those that fit the VRAM the *physical* card
  has, computed exactly because every S3 mode is proven packed linear. The 2 MiB
  VLB Trio64 therefore gets 10 of the 12 modes the chips declare against 4 MiB;
  16bpp 1280x1024 and 32bpp 1024x768 are pruned before the install instead of
  refused at Enable, where the symptom would be a black screen and a stage code
  at the next boot. One helper, `Get-V9xFamilyManualSelectModes`, serves both the
  emitter and the assertions, so they cannot disagree about the list.

  What holds it: the models section must contain nothing but PCI models and, at
  most, the one declared manual line, so an accidental second ID-less model
  fails the build; `[Velocity9x.Registry.Manual]` must be exactly the derived
  list and nothing else; the description may not carry a SetupX field separator
  and must be unique across every family's manual descriptions and every chip's
  `DeviceDesc`, since it is all a human has to pick by; and the effective
  default mode must be one the model advertises. That last check also refuses a
  `-ForceModeIndex` that names a mode outside the derived list — for the s3
  family, indexes 7 (`16,1280,1024`) and 10 (`32,1024,768`) can no longer be
  built, because `DEFAULT,Mode` is written by the shared registry section the
  manual model also reads. `docs/specifications/family-manifest.md` documents
  the key; `INSTALL.TXT`, the floppy README's hardware-ID sentence and the
  package `MANIFEST.TXT` now all say what to do when the Details page shows no
  PCI ID at all.

  Driven onto the physical 486 the same day, and the model half worked on the
  first attempt: Win95 4.00.950's Have Disk list offered
  `Velocity9x S3 (VLB manual select)` on a machine with no PCI bus, and
  accepting it rewrote the device to `v9xdisp.drv,*vdd,*vflatd,v9xmini.vxd`.
  The boot after it did not reach the shell — the machine answers ICMP and
  NetBIOS but the agent never came back, so nothing remote can read the stage
  code. `docs/handoffs/2026-08-21-vlb-first-driver-run.md` records what was
  proven, what stopped, and what has to happen at the keyboard.

- **The VGA hardware survey at schema 2, for a card on a bus with no PCI.** The
  tool we shipped would have come back from a 486 with an S3 Trio on VESA Local
  Bus nearly empty, because almost everything it collected was reached through
  PCI. Tier 2 now falls back to identifying an S3 from the card's own identity
  registers — read with the locks as the BIOS left them, and unlocked only after
  those reads have already said S3 — dumps the whole extended register file
  (CR30-CR6F, SR08-SR1F) rather than a list chosen for the PCI parts, and adds
  the DAC identity by reading only. A new `[Platform]` section records CPU class
  without assuming CPUID exists, installed RAM three ways, A20 and the memory
  managers. A third opt-in, `/aperture`, reads the linear window's physical base
  through `INT 15h AH=87h` and states its own three limits in the report.
  `scripts/parse-vga-survey.ps1` reads schema 1 and 2 alike, derives the bus,
  names the S3 chip, decodes CR58 and CR36, and refuses to call an aperture live
  when its base overlaps RAM. `docs/decisions/2026-08-20-vlb-survey-schema2.md`
  records the design and, below it, what the 486 actually returned.

- **The survey's source-safety gate grew with the tool, and `run-checks` now
  runs it.** Schema 2 introduced inline assembly, so the gate additionally
  allowlists every literal port constant at an `inp`/`outp` call site and every
  raw opcode byte a `#pragma aux` emits, and refuses the VBE setters and the
  state-changing instructions a pragma could otherwise smuggle in. It is honest
  in the script about what a regex over source text cannot see.
  `build-vga-survey.ps1 -GateSelfTest` asserts the gate rejects each of nine
  deliberately broken copies of the source, needs no compiler, and is now a
  `run-checks` step rather than a check performed once.

- **The 486 VLB run happened, and the aperture question is still open.** An S3
  Trio64 on a Diamond Stealth 64 DRAM in a 486: the no-PCI branch executed for
  the first time on any target, the locked-read identification named the card,
  and the register restore is proven byte-identical across three runs. Two
  premises were corrected by it. The locks gate writes rather than reads - this
  card holds `59`/`BD` in CR38/CR39, not the `48`/`A5` the PCI parts show, and
  identified itself anyway - and CR38/CR39 turn out to be key latches that do
  not read back what is written. The window sits at `0x7F000000` with linear
  addressing disabled, which is above what `INT 15h AH=87h` can reach, so
  **whether the 486 decodes anything there is still unknown** and needs a probe
  that can address above 16 MB. The VBE 2.00 the reports show is S3VBE 3.18, a
  resident TSR, not the card - so no S3 ROM has yet been seen offering a linear
  framebuffer and the plan's premise stands. It advertises that framebuffer at
  64 MB where the card's registers say 2 GB, which is the most useful thing to
  come out of the run: a VLB driver may have to program the window base rather
  than read it, since there is no host bridge to have routed one.

  A fourth run from a clean boot then measured the card's own BIOS with nothing
  loaded: **VBE 1.02, and bit 7 clear on all 18 modes with `PhysBasePtr` zero
  throughout**. So tier-0 is closed off on this card by measurement rather than
  inference, the premise now holds for a VLB S3 as well as the four PCI ones, and
  S3VBE is *adding* linear addressing to modes whose ROM offers none rather than
  passing it through. It also gave the real memory figure EMM386 had been hiding
  (32 MiB), confirmed CR58/CR59/CR5A read the same clean as dirty, and ruled out
  two confounds - none of the register readings were EMM386 artifacts, and the
  unexpected `59`/`BD` lock state is the video BIOS's own. Full account and the
  four raw reports in `docs/decisions/2026-08-20-vlb-survey-schema2.md`.

### Fixed

- **The parser reported 1 MB of RAM when a memory manager was hiding it.**
  EMM386 answers `INT 15h AH=88h` with 0 KB, which the parser turned into
  "1 MB installed" - and that figure is what the aperture probe's window base is
  compared against, so a wrong small answer silently disabled the check that
  turns a false positive into a negative. It now reports the figure as unknown,
  says which call declined to answer, and attaches a caveat to any positive
  aperture result it could not check.

- **The survey could not say whose INT 10h answered.** The 486 run reported VBE
  2.00 from a machine whose card ROM contains no VBE strings in plaintext, with
  a `PhysBasePtr` contradicting the card's own aperture registers - and the
  report had no way to distinguish the ROM from a resident hook. `[BiosData]`
  now carries the INT 10h and INT 42h vectors, so it can.

- **The report could not say whose VBE it was describing, and the answer was
  already in it.** `[VBEModes] ModeListPointer` is a far pointer, and its segment
  names the provider: the 486 VLB card's own BIOS returns `C000534F`, into its own
  option ROM, while the S3VBE TSR on the same machine returned `0DC62612` in low
  RAM. The parser now derives and prints that, and says plainly that a
  linear-framebuffer attribute from a non-ROM provider is software's promise
  rather than a property of the card. It works on every schema-2 report,
  including ones taken before the `Int10Vector` key existed.

- **A non-PCI card's own ROM can name it, and the parser now looks.** The VLB
  Stealth 64's option ROM carries a valid `PCIR` header reporting `5333:8811`,
  because Diamond shipped one image for both bus variants. That is a read-only
  identification route needing no bus at all.

- **The VLB linear aperture works, at both candidate addresses, and the driver
  already does enough.** A 32-byte marker written into video memory through the
  banked `A0000h` window came back out at the linear base on the 486 VLB Trio64 -
  at `0x7F000000`, the address the card's BIOS chose, and again at `0x04000000`
  after relocating there. The same address read all `FF` with linear addressing
  disabled and the marker with it enabled, one bit apart, which is as clean a
  control as the hardware affords. So VLB linear framebuffer support is possible,
  `Cr58ReadBackHonoured=yes` clears the read-back guard in
  `v9x_s3_enable_linear_aperture`, and the prediction that a non-PCI machine
  would need the driver to *place* the window rather than accept it was wrong:
  VESA Local Bus is the CPU's own bus brought out to a slot, so the card decodes
  A31-A2 itself and there is no host bridge to persuade. Relocation works if a
  board ever needs it. `tools/diag/vlb_aperture_dos.c` is the probe; it sets
  modes and writes the card, so it is not the survey and never goes to a tester.
  `docs/decisions/2026-08-21-vlb-aperture-answered.md`.

  **Three of the five runs measured nothing**, because the card's ROM closes the
  extended register lock behind a mode set and the probe did not re-open it: the
  window registers all read back `42h`, the base computed from them was
  `0x42420000`, and a `Cr58ReadBackHonoured=no` line looked like a finding about
  the card when it was a bug in the tool. Fixed, re-run as runs 6 and 7, and the
  probe now refuses to proceed when CR58, CR59 and CR5A read identically. The
  driver is unaffected - it already unlocks before every extended access, which
  is precisely what the probe was not doing.

### Known issues

- **The Mach64's display is wrong at 16 bpp, and its mode matrix passes anyway
  (D5).** Captured directly on 2026-08-20: the driver's own screenshot at
  1024x768x16 shows a flawless desktop and the `ati` mode matrix passes 6/6,
  while a host-side capture of the same moment shows shredded noise and no
  desktop. So on that card a green matrix is actively misleading rather than
  merely weak evidence. Unchanged by this release and not caused by it; the
  `ati` and generic-VESA families have no 24/32-bpp modes. Details and the
  capture method in `docs/issues/2026-08-16-tier0-defects-deferred.md`.

- **A live mode switch does not fully repaint the desktop on the physical
  Trio64.** After `ChangeDisplaySettingsA` with `CDS_UPDATEREGISTRY` - what
  Display Properties does - large areas keep the previous mode's framebuffer
  indefinitely. A reboot into the same mode is clean, and neither emulated S3
  target reproduces it. Ruled out: the capture race below, a stride fault (the
  regions that do repaint are correct and the driver's `Surface=` line agrees),
  and the no-clear flag alone. So the fault is in invalidation after the
  in-place PDEVICE rebuild. Seen at 800x600x32 and on a live 8 -> 16 switch, so
  it is probably older than the depths this release added rather than caused by
  them - but that is not established. The mode matrix cannot see it because it
  reboots between every mode.
  `docs/issues/2026-08-20-live-mode-switch-no-repaint-barry.md`.

- **A screenshot taken straight after a mode change can look like a stride
  bug.** On a slow machine the desktop repaint outlasts the capture, so the
  image holds the previous mode's framebuffer being overwritten. This produced a
  regression report against this release that had to be withdrawn; capture twice
  and compare. `docs/issues/2026-08-20-barry-tiling-was-a-screenshot-race.md`.

- **The `vbe` family's VM target does not exist.** Its manifest names QEMU
  std-vga on port 9872; no QEMU is installed and the profile that answers there
  is an 86Box Mach64. That family has had no guest run this release.

## 0.4.3 - 2026-08-20

True Color on the S3 cards, and every row of it measured from a real video BIOS
before it was written down. Four S3 BIOSes were dumped - the physical Trio64,
the 86Box ViRGE/DX and the 86Box Trio64 - and they settled a question the VESA
standard does not: the mode numbers usually described as 24-bit are 32 bpp on
all of them, so this family has no 24-bpp mode and will not get one.

Verified on the real display rather than through GDI. Host-side captures of the
86Box windows, which share none of the driver's assumptions about pitch, base or
depth, show clean correct output at 1024x768x32 on both S3 chips and at
1280x1024x16 on the ViRGE. That distinction is not academic: the same technique
on the Mach64 the same day showed noise behind a screenshot that looked perfect.

### Added

- **True Color and 1280x1024 on the S3 targets.** The `s3` package now offers
  32 bpp at 640x480, 800x600 and 1024x768, and 1280x1024 at 8 and 16 bpp, on
  top of the modes it already had. Every row was measured first: four S3 BIOSes
  were dumped — the physical Trio64, the 86Box ViRGE/DX and the 86Box Trio64 —
  and no row exists for a mode number none of them offers
  (`docs/decisions/2026-08-20-vbe-mode-inventory.md`).

  **There is no 24-bpp mode, and there will not be one on these cards.** The
  VESA numbers usually described as 24-bit — 0x112, 0x115, 0x118 — report
  `BitsPerPixel=32` on all four BIOSes, with a scan line of `width * 4` and a
  reserved byte at `8@24`. Not one of them has a packed 24-bpp mode anywhere in
  its list, so the depth those numbers carry is a per-BIOS fact and for the S3
  family the answer is 32.

  1600x1200x8 is deliberately left out although both 4 MiB cards list it and it
  would fit a 2 MiB card's memory: the physical Trio64's BIOS does not offer the
  mode. A VRAM check can refuse a mode that is too large and cannot refuse one
  that is absent, so the row would have validated and then failed at 4F02h.

- **`ValidateMode` measures a mode against the card, not just the table.** A
  family table is shared by chips that are not: the 2 MiB physical Trio64 and a
  4 MiB ViRGE take the same list, and 1024x768x32 needs 3 MiB. Modes that do not
  fit are now refused rather than accepted and failed at the mode set, which is
  what lets one shared table carry rows only the larger cards can hold.

- **24- and 32-bpp support through the whole driver.** `v9x_mode_calculate`
  accepts the depths that divide into whole bytes and still refuses the rest, so
  the 15-bpp modes every S3 BIOS lists stay refused. The DirectDraw blit
  callbacks admit the new depths and the CPU fill path gained 3- and 4-byte
  cases.

### Fixed

- **The DIB engine was told 5:6:5 at every depth above 8 bpp.** The PDEVICE flag
  fork set `FIVE6FIVE` for anything that was not palettized, which at 24 or
  32 bpp describes three channels packed into the first two bytes of a pixel.
  DIBENG.INC defines no flag for the higher depths — the engine takes the layout
  from `biBitCount` — so the fork now sets neither.

- **CPU colour fills discarded the red channel.** `v9x_cpu_fill` truncated
  `dwFillColor` to a `WORD`. Latent at 8 and 16 bpp, wrong the moment a deeper
  mode existed.

- **Both S3 blitters would have corrupted high-colour blits.** Neither had a
  depth guard: the Trio64 path discarded `bytes_per_pixel` entirely and writes
  its foreground colour as a single 16-bit word, and the ViRGE encodes depth in
  its command word but has never run a 24-bpp blit. Both decline above 16 bpp
  now and the CPU fallback serves those depths. ViRGE S3D at 24 bpp is a
  recorded follow-up rather than a shipped path.

- **A page flip could shift the whole frame.** `v9x_set_display_start` programs
  a doubleword offset and silently rounded a byte-granular one down. Impossible
  at 8, 16 and 32 bpp, where the pixel size divides 4; reachable at 24 bpp. The
  flip is declined instead.

- **The DirectDraw mode list was a third copy of the mode table**, hardcoded
  into a HAL that cannot see which family it serves — so the Matrox build, whose
  family offers one mode, published seven. The 16-bit side owns the list now and
  the shared block carries a count, which `DriverInit` validates. Capacity is
  32 modes, measured against the 4096-byte DPMI block it has to fit: 3096 bytes
  in total, so the allocation is unchanged.

- **`-ForceModeIndex` had a hardcoded range** that went stale whenever a family
  gained or lost a forced mode. It is checked against the family's own list now,
  before anything is compiled.

## 0.4.2 - 2026-08-18

The mini-VDD's first meeting with a real video BIOS, and the second bug found by
the same 2 MiB Trio64 in one day. `V9XMINI.VXD` hung the boot on physical
hardware with a Windows protection error that no 86Box guest had ever produced.

### Fixed

- **The mini-VDD's VBE scratch buffer is paragraph-aligned now.** The boot-time
  collection allocated its V86 scratch byte-aligned
  (`_Allocate_Global_V86_Data_Area` with flags 0) and truncated the linear
  address to a real-mode segment with `shr eax, 4`. On a non-paragraph-aligned
  block that segment starts up to 15 bytes *below* the allocation - so the
  ring-0 `'VBE2'` stamp landed in whatever V86 global data preceded the buffer,
  every result peek was skewed, and the BIOS could write past the end. 86Box
  happened to hand back aligned blocks; the physical machine did not have to.
  The allocation now passes `GVDAParaAlign + GVDAZeroInit`, and the stamp uses
  the returned linear address instead of recomputing it from the truncated
  segment. Verified on the card that exposed it: two clean boots to
  `enable-ok` at 1024x768x16 with the full collection running.

- **Installing the driver now actually installs the Velocity9x tab.** Two
  separate faults kept it off every machine that was not set up by
  `update-associated-driver.ps1`. First, `[Velocity9x.Previous]` listed the same
  three shell keys `[Velocity9x.Registry]` adds, and SetupX applied that DelReg
  *after* the AddReg: the install deleted its own registration, and
  `HKCR\CLSID` survived only because Win9x cannot delete a key that still has a
  subkey — which is exactly the half-registered wreckage found on BARRY.
  Second, Windows 98 validates a Display property-sheet handler against a `Tag`
  DWORD built from a per-machine seed, ignores any handler whose Tag does not
  check out, and deletes the key, so an INF can never register this page by
  itself. `V9XSETP.DLL` now exports `V9xRegisterPage`, which recovers the seed
  by inverting that expression over a handler Windows has already accepted (a
  stock install ships two, and they agree) and writes the handler key, the Tag
  and the Approved entry. The INF fires it through `RunOnce` at the first boot
  after the install. Verified on the Trio64: with the registration wiped and
  the Approved value replaced by a sentinel, the RunOnce boot rewrote both,
  computed the same Tag the PowerShell path does, and the tab was present on
  that same boot.

### Changed

- **Families that never read the VBE cache no longer run the collection.**
  `Build.MiniVddVbeCollect = $false` in a family manifest builds that family's
  `V9XMINI.VXD` with the whole collection assembled out
  (`V9X_NO_VBE_COLLECT`); the `s3` and `matrox-m2` families set it. Their
  drivers read the aperture from hardware and never consult the 4F9Ch cache,
  so for them the collection was eight nested BIOS calls at `Device_Init` with
  nothing to show - all risk, no benefit. The 4F9Ch API stays in the image
  and reports the zeroed cache as "collection never ran", exactly the state
  the 16-bit side is designed to refuse. Also verified on the Trio64: two
  clean boots. Tier-0 families (`vbe`, `ati`) keep the fixed collection.
  Decision record:
  [docs/decisions/2026-08-18-minivdd-vbe-collect-gating.md](docs/decisions/2026-08-18-minivdd-vbe-collect-gating.md).
- **The collection narrates itself over COM1 now.** `vbe-collect start`, one
  `vbe-call fn=/arg=` line before every BIOS call, `ret=` after it, and
  `vbe-collect done` - all bounded writes. `Exec_Int` into a BIOS that never
  returns cannot be timed out at ring 0, so if a tier-0 machine ever hangs in
  the collection again, a serial capture now names the exact call. A
  no-collect build announces itself with `vbe-collect disabled`, and the build
  script asserts the marker's presence or absence so the variants cannot be
  mistaken for one another.

## 0.4.1 - 2026-08-18

A bug fix release, found by reading before installing. Preparing to bring up a
physical S3 Trio64 turned up a 4 MiB video-memory assumption on every native
family's DirectDraw heap — correct on every card this driver had ever run on,
and wrong on the 2 MiB card about to be tried.

### Fixed

- **The DirectDraw heap is sized from the chip, not from an assumption.**
  `dd16.c` sized video memory as `v9x_vbe_vram_bytes ? : 4 MiB`, and only the
  tier-0 path ever set that variable: a family with a `read_aperture` hook never
  calls VBE 4F00h, so every native family took the 4 MiB literal. Both S3 parts
  and the Matrox family are native, and the S3 guests this driver was brought up
  on hold exactly 4 MiB, so the assumption was invisible — it is only wrong on a
  card holding less.

  On a 2 MiB Trio64 it over-states off-screen memory by 2 MiB in every mode, and
  by 5x at 1024x768x16 (512 KiB real, 2.5 MiB advertised). That figure is
  `dwVidMemTotal`/`dwVidMemFree`, which is what DirectDraw allocates against and
  what `ddhal_core.c` bounds every blit with, so it does not merely mislead: it
  grants surfaces past the end of installed VRAM, which an S3 aliases rather
  than faults on. A documented memory limit would present as corrupted
  rendering instead of a clean allocation failure.

  `V9X_HW16_OPS` gained a nullable `read_video_memory` hook, and the S3 families
  supply the CR36 decode they were already reading for diagnostics but never
  feeding to the heap. The answer goes through the same ceiling-and-floor
  correction as the tier-0 figure — clamped to what the family maps, floored at
  what the mode displays — now factored into one helper rather than written once
  inline. An undecodable size code returns 0 and keeps the old default, so no
  card that worked before stops working.

  Not a regression fix: no released version behaved differently, and on 4 MiB
  hardware the corrected figure equals the assumed one.

  **Verified on a physical 2 MiB S3 Trio64** on 2026-08-18, which is the card the
  fix was written for. CR36 decodes to `VideoMemoryBytes=2097152`, status `valid`,
  and `V9XDDP.EXE` at 1024x768x16 reports `GblHalVidMemTotal=0x00080000` —
  524,288 bytes, exactly `2,097,152 - (2048 x 768)`. The pre-fix 4 MiB literal
  would have advertised 2,621,440 there, five times the off-screen memory the card
  holds. First hardware measurement of the fix, and it lands on the predicted
  number.

  Reaching that measurement needed a workaround unrelated to this fix: the
  driver's own `V9XMINI.VXD` hangs the boot on this card, and the reading was taken
  with the stock `S3.VXD` in the `minivdd` slot. See
  [docs/issues/2026-08-18-trio64-minivdd-boot-hang.md](docs/issues/2026-08-18-trio64-minivdd-boot-hang.md).

### Notes

- The ATI and generic VBE families are unaffected: both are tier-0 and were
  already taking their size from 4F00h. The Matrox Millennium II keeps the 4 MiB
  default, having no memory-size decode and never having run on its card.
- CR58[1:0] still selects a 4 MiB linear window on both S3 parts, deliberately
  unchanged: the window size is what the aperture decodes, not what the heap
  advertises, and the current value is proven on hardware.

## 0.4.0 - 2026-08-17

Tier-0 became real: the generic VBE package now drives a card its own INF does
not claim, which is what the tier was always for and had never once done.

### Added

- **VBE queries at ring 0.** `V9XMINI.VXD` gained a private device id (`4F9Ch`)
  and a protected-mode API, reached through `INT 2Fh AX=1684h` — the mechanism
  `runtime.asm` already used for the master VDD. It collects 4F00h and the seven
  standard 4F01h answers once at `Device_Init` under nested execution and serves
  them from a cache; the 16-bit side reads them through callers in `runtime.asm`,
  because a VxD entry point needs 32-bit registers. The id is unallocated by
  anyone, so function 0 is a handshake returning a magic value and the driver
  refuses the entry point until it sees it — a collision costs a refusal rather
  than a call into a stranger.
- Tier-0 forces the scan line length through 4F06h when a family supplies no
  `post_mode_set`, and refuses if it cannot be made to match. Not the cause of
  D5, but right on its own merits: the Millennium II has always needed it,
  because a BIOS can accept a mode set and then scan out at a stride of its own.
- `VbeDetail` and `VbeCache` keys in `C:\V9XBOOT.INI`, and `DrawPitch`,
  `VbeScanBytes`, `VbeScanPixels`, `VbeScanBefore` in `C:\V9XHW.INI`. The stage
  code is the right granularity for the boot-trace contract and the wrong one to
  act on: it cannot separate a BIOS reporting an unusable stride from a BIOS call
  that never ran, and on a tier whose purpose is untested cards that is the first
  thing a bug report needs.
- The first benchmark against a stock driver, in
  `docs/decisions/2026-08-17-native-driver-benchmark.md`. Retail S3 `s3v.drv`
  19 FPS against Velocity9x ViRGE 18 FPS on Ironfield's `BltFast` path — about
  6% behind, same binary copied between guests so the game is not a variable.
  Every Ironfield number before this compared Velocity9x with earlier
  Velocity9x. Tier-0 on the Mach64 measured too: 6 FPS on `BltFast` against the
  ViRGE engine's 18, which prices the missing `eng_mach64.c`.
- The `ati` family: ATI Mach64 VT2 (`1002:5654`) and Rage Mobility-M
  (`1002:4C4D`) in one binary with run-time PCI dispatch, on tier-0. Every
  `hw16` hook is NULL, so the VBE mode set programs the card and the CPU draws;
  `EngineType` is `NONE` until `eng_mach64.c` exists. Phase 10 of the
  multi-chip restructure, against a Gateway Solo 2150.
- A hardware audit of the Mach64/Rage 2D engine, memory sizing, LCD panel path
  and errata, drawn from `xf86-video-mach64` (MIT), FreeBE/AF and 86Box's
  emulation - `docs/decisions/2026-08-16-ati-mach64-hardware-audit.md`. The
  laptop's panel (LG LP141XA, 1024x768) was decoded out of its own video BIOS,
  since the internal panel has no EDID.
- Per-target `Emulator` in a family manifest's `Vm.Targets`. A family can now be
  part emulated and part physical, which `ati` is: 86Box emulates the VT2 but no
  Rage, so `rage-mobility-m` is real hardware only and the mode matrix refuses
  it explicitly instead of resolving to port 0 and failing on a parameter
  validation error. Absent the key a target inherits the family emulator, so
  every existing manifest is unchanged.
- A family matrix generated from the manifests
  (`scripts/lib/family-matrix.ps1`) and `tests/host/test_family_matrix.c`
  asserting the C side against it: every declared PCI ID resolves to a backend,
  chips of one family share that backend and chips of different families do
  not, undeclared hardware resolves to nothing, and no engine capability is
  claimed against `EngineType = 'NONE'`. Chips gained a `VideoMemoryBytes` key,
  and every mode a family's INF advertises must be one its backend can lay out
  in that VRAM — the first check holding the INF and the driver to the same
  answer. It needs no emulator and no guest.
- The mode check runs one direction only, deliberately: `validate_mode` is a
  VRAM-bounded layout calculator rather than a whitelist, so "accepts exactly
  the advertised modes" is not a property it has. Advertising something
  unservable is the failure that matters, and that is what is caught.

### Fixed

- **Tier-0 could not reach an unlisted card at all (D3).** A family carries two
  allowlists and Have-Disk only satisfies one: the INF's, while
  `v9x_hw16.devices[]` is compiled into `v9xdisp.drv` and refused at stage 1
  whatever the INF said. So the route the manifest advertised produced a
  bound-but-inert driver on every card it existed to serve. `v9x_hw16_ops` gained
  `pci_match_optional`, set only by `vbe`; the scan still runs, because it
  decides whose hooks execute, but a miss is no longer fatal for a family that
  pokes no chip register and takes its aperture from the BIOS. The field sits
  last in the struct so the strict behaviour is what an initializer that forgets
  it gets.
- The same question was being asked in three places and answered differently.
  `ddi.c` checks `V9xHardwarePresent` at its Enable entry point *and* in
  `ValidateMode`, and fixing only the staged sequence left Enable refusing at
  `ddi.c`'s own check and `ValidateMode` rejecting every mode — which is how GDI
  ends up told a driver loaded and then offered nothing. All three now share
  `v9x_hardware_acceptable()`.
- **Ring 3 has no working way to hand the VBE BIOS a buffer (D4).** DPMI 0100h
  will not allocate a DOS block under Windows' DPMI host, and with a buffer that
  does work the DPMI 0300h simulated interrupt faults the machine. Both were
  measured on a guest, twice at the cost of a bluescreen. That code is deleted
  rather than kept behind a flag — it linked into every family image — and the
  mini-VDD does the calls instead.
- `matrox-m2` no longer claims `mov ax,4F06H` as a required pattern. Tier-0 now
  issues 4F06h from shared `vbe16.c`, so the instruction appears in every image
  and discriminates nothing, and because one family's `Required` is every other
  family's `Forbidden` it broke the `ati` build.
- The ViRGE's `test\s+al,8` pattern is anchored to `test\s+al,8\b`. Unanchored it
  also matched `test al,80H` — which the VBE linear-framebuffer attribute check
  compiles to in every family — so the Matrox image stood accused of running the
  ViRGE's MMIO sequence. These patterns are regexes over disassembly, and the
  false positive surfaces in a *different* family from the one that owns it.
- `Get-V9xFamilyVmTarget` and `Get-V9xFamilyVmChipIds` treated
  `@($Family.Vm.Targets)` as empty when a family declares none, but `@($null)`
  has one element in PowerShell, so both fell through to the per-target branch
  and dereferenced `$null`. `vbe` is the first family without targets and the
  first to hit it — the mode matrix could not resolve it at all.
- `check-tree.ps1` now requires `packaging/families/vbe/family.psd1`, which
  phase 9 added without listing, alongside the new `ati` manifest.
- `scripts/build-host-msvc.ps1` had a stale source list — the clock, memory,
  registry and Matrox modules were missing — so the second-compiler pass failed
  at the link step on every run and never executed a test. It compiles, links
  and runs again, now over the same source set as the Watcom pass.
- The host build joins the driver and HAL compiles in using `-we`, so a warning
  fails it rather than being reported alongside a successful exit.

### Verified

- Eighteen modes across three chips on one build: `Win86SE` (ViRGE/DX) and
  `Win98SE-Trio64` six each on the hooked path, and `Win98SE-Mach64VT2` six on
  tier-0 — a card not in its family's device list, reached by Have-Disk, taking
  its aperture entirely from the BIOS. The S3 pair were re-run after the
  mini-VDD change, since its init-time collection runs for every family and
  those two had a working driver to lose.
- `V9XHW.INI` on the Mach64 reads `Adapter=Unrecognised card on the generic VBE
  path` with both ids `unmatched`: the family accepted a card it does not name
  and says so, rather than publishing the one id its INF claims.

## 0.3.5 - 2026-08-16

The multi-chip restructure through phase 8: one S3 binary serving both chips,
and the 32-bit HAL split along the seams that made it possible.

### Changed

- The `s3-virge` and `s3-trio64` families merged into one `s3` family: one
  binary, one INF with a model per chip, and dispatch by PCI id at scan time.
  `enable_aperture` and `fill_engine_descriptor` moved from the family ops
  table into the device entry — they are the only two hooks the ViRGE and the
  Trio64 disagree about — and `V9X_HW16_OPS.devices` became an array of
  pointers so each chip module owns its own identity and hooks. The per-object
  audit layer now does the work the image-wide one cannot, asserting the
  ViRGE's CR53 new-MMIO sequence is present in its object and absent from the
  Trio64's. Verified on both 86Box guests from the one package: correct
  per-chip identity and clocks, all six modes green on both, and the Trio64
  still advertising no Direct3D.
- The two-model INF was verified through a real SetupX Have-Disk install on
  both guests, not just by generation: each card was offered only its own
  model, bound its own install section (`Velocity9x.Install.virge-dx` /
  `.trio64`), got its own `MODES` AddReg, and reproduced its full phase 7
  readings afterwards. Also measured, and worth knowing: a PnP re-detect
  installs Microsoft's in-box `DXS3.INF` rather than this driver, so removing
  the display device is not a way to reinstall — the documented Have-Disk
  selection is required.
- Retired with it: the two single-chip manifests, `LegacyOutputName` /
  `LegacySkeletonOutput` / `LegacySwitch`, the `-S3Trio64` and
  `-MatroxMillennium2` builder aliases, and the checked-in
  `packaging/win98se/velocity9x.inf`. Byte-for-byte golden compare against the
  pre-restructure images ends here by design — one package with both chips
  cannot reproduce either single-chip package — so `golden-baseline.ps1` is now
  a build-to-build check and a code-size budget.

- The per-chip `V9X_DD_ENGINE_S3_*` identity bits retired. `engine.flags` is
  now runtime state only — VALID plus the `STATUS_VALIDATED` latch — and which
  chip this is comes from `engine_type` alone, so a new chip is a new enum
  value rather than a new bit. The old derivation in `dd16.c` also read as
  ViRGE for any engine type it did not recognise, `NONE` included, which a
  family with a descriptor hook and no engine would eventually have hit.
  `V9XTRACE.EXE` gained `EngineType` and `EngineCaps` in the same change, since
  `EngineFlags` alone can no longer say which engine ran.
- Both compiles now use `-we`. `-wx` is Watcom's warning *level*, not
  warnings-as-errors, so the HAL build had been reporting warnings and exiting
  0 — which is how six dead locals survived the `ddhal.c` split.
- `src/display32/ddhal.c` split into `ddhal_core.c`, `blt_cpu.c`,
  `engines/{vga_scanout,eng_s3_virge,eng_s3_trio}.c` and `d3d/d3d_virge.c`
  behind one private header, `ddhal_internal.h`. The header is deliberately
  narrow — a symbol crosses it only where a second module needs it — so the
  ViRGE's MMIO accessors and engine recovery and the Trio's readiness test are
  now unreachable from outside their own engine. It is also the HAL's only
  `<windows.h>`, which `check-tree.ps1` enforces in place of the old per-file
  allowance. `build-ddraw-hal-dll.ps1` compiles and links a source list.
- The 32-bit HAL's runtime chip dispatch is now a `v9x_engine32_ops` table
  selected from `engine.engine_type`, replacing the
  `v9x_trio_engine_ready() ? trio : virge` pair inlined at the drain, source
  copy, colour fill and GetBltStatus call sites. Writing them out showed the
  pairs were not asking one question but three, so the table keeps `ready`,
  the latching `validate_status`, the passive `status_validated` and `can_blt`
  apart by name. There is no `recover` member: it is only ever called from
  inside the bounded wait that expired, and the Trio64 has none.
  `V9X_DD_ENGINE_TYPE_*` and `V9X_DD_ENGINE_CAP_*` moved to a new
  dependency-free `include/velocity9x/engine_abi.h`, so `win9x_ddraw_abi.h` no
  longer reaches into the 16-bit-only `hw16.h`. Behaviour is unchanged against
  both S3 guests: D3D gate set, engine counters, Ironfield FPS and the
  timeout-injection asymmetry all match the recorded pre-split baselines.

- `V9X_DD_ENGINE` gained `engine_type`, `engine_caps`, `io_base`,
  `crtc_index_port` and two reserved DWORDs, appended so the existing fields
  keep their offsets and `ddhal.c` reads them unmodified. One deliberate
  `V9X_DD_SHARED_ABI` bump to 2026081601; a mixed old/new DRV+DLL pair fails
  safe on the dwSize/abi check. `dd16.c` fills the descriptor through a new
  `fill_engine_descriptor` hook, and the Trio64 caps clamp is now driven by
  `engine_caps` rather than a build-time define — so a family that does not
  claim D3D cannot have it advertised on its behalf.
- The staged hardware enable sequence moved from `runtime.asm` into
  `src/display16/enable16.c`, which calls the new `vbe16` mode-set service and
  the family's `post_mode_set`, `read_aperture` and `enable_aperture` hooks.
  `runtime.asm` now has no chip `IFDEF` at all: it keeps the DIB thunks, the
  VDD calls, and two chip-agnostic primitives — a PCI BAR0 read and the DPMI
  selector/mapping helper, both of which need 32-bit registers the 8086-target
  C cannot use. Stage code numbering, selector reuse across Enable cycles and
  the never-free-the-selector Disable behaviour are unchanged.
- The audit now scans every object in the image rather than `runtime.obj`
  alone: chip code lives in C modules now, and a check that only looked at the
  assembly would pass vacuously once the code it audits moved out. Several
  instruction signatures were retargeted to what the compiler actually emits
  (Watcom folds `(v & 0xFC) | 0x13` into `and al,0ECH`), and the PCI BAR0
  patterns were dropped from the Matrox set because that code is now a shared
  primitive and no longer distinguishes one family from another. The link-map
  symbol layer carries proportionally more of the audit as a result.
- `runtime.asm` no longer selects chip literals with `IFDEF`. The PCI identity,
  the VBE 4F02h mode-set flag and the DPMI aperture size are DGROUP variables
  stamped from the family's `v9x_hw16` table at load, and `V9xFindPciDevice`
  walks the device list rather than testing one hard-coded ID — which is what
  lets one binary serve more than one card. Stage code numbering is unchanged,
  so the boot-trace tooling still detects divergence.
- Because the PCI identity and VBE flag are now data rather than immediates,
  the per-chip audit follows them: each chip declares a `MapSymbols` entry for
  its device table, and every family declares `Audit.DispatchSymbol`. A family
  binary must contain its own device-table symbol and no other family's.
- Chip data moved out of `src/display16/ddi.c` into a 16-bit hardware layer.
  `include/velocity9x/hw16.h` declares one statically linked `v9x_hw16_ops`
  table per family binary carrying the PCI identity, the audited VBE mode
  table (640x400 still ordered last, for Doom95), the `C:\V9XHW.INI` strings,
  and nullable hooks; a NULL hook means the chip-agnostic default. The tables
  live in `src/chipsets/{s3/virge,s3/trio64,matrox/millennium2}/*_hw16.c`, and
  the shared S3 CR36/PLL register reads in `src/chipsets/s3/common/s3_regs16.c`.
  No `#ifdef V9X_TARGET_*` remains in `ddi.c`. `C:\V9XHW.INI` is byte-identical
  on both S3 targets and the code segment grew 76, 78 and 338 bytes for ViRGE,
  Trio64 and Millennium II respectively.

- The build system is now driven by per-family manifests
  (`packaging/families/<id>/family.psd1`) instead of per-chip switches. A
  family is one package covering one or more chips that share a driver binary;
  the manifest declares its chips, sources, defines, audit signatures, INF
  metadata, floppy placement and VM profile. `-S3Trio64` and
  `-MatroxMillennium2` remain as aliases for `-Family`. See
  `docs/specifications/family-manifest.md` and
  `docs/decisions/2026-08-16-per-family-packaging.md`.
- The INF is generated from the manifest rather than rewritten out of a
  checked-in single-model file, so a family can carry more than one chip. The
  single-hardware-ID assertion became set equality against the manifest. The
  generated file is byte-identical to the previous output for both S3 targets.
- Post-link auditing moved to `scripts/audit-family-binary.ps1`. Chip signature
  checks are manifest-driven: an image must match all of its own chips'
  signatures and none of any other family's, with the forbidden set derived
  from the sibling manifests, so adding a family strengthens every existing
  family's audit with no script change.
- `run-vm-mode-matrix.ps1` takes `-Family`, and with it the guest port, package
  path and mode list, so it can address a guest other than the controller's
  default. A family declaring no emulator is refused explicitly instead of
  silently testing the wrong machine. Its depth check no longer trusts the
  remote agent's `BitsPerPixel`, which reports 0 against this driver while
  reporting correctly against the stock S3 driver; depth is verified from the
  guest-side GDI test result, which has always been accurate.

### Added

- `scripts/run-checks.ps1`, the local CI gate: tree check, host tests,
  per-family builds with audits and INF assertions, floppy.
- `scripts/build-all-packages.ps1`, which builds every declared family and
  writes `build/packages.json` with per-file SHA-256.
- `scripts/golden-baseline.ps1`, which captures and compares the byte-level
  baseline the restructure must preserve. Win32 PE link timestamps are zeroed
  before hashing so a rebuild is reproducible.

### Fixed

- `scripts/backup-86box-profile.ps1` named every backup after the ViRGE
  profile whatever it was given. The copied contents were always correct,
  which is the worse failure: nothing looks wrong until someone restores what
  they believe is one guest over another.
- `scripts/run-vm-mode-matrix.ps1` wrote the requested mode to a hardcoded
  `Services\Class\Display\0001`, which is the wrong key on any guest whose
  display-class index differs — the Trio64 guest's is `0002`. It passed
  regardless because the `Config\0001` half of the same `.reg` is what takes
  effect, so the matrix had been green by accident. The key is now resolved
  from the registry by finding the one that names `v9xdisp.drv`, and the run
  refuses if no key does.

## 0.3 - 2026-08-15

### Added

- Add a conservative S3 Trio32/64 86C764 (`5333:8811`) build target with
  strict INF matching, Trio-aware PCI discovery and hardware reporting, and
  no ViRGE-only DirectDraw/MMIO/S3D exposure. The 86Box target passes live
  640x480, 800x600, and 1024x768 switching plus GDI validation at both 8 and
  16 bpp, with palette validation at 8 bpp.
- Register and verify the Velocity9x native Display Properties page and
  standalone settings utility on the Trio64 target, including adapter, mode,
  framebuffer, clock, build, and last-test reporting.
- Added a Trio64 DirectDraw framebuffer HAL with video-memory surfaces, CRTC
  page flips, and bounded 8/16-bpp hardware solid fills. Direct3D remains
  intentionally disabled on this non-ViRGE target.
- V9XDDP now dumps the DirectDraw runtime's own `DDRAWI_DIRECTDRAW_GBL` —
  flags, resolved HAL callback tables, video-memory info, mode list and
  PDEVICE — so a rejected HAL can be told apart from an accepted one without
  guessing, and tests a source-copy blit's HRESULT and resulting pixels.
- The HAL trace distinguishes a blit the driver executed from one it declined
  (`BltEngine`), which the previous `Blt` counter could not: `ddRVal` is
  `DD_OK` either way, and `GetBltStatus` polling floods the trace ring.
- The DirectDraw HAL now writes its callback ring directly to
  `C:\V9XTRACE.INI` on an unhandled process fault or bounded ViRGE engine
  timeout, before recovery can discard the last useful callback history. The
  manual trace utility writes `C:\V9XSNAP.INI` so it cannot erase that evidence.

- ARGB4444 Direct3D textures. The texture unit selects its format from bits
  7:5 of the command register, so 4444 and 1555 are both native and need no
  conversion; only 1555 was published, leaving an application one format with
  a single alpha bit. V9XDDP dumps every enumerated format and pixel-verifies
  a 4444 texture render.
- Publish 640x400x8 (VBE mode `100h`) in the mode table and the INF. It works
  as a GDI desktop mode, but DirectDraw admits no sub-480-line mode outside
  its own ModeX set, so it does not make `SetDisplayMode(640, 400, 8)`
  succeed - see
  [docs/issues/2026-08-15-doom95-low-resolution-modes.md](docs/issues/2026-08-15-doom95-low-resolution-modes.md).
- Extend V9XDDP with a `/pal8` palettized-presentation test that records the
  depth and pitch DirectDraw actually delivers, dumps both the GDI and the
  DirectDraw mode lists, and reads a known palette index back through the
  surface and the screen DC. Rework `V9XMSW /depth` into `/depth:N`, a
  depth-cycle stress, now that live depth changes are expected to succeed.
- Extend V9XDDP with an RGB565 Direct3D texture lifecycle and add V9XWND, a
  GDI-free top-level window inventory for diagnosing blocked fullscreen
  dialogs.
- Program ViRGE 8.7 color gradients for Gouraud-shaded triangles and expose
  the hardware's perspective-correction raster capability required by
  Hellbender's Direct3D device filter.
- Publish a coherent RGB565 Direct3D texture format and bounded legacy
  texture-handle lifecycle callbacks, with per-operation trace diagnostics.
- Add dormant legacy Direct3D execute-buffer parsing and DirectDraw
  pseudo-surface lifecycle tracing. Win98 rejects a HAL that publishes the
  obsolete `Execute` entry, so the valid DX5 callback path remains advertised.
- `scripts/build-floppy-package.ps1` assembles an offline transfer folder that
  fits one 1.44 MB floppy. It carries both chip packages, because the card in
  an offline machine cannot be identified ahead of time and a second trip is
  expensive, plus a root README written for real hardware rather than a VM.
  The output is a plain directory tree, not an archive: Windows 98 has no
  built-in extractor. The script fails rather than emitting a tree too large
  for the disk.

### Changed

- Support live colour-depth changes. The 8-bpp PDEVICE needs a palette the
  16-bpp one does not, so an in-place rebuild across a depth change used to
  overrun the allocation and `ReEnable` refused it; `dpDEVICEsize` now always
  reserves the palette so one GDI allocation serves either depth, the size
  actually granted is recorded and re-checked before every rebuild, and the
  palette is rebuilt whenever the depth changes. Verified on both guests:
  20/20 alternating 8/16-bpp switches with cursor agitation, palette
  animation and GDI readback passing at both depths, and a clean boot with
  the registry left at 8 bpp. `ModeSwitching` now reports `live-any-depth`.

### Fixed

- Report installed video memory. The driver decodes it from CRTC register
  36h, which the Trio32/64 and the ViRGE/DX encode the same way, instead of
  the flat 4 MiB the shared block had always assumed. Codes belonging to
  other S3 parts are reported as unavailable rather than guessed, and the
  decode is covered by host tests.
- Replace the settings page's three permanently checked, permanently greyed
  checkboxes with value rows. "DIB Engine rendering", "Hardware acceleration"
  and "Live mode switching" were statements of fact rather than settings, so
  the boxes could never say anything else. The page now reports the PCI ID,
  installed video memory, and separate Rendering, DirectDraw, Direct3D and
  mode-switching rows whose text narrows on a chip or build that does less -
  the Trio64 reports Direct3D as not advertised, for instance. `V9XHW.INI`
  gained `VideoMemoryBytes`, `VideoMemoryStatus` and `Direct3D`, and
  `Acceleration` now reads `directdraw-fill-blt` rather than the stale
  `directdraw-solid-fill`.
- The Display Properties dialog fits a 640x480 screen again. A property sheet
  sizes itself to its tallest page and this page is the tallest, so it set
  the height of the whole native dialog; at the driver's own default
  first-boot mode the OK/Cancel/Apply row sat below the bottom of the screen.
  The page is now 211 dialog units on an 11-unit row pitch. The standalone
  V9XSET.EXE panel was over the same limit and was compacted to match.
- The Display Properties page no longer clips its logo. The generated bitmap
  was 355x71 into a static control roughly 357x49 pixels, and `SS_CENTERIMAGE`
  clips rather than scales, so the logo lost its top and bottom edges. The
  logo slot is now 238x46 dialog units and the bitmap 320x65, with margin in
  both axes.
- The boot trace keeps the furthest stage it reached. `enable-ok` was written
  and then overwritten by a later GDIINFO query, because the guard tested
  `v9x_enabled`, which `Disable` clears — and Windows disables and re-enables
  the display during startup. A latch set on the first successful Enable and
  never cleared replaces it, so the settings page no longer reports a healthy
  driver as "Not confirmed - stage: query-ok".
- The active packages are built with the boot trace enabled by default. It was
  opt-in behind `-BootTrace`, so the shipping ViRGE driver never wrote
  `C:\V9XBOOT.INI` at all and the settings page read whatever stale file was
  left behind. `-NoBootTrace` omits it; `-BootTrace` is still accepted.
- Trace DirectDraw surface negotiation through `CanCreateSurface`,
  `CreateSurface`, `DestroySurface`, and `AddAttachedSurface`; enlarge the
  shared callback ring, correct Win16 exit bookkeeping, and honor the
  `GetDriverInfo` handled-return contract.
- Guard the Win16 `SetCursor` and `MoveCursor` DIBENG extension thunks while
  the display PDEVICE is unavailable during mode teardown, preventing a null
  PDEVICE fault in `DIB_MOVECURSOREXT` observed when Hellbender exits a failed
  full-screen initialization; guarded Pascal returns discard their four bytes
  of original cursor arguments before returning to USER.
- Follow the Windows 98 DIBENGINE mini-driver ReEnable ordering by rebuilding
  the PDEVICE directly, without carrying a BeginAccess cursor exclusion across
  `CreateDIBPDevice`; the old exclusion state is invalid after the in-place
  PDEVICE rebuild and caused striped framebuffer writes plus a cursor fault.
- The texture sampler reads the surface's own pixel format instead of assuming
  ARGB1555 for everything. `ddpfSurface` is only allocated when the surface's
  format differs from the primary's, so it is read only when the owning local
  surface has `DDRAWISURF_HASPIXELFORMAT`; a surface without it carries the
  primary's RGB565, which this engine cannot sample and now declines rather
  than misreading.

- The Direct3D texture capabilities now describe what the sampler actually
  does. `v9x_d3d_texture_setup` accepts only square, power-of-two, 16-bit
  surfaces in video memory and silently declines anything else, but the
  shipping build declared neither `D3DPTEXTURECAPS_POW2` nor `SQUAREONLY`, so
  an application had no way to comply and its textures were simply dropped.
  `ALPHA` and `D3DDEVCAPS_TEXTUREVIDEOMEMORY` are declared for the same
  reason: the published format carries an alpha bit the sampler reads, and the
  sampler rejects system-memory surfaces outright. `dwTextureCaps` is now
  `0x27` against the retail S3 ViRGE driver's `0x2F`, the remainder being
  colour-key transparency, which is not implemented.
- Removed the concluded C4 caps experiment. Its "control" arm was the shipping
  configuration and under-declared the texture constraints, while the arm
  labelled "self-consistent texture advertisement" had the correct answer.

- The Direct3D device advertises `D3DPSHADECAPS_FOGFLAT`. The driver already
  blends fog into the vertex colour and flat shading reuses that colour across
  the triangle, so the capability was implemented but unpublished. It was the
  only capability difference from the retail S3 ViRGE driver with a visible
  symptom: Hellbender warned that the adapter could not show fog, and no
  longer does.

- The Direct3D device advertises `D3DDEVCAPS_EXECUTESYSTEMMEMORY`. A DirectX
  2/3-era title renders only through execute buffers and selects its device by
  capability, and the Windows 98 DDK's ViRGE sample sets this bit while
  leaving the `Execute` callbacks null exactly as this driver does — the
  runtime decomposes execute buffers into `RenderState` and `RenderPrimitive`
  calls, which the probe pixel-verifies. `D3DDEVCAPS_TEXTUREVIDEOMEMORY` and
  `D3DDD_LINECAPS` were measured against Hellbender in the same way and
  reverted: neither changed its behaviour and neither is implemented.
  See [docs/issues/2026-08-15-hellbender-software-fallback.md](docs/issues/2026-08-15-hellbender-software-fallback.md).

- The framebuffer selector is stable for the driver's lifetime. `Disable`
  freed its LDT descriptor and the next `Enable` allocated a different one,
  but the DIB Engine caches that selector inside the PDEVICE and does not
  reacquire it, so after one cycle it was writing through a descriptor that
  had been returned to the LDT. Hellbender hit exactly that — a general
  protection fault in `DIBENG.DLL` with `ES` holding the previous selector —
  and now reaches gameplay for the first time, past both the black-frame hard
  wedge and the GPF.
  See [docs/issues/2026-08-14-hellbender-dibeng-gpf.md](docs/issues/2026-08-14-hellbender-dibeng-gpf.md).
- The HAL trace publishes the live framebuffer selector and the Enable and
  Disable counts, which is what made the selector change observable.
- V9XMSW gained a `/cursor` switch that moves the pointer and forces it to be
  redrawn across every mode change, and now flushes its results file before
  exiting — a mode change immediately before process exit was discarding the
  tail of the file, so a passing run could report nine of ten cycles and no
  verdict.
- The ViRGE DirectDraw blitter is now reachable at all. The HAL published
  `DDCAPS_BLTCOLORFILL` without `DDCAPS_BLT`, which the runtime treats as no
  blitter, so the bounded solid fill added in the ViRGE engine foundation had
  never executed once — the guest baseline measured zero `Blt` dispatches.
  Colour fills now run on the ViRGE engine (16 ms to 1 ms) with Direct3D and
  its callback counts unchanged.
  See [docs/decisions/2026-08-14-virge-blitter.md](docs/decisions/2026-08-14-virge-blitter.md).
- `V9X_DD_ENGINE_STATUS_VALIDATED` aliased `V9X_DD_ENGINE_S3_TRIO64` — both
  were `0x4` — so validating the ViRGE engine status set the Trio64 identity
  bit and would have routed ViRGE blits through the Trio64 port-I/O command
  sequence once the blitter was advertised.
- Engine-status validation is no longer latched only by
  `GetBltStatus(DDGBS_CANBLT)` and the Direct3D draw callbacks, which left the
  blit path unable to reach the engine for an application that does not poll
  first. It is one helper shared by all call sites, it runs on the blit path,
  and it re-samples the status register briefly so the first fill after a mode
  change is accelerated rather than falling to the CPU path.
- Every blit the driver admits now completes in the driver: the engine paths
  report declined/busy/done instead of refusing the callback, and CPU fills
  and source copies through the mapped aperture backstop them. This matters
  because a declined blit is reported to the application as
  `DDERR_UNSUPPORTED` rather than being emulated.
- DirectDraw source copies now run on the hardware blitter: the ViRGE S3D
  screen-to-screen BitBLT and the Trio32/64 8514/A-compatible equivalent,
  with the CPU copy left as the fallback for shapes neither engine can
  express. Overlapping copies are handled by scan direction rather than row
  order. Ironfield RTS's `BltFast` presentation path went from 3 FPS to 18
  (ViRGE) and 16 (Trio64), level with the direct-backbuffer path instead of
  six times slower, with every frame's blit engine-executed and no engine
  timeout or reset.
- V9XDDP repeats its overlap check on a display-pitch surface. The offscreen
  surface it used has its own pitch, which only an engine with per-surface
  base and stride registers can address, so the Trio64 engine copy had no
  pixel-verified coverage and silently fell back to the CPU for every probe
  blit.
- The CPU fill and source-copy fallbacks move a dword per iteration instead of
  a byte. A byte loop over a 640x480x16 frame cost roughly 700 ms, which
  Ironfield RTS's `BltFast` presentation path turned into 1 FPS; widening it
  trebled the frame rate. The remaining cost is the video-to-video aperture
  round trip, which needs the screen-to-screen BitBLT engine.
- V9XDDP covers an overlapping same-surface copy in both directions over a
  per-row and per-column ramp, so a wrong copy direction shows up as a
  repeated band. The previous distinct-surface copy could not reach that code
  at all.
- Windows 98 DirectDraw no longer reports `DDCAPS_NOHARDWARE` for the Trio64
  target. The runtime discards a driver's entire `DDHALINFO` — not just its
  blitter — when `DDCAPS_BLT` is set without ROP3 `SRCCOPY` in `dwRops`, so
  no HAL callback was ever dispatched and every DirectDraw operation was
  served by the software HEL. The driver now advertises `SRCCOPY` alongside
  `PATCOPY`, drops the inaccurate `DDCAPS_VBI` claim, and implements bounded
  video-memory source copies, because a HAL that claims `DDCAPS_BLT` and then
  declines a blit gets `DDERR_UNSUPPORTED` returned to the application rather
  than a HEL fallback. Guest-verified: hardware page flips, vertical-blank
  waits, and engine-executed solid fills with correct pixels.
  See [docs/issues/2026-08-14-directdraw-hal-nohardware.md](docs/issues/2026-08-14-directdraw-hal-nohardware.md).
- The Trio64 HAL clamp now applies to the `DDHALINFO` copy handed to
  `DDHAL_SetInfo` instead of the shared block, and the duplicate clamp in
  `DriverInit` — which ran before the 16-bit side had published the engine
  identity it branched on — was removed. `ddCaps.dwVidMemTotal` and
  `dwVidMemFree` are refreshed from the framebuffer descriptor instead of
  being computed before it is valid, where they were always zero.
- Restored the `C1_DIBENGINE` GDI-info declaration, dropped earlier on the
  disproven theory that it caused `DDCAPS_NOHARDWARE`. The driver does build
  its PDEVICE with `CreateDIBPDevice` and forward output to the DIB Engine.
- Runtime GDI-info queries no longer overwrite an existing `enable-ok` boot
  marker, and settings now report the active DirectDraw acceleration subset.
- Direct3D primary and flip-chain render targets now use the live scanout
  pitch, dimensions, and RGB565 description instead of potentially stale
  per-surface metadata. Target layout is included in the callback trace.

### Known limitations

- Direct3D is ViRGE-only and accepts pre-transformed, pre-lit vertices only.
  Transform, lighting, clipping (`dwNumClipVertices` is zero), backface
  culling, lines and indexed primitives are not supported.
- The S3D triangle engine still writes native ZRGB1555 while the 16-bpp
  display mode is RGB565.
- Colour-key transparency, `SORTINCREASINGZ` and `SPECULARFLATRGB` are still
  absent against the retail S3 ViRGE driver's capability set; `dwTextureCaps`
  is `0x27` against its `0x2F`.
- DirectDraw admits no sub-480-line mode outside its own ModeX set, so the
  published 640x400x8 mode is reachable from GDI but not from
  `SetDisplayMode`. The 320x200/320x240 ModeX path reports success and then
  fails in use; applications configured for it can crash. See
  [docs/issues/2026-08-15-doom95-low-resolution-modes.md](docs/issues/2026-08-15-doom95-low-resolution-modes.md).
- GDI acceleration and a hardware cursor are not advertised on either chip.
- Trio64 has no Direct3D and no monitor-power behaviour in this baseline.

## 0.2 - 2026-08-11

### Added

- A flat 32-bit DirectDraw HAL with video-memory surfaces, vertical-blank
  services, CRTC display-start flipping, and bounded ViRGE solid-color fills.
- A minimal Direct3D HAL device and allocation-free context lifecycle.
- Pixel-verified S3D rendering for flat-color, pre-transformed/lit triangle
  lists through the legacy Direct3D v1 `RenderPrimitive` callback.
- DirectDraw, Direct3D, GDI, mode-switch, palette, power, and driver-stage
  guest diagnostics.
- A read-only Velocity9x page in Display Properties and a standalone settings
  utility showing hardware, mode, clock, framebuffer, test, version, and build
  information.

### Changed

- Same-depth resolution changes now apply live; color-depth changes remain
  reboot-selected.
- The supported framebuffer matrix covers 640x480, 800x600, and 1024x768 at
  8 and 16 bpp.

### Fixed

- Corrected the Windows 98 `DDHAL_FLIPTOGDISURFACEDATA` ABI layout and added
  an exclusive-mode lifecycle callback that restores CRTC display start when
  returning from flipped DirectDraw surfaces to the GDI desktop.
- Prevented unattended GDI validation from reporting false pixel failures
  when a boot-time utility dialog obscures the sampled client area.

### Known limitations

- S3D triangle output is native ZRGB1555 while the current 16-bpp Windows mode
  is RGB565; version 0.2 proves hardware execution but is not general Direct3D
  compatibility.
- Textures, Z buffering, blending, fog, lighting, transforms, clipping, lines,
  and indexed primitives are not supported.
- GDI acceleration and a hardware cursor are not yet advertised.

## 0.1 - 2026-08-08

### Added

- Initial repository structure, portable driver core, S3 ViRGE/DX device
  targeting, Win16 display-driver skeleton, mini-VDD lifecycle probe, host
  tests, diagnostics, packaging, and recovery documentation.
