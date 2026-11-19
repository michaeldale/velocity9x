# Sandy Bridge HD 3000 on the Haswell path: BCS blits at 8, 16 and 32 bpp, and flips

Date: 2026-11-19
Status: **measured once, M-NZHWBR boot 61.** Steps 4 and 5 of
`docs\plans\intel-sandybridge-gen6.md`. Build `448fc8ab-dirty`, branch
`intel-snb` (uncommitted when run): `intel-gma` claims `8086:0112` as
`hd3000-snb`, and `HSW_ENGINE_OPEN` chip 2 takes Gen6 forcewake
(`2026-11-19-sandybridge-read-only-mmio.md`).

Evidence: `docs\probe\m-nzhwbr-snb-hd3000-2d-2026-11-19\`, every file
prefixed `boot61-`.

## Install

The eight driver files went over the released 0.56.2 `vbe` set by
WININIT.INI renames and `reset` (boot 60 to 61), the release script's
route. All eight hashes matched after the boot. Only files were swapped:
the device's registry description still reads "Velocity9x VBE-generic
display", from the vbe INF. Large puts over the relay needed the agent
scripts' `PipelineWindow` at 4; at 32 the HAL's put timed out.

## Enable and engine open (`boot61-V9XHW.INI`, `boot61-V9XBOOT.INI`)

- `Stage=enable-ok` at 1024x768x32, `Aperture=m=14 bar=2`, 64 MiB mapped,
  PAT write-combining over it.
- `Adapter=Intel HD Graphics 3000 (Sandy Bridge)`,
  `EngineStatusEnable=open-reused`, `EngineCaps=00080007`: fill, copy,
  flip and the Ivy Bridge plane bit; no D3D. `Direct3DMode=none`.
- `HeapLayout=chip`, `Reg1082C0=00000000` (stolen-reserved not enabled, so
  nothing protected), `ReserveOffset=03FA0000`: the reserve at the top of
  the 64 MiB, under the probe region.
- `RenderClock=not-requested`, as built.

So the open passed all eight of its checks on Gen6 with the legacy
forcewake pair, and the GTT check at the reserve held there too.

## DirectDraw on the BCS ring

`V9XDDF.EXE` at each depth, the depth reached by `V9XMSW /set:` (the
ChangeDisplaySettings path Display Properties uses: Disable, then a fresh
Enable), 32 first, then 16, 8 and 32 again:

| Depth | V9XMSW | `V9XDDF` | Surface pitch | Fill pixel | Copy pixel | Mismatches | CountBltEngine / CountBlt (cumulative) |
|---|---|---|---|---|---|---|---|
| 32 | (boot) | PASS | 256 | `0012A5C3` | `00ED5A3C` | 0 / 0 | 3 / 3 |
| 16 | PASS | PASS | 128 | `07E0` | `F81F` | 0 / 0 | 6 / 6 |
| 8 | PASS | PASS | 64 | `5A` | `A5` | 0 / 0 | 9 / 9 |
| 32 | PASS | PASS | 256 | `0012A5C3` | `00ED5A3C` | 0 / 0 | 12 / 12 |

Every blit ran on the engine. Engine FIFO, idle and ring-space timeouts and
engine resets: 0 throughout. The pixels are the HD 4000's
(`2026-10-21-ivybridge-bcs-blits.md`). The desktop draws at 1024x768x32
afterwards (`boot61-desktop-1024x768x32.png`).

As on Ivy Bridge, `EngineStatusEnable` reads `open-reused` after each
change, so these files do not show whether the Disable hook's close ran in
between.

## Flips (`boot61-ddp-V9XDD.INI`, `boot61-ddp-V9XSNAP.INI`)

`V9XDDP` default, a 640x480x16 flip chain:

- `SetModeHr` 0, `FlipHr` 0, `Flip20Ms` 320, `FlipMaxMs` 17: twenty
  `DDFLIP_WAIT` flips paced at 60 Hz.
- `FlipHandled` 23, `FlipTakenAtDone` 23, `FlipNotTakenAtDone` 0,
  `FlipDeclined` 0, `FlipForcedIdle` 0, `DrawsIntoPresented` 0: every flip
  the HAL called done had `DSPASURFLIVE` holding the base it wrote.
- `FlipPixelOk=0`, the GDI readback this probe fails under real flips on
  the HD 4600 and HD 4000 too.

So plane A's base and `DSPLINOFF` take the Ivy Bridge split unchanged on
Gen6. The flips were not watched on the monitor; the machine came back
to its 1024x768x32 desktop.

## Not measured

- The engine close on Disable, and a DOS box.
- Flips at 8 and 32 bpp, and at the 800x600 chain.
- Gen6 RPS (the render clock stays at what the PCU picks).
- Anything 3D: Direct3D, OpenGL and Glide run in software or not at all
  on this part.
