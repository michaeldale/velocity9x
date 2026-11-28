# GeForce 9500 GT (G96): a 3D push ring with a fence, measured and not shipped

Date: 2026-11-28

Step 1-2 of [nvidia-tesla-3d-push-ring.md](../plans/nvidia-tesla-3d-push-ring.md),
on M-Z2T02D after the write-combining fix
([2026-11-28-g96-pushbuffer-write-combined.md](2026-11-28-g96-pushbuffer-write-combined.md)).
The code is on branch `nvidia-push-ring`, not on main. Evidence:
[nvidia-push-ring-m-z2t02d-2026-11-28/](../probe/nvidia-push-ring-m-z2t02d-2026-11-28/).

## What was built

- A fence: each 3D batch ends with SET_OBJECT on the 3D subchannel and
  QUERY_GET (1B00h-1B0Ch, GET 1000F010h, Mesa's nv50 fence) writing its
  sequence to a word in the reserve (E7FF0h) once the CROP unit is done.
- The push area as slots; `v9x_nv50_3d_begin` waits only for its slot's
  last sequence instead of draining the engine; submits go in at PFIFO's
  IB PUT, waiting if the ring is full.
- Texture tables per slot (4 TIC, 4 TSC at E6000h + 100h a slot), the
  batch pointing TIC/TSC_ADDRESS at them with both flushes.
- A full drain (`v9x_nv50_3d_quiesce`, which also checks the fence word
  equals the last sequence) before the setup programs and a rewritten
  pixel shader slot.
- Host tests for the fence and table pushes and the wrap-safe compare.

## Measured

Same profiled 3DMark 2001 SE run each time (V9XPROF, defaults, agent
driven), recorded, not chased:

| Build | Score | Car Chase low / high | Polling (read32 + fence wait) |
|---|---|---|---|
| Write-combined, one push area (7644e535) | 2077 | 23.3 / 14.2 fps | 32.4% |
| Ring, 4 slots of 192 KiB, chunks unchanged | 1982 | 23.1 / 8.6 fps | about 21% |
| Ring, 2 slots of 384 KiB, chunks 1,536 / 768 triangles | 1775 | 7.9 / 17.5 fps | about 23% |

- The 4-slot build cut batches for the whole area (2,048 triangles,
  about 129,000 words at the widest vertex with a second unit) into
  49,145-word slots: its largest batches overflowed and were refused,
  which fits Car Chase high's drop. The 2-slot build fixed that.
- Neither build gave the engine up (`V9XN50F.TXT` unchanged, 1,805
  bytes, the boot-12 record). On the 4-slot build V9XD32 failed only T5
  at 32 and 16 bpp, V9XGDI passed, and Quake 3 demo001 ran at 75.6 fps
  (62.9 on 7644e535); on the 2-slot build V9XD32 and Quake 3 ran while
  the agent was restarting after the reset and returned nothing.
- The polling share fell, but the scores did not rise, and single tests
  moved by factors of two in both directions between builds (Car Chase
  low 23.3 to 7.9 fps). What makes them move was not found: no refusal
  counts were read, and one run of each is not enough to separate the
  ring from run-to-run spread.

## Decision

Not shipped: on this evidence it is not better and may be worse, and it
changes how every Tesla 3D batch is fenced and indexed. Main keeps the
write-combined single push area, reinstalled on M-Z2T02D. To pick it up:
read the refusal and decline counters (V9XTRACE) on a ring run, run each
build more than once, and look at what Car Chase low does per batch.

Gates on the branch: build-host (with the new tests), the nvidia package
built and audited.
