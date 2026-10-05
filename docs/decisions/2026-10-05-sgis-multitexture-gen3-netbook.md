# GL_SGIS_multitexture on Gen3: correct on the GPU, and slower in both games

Date: 2026-10-05. Plan: `docs/plans/gen3-sgis-multitexture.md`, step 4's
netbook gate. Machine: MICHAEL-NETBOOK (945GSE / GMA 950), boot 100,
1024x576 565, wifi 10.0.1.254. DRV, mini-VDD, HAL, settings page and ICD
deployed together by WININIT rename from ff294d9's tree (code as f7286f4);
the boot-99 set is kept in the session scratchpad.
Evidence: `docs/probe/sgis-multitexture-netbook-2026-10-05/`.

## Correct

- **V9XGLP** (`V9XGLP-boot100.ini`): `Renderer=Velocity9x GMA 950`,
  `SgisAdvertised=1`, all seven SGIS cases in tolerance. MODULATE read
  0x6B6510 where the software engine read 0x636508: one 5-bit red step and
  one 6-bit green step, Gen3 rounding where the CPU truncates. The ICD's
  counters for the probe show 82 batches on hardware textures, none on the
  CPU, none refused, six of them two-unit: Gen3 drew the two-unit draws,
  not the software fallback (which takes no surface texture).
- **Quake 2 3.14 demo**, demo1 spawn, 640x480 windowed, console closed
  (`q2-spawn-mtex{1,0}.png`): over the left wall, the right panel and the
  floor (133,600 pixels) every pixel but two is identical or within one or
  two 565 steps between the two paths.
- **Half-Life 1.1.1.0** `-gl` takes the extension: 268,776 two-unit
  batches in the run, MODULATE on unit 0 and on unit 1 (the census's
  `tex1=.../01010200`). No draw failed.

## Slower

| | multitexture | without | |
|---|---|---|---|
| Quake 2 `timerefresh`, demo1 spawn | 21.00 fps | 31.83 fps | `gl_ext_multitexture 1` / `0` |
| Half-Life `timedemo mwd5` (two runs after a warm-up) | 14.83, 14.59 fps | 17.47, 17.88 fps | ICD built with the extension off |

Recorded as measured; neither is a target. Half-Life ignores `-nomtex`
(the same 268,776 two-unit batches), so its comparison is this tree's ICD
built once with the extension not offered, swapped in for the run and
swapped back after. That build was not committed.

## Where the difference is, and what is not yet separated

- **Texture uploads.** Quake 2's ICD counters: 19,186 hardware-copy
  uploads, 614 MB, with multitexture; 2,658, 85 MB, without.
  Half-Life's: 3,093 uploads, 95.8 MB, against 696, 19.1 MB. Each is a
  whole 128x128 lightmap page (32 KB): both games' multitexture paths
  update the dynamic lightmap per surface as it is drawn, where their
  two-pass paths update it once per lightmap block, and the ICD refills a
  texture's whole hardware copy - a DirectDraw Lock, which drains the
  GPU, and the copy through the uncached aperture - on every
  glTexSubImage2D. The ICD's `upload-ms` (the copy alone) reached about
  500 ms in a ten-second Half-Life interval with multitexture.
- **Not separated:** what the drains cost against the copies, and
  whether the two-unit program costs the GPU more per pixel than two
  passes. Half-Life's ten-second lines are not aligned to the demo, so
  per-frame figures from them (727 draws a frame against 826, 18 ms of
  interface time against 20.7) are indications only.

## What follows

The lever the counters point at is the upload path, not the program: an
update of a sub-rectangle could write that rectangle only, and a
frequently updated texture could avoid the drain. Either is a change to
the ICD's hardware copies and the HAL's Lock, outside this plan's steps.
Until one lands, the extension as offered on Gen3 is a regression for
both games measured.
