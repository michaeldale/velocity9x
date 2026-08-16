# Changelog

All notable Velocity9x changes are recorded here. The project uses semantic
version numbers for product milestones; diagnostic builds retain a separate
build identifier so exact guest-tested binaries remain traceable.

## Unreleased

### Added

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

- `scripts/build-host-msvc.ps1` had a stale source list — the clock, memory,
  registry and Matrox modules were missing — so the second-compiler pass failed
  at the link step on every run and never executed a test. It compiles, links
  and runs again, now over the same source set as the Watcom pass.
- The host build joins the driver and HAL compiles in using `-we`, so a warning
  fails it rather than being reported alongside a successful exit.

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
