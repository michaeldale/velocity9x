# The S3 family merge

Date: 2026-08-16
Status: accepted

Phase 8 of `docs/plans/multi-chip-restructure.md`. The `s3-virge` and
`s3-trio64` families become one `s3` family: one binary, one INF with two
models, and runtime dispatch between the chips by PCI id. This is the first
time the restructure's premise is actually exercised, and it is the sync point
between the source track and the build track.

## What had to change in the source

Everything up to here had one chip per binary, so "the family" and "the chip"
were the same thing and `v9x_hw16_ops` could hold both. With two chips in one
image that stops being true, and the split falls exactly where the two chips
differ:

- `enable_aperture` and `fill_engine_descriptor` moved from the family ops
  table into `V9X_HW16_DEVICE`. They are the only two hooks the ViRGE and the
  Trio64 disagree about - the ViRGE opens its CR53 new-MMIO window and
  describes an S3D engine, the Trio64 does neither - and making them per chip
  is what lets one image serve both without a branch on chip anywhere.
- `V9X_HW16_OPS.devices` became an array of pointers rather than an array of
  structs. A flat array would have had to live in one file and drag every
  chip's identity and hooks into it; pointers let each chip module own its
  complete entry, which is also what the per-object audit needs.
- `V9xFindPciDevice` now records the index it matched into `_v9x_pci_match`,
  and `v9x_hw16_active_device()` resolves it. That index is the one thing the
  scan learns that the manifest cannot state in advance. Before Enable, or when
  nothing matched, it falls back to the first entry, which is what a
  single-chip family always saw.
- `src/chipsets/s3/s3_hw16.c` holds what the two genuinely share: the mode
  table, the mode-set flags, the aperture map size, the diagnostics publisher
  and the CR59/CR5A aperture read. The chip modules hold what they do not.

The mode table is shared rather than duplicated. Both chips take the same S3
BIOS mode numbers at the same audited pitches, and two copies of one fact can
disagree.

### A latent bug this surfaced

`dd16.c` used to derive the identity flags as "Trio64 if `engine_type` is
Trio64, otherwise ViRGE". That reads as ViRGE for any type it does not
recognise, `NONE` included, so a family with a descriptor hook and no engine
claimed a ViRGE. It was inert only because `v9x_engine_ready` separately
demands a non-zero control base and a mapped aperture. It went away with the
identity bits themselves (`docs/decisions/2026-08-16-engine32-vtable.md`); the
merged family is what made it worth writing down, because a family with several
chips is where that fallback would first have been reached.

## What the audits now prove

The image-wide signature check cannot tell two sibling chips apart - both
chips' code is legitimately present. So the per-object layer, dormant since
phase 3, is what carries the weight here:

- `virge_hw16.obj` must contain the CR53 sequence (`mov ax,53H`, `or al,8`,
  `test al,8`); `trio_hw16.obj` must not, by sibling derivation. Verified
  directly: the ViRGE object has all three, the Trio object none.
- The shared S3 unlock (`or al,13H`, `cmp al,13H`) moved from per-chip
  required to family-wide required, because it lives in `s3_regs16.obj` which
  both chips call. A per-chip requirement for it would have failed on both.
- The Trio64 declares **no** required instructions. What makes it the Trio64 is
  its PCI id and its engine descriptor, both data. Its coverage is the derived
  forbidden set, its map symbol, and the INF hardware-ID set equality.
  Declaring a signature it does not own would be a check that proves nothing.

`Chips[].Objects` is now validated by the loader: names must resolve to real
objects, and a family declares it for every chip or for none - declaring it for
some would audit those and silently skip the rest, which reads as coverage it
does not have.

## The INF

Generation already handled multiple models; what it did not handle was saying
so. The header hardcoded "First and only supported adapter: S3 ViRGE/DX", and
`[Strings]` carried a single `DeviceDesc` taken from the first chip. Both are
generated per chip now. The hardware-ID set equality assertion is what makes
the two-model INF safe, and it was already in place from phase 3.

## Gate

`run-checks.ps1 -BuildId golden-compare` green: 2 families, all three audit
layers, 2 per-chip objects audited.

Both guests were updated to the one `build/win98se-s3` package. The identity
each publishes is the merge working:

| `C:\V9XHW.INI` | ViRGE guest (9869) | Trio64 guest (9871) |
|---|---|---|
| Adapter | `S3 ViRGE/DX 86C375` | `S3 Trio32/64 86C764` |
| Vendor / device | `5333` / `8A01` | `5333` / `8811` |
| Direct3D | `hardware-s3d` | `not-advertised` |
| CoreClockKHz | 56079 | 69800 |
| Stage | `enable-ok` | `enable-ok` |

