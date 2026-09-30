# Final Reality 1.01, full default run, on the netbook and the Gateway: both complete, no engine faults

Date: 2026-10-01
HAL: `d78d4f4` (stack buffers off the stack, frame gate), hash-verified on
both machines after install.
Evidence: [`../probe/final-reality-full-2026-10-01/`](../probe/final-reality-full-2026-10-01/)

The first full Final Reality runs on either machine since the DrawPrimitives
stack fault
([issue](../issues/2026-09-30-final-reality-robots-faults-inside-drawprimitives-on-the-netbook.md)).
Scores are recorded, not compared.

## Procedure

[`final-reality-101-runbook.md`](../specifications/final-reality-101-runbook.md),
driven through the agent's `input` verb. `Reset all options to default`,
which checks all eight tests and `Run all tests 5 times`
(`netbook-options.png`, `gateway-options.png`, taken before starting), then
`Run advanced benchmark`.
Both runs started at 09:06:21 host time. Results were read from the
`Benchmark results` tabs, one screenshot per tab; the screenshots are the
evidence. `Compare to` is `<none>` in every one, so the second row of each
bar is FR's `n/a` placeholder, not a reference.

The Gateway was not contacted for the first 20 minutes of its run, because
boot 78 locked with its agent log ending at a mid-run screenshot. Its first
check, at 09:26:53, found FR already back at Advanced Options. The netbook
was polled by screenshot every two minutes and was back by 09:25:07. So the
netbook's run took under 19 minutes and the Gateway's under 20.5; neither
end time is known more closely than that.

| | Netbook | Gateway |
|---|---|---|
| Machine | MICHAEL-NETBOOK, 10.0.1.254, boot 78 | SOLO2150, 10.0.1.22, boot 80 |
| Graphics | 945GSE / GMA 950 `8086:27AE`, Gen3 engine | ATI Rage Mobility-M `1002:4C4D`, Mach64 engine |
| CPU as FR names it | "1667 MHz Intel Pentium II" (an Atom N280) | "450 MHz Intel Pentium II" |
| Desktop | 1024x576x16 | 1024x768x16 |

## Scores

| Test | Netbook raw | Netbook R marks | Gateway raw | Gateway R marks |
|---|---|---|---|---|
| Radial blur | 141.89 images/s | 19.62 | 43.91 images/s | 6.07 |
| Chaos zoomer | 396.37 images/s | 19.26 | 69.96 images/s | 3.40 |
| **2D image processing** | | **19.44** | | **4.74** |
| 25 pixel | 158.34 Kpolys/s | 5.06 | 114.66 Kpolys/s | 3.66 |
| Robots | 18.63 images/s | 4.83 | 18.22 images/s | 4.72 |
| Fill rate | 8.18 Mpixels/s | 1.77 | 38.26 Mpixels/s | 8.28 |
| City scene | 17.85 images/s | 4.43 | 18.94 images/s | 4.70 |
| Visual appearance | 85.19 % | - | 92.59 % | - |
| **3D performance** | | **2.19** | | **2.49** |
| 2D transfer rate | 159.03 MB/s | 5.06 | 78.89 MB/s | 2.51 |
| 3D transfer rate | 155.43 MB/s | 13.28 | 73.84 MB/s | 6.31 |
| **Bus transfer rate** | | **7.53** | | **3.65** |
| **Overall** | | **8.17** | | **3.34** |

`Visual appearance` is not an image-quality measurement (runbook). The two
machines advertise different capability lists (the netbook greys out
`Depth fog` and `Specular gouraud`, the Gateway greys out
`Subpixel accuracy`), which is consistent with the runbook's reading that it
scores the advertised set. Not proven here either.

## Driver counters over the full run

Both boots had already run Robots once on this HAL, so these are the
post-run snapshot minus the pre-run one (`*-before-full-V9XSNA7.INI`,
`*-post-V9XSNA7.INI`). DWORD diagnostics only; the WORD `Count*` ring
counters wrap and are not used.

| Counter | Netbook | Gateway |
|---|---|---|
| D3dContextCreates / Destroys | 5 / 5 | 5 / 5 |
| D3dRenderPrimitiveCalls | 1,190,429 | 1,072,324 |
| D3dTextureCreates / Destroys | 3,360 / 3,360 | 3,360 / 3,360 |
| D3dDepthOffered / Accepted | 4 / 4 | 4 / 4 |
| DpRecords | 9,622,036 | 8,669,086 |
| DpRecordTriangles | 10,404,722 | 9,329,733 |
| DpRecordRuns | 5,373,853 | 4,783,204 |
| Engine draws submitted | 5,501,341 (`I9xxDrawsSubmitted`) | 4,752,788 (`M64Draws`), 14,596,902 triangles |
| Engine refusals | 0 (`I9xxDrawsRefused`) | **124,496** (`M64Refused`, = `BatchesEngineRefused`) |
| EngineFifoTimeouts / IdleTimeouts / Resets | 0 / 0 / 0 | 0 / 0 / 0 |

Both whole-boot snapshots also read zero flip-wait, breadcrumb and
ring-space timeouts, `D3dContextRejects=0`, and no `V9XTRACE.INI` fault
dump exists on either machine.

## The Gateway's 124,496 refusals

About 2.5% of the Mach64's batches (124,496 of 4,877,284 attempted) were
refused by its policy check and not drawn. `M64RefuseLast=3`
(`V9X_D3D_MACH64_REFUSE_POLICY`) and `M64PolicyLast=10`
(`V9X_M64_REFUSE_TEXTURE_FILTER`). Only the last reason is kept, so this
says the final refusal was a texture-filter combination the policy does
not map, not that all of them were. The Robots-only run on the same boot
refused none, so they come from the other tests; FR's texture tests
include tri-linear mip-mapping, which is the likely candidate, and nothing
here measured which test they came from. A refused batch is counted and
skipped, and the run continued, so the 3D scores above include frames with
that geometry missing.

## What this does not establish

- That any image was right. Nobody watched either run; the screenshots are
  of the results dialogs only.
- Anything from comparing these scores with each other or with earlier
  runs. The machines differ in CPU, bus and engine, and the earlier records
  used other builds.
