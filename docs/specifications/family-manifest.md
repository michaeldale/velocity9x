# Family manifest specification

Status: current (schema version 1)
Recorded: 2026-08-16

A *family* is one built package covering one or more chips that share a driver
binary. `packaging\families\<familyId>\family.psd1` is the single declaration
of what that package contains, how it is built, how it is audited, what its INF
says, and which VM validates it. Build scripts read it; they do not hard-code
chip facts.

The file is PowerShell data (`Import-PowerShellDataFile`): built into
PowerShell 5.1, data-only, and it evaluates no code, so regex audit patterns
need no JSON escaping.

Load it with `scripts\lib\family.ps1`. `check-tree.ps1` validates every
manifest on every run, so a schema error is caught before a build.

## Top-level keys

| Key | Meaning |
| --- | --- |
| `SchemaVersion` | Must equal the loader's version (currently 1). |
| `Id` | Lowercase kebab-case, matching the directory name. |
| `DisplayName` | Human name used in build output. |
| `Description` | One line; what the family covers and any limits. |
| `Chips` | One entry per supported chip. See below. |
| `Build` | Source list, defines, output directories, variants. |
| `Audit` | Family-wide audit additions. |
| `Inf` | INF metadata, or `Generate = $false`. |
| `Package` | Lines the package `MANIFEST.TXT` states. |
| `Floppy` | Whether and where the family rides the transfer disk. |
| `Vm` | Emulator, profile, port and modes for automated validation. |

## Chips

Each chip declares its PCI identity, the strings the driver publishes about it,
its MODES capability, and its instruction signatures.

```powershell
@{
    Id = 'virge-dx'
    Name = 'S3 ViRGE/DX 86C375'
    VendorId = '5333'          # four uppercase hex digits
    DeviceId = '8A01'
    DeviceDesc = 'Velocity9x S3 ViRGE/DX 86C375 (Phase 3 mode matrix)'
    Adapter = 'S3 ViRGE/DX 86C375'   # as written to C:\V9XHW.INI
    Modes = @(
        @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
    )
    Objects = @('virge_hw16')        # this chip's own object(s)
    MapSymbols = @('v9x_virge_device')
    EngineType = 'S3_VIRGE_DX'       # V9X_DD_ENGINE_TYPE_* without the prefix
    EngineCaps = @('SOLID_FILL', 'SCREEN_COPY', 'FLIP', 'VBLANK', 'D3D')
    VideoMemoryBytes = 4194304       # the VRAM Modes is declared against
    Audit = @{ Required = @('mov\s+ax,53H'); Forbidden = @() }
}
```

`Objects` names the objects this chip's code compiles into. It is what makes
the per-object audit layer possible: once two chips share one image, an
image-wide signature check can no longer tell their code apart. Declare it for
every chip in a family or for none - a family that declares it for some would
audit those and silently skip the rest. `EngineType` and `EngineCaps` document
what the chip's `fill_engine_descriptor` hook publishes; they are the per-chip
values the 16-bit caps clamp narrows DDRAW's view from. A chip declaring
`EngineType = 'NONE'` may declare no capabilities: the 32-bit HAL resolves no
ops table for that type, so anything claimed there would be advertised and then
never served.

`VideoMemoryBytes` is the VRAM `Modes` is declared against. It is what the host
family-matrix test binds before asking the backend to lay out each advertised
mode, so it is the number that decides whether a mode list is honest.

`Modes` is the chip's capability, not the driver's mode table. The INF
generator orders it by depth, width, height; `ddi.c`'s table has its own order
(640x400 last, so GDI enumerates it after the other 8-bpp modes).

A PCI ID may be claimed by exactly one family. Two families claiming one device
would give Windows two matching INF models, and which driver installs becomes a
coin toss. `Get-V9xFamilies` enforces this.

## Audit signatures and cross-family derivation

`Chips[].Audit.Required` lists `wdis` patterns that must appear in this chip's
code. Every *other* family's required patterns automatically become this
family's forbidden set, minus anything this family legitimately produces. That
is the whole cross-contamination rule, and it means adding a family
strengthens every existing family's audit with no script edit.

