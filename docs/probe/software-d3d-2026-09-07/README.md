# Software rasterizer benchmark evidence

See the [decision record](../../decisions/2026-09-07-software-rasterizer-edge-stepping.md)
for the machine configurations, methodology, results and limits. INIs here are
the retrieved guest reports; CSVs contain median milliseconds per scene or
memory sweep and baseline/candidate speedup. They are synthetic timings,
not game FPS. `trio64-probe-*.ini` instead exercise the installed HAL through
DirectDraw/Direct3D; unchanged known unsupported cases are expected.

## Reproduce an A/B run

Build both executables with the same benchmark source. A different scene tool
is not a valid baseline. From the repository root, capture the old production
source without switching the working tree:

```powershell
New-Item -ItemType Directory -Force build/software-d3d | Out-Null
git show f21d703:src/display32/d3d/d3d_raster.c |
    Set-Content -Encoding Ascii build/software-d3d/baseline-raster.c
./scripts/build-software-bench.ps1 -BuildId soft-base `
    -RasterSource build/software-d3d/baseline-raster.c `
    -OutputDirectory build/software-d3d/baseline
./scripts/build-software-bench.ps1 -BuildId soft-edge `
    -OutputDirectory build/software-d3d/candidate
```

The builder uses Open Watcom at `$env:WATCOM` or `C:\WATCOM`. It links no CRT
and audits imports against KERNEL32/USER32; DDRAW is loaded dynamically.
The tool creates/overwrites only `C:\V9XDIAG\V9XSOFT.INI` and temporary scratch
surfaces. No driver installation or display-mode change is required. Use a
16-bpp desktop to include VRAM; other depths or failed VRAM allocations yield
`VramAvailable=0` and RAM-only results. `Result=PASS` means the selected
workloads completed, not that missing VRAM coverage passed.

Example for the existing **Trio64 VM on loopback port 9871**:

```powershell
& $env:V9X_AGENT_CTL put -Port 9871 `
    -Source "$PWD\build\software-d3d\baseline\V9XSOFT.EXE" `
    -Destination C:\V9XDIAG\SOFTBASE.EXE -Json
& $env:V9X_AGENT_CTL exec -Port 9871 -Application C:\V9XDIAG\SOFTBASE.EXE `
    -WorkingDirectory C:\V9XDIAG -TimeoutSeconds 300 -Json
& $env:V9X_AGENT_CTL get -Port 9871 -Source C:\V9XDIAG\V9XSOFT.INI `
    -Destination "$PWD\build\software-d3d\baseline.ini" -Json
```

Repeat using the candidate EXE, a different guest filename and `candidate.ini`.
Run baseline again on the same boot to check drift. Keep VM configuration and
host load steady, and do not benchmark two guests simultaneously. Compare:

```powershell
./scripts/compare-software-bench.ps1 `
    -Baseline build/software-d3d/baseline.ini `
    -Candidate build/software-d3d/candidate.ini | Format-Table
```

The comparator rejects incomplete reports, differing configurations, missing
samples and changed colour/Z hashes. Memory read checksums depend on the
timed pass count and are not compared. Runtime and rendering correctness must
also be checked through the installed driver for any production change.

## Artifact identities (SHA-256)

| Artifact | SHA-256 |
|---|---|
| Baseline `V9XSOFT.EXE`, 8,192 bytes | `76CBD8A4241AD891D38AB0E436DABBC350B297DAAD3C4859AD6F7C2183DF9AE5` |
| Candidate `V9XSOFT.EXE`, 9,216 bytes | `F7FC2F037B4A191E8674EB39FFB73CFA50959511830471466F2A1141F4FB8F80` |
| Original installed Trio64 HAL, 42,496 bytes | `E68D4687A57AC45873238E23391C6E486424EFD844375C2836B36610DBA1A7AB` |
| Candidate/installed HAL `soft-edge-01`, 43,520 bytes | `3D0715A40D24ECBDF3838753F52893457337AA712DA04918ACDA9FBC2E88E836` |

Local executable snapshots and original HAL rollback copy are under
`build/software-d3d/`; they are not tracked artifacts. Rebuilt PE timestamps
may change file hashes even with the same source. The original probe binary
was 67,584 bytes, CRC32 `04EC1C9F`, build label `87b74a8-dirty`, identical in
both installed-driver runs. That label describes the probe, not the HAL.
