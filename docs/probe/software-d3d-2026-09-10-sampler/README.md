# Sampler fixes benchmark evidence

Retrieved guest reports and CSV summaries for the
[texel-units and bilinear record](../../decisions/2026-09-10-rasterizer-texel-units-and-bilinear.md),
which holds the machine configurations, methodology, results and limits. CSVs
carry median milliseconds per scene or memory sweep with the speedup. These
are synthetic timings, not game FPS.

| File | What it is |
|---|---|
| `trio64-baseline-1.ini` | `36f08d4` rasterizer - fixes 2 to 4 - Trio64 guest, boot 336 |
| `trio64-baseline-2.ini` | the same executable again on the same boot, for drift |
| `trio64-candidate-1.ini` | fixes 5 and 6 on top of it |
| `trio64-preceding-baseline.ini` | `c4988fe`, the pre-scalar-fixes rasterizer, run on this same boot |
| `trio64-comparison.csv` | baseline-1 against candidate-1: fixes 5 and 6 alone |
| `trio64-cumulative.csv` | preceding-baseline against candidate-1: fixes 2 to 6 together, measured rather than multiplied |
| `trio64-drift.csv` | the two baseline runs |
| `virge-baseline-1.ini`, `virge-baseline-2.ini`, `virge-candidate-1.ini` | the same A/B and drift check on the ViRGE guest, boot 574, RAM only |
| `virge-comparison.csv`, `virge-drift.csv` | those |
| `trio64-probe-before.ini`, `trio64-probe-after.ini` | the DirectDraw probe through the *installed* HAL on the Trio64 guest, `Direct3D=2`, boots 337 and 338 - the run the benchmark cannot make, because it links the rasterizer directly and never loads the driver |
| `trio64-hw-after.ini` | that guest's `V9XHW.INI`, which says `Direct3DMode=software` and `ColourLayout=565-auto` |

## The installed-HAL run

`compare-probe.ps1` over the pair reports **0 unexpected, 0 expected, 0
only-left, 0 only-right** across 1117 keys, with `Result=COMPLETE` on both.
The HAL changed from 43,520 bytes - the `soft-edge-01` build from
[2026-09-07](../software-d3d-2026-09-07/README.md) - to 44,032, which carries
both of this day's rasterizer commits. Nothing else on the guest was touched:
the 16-bit driver and the mini-VDD are as they were, so the diff is the HAL's
arithmetic alone.

`RampOk`, `SpriteOk`, `MipLadderOk` and `AlphaCurveOk` read 0 in both runs and
in both of the 2026-09-07 runs. That is the documented software capability
set, not a regression: the rasterizer implements no texture alpha and selects
no mip level, and those four rungs are texel-alpha and mip rungs.
`VtxAlphaCurveOk` - vertex alpha, which it does implement - reads 1, as does
`ZDepthFillOk`.

```powershell
./scripts/compare-probe.ps1 `
    -Left docs/probe/software-d3d-2026-09-10-sampler/trio64-probe-before.ini `
    -Right docs/probe/software-d3d-2026-09-10-sampler/trio64-probe-after.ini
```

## Reproduce this A/B

The [2026-09-07 probe README](../software-d3d-2026-09-07/README.md) documents
the instrument, its guest-side files and the comparator's rejection rules.

```powershell
New-Item -ItemType Directory -Force build/software-d3d | Out-Null
git show 36f08d4:src/display32/d3d/d3d_raster.c |
    Set-Content -Encoding Ascii build/software-d3d/baseline-raster-5-6.c
./scripts/build-software-bench.ps1 -BuildId soft-scalar234 `
    -RasterSource build/software-d3d/baseline-raster-5-6.c `
    -OutputDirectory build/software-d3d/base56
./scripts/build-software-bench.ps1 -BuildId soft-sampler `
    -OutputDirectory build/software-d3d/cand56
```

Then `put`, `exec` and `get` each executable in one guest at a time - port
9871 for the Trio64 guest, 9869 for the ViRGE guest - and run
`compare-software-bench.ps1`. Run the baseline again on the same boot before
believing a difference smaller than a per cent.

## Artifact identities (SHA-256)

| Artifact | SHA-256 |
|---|---|
| Baseline `V9XSOFT.EXE`, 9,216 bytes, `soft-scalar234` | `3664B1B215E1EABA62A5478F21554BA345BA86D5965EF4D30171B5DA3C0931D0` |
| Candidate `V9XSOFT.EXE`, 9,728 bytes, `soft-sampler` | `A72CC10798CEB1FE714BEC833468D8DE386EA8C1F5E12BF64753D9A55BCF872A` |

Executable snapshots stay under `build/software-d3d/` and are not tracked.
Rebuilt PE timestamps can change these hashes from identical source, so the
guest-side CRC32 from each `put` is the closer identity: `C8F0D872` for the
baseline and `638941AF` for the candidate. The preceding baseline is the
`soft-base-2` executable from the
[previous probe](../software-d3d-2026-09-10/README.md), CRC32 `F9352758`,
already on both guests as `C:\V9XDIAG\SOFTBASE.EXE`.
