# HD 4600: 32-bit textures, and draws and clears that wait for a pending flip

2026-10-20, i5-4570, Windows 98 SE, boots 55 to 58. Builds `0800cb48-dirty`
(the tree after `e065800a`): boot 55 with 32-bit textures, boots 56 to 58
with the flip waits as well. Evidence:
[`../probe/hsw-4570-32bpp-textures-flip-wait-2026-10-20/`](../probe/hsw-4570-32bpp-textures-flip-wait-2026-10-20/).
OBS recordings, not in the tree: `Y:\MWD\videos\obs\2026-10-20 19-51-12.mkv`
(Michael's, before) and `2026-10-20 20-06-17.mkv` (after).

## 32-bit textures

- `v9x_d3d_hsw_texel_format` (`d3d_hsw_map.c`, host-tested; the new checks
  failed first against a stub) maps ARGB8888 to B8G8R8A8_UNORM and
  XRGB8888 to B8G8R8X8_UNORM, beside the three 16-bit layouts.
- `d3d_hsw.c` advertises five texture formats. Mip trees are laid out and
  checked at the texture's own texel size, where 2 bytes was hard-coded.
- The shared block's `texture_formats` grows from three to five
  (ABI 2026102002). Every other engine still lists two or three.
- V9XDDP gains `Tex8A8888` and `Tex8X8888`: 8x8, green left, blue right,
  drawn with COPY. XRGB8888's X byte is 0.

Boot 55: V9XDDP `COMPLETE`, 209 `Ok=1` (207 and the two new), and the
same 17 `Ok=0` as the 0.21 control. Both new textures were created with
pitch 32 and read green, then blue.

3DMark 2001 SE now offers and selects 32-bit textures itself
(`settings-boot55-32bit-textures.png`). The run completed with no engine
timeouts and no refused batch beyond V9XDDP's 8. Score 987, recorded.
3DMark still reports "not run using default settings". What it
is comparing against was not read, so the acceptance in `STATUS.md`
("run at its default 32-bit textures") is met for textures but not for
that message.

Doubling the texels refills the heap: `MipTreeDeclined` 1,160 (last
reason 4, no room), against 0 at 16-bit textures on boot 54 and 7,366
before the heap change.

## The flicker

Michael's capture of the Lobby test at 60 fps (117 frames) shows frames
with part of the scene over black, no character and no 3DMark overlay
(`flicker-before-frame08.png`, `-frame10.png`). 3DMark draws the overlay
last, so each is a back buffer shown before its frame was finished. Two
gaps in the driver allowed it:

- The Haswell draw path never waited for a pending flip
  (`DrawsFlipWaited=0`). Flip returns once DSPASURF is written. The plane
  takes it at the next blank. Direct3D does not ask GetFlipStatus before
  drawing into the buffer the flip released. Gen3 has waited here since
  intel78, and so has the ViRGE; Haswell had not.
- Engine fills and copies (`ddhal_core.c`) only counted a pending flip.
  The boot 54 blit log shows 3DMark's clears landing in the released
  buffer (destination = retiring, `0x00300000`).

The fix:

- `d3d_hsw.c` waits for the flip before building a batch, with Gen3's
  rule: a wait that runs out draws anyway.
- `ddhal_core.c` `v9x_blt_flip_guard` waits before an engine fill or copy
  whose destination is the released buffer while the flip is pending.
  A caller that may not wait gets `DDERR_WASSTILLDRAWING`. This is shared
  code, so it changes every chip's blit path. Only the HD 4600 ran it.
- New counters `BltFlipWaited` and `BltFlipWaitTimeouts`, appended to the
  diagnostics within ABI 2026102002.

Boot 57, the full default benchmark, recorded by OBS:

| | boot 54 (before) | boot 57 (after) |
|---|---|---|
| `FlipStillDrawing` | 9,633,058 | 0 |
| `DrawsFlipWaited` | 0 | 12 |
| `BltFlipWaited` | (not counted) | 3,337 |
| wait timeouts | | 0 |
| engine timeouts, resets, refused batches | 0 | 0 |

`hudscan.py` counts captured frames whose overlay box is missing while
the frames either side have it:

| recording | frames | overlay present | isolated drops |
|---|---|---|---|
| 19:51, before | 117 | 108 | 20 |
| 20:06, after | 32,310 | 29,883 | 0 |

So the waits that mattered were the clears', 3,337 against 12 draws.
Score 873, recorded. Gates: `check-tree.ps1`, `build-host.ps1` and
`run-checks.ps1` passed on this tree.

## Boot 56 hung; not reproduced, parked

On boot 56, V9XDDP ran, and then 3DMark was started. About a minute later
the agent stopped answering and the capture showed no signal, while ping
still answered. Michael power-cycled it. No file on the machine recorded
the hang. V9XDDP's window was still in the taskbar after it had exited,
which it was not on boot 55.

The same build did not hang in three tries:
- boot 57 with 3DMark alone (the full benchmark);
- boot 57 with 3DMark closed, then V9XDDP, then 3DMark;
- a fresh boot 58 with V9XDDP and then 3DMark, as on boot 56.

The third try's screenshots taken during 3DMark's start-up, the one step
of boot 56 not repeated, were not run. Michael's reading is that it was
Windows 98 and not the driver. Parked on that, with nothing measured
either way.

Separately, closing 3DMark from its score dialog with WCLOSE faulted
3DMark itself, an invalid instruction at `0000001B`
(`3dmark-close-fault-boot57.png`). Not looked into.

## Not run

- Gen3, the ViRGE, Matrox and the other engines with the shared blit
  guard. Their builds change with it.
- Half-Life and V9XDDP's own flip checks with the guard on, beyond the
  boot 56 and 58 V9XDDP runs, which read 209 and the same 17.