Every field matches the phase 1 baseline, from one image.

`run-vm-mode-matrix.ps1 -Family s3 -ChipId virge-dx` and `-ChipId trio64` both
passed all six modes: `enable-ok` after every reboot, GDI PASS, palette PASS in
all three 8-bpp modes. That is the phase 8 gate.

The phase 7 set was rerun as a regression and reproduced exactly: ViRGE
`CountBlt`/`CountBltEngine` 7/7 with the full 11-item D3D gate set and
`EngineCaps` `0x1F`; Trio64 6/3, no D3D HAL, `EngineCaps` `0x0F`. One binary,
and the Trio64 still advertises no Direct3D - which is the D3D-leak risk the
plan called out, now closed by measurement rather than by construction.

Code cost: `_TEXT` 14383 bytes against 14023 for the ViRGE-only build, so +360
bytes to carry a second chip. Well inside the 2 KiB per-step budget.

## What ended here

Byte-for-byte golden compare against the pre-restructure images. One package
containing both chips cannot reproduce either single-chip package, and that is
the intended outcome rather than a regression. `golden-baseline.ps1` now tracks
`build/win98se-s3` and is a build-to-build comparison and a code-size budget,
not a comparison against phase 0.

Also retired: the `s3-virge` and `s3-trio64` manifests, `LegacyOutputName` /
`LegacySkeletonOutput` / `LegacySwitch`, the `-S3Trio64` and
`-MatroxMillennium2` aliases on the family builders, and the checked-in
`packaging/win98se/velocity9x.inf` - the INF has been generated since phase 3
and the checked-in copy was already only a source of drift.

## The SetupX install, measured

The plan flagged per-model `MODES` AddReg as the one Win98-specific mechanism
worth an empirical check. It was run on both guests, from cold profile backups,
through the documented Have-Disk procedure in `docs/INSTALL.md`.

**It works, in both directions.** Given one INF carrying two models, the Select
Device dialog offered exactly the model matching the fitted card - only
`Velocity9x S3 ViRGE/DX 86C375` on the ViRGE guest, only
`Velocity9x S3 Trio32/64 86C764` on the Trio64 - and binding produced the
per-chip section:

| after install | ViRGE guest | Trio64 guest |
|---|---|---|
| `InfSection` | `Velocity9x.Install.virge-dx` | `Velocity9x.Install.trio64` |
| `DriverDesc` | `Velocity9x S3 ViRGE/DX 86C375` | `Velocity9x S3 Trio32/64 86C764` |
| `MatchingDeviceId` | `PCI\VEN_5333&DEV_8A01` | `PCI\VEN_5333&DEV_8811` |
| `MODES` applied | 8: 640x400/480, 800x600, 1024x768; 16: three | identical |
| driver after reboot | `enable-ok`, `hardware-s3d`, 56079 kHz | `enable-ok`, `not-advertised`, 69800 kHz |

Both then reproduced their full phase 7 readings from the INF-installed driver:
ViRGE 7/7 blits with the complete 11-item D3D gate set and `EngineCaps` `0x1F`,
Trio64 6/3 with no D3D HAL and `0x0F`. The guests are now installed the way a
user installs, rather than by the file replacement they had been carrying.

### Auto-detect does not choose this driver

Worth recording because it is easy to assume otherwise. The first attempt drove
the install by deleting the device's `Enum` key and letting PnP re-detect on
reboot, with the INF placed in `C:\WINDOWS\INF` and the driver-info cache
invalidated. Windows 98 installed **Microsoft's in-box `DXS3.INF`** instead -
`drv=s3.drv`, `ProviderName=Microsoft` - and the Velocity9x driver was
displaced.

That is not a defect: `docs/INSTALL.md` step 6 has always said to pick the
Velocity9x entry explicitly through Have Disk. But it means a re-detect is not
a shortcut for reinstalling, and anyone who removes the display device
expecting the driver to come back will get the in-box S3 driver instead. The
guest was recovered by re-importing its pre-test registry export, which is a
cheaper revert than the profile backup and worth knowing works.

### Two things found in passing

`backup-86box-profile.ps1` named every backup `Win86SE-*` regardless of the
profile it copied. Contents were always correct, which is the worse failure -
nothing looks wrong until someone restores the wrong disk. Fixed.

`run-vm-mode-matrix.ps1` writes the mode to
`Services\Class\Display\0001\DEFAULT`, but the active display key index is per
guest: `0001` on the ViRGE, `0002` on the Trio64, because each carries
leftovers from earlier drivers. The matrix passes anyway because the
`Config\0001\Display\Settings` half of the same `.reg` is what actually takes
effect. It works by accident and should be driven from the device's own
`Driver` value instead.
