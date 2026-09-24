# 3DMark99 on the netbook's 0.8.0 install: it completes, and many textures are missing

2026-09-24, MICHAEL-NETBOOK (945GSE / GMA 950, `8086:27AE`), hard-disk
Win98 SE, v9x-remote-agent 0.6.2 at 10.0.1.254:9869. Driver 0.8.0
`intel-gma`, build `890c828`, boot 2 after a fresh install, no arm file
(Direct3D on by default: `EngineCaps=0x214`, D3D | FLIP | FLIP_RING).
Record only; nothing investigated.

## Run

3DMark 99 Max, defaults as it chose them: "Velocity9x Intel GMA 950
(945GSE)", 1024x576, 16-bit colour, 16-bit Z, triple buffer, every test
selected. It first warned that 800x600 is unavailable, which is correct -
the VBIOS offers nothing taller than 576 lines. Launched through the
agent's `exec`, not `shell` (a DOS VM puts this display on the VGA plane:
`2026-09-24-a-dos-vm-switches-the-netbook-to-the-vga-plane.md`). The
desktop had been mode-set back to 1024x576x16 after the last DOS VM.

Started 21:10:46, score dialog by 21:17 (~6 min).

**834 3DMarks, 15102 CPU 3DMarks.** `2026-09-24-netbook-3dmark99-score.png`.
No earlier netbook 3DMark99 score is on record to compare with.

## What the operator saw

- **Lots of textures missing.**
- **No flickering.**
- **Many tests never ran correctly.** Which tests, and what "not correctly"
  looked like in each, was not recorded. 3DMark's per-test results were not
  saved.

Screenshots cannot see flipped frames on this machine, so the panel is the
only view of rendering; there is no image capture from this run.

## What the driver counted

`2026-09-24-netbook-3dmark99-before-V9XSNA4.ini` (Direct3D counters all
zero, `CountDriverInit=2`) and `...-after-V9XSNA5.ini`, both from the
`d88213f-dirty` V9XTRACE. Counters are cumulative from boot; the "before"
file shows no Direct3D activity, so these are this run's:

| | |
|---|---|
| contexts created / destroyed / rejected | 3 / 3 / 0 |
| `D3dRenderPrimitiveCalls` | 25,908 |
| `D3dRenderStateCalls` / `D3dExecuteCalls` | 0 / 0 |
| `DrawsToBack` / `DrawsToFront` / `DrawsIntoPresented` | 169,298 / 0 / 0 |
| `DrawsNoHandle` | 26,583 |
| `TextureHandleSets` | 4,676 |
| `DrawsMagLinear` = `DrawsMinLinear` | 142,714 |
| texture creates / destroys | 560 / 560 |
| every `D3dTextureRefused*` | 0 |
| `D3dTextureCreateSysmem` | 0 |
| `D3dTextureLastSize` / `LastOffset` / `LastCaps` | 128 / `0x00480000` / `0x10405008` |
| `D3dTextureLastTexels` | `0x00000000` |
| `D3dMipChainChecks` | 0 |
| depth offered / accepted | 3 / 3 |
| flips handled / ring-issued / declined / ring-refused | 2,732 / 2,739 / 0 / 0 |
| `DrawsFlipWaited` / timeouts | 402 / 0 |
| engine FIFO / idle timeouts, resets | 0 / 0 / 0 |
| `PipestatBFirst` / `PipestatBOr` | `80000202` / `00000202` |
| `WmWritten` | `0x0314011A` |
| `D3dPidDistinct` | 1 |

Readings, none of them tested:

- **Nothing was refused.** Every texture was created, every flip taken,
  no draw aimed at the displayed buffer, no engine timeout. Whatever is
  missing is missing from submissions the driver accepted, which is the
  same shape as intel102 ("the draws are aimed correctly and still nothing
  lands").
- **`D3dTextureLastTexels=0`** for the last texture created. If that field
  samples texel content, the last texture read as zero; what the field
  samples, and whether it is representative, is not established here.
- **No fresh display underrun on pipe B.** `PipestatBFirst` had bit 31 set
  from earlier; `PipestatBOr` after the clear does not. Consistent with the
  operator seeing no flicker.

## Next, when this is picked up

Per-test results from 3DMark's Result Browser; a photograph of one test
with missing textures; and which tests "never ran correctly" in the
operator's words. The earlier intel94-intel102 records on untextured and
black-buffer draws are the place to start.
