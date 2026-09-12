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
