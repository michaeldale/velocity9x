# Intel Phase 3 event-matrix implementation is ready for physical measurement

Date: 2026-09-12
Status: implemented and host-verified; physical Phase 3 matrix pending.

The Intel package now records firmware ownership at the events named by Phase
3 of `docs/plans/hardware-d3d-on-intel-gma950.md`. Mini-VDD API v5 retains up
to 32 chronological records. Each contains two reads of PGTBL_CTL, ring state,
HWS_PGA and all eight Gen3 fence registers, plus two complete GTT hashes. The
record also proves the render ring is disabled and head equals tail.

The display driver records cold-boot enable, later enable, disable, live mode
switch and same-mode restore. DPMS cannot safely write a file from its VxD
callback, so that callback records into locked mini-VDD storage; the next
display lifecycle event drains the complete journal to
`C:\V9XDIAG\INTELEVT.TXT`. The journal stops at its bound instead of
overwriting the boot baseline and reports every dropped attempt.

The implementation performs no Intel write. Tree checks hold the positive
Intel-family guard and require exactly two MMIO read sites and two GTT read
sites in the event capture, with no store through either mapped pointer.
`scripts/check-intel-event-capture.ps1` independently checks record layout,
sequence, event coverage, stable reads, ring/PGTBL relationships and the Phase
2 initial GTT hash, then reports adjacent field-level changes. Its clean
fixture and three corruptions run in `run-checks.ps1`.

Phase 3 is not complete until the physical netbook produces a `READY` matrix
covering boot, mode switch, disable/enable, low-power DPMS and D0 DPMS, and all
reported ownership changes have been explained. Phase 4 remains blocked.

## Verification

- `scripts/run-checks.ps1 -BuildId intel-phase3-event`: green, including both
  event-validator fixtures, host tests, binary audits and all five family
  packages.
- The event validator self-test also passes under Windows PowerShell 5.1, the
  shell available on the evidence host.
- Shared Win16 enable gate: ATI Mach64 VT2 on port 9873 and S3 ViRGE/DX on port
  9869 both reached `Stage=enable-ok`. The additional emulated Trio64 target
  on port 9871 was not running; the gate reported that target explicitly
  rather than treating it as covered.
