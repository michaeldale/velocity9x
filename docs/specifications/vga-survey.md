# VGA hardware survey report contract

`V9XSURV.EXE` is a real-mode DOS tool given to owners of cards Velocity9x does
not support, to collect what writing a chipset backend requires. It writes one
INI report, by default `C:\V9XSURV.INI`, consumed by
`scripts/parse-vga-survey.ps1`.

This contract is separate from `hardware-diagnostics.md`. That one describes what
a *running driver* publishes about hardware it already supports; this one
describes what an *external tool* captures about hardware nothing here supports
yet.

## The governing rule

The tool captures; it does not interpret. PCI configuration space, the video BIOS
image and EDID go into the report verbatim as hex, and every decode happens
host-side. A decoding mistake is then fixed by editing a PowerShell script and
re-running it over every report already collected, rather than by shipping a new
executable to everyone who helped.

The only interpretation the tool performs is what it prints on screen so a tester
can see the run worked.

## Safety tiers

**Tier 1** always runs. It writes no device register except to select an index,
and restores every index it touches. PCI is read through PCI BIOS function
`B108h` only; the write functions `B10Bh`/`B10Ch`/`B10Dh`, the VBE setters
`4F02h` and `4F05h`-`4F0Bh`, and VBE `4F14h` (OEM extension) do not appear in the
source, and `scripts/build-vga-survey.ps1` fails the build if they ever do. Since
schema 2 the same gate also holds an allowlist of the literal port constants and
of the raw opcode bytes inline assembly may emit. No BAR sizing is performed,
because determining an aperture size means writing to a BAR on a live device.

**Tier 2** is opt-in, and the report records which way the tester answered. It
writes documented per-vendor unlock keys, reads the registers behind them, and
restores the originals. It is dispatched on PCI vendor ID, so an unfamiliar card
is never poked speculatively — and where there is no PCI, on the card's own
locked identity registers instead. See *Identification without PCI* below.

**The aperture probe** is opt-in again, on its own `/aperture` switch, which
implies `/tier2` because the window base is something only Tier 2 can read.
`/notier2` still wins over it. It is the only step that reads an address the card
claims rather than a register the card answers for, and it runs last.

Tier 1 is closed on disk before Tier 2 begins, and Tier 2 reopens the file in
append mode. If a vendor probe wedges an unknown card, the tester still has a
complete Tier 1 report. `scripts\build-vga-survey.ps1 -GateSelfTest` asserts the
source gate rejects each thing the tool must never grow, and needs no compiler.

In practice a declined Tier 2 costs less on S3 parts than it looks: the Tier 1
register dump, which unlocks nothing, already carries CR2D/CR2E (device ID),
CR30 (chip ID) and CR36 (memory size).

The reason is not the one first recorded here. The ViRGE/DX and Trio64 test
machines both show CR38/CR39 already holding `48`/`A5` before the survey runs,
which looked like the explanation — the video BIOS leaving the bank unlocked at
POST. The 486 VLB Trio64 measured on 2026-08-21 holds `59`/`BD` in those
registers instead, and its locked Tier 1 dump still returned the true
CR2D/CR2E/CR30. So the actual reason is that **on S3 these locks gate writes,
not reads**, which is a much better property to be relying on. See
`docs/decisions/2026-08-20-vlb-survey-schema2.md`.

On that same part, writing `48` to CR38 and reading it back returns neither `48`
nor the value read before the write: CR38 and CR39 are key latches whose read
is not their written value. Tier 2 still saves and restores them, and the end
state is provably unchanged — the Tier 1 bank before any unlock is byte-identical
to the one after — but do not read that restore as having put a real value back.

Do not assume any of this of an unfamiliar card. Do read a Tier 2-declined S3
report before asking the tester to run it again.

Implemented Tier 2 families: S3 (`5333`, CR38/CR39 and SR08 unlock) and Cirrus
Logic (`1013`, SR06 key). Every other vendor gets a section with
`Status=unsupported` and a reason — ATI because the register base would have to
come from a table inside the card's own ROM, Trident because reading SR0B
switches the register file mode, Tseng because its unlock writes the
non-indexed CGA and Hercules mode-control ports, and Matrox/nVidia/3dfx/SiS
because their registers are MMIO that real mode cannot reach.

## Identification without PCI

A VESA Local Bus or ISA machine has no PCI configuration space, so there is no
vendor ID to dispatch Tier 2 on. `[Chipset.Identify]` records the fallback, and
the order of operations is the safety property: the identity registers are read
with the locks exactly as the BIOS left them, and the CR38/CR39 unlock keys are
written only after those reads have already spelled S3.

