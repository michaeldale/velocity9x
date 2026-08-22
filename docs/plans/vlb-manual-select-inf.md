# A manual-select INF model, then the first driver run on the 486

## Context

The [VLB bring-up handoff](../handoffs/2026-08-21-vlb-bringup.md) leaves the S3
driver *capable* on VESA Local Bus but *not installable*. The aperture question
is answered — `0x7F000000` decodes once CR58[4] is set, which is exactly what
`v9x_s3_enable_linear_aperture` already does — and `identify_without_pci` is
wired, host-tested, and fires only when `V9xPciBiosPresent()` says there is no
PCI BIOS at all. What blocks the first run is the INF: it advertises only
`PCI\VEN_5333&DEV_xxxx` models, and SetupX cannot bind those on a bus it does
not enumerate. The 486's Win95 guest root-enumerates the card as `*PNP0913`.

The design is settled by the handoff and by evidence from Win95's own
[MSDISP.INF](../decisions/2026-08-21-win95-msdisp-486vlb.inf): a second model
with **no hardware ID at all** — Windows' own pattern, eight such models in
that file (e.g. `%SuperVGA.DriverDesc%=SVGA` at line 182, model line minus the
ID field, install section entirely ordinary). Manual-select only, installable
over a device that has an ID because the display class permits the override.
**Do not bind `*PNP0913`** — it covers every S3 801/805/928 card `DETECTS3801`
finds, and we have code for none of them; claiming it reproduces the Mach64 D3
failure exactly. (Deviation: `a1709d0` binds it as a *compatible* ID, as a
diagnostic against Code 24. It did not fix it. The
[handover](../handoffs/2026-08-22-vlb-manual-select-handover.md), section 5.5,
requires removing the binding before merge unless the experiments prove it
necessary.)

Decisions taken for this plan:

- The manual model advertises a **2 MiB-safe mode subset**. The physical card
  has 2 MiB; the trio64 manifest list is declared and validated against 4 MiB,
  and two of its rows (16bpp 1280x1024 at 2.5 MiB, 32bpp 1024x768 at 3 MiB) do
  not fit. The handoff says to check those rows before installing rather than
  after; a registry mode the driver refuses at Enable fails at the next boot
  with only a stage code.
- The plan includes the **install-and-observe loop** on the 486 via the remote
  agent, not just the INF work.
- The schema-2 survey regression on the 86Box PCI targets stays outstanding;
  it is independent of everything here.

**No C source changes anywhere.** The driver side is complete
(`src/display16/enable16.c:154-158`, `src/chipsets/s3/s3_hw16.c:114`); the
runtime reads real VRAM from CR36, so the heap sizes itself. This is a
packaging change plus a test drive.

## Part A — the INF binding

Three coordinated pieces, as the handoff prescribes: a manifest field, the
emitter, and the check widened deliberately.

### A1. `scripts/lib/family.ps1` — helper, schema, uniqueness

