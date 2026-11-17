# Oland: CP_RB0_RPTR reads past the write pointer, cause unknown

Date: 2026-11-17

Machine: W9QI0DAD (Oland XT `1002:6610`). Status: open. The defect it
caused is fixed by avoidance: since 1101edcb the HAL reads the ring's
read pointer from the writeback copy, not from this register
([stall issue](2026-11-17-oland-3dmark2001se-cp-stall-hang.md)).

## What was measured

In the evidence run (`../probe/oland-cp-stall-w9qi0dad-2026-11-17/3dmark2001se-9-evidence/V9XCZCP.TXT`),
taken when the HAL found the ring "full":

| Value | Reading |
|---|---|
| HAL's wptr (matches hardware CP_RB0_WPTR; no `V9XCPWPT.TXT`) | 410h |
| CP_RB0_RPTR (8700h), five reads | 480h each |
| read pointer written back to memory | 400h |
| CP_STALLED_STAT1 | 04008000h |

480h is 70h dwords past the write pointer, in a 16 KiB ring (4,096
dwords, `V9X_VI_CP_RING_BYTES`). The
writeback copy is consistent with the CP being 10h dwords behind.

## Hypotheses, none tested

- The register is read while the ME is itself waiting on a register
  read (CP_STALLED_STAT1), and the read returns a stale or prefetch
  value, not the consumed position.
- The register reports the PFP's fetch position, which may run ahead of
  the ME, not the ME's. If so, it can lead the write pointer only when
  the fetch wraps or over-fetches, and 480h against 410h would need a
  fetch past the written data.
- A read during the HAL's tight MMIO polling disturbs the register
  path. This is the reason the fix polls memory.

## How to look

Read CP_RB0_RPTR and the writeback copy side by side at a low rate
during a 3DMark run (the evidence record's fields, sampled not just at a
stall), and note whether the register ever leads the write pointer
outside a stall. Carrizo and Mullins (same code path) are worth the same
sample once their owners bring them online.
