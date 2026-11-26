# GeForce 210, vsync off: frames flash black or half black, because an unsynced flip is not held while the old buffer is still scanned

Date: 2026-11-26. Machine: W9QI0DAD (GT218 `10DE:0A65`, Windows 98 SE,
boot 158, the 0.61.0 tree's `nvidia` set), `[Velocity9x] VSync=2` in
SYSTEM.INI. Status: open, cause read from the counters and the source,
not yet confirmed by a change. Deferred to 1.1 (Michael, 2026-11-26).
Vsync on (`VSync=0` or `1`, the default) is not affected.

## What is seen

Michael's OBS capture of 3DMark 2001 SE Game 4, Nature, at
1024x768x32 (8.4 s, 60 frames a second; the recording stays at
`Y:/MWD/videos/obs/2026-11-26 17-52-00.mkv`, not in the repo). Over the
3 s of Nature in it (frames 325-503), 15 of 178 captured frames are
not whole:

- some all black or nearly so (mean luma 18-30 against 88 for a whole
  frame);
- some with the scene drawn from the top down to a line and black
  below it, the line at a different height each time (frames 369 and
  373 in [the sheet](../probe/gt218-vsync-off-flicker-2026-11-26/frames-367-382.jpg)).

They come about every 9 captured frames, at the test's 13 fps. Black
below the line is not tearing: a tear shows two whole frames split at
a line, and neither of the buffers 3DMark presents is black below the
line when presented.

## Counters

`V9XTRACE` snapshot taken after the capture
([V9XSNA-after.ini](../probe/gt218-vsync-off-flicker-2026-11-26/V9XSNA-after.ini)),
against the one taken after the morning's vsync-on Helicopter run on
the same boot
([snapshot-after.ini](../probe/gt218-heli-rerun-2026-11-26/3dmark2000/snapshot-after.ini)):

| | 11:08, after the VSync=1 run | after the capture |
|---|---|---|
| `FlipHandled` | 12,933 | 14,925 |
| `FlipVSyncOverridden` | 0 | 617 |
| `FlipUnsyncedHeld` | | 0 |
| `BltFlipWaited` | 3 | 1,373 |
| `LockFlipWaited` | | 0 |

617 flips were made unsynced and none was held. The last 16 entries of
the Blt-against-flip log are 3DMark's per-frame clears: a colour fill
(op 2) whose destination is the buffer the last flip retired, with no
flip pending, then a depth fill (op 3) at `0x600000`.

## Reading

`V9xHalFlip` holds an unsynced flip pending, as a synced one is, only
where `v9x_scanout_unsynced_base_latches()` says the scanout takes the
new base at the blank (`ddhal_core.c`). That function names the Gen3
plane, the GCN queue and the backends in `v9x_scanout_hw_flip()`
(`i9xx_scanout.c`); NV50 is not among them. So on the GT218 an unsynced
flip leaves nothing pending, the old front buffer goes straight back to
3DMark, and its next clear runs while that buffer is still on screen.

`v9x_nv50_set_display_start` (`eng_nv50.c`) flips through the VGA CRTC
emulation (CR0D, CR0C, CR35, CR34). Its comment says it was not
measured whether the emulation takes the start at the retrace, as VGA
does, or at once. This capture answers it by inference: a clear could
only be seen in the old buffer if the old buffer was still scanned
after the flip was written, which is the latch-at-retrace case. Not
measured directly (no scanout readback was taken).

This is the fault fixed for Gen3 in efd2fa90
([issue](2026-11-01-gma950-3dmark2001se-flicker-refused-vertices.md))
and later for Haswell, on another backend.

## Fix, for 1.1

Hold an NV50 flip until the scanout has taken it: either name NV50 in
`v9x_scanout_unsynced_base_latches()` and release the flip at the next
retrace (3DAh bit 3), or read back the start the head is scanning, if
the emulation offers one. Then the same capture again with `VSync=2`:
every frame whole, `FlipUnsyncedHeld` counting.

Other backends outside `v9x_scanout_hw_flip()` (the RIVA 128, Matrox,
S3 and Mach64) may have the same gap under `VSync=2`; not
looked at.