| Signal | Key | Accepts alone? |
|---|---|---|
| CR2D/CR2E device id, high byte one of six S3 values | `DeviceIdSignal` | yes |
| CR30 chip id, one of nineteen documented values | `ChipIdSignal` | no |
| An S3 string in the ROM image at `C000` | `RomSignal` | never alone |

CR30 is the only id register the pre-Trio parts have, but its documented values
span seven of the sixteen high nibbles, so a CR30-only match is accepted only
when the ROM also names S3. `Accepted=no` carries
`Reason=unidentified-non-pci-display`, and `[Chipset]` repeats it under the name
a schema-1 reader looks for.

All three signals are reported whichever way the decision goes, so a refusal is
diagnosable from the report rather than being a dead end.
`WritesBeforeDecision=none` states the ordering property outright: everything
`[Chipset.Identify]` reports was read, and the unlock happens afterwards, in
`[Chipset.S3]`.

CR38/CR39 on a non-S3 card mean something else. The read-first ordering shrinks
that exposure and does not remove it, and on a machine with no PCI the Tier 2
prompt says out loud that the check is trusting the tester's word about the card
as much as it is trusting the registers.

## Formatting rules

- ASCII, one `Key=Value` per line, no spaces around `=`.
- Parsers must split on the **first** `=` only: strings extracted from a video
  BIOS can contain more.
- `;` begins a comment only at the start of a line.
- Hex is uppercase without a `0x` prefix, fixed width to the field
  (`VendorId=5333`, `Bar0=F8000008`).
- Keys ending `Bytes`, `KB`, `Count` are decimal; others are hex unless noted.
- Blobs are offset-keyed lines of contiguous hex, 16 bytes per line:
  `Config.00=3353018A83000002...`. Offsets are two hex digits for blobs up to
  256 bytes and four for the ROM image. One rule reassembles config space, the
  ROM and EDID alike.
- In a **register bank** blob the offset is the register index, not a position
  in a buffer. `Crtc.` and `Seq.` start at index 0, so the two coincide there,
  but the schema-2 banks do not: `CrtcUnlocked.` starts at `30` and
  `SeqUnlocked.` at `08`. A reader should never have to add a base back on to
  know which register a byte came from.

## Status vocabulary

Every section carries a `Status`. A probe that could not run says so — it is
never silently omitted, because the parser has to distinguish "this card does not
have that" from "we never looked".

| Value | Meaning |
|---|---|
| `ok` | probed, data present |
| `unavailable` | the mechanism answered, but with nothing usable |
| `unsupported` | the call or probe does not exist on this hardware, or policy excludes it |
| `skipped` | deliberately not attempted |
| `declined` | the tester declined Tier 2 |
| `error` | the probe failed part way |

Sections carrying anything other than `ok` also carry a `Reason`.

## Sections

| Section | Contents |
|---|---|
| `[Report]` | `SchemaVersion`, `Tool`, `Build`, `Access`, `Date`, `Time`, `CommandLine`, `Note` |
| `[System]` | DOS version, `WindowsPresent`, conventional memory, coarse CPU class |
| `[Platform]` | schema 2. Refined CPU class and CPUID, installed RAM by `AH=88h`/`E801h`/`E820h`, A20 state, XMS and EMS presence |
| `[BiosData]` | BIOS data area video fields, the INT 10h and 42h vectors, INT 10h `AH=1Ah` display combination, `AH=1Bh` functionality block |
| `[PciBios]` | INT 1Ah `B101h` presence, version, hardware mechanism, last bus |
| `[PciInventory]` | every PCI function found, one CSV line each, with a `Fields` key naming the columns |
| `[PciDevice.N]` | one per display-class device: decoded header fields plus the full 256-byte `Config.` blob |
| `[VideoBios]` | the ROM at `C000`: size, checksum, `PCIR` structure, `$PnP` header, extracted strings, and a `Rom.` blob |
| `[OptionRom.N]`, `[OptionRomScan]` | secondary adapter ROMs found between `C000` and `E000` |
| `[VBE]` | `4F00h` controller info with the `VBE2` request, OEM strings, current mode, `4F0Ah`/`4F10h`/`4F11h` capability queries |
| `[VBEModes]` | the complete mode list walked from `VideoModePtr`, one CSV line per mode, with a `Fields` key and a `Truncated` flag |
| `[EDID]` | DDC level, then `Block0.`/`Block1.` blobs |
| `[VGARegisters]` | MISC, feature control, DAC state, and `Seq.`/`Crtc.`/`Gdc.`/`Atc.` register banks, plus `Trust` |
| `[Tier1]` | marks the end of the always-safe capture |
| `[Tier2]` | `Requested`, `Decision` |
| `[Chipset.Identify]` | schema 2, non-PCI machines only. The accept signals and the decision |
| `[Chipset.*]` | vendor probe results, or the reason there are none |
| `[Aperture]` | schema 2. The `/aperture` result, its limits, and the memory-manager context |
| `[Result]` | `Status`, `IdentifiedBy`, `DisplayDeviceCount`, `Complete` |

