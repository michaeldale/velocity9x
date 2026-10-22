# Haswell, a tester's HD 4600 (report V9X-WM1C1K): Tux Racer draws untextured, Quake 3 stutters, Re-Volt tyres glitch

Date: 2026-10-23. Reported on 0.25.0 (`e318f4ed`) by the tester of
V9X-D9VZS6 and V9X-D9VC83: "quake 3 arena now runs but has massive
stutters and mouse can be unresponsive, tuxracer regressed and doesn't
render the menu, revolt has gliched graphics on the car tires". Their
desktop is 1024x768x32. Status: Tux Racer and the Quake 3 rate are
reproduced on the i5-4570 (W9QI5A83, boot 64). Tux Racer's cause is found
and fixed in the ICD and seen on the 4570 (below). The Quake 3 rate's
cause is not established. Re-Volt is not run. Evidence:
[`../probe/hsw-tux-quake3-baseline-2026-10-23/`](../probe/hsw-tux-quake3-baseline-2026-10-23/).

The 4570 ran the dev set it had (`9d4c7012-dirty`, ICD `8aee07c0`), not
the 0.25.0 package. Its desktop was 800x600x16, not the tester's 32 bpp.

## Tux Racer 0.61a

`tuxracer-win32-0.61a.zip` from the project's SourceForge files
(8,009,319 bytes, SHA-256 `1497e326...98823c34`), the build the tester
named, unpacked to `C:\TUXRACER\tuxracer-win32-0.61a`. Its defaults:
full screen 640x480 at the desktop depth.

**Why it "regressed".** On 0.22.0 the ICD offered no pixel format at
32 bpp, so Tux never made a context on it (`slots-called=0` in the
tester's earlier log) and ran on Microsoft's software renderer. 0.25.0
offers 32 bpp (`9d4c7012`), so Tux now uses the ICD.

**Reproduced at 16 bpp.** The 4570's log reads as the tester's: 339
`glTexImage2D` calls, textures bound (names in the 20s to 50s), no hardware texture
made (`hwtex creates=0`), and every batch untextured with `hwno
not-cpu`. The state lines carry storage NONE and size 0, so
`v9x_gl_tex_describe_unit` found the bound object absent or incomplete.
The desktop depth is not the trigger.

| Screen | Ours | GDI Generic (`V9XGL.DLL` renamed aside) |
|---|---|---|
| Splash | flat blue, untextured snow squares, no logo or text (`tux-ours-splash.png`) | logo, "Press any key to start", textured snow (`tux-gdi-generic-splash.png`) |
| Main menu | white blocks for the frame and each label (`tux-ours-main-menu.png`) | frame, logo and four labels (`tux-gdi-generic-main-menu.png`) |

Tux loads every texture the same way (`src/textures.c`): `glGenTextures`,
`glBindTexture`, `glPixelStorei(GL_UNPACK_ALIGNMENT, 4)`,
`glTexParameterf` for wrap (REPEAT or CLAMP) and both filters, a
`GL_MAX_TEXTURE_SIZE` check, then `gluBuild2DMipmaps` with 3 or 4
components, RGB or RGBA, `GL_UNSIGNED_BYTE`. Each of those reads as
accepted in `gl_texture.c`, and the pixel-store queries Win98's GLU makes
are answered in `gl_get.c`. Which check rejects the upload, or leaves the
chain incomplete, is not established.

**Cause: GLU uploads in UNSIGNED_SHORT.** The 4570's
`C:\WINDOWS\SYSTEM\GLU32.DLL` (140,224 bytes, CRC32 9D7DF9E4) imports
`glTexImage2D` once. Its single call site, at 7D1079A5, follows the
`gluBuild2DMipmaps` export (the nearest export below it; function
bounds were not traced) and pushes `0x1403` (`GL_UNSIGNED_SHORT`): the
only type this GLU ever uploads in. The ICD took
`GL_UNSIGNED_BYTE` only and set `GL_INVALID_ENUM` for anything else, so
each level was dropped and no Tux texture was ever complete. Quake 3 and
the other games run so far build their own levels in bytes. The scan was
a PE import walk over the DLL (call `[IAT]`, then the 32-bit pushes
before it), not a disassembly of the whole function.

**Fix.** `glTexImage2D` and `glTexSubImage2D` take `GL_UNSIGNED_SHORT`:
each component c is c / 65535 (3.6.4), rounded to a byte, then packed as
before. `UNPACK_SWAP_BYTES` is now stored, honoured for 16-bit
components and returned by `glGet`; it was accepted and ignored. The
other transfer types stay refused. Host test first
(`test_unsigned_short_uploads`, seen failing at 8 checks); the swap and
query checks were written after the fix and not seen failing.
`build-host` and `run-checks` pass.

**On the 4570**, the ICD alone replaced (`b09a5d77-dirty`; the previous
one kept as `C:\WINDOWS\SYSTEM\V9XGL.A8E`): the splash and the main
menu draw as the GDI Generic references do (`tux-fixed-splash.png`,
`tux-fixed-main-menu.png`). The log: `untextured=0/0`,
`hw=12913/558334`, `not-cpu=0`, 12 textures made, none refused, after
about 30 seconds. Tux quit from its own menu. A race was not run, and
lighting and display lists are still stubs. Quake 3's byte uploads now
pass through the same fetch: `demo001` at 800x600 read 47.2 fps on the
fixed ICD (`quake3-benchfix-800.txt`), one run, against 46.4 and 46.7
before.

Also seen: Tux calls `glLight*`, `glMaterial*`, `glTexGen*` and the
display-list entry points, which are stubs. It draws its own cursor from
relative mouse motion, so the agent's `move` does not steer it; use
`moverel`. Keys sent by the agent did not reach it. It ignores WM_CLOSE.
Under the ICD it was ended with `TerminateProcess` and the desktop reset
with `V9XMSW /set:800x600x16`.

## Quake 3 demo 1.11, `timedemo 1`, `demo demo001`

Michael's settings: 32-bit colour and textures, 24-bit Z, multitexture
on, picmip 1, trilinear, `com_maxfps 85`. One process, runs back to back.

| Mode | Result |
|---|---|
| 800x600 | 1346 frames, 28.8 s: 46.7 fps (an earlier run in the process) |
| 800x600 | 1346 frames, 29.0 s: 46.4 fps |
| 1024x768 | 1346 frames, 37.1 s: 36.3 fps |

The ICD's 10-second lines put 66-74 % of wall time in `sink flush-ms`, at
the menu (about 97 swaps a second, 230 µs a flush) and in the demo at
1024x768 (150-230 µs a flush). The tester's log has the same share at
1024x768. GL never takes DirectDraw exclusive mode, so
`v9x_present_gdi_may_draw()` is always true for it, and
`v9x_d3d_hsw_draw_waits` makes every Haswell draw from the ICD wait for
the render engine. That fits the rate. Whether it causes the stutters or
the mouse lag is not measured: a timedemo does not show either.

Quake 3 reports `color(16-bits) Z(24-bit)` after asking for 32/24 at
800x600 and 1024x768, with the screen at 32 bpp. The ICD described its
formats at attach, when the desktop was 16 bpp (`formats=00000002`).
Whether it then drew a 16-bit format into a 32 bpp mode is not checked.

## Not run

- Re-Volt. The tester's counters explain nothing (no refusals or state
  drops; MipTreeDeclined and MipChainGaps now 0). A screenshot is owed.
- Tux and Quake 3 on a 32 bpp desktop, and on the 0.25.0 package.
