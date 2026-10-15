# 3DMark2000 shows "3D pattern corruption" on a reporter's GMA 900

Date: 2026-10-11. Status: closed 2026-10-15. The pattern was Gen3's
polygon stipple left at its power-on value; fixed in 0.18.4, and the
reporter confirmed it on the GMA 900 (GitHub issue 7).

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

## The screenshots: a 4x4 write mask (2026-10-15, GitHub issue 7)

GitHub [issue 7](https://github.com/michaeldale/velocity9x/issues/7),
same reporter and laptop, on 0.18.3 ("problem first discovered in
0.15.0"): "on some instances of Windows 98SE" 3D shows "a pattern on the
image", and Carmageddon II (Glide) and 3DMark2000 also alternate between
the pattern and a "bleed through" image of 2D. Quake II (OpenGL) and
3DMark2001 show the pattern only. Field reports V9X-1QBCF0 (3DMark2000)
and V9X-VRD4N0 (Carmageddon II, Glide), both 0.18.3 `c6af544e`, desktop
1024x768x16 at the 2112 pitch, panel on pipe B with plane B
(`INTELMM.TXT`), carry the same 8 files; their `V9XTRACE.INI` is
byte-identical, a stale application fault, not these runs.

The three screenshots (two 640x480 Carmageddon II, one 1024x768
3DMark2000 over the desktop), decoded pixel by pixel:

- The pattern is a fixed **4x4 mask**, the same in all three. In each
  4x4 cell **6 pixels** carry the current 3D frame and the other 10 hold
  something else. In the 3DMark2000 capture the 10 are exactly `007F7F`,
  Windows 98's default desktop colour; in Carmageddon II they are an
  earlier frame (the title screen over a race HUD).
- By row (y mod 4, measured from the 3DMark2000 capture at x mod 4):
  0 -> {2,3}, 1 -> {2}, 2 -> {1}, 3 -> {1}.

A mask that writes some pixels of every primitive and leaves the rest as
they were is what Gen3's polygon stipple does: `_3DSTATE_STIPPLE`, a 4x4
pattern in ST1 bits 15:0 enabled by ST1 bit 16. It also explains the
"bleed through" (pixels never written keep the desktop or an older frame)
and why every API shows it (Direct3D, Glide and OpenGL all draw through
the same Gen3 state). Nothing else in the Gen3 pipeline masks individual
pixels in a repeating pattern: dither changes values, it does not
preserve old ones, and the colour write mask is per channel.

**This driver never programs stipple.** The state block is the
intersection of Mesa's and xf86's invariant blocks
(`i9xx_3d.c`, `v9x_i9xx_emit_invariant`), and the 2026-09-14 packet
audit left `_3DSTATE_STIPPLE` out with four other packets both drivers
emit outside that block, on the reasoning that this driver, unlike them,
starts from a freshly reset engine. Both emit it as `_3DSTATE_STIPPLE`
then `0` (Mesa `i915_state_dynamic.c`, `upload_STIPPLE`; xf86
`i915_3d.c:106`), so the value is double-sourced. The stipple state is
whatever the part held when the driver started. On the netbook's 945 it
has evidently been off; on this 915 it is on, at least on some boots,
which would fit "some instances of Windows".

Leading hypothesis, not yet measured. The other four packets the audit
left out (`_3DSTATE_MODES_4`, `_3DSTATE_INDEPENDENT_ALPHA_BLEND`,
`_3DSTATE_RASTER_RULES`, `_3DSTATE_BACKFACE_STENCIL_OPS`) are in the same
position and would show different symptoms.

### Reproduced on the netbook (2026-10-15, boot 203)

Build `issue7-stipple-on`: every runtime batch opened with
`_3DSTATE_STIPPLE` and ST1 `0x00012346` (enabled, rows 0010 0011 0100
0110, six bits of sixteen). On MICHAEL-NETBOOK's panel, Carmageddon II
then showed what the reporter's screenshots show; Michael confirmed
"same pattern". The netbook's 945 had never shown it with the stipple
left unprogrammed, so its power-on stipple is off and the 915's is not,
at least on the reporter's boots.

### Fix

Every runtime batch now opens, after its MI_FLUSH, with
`_3DSTATE_STIPPLE` and ST1 zero (`d3d_i9xx.c`), the value Mesa and xf86
both send. The decoder accepts that pair in a runtime stream only, with
ST1 exactly zero, in the HAL build only (`V9X_I9XX_TWO_UNITS`): the
16-bit driver never emits it, and its I9XXCODE segment is nearly full.
`test_decoder_stipple` failed on the decoder before the change (the pair
refused as an unknown packet) and passes after it.

The four other packets the 2026-09-14 audit left out
(`_3DSTATE_MODES_4`, `_3DSTATE_INDEPENDENT_ALPHA_BLEND`,
`_3DSTATE_RASTER_RULES`, `_3DSTATE_BACKFACE_STENCIL_OPS`) are not
changed here: nothing measured implicates them, and each of their enables
sits in state this driver does set (S5/S6, and the IAB disable on every
blended batch).

Build `issue7-fix` on the netbook's panel, boot 204: Carmageddon II and
Half-Life look as before (Michael). That is a regression check only; the
netbook never showed the pattern, so the fix itself is measured only
through the decoder test and the stipple-on reproduction.

### Next

- The reporter's confirmation on the GMA 900.
- Whether the pattern changed between boots of the same install, and how
  the screenshots were captured, are no longer needed to fix it.

## Confirmed on the GMA 900 (2026-10-15)

`interrupt7` on GitHub issue 7, 11:50 UTC, after installing 0.18.4:
"Installed and working for now. I'll report back if the fault
reoccurs." The issue is closed on GitHub. No files came with the
confirmation, so it rests on the reporter's eyes, not on a capture.
