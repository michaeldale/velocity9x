# Five scenes in one boot: green rounds, and the shared edge is owned once

**Machine:** MICHAEL-NETBOOK, 945GSE A3, `8086:27AE` rev 03, AC power.
**Build:** `4510fc4`. **Capture:** `C:\temp\intel44\INTEL3D0.TXT`.
**Result:** `PASS`, five scenes completed, token retired
(`pass:phase6:p6-20260916-4510fc4`).

First multi-draw boot on this part. Every scene staged in full, every scene's
CRC matched the generated one, every in-reserve guard read `A5A5A5A5` and
`00000000` after every scene, and no error register moved.

## 1. The regression scene reproduces exactly

Scene 0 is the Phase 5 triangle. Seven interior probes `1C3E`, seven exterior
`0842` — **byte-identical to `C:\temp\intel42`**.

That validates two changes at once, which is what it was placed first to do:

- The **depth `BUF_INFO` removal**. The binding at graphics address zero is
  gone and no pixel changed, which is the result
  `docs/issues/2026-09-15-intel-depth-buf-info-at-address-zero.md` was owed.
- The **qword padding**. The stream grew an `MI_NOOP` before the primitive and
  the picture is unchanged.

## 2. Green rounds. The hybrid backend is dead

Scene 1 drew `0xFF2E03C8` and stored **`0x3038`**.

| ch | byte | `trunc` | `round` | stored |
|---|---|---|---|---|
| R | 46 | 5 | 6 | **6** |
| G | **3** | **0** | **1** | **1** |
| B | 200 | 25 | 24 | **24** |

Green 3 was chosen precisely because it separates the two rules, and it rounds.

`docs/issues/2026-09-15-intel-565-conversion-outside-measured-values.md`
recorded that green had agreed with truncation at both previously tested
values, so a backend rounding red and blue while truncating green fitted every
observation. **It no longer does.** The conversion is uniform rounding across
all three channels, now measured at nine channel values across three colours.

Still not established: the other 247 byte values per channel. The rule is
measured at nine points, not proved.

## 3. The shared edge is owned exactly once

The three edge probes sample pixels whose centres lie **exactly on** the
diagonal — the property the geometry was rebuilt for, after the first version
used a slope that no sample centre touched.

| Probe | Upper alone | Lower alone | Both |
|---|---|---|---|
| EdgeA (250,200) | `1C3E` | `0842` | `1C3E` |
| EdgeB (320,270) | `1C3E` | `0842` | `1C3E` |
| EdgeC (390,340) | `1C3E` | `0842` | `1C3E` |

Covered in the upper scene, fill in the lower: **exactly one triangle claims
each shared-edge pixel.** No double coverage, no gap.

The diagonal is a left edge for the upper triangle and a right edge for the
lower, and the upper is the one that claims it — which is the **top-left fill
rule**. Two opaque triangles in one scene could not have shown this; the
second would simply have overwritten the first, which is why the experiment
needed three scenes.

Flanks and bodies agree in all three scenes, and the combined scene shows the
upper triangle's colour on the edge, consistent with the lower not covering it.

**Scope:** one diagonal, slope 1, sampled at three points. It does not
establish behaviour for other slopes, for horizontal or vertical shared edges,
or for degenerate triangles.

## 4. The read budget held

`ExpectedApertureReads=0x71A` (1818) published before the replay read anything.
`DriverApertureReads=0x42` (66) — exactly the 52 probes plus 14 guard and heap
reads the arithmetic predicted. No hang at 1.5x the reads of the last boot that
completed.

## What this boot also exposed

**A capture was not a record of one boot.** `WritePrivateProfileString` updates
keys in a file that survives a reboot, so every key a run does not write is
inherited. This capture reports `Result=PASS` with five scenes completed **and**
carries `SceneFailed=0` and `S0Scene=EXECUTE-REFUSED` from `intel43`, whose
`S0ExecFailStep` value it reproduces byte for byte.

The validator read the stale `SceneFailed`, checked **one scene of five**, and
accepted. Four scenes of evidence went unexamined and the verdict said nothing
about it.

Both halves are fixed: the driver deletes the section before writing the first
key of a run, and the validator refuses a capture carrying both
`ScenesCompleted` and `SceneFailed` rather than silently preferring one. The
results above were confirmed by re-validating with the stale keys removed — 5
of 5 scenes, 36 established probes matched, 9 edge measurements, 7 predicted
reported.

Older Intel captures under `docs/probe` may carry the same contamination. None
has been re-examined, and any conclusion drawn from a key that a run might not
have written is worth rechecking against this.
