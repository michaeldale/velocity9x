# Half-Life mwd5 on the HD 4600: 52.4 fps Direct3D, drawn whole, against the GMA 950's 41.3

Date: 2026-10-18. Machine: the i5-4570 (`8086:0412`), boot 18, DRV and
HAL `0.19.0 59f2ffcf-dirty`: Phases A-F3 of
[intel-haswell-direct3d.md](../plans/intel-haswell-direct3d.md), with the
timing buckets on. Evidence:
[`../probe/hsw-4570-halflife-d3d-2026-10-18/`](../probe/hsw-4570-halflife-d3d-2026-10-18/).

## Setup

- **Install.** Half-Life GOTY from Michael's ISO, copied to `C:\HLCD`
  (unpacked on the host: the agent caps one transfer at 256 MiB).
  Installed into `C:\Sierra\Half-Life` with Michael's CD key, and his
  agreement to Sierra's and Valve's licences.
- **DirectX 7.** The installer put DirectX 7 on this Windows 98 SE, at
  Michael's say-so. Every 4570 measurement before boot 18 ran on the older
  runtime.
- **Patch and demo.** Patched to 1.1.1.0 with `HL1110.EXE`, fetched by the
  4570 from Michael's server. `mwd5.dem` is `tests/benchmarks/hl1`'s (CRC
  `8D0D0876`); the server's copy matched it.
- **Run.** `hl.exe -console -d3d -w 640 -h 480 -full -condebug`, the
  desktop at 1024x768x16, `fps_max` the default 72. One console session,
  `timedemo mwd5` three times, best of three
  (`tests/benchmarks/hl1/README.md`).

## Results

| Run | Frames | Seconds | fps |
|---|---|---|---|
| 1 | 393 | 8.264 | 47.557 |
| 2 | 393 | 7.493 | **52.447** |
| 3 | 393 | 7.515 | 52.292 |
| 4 (with screenshots running) | 393 | 7.831 | 50.185 |

**Best of three: 52.447 fps.** Runs 2 and 3 agree within 0.3%.

**Against the GMA 950:** 41.3 fps on the netbook, best of its runs
(`2026-10-07-gen3-clip-in-core.md`, DDI 5, 640x480x16). That is 27% more
here. The two are not like for like:

- the driver builds differ (0.12.x against this tree);
- the CPUs differ (a 1.6 GHz Atom against a 3.2 GHz Haswell);
- this machine now has DirectX 7, and the netbook's runtime is not
  recorded there.

So the number says the 4570 runs Half-Life faster, not by how much the
engine is responsible.

## What the driver did (boot 18, HL.EXE the only process)

- One Direct3D context.
- **1,072,291 engine batches** carrying **12,261,209 triangles**: 11.4 a
  batch.
- One Z buffer placed at pitch 1280 (640 x 2, already whole tiles) and
  accepted.
- 241 textures created.
- No refusal (`M64PolicyLast` 0, every `D3dTextureRefused*` 0) and no
  engine timeout.
- **No flips.** Half-Life's Direct3D presents with Blt: 9,466 copies
  through the blitter (`TimeBltCopyCalls`), and `FlipHandled` and
  `FlipDeclined` are both 0. So F3's flip path is not on this game's path.
- `DrawsNoHandle` 1,164, equal to `TimeBltFillCalls`: the frame clears.
- Over the session, the GPU's share (render drain) is about 36% of the
  engine-draw time. That is the same shape as 3DMark's Game 2: the
  per-batch CPU cost dominates
  (`docs/issues/2026-10-17-haswell-3dmark99-flicker-and-game2.md`).

## The picture

A screenshot taken during a fourth run (`mwd5-frame-mid-run.png`; blit
presentation lands on the GDI page) shows the frame whole:

- the Xen sky;
- textured terrain and structures;
- the HUD and the weapon;
- the blended gluon beam.

Nothing missing or misplaced is visible in that frame. One frame is not
the whole demo, and nobody watched the monitor for this run.

## Not done

- Half-Life `-gl` on this machine. The ICD path draws CPU-level textures
  untextured on Haswell today; see
  [intel-haswell-glide-3dcif-opengl.md](../plans/intel-haswell-glide-3dcif-opengl.md),
  step 1.
- Separating the CPU's share from the GPU's within mwd5 alone: the timing
  buckets cover the whole session, menus included.
