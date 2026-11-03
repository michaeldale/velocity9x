# The HD 7660D passes the Direct3D and OpenGL gates; the HD 7470 passes on the same build

Date: 2026-11-04
Status: Phase 6 of [ati-trinity-hd7660d.md](../plans/ati-trinity-hd7660d.md)
done. M-D7TFW4's HD 7660D (`1002:9901`, Trinity) runs V9XD32, V9XDDP,
3DMark 2000 and the Quake 3 demo on the CP. M-BXMFA8, which now holds the
HD 7470 (`1002:6778`, Caicos), runs the branch's clean build with the
same results as Trinity.
Captures: [`docs/probe/m-d7tfw4-trinity-d3d-2026-11-03/`](../probe/m-d7tfw4-trinity-d3d-2026-11-03/)
(`boot23-*`) and
[`docs/probe/m-bxmfa8-caicos-regression-2026-11-04/`](../probe/m-bxmfa8-caicos-regression-2026-11-04/).

## M-D7TFW4, HD 7660D (boot 23)

The fixed set from [the hang record](2026-11-03-trinity-hal-direct3d-hang.md)
(HAL `ead9d8f0...`, labelled `d3aee6b7-dirty`). Desktop 1024x768x16.

| Run | Result |
|---|---|
| V9XD32, 32 and 16 bpp (boot 22) | `PASS`, in the hang record |
| Quake 3 demo, owner's 1280x1024x32 | 33.4 fps, every batch on the engine, in the hang record |
| V9XDDP (from `41201dbc`) | `Result=COMPLETE`, `AfterModeBpp=16`. `V9XDD.INI` 92 `Ok=1` / 13 `Ok=0`; `V9XDD2.INI` 120 / 1 |
| 3DMark 2000, Run Default Benchmark | completes, **3634** 3D marks (`boot23-3dmark2000-score.png`) |
| `V9XEGCP.TXT` after all of it | unchanged from boot 19's (sha256 `FDD2A1E2...`): no quarantine |

The `Ok=0` keys: `D3DZCompareOk`, `D3DZWriteMaskOk`, `D3DZPCompareOk`,
`D3DZPWriteMaskOk`, `MixedOrderingOk`, `MixedColorEncodingOk`,
`MixedDepthEncodingOk`, `MixedOk`, `Tex8X8888HalvesOk`,
`Tex8DecalAlpha4444Ok`, `Tex8DecalAlpha565Ok`, `MipLadderShapeOk`,
`MipTriDegradedOk`, and `FlipPixelOk` in the second file. The HD 7470's
boot 6 (an older V9XDDP) read 93 / 13 and 121 / 1; the set below is the
comparison that holds the probe constant.

The 3DMark score is recorded, not compared.

## M-BXMFA8, HD 7470 (boot 61)

The HD 5770 the plan meant to use for the regression run is no longer in
M-BXMFA8. Boot 60 reported `Adapter=AMD Radeon HD 7470 (Caicos)`,
`1002:6778`, BAR0 `D0000000`; the owner's msg 83 of 2026-11-01 already
calls it "newly added". Caicos shares `eg_cp.c` and `d3d_eg.c` with
Juniper and Cedar, which this branch changed, so it stands in for the
HD 5770. Juniper itself is not covered.

Boot 60's four files were copied to the host first (HAL sha256
`ac33800d...`, 842,752 bytes). The ati set from `41201dbc` (clean,
`run-checks.ps1` green) went on by WININIT renames after every staged
hash matched, then `reset`; boot 61's four files read back with the
staged hashes (HAL `4f2215ac...`). V9XD32, V9XDDP and V9XTRACE from the
same build.

| Run | Result |
|---|---|
| V9XD32, 32 bpp | `PASS`; T13, T14, T15, T16 `PASS`; T12 one stage, T18 no T&L device, T19 no PS, as before |
| V9XD32, `/bpp16` | `PASS`, the same lines |
| V9XDDP | `COMPLETE`; **the same `Ok` keys as the HD 7660D, key for key**, in both files |
| Quake 3 demo, owner's 640x480x32, picmip 0 | 1346 frames, 40.1 s, **33.6 fps**; `GL_RENDERER` "Velocity9x Radeon HD 7470"; 32-bit colour, 16-bit Z |
| ICD paths in that run | hw 210,027 batches / 5,300,034 triangles; cpu 0; hw-refused 0; 355 textures, 18,922 KB, no failures or evictions |
| V9XTRACE after | `Ok=1`, `BuildsAgree=1`, `DriverBuild=0.35.0 41201dbc`, `EngineFlags=9`, `FlipHandled=31` |
| `V9XEGCP.TXT` | dated 2026-10-26 (the HD 5450's): no quarantine on this build |

The owner's `q3config.cfg` came back with the same values in reverse
order, which is how Quake 3 rewrites it; it was not restored.

## Not established

- The HD 5770 (Juniper) on this branch: not in any reachable machine.
- V9XTRACE counted 31 flips on M-BXMFA8 and 0 on M-D7TFW4 after a Quake 3
  run; `FlipPixelOk=0` on both. Not looked into.
- The owner's report (msg 83) that the HD 7470 scores about 1500 in
  3DMark 2000 against the HD 5450's 4800. 3DMark was not run on
  M-BXMFA8 here.
- What either monitor showed.
- `V9XGL.LOG` on M-BXMFA8 is 10 MB and stalled a relay `get`; only its
  tail is kept.