1. **New helper `Get-V9xFamilyManualSelectModes -Family`.** Returns `@()` when
   `Inf.ManualSelect` is absent; otherwise the intersection of all chips'
   `Modes` (keyed bpp/width/height/refresh, first chip's ordering) filtered by
   `width * height * bpp/8 <= ManualSelect.VideoMemoryBytes`. The arithmetic is
   exact, not approximate, because `test_mode_pitches_are_packed`
   (`tests/host/test_hw16_modes.c:172`) already proves every s3 mode is packed
   linear. Both the emitter and the assertions call this one helper, so they
   cannot disagree about the list.
2. **Schema validation** in `Test-V9xFamilyManifest`, guarded so matrox-m2's
   `Inf = @{ Generate = $false }` passes untouched. When
   `Inf.ContainsKey('ManualSelect')`:
   - reject if `Generate -eq $false`;
   - require `Description`: non-empty, none of `,` `=` `%` (they would corrupt
     a SetupX model line), and not containing `ddc` or `carddvdd` in any case
     (`Assert-V9xInf`'s forbidden-substring list — fail early with a message
     that says why);
   - require `VideoMemoryBytes`: positive integer;
   - the derived mode list must be non-empty, and `Inf.DefaultMode`
     (`8,640,480`) must name a mode in it — the shared `Velocity9x.Registry`
     writes `DEFAULT,Mode` for the manual model too, and a default the manual
     MODES section does not advertise would boot into an unlisted mode.
3. **Cross-family uniqueness** in `Get-V9xFamilies`, beside the existing PCI
   `$owners` loop (lines 310-325): throw if a `ManualSelect.Description` is
   claimed by two families, or equals any chip's `DeviceDesc` in any family.
   Two identically-named entries in the Have Disk list would make the pick a
   coin toss — the same argument the PCI loop already makes. This is the no-ID
   slot's own uniqueness story rather than a hole in the check, and it runs
   under `check-tree.ps1` through its existing `Get-V9xFamilies` call.

### A2. `scripts/lib/inf.ps1` — emission

In `New-V9xInfText` (line 36), when the family declares `ManualSelect`:

- a header comment line naming the manual model, beside the per-chip
  `Supported adapter` lines, so the header cannot drift from the models below;
- in the models section, after the per-chip loop (lines 84-94):

  ```
  "Velocity9x S3 (VLB, manual select)"=Velocity9x.Install.Manual
  ```

  No third field — MSDISP.INF confirms that syntax for a no-ID model. The
  description is inline and double-quoted, the house style; a `%token%` form
  would trip `Assert-V9xInf`'s unresolved-token check (line 219).
- a new install section, the same shape as the per-chip ones:

  ```
  [Velocity9x.Install.Manual]
  CopyFiles=Velocity9x.Copy
  DelReg=Velocity9x.Previous
  AddReg=Velocity9x.Registry,Velocity9x.Registry.Manual
  ```

- a new `[Velocity9x.Registry.Manual]` section emitting
  `Get-V9xInfModeLines -Modes $manualModes`, after the per-chip registry
  sections (lines 181-189). The single-chip inlining condition at line 156
  becomes `if ($chips.Count -eq 1 -and -not $manual)` so a future single-chip
  family with `ManualSelect` does not leak its full list into the shared
  section the manual model also AddRegs — no effect on any current family;
- in `[Strings]`: `DeviceDesc.manual="<Description>"`, matching the per-chip
  pattern (informational; SetupX reads the inline text).

Expected in `build\win98se-s3\VELOCITY9X.INF`: the model line, the install
section, and a manual MODES section with **10 modes** — trio64's 12 minus
`16/1280,1024` and `32/1024,768`, exactly the two the manifest's own comment at
`packaging/families/s3/family.psd1:226-231` says the 2 MiB card must refuse.

### A3. `Assert-V9xInf` widened (scripts/lib/inf.ps1:211-268)

The existing hardware-ID set-equality check is untouched — a no-ID model line
contributes no `PCI\VEN_` match, so it passes by construction. What is added is
positive:

1. **Exactly one no-ID slot, and only when declared.** Extract the
   models-section body (between the `[<ModelsSection>]` header and the next
   section). Every line must match
   `^"[^"]+"=\S+,PCI\\VEN_[0-9A-Fa-f]{4}&DEV_[0-9A-Fa-f]{4}$`, except: when
   `ManualSelect` is declared, exactly one line must equal the literal manual
   model line; when not declared, zero ID-less lines are permitted and
   `Velocity9x.Install.Manual` must be absent from the whole text. Any
   accidental second ID-less model, from any future edit, fails the build.
2. **The manual MODES section is the derived list, exactly.** When declared:
   the `[Velocity9x.Registry.Manual]` body must contain one
   `MODES\{bpp}\{w},{h}` entry per mode from
   `Get-V9xFamilyManualSelectModes`, and no `MODES\` line outside that set —
   the pruned modes must not reappear. The install section must carry
   `AddReg=Velocity9x.Registry,Velocity9x.Registry.Manual`.

**No new host tests and no family-matrix row.** A pruned pseudo-row would
break `test_mode_tables_match_manifests` (`tests/host/test_hw16_modes.c:127`),
which insists every matrix row equals the family's hand-written C table
row-for-row — and C-side servability of every member mode is already proven at
4 MiB by `test_advertised_modes_are_servable`. The 2 MiB packed-fit subset is
the only new claim, and the PowerShell assertions above pin it.

### A4. `packaging/families/s3/family.psd1`

Add to the `Inf` block (lines 190-202):

```powershell
ManualSelect = @{
    Description = 'Velocity9x S3 (VLB, manual select)'
    VideoMemoryBytes = 2097152
}
```

with a comment tying it to the 486: root-enumerated `*PNP0913`, no PCI bus,
`identify_without_pci` picks the chip at Enable, and 2 MiB is why the list is
the pruned subset. Update `Floppy.HardwareIdHint` (line 213) to mention VLB via
manual select — it is a hand-written display string in the floppy README chip
table, 73-column constraint noted at `build-floppy-package.ps1:75`.

### A5. Documentation and operator text

- `docs/specifications/family-manifest.md` (a check-tree required file):
  document `ManualSelect` — optional; keys and their constraints; the model
  line carries no hardware ID; the MODES list is derived, not declared; the
  assertions that pin it; incompatible with `Generate = $false`.
- `packaging/win98se/INSTALL.TXT` (static, copied at
  `build-active-package.ps1:129-130`): extend the model-selection step — on a
  machine with no PCI bus, pick the manual-select entry; the driver identifies
  the chip itself at boot; only modes that fit 2 MiB are offered.
- Optional one-liners, skippable if minimality wins: the floppy README's
  "if it shows neither" sentence (`build-floppy-package.ps1:106-110`) and the
  MANIFEST.TXT `Target:` line (`build-active-package.ps1:143`).
- `CHANGELOG.md`.

### A6. Host verification

```powershell
.\scripts\check-tree.ps1                       # schema + uniqueness
.\scripts\build-host.ps1                       # matrix/hw16 tests unchanged-green
.\scripts\build-active-package.ps1 -Family s3  # emitter + widened Assert-V9xInf
.\scripts\run-checks.ps1                       # full gate, incl. matrox-m2's
                                               # Generate=$false path and floppy
```

Then inspect `build\win98se-s3\VELOCITY9X.INF` by eye: the manual model line
present once; `MODES\16\1280,1024` and `MODES\32\1024,768` in the two chip
sections only. Two negative spot-checks, once: `ManualSelect` commented out —
the absence branch must pass; the description duplicated into a scratch family
— `Get-V9xFamilies` must throw. If a golden baseline covers the s3 package,
recapture it before running `-GoldenCompare`.

## Part B — install and observe on the 486

Target: the Win95 guest, v9x-remote-agent at `10.0.1.217:9869`, driven through
`v9xctl.ps1` (out of repo; `$env:V9X_AGENT_CTL`). Currently running Win95's
built-in `S3.DRV`; `C:\V9XHW.INI` absent; no DirectX, which is fine — the
aperture mapping happens on the 16-bit side at Enable.

Agent discipline, all already paid for once: `-Json` on every call (without it
property access silently yields nothing); `wait-desktop` before any GUI `exec`
or `screenshot`; never screenshot during a mode transition or a suspected
wedge; a reboot is proven by the old connection ending, a new one with the
exact `-JobId` from `PENDING.DAT`, and `wait-desktop` — never assumed. The DOS
probes are not re-runnable from here; anything needing a clean boot (F5, not
F8) is a request to Michael at the keyboard.

1. **Baseline.** `stat C:\V9XHW.INI` (expect absent — its later appearance is
   itself the first proof), a screenshot, the current driver noted.
2. **Push.** `put` the built s3 package (staged at `build\vm-probe\S3`) to a
   guest folder, e.g. `C:\V9XPKG`.
3. **Install.** Device Manager → the `*PNP0913` display device → Change
   Driver → Have Disk → `C:\V9XPKG` → **"Velocity9x S3 (VLB, manual select)"**.
   Driven via `input` + `screenshot` iterations; fall back to Michael at the
   keyboard only if the Win95 install GUI proves undriveable remotely.
4. **Proven reboot**, per the discipline above.
5. **Observe.** The three questions, all 16-bit: does `identify_without_pci`
   fire, does the aperture map, does a mode set land.
   - `get C:\V9XHW.INI` — read which adapter/chip the driver recorded;
   - the boot trace / `C:\BOOTLOG.TXT` for the driver load;
   - `wait-desktop`, then a screenshot of the desktop at a Velocity9x mode;
   - Display Settings offers the 10-mode list; one mode change lands.
6. **On failure.** A stage-1 refusal or a desktop that never comes up: capture
   the stage code and boot trace before changing anything; the package ships
   `RECOVER.TXT` and `V9XFIX.BAT`. Do not install DirectX (ask first, per the
   handoff). Win95 wrinkles to keep in mind: `MSDISP.INF` not `DISPLAY.INF`,
   and no Win95 evidence anywhere else in the tree — this is the first Win95
   machine the driver has met.
7. **Record.** Changelog entry plus a decision or handoff note in the house
   pattern: whether `0x7F000000` mapped from protected mode, which mode the
   desktop landed on, and anything Win95-specific worth not re-deriving.

## Files touched (Part A)

- `scripts/lib/family.ps1` — helper, schema validation, cross-family
  uniqueness
- `scripts/lib/inf.ps1` — emission and `Assert-V9xInf`
- `packaging/families/s3/family.psd1` — `ManualSelect`, floppy hint
- `docs/specifications/family-manifest.md`, `packaging/win98se/INSTALL.TXT`,
  `CHANGELOG.md`

No C source changes; no new host tests.
