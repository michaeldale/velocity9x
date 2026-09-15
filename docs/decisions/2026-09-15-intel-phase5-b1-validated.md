# B1 validated on hardware: the bounded read-back works

Measured on MICHAEL-NETBOOK (945GSE A3), 2026-09-15, build `318584c`.
Unarmed, no hardware writes. Capture preserved at
`probe/intel-phase5/INTEL3D0-b1-318584c.txt`.

This is the first Phase 5 capture that has ever satisfied
`check-intel-3d-capture.ps1` against real hardware.

## What the boot established

| Fact | Evidence |
|---|---|
| The driver enables and the machine is usable | `Stage=enable-ok`; normal desktop, no recovery needed |
| The bounded read-back is survivable | `SampleReads=00000010` - 16 aperture reads where 307,200 hard locked the same machine |
| The sampled addresses are stable | `SampleStable=1`, eight `SA`/`SB` pairs equal |
| Mapping, backing and bounds hold before any aperture access | `MappingCheck=00000000` |
| The stream built on hardware is the reviewed one | `StreamCrc=0ED8C9A3` = `GeneratedCrc` |
| The packet offsets locate the packets | `LengthFill=7`, `OffsetState=7`, `OffsetVertices=32` |
| The geometry is transported correctly | `VD0000/1` = A0/78, `VD0008/9` = 1E0/78, `VD0010/11` = 140/190, W=1 on all three |

Those decoded vertices are 160,120 / 480,120 / 320,400 - the triangle as
specified, recovered from IEEE-754 bits built without an FPU and read back off
the machine.

## What it did NOT establish

- **Nothing about the target's contents.** Eight points agreeing with
  themselves is sample stability at eight addresses. The samples read a mix of
  `00000000` and `FFFFFFFF`, which is uninitialised stolen memory and means
  nothing.
- **Nothing about drawing.** No write has been made. The GPU has not executed a
  Phase 5 packet.
- **No safe read bound.** 16 reads completed; 307,200 did not. The interval
  between remains unmeasured and confounded - see
  `plans/intel-phase5-bounded-readback.md`.
- **Nothing about repeatability under load.** One boot, idle machine.

## The defect this boot caught, at the cost of no armed boot

The previous B1 (`b46cb1a`) passed on the machine and was then **refused by the
capture checker** on vertex decoding. The checker was right: `intel_3d16.c`
derived its packet offsets from the builders' extents and had not been updated
when the fill moved to the GPU, so it published stream dwords 44-47 - fragment
program and probe words - as vertex bits. The geometry was correct at dword 51
throughout.

The CRC gate could not have caught it. `StreamCrc` covers the stream; the
offsets point *into* the stream and were never checked against what they point
at. The comment above them claimed they could not disagree with the stream,
which is an assertion, not a mechanism.

Fixed in `318584c`, with a host test that asserts content at the computed
offsets rather than the arithmetic, verified by restoring the old derivation and
watching the suite fail.

This is what B1 is for, and it is the second real defect it has found - the
first being the bulk aperture read itself. Both would have surfaced on an armed
boot instead, and both would have cost the token.

## Standing preconditions for the first armed draw

From `2026-09-15-intel-phase5-errata-gate.md`, with current status:

- B1 unarmed validated - **done, this record**.
- The Part 1 regression boot - **done**: `Stage=enable-ok` on `318584c` is that
  evidence; the segment split and the 1 MiB layout move have now booted.
- The stick re-armed to the combined CRC `D0478966`, against build id
  `318584c` - **outstanding**.
- AC power - **outstanding**, operator's.

The read-back the armed boot will perform is 14 probe reads, each committed
either side, with the full-target hash and the 480 row CRCs removed. A wrong
picture will therefore be reported but not localised; that trade is recorded in
`plans/intel-phase5-bounded-readback.md`.
