# 3DMark2000 shows "3D pattern corruption" on a reporter's GMA 900

Date: 2026-10-11. Status: open (the corruption has not been seen; the
reporter's screenshot is promised on GitHub).

Source: field report V9X-WPVJHN, sent 2026-10-11 00:18 server time
through `V9XUPD.EXE /REPORT` 0.15.0. "3D pattern corruption. In this
exaple i ran 3DMark2000, will provide screenshot in GitHub". No contact
address.

Machine: the same reporter and laptop as
[the Carmageddon II reports](2026-10-10-carmageddon2-exits-after-intro-gma900.md)
- Intel GMA 900 (915GM) `8086:2592` SUBSYS `17AA:2062`, Windows 98 SE,
512 MB, desktop 1024x768x16, VBE-reported VRAM 8,060,928 bytes,
Velocity9x 0.15.0 `11d348f`. `V9XBOOT.INI` and `V9XGL.LOG` are
byte-identical to that report's (same SHA-256); the GL log is the stale
Quake 2 session described there.

## What the snapshot shows (V9XSNA6.INI)

One boot (`BootId` 2005-01-09 00:28:41.63 on the guest clock), two
programs: `3DMARK2001SE.EX` (2 D3D contexts) then `3DMARK2000.EXE`
(3 contexts). No fault capture; the ring ends in an ordinary teardown.

- `D3dTextureCreates` / `Destroys` 31,304 / 31,304; `I9xxDrawsSubmitted`
  169,626; `Dp2Calls` 9,570 (3DMark2001 SE's DrawPrimitives2 path).
- `I9xxDrawsRefused` 804, `I9xxRefuseLast` 6
  (`V9X_I9XX_REFUSE_VERTICES`), `BatchesEngineRefused` 804.
- `MipTreeAllocs` 28,431, `MipTreeDeclined` 21,121,
  `MipTreeDeclinedLast` 4 (no free heap block), `MipDraws` 158,037,
  `MipLevelsMax` 9.
- `DrawsNoHandle` 680: `NoHandleBlendOff` 469, `NoHandleBlendModulate`
  131, `NoHandleBlendOther` 80, `NoHandleWithUv` 187,
  `NoHandleLastColor` 0x0000FF00.
- Draw targets at 0x000000, 0x0EA600, 0x18C000 and 0x318000; the four
  frame-cover samples (1024x768, buffers 0x18C000 and 0x318000) all read
  `Drawn=0`.
- The ring is the 0.15.0 one, before repeat folding: 36 entries of
  `Flip exit 0x8876021C` (DDERR_WASSTILLDRAWING), then the teardown. It
  says nothing about the scenes.

## Candidates, none confirmed

1. **Mip levels.** 21,121 mip-tree placements were declined for want of
   room against 28,431 made. At 1024x768x16 with triple buffering and Z
   the display takes about 6 MB of the 8 MB. A decline is not a failed
   texture - DirectDraw then allocates the chain one level at a time - and
   whether those textures are sampled with their mip levels is not
   measured
   ([2026-09-25 issue](2026-09-25-gen3-video-memory-heap-may-fragment.md)).
   If they are not, distant textures would shimmer in a moire pattern,
   which fits the words "pattern corruption". Not shown.
2. **Untextured draws.** 680 draws carried no texture handle, 187 of them
   with texture coordinates; they are drawn in vertex colour (the last
   one pure green). A surface meant to be textured coming out flat would
   read as corruption.
3. **Refused geometry.** 804 batches refused at reason 6, the same
   refusal as Carmageddon II's Glide black screen, here at 0.5 %. That
   shows as missing triangles rather than a pattern.

## Next

- The reporter's screenshot, and which 3DMark2000 test shows it.
- If it is moire on distant textures: 3DMark2000 on MICHAEL-NETBOOK
  (945GSE, the same Gen3 engine) at 1024x768x16, then at 640x480x16 to
  free video memory, comparing `MipTreeDeclined` and the image.
- A report from the current build: its trace ring folds repeats, and it
  sends the Glide logs.
