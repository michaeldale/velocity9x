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
disable and mode-switch coverage (`Coverage` mask `0D`). Mode-restore (`10`)
is reported but not required: it follows only a full-screen DOS box, whose
return hard-locks the netbook and is excluded as a tier-0 display defect. The
DPMS bits (`20` D0, `40` low power) are reported but not required: measured
2026-09-12, `V9XPWR` produced no DPMS record because the mini-VDD's
`GetMonitorPowerStateCaps` advertises D0 only, so Windows never asks for a
low-power state and `V9XPWR`'s `PASS` means only that its broadcast
returned. Every
record must be stable and no record may have been dropped. `CAPTURED` is a
valid partial matrix, while `CAPTURE-FAILED` or `STREAM-FAILED` is a refusal.

Sections `[IntelEvent00]` through `[IntelEvent1F]` contain 20 hexadecimal
dwords: `Sequence`, `Kind`, `Context`, `Flags`, `PgtblCtl`, `RingTail`,
`RingHead`, `RingStart`, `RingCtl`, `HwsPga`, `Fence0` through `Fence7` (the
first eight fence registers at `2000`-`201C`; the 945's second bank of eight at
`3000`-`301C` is not captured), and
`GttHashA`/`GttHashB`. `Context` is the VBE mode for display events and the
requested power state for DPMS. Kinds: 1 boot enable, 2 a later plain
Enable, 3 Disable (captured before the teardown), 4 ReEnable with a mode
change (a Display Properties resolution change; measured 2026-09-12 to
produce kind 4 alone, with no Disable), 5 ReEnable in the same mode (return
from a full-screen DOS box, preceded by a kind 3 Disable), 6 DPMS. Nothing in
a normal session produces kind 2. Required flags (`0000003F`) mean the MMIO
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

## Intel Gen3 Phase 4 ring plan and first write

Phase 4 writes `C:\V9XDIAG\INTELRNG.TXT`, section `[IntelRing]`, and keeps its
one-shot arm state in `C:\V9XDIAG\INTELARM.TXT`, section `[Velocity9x]`. The
arm file is written by `scripts\arm-intel-phase4.ps1` on the host and by the
driver at load and at refusal; it is deliberately not `SYSTEM.INI`.

`Access` is `no-hardware-writes` on an unarmed boot and `armed-hardware-write`
once the executor has committed. `TokenMover` reports the load-time
transaction: `READY` (nothing armed, the normal unarmed boot), `ARMED`,
`INCOMPLETE` (a previous attempt left `IntelInFlight` set), `BAD-TOKEN`,
`BAD-CRC`, `BAD-BUILD`, `EXECUTOR-ABSENT` or `IO-FAILED`.

`Result=ERRATA-GATED` is the complete, healthy unarmed capture. An armed boot
ends at `PASS`, or at one of the named refusals in the design note.

### PreconditionCode

A refusal before any hardware write publishes `PreconditionCode`, and beside
it `RefErr0` through `RefErr6`, the read-only registers IPEIR `2088`, IPEHR
`208C`, INSTDONE `2090`, ACTHD `20C8`, EIR `20B0`, **EMR** `20B4` and **ESR**
`20B8` in that order, plus `RefReserveOffset`, `RefReservePhysical`,
`RefScratchOffset`, `RefBsm`, `RefPgtbl`, `RefGttHashA` and `RefEventCount`,
which are the operands the checks read rather than the values published
earlier in the same file.

| Code | Meaning |
|---|---|
| `01` | An arm key could not be read from the arm file |
| `02` | `IntelArmBuildId` is not this driver's build |
| `03` | `IntelAccelDefault` is not `0` |
| `04` | `IntelArmCrc` is not eight hexadecimal digits |
| `05` | The arm contract rejected; `PreconditionArmReject` gives its reason |
| `06` | The armed CRC does not match the one latched at load |
| `07` | The two flush-page config reads disagreed |
| `08` | The sandbox layout is not the measured one |
| `09` | BSM is not `7F800000` |
| `0A` | PGTBL_CTL is not `7FFC0001` |
| `0B` | A ring register was not zero at entry |
| `0C` | The GTT hash is not the Phase 2 baseline |
| `0D` | The event journal is full or dropped a record |
| `0E` | `INTELMM.TXT` does not say `PASS` |
| `0F` | `INTELGTT.TXT` does not say `PASS` |
| `10` | A diagnostic register read through the mini-VDD failed |
| `11` | EIR is not clear |
| `12` | ESR is not clear |

### StageFail

`Result=GTT-MIRROR-FAILED` means the ten dwords and the guard pattern were
staged into the reserve but did not read back. It publishes `StageFail`,
`StageIndex`, `StageExpected`, `StageMemory` and `StageGmadr`.

| `StageFail` | Meaning |
|---|---|
| `01` | The mini-VDD refused to stage a dword; `StageIndex` is which |
| `02` | The mini-VDD refused a read-back |
| `03` | The mini-VDD read back its own write and it differed |
| `04` | The mini-VDD read back correctly but GMADR showed something else |
| `05` | The scratch guard pattern failed the mini-VDD read-back |
| `06` | The scratch guard pattern failed the GMADR read |

`01` publishes `StageVxdFail`, the mini-VDD's own reason: `01` physical base,
`02` MMIO base, `03` no Phase 1 capture, `04` out-of-order index, `05` index
bound, `06` value outside the reviewed stream, `07` `_MapPhysToLinear` refused
the reserve, `08` no mapping at write time, `09` the store did not read back.

`SnnFailure` is the mini-VDD's reason for a step: `00` success, `01` the ring
registers were not zero at entry, `02` the poll timed out, `03` refused before
running, `04` poison latched, `05` fewer than ten dwords declared, `06`
execution CRC mismatch, `07` MMIO base, `08` MMIO not mapped, `09` ring window
not mapped, `10` unknown step, `11` out-of-order step, `12` the staged stream
did not verify, `13` the scratch guard did not verify, `14` reached the
register writes.

`03` and `05` are mapping faults. **`04` and `06` are the interesting pair:**
they would mean a CPU write to stolen memory through the mini-VDD's physical
mapping is not visible through the graphics aperture, which is what the Intel
Flush Page at `FED13000` exists to resolve. That would be a finding about this
chipset, not a driver defect, and it belongs in a decision record.

`PreconditionArmReject` is meaningful only for code `05`: 1 not enabled this
boot, 2 Safe Mode, 3 errata gate closed, 4 PCI identity, 5 phase, 6 token,
7 command CRC.

EIR and ESR are latched error state and must be clear. EMR is the error
*mask*: measured `FFFFFFFF` on the netbook 2026-09-14, reported and never
required to be clear. Note the register order, which is EIR, EMR, ESR rather
than the alphabetical one; assuming otherwise briefly moved the second check
onto the mask.
