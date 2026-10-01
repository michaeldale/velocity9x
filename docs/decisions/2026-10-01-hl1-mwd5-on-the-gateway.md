# Half-Life mwd5 on the Gateway: 6.65 fps OpenGL, 13.07 fps Direct3D with three quarters of its batches refused

Date: 2026-10-01
Machine: Gateway SOLO2150, 10.0.1.22, ATI Rage Mobility-M (Mach64), 4 MiB,
~450 MHz Pentium II-class CPU, 1024x768x16 desktop, boot 80. Driver set:
the `dp-record-merge-ati` job's DRV/VXD (ABI 2026093003) with the `d78d4f4`
HAL; the ICD is that job's `V9XGL.DLL`, which predates the ICD changes of
`43fe5b7`.
Evidence: [`../probe/hl1-gateway-2026-10-01/`](../probe/hl1-gateway-2026-10-01/)

## Setup

Half-Life GOTY installed from `Half-Life-Game-Of-The-Year-Edition.iso`
(the operator's copy) and patched with `HL1110.EXE` to 1.1.1.0, into
`C:\Sierra\Half-Life`. The netbook's video settings were imported into
`HKCU\Software\Valve\Half-Life\Settings` (640x480x16 fullscreen,
`EngineType=2`) without its CD key; the operator supplied the Gateway's
key. `config.cfg` is the netbook's (`fps_max 72`). `mwd5.dem` from
`tests/benchmarks/hl1` (CRC 8D0D0876). Procedure as the netbook's: one
console session, three `timedemo mwd5`, best of three.

## Results

| Renderer | Runs (fps) | Best |
|---|---|---|
| As configured (no flag), which ran **OpenGL** | 6.073 (cut short at 277 frames by the next command), 6.647, 6.636, 6.635 | **6.647** |
| `-d3d` | 11.856, 13.066, 12.824 | **13.066** |

With the same registry settings and no renderer flag the netbook ran
Direct3D and the Gateway ran OpenGL: the Gateway's Direct3D counters did
not move during the first session and the ICD's log carries the session's
process. Why the two machines chose differently was not investigated.

## Neither number is a clean measurement

**Direct3D.** Over the `-d3d` session the Mach64 drew 204,048 batches and
refused **580,351** (`M64Refused`, = `BatchesEngineRefused`), with
`M64PolicyLast=12`, `V9X_M64_REFUSE_TEXTURE_OP`: Half-Life's texture
combine is not one the Mach64 policy maps, so about three quarters of its
batches were not drawn. 13.07 fps is the speed of a frame missing most of
its geometry. Only the last refusal reason is kept, so the mix is not
known; nobody watched the screen.

**OpenGL.** The ICD log for the session (`gateway-hl-opengl-V9XGL-timing.log`)
shows 2,590 texture creates with 1,229 failures and 1,217 evictions on the
4 MiB card, about 1.92 million triangles refused by the hardware path
(`failed r4`) and sent to the CPU copy, and about 1.02 million draws through
the non-square texture copy. It is also the ICD from before today's fixes.

No engine timeouts or resets in either session.

## Not done

- Rerunning OpenGL with the current ICD (`43fe5b7`), which needs the
  Gateway's DRV/HAL moved to the current ABI set first.
- Finding which texture op Half-Life uses that the Mach64 refuses.
- Looking at the picture.
