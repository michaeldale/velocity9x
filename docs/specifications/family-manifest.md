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
    Variants = @( @{ Id = '8bpp'; Defines = @(); AllowedModeIndexes = @(0); Default = $true } )
}
```

`Sources` is both the compile order and the link order. Reordering it changes
the linked image, so a reorder needs a golden re-baseline.

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
