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

## Refilling only what changed (same day, same boot)

The ICD now records, per texture object, the rectangles glTexSubImage2D
wrote since its hardware copy was last current, and refills only those,
locking no level nothing touched. A copy that is older than an image
specification, squared, or of fitted levels still refills whole.
Evidence: `docs/probe/sgis-multitexture-netbook-2026-10-05/partial-uploads/`.
ICD alone replaced (the ABI did not change).

| | multitexture | without |
|---|---|---|
| Quake 2 `timerefresh`, demo1 spawn | **35.29 fps** (was 21.00) | 32.96 fps (was 31.83) |
| Half-Life `timedemo mwd5` | 15.68, 15.53; 15.05, 15.90 | 17.24, 18.19 |

Quake 2: 41,211 refills of which 41,144 partial, 2.2 MB refilled whole
(was 614 MB). Multitexture is now the faster path.

Half-Life: whole refills fell from 95.8 MB to 4.5 MB, and it is still
slower with the extension. A HAL snapshot either side of one measured
timedemo each way (`hlprof.py`) shows the HAL is not where it goes: about
297,000 submissions in both windows, render-interface time 9.30 s against
9.00 s, no wait for the GPU in either, breadcrumb drains 0.03 s against
0.04 s. Per frame Half-Life sends about 3,500 vertices and 407 draws with
the extension, 3,660 and 358 without: the extension saves it little
geometry and costs it draws. What the remaining difference - about 8 ms a
frame - is spent on, in the ICD or in Half-Life itself, is not measured.
A first attempt to sum the ICD's ten-second buckets misparsed the log and
was discarded.

## Timing the ICD's entry points, and texture state that drew the batch

Evidence: `docs/probe/sgis-multitexture-netbook-2026-10-05/state-no-flush/`
(`entrysum.py` sums the ten-second lines of a log's last session, demo
intervals only; Win9x reuses process ids, which is what the discarded
first attempt above got wrong).

The ICD now times each kind of texture command, the held batch it draws
first, the SGIS calls and glEnable/glDisable, with call counts (the
`entry` line). Half-Life `mwd5`, per frame, on the partial-upload ICD
(`hl-entry-*-per-frame.txt`):

| | multitexture | without |
|---|---|---|
| glTexEnv calls | 543 (13.6 ms) | 361 (10.8 ms) |
| glBindTexture calls | 80 (2.8 ms) | 51 (2.1 ms) |
| held batch drawn by a texture command | 425 (15.6 ms) | 375 (12.2 ms) |
| draws | 427 | 382 |
| glMTexCoord2fSGIS / glSelectTextureSGIS | 963 (0.10 ms) / 222 (0.04 ms) | - |

Nearly every draw was a texture command drawing the held batch, and the
commands Half-Life calls most - glTexEnv and glBindTexture - change no
texel. The batch carries its own copy of what it was described with and
reaches its object only by name, so binding, parameters, the
environment, pixel store and name generation no longer draw it; only
glTexImage2D, glTexSubImage2D and glDeleteTextures do.

V9XGLP afterwards: every key as before on the netbook (boot 100) and on
86Box `Win98SE-Fast-D3D` (software engine, boot 601).

| | multitexture | without |
|---|---|---|
| Half-Life `mwd5` | 17.64, 17.65 fps | 17.63, 17.40 fps |
| Quake 2 `timerefresh` | 34.92 fps | 30.40 fps |
| Half-Life draws a frame | 48 (was 427) | 42 (was 382) |

The two Half-Life paths are now level. Its frame rate did not follow its
draws - 382 to 42 a frame without multitexture left it at 17.4-17.6
where it was 17.5-17.9 - so the timedemo is now bound by something the
ICD's counters do not show: Half-Life's own work on the Atom, or the
GPU. Not measured. Quake 2's no-multitexture figure is 2.6 fps under the
previous build's 32.96; not separated from run-to-run variation or the
added timers.