### `Trust` on `[VGARegisters]`

`hardware` or `virtualized`. Under Windows the virtual display driver traps VGA
port I/O and returns per-VM values rather than what the silicon holds. The
capture is still recorded, but nothing about the chipset may be concluded from
it, and the parser warns.

### The bus

Schema 2 adds no `[Bus]` section, because the report already holds the facts:
`[PciBios]` records whether `INT 1Ah AX=B101h` answered, with
`Reason=int1a-b101-failed` when it did not, and `[PciInventory]` carries
`DisplayDeviceCount`. Drawing the conclusion is `parse-vga-survey.ps1`'s job,
which means a schema-1 report from an ISA card yields the same verdict without
having been re-collected.

### `[Platform]` and the 386 gate

The tool is 8086 code. The AC and ID bit tests that separate 386 from 486 from
CPUID need 32-bit `PUSHFD`/`POPFD`, so they sit behind a 16-bit pre-check and a
pre-386 CPU never reaches them. `Cpu386Probe` is that pre-check's answer and is
the only thing that gates the 32-bit encodings; `CpuClass` may additionally read
`386-or-later-inferred` with `Cpu386Inference=v86-host-present` when the probe
said no but a V86 host is present, because `POPF` under a memory manager is
emulated rather than executed. An inference never enables a 32-bit probe, so a
report in that state carries no CPUID detail and no `E820` map, and says so.

### Who is answering VBE

`[VBEModes] ModeListPointer` is the far pointer the controller-info block
returned, and its **segment names whose VBE this is**. The card's own BIOS
returns a pointer into its own option ROM: `C000534F` on the 486 VLB Trio64
measured 2026-08-21. With an S3VBE 3.18 TSR resident, the same machine returned
`0DC62612` - low RAM.

That matters more than it looks. Everything `[VBE]` and `[VBEModes]` describe
belongs to whoever that segment names, so a linear-framebuffer attribute from a
non-ROM provider is a promise made by software, not a property of the card. On
that machine the ROM offered VBE 1.02 with bit 7 clear on all 18 modes, and the
TSR offered VBE 2.00 with bit 7 set and a `PhysBasePtr` that disagreed with the
card's own CR59/CR5A. `parse-vga-survey.ps1` derives and prints this.

### `Int10Vector` on `[BiosData]`

Who answered every INT 10h call in the report. A segment inside `C000`-`C7FF` is
the video BIOS answering for itself; anything else is a TSR, a memory manager or
a shadow copy in between, and the VBE section then describes that thing rather
than the card. `Int42Vector` is where a hooker conventionally leaves the original
handler. Both are read from the vector table; neither is called.

### `[Aperture]`

`Status=ok` means the block move returned bytes, not that the window is live.
Four keys qualify it, and a consumer that ignores them will over-read a positive:

| Key | Why it matters |
|---|---|
| `Base` | at or below the top of installed RAM, `AH=87h` returns RAM contents; the parser reports `unreadable-by-this-method` |
| `ProtectedOrV86`, `EmsPresent`, `XmsPresent` | in V86 mode a memory manager emulated `AH=87h` instead of the BIOS — a different code path |
| `Limitation` | no mode was set, so a dead window may simply not be switched on yet |
| `Reason` | on `skipped`, why: base zero, base above the 16 MB `AH=87h` limit, or a pre-286 CPU |

`Method=int15h-ah87h-block-move` is a copy *from* the address in question *to* a
buffer in the tool's own data segment. Nothing is written.

### `Complete` on `[Result]`

`Complete=yes` is the last key the tool writes. A report without it was cut off —
by a full disk, a power cycle, or a mangled transfer — and the parser rejects it
rather than drawing conclusions from a fragment.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | report written |
| 1 | report written, but it could not be reopened for the vendor probe |
| 2 | report written, no PCI display device found (an ISA or VLB card) |
| 3 | nowhere writable; nothing was produced |

## Versioning

`SchemaVersion` is `2`. Adding a section or a key does not bump it — consumers
must ignore what they do not recognise. Removing or repurposing a key does.

Schema 2 bumped it for one repurposing: `[Result] Status` used to be `PARTIAL`
whenever no PCI display device was found, because before schema 2 that left
nothing in the report identifying the chip. A register-identified card is not a
partial result, so `Status` now follows the new `IdentifiedBy` key
(`pci`, `registers`, or `none`) rather than the bus. Everything else schema 2
added is additive, and `parse-vga-survey.ps1` reads schema 1 and schema 2 alike.

Exit code 2 still means "no PCI display device found", regardless of whether the
registers went on to name the card.
