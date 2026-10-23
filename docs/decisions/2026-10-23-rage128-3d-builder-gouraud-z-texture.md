# Rage 128 3D builder: Gouraud, Z16 and a texture draw right through the CCE

Date: 2026-10-23

Status: **measured, one card, with an open fault.** A8U4I5's Rage Fury Pro
(`1002:5046`), boots 524 and 525, the ati package from `9f992739`
installed, the CCE in PIO mode with linux-firmware's microcode (see the
[CCE record](2026-10-22-rage128-cce-pio-runs-packets-and-a-triangle.md)).
Step 1 of Direct3D in [ati-rage128.md](../plans/ati-rage128.md). Evidence:
[`../probe/a8u4i5-rage128-cce-pio-2026-10-22/`](../probe/a8u4i5-rage128-cce-pio-2026-10-22/),
`R128CCE-builder-boot525.TXT` and `V9XR1MM-boot525-after-cf9-and-cce.INI`.

## The builder

`src\chipsets\ati\rage128\r128_3d.c` (`include\velocity9x\r128_3d.h`)
turns a state description into CCE packets. It follows Mesa 7.11's r128
`r128DDInitState` and `r128_texstate.c`, and the Linux DRM's
`r128_emit_context`. In order, the stream holds:

1. a GUI pixel-cache flush with read-invalidate;
2. the 12-register context block (destination, master control, the 3D
   scissor, Z offset, pitch and control, `TEX_CNTL_C`, blend and alpha
   test, fog colour);
3. `SETUP_CNTL`, `PM4_VC_FPU_SETUP`, `SCALE_3D_CNTL` and
   `WINDOW_XY_OFFSET`;
4. the constant and border colours, stencil reference and plane mask;
5. when textured, the 14-register texture block (control, combine,
   size/pitch and all eleven level offsets).

A separate call builds a `3D_RNDR_GEN_PRIM` triangle-list header, and
another the batch-ending cache flush. The vertex is all eight dwords with
every TL format bit (`C9h`): X, Y, Z, RHW, diffuse ARGB, specular with
fog in its alpha byte, S, T. That is `V9X_R3D_VERTEX` dword for dword, so
the HAL can write its vertices as they are. `tests\host\test_r128_3d.c`
holds the encodings. Its expectations were worked out by hand from
`r128_reg.h`; two of them were wrong and failed until corrected. The
builder was right both times.

## What the probe drew (boot 525, after a CF9h reset)

The probe (`tools\diag\r128_cce_win32.c`) uses the builder's streams on a
64x32 16-bpp target at VRAM 16 MiB.

| Test | Result |
|---|---|
| Z: Z cleared to FFFFh, a blue rectangle at z 0.25, then a red one over everything at z 0.5, Z16 LESS with writes | **PASS**: blue survives inside, red outside. The Z buffer, read back untiled, holds **4000h** inside and **8000h** outside: z x 65535, linear, no `Z_TILE` |
| Texture: 16x16 RGB565, every texel distinct, on a 16x16 rectangle at (8,8), nearest, replace, clamped | **PASS**: 256 of 256 pixels equal their texel, no shift |
| Red, green, blue, red, each a flat triangle with the full vertex, one after another | Each its own colour |
| Red, green and blue corners, full vertex | Gouraud: `D842` near red, `0EE2` near green, `105B` near blue, `6249` mid |

Every run left the CCE stopped and the engine reset, and A8U4I5 answered
throughout.

## The fault this does not explain: one draw late, in boot 524

In boot 524 every Gouraud and flat triangle test, whatever the vertex
format, read back the image the **previous** test should have drawn.
Before each stream the target read 0, as prefilled. The Z and texture
tests passed there too: their large rectangles may push the stale lines
out, but that is not established. Tried in boot 524 without effect: a GUI
pixel-cache flush packet at the end of the stream, a second state block
after the primitive, and a degenerate triangle after it. Setting
`SETUP_CNTL`'s `DONT_START_TRIANGLE` jammed the CCE (no idle, and the
engine reset did not recover it). That needed a CF9h reset, which began
boot 525.

In boot 525 the same tests drew correctly, with and without an extra
CPU-side `PC_GUI_CTLSTAT` flush (`/noflush` compared both on one boot).
The flush was not the fix.

Boot 524 started about eight hours after boot 523's last agent traffic,
with no restart command from this side. So it was most likely a power-on,
where boot 525 came from a CF9h reset. A power-on and a reset may leave
some engine state different. The pixel-cache mode registers no open driver
writes are the first suspects: `PC_GUI_MODE` and `PC_NGUI_MODE` read
`124h` and `PC_MISC_CNTL` `4000h` in boot 525, now in V9XR1MM. Not
established. **Owed: a capture and this probe after a real power-on**,
which needs Michael at the machine. The Direct3D engine must not be
trusted until that has run.

Gates: `run-checks.ps1` passed (host tests included).
