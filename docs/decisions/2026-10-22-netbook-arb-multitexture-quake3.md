# GL_ARB_multitexture on the GMA 950: the probe matches SGIS exactly, and Quake 3's demo001 runs 14.8 fps with it against 13.1 without

Date: 2026-10-22. Machine: the netbook (W9QI2BE2, GMA 950, 945GSE), boot
214, 640x480 16 bpp desktop. Plan:
[`../plans/opengl-arb-multitexture.md`](../plans/opengl-arb-multitexture.md),
steps 4 and the netbook half of 5. Evidence:
[`../probe/netbook-arb-multitexture-2026-10-22/`](../probe/netbook-arb-multitexture-2026-10-22/).

## What ran

The rest of the driver stayed at merged main `cd04a7b8` (0.24.0 code,
installed since boot 214). Only `V9XGL.DLL` was replaced, by `put` with
Quake 3 closed: build `d4abadcf-dirty`, which is commit `1a0c6e6c`'s ICD
built before that commit was made (459,776 bytes, CRC32 DD71B54E). The
0.24.0 ICD it replaced (456,704 bytes) is kept in the session's
scratchpad. V9XGLP was built from the step 4 source (`Build=1a0c6e6c-dirty`)
and run as `C:\V9XGLPA.EXE`.

## V9XGLP

`Result=PASS`. The new ARB section draws the SGIS section's seven cases
(MODULATE, REPLACE, BLEND, DECAL, unit 1 at its own s left and right,
unit 1 disabled) three ways: immediate mode with `glMultiTexCoord2fARB`,
`glDrawArrays`, and `glDrawElements` from one texture-coordinate array
per unit. All 21 are `Ok=1`, and every colour read back is the SGIS
section's, to the bit (`ArbImmModulate`, `ArbArraysModulate`,
`ArbElementsModulate` and `SgisModulate` all `0x006B6510`, and so on).
`ArbMaxUnits=2`, `ArbActiveTexture` and `ArbClientActiveTexture` read
`0x84C1` after selecting unit 1, and no GL error after setup or after
the draws.

This compares against the SGIS path on the same engine, not against the
software engine, which the plan's step 4 named. The SGIS expectations
are GL 1.1 table 3.18's colours, within 8 a channel.

## Quake 3 demo 1.11, `timedemo 1`, `demo demo001`

The default settings Michael used (`r_mode 3`, 640x480, 16-bit colour
and Z, `r_picmip 1`). The demo was started from the console. A `+demo`
on the command line left the game at the menu.

| ICD | `r_ext_multitexture` | Quake 3 reports | Result |
|---|---|---|---|
| 0.24.0 (`cd04a7b8`), Michael's run | 1 | not offered by this ICD; console not kept | 12.6 fps |
| `1a0c6e6c` | 1 | "...using GL_ARB_multitexture", "multitexture: enabled" | 1346 frames, 90.9 s: **14.8 fps** |
| `1a0c6e6c` | 0, after `vid_restart` | "...ignoring GL_ARB_multitexture", "multitexture: disabled" | 1346 frames, 103.0 s: **13.1 fps** |

The two `1a0c6e6c` runs are one process, back to back. Michael's 12.6 is
from another process, earlier in the same boot, on the 0.24.0 ICD.
Run-to-run spread was not measured: no run was repeated. So the 12.6 to
13.1 gap is not attributed to anything.

V9XGL.LOG for the multitexture run: Quake 3 called `glActiveTextureARB`,
`glClientActiveTextureARB` and `glMultiTexCoord*ARB`. The ICD drew
48,763 two-unit batches (1,223,841 triangles) on Gen3, with no split and
no refusal. The multitexture-off run added no two-unit batches.
`hw-refused` stayed 0 for the whole process. 44 batches went to the CPU
path, in two bursts of 22 at the end of each demo, not examined.

## Not established

- **What the frames looked like.** No screenshot was taken during the
  demo: the agent's screenshots are blind during flips. Whether the
  single-pass lightmaps draw as the two-pass ones did is unchecked.
- **The menu logo.** On this ICD, the menu's QUAKE III logo drew
  clean in a screenshot before the first demo
  (`quake3-menu-before.png`). It had green and red fringes in one after
  that demo, and still had them after `r_ext_multitexture 0` and
  `vid_restart` (`quake3-menu-after.png`). Turning multitexture off did
  not clear it, so it is probably not the ARB path. It may also be just
  another frame of the animated logo. Not followed.
- **Quake 2 and Half-Life**, which may now take the ARB path where they
  took SGIS. Not run.
- **The HD 4600**, the plan's other step 5 machine. Not run.
