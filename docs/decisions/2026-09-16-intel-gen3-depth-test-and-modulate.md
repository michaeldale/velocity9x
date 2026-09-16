# The GMA 950 rejects a hidden surface, and modulation rounds

**Machine**: MICHAEL-NETBOOK, Intel GMA 950 on 945GSE (8086:27AE), Windows 98 SE.
**Build**: `299dde5`, armed `p6-20260916-299dde5`, Phase 6.
**Capture**: `C:\temp\intel46` — `INTEL3D0.TXT`, schema 3, `Result=PASS`.
**Date**: 2026-09-16.

Five scenes, five draws, the errata amendment's whole authorisation. All 38
probes read what the build predicted.

## 1. Depth testing works

Two scenes draw the **same three triangles at the same three depths** and
differ only in `S6_DEPTH_WRITE_ENABLE`. A is drawn first at z = 0.75, B second
and nearest at z = 0.25, C last and furthest at z = 0.90.

| Probe | Covered by | Scene 3, writes OFF | Scene 4, writes ON |
|---|---|---|---|
| `DepA` | A | `1C3E` A | `1C3E` A |
| `DepAB` | A, B | `F325` B | `F325` B |
| `DepABC` | A, B, C | `3038` **C** | `F325` **B** |
| `DepBC` | B, C | `3038` **C** | `F325` **B** |
| `DepC` | C | `3038` C | `3038` C |
| `DepOut` | none | `0842` fill | `0842` fill |

**Two probes change their answer and four do not.** With writes off nothing
records a depth, every triangle passes against a far-cleared buffer, and paint
order decides — C is last, so C wins everywhere it covers. With writes on, A
and B record their depths and C is **rejected** wherever they drew.

That rejection is the result. It is the first time this driver has had the
hardware decline to draw something.

`DepC` reading `3038` in both scenes is what makes it a rejection rather than a
disappearance: C ran, and was refused only where something nearer already was.
Without that probe, "C was correctly rejected" and "C never executed" would look
identical.

### What it does not establish

**Nothing about the depth SCALE.** The audit records that neither reference
tree states how a post-transform Z maps to a 16-bit fixed depth buffer, so the
scenes were built to depend only on the ORDER of 0.25 < 0.75 < 0.90, which
survives any monotonic mapping. This result is consistent with every such
mapping and distinguishes none of them.

**Nothing about the compare function beyond LESS**, nothing about depth ranges,
and nothing about interpolated depth — the depths are flat per triangle, on
purpose, so that interpolation is not a second unknown.

**The depth `BUF_INFO` encoding still has one source.** xf86 has no depth
buffer anywhere and cannot corroborate Mesa. What this boot adds is that the
single-sourced encoding *works*: the binding was accepted, read through, and
produced the predicted picture, with the guard page past the buffer unchanged
at `FFFFFFFF` in both scenes.

## 2. Modulation works, and the conversion rounds

Scene 2 draws scene 1's texture through a fragment program that multiplies the
texel by a half-intensity vertex colour (`0xff808080`).

| Quadrant | Texel | Read |
|---|---|---|
| 0 | `1C3E` | `0A2F` |
| 1 | `F325` | `79A3` |
| 2 | `3038` | `180C` |
| 3 | `07E0` | `0400` |

Every probe read a product, not the texel: the multiply happened. The products
were **reported, not required** — both the shader's arithmetic and the 565
conversion at these channel values were predictions — but they turn out to fit
one model exactly and to exclude the others.

Testing four candidate models over all twelve channels:

| Model | Channels matched |
|---|---|
| **bit-replicate to 8 bits, multiply, round to nearest** | **12 / 12** |
| divide by max, multiply, round to nearest | 10 / 12 |
| divide by max, multiply, truncate | 8 / 12 |
| bit-replicate to 8 bits, multiply, truncate | 7 / 12 |

**Truncation is excluded at five channels**, where the exact product is 16.62,
12.53, 2.99, 2.50 and 31.62 and the part stored 17, 13, 3, 3 and 32. No product
lands on an exact half, so no tie-breaking rule is under test.

This advances
`docs\issues\2026-09-15-intel-565-conversion-outside-measured-values.md`: the
rounding rule now has evidence at twelve channel values that were not in the
measured set. **Qualification**: this is the shader-output path, and whether it
is the same colour backend as the vertex-colour path is an assumption, not a
measurement. The issue stays open, narrower.

## 3. The read budget, and that it did not hang

Predicted before any read was spent, and published this time — the reset defect
that erased it from `intel45` is fixed:

| | Reads |
|---|---|
| Phase 4 replay | 1070 |
| Mini-VDD, staging and verify | 1032 |
| Driver, probes and guards | 58 |
| **Expected total** | **2126** |

`DriverApertureReads` read back **58**, exactly the predicted driver term. The
mini-VDD side is not independently counted, so the total is confirmed on one
side only.

**2126 reads against 1448 on the last boot that completed — and the part did
not hang.** That is the largest aperture-read count this project has attempted,
at 1.47× the previous maximum, and it is worth recording precisely because the
bulk-read hazard
(`docs\decisions\2026-09-15-bulk-aperture-reads-hang-the-945gse.md`) is what
the budget rule exists for. It remains a bounded probe set, not a bulk read:
38 probes, not 153,600.

## 4. The defect this capture exposed

**Nine probes published `unknown` as their expectation** — four modulated
quadrants and five triangle-2 probes.

The mapper from expectation to capture word lived in the display driver and
never learned the constants added beside it in the header. Its own comment
states the cost: *"a probe with no expectation and a probe whose expectation was
lost look identical in a capture, and only one of them is evidence."* Nine
looked lost.

**The result stands.** The validator compares the numeric expectation carried
in the generated table and never read that word, which is why the capture passed
and why nothing noticed. What was lost is the capture's readability to a person.

Fixed in three places, because one would have been the same defect waiting:

- the mapping moved to the scene unit, where it is pure logic over a value that
  unit owns;
- a host test names every defined expectation, requires the words to be
  distinct, and requires every probe in the table to have one;
- the capture validator now refuses `unknown` outright.
