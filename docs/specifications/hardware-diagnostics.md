# Hardware diagnostics contract

Velocity9x chipset backends publish read-only runtime facts to
`C:\V9XHW.INI`. The settings utility consumes this contract without direct
port or MMIO access, keeping the UI independent of S3, Matrox, 3dfx, or other
future hardware backends.

The version 1 section is `[Velocity9xHardware]`:

- `SchemaVersion`: decimal contract version, currently `1`.
- `Adapter`: human-readable model from the active backend.
- `VendorId` and `DeviceId`: four-digit hexadecimal PCI identifiers.
- `ClockDetector`: stable backend detector identifier.
- `ClockStatus`: `valid` or `unavailable`.
- `CoreClockKHz`: integer engine/core clock in kHz when valid.
- `MemoryClockKHz`: integer memory clock in kHz when valid.
- `CoreClockRelation`: `independent` or `shared-memory-clock`.

A backend must omit clock values and publish `unavailable` when it cannot
identify the relevant clock domain safely. The settings layer never substitutes
a pixel clock, BIOS default, marketing value, or device-table estimate.

For S3 ViRGE/DX, extended sequencer registers SR10 and SR11 describe MCLK. The
backend decodes the PLL using the 14.318 MHz reference clock and range-checks
the result. The ViRGE graphics engine shares MCLK, so the same detected value is
published for core and memory with `shared-memory-clock`; it is not represented
as an independently programmable core clock.

## Intel Gen3 read-only fingerprint

The strict `intel-gma` family additionally writes
`C:\V9XDIAG\INTELMM.TXT`, section `[IntelMmio]`. This is an experimental
hardware-evidence contract and is deliberately separate from the versioned
settings UI contract above.

- `Access`: always `read-only`.
- `BarProvenance`: always `PCI-BAR0-runtime`; BAR0 is reread for every capture.
- `Bar0`: the physical MMIO base returned with the capture.
- `RnnO`, `RnnA`, `RnnB`, `RnnD`: register offset, first read, second read and
  XOR delta for allowlist index `nn` (two hexadecimal digits). All values are
  eight-digit hexadecimal.
- `Flags`: fingerprint relationship bits from `intel_gma.h`.
- `LivePipe`: zero or one, or `0000FFFF` when there is no unique enabled pipe.
- `LivePlane`: the enabled plane whose pipe select names `LivePipe`, or
  `0000FFFF` when that is not exactly one plane. Gen3 planes are not tied
  to their same-letter pipe; plane A on pipe B is a common VBIOS layout.
- `TimingWidth`, `TimingHeight`, `TotalWidth`, `TotalHeight`, `SourceWidth`,
  `SourceHeight`, `PlaneBpp`, `PlaneStride`, `PlaneAddress`: decoded fields for
  the unique live pipe, also written as eight-digit hexadecimal values.
- `Result`: `PASS` only when all Phase 1 bits (`0000003F`) are present;
  `REVIEW` for a decoded but incomplete relationship; or `CAPTURE-FAILED`,
  `CONTRACT-FAILED`, or `DECODE-FAILED` for the named refusal.

The ring-quiescent bit (`00000040`) is collected in Phase 1 but is not part of
its `PASS` verdict. It becomes a takeover gate in Phase 3. No result in this
file authorizes an Intel MMIO write.

## Intel Gen3 firmware-ownership events

Phase 3 writes `C:\V9XDIAG\INTELEVT.TXT`. The mini-VDD keeps the source
journal in locked memory and the 16-bit driver rewrites this text projection
after every display lifecycle event. A DPMS callback records at ring 0 and is
therefore visible when the next display event drains the journal.

`[IntelEvents]` contains `Access=read-only`, `Count`, `Dropped`,
`RecordDwords`, `Coverage`, and `Result`. `READY` requires boot-enable,
mode-switch, disable, later enable, low-power DPMS and D0 DPMS coverage; every
record must be stable and no record may have been dropped. `CAPTURED` is a
valid partial matrix, while `CAPTURE-FAILED` or `STREAM-FAILED` is a refusal.

Sections `[IntelEvent00]` through `[IntelEvent1F]` contain 20 hexadecimal
dwords: `Sequence`, `Kind`, `Context`, `Flags`, `PgtblCtl`, `RingTail`,
`RingHead`, `RingStart`, `RingCtl`, `HwsPga`, `Fence0` through `Fence7`, and
`GttHashA`/`GttHashB`. `Context` is the VBE mode for display events and the
requested power state for DPMS. Required flags (`0000003F`) mean the MMIO
repeat reads and GTT hashes were stable, the ring was disabled and idle, and
PGTBL_CTL remained enabled. These are observations only; neither the capture
nor a `READY` result authorizes an Intel register or GTT write.

## Intel Gen3 read-only GTT inventory

Phase 2 writes `C:\V9XDIAG\INTELGTT.BIN` (the 65536 PTEs, 256 KiB, read
through PCI BAR3 by the mini-VDD) and `C:\V9XDIAG\INTELGTT.TXT`, section
`[IntelGtt]`. All values are eight-digit hexadecimal.

- `Access`: always `read-only`. `BarProvenance`: always `PCI-BAR3-runtime`.
- `Bar3`, `GmadrBar2`, `Bsm`, `Ggc`, `StolenBytes`, `VbeBytes`, `PgtblCtl`,
  `GttStorage`: the configuration the table was decoded against. `GttStorage`
  is the inferred top-of-stolen GTT location, `Bsm + StolenBytes - 40000`.
- `HashA`, `HashB`: FNV-1a over two complete passes in the mini-VDD.
  `HashStream`: the same hash over the dump as streamed to the display driver.
  A stable table has all three equal.
- `Present`, `Uncached`, `Local`, `Cached`, `UnknownAttrs`: PTE counts.
- `Runs`, `RunsLogged`, `RunnnnnS/N/P/D/A`: contiguous runs (start, count,
  first physical page, stride, attribute bits). At most 256 are logged.
- `BackedPrefix`: PTEs from entry 0 that map `Bsm + n * 1000` in order.
- `ReserveEntry`, `ReserveEntries`, `ReserveOffset`, `ReservePhysical`: the
  proposed 128 KiB reservation at the top of the VBE-reported framebuffer.
- `SampleCount`, `Sample0*`, `SampleReserve*`: the only GMADR reads in this
  phase, taken through the framebuffer selector at offsets 0 and
  `ReserveOffset`, each only after its PTE decoded present and BSM-linear.
  `SampleCount=0` with `Result=SAMPLE-REFUSED` means the table was captured
  but neither PTE qualified.
- `Flags`: `0001` complete, `0002` stable, `0004` nontrivial, `0008` entry 0
  maps BSM, `0010` VBE framebuffer fully backed, `0020` reservation backed,
  `0040` PGTBL_CTL selects `GttStorage`. `Result=PASS` needs all of `007F`,
  no unknown attributes, and a complete run log. Other results:
  `CONFIG-FAILED`, `CAPTURE-FAILED`, `FILE-FAILED`, `STREAM-FAILED`,
  `SAMPLE-REFUSED`, `REVIEW`.

`scripts\check-intel-gtt-capture.ps1` recomputes every count, run, hash and
sample relationship from `INTELGTT.BIN` rather than trusting the text.
