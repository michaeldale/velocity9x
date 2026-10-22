# Half-Life on the GMA 950 takes GL_ARB_multitexture when offered, and mwd5 runs as fast as it did on SGIS: 16.2-17.0 fps against 16.0-16.9

Date: 2026-10-22. Machine: the netbook (W9QI2BE2, GMA 950), boot 214,
the rest of the driver at `cd04a7b8`. Follows
[2026-10-22-netbook-arb-multitexture-quake3.md](2026-10-22-netbook-arb-multitexture-quake3.md);
plan [`../plans/opengl-arb-multitexture.md`](../plans/opengl-arb-multitexture.md),
step 5. Evidence:
[`../probe/netbook-hl1-arb-vs-sgis-2026-10-22/`](../probe/netbook-hl1-arb-vs-sgis-2026-10-22/).

## Why

Michael ran Half-Life OpenGL on the ARB ICD and found it drew right, but
thought it might be slower. This compares the two ICDs on the same boot
and the same demo, three runs each in one game session per ICD.

## Method

Half-Life 1.1.1.0, `HL.EXE -console -condebug -gl`, full screen at
640x480. Each session ran `timedemo mwd5` three times from the console,
typed through the agent. The ICD was swapped by `put` between the two
sessions, with Half-Life closed:

- **ARB:** `1a0c6e6c`'s ICD (build id `d4abadcf-dirty`, CRC32 DD71B54E).
- **SGIS:** the 0.24.0 ICD that was installed before (`cd04a7b8`, CRC32
  38069368).

## Results

| ICD | Path taken (V9XGL.LOG) | mwd5, 393 frames (fps) |
|---|---|---|
| ARB | `glActiveTextureARB`, `glMultiTexCoordARB` | 16.202, 16.936, 17.019 |
| SGIS | `glSelectTextureSGIS`, `glMTexCoord2fSGIS` | 15.978, 16.728, 16.906 |

In both orders the first run of a session is the slowest, by about
0.7-0.8 fps. Run for run, ARB is 0.1-0.2 fps ahead, which is smaller
than that spread. **No slowdown was measured.**

Both sessions drew the same work: 68,793 two-unit batches (663,408
triangles), none split and none refused, `cpu=0/0`. Michael's earlier
session on the ARB ICD logged the same 68,793. Both made 2,870,154
texture-coordinate calls for unit 0 or 1. Those calls took 592 ms on the
ARB entry point against 351 ms on SGIS's over the whole session (TSC
buckets). That is about 0.08 µs more a call, roughly 0.3% of the three
runs' time. The ARB forms gather their arguments generically, where
SGIS has a direct float path.

## Not established

- **Where 0.19.0's 20.8 fps went.** On 2026-10-18 (boot 207, 0.19.0
  package) the same demo ran 19.8, 20.8 and 20.8 fps
  ([2026-10-18-netbook-gma950-0.19.0.md](2026-10-18-netbook-gma950-0.19.0.md)).
  Both ICDs here are near 17. That difference is older than this
  change: the SGIS run uses the 0.24.0 ICD. It was not looked into.
  The 2026-10-09 runs were 15.8-17.8.
- **What Michael saw as slower** may be that difference, or a first
  run's lower figure. His own session's scores were not logged: its
  `qconsole.log` still held the 2026-10-09 lines (16.794 fps), probably
  because that session was started without `-condebug`.
- The frames of these runs were not looked at. Michael reports Half-Life
  looking right on the ARB ICD.

## After

The ARB ICD is back on the netbook (CRC32 DD71B54E), Half-Life closed.
