# A second 915GM runs GTA and Deus Ex on 0.19.0 with no fault: the only limit seen is 8 MiB of stolen memory

Date: 2026-10-19. Status: recorded. Source: two reports from V9XTRACE's
upload, read from the update server; their files stay there.

## The machine

| | |
|---|---|
| IGD | `8086:2592` rev 03 (915GM, GMA 900), SUBSYS `1025:006A` (Acer) |
| Earlier reporter | SUBSYS `17AA:2062` (Lenovo), issue 7 and the Carmageddon II issue |
| Windows | 98 SE (4.10.2222 A), DirectX 9.0c runtime (4.09.00.0904), 1 GiB RAM |
| Driver | 0.19.0 `f0f12a6b`, all four binaries agree |
| Video memory | VBE reports 8,060,928 bytes (from 8 MiB stolen) |
| Display | Pipe B (LVDS) with the panel fitter on; 1280x800 panel |

Whether this is the earlier reporter on a second laptop is not known.

## V9X-QGKZJ6: Grand Theft Auto, DirectDraw only

Desktop 1024x768x8; the game at 640x480x16 (`DSPBCNTR 0x95000000`,
pipe source `0x027F01DF`), scaled by the fitter (`PFIT_CONTROL
0x80002668`). No description from the reporter.

- 3,068 ring flips issued, 3,063 completed, 0 refused, 0 deferred.
- `FlipStillDrawing` 91,940: the game polls the flip status rather than
  waiting; expected.
- 0 engine FIFO or idle timeouts, 0 resets, 0 ring-space timeouts.
- Frame covers 1-3 drawn across 632x472 of 640x480.
- The process left through `ExitProcess`, exit code 0
  (`WatchExitVia=1`), no fault, no message box.

## V9X-8PXDMY: Deus Ex, Direct3D

Desktop 1024x768x32; the game at 640x480x16 with a 16-bit Z buffer.
Three runs (74-152 s, 158-212 s, 235-458 s of uptime), each ended by
process exit (`Ended=2`, `V9X_WATCH_ENDED_TERMINATED`). The reporter wrote
only "Tested Deus Ex".

- 961,737 DrawPrimitive calls, 1,420,233 Gen3 draws, **0 refused**,
  0 declined triangles, 0 texture refusals of any kind, 0 mip-chain gaps.
- 226 texture creates; no texture-table overflow.
- 0 engine timeouts or resets; async submission on, 1,457,508 breadcrumbs,
  none abandoned.
- `RenderDrainStalls` 1,083,857 of 1,321,527 drains. Not pursued
  (no performance chasing); recorded for anyone who looks at the Gen3
  drain later.

### Two counters that look wrong and are not

- **84 `TextureDestroy` calls returned `E_INVALIDARG`** at shutdown
  (`Ring10`). Deus Ex released the texture surfaces first;
  `v9x_d3d_textures_forget_surface` had already cleared those records, so
  the runtime's later destroy names a handle that no longer exists. The
  Rage XL's Quake 2 traces of 2026-10-03 show the same pattern. Harmless
  as far as these traces show.
- **`MipTreeFrees` 279 against `MipTreeAllocs` 277.** The free count also
  counts padded Z buffers (`d3d_i9xx.c`, `v9x_d3d_i9xx_destroy_surface`),
  and `ZPlaced` is 2. Balanced.

### What memory cost

`MipTreeDeclined` 4, last reason 4 (`V9X_D3D_PLACE_ALLOC`: DirectDraw's
heap had no block of that size). Those four chains were placed by the heap
instead and are sampled at their top level only. With a 32 bpp desktop
the driver reserves 3 MiB of the 7.7 MiB for the visible screen
(`VddReserve`). The game's three 640x480 buffers and its Z buffer sit
inside that reserve (offsets `0x0` to `0x258000`), leaving about 4.7 MiB
for textures.

This is the netbook's Serious Sam limit
([plan](../plans/intel-gen3-system-memory-textures.md)) appearing on a
second chip in the field, in a game far lighter than Serious Sam. It moves
the system-memory texture pool (DVMT) up the list; see the plan's
2026-10-19 note.

## Not established

- What the reporter saw on screen: no frame image or screenshot was
  uploaded with either report.
- Whether a 16 bpp desktop would have removed the four declines.
- The Carmageddon II re-run owed on the Lenovo is not covered by these.