Only patterns that no manifest declares need an explicit `Forbidden` entry.

Not every chip fact is an instruction. The PCI identity and the VBE mode-set
flag live in the chip's `hw16` object as data and are stamped into DGROUP at
load, so no signature can find them. Those are audited instead through
`Chips[].MapSymbols` (the chip's device table must be in the link map, and no
other family's may be) and through the generated INF's hardware-ID set
equality. What remains in `Audit.Required` is the chip register sequence,
which is still instructions.

Match the disassembler's spelling, not the assembler's: `wdis` prints small
immediates bare, so `or al,08h` in the source is `or\s+al,8\b` in a pattern.

**Anchor the immediate.** The `\b` is not decoration. These patterns are
regexes over disassembly text, so an unanchored `test\s+al,8` also matches
`test al,80H`, and because required patterns become other families' forbidden
patterns, the false positive surfaces as a build failure in a *different*
family from the one that owns the pattern. That happened: the ViRGE's CR53
pattern shipped unanchored and convicted the Matrox image the moment shared
code gained a `test al,80H`. See
`docs\decisions\2026-08-16-vbe-tier0-family.md`.

**Only claim instructions your own objects own.** The audit scans every object
in the image, including shared ones that every family links. An instruction in
`src\common\` or `src\display16\` appears in all families, so declaring it as a
required pattern forbids it everywhere else and fails those builds while
proving nothing about yours. The `vbe` family declares no required patterns for
exactly this reason - its 4F00h/4F01h calls live in shared `vbe16.c`. If a
family needs a signature, move the code that produces it into that family's own
object first.

`scripts\audit-family-binary.ps1` applies three layers:

1. **Cross-family** - the family image matches all its chips' signatures and no
   other family's.
2. **Per-chip objects** - `Chips[].Objects` names objects that must carry that
   chip's signatures and none of its siblings'. This is the layer that does the
   work in a multi-chip family: the S3 image contains both chips, so only this
   can assert that the ViRGE's CR53 new-MMIO poke is absent from the Trio64's
   object. A chip whose identity is entirely data declares no required patterns
   and is covered by the derived forbidden set, its map symbol and the INF
   hardware-ID equality - declaring a signature it does not own would be a
   check that proves nothing.
3. **Link-map symbols** - `Audit.RequiredMapSymbols`, `Audit.DispatchSymbol`,
   `Chips[].MapSymbols` are required; other families' `Audit.BackendSymbols`
   are forbidden.

Chip-agnostic audits (VDD handoff, DIB thunk guards, NE header, exports,
segment flags) stay as script logic in the auditor - they are the same for
every family.

## Build

```powershell
Build = @{
    Sources = @( @{ Name = 'ddi'; Path = 'src\display16\ddi.c' } )
    Defines = @('V9X_TARGET_S3_TRIO64=1')     # wcc
    RuntimeDefines = @('V9X_TARGET_S3_TRIO64=1')  # wasm, runtime.asm
    SkeletonOutput = 'build\win16-ddi-s3'
    PackageOutput = 'build\win98se-s3'
    VmStageDirectory = 'build\vm-probe\S3'
    MiniVddVbeCollect = $false
    Variants = @( @{ Id = '8bpp'; Defines = @(); AllowedModeIndexes = @(0); Default = $true } )
}
```

`Sources` is both the compile order and the link order. Reordering it changes
the linked image, so a reorder needs a golden re-baseline.

`MiniVddVbeCollect` is optional; absent means `$true`. `$false` builds the
family's `V9XMINI.VXD` with the boot-time VBE collection assembled out
(`build-minivdd-skeleton.ps1 -DisableVbeCollect`), which is correct for any
family whose chips have a `read_aperture` hook: such drivers never consult the
mini-VDD's 4F9Ch cache, and the collection is eight nested BIOS calls at
`Device_Init` with nothing to show for them. Tier-0 families must leave it on.
See `docs\decisions\2026-08-18-minivdd-vbe-collect-gating.md`.

`Variants` are build-time flavours of one family (the Matrox 8-bpp and 16-bpp
drops). A family with no variants omits the key.

`LegacyOutputName` and `LegacySwitch`, which kept the pre-restructure directory
names and command-line switches alive, were retired at phase 8 along with the
byte-for-byte golden compare they existed to serve.

## Inf

`scripts\lib\inf.ps1` generates a Chicago INF from this section plus `Chips`:
one `[Velocity9x.Models]` line and one install section per chip, shared copy
and registry sections, per-chip `MODES` AddReg. A single-chip family produces
exactly one install section named `Velocity9x.Install`.

The generated hardware-ID set is asserted equal to the family's declared set -
that set equality is what lets a family carry more than one chip.

A family that installs by guarded file replacement sets `Generate = $false` and
supplies no other INF keys.

### Inf.ManualSelect

Optional. It adds one more models line, with **no hardware ID field at all**:

```powershell
Inf = @{
    # ... Provider, Manufacturer, DiskName, DefaultMode, ForcedModes
    ManualSelect = @{
        Description = 'Velocity9x S3 (VLB manual select)'
        VideoMemoryBytes = 2097152
    }
}
```

```
"Velocity9x S3 (VLB manual select)"=Velocity9x.Install.Manual
```

That is Windows' own pattern for a manual-select display model - `MSDISP.INF`
carries eight of them - and it is the only way to reach a card on a bus Windows
does not enumerate. The case it was written for is the 486: an S3 Trio64 on
VESA Local Bus, root-enumerated by Win95 as `*PNP0913`, on a machine with no
PCI bus for SetupX to match a `PCI\VEN_` model against. The display class
permits the override, so the entry installs over a device whose real ID nothing
in the INF claims, and `identify_without_pci` picks the chip at Enable.

Binding `*PNP0913` instead would be wrong: that ID covers every S3 801/805/928
card Win95's `DETECTS3801` finds, and claiming it would offer this driver for
parts it has no code for.

| Key | Meaning |
| --- | --- |
| `Description` | The entry as it appears in the Have Disk list. |
| `VideoMemoryBytes` | The VRAM the *physical* card has, which narrows the mode list. |

`Description` is emitted inline and double-quoted, matching the per-chip model
lines. It may not contain `,`, `=` or `%` - the first two are SetupX field
separators and the third would read as a `%token%` the INF never resolves - nor
`DDC` or `carddvdd`, which `Assert-V9xInf` forbids anywhere in a generated INF.
`Get-V9xFamilies` additionally requires it to be unique across every family's
manual descriptions *and* every chip's `DeviceDesc`: it is the only thing
distinguishing the entry, with no hardware ID to fall back on, so a duplicate
would make the pick a coin toss for a human the way a duplicate PCI ID does for
Windows.

**The MODES list is derived, never declared.** `Get-V9xFamilyManualSelectModes`
returns the intersection of every chip's `Modes`, in the first chip's order,
narrowed to those where `width * height * bpp/8 <= VideoMemoryBytes`. The
intersection is because a model with no hardware ID can be picked over any card
in the family, so it may only offer what all of them serve; the fit is exact
rather than approximate because `test_mode_pitches_are_packed`
(`tests\host\test_hw16_modes.c`) proves every mode is packed linear. Modes are
pruned here rather than refused at Enable, where the failure is a black screen
and a stage code at the next boot. The 2 MiB S3 card drops two of the twelve
rows its chips declare against 4 MiB: 16bpp 1280x1024 (2.5 MiB) and 32bpp
1024x768 (3 MiB).

The generated model gets its own `[Velocity9x.Install.Manual]` and
`[Velocity9x.Registry.Manual]`, and AddRegs the shared `Velocity9x.Registry`
alongside its own. Declaring `ManualSelect` therefore also forces per-chip
registry sections even in a single-chip family, so no chip's full list leaks
into the shared section the manual model reads.

`Generate = $false` and `ManualSelect` are incompatible: a family with no
models section has nowhere to put the model.

What pins all of this, beyond the schema checks above:

* the hardware-ID set equality is unaffected - an ID-less line contributes no
  `PCI\VEN_` match, so it passes by construction;
* every line in the models section must be either a `PCI\VEN_...&DEV_...`
  model or, when `ManualSelect` is declared, the one literal manual line. Any
  accidental second ID-less model fails the build, and a family that declares
  none may not mention `Velocity9x.Install.Manual` at all;
* `[Velocity9x.Registry.Manual]` must hold exactly the derived `MODES` list and
  nothing else, so a pruned mode cannot reappear;
* the effective default mode - `Inf.DefaultMode`, or whatever
  `build-active-package.ps1 -ForceModeIndex` overrides it with - must be one
  the manual model advertises, because `DEFAULT,Mode` is written by the shared
  registry section this model also AddRegs. A forced index outside the derived
  list is refused at build time rather than shipped.

There is deliberately no host test and no family-matrix row for the pruned
list. A pseudo-row would break `test_mode_tables_match_manifests`, which
insists every matrix row equals the family's hand-written C table row for row,
and C-side servability of every member mode is already proven at the chips'
declared VRAM by `test_advertised_modes_are_servable`. The 2 MiB packed fit is
the only new claim, and the assertions above are what hold it.

## Vm

```powershell
Vm = @{
    Emulator = '86box'      # or 'none' for physical-hardware-only families
    Controller = 'virge_dx_pci'
    Profile = 'Win86SE'
    Port = 9869             # remote-agent port for this guest
    ReferenceProfile = 'Win98SE-Native-S3'
    ReferencePort = 9870
    Modes = @('640x480x8', '1024x768x16')
    Targets = @(                       # one per chip; required when >1 chip
        @{ ChipId = 'virge-dx'; Profile = 'Win86SE'; Port = 9869 }
        @{ ChipId = 'trio64'; Profile = 'Win98SE-Trio64'; Port = 9871 }
    )
}
```

`run-vm-mode-matrix.ps1 -Family <id> [-ChipId <chip>]` takes the port, the
package directory and the mode list from here. `Emulator = 'none'` makes it
refuse with an explicit real-hardware-only error rather than silently testing
the wrong guest.

`Targets` is one VM per chip, and a multi-chip family with an emulator must
declare it: without it a matrix pass on one guest would read as a pass for the
whole family, which is exactly the claim a merged binary has to prove. The
family is green only when every target passes from the same package.

## Adding a family

1. Create `packaging\families\<id>\family.psd1`.
2. Run `scripts\check-tree.ps1`; fix schema and cross-family errors.
3. Run `scripts\build-host.ps1`. It regenerates `build\host\v9x_family_matrix.h`
   from every manifest and runs the family-matrix tests, which will refuse a
   chip the backend registry does not resolve, a chip that shares a backend
   with another family, an engine capability declared against no engine, and a
   mode the backend cannot lay out in the declared VRAM. This is the cheapest
   feedback in the sequence — no emulator, no Windows.
4. Build it: `scripts\build-active-package.ps1 -Family <id>`.
5. The audit will report any signature that the manifest claims but the
   binary does not produce, and vice versa.
6. Add the manifest path to `check-tree.ps1`'s required-file list.

## The generated family matrix

`build-host.ps1` writes `build\host\v9x_family_matrix.h` from the manifests and
compiles `tests\host\test_family_matrix.c` against it. The point is that the C
side restates parts of the manifest by hand — the backend registry dispatches
on literal PCI ids, and each backend decides for itself which modes it can lay
out — and nothing else makes the two agree.

The mode check runs one way only, deliberately. `validate_mode` is a layout
calculator bounded by VRAM, not a whitelist, so it cannot be asserted to accept
*only* the advertised modes; it will lay out plenty no INF mentions. What it is
held to is the direction that matters: every advertised mode must be servable,
and a mode that does not fit the declared VRAM must be refused.
