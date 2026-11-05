# Ivy Bridge mobile (HD 4000, 8086:0166) on M-ZJQ2DX

Status: **steps 1-4 done 2026-11-05** on branch `intel-ivb-mobile`: the
alias runs 2D, flips and Direct3D on the LVDS laptop. Not merged; nobody
has looked at the panel yet.

## Machine

W9QI75E0, Tom Rossell's Lenovo (SUBSYS `17AA:5001`), remote help
`M-ZJQ2DX`. i7-3632QM, IGD `8086:0166` rev 09, an Optimus GF108M that
Windows cannot start, a 1366x768 LVDS panel (LGD033B), HDMI on port B and
a VGA port. Audit: `docs\decisions\2026-11-05-w9qi75e0-ivb-mobile-audit.md`.

The owner works on HDA audio drivers on this machine. Remote help on is
consent to install and restart; say so in the chat before a series.

## Why it is not just an id

`0166` is the same GT2 silicon as `0162`, and the audit's config decode
matches W9QI4E14's. But the Haswell/IVB path has only ever scanned out to a
CRT through FDI and the PCH. There is no LVDS, panel power or backlight
code in `src\chipsets\intel\hsw`. The driver sets modes through VBE, so the
BIOS owns the panel; what is new is whatever our own code does to the
display: flips through plane A, which `hsw_scanout.c` takes only while
pipe A and plane A are live.

## Order

1. Tier-0 `vbe` baseline: modes, EDID through 4F15h, the panel at
   1366x768. Shows what VBE gives before our code runs.
2. Claim `0166` as an alias of `hd4000-ivb` (the `041E` pattern): its own
   `V9X_HW16_DEVICE`, the Ivy Bridge open and forcewake, the same caps.
   Install, boot, `V9XDDF`/`V9XDDP`.
3. Read-only MMIO: which pipe the LVDS is on, PP_STATUS/PP_CONTROL, the
   backlight PWM, before and after a mode change.
4. Flips and Direct3D: `V9XDDP`, `V9XD32` at 16 and 32 bpp.
5. If any step turns the panel off, decide then whether panel power code is
   needed. Not before.

## Records

- Step 1, 2026-11-05: `docs\decisions\2026-11-05-w9qi75e0-ivb-mobile-tier0.md`.
  `enable-ok`, EDID 1366x768, 1366x768x16 sets live.
- Steps 2 to 4, 2026-11-05:
  `docs\decisions\2026-11-05-w9qi75e0-ivb-mobile-intel-gma.md`. The LVDS is
  on pipe A behind panel fitter 0; flips (27 of 27 taken) and `V9XD32` at
  16 and 32 bpp, Z16 and Z24, pass with no code beyond the claim.

- OpenGL and Glide, 2026-11-05:
  `docs\decisions\2026-11-05-w9qi75e0-ivb-mobile-gl-glide.md`. W9QI4E14's
  results, unchanged.
- 3DMark 2000, 2026-11-05: **hard lock in Game 1**, one run
  (`docs\issues\2026-11-05-ivb-mobile-hang-in-3dmark2000-game1.md`).

## Next

- The 3DMark 2000 hang blocks the merge. The likeliest cause is Haswell
  work since 21 October that no Ivy Bridge has run; the issue gives the
  bisect order.
- The owner's answer on what the panel shows.
- A DOS box round trip needs the owner at the keyboard.
