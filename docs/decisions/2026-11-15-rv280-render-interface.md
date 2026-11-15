# RV280 render interface: scissor, write mask, and a 32-byte colour pitch

Date: 2026-11-15

Machine: M-HNBJNV, Radeon 9250 (`1002:5960`), Windows 98 SE, branch
`ati-rv280`, boots 23 to 26. This follows the
[Direct3D HAL](2026-11-15-rv280-d3d-hal.md), which kept the engine out of
the render interface (`render_interface_off`) because the interface's
draws need a scissor and a write mask. Evidence:
- [`../probe/rv280-scissor-mask-probe-2026-11-15/`](../probe/rv280-scissor-mask-probe-2026-11-15/)
- [`../probe/rv280-pitch-probe-2026-11-15/`](../probe/rv280-pitch-probe-2026-11-15/)
- [`../probe/rv280-wide-z-probe-2026-11-15/`](../probe/rv280-wide-z-probe-2026-11-15/)
- `../probe/field-radeon-9250-hnbjnv-2026-11-15/gl-boot23/` to `gl-boot26/`

## Scissor and write mask

Mesa's r200 sets `RE_CNTL`'s `SCISSOR_ENABLE` (bit 1). It writes the
corners into `RE_TOP_LEFT` and `RE_WIDTH_HEIGHT`, inclusive
(`r200_vtbl_update_scissor`, `r200_state.c:1577-1600`). A masked colour
channel is cleared in `RB3D_PLANEMASK`, which takes effect only with
`PLANE_MASK_ENABLE`. The builder does the same; its host checks failed
first.

At 16 bpp the channel's bits are written in both halves of the register,
so either reading of it works. Only the low half was measured, and since
the result was exact, the reading is not settled.

V9XR23D, at 16 and 32 bpp:
- **Scissor:** a red quad over every column of rows 0-15, scissored to
  columns 12-39 and rows 2-5. Exact.
- **MaskGreen and MaskRedBlue:** a white quad with those channels masked,
  and the X byte kept at 32 bpp. Exact. Pixel 20 read `FDBF` and `A7E5`
  at 16 bpp over the `A5A5` prefill, and `A5FFA5FF` and `A5A5FFA5` at
  32 bpp.

The map now accepts the interface's explicit draws with their scissor and
write mask, as the RS690's does. `render_interface_off` is removed, since
no engine needs it now.

## First OpenGL run (boot 23): sheared drawing

Our ICD drew on the R200 (`Renderer=Velocity9x Radeon 9250`). Results:
- V9XGLIDP: 0 failures. V9XD32: PASS at both depths.
- V9XGLP:
  - TexPhase's 16x16 quad read texels 8 to F over half its columns, and
    every other row;
  - TnL drew 466 pixels outside its viewport;
  - GeomScan had a black band.

The HAL's counters showed no refusals. A build with the scissor off read
the same (boot 24).

The OpenGL back buffer is 360 pixels wide, so its rows are 720 bytes.
V9XR23D's new Pitch checks draw a quad into the test surface with its
pitch widened:

| Pitch | 16 bpp | 32 bpp |
|---|---|---|
| +8 pixels | 144 bytes: 96 wrong, from row 1 | 288 bytes: exact |
| +32 pixels | 192 bytes: exact | 384 bytes: exact |

`RB3D_COLORPITCH`'s unit is 8 pixels, but the pitch must also be a
multiple of 32 bytes. Mesa aligns R200 colour pitches to 64 bytes, and
every target measured before (640 wide, or the probe's 64) met that by
chance.

Changes:
- The builder refuses any other pitch (`V9X_RV280_COLOR_PITCH_ALIGN`), so
  such a target is refused, not drawn sheared. The host check failed
  first.
- The engine reports a 32-byte `target_pitch_align`.
- The engine places offscreen render targets with their rows rounded to
  32 bytes, as `d3d_eg.c` places its own for the same 360-pixel buffer.

## Boot 25 and 26

- **V9XD32:** `Failures=0`, PASS at 16 and 32 bpp.
- **V9XGLIDP:** `Result=PASS failures=0`.
- **V9XR3DP:** `COMPLETE`, with scissor and mask OK.
  `TexturedScissorOk=0` with result 4, as on the Carrizo. `RightDepthRaw`
  is read linearly from a tiled buffer.
- **V9XGLP:** `Result=PASS`.
  - TexPhase reads `0123456789ABCDEF` at every offset.
  - `TnlResult=match`.
  - CVA, offset and fog pass.
  - The mipmap checks read level 0, since only level 0 is sampled.
  - `LitResult=differ`, as on every card.

GeomScan's band stayed. Z buffers were moved to 4 KiB alignment, as the X
driver places them (`radeon_accel.c:1303`), and the band did not move.
V9XR23D's WideZ checks then drew the same shape exactly, with a 360-pixel
target over a 384-pixel-pitch Z buffer set by the CPU or by the 2D
engine. The band is open in
[`../issues/2026-11-15-rv280-gl-geomscan-band.md`](../issues/2026-11-15-rv280-gl-geomscan-band.md).

## Not checked

- Games, Quake 2 and Half-Life included.
- Partial depth clears (glScissor with a depth clear), which the tiled
  depth fill declines.
- Glide beyond V9XGLIDP.
- Placed render targets are not freed when destroyed
  ([issue](../issues/2026-11-15-placed-render-targets-never-freed.md)).
