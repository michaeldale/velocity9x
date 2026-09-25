# 98SE's OPENGL32 asks the display driver QUERYESCSUPPORT(0x1101), then OPENGL_GETINFO with an unprimed buffer, seven times for one screensaver, and falls back to its own renderer when the name has no registry value

Date: 2026-09-26
Plan: `docs/plans/opengl-1.1-icd.md`, Phase 0.1
Guest: 86Box `Win86SE` (port 9869), Windows 98 SE 4.10.2222, OPENGL32.DLL
4.00 (753,808 bytes, CRC `02F464BB`), DDRAW.DLL 4.06.03.0518, S3 ViRGE/DX,
1024x768x16, driver build `1fa48b0-dirty` (boot 630)
Evidence: `2026-09-26-98se-opengl-escape-com1-9869.txt` (the COM1 capture),
`2026-09-26-98se-win16-callbacks-run1-V9XSNA7.ini` (the trace ring,
`Ring00`-`Ring13`)

## Instrument

`Control()` in `src/display16/dd16.c` now answers the OpenGL discovery
escape: `QUERYESCSUPPORT` with input word `0x1101` returns 1 when the family
advertises Direct3D, and `OPENGL_GETINFO` (0x1101) fills the caller's
buffer with the 9x layout the research record inferred from vmdisp9x:
`{ DWORD version = 2; DWORD driver_version = 1; char dll[262] = "Velocity9x" }`.
Each call writes a COM1 line and a shared-block trace event whose detail is,
for the query, the answer, and for the info call, the first DWORD found in
the caller's buffer *before* the driver wrote it. No registry value named
`Velocity9x` exists on the guest, on purpose: the question was what the
runtime does with the answer, not whether an ICD loads.

Workload: `C:\WINDOWS\SYSTEM\3D Pipes.scr /s` launched detached, left for
about ten seconds, then dismissed with ESC.

## Measured

COM1, after the driver's enable line:

```
V9X-DD opengl-query yes
V9X-DD opengl-getinfo Velocity9x
(... the same pair seven times in all)
```

The trace ring, in order: `Dd16OpenGLQuery enter 0x00000001` then
`Dd16OpenGLGetInfo enter <first DWORD>` seven times, with the first DWORDs
`0x00000000, 0x00001300, 0x25CF0000, 0x000036EC, 0x0000016F, 0x0000FFFF,
0x057F0406`. `DriverInitDone=0` at the time of the dump: DirectDraw had not
initialised the HAL, because the screensaver does not use DirectDraw.

The screenshot taken during the run shows the pipes rendered (gold pipes on
black, full screen): OPENGL32 served the screensaver through its own
renderer after the lookup of `Velocity9x` under `OpenGLDrivers` found
nothing.

## What this settles

- **The discovery sequence is as the research record inferred:**
  `QUERYESCSUPPORT` for 0x1101 first, then 0x1101 itself, with a non-null
  output buffer. Answering yes to the query is what makes the info call
  happen.
- **The buffer is not primed.** Seven different first DWORDs, none of them
  a size or a version, so OPENGL32 hands over uninitialised stack and reads
  back whatever the driver wrote. The driver must write the whole structure
  and cannot read a size from it.
- **Seven asks for one application.** The escape runs per context or
  pixel-format inquiry, not once per process; a driver that did expensive
  work in it would pay seven times. The answer here costs a shared-block
  lookup.
- **A name with no registry value is harmless.** OPENGL32 falls back to the
  generic renderer and the application draws, which is the plan's intended
  behaviour on a family whose ICD is absent - and, until Phase 3 ships the
  ICD, on every family.
- **The escape arrives before DirectDraw.** The shared block was allocated
  by the query's own `v9x_dd_block()` call, ahead of any
  `DDGET32BITDRIVERNAME`, which is a new first caller for that allocation
  and worked.

## What this does not establish

- That OPENGL32 would *load* an ICD named by that value, or what version
  handshake it performs with one: that is Phase 0.9's probe, with a
  registry value and a DLL behind it.
- Whether the `version`/`driver_version` values (2, 1) are read at all.
  Nothing in this run could tell; a wrong value would show as the ICD not
  loading in Phase 0.9.
- Anything about original Windows 98 or ME.

## Standing

Phase 0.1 is measured on 98SE. The escape answer stays in the driver as
shipped: it is the production path, gated on the same Direct3D capability
the HAL publishes on, and is inert while no registry value names an ICD.
