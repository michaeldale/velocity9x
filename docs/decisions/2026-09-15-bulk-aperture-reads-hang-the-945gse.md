# Bulk aperture reads hang the 945GSE; single reads do not

Measured on MICHAEL-NETBOOK (945GSE A3), 2026-09-15, build `9e38150`.
Unarmed boot, no hardware writes anywhere in the run.

## The measurement

`INTEL3D0.TXT` ends at:

```
B1Step=unarmed-hash
```

with `IntentStep=00000014`, `Precondition=00000001` (NOT_ARMED), and every key
that the sequencer writes *after* the hash absent - `UnarmedHashA`,
`UnarmedHashB`, `UnarmedSample0`, `UnarmedSample1`, `Result`. The machine hard
locked there and had to be recovered from DOS.

`v9x_p5_hash_target` asks the mini-VDD to hash the render target:
**153,600 dwords, 614,400 bytes**, through `V9xMini_I9xx_Hash_Range`, and it
does two passes. The mapping is `_MapPhysToLinear` over
`V9X_I9XX_RESERVE_PHYS` = `0xD0000000 + 0x6B0000` - the **GMADR aperture**, not
the stolen-memory physical address.

## What the same capture rules out

Everything written before the hash survived, and it clears the hypotheses this
cost several boots to reach:

| Key | Value | What it settles |
|---|---|---|
| `P5RefReserveFirst` | `000006B0` | |
| `P5RefReserveCount` | `00000100` | |
| `P5RefBackedPrefix` | `000007BF` | `0x6B0 + 0x100 = 0x7B0 <= 0x7BF`: the reserve **is** backed. Not a GTT fault. |
| `HeapProbeBefore` | `FFFFFFFF` | A single aperture read below the reserve completed. |
| `GLow0`, `GUpp0` | `00000000` | Two more single aperture reads completed. |
| `StreamCrc` | `0ED8C9A3` | Equals `GeneratedCrc`: the 66-dword stream built correctly on hardware. |

So the address path works, the layout is right, the backing is right, and the
stream is right. Three individual aperture reads succeed and 153,600 of them
hang.

## What this is

Consistent with erratum 12 of Intel document 309220-0132 - a CPU/GPU
memory-access sequence that hangs the part, 945GSE A3, no silicon fix. The GPU
is scanning out the display throughout, so a sustained CPU read stream across
the aperture is exactly the contended sequence the erratum describes. This
records a correlation with a documented erratum, not a proof of mechanism;
nothing here identifies which access pair actually wedges.

It is **not** the theory this replaced. The reserve is backed, so it is not an
unmapped-page fault, and the mapping is the aperture, so it is not the
CPU-cannot-reach-stolen-memory-directly rule recorded on 2026-09-14.

## Hypotheses this killed

Each of these was stated with more confidence than it deserved and then
disproved by a later capture:

1. **The `I9XXCODE` far call.** Dead: `Stage=arm-post` on the netbook.
2. **`arm_prepare` never reaching its first write.** Dead, and the reasoning
   was invalid - `WritePrivateProfileString` does not touch a file whose value
   is unchanged, so an unmoved timestamp proved nothing.
3. **The mini-VDD being the bug.** Too strong. Its presence is *necessary* for
   the hang only because removing it makes the hash call fail harmlessly.
   `MiniVDD_Dynamic_Init` is unchanged from the last good build, and variant B
   - smaller than `1f386d0` - also hung.
4. **The Phase 4 or Phase 5 compile guards.** Dead: both variants hung.

## The instrumentation defect that hid it

`intel_3d16.c` had no `WritePrivateProfileString(0,0,0,...)` anywhere, so every
Phase 5 key sat in the profile cache and `IntentStep` - the whole mechanism for
locating a hang on a machine with no serial port - never once reached the disk.
Six boots were spent reasoning about a file that could not have answered.
`v9x_boot_trace` in `ddi.c` carries a comment about precisely this failure
mode. Fixed in `efb37b6`; the marker granularity that named the hash came in
`9e38150`.

Separately, Windows re-latches `*DisplayFallback=1` after every locked boot, so
the following boot skips `Enable` entirely and produces a capture that looks
like a clean run and means nothing. Two boots were lost to that before it was
understood.

## Consequences

**The no-write path is not the no-risk path.** B1 exists to catch faults at the
cost of no armed boot; this fault *is* in B1, and it reads 614 KiB plus three
aperture locations before the preflight that would validate any of it, because
that preflight returns `NOT_ARMED` on its first line.

**The armed path carries the same defect, and worse.** After the draw it runs
480 row CRCs of 320 reads each - 153,600 aperture reads, the same figure - plus
another full-target hash. An armed boot would have reached the draw and then
hung while reading the result back, losing the evidence it was spent on. That
is the outcome B1 was built to prevent, and it did prevent it.

Any read-back of the render target has to be bounded from here on, and how much
evidence a bounded read-back can still carry is a design question, not a
bug fix.
