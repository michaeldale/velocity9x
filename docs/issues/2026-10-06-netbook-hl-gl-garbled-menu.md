# Half-Life (OpenGL) drew a garbled menu on the netbook, once

**Status: PARKED. Not reproduced.** Seen once by the operator on
2026-10-06. Three attempts to reproduce it failed, and on the operator's
next look it was working again.

**Machine.** MICHAEL-NETBOOK (945GSE / GMA 950), agent at
10.0.1.254:9869, boot 101. OpenGL ICD `676f700-dirty`: SGIS multitexture,
partial uploads, texture state that does not flush. The V9XTRACE identity
work (3ece106) was not deployed there. Evidence:
[docs/probe/netbook-hl-gl-garbled-menu-2026-10-06](../probe/netbook-hl-gl-garbled-menu-2026-10-06/).

## What was seen

Half-Life `-gl` at 640x480x16, on the Multiplayer menu
(`panel-photo-boot101.jpg`):
- The background tiles were noise.
- The menu labels were missing.
- Each description line appeared several times, shifted sideways.
- Horizontal streaks of texture colour ran across the screen.

## What the evidence says

- **In video memory, not scanout.** The agent's GDI screenshot of the front
  buffer (`framebuffer-boot101.png`) shows the same picture as the panel.
  The live display registers in `boot101-V9XSNA7.INI` were correct:
  plane B on, 640x480, stride 1280, base 0, panel fitter on. This is not
  the 2026-09-24 VGA-plane fault, which showed a text-mode plane instead
  of the picture.
- **The ICD saw nothing wrong** (`boot101-V9XGL-session.LOG`). Over 24
  minutes it drew a steady ~23 fps and 4.25M hardware batches (2.99M of
  them two-unit), with no refusals and no failed creates. The same 60
  texture copies were used throughout, with no uploads after the first.
  Whatever changed, the ICD did not write it through its upload path.
- **Nothing else ran that boot.** No Direct3D context, and no flips.
  `EnableCount`, `CountDriverInit` and `CountSetExclusiveMode` match a
  clean boot (`boot102-V9XSNA7.INI`). The drain stall ratio is the same on
  both boots, so it is not a symptom.
- **One oddity.** The session's first interval was 245 s with no frame
  presented: Half-Life sat on its loading screen about four minutes. It
  normally reaches the menu in about 45 s. Not explained.

A reading, not a finding: the background tiles and font look sampled from
texture memory that something else had overwritten, and the duplicated
text looks like screen rows in that memory. Nothing measured shows what
did the writing.

## Not reproduced (boot 102, same ICD)

- Fresh launch → menu → `timedemo mwd5` → console → menu.
- New Game → Easy → `c0a0` → Esc → Multiplayer: the operator's path
  (`framebuffer-boot102-multiplayer.png`).
- Alt+Tab out, with the NETGEAR WG111v3 wizard over the game, and back.

All drew correctly.

## If it comes back

Run `C:\V9XTRACE.EXE` while the garbled picture is still on screen,
before quitting Half-Life. Note what happened since boot: idle time, lid,
screensaver, pop-ups, video settings. Then fetch `C:\V9XDIAG\V9XGL.LOG` and
the newest snapshot. A build with 3ece106 on the netbook would also name
the programs and builds in the snapshot.
