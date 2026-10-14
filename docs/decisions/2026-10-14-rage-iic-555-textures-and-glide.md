# The Rage IIC at 5:5:5: untagged textures draw, and Glide opens and converts the LFB

Date: 2026-10-14

Status: Measured on A8U4I5 (Rage IIC AGP), boot 464, default
`ColourLayout=555-auto`. Phase 1 (textures) and Phase 2 (Glide) of
[ati-555-desktop.md](../plans/ati-555-desktop.md) on the Rage II. Evidence:
`docs/probe/a8u4i5-rage-iic-555-2026-10-14/boot464-*`.

## Untagged textures

A texture created with no pixel format takes the display's, which on a
5:5:5 desktop is XRGB1555. Before this change the Rage II classifier
refused it (`d3d_rage2.c` matched 565, ARGB1555 and ARGB4444 only). That is
from reading the code: no title or probe drew one, and the refusal was
never measured.

- **The change.** `V9X_M64_TEXTURE_FORMAT_XRGB1555` (3). The Rage II
  samples it as hardware format 1555 without TEX_MAP_AEN or the alpha
  mask, so its top bit is never read as alpha. DECAL tests its alpha as
  255. The Mach64 policy refuses format 3. `test_rage2_draw.c` gained five
  checks first, and all five failed before the fix.
- **The probe.** V9XDDP's new `D3DUntaggedTexture` key creates a 64x64
  texture with no pixel format and fills it with `0x03E0`, top bit clear.
  It draws the texture MODULATE under SRCALPHA/INVSRCALPHA with opaque
  white vertices. A driver that read the top bit as alpha would leave the
  black background.
- **Result.** `D3DUntaggedTextureRaw=992` (`0x03E0`),
  `D3DUntaggedTextureOk=1`.
- **Regression check.** The other 221 keys match boot 461's 555 run (152
  pass); no key moved.
- **Answered:** Phase 0's open question, for this case. The Rage II's 1555
  format with TEX_MAP_AEN off ignores the top bit.

## Glide

Glide opens on whichever 16-bit layout the display has. `glide_surface.c`
reads the primary surface's format and requires the matching target bit.
- **Triangles** already follow the surface's own format through the render
  interface.
- **LFB on a 5:5:5 buffer:**
  - A write lock in 555, 1555 or "any" gets the buffer itself; "any"
    reports 555.
  - A read lock, or a write lock in 565, gets the shadow. The shadow is
    filled from the buffer converted to 565, and converted back at unlock.
    555 survives the round trip; a host test checks all 32,768 values.
  - grLfbReadRegion and grLfbWriteRegion convert row by row.

The measurements:
- **Diablo II demo (Glide 3)** opened at 640x480 and 800x600 with
  `device: target 555=1` and reached the town: 131,168 triangles drawn,
  1,284 refused (1%). Every logged refusal is `storage/format=0203`, the
  same class as the Rage XL's translucent keyed sprites. It made no LFB
  calls, so it does not exercise the conversion.
- **NFS II SE 3Dfx demo (Glide 2)** opened at 555 and made two write-only
  565 locks of the front and back buffers (`type=01 wmode=00`). Both went
  through the convert path, and each lock-to-unlock took about 230 ms. The
  race then drew 380,175 triangles with 39,505 refused (9.4%).
  - The last run of this demo in the log, 0.15.0 on 2026-10-11, refused
    5.2%. That run was probably on the Rage XL, so the two are not a
    like-for-like comparison.
  - The refusals are `storage/format=0103` (23 logged) and `0203` (1).

Michael checked the colours by eye on the monitor (2026-10-14):
- Diablo II "looks good". It makes no LFB calls.
- The NFS II demo "looks ok". Its two LFB writes go through the 565 → 555
  conversion.

OBS was not connected, and the agent's screenshots cannot see flipped
frames.

## Not changed, and why

- **The 2D engine's DP_PIX_WIDTH (code 4) and DP_CHAIN_MASK (`0x8410`).**
  Fill and copy use no ROP and no colour compare, so the width code cannot
  change the bits they write. The desktop drew correctly at 555 (boots
  460–464). The chain mask is used by nothing these builders emit.
  Revisit if a ROP or colour-compare path is added.
- **The blend step.** BlendModulate at 555 is one step low (boot 461),
  and ATI's driver runs SCALE_3D_CNTL with its low nibble at `F`. The tree
  defines none of those bits for the Rage II. The next step is a sweep of
  that nibble in the ATIRX scene tool, and nothing is guessed until then.
- **3DCIF on a 5:6:5 desktop.** It still draws, but colours the title
  writes as raw pixels come out wrong. ATI3DCIF.DLL now logs once that the
  display is 5:6:5 and names `HighColor`, for bug reports.

Gates: build-host, run-checks.
