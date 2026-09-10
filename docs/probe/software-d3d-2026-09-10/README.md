# Scalar fixes benchmark evidence

Retrieved guest reports and CSV summaries for the
[2026-09-10 decision record](../../decisions/2026-09-10-rasterizer-scalar-fixes.md),
which holds the machine configurations, methodology, results and limits. CSVs
carry median milliseconds per scene or memory sweep with the speedup. These
are synthetic timings, not game FPS.

| File | What it is |
|---|---|
| `trio64-baseline-boot335.ini` | `c4988fe` rasterizer, Trio64 guest, boot 335 |
| `trio64-candidate-boot335.ini` | the committed source, same boot - **the headline A/B** |
| `trio64-comparison.csv` | those two |
| `trio64-baseline-boot334-1.ini`, `trio64-baseline-boot334-2.ini` | the same baseline executable twice on the previous boot |
| `trio64-drift.csv` | those two, which is the run-to-run drift figure |
| `trio64-candidate-calls-boot334.ini` | the **rejected** first candidate, whose clamp and divide were `static` functions |
| `trio64-comparison-calls.csv` | boot-334 baseline against that rejected candidate |
| `trio64-candidate-macros-boot334.ini` | the candidate before three compile-time asserts were added; it reproduces the boot-335 figures |
| `virge-baseline-1.ini`, `virge-baseline-2.ini`, `virge-candidate-1.ini` | the same A/B and drift check on the ViRGE guest, boot 573, RAM only |
| `virge-comparison.csv` | ViRGE baseline-1 against candidate-1 |

`trio64-candidate-calls-boot334.ini` is kept deliberately: it is what a
per-pixel function call costs in a build that requests no optimization, and it
is the reason the shipped form uses macros. See the record's own section on it.

## Reproduce this A/B

The [2026-09-07 probe README](../software-d3d-2026-09-07/README.md) documents
the instrument, its guest-side files and the comparator's rejection rules. The
only differences here are the baseline commit and the guest boots:

```powershell
New-Item -ItemType Directory -Force build/software-d3d | Out-Null
git show c4988fe:src/display32/d3d/d3d_raster.c |
    Set-Content -Encoding Ascii build/software-d3d/baseline-raster.c
./scripts/build-software-bench.ps1 -BuildId soft-base-2 `
    -RasterSource build/software-d3d/baseline-raster.c `
    -OutputDirectory build/software-d3d/baseline2
./scripts/build-software-bench.ps1 -BuildId soft-scalar2 `
    -OutputDirectory build/software-d3d/candidate4
```

Then `put`, `exec` and `get` each executable in one guest at a time - port
9871 for the Trio64 guest, 9869 for the ViRGE guest - and run
`compare-software-bench.ps1`. Run the baseline again on the same boot before
believing a difference smaller than a per cent.

## Artifact identities (SHA-256)

| Artifact | SHA-256 |
|---|---|
| Baseline `V9XSOFT.EXE`, 9,216 bytes, `soft-base-2` | `85B42F7ED9E688640108F106546395FFB1AB48004CCA7A40569C59AD3E22CB3F` |
| Rejected first candidate, 9,216 bytes, `soft-scalar` | `23AC5FD28D1D3A9CFB603EDB960B9918158954D2EC0FFF345BBB1E45BE51852F` |
| Pre-assert macro candidate, 9,216 bytes, `soft-scalar2` | `34855C204894FB0BA45A16525E63D83C83E57BADE87DDA953C0084E34929E5A8` |
| Committed-source candidate, 9,216 bytes, `soft-scalar2` | `56D53AC8373289E82EB820A199AE881024C1E76B8E3EDFB8741DF97975F35CF4` |

Executable snapshots stay under `build/software-d3d/` and are not tracked.
Rebuilt PE timestamps can change these hashes from identical source, so the
guest-side CRC32 in each `put` result is the closer identity: `F9352758` for
the baseline, `6BA32996` for the rejected candidate, `AA5C1D54` for the
pre-assert macro build and `23A92742` for the committed source.
