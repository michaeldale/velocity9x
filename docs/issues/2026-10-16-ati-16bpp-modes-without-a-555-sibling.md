# ATI: 16 bpp modes with no 15 bpp sibling stay 5:6:5, and ATI titles draw them as 5:5:5

Date: 2026-10-16. Status: open. Measured on A8U4I5's Rage XL (boot 482);
no fix designed.

## What was seen

The ATI family chooses 5:5:5 under hardware Direct3D by switching each
16 bpp VBE mode to its 15 bpp sibling (`modes16.c`,
`v9x_modes16_row_is_555`). A row with no sibling stays 5:6:5 and is
published as 5:6:5, so DirectDraw's masks are honest. ATI titles ignore
them:

- **Wipeout for 3D Rage** runs its title at 512x384. The render
  interface describes a 565 target, the DLL logs "the display is 5:6:5",
  and the backdrop the game writes with the CPU shows as green and purple
  noise (`docs/probe/a8u4i5-rage-xl-wipeout-2026-10-16/title-512x384-565.png`).
- **Assault Rigs for 3D Rage** runs its menus and play at 400x300, also
  5:6:5. Its 3D draws correctly (the DLL converts to the target). Michael
  saw an in-game panel drawn as flat bars with nothing in them; the game
  writes a 2D overlay with the CPU (63,814 surface locks in one session,
  no keyed blits reaching the HAL). Whether the overlay's loss comes from
  the layout is not established.
- The 640x480 screens of both games, and Formula 1's race, ran at 5:5:5
  and looked right.

ATI's own driver programs the CRTC itself and runs every 16 bpp mode as
5:5:5, so these titles never met a 5:6:5 mode there.

## What is not known

- Which of the Rage XL's 16 bpp modes lack a 15 bpp sibling; at least
  400x300 and 512x384.
- Whether the Mach64 CRTC can scan a VBE 16 bpp mode out as 5:5:5 by its
  pixel-width field after the mode set, and what the BIOS's 400x300 and
  512x384 modes are (VBE numbers, timing). Nothing measured.
- The tester's report (`2026-10-16-3dcif-games-on-a-testers-rage-pro.md`)
  of DirectDraw describing 5:6:5 while the screen showed 5:5:5 at 640x480
  is a different case and still open.

## Next

Read the Rage XL's VBE mode list for 15 and 16 bpp rows, then measure
whether a 16 bpp mode can be switched to 5:5:5 in the CRTC alone.
