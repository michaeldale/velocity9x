# Changelog

All notable Velocity9x changes are recorded here. The project uses semantic
version numbers for product milestones; diagnostic builds retain a separate
build identifier so exact guest-tested binaries remain traceable.

## 0.26.1 - 2026-10-23

- **`vbe` package: a framebuffer mapping Windows refuses is retried
  smaller.** On a reporter's HD 3000 laptop the driver stopped at
  `fail-hardware-dpmi-map`: Windows refused to map the package's 16 MB
  framebuffer window, although no mode there needs more than about 4 MB.
  The package now asks again for 8 MB, then for what its largest mode
  needs, and shrinks the DirectDraw memory to match. The other packages
  are unchanged; their mappings cover chip registers and are not shrunk
  ([issue record](docs/issues/2026-10-22-github-13-hd3000-vbe-dpmi-map.md),
  [GitHub issue 13](https://github.com/michaeldale/velocity9x/issues/13)).
  Not tested on any machine: whether the smaller mapping is granted on
  that laptop is unknown.
- **`V9XBOOT.INI` records why a framebuffer mapping failed.** A new
  `DpmiMap=` line gives each size asked for and Windows' error code, or
  `ok`, on every package.

## 0.26.0 - 2026-10-23

- **OpenGL: textures built by `gluBuild2DMipmaps` now draw.** Windows
  98's GLU uploads every mip level it builds as `GL_UNSIGNED_SHORT`, which
  the OpenGL driver refused, so no such texture was ever complete. Tux
  Racer drew its splash and menus as flat colour and white blocks on every
  card, at 16 bpp all along and at 32 bpp since 0.25.0 let it use the
  driver there (before, a 32 bpp desktop gave it Microsoft's software
  OpenGL). Uploads now take `GL_UNSIGNED_SHORT`,
  and `GL_UNPACK_SWAP_BYTES` is honoured. Tux Racer's splash and menu
  match Microsoft's renderer on the HD 4600 and on the software renderer;
  V9XGLP reads as before
  ([issue record](docs/issues/2026-10-23-haswell-tux-racer-untextured-and-quake3-gl-sync.md)).
  A race was not run, and lighting and display lists are still not
  implemented.
- **HD 4600: OpenGL draws no longer wait for the GPU one by one.** Every
  draw outside full-screen exclusive mode waited for the render engine to
  finish it, which only draws onto the visible desktop need; OpenGL never
  uses exclusive mode. Now only draws onto the desktop wait. Quake 3
  `demo001` at 1024x768 x 32: 35.7 to **42.6 fps**, and frames slower
  than 33 ms halved
  ([record](docs/decisions/2026-10-23-haswell-gl-draws-run-on.md)).
  Windowed Direct3D benefits the same way; Half-Life's windowed Direct3D
  was checked by eye. The HD 4000 shares the code and has not run it.
  The GPU's own cost per draw is now the limit (a
  [parked plan](docs/plans/intel-haswell-batch-cost.md)).
- **ATI Rage 128 Pro (`1002:5046`): installs, and DirectDraw fills and
  copies run on its 2D engine.** The ati package now installs on the
  Rage 128 Pro "PF" (Rage Fury Pro / Xpert 2000 Pro), with BIOS modes up
  to 1280x1024 at 8, 16 and 32 bpp
  ([record](docs/decisions/2026-10-22-rage128-ati-baseline-and-tier0.md)).
  - DirectDraw fills and copies, including overlapping copies, are drawn
    by the chip's 2D engine
    ([record](docs/decisions/2026-10-22-rage128-directdraw-on-the-2d-engine.md)).
  - GDI and Direct3D are drawn by the CPU for now.
  - Measured on one card, 32 MiB AGP, of which 16 MiB is used for now.

## 0.25.0 - 2026-10-22

- **OpenGL: `GL_ARB_multitexture`, and Quake 3 draws its lightmaps in one
  pass.** Wherever the driver offered `GL_SGIS_multitexture` (the
  software renderer, the Intel GMA 900/950/3150, GMA X3100, Ironlake,
  HD 4000 and HD 4600, and the ATI Rage Pro), it now offers the ARB
  extension beside it: `glActiveTextureARB`, `glClientActiveTextureARB`,
  the 32 `glMultiTexCoord*ARB` forms, a texture-coordinate array per
  unit, and the ARB queries. Without it, Quake III Arena drew every
  lightmapped surface twice. Quake 3 demo `demo001` at 640x480 x 16:
  - HD 4600: 41.4 to **52.8 fps**
    ([record](docs/decisions/2026-10-22-arb-multitexture-software-engine-and-hd4600.md)).
  - GMA 950: 13.1 to **14.8 fps**
    ([record](docs/decisions/2026-10-22-netbook-arb-multitexture-quake3.md)).

  V9XGLP's new section draws each texture environment through the ARB
  calls, from immediate mode and from both kinds of array draw. On the
  software renderer, the GMA 950 and the HD 4600, every colour equals
  what the SGIS calls draw there, bit for bit.
- **Half-Life now takes the ARB calls** when both extensions are offered,
  where it used SGIS. On the GMA 950 `mwd5` runs 16.2-17.0 fps through
  ARB against 16.0-16.9 through SGIS, the same work drawn
  ([record](docs/decisions/2026-10-22-netbook-hl1-arb-vs-sgis.md)).
  **Quake 2 may switch too** (id's source prefers ARB); it has not been
  run on this version. Nor has the Rage Pro, Gen4 or the HD 4000.
- Unit 1 carries two texture coordinates, s and t, as it did under SGIS.
  A third or fourth coordinate given for it is dropped.

## 0.24.0 - 2026-10-22

- **Intel HD Graphics on Ironlake (Clarkdale, `8086:0042`): new chip.**
  The `intel-gma` package claims it: the VBE display, DirectDraw fills and
  copies on the render ring, page flips on the plane, and hardware
  Direct3D, OpenGL and Glide on the GMA X3100's engine at 16 and 32 bpp.
  Measured on one Clarkdale desktop.
- **Intel GMA and GMA X3100: a BIOS reporting more video memory than the
  driver maps no longer stops the boot.** A start-up diagnostic read memory
  at the BIOS's figure rather than the 16 MiB the driver maps. Every Gen3
  and Gen4 BIOS measured reports less, so only Ironlake's 128 MiB reached
  it.

- **OpenGL on a 32 bpp desktop.** The OpenGL driver now offers pixel
  formats on a 32 bpp desktop wherever Direct3D draws 32 bpp: the
  software engine, Intel Gen3, Gen4, Haswell and Ivy Bridge, and the
  Millennium II and Mystique. Before, those desktops got Microsoft's
  software OpenGL.
- **HD 4600 and HD 4000: blits now finish before they count as done.**
  The blitter reported each copy finished before its last pixels reached
  memory. Drawing that followed could be overwritten by the end of the
  copy, and Windows could read the frame before. In OpenGL, a front-buffer
  draw made straight after a swap was lost, and at 32 bpp a window read
  back half old.
- **Blits to the visible screen finish before they return while Windows
  may draw there**, on every chip, as draws there already did on the
  Intel parts. Full-screen exclusive programs are unaffected.
- **24-bit depth for OpenGL.** A second pixel format carries a 24-bit Z
  buffer (32-bit on the Millennium II) beside the 16-bit one, at 16 and
  32 bpp. It is measured on the HD 4600, the GMA 950 and the Millennium
  II. Asked for 32 depth bits, as GLQuake asks, Windows' ChoosePixelFormat
  gives the Millennium II its 32-bit format but gave the HD 4600 its
  16-bit one, since the Intel parts' deeper format is 24 bits; a title
  that sets the format itself gets the deeper one.
- **OpenGL: blending by destination alpha now draws.** None of the
  driver's pixel formats has an alpha channel, so OpenGL treats the
  screen's alpha as 1. Blend modes that read it are now drawn that way
  instead of being dropped. In the Quake 3 demo this was a patch of floor
  on the first map drawn without its blend pass (GitHub issue 11),
  measured on the HD 4600.
- **Render interface version 6.** GLIDE2X.DLL, GLIDE3X.DLL and
  ATI3DCIF.DLL from 0.23.0 or earlier no longer work with this driver.
  Replace any copy in a game folder with the one from the package.

## 0.23.0 - 2026-10-21

- **Intel GMA X3100 (GM965, `8086:2A02`): new chip.** The `intel-gma`
  package now claims the first Gen4 part. On one HP laptop (Core 2 Duo
  T7250) reached through remote help:
  - DirectDraw fills and copies ran on the GMA 950's ring and blitter
    and read back correctly. Gen4 keeps its page table in BAR0 instead
    of a BAR of its own; the driver now finds it there.
  - Hardware Direct3D runs on a new Gen4 engine: its own pixel and setup
    programs and pipeline state. The Direct3D probe passed every check at
    16 and 32 bpp, including texturing, depth and blending. 3DMark 2000's
    default benchmark ran to its score. Draws are queued two deep, so the
    CPU builds one while the GPU runs the last. Mip-mapped textures sample
    every level, with nearest and trilinear filtering between them.
    Colour-keyed textures draw with the key transparent.
  - OpenGL draws on the engine, with hardware textures and two texture
    units (GL_SGIS_multitexture): the OpenGL probe passed every check.
    Z buffers are now placed at a pitch the engine's depth unit accepts;
    an odd window width used to hang it.
  - 16-bit and 24-bit Z buffers, both in the tiled layout the depth unit
    expects, cleared by the 3D engine.
  - Glide runs on the engine: the Glide probe passed all 21 checks, and
    a Glide 3 session opened, drew and closed.
  - Page flips, through Gen4's own plane registers, at the panel's
    refresh, or through the ring when the next buffer sits a multiple of
    128 KiB from the current one.
  - Titles: 3DMark 2000 ran to its score at its default settings and at
    640x480 in 32-bit colour with a 32-bit Z buffer. Quake 2's OpenGL
    renderer ran on the ICD, its lightmaps on the second texture unit
    drawing as the two-pass path does. On the test laptop Quake 2 needs
    `+set s_initsound 0`, or it stops at DirectSound's start.

  Not offered:
  - The alpha test, or a colour key, together with depth writes. That
    combination hung the 3D engine; such a draw falls back to software.
  - Flips through the ring for every frame. Placing the flip chain on
    128 KiB boundaries made 3DMark 2000 show half-drawn frames; most
    flips stay register writes.

- **Intel HD Graphics 4000 (Ivy Bridge, `8086:0162`): new chip.** The
  `intel-gma` package now claims it. It was tested on one MSI B75MA-P45
  desktop with an i5-3570K, reached through remote help.
  DirectDraw fills and copies run on the blitter ring, the same way as on
  the HD 4600, and read back correctly in 256 colours, 16-bit and
  32-bit colour. Ivy Bridge wakes its graphics core differently from
  Haswell, and the driver now does it the Ivy Bridge way. DirectDraw
  page flips move the display at the monitor's refresh. Ivy Bridge pans
  through a different register from Haswell, so a back buffer that does
  not start on a 4 KiB boundary (800x600 in 16-bit colour) needed its own
  path. Hardware Direct3D runs on the render engine, sharing the HD
  4600's with Ivy Bridge's own state words and the extra pipeline
  flushes Ivy Bridge needs. The Direct3D probe passed every check at 16
  and 32-bit colour and with a 24-bit Z buffer, and 3DMark 2000's
  default benchmark ran to its score. The OpenGL and Glide probes pass
  as on the HD 4600. Before this, the generic VESA package ran it at up
  to 1920x1440.

- **V9XTRACE no longer hangs on a reused snapshot.** Once all eight
  snapshot names were taken, a run that reused one whose section had
  grown past 32 KiB never returned, and the file kept its old contents.
  The reused file is now emptied first.

## 0.22.0 - 2026-10-20

- **Intel GMA 3150 (Pineview, `8086:A011`): new chip.** The `intel-gma`
  package now claims the Atom N4xx/N5xx netbook GPU with the GMA 950's
  Gen3 engine. On one Lenovo (Atom N455) reached through remote help,
  the following were measured:
  - DirectDraw on the blitter, with page flips;
  - Direct3D on the engine at 16 and 32 bpp with 16-bit depth, passing
    the same probe checks as the GMA 950;
  - the OpenGL probe, including two texture units, and the Glide 2 probe;
  - the panel's native 1024x600, through its video BIOS.

  3DMark 2000 runs at 1024x600x16 with no draw refused (755 3DMarks; its
  default 1024x768x32 needs 16 MB and is refused). Its second PCI function (`A012`) is left
  alone. Desktop parts (`A001`) are not claimed
  ([plan](docs/plans/intel-pineview-gma3150.md),
  [first run](docs/decisions/2026-10-20-pineview-tommo33-gen3-first-run.md),
  [tier-0](docs/decisions/2026-10-20-pineview-tommo33-tier0.md)).
  - Two changes were needed. First, the Gen3 command ring now sits where
    the video-memory heap ends, worked out from what the video BIOS
    reports. Before, it sat at a fixed place that fit only parts
    reporting the GMA 950's figure, and the GMA 3150 reports more. This
    is shared code: every Gen3 chip runs it. It has been measured on the
    GMA 3150 and on a tester's GMA 900, not yet on the GMA 950
    ([GMA 900 record](docs/decisions/2026-10-20-gma900-tommo33-949fc64b.md)).
  - Second, the driver does not write the GMA 950's display watermark
    register on the GMA 3150, whose watermarks are elsewhere.
- **Intel Gen3: the OpenGL renderer string names the chip.** It said "GMA
  950" on every Gen3 part except the GMA 900; the GMA 3150 now reads
  "Velocity9x GMA 3150".

- **Intel HD 4600 (Haswell): video memory beyond 16 MiB.** The
  DirectDraw heap stopped at 16 MiB on every HD 4600. It now runs up to
  the engine's reserve, which the driver places at the top of the stolen
  memory the CPU can write. It reads that top from the chip. On the
  i5-4570 (32 MiB stolen) the heap ends at 30.8 MiB, and 3DMark 2001 SE
  turned away no mipmapped texture for room, where it had turned away
  7,366. A board whose layout the driver does not understand keeps the
  old 16 MiB heap, and `HeapLayout=` in `V9XHW.INI` says why
  ([plan](docs/plans/intel-haswell-heap-beyond-16-mib.md),
  [stolen memory](docs/decisions/2026-10-20-haswell-stolen-layout.md),
  [result](docs/decisions/2026-10-20-haswell-heap-to-31-mib.md)).
- **Intel HD 4600: 32-bit textures.** Direct3D textures can be ARGB8888
  and XRGB8888 as well as 16-bit, so 3DMark 2001 SE now runs with its
  32-bit textures
  ([record](docs/decisions/2026-10-20-haswell-32bit-textures-and-flip-wait.md)).
- **Intel HD 4600: no more half-drawn frames in 3DMark 2001 SE.** A
  frame could be drawn, or cleared, into the buffer the screen was still
  showing, because a flip takes effect only at the next vertical blank.
  Direct3D draws and DirectDraw engine clears now wait for it. In an OBS
  recording of the whole benchmark, no half-drawn frame was seen, against
  20 in 2 seconds of Lobby before. The clear's wait is in code every chip
  shares; it has been measured on the HD 4600 only
  ([record](docs/decisions/2026-10-20-haswell-32bit-textures-and-flip-wait.md),
  [issue](docs/issues/2026-10-20-haswell-3dmark2001se-flicker.md)).

## 0.21.0 - 2026-10-20

- **Intel HD 4600 (Haswell): Direct3D no longer waits for each draw.**
  Every draw used to wait for the GPU to finish it before returning,
  nearly half of each batch's time on the i5-4570. Now one draw is left
  running while the next is built. A draw still waits while GDI may be
  drawing on the screen (outside exclusive mode, or after a flip back to
  GDI). Measured on the i5-4570 with DirectX 7: 3DMark 2000 from 157 to
  112 µs a batch, and Half-Life's mwd5 timedemo from 60.5 fps to 79.3 fps
  ([plan](docs/plans/intel-haswell-pipelined-submission.md),
  [wait split](docs/decisions/2026-10-20-haswell-pipelined-phase0-wait-split.md),
  [first build](docs/decisions/2026-10-20-haswell-pipelined-phase3-deferred-wait.md),
  [the GDI rule](docs/decisions/2026-10-20-haswell-pipelined-gdi-rule.md)).
  - DirectDraw's Flip, Lock and Blt now wait for draws as well as blits.
  - Turning the engine off at Disable waits, bounded, for the last draw
    to finish.
- **Intel HD 4600: draws with both specular and fog are drawn.** Their
  shader did not fit the space the driver gave it, so every such draw
  was refused and left a hole. That was the holes in a tester's
  3DMark 2001 SE run (report V9X-D9VZS6). 3DMark 2001 SE now runs with
  no refused draw
  ([record](docs/decisions/2026-10-20-haswell-kernel-slots-768.md),
  [issue](docs/issues/2026-10-20-haswell-3dmark2001se-artifacts-field.md)).
- **Direct3D 8 games: the texture coordinate set a stage asks for is
  used.** The driver always took a vertex's first coordinate set. When a
  game asked for another one, typically a lightmap pass, textures were
  sampled with the wrong coordinates. In 3DMark 2001 SE this drew the
  Lobby's walls and floors and Dragothic's buildings as a mosaic of
  wrong texels. The fix is in the shared Direct3D core, so it applies to
  every chip's DirectX 8 path. Seen on the HD 4600, and on a tester's
  GMA 900 (915GM), whose Lobby frame draws correctly
  ([issue](docs/issues/2026-10-20-haswell-3dmark2001se-artifacts-field.md),
  [GMA 900 record](docs/decisions/2026-10-20-gma900-tommo33-0.21.0.md)).
- **Intel HD 4600: refused Direct3D batches say why** in V9XTRACE
  snapshots (`M64Policy11` to `M64Policy19`, with the failing texture
  in `MgaTexRefusedLast*`).
- Known limits on the HD 4600, unchanged:
  - Direct3D textures are 16-bit only, so 3DMark 2001 SE marks its result
    as not run at default settings.
  - Video memory is capped at 16 MiB.
  - In 3DMark 2001 SE, one vertex-shader scene is cut by a diagonal edge.

  The first two are in [STATUS.md](docs/STATUS.md)'s open work.
- Known limits on the GMA 900, 950 and other Gen3 chips, found in
  testing for this release:
  - With 8 MiB of stolen memory the Direct3D heap is about 6 MB, so
    3DMark 2001 SE's default 1024x768 at 32-bit colour fails for video
    memory. It runs at 16-bit colour.
  - In 3DMark 2001 SE's Car Chase and Lobby, some Direct3D batches are
    refused for a vertex the draw path does not accept. They are retried
    one draw at a time, so how much geometry is lost is not known
    ([record](docs/decisions/2026-10-20-gma900-tommo33-0.21.0.md)).
- Tested for this release: the HD 4600 (i5-4570), a tester's GMA 900
  (915GM) and the Millennium II, whose probes match 0.20.0
  ([record](docs/decisions/2026-10-20-mga2164w-0.21.0.md)). The other
  families were built and host-tested but not run on hardware.

## 0.20.0 - 2026-10-19

- **Direct3D at 32 bpp, and 24-bit depth buffers.** Until now every
  Direct3D engine drew only at 16 bpp, so a 32 bpp desktop got no
  hardware device and games fell back to Microsoft's software renderer
  ([plan](docs/plans/32bpp-acceleration-and-3d.md)).
  - **Intel GMA 950/900 (Gen3):** Direct3D renders at 32 bpp on the
    engine. Flat colour, Gouraud shading, depth, textures and alpha
    blending match their references on the netbook. A 24-bit depth buffer
    is offered beside the 16-bit one, at 16 and 32 bpp
    ([32 bpp](docs/decisions/2026-10-18-gen3-direct3d-at-32-bpp.md),
    [24-bit depth](docs/decisions/2026-10-18-gen3-24-bit-depth.md),
    [textures and blending](docs/decisions/2026-10-18-gen3-textured-and-blended-at-32-bpp.md)).
  - **Intel HD 4600 (Haswell):** the same, with the 24-bit depth buffer in
    the chip's tiled layout, measured on the i5-4570
    ([32 bpp](docs/decisions/2026-10-18-haswell-direct3d-at-32-bpp.md),
    [24-bit depth](docs/decisions/2026-10-18-haswell-24-bit-depth.md)).
  - **Matrox Millennium II (MGA-2164W):** Direct3D renders at 32 bpp on
    the engine, and a 32-bit depth buffer is offered beside the 16-bit one,
    at 16 and 32 bpp. Measured on A8U4I5. A depth buffer whose size differs
    from the desktop's (16-bit at 32 bpp, or 32-bit at 16 bpp) is cleared
    by the CPU
    ([record](docs/decisions/2026-10-18-mga2164w-direct3d-at-32-bpp.md)).
  - **Software Direct3D** (Direct3D set to software on the settings page)
    renders at 32 bpp, with a 16-bit depth buffer
    ([record](docs/decisions/2026-10-18-software-direct3d-at-32-bpp.md)).
  - The other chips' Direct3D engines still draw at 16 bpp only, as before.
  - **3D WinBench 98** asks for a 16-bit depth buffer even when 32 is
    offered, so its Z accuracy tests still fail as in 0.19.0. It now runs
    at 640x480x32 on the GMA 950's hardware
    ([record](docs/decisions/2026-10-18-3d-winbench-98-z-accuracy-with-z24-offered.md)).
- **OpenGL and Glide draw on the Matrox Millennium II.** Before, every
  OpenGL and Glide triangle was refused on this card and nothing
  appeared. They are now drawn by the CPU into the card's surfaces, as on
  the GMA 950, after the two were measured to agree in one frame. The
  OpenGL and Glide probes pass at 16 bpp. Glide now resends a texture
  the card refuses as a CPU copy, as the OpenGL driver already did. What
  the card draws exactly (untextured or point-sampled triangles, with
  scissor and write masks) it now draws itself. Filtered textures are
  drawn point-sampled, and blending as Direct3D draws it on this card (a
  screen-door pattern), so a surface's passes stay on the card.
  Textures DirectDraw takes back (after Half-Life's Multiplayer menu) are
  made again. OpenGL textures whose alpha is fully on or off, such as
  Half-Life's fences, are converted for the card rather than drawn by the
  CPU, so their holes no longer come out solid
  ([plan](docs/plans/matrox-mga2164w-render-interface.md),
  [engine](docs/decisions/2026-10-18-mga2164w-explicit-draws-on-the-engine.md),
  [fallback](docs/decisions/2026-10-18-mga2164w-software-fallback.md),
  [Glide](docs/decisions/2026-10-18-glide-cpu-copy-retry.md)).
- **Matrox Millennium II: triangles that start on an empty row draw
  right.** The Direct3D setup started a trapezoid at its first row even
  when that row had no pixel, so its depth and colour were extrapolated
  from outside the triangle. That shifted the depth of every pixel after
  it, or refused the whole triangle. It now starts at the first row that
  has a pixel
  ([record](docs/decisions/2026-10-18-mga2164w-direct3d-at-32-bpp.md)).
- **Matrox: the engine's register window is found by the BAR's
  prefetchable bit**, not by a fixed order per chip, so that the Mystique
  and Mystique 220, which order their BARs oppositely, bind with one rule.
  Measured on a Millennium II and the Mystique. The original Millennium
  (MGA-2064W) has run it only in 86Box, where its old order is the
  fallback
  ([record](docs/decisions/2026-10-18-mga-control-bar-from-the-prefetch-bit.md)).
- **Matrox Mystique (PCI 102B:051A) joins the matrox family**, with
  DirectDraw, GDI, Direct3D, OpenGL and Glide on the card's engine.
  Measured on a revision 02 card (an MGA-1064SG, 4 MiB) in A8U4I5
  ([plan](docs/plans/matrox-mystique.md)).
  - Mode setting by the card's BIOS: 14 modes up to 1600x1200 at 16 bpp
    and 1024x768 at 32 bpp, all switching cleanly
    ([record](docs/decisions/2026-10-19-mystique-phase1-cpu-only-enable-ok.md)).
  - The 2D engine draws DirectDraw and GDI fills, copies and text, at
    8, 16 and 32 bpp. Page flips were watched on the monitor
    ([record](docs/decisions/2026-10-19-mystique-drawing-engine.md)).
  - Direct3D on the Millennium II's 3D engine, which this card shares,
    with a 16-bit depth buffer only: the Mystique has no 32-bit one. The
    Direct3D probe matches the Millennium II's results check for check
    ([interface](docs/decisions/2026-10-19-mystique-texture-interface.md),
    [Direct3D](docs/decisions/2026-10-19-mystique-direct3d.md)).
  - OpenGL and Glide probes read as on the Millennium II
    ([record](docs/decisions/2026-10-19-mystique-opengl-glide.md)).
    Half-Life's mwd5 timedemo runs at about 6.5 fps in OpenGL and 9.2 fps
    in Direct3D, nearly all of it on the card; under OpenGL its textures
    outgrow 4 MiB and some frames fall back to the CPU
    ([record](docs/decisions/2026-10-19-mystique-half-life.md)).
  - ARGB4444 textures are not supported here either: the format draws
    garbage, as on the Millennium II.
  - The Mystique 220 (MGA-1164SG), which shares the PCI id, has its
    control and framebuffer BARs the other way round. The driver picks
    them by the prefetchable bit, so it should bind, but no 1164SG has
    run it.
- **DirectDraw fills and copies at 32 bpp on two more engines.**
  - **Intel GMA 950/900:** on the blitter, every pixel matching a CPU
    reference at 8, 16 and 32 bpp
    ([record](docs/decisions/2026-10-18-gen3-blits-at-32-bpp.md)).
  - **S3 Trio64:** done the way S3's own drivers do it. Measured in 86Box
    only; a real Trio64 has not run it yet
    ([record](docs/decisions/2026-10-18-trio64-blits-at-32-bpp-on-86box.md)).
- **Survey: a VBE mode list in RAM is no longer called a TSR.**
  `parse-vga-survey.ps1` said VBE came from "a TSR or a shadow copy"
  whenever the mode list was outside the ROM. Most VBE 2.0+ BIOSes build
  the list in the caller's buffer, so 8 of 10 reports received
  2026-10-17/18 were misread. V9XSURV now records `InfoBlockPointer`, the
  buffer's address, and the parser decides from that, then from the
  INT 10h vector, and otherwise says it cannot tell
  ([spec](docs/specifications/vga-survey.md)).
- **Known limits:**
  - The OpenGL ICD, Glide and 3DCIF still need a 16 bpp mode.
  - No game has been run that asks for a 32-bit depth buffer.
  - Desktop (GDI) drawing on the Trio64 still uses the CPU at 32 bpp.
  - On the Millennium II, Half-Life's Multiplayer menu draws corrupted
    while the game runs in OpenGL; Direct3D is clean
    ([issue](docs/issues/2026-10-19-mga2164w-hl-gl-multiplayer-menu-corrupt.md)).
    Not checked on the Mystique.
  - Some triangles the Matrox setup cannot split are refused and not
    drawn (a few thousand in a Half-Life timedemo).

## 0.19.0 - 2026-10-18

- **Intel HD Graphics 4600 (Haswell, `8086:0412`) joins the `intel-gma`
  family, with DirectDraw, Direct3D, OpenGL and Glide on the chip's own
  engines.** Measured on an i5-4570; other Haswell parts are not
  recognised yet.
  - **DirectDraw:** fills and copies run on the chip's blitter. Every
    pixel matches a CPU reference at 8, 16 and 32 bpp, including across
    mode switches
    ([record](docs/decisions/2026-10-17-haswell-phase-b2-directdraw-on-bcs.md)).
  - **Page flips:** a flip takes effect at the display's blank, so a
    triple-buffered game shows only finished frames. 3DMark 99 runs
    without flicker
    ([record](docs/decisions/2026-10-17-haswell-phase-f3-flips.md)).
  - **Direct3D:** draws run on the render engine at 16 bpp, with 16-bit Z
    buffers and every comparison
    ([record](docs/decisions/2026-10-17-haswell-phase-f2a-direct3d-engine.md),
    [depth](docs/decisions/2026-10-17-haswell-phase-f2b-depth.md)).
    Half-Life's `timedemo mwd5` at 640x480 runs at 59.2 fps, against
    41.3 on the GMA 950
    ([record](docs/decisions/2026-10-18-haswell-batch-cost-uncached-kernel-build.md)).
  - **OpenGL:** the ICD draws on the same engine:
    - textures, the write mask and the scissor;
    - two texture units, so `GL_SGIS_multitexture` is offered and
      lightmaps take one pass;
    - mipmaps.

    Half-Life `-gl` runs full screen at 67.5 fps, against 16.8 on the
    GMA 950 and 61.3 with one texture unit
    ([step 2](docs/decisions/2026-10-18-haswell-render-interface-step2.md),
    [two units](docs/decisions/2026-10-18-haswell-render-interface-step4.md),
    [mipmaps](docs/decisions/2026-10-18-haswell-mipmaps.md)).
  - **Mipmaps:** Direct3D and OpenGL both sample every level of a mip
    chain.
  - **Glide:** the Glide 2 probe passes all its checks on the engine, and
    a Glide 3 session opens and closes. No Glide game has been run on
    this chip
    ([record](docs/decisions/2026-10-18-haswell-glide-3dcif-probes.md)).
  - **Known limits:**
    - 3D runs at 16 bpp only.
    - A program that locks a Z buffer reads the chip's tiled layout, not
      rows.
    - Each batch of triangles costs about 65 µs to submit, which limits
      draw-heavy scenes such as 3DMark 99's Game 2
      ([issue](docs/issues/2026-10-17-haswell-3dmark99-flicker-and-game2.md)).
    - Half-Life refused full-screen OpenGL twice before it ran there.
      This was not investigated.
  - **3DCIF is not supported on this chip in 0.19.0.** Draws that use a
    depth buffer are refused, so the SDK demos draw only partly, and
    Laputa hangs after its first frame
    ([record](docs/decisions/2026-10-18-haswell-glide-3dcif-probes.md)).

- **The GMA 950 is unchanged by the HD 4600 work.** The same `intel-gma`
  package installs on it through Have Disk, and its probes lose no
  check. Half-Life's mwd5 runs at 46.0 fps in Direct3D and 20.8 in
  OpenGL ([record](docs/decisions/2026-10-18-netbook-gma950-0.19.0.md)).
  An emulated Trio64 also installs 0.19.0 over 0.18.5 without losing a
  check ([record](docs/decisions/2026-10-18-86box-trio64-0.19.0-install.md)).

## 0.18.5 - 2026-10-16

- **3DCIF: Assault Rigs and Wipeout draw their textures.** Early 3DCIF
  titles describe a texture in a 68-byte structure without a palette
  field, and the DLL refused every one, so Assault Rigs for 3D Rage drew
  plain white and Wipeout stopped with error boxes (report V9X-AQTFNK).
  Those textures are now accepted. Measured on the Rage XL: menus and
  levels textured, nothing refused
  ([record](docs/decisions/2026-10-16-3dcif-early-tmap-68-bytes.md)).

- **3DCIF draws lines.** Line lists and strips are drawn as two triangles
  a pixel wide, as the Glide wrapper draws them. Assault Rigs draws parts
  of its scenes with lines; 50,097 were skipped in one session on the
  Rage XL, and none are now.

- **3DCIF: keyed and masked textures are no longer refused on the Rage
  Pro class.** With blending off, the chroma key and the alpha mask now
  test the texel's alpha alone, since nothing else reads the alpha.
  They tested texel alpha times vertex alpha, which the Mach64 refuses:
  a tester's Tomb Raider lost 30-65% of its triangles and Formula 1
  over half. Measured on the Rage XL: Formula 1's demo race draws
  textured, with 3% refused (blended, masked draws, a known gap). Tomb
  Raider has not been run here
  ([issue](docs/issues/2026-10-16-3dcif-games-on-a-testers-rage-pro.md)).

- **ATI: every 16 bpp mode runs 5:5:5 under hardware Direct3D.** The
  Rage BIOS gives its 400x300, 512x384, 320x240 and 640x350 modes its
  own numbers, and the driver only knew the 15 bpp twins of the VESA
  modes. Those modes stayed 5:6:5, which ATI titles do not expect:
  Wipeout's 512x384 title backdrop was noise. The twin is now taken from
  the BIOS's own mode list. Measured on the Rage XL: Wipeout's title
  draws correctly
  ([record](docs/decisions/2026-10-16-ati-555-siblings-are-in-the-bios-list.md)).

- **Send report includes `V9XCIF.LOG`**, the log of ATI3DCIF.DLL. A
  report of a 3DCIF title, such as the Cubes demo clearing blue rather
  than grey, needs its line saying whether the desktop runs 5:6:5.

## 0.18.4 - 2026-10-15

- **Fixed: a dot pattern and "bleed through" in 3D on some GMA 900s.**
  Every Direct3D, OpenGL and Glide frame could show a fine regular
  pattern, with the desktop or an earlier frame showing between the
  dots. In each 4x4 block of pixels only six were drawn. The driver
  never set the 3D engine's polygon stipple, so it drew with whatever
  stipple the chip held at power-on; on the reporter's GMA 900 that was
  switched on. Every batch now turns it off, as Mesa and the X.org
  driver do. A test build that switched stipple on reproduced the
  pattern exactly on the GMA 950 netbook, and with the fix Carmageddon
  II and Half-Life draw as before there
  ([issue #7](https://github.com/michaeldale/velocity9x/issues/7)).
  Not yet confirmed on the reporter's GMA 900.

## 0.18.3 - 2026-10-15

- **Fixed: GMA 950 boards that display on pipe A hard-locked at the first
  page flip.** On a 945 with its picture on pipe A and pipe B switched
  off - an embedded board driving only a VGA monitor - any full-screen
  program that page-flips (Carmageddon II's intro video was the report)
  froze the machine at its first flip, and so did "Send report..." after
  a game. The driver and `V9XTRACE` read the
  scanline and frame counters of pipe B by name, and reading those on a
  disabled pipe locks this chip. Both now read only the live pipe.
  Reproduced and verified on the GMA 950 netbook with its display moved
  to pipe A: Carmageddon II ran 1,395 flips and a mid-game `V9XTRACE`
  returned, where every earlier build locked
  ([issue #6](https://github.com/michaeldale/velocity9x/issues/6),
  [record](docs/decisions/2026-10-15-a-disabled-pipes-counters-hang-the-945.md)).
  Laptops on their own panel (pipe B) were not affected. Not yet
  confirmed on the reporter's board.

## 0.18.2 - 2026-10-15

- **Fixed: Carmageddon II's HUD panels drew red in Glide.** The game
  draws them under the Glide 2 colour preset ITRGB_DELTA0, which takes
  its colour from `grConstantColorValue4` and not from the vertices. The
  Glide DLL read the vertices, which the game never fills in. The panels
  now draw as the dark translucent bars the game intends. Measured on
  the GMA 950 netbook
  ([issue #5](https://github.com/michaeldale/velocity9x/issues/5)).
- **Checked: the updater, end to end.** The 86Box Mach64 VT2, on the
  released 0.18.0, took 0.18.1 through Check for updates; all eight
  installed files equal the package by hash.

## 0.18.1 - 2026-10-15

- **New: Direct3D table fog.** Games that fog by distance
  (FOGTABLEMODE) now get fog on the Rage XL and the rest of the Rage Pro
  class, the Rage IIC, the GMA 950 and the SiS 6326. No card here has a
  fog table, so the driver works out the fog at each vertex. Measured on
  the Rage XL and the GMA 950; Thief II's fog uses it. Not on the ViRGE,
  which has no hardware fog.
- **Fixed: fogged see-through surfaces vanished on ATI cards.** The
  Rage chips cannot fog and blend in one pass, and such draws were
  dropped. They are now drawn without the fog. Thief II's lighting
  pass is one of them, and the game drew unlit without it. On the
  Rage Pro class a fogged multiply, such as that lightmap pass, is now
  fogged correctly through the texture unit instead.
- **New: transparent (colour-keyed) textures on the Rage XL and the
  Rage Pro class.** Keyed draws were dropped; they now use the chip's
  own colour compare. Measured with 16-bit textures with and without
  alpha.
- **New: brightness (gamma) on ATI cards.** Games' brightness sliders,
  and DirectDraw's gamma control, now work on the Rage family. Measured
  with Thief II on the Rage XL. Not on the Mach64 VT2/VT3/VT4, which
  have no register window; other families do not offer gamma yet.
- **Known:** in Thief II on the Rage XL a floor can show through the
  floor
  ([issue](docs/issues/2026-10-14-thief2-floor-visible-through-floor-rage-xl.md)).
- **Diagnostics:** `C:\V9XDIAG\V9XESC.INI` lists escapes the driver
  does not answer, and gamma ramps it was given; `V9XTRACE` reports
  table-fog and colour-key counts.

## 0.18.0 - 2026-10-14

0.17.1 was never released; its changes are in this version.

- **New: ATI3DCIF.DLL, ATI's 3D Rage interface (3DCIF).** Games and
  demos written for ATI Rage cards load ATI3DCIF.DLL to draw. ATI's own
  copy talks to ATI's driver and cannot run on Velocity9x. Ours draws
  through the same engine as Direct3D, OpenGL and Glide. Every package
  installs it to SYSTEM, replacing any copy already there.
  - **Measured:** all eight of ATI's SDK 4.1 demos draw and exit
    cleanly (Cubes, ATi-3D, Pixie, Fairy, Laputa, and the Knight DEMO,
    MORPH and VQ) on the Rage IIC, the Rage XL and the Intel GMA 950.
    Stock 3DCIF never supported the Rage XL PCI or the GMA 950.
  - **VQ-compressed textures** are decoded; their codebook is RGB565.
  - **Textures** too large for the engine, or for its free memory, are
    drawn a level smaller.
  - **Not drawn:** on both ATI engines, two keyed, blended sprites a
    frame in Pixie, Fairy and Laputa, which need texel times vertex
    alpha. The GMA 950 draws them.
  - **Not yet:** mipmaps (level 0 only), the Rage Pro's tiled texture
    layout, meshes, TLVERTEX vertices, lines and points.
  - **On the GMA 950:** some flicker, and some demos run too fast.
  - **No game has been run yet,** only the SDK demos. A game that
    carries its own ATI3DCIF.DLL in its folder loads that first; rename
    it.

  [Census](docs/decisions/2026-10-14-3dcif-demo-census.md),
  [VQ](docs/decisions/2026-10-14-3dcif-vq-textures.md),
  [Rage XL](docs/probe/a8u4i5-rage-xl-3dcif-2026-10-14/README.md),
  [GMA 950](docs/probe/netbook-gma950-3dcif-2026-10-14/README.md).

- **ATI cards with hardware Direct3D run 16 bits as 5:5:5**, as ATI's
  own driver does and as titles written for ATI cards assume. That
  covers the Rage IIC and the Rage Pro class: Rage Pro, XL and
  Mobility. Their 16 bpp desktops change layout; `[Velocity9x]
  HighColor=16` in SYSTEM.INI keeps 5:6:5. Both engines' Direct3D draws
  into either layout. Two changes were needed:
  - The mini-VDD now keeps the 15 bpp modes an ATI BIOS reports. They
    were dropped before, so a 555 mode set failed.
  - Both 3D engines draw into 1555 targets as well as 565.

  Textures created without a pixel format, which take the desktop's
  5:5:5, now draw. Glide opens on a 5:5:5 desktop and converts the 565
  frame buffer a game reads and writes. ATI3DCIF.DLL logs once when the
  desktop is 5:6:5, since titles written for ATI then show wrong colours.
  Measured on the Rage IIC and the Rage XL; the Mobility follows by
  design and has not booted it.
  [Desktop](docs/decisions/2026-10-14-rage-iic-555-desktop.md),
  [3D](docs/decisions/2026-10-14-rage-iic-draws-1555-targets.md),
  [textures and Glide](docs/decisions/2026-10-14-rage-iic-555-textures-and-glide.md),
  [Rage XL](docs/decisions/2026-10-14-rage-xl-555-desktop.md).
  Open: the Rage IIC blends one step low into 5:5:5
  ([issue](docs/issues/2026-10-14-rage-iic-blend-one-step-low-at-555.md)).

- **The settings page's 16-bit colour layout choices describe every
  card.** They said what the S3 ViRGE does. Now they read "Automatic
  (the 3D engine's layout)", "5:6:5 - 64 greens; ATI 3DCIF games
  miscolour" and "5:5:5 - S3 ViRGE 3D and ATI 3DCIF games".

- **Send report includes `INTELMM.TXT`** on Intel machines. GitHub
  issue 6 needed the live display pipe it records
  ([issue](docs/issues/2026-10-14-github-6-gma950-embedded-hard-lock-on-3d.md)).
  No report has yet been seen to carry it.

- **The floppies are two disks a family.** Disk 1 holds everything the
  install reads; disk 2 holds the test tools, `V9XDDP.EXE` included,
  which no single disk had room for. Copy both into one folder.

## 0.17.0 - 2026-10-13

0.16.1 was never released; its changes are in this version.

- **A new family: 3dfx VSA-100, the Voodoo4.** The `3dfx` package
  drives the Voodoo4 4500 at 640x480, 800x600 and 1024x768 in 8 and 16
  bpp. Video memory is read from the chip's straps (32 MiB less a page),
  because the BIOS's VBE reports half of it. DirectDraw and GDI fill
  and copy run on the 2D engine (`V9XGDI /accel` PASS at 8 and 16 bpp).
  Measured on a Voodoo4 4500 PCI in A8U4I5, and an AGP 4500 before it.
  A Voodoo5 binds to the same id but is untested; Velocity9x uses one
  chip.
  [Tier-0](docs/decisions/2026-10-11-voodoo4-runs-velocity9x-at-tier-0-and-its-vbe-reports-half-its-memory.md),
  [GDI](docs/decisions/2026-10-11-voodoo4-gdi-fill-and-copy-on-the-engine.md).

- **3dfx's own Glide runs on Velocity9x.** The 3dfx package ships no
  Glide; the card keeps 3dfx's `GLIDE2X.DLL` and `GLIDE3X.DLL`. The
  driver answers the requests 3dfx's Glide sends a display driver before
  it takes the card (QUERYDEVNODE and EXT_HWC), withholds the engine
  from GDI and DirectDraw while Glide holds it, and takes it back after.
  The desktop moves to 15 MiB, above the memory and command FIFO Glide
  uses, and the mode set no longer clears video memory.
  - Diablo II, Rollcage and the Need for Speed II SE demo draw and exit
    cleanly to a working desktop.
  - A Glide game that crashes before closing Glide is untested.
  [Decision](docs/decisions/2026-10-12-glide-games-run-on-3dfx-glide-and-exit-cleanly.md).

- **Hardware Direct3D and OpenGL on the Voodoo4.** The 3D engine draws
  what follows; fog, specular, trilinear, 32-bit colour, the second
  texture unit and hardware flip are refused or not used.
  - Gouraud shading and a 16-bit Z buffer with all eight compares.
  - Alpha blending and the alpha test.
  - 565, 1555 and 4444 textures up to 256 a side, including rectangles
    to 8:1.
  - Mipmaps at Direct3D's nearest level, and bilinear filtering.

  Pixel centres follow each API's rules, measured texel by texel against
  3dfx's driver. Half-Life `timedemo mwd5` at 640x480:

  | Renderer | fps |
  |---|---|
  | Direct3D, Velocity9x | 25.7 |
  | Direct3D, 3dfx's driver | 58.9 (possibly held to a 60 Hz vsync) |
  | OpenGL, Velocity9x | 12.5 |

  Quake 2's `timerefresh` on the demo map: 20.7 fps.
  [Direct3D](docs/decisions/2026-10-12-voodoo4-direct3d-engine-passes-v9xddp-core.md),
  [mipmaps](docs/decisions/2026-10-13-voodoo4-mipmaps-and-direct3d-pixel-centres.md),
  [OpenGL](docs/decisions/2026-10-13-voodoo4-opengl-and-subpixel-correction.md).

- **Direct3D draws on Direct3D's pixel centres on the GMA 950 and 900
  and in software Direct3D.** Direct3D puts a pixel's centre on its whole
  coordinate; these engines sample half a pixel further on, so point-
  sampled textures drawn one texel to a pixel, such as a game's text,
  read the next texel along. Direct3D's vertices now move by half a
  pixel for the engines that sample that way, which each engine
  declares. The software engine's last column and row are no longer
  dropped. Measured texel by texel on the GMA 950 and the Voodoo4. The
  ViRGE samples at Direct3D's own centres and is unchanged, as are the
  Millennium II, SiS 6326, Mach64 and Rage IIC, which have not been
  measured.
  [Decision](docs/decisions/2026-10-13-direct3d-pixel-centres-per-engine.md).

- **Carmageddon II's menus have their backgrounds.** The game paints
  each menu's backdrop straight into the frame buffer, then clears only
  the depth buffer with colour writes masked by `grColorMask`. The Glide
  DLLs ignored that mask, so the clear painted the backdrop black.
  `grColorMask` now masks colour for clears and triangles in both
  `GLIDE2X.DLL` and `GLIDE3X.DLL`, and the depth-only clear stays on the
  3D engine.

- **Drawing to the front buffer of a flip chain works.** The HAL refused
  any primary surface that was not at the start of video memory, but
  flipping moves the front buffer, so with three buffers two flips in
  three left it elsewhere. Glide clears after `grRenderBuffer` selected
  the front buffer were refused; Carmageddon II's startup clears now all
  run.

- **Reports say how a game ended.** When a program first uses the HAL,
  the driver now records how it ends: the exit code and the code address
  that called `ExitProcess` or `TerminateProcess`, any message box's
  caption and text, a fault's address, and whether the process exited or
  only unloaded the driver. Each record also keeps the memory Windows
  reported to the program. A game that installs its own fault handler no
  longer removes the driver's; both run. `V9XTRACE` writes these as the
  `Watch*` keys, and the Glide logs say whether the process was exiting
  when Glide detached. Prompted by Carmageddon II reports that ended with
  nothing recorded
  ([issue](docs/issues/2026-10-10-carmageddon2-exits-after-intro-gma900.md)).

- **Diagnostics.**
  - `V9XMSW /set` keeps its verdict. It exited without flushing its
    results, so a switch could read back as INCOMPLETE.
  - `V9XDDP` and `V9XGLP` gain a texel-phase test: a texture drawn one
    texel to a pixel, recording which texel each pixel took.

## 0.16.0 - 2026-10-11

0.15.1 was never released; its changes are in this version.

- **Hardware Direct3D on the Matrox Millennium II (MGA-2164W).** The
  2164W's 3D engine now draws Direct3D: Gouraud shading, a 16-bit Z
  buffer with all eight compares, perspective-correct 565 and 1555
  textures, decal, modulate and copy. Matrox's public specifications
  omit the texture engine, so every register was measured on A8U4I5's
  card with a new probe (`MGA2D.EXE /tri`, `/depth`, `/tex`) before the
  driver used it. Against Matrox's own driver 4.33c on the same card,
  V9XDDP passes every triangle, texture and depth check Matrox's passes,
  and six it fails, except specular, which is not drawn. Fog, specular,
  4444 textures and filtering are refused rather than drawn wrong. The
  original Millennium (2064W) has no 3D engine and keeps software
  Direct3D. [Decision](docs/decisions/2026-10-10-mga2164w-d3d-engine.md).
- **Half-Life on the Millennium II.** Half-Life's Direct3D renderer
  draws with its HUD, ladders and lighting, and textured walls hold
  still as the view moves. The chip cannot blend, so the HUD's additive
  sprites are drawn with their black keyed out, and the lighting pass is
  drawn at a half-density stipple rather than multiplied in. Matrox's
  own driver draws the HUD in black boxes and the lightmaps over the
  walls' textures. Half-Life `timedemo mwd5` at 640x480: 8.49 fps.
  Where the time goes is written up, and the follow-ups parked, in the
  [performance plan](docs/plans/matrox-mga2164w-performance-parked.md).
- **The Millennium II uses all its memory.** Its VBE BIOS reports 4 MiB
  of A8U4I5's 8. The matrox family now measures the memory itself after
  mapping the aperture, and DirectDraw's heap and the 3D engine's Z
  buffers and textures use all 8 MiB. The Velocity9x page still shows
  the BIOS's 4 MB, an open
  [issue](docs/issues/2026-10-11-settings-page-shows-4mb-on-8mb-millennium-ii.md).
- **Field reports are easier to read.** A repeated trace event is now
  counted on the entry already there, so a teardown of 546 textures no
  longer fills the trace ring by itself. **Send report** now includes
  the Glide logs, and a failed upload records why, which the next report
  carries.
- New diagnostics: V9XTRACE lists which blend modes the Millennium II
  refused and why triangles were declined, and times its draw stages on
  a CPU with a time-stamp counter. The driver, HAL and V9XTRACE of this
  version go together.
- **Glide 3: `GLIDE3X.DLL`.** Games written for Voodoo3-era Glide 3 now
  draw through the same engine as `GLIDE2X.DLL`. Diablo II (shareware
  demo) runs from its title screen to the Rogue Encampment on an ATI
  Rage XL. Rollcage (demo) draws its menus and races on the Rage XL and
  the Intel GMA 950: 4.2 million triangles on the GMA with none refused.
  On the Rage XL about 0.8 % of Diablo II's triangles are refused: these
  are see-through sprites the chip has no blend for.
  [Decision](docs/decisions/2026-10-10-glide3-first-frames.md).
- **Every package installs `GLIDE3X.DLL` to SYSTEM** beside `GLIDE2X.DLL`,
  under the same rule: a newer copy, such as a Voodoo3's own, is kept.
  To make room on the floppies, the DirectDraw probe `V9XDDP.EXE` is now
  in the zip downloads only.
- **Carmageddon II in Glide.** The game now offers its 3DFX (Glide)
  option, and its intro, menus and races draw on the Intel GMA 950. A
  netbook session ran 8.5 minutes into a race. Four entry points it needs
  are written (`grHints`, `grConstantColorValue4`,
  `guColorCombineFunction` and `grTexTextureMemRequired`), and
  `grLfbWriteRegion` now writes pixels instead of only logging them.
  `grTexTextureMemRequired` was the stub behind a menu drawn as a grid
  of dots: it returned 0, so the game put every texture at the same
  address. Pressing Escape in a race quits the game; whether Direct3D
  does the same is not checked.
  [Plan and result](docs/plans/glide-tmu-byte-memory.md).
- **Glide texture memory is held as bytes.** Each draw reads the texture
  memory it names, as a Voodoo's TMU does, so a game that reuses or
  partly overwrites texture memory draws what it wrote. Draws skipped
  for a missing texture went from thousands to none in Carmageddon II.
  The memory is 4 MiB, the size `GLIDE3X.DLL` reports; at 2 MiB, Diablo
  II's town drew without its sprites. NFS II SE races at about 17 fps on
  the netbook, and Diablo II and Rollcage draw as before on the Rage XL.
- **Glide chroma key.** A keyed texture under an alpha combine that takes
  alpha from the vertex or a constant now hides its keyed texels. Before,
  they were drawn, or, on the Rage XL, the whole draw was refused. This
  applies to 565 and paletted textures; formats with their own alpha
  behave as before.

## 0.15.0 - 2026-10-09

- **Matrox Millennium (MGA-2064W).** A `matrox` family boots the 2064W
  at tier-0 through its VBE BIOS, then puts DirectDraw fills and copies
  on the MGA drawing engine. On A8U4I5's physical card V9XDDP completed
  with every fill and overlap cell pixel-correct, 4 of 6 operations on
  the engine (the other two are a surface the engine cannot address) and
  no engine timeouts.
- **GDI on the Millennium's engine.** Solid fills and screen copies,
  overlapping ones included, run on the MGA engine at 8, 16 and 32 bpp.
  The register values come from the same builder DirectDraw uses.
  `V9XGDI /accel` passed at all three depths on the 86Box guest and on
  A8U4I5's card, with 160 fills and 131 copies per run on the engine and
  no timeouts. Nothing was timed, and nobody has looked at the monitor.
  [Decision](docs/decisions/2026-10-09-mga2064w-gdi-on-the-engine.md).
- **Text on the Millennium's engine**, on by default in that family. Each
  string the DIB Engine lays out is expanded onto the screen by one MGA
  ILOAD. `V9XGDI /accel` passed at 8, 16 and 32 bpp on the guest and on
  A8U4I5's card: all 84 text operations per run were drawn on the engine,
  none fell back, and every harness check ran. The harness had been
  skipping its timeout and clip checks at 32 bpp, a depth the S3 engines
  do not draw; it now runs them on the MGA, which does, and the GDI
  record's 32 bpp claim is corrected.
  [Decision](docs/decisions/2026-10-09-mga2064w-text-on-the-engine.md).
- **Page flips on the Millennium.** DirectDraw flips now move the display
  start instead of being declined. The start-address unit comes from the
  mode's own line-length register, because the 1064SG databook and
  FreeBE disagree about it on the 2064W. On A8U4I5's card 20 flips take
  331 ms, paced by the retrace, where they were declined before. The
  86Box guest's scanout shows each flipped page. Nobody has yet watched
  the physical card's monitor during a flip.
  [Decision](docs/decisions/2026-10-09-mga2064w-page-flips.md). The Millennium II (MGA-2164W) joins the same family and the
  separate `matrox-m2` drop-in package is retired; the 2164W has not run
  on this path.
  [Plan](docs/plans/matrox-millennium-family.md).
- The DIB now follows the BIOS's padded row stride on every family, not
  only Gen3. Before this, a mode whose stride was wider than its width
  sheared every row on the 2064W.
- Through a USB HDMI capture the 2064W's desktop looks soft under both
  Velocity9x and Matrox's own driver. This is parked as an open issue
  with a timing probe, `V9XTIME.EXE`.
  [Issue](docs/issues/2026-10-09-mga2064w-capture-looks-soft.md).
- **A redesigned Velocity9x page.** Display Properties now has two
  stock-size tabs. **Velocity9x** shows the adapter (with PCI revision and
  mini-VDD build), the display mode, the acceleration paths and the last
  diagnostics. **Velocity9x Advanced** holds the Direct3D, DDI, vertical
  sync and 16-bit colour selectors and the core and memory clocks.
  Texture filtering, write combining and Run diagnostics are shown greyed
  out until the driver has them. `V9XSET.EXE` uses the same layout.
- The page's video memory line read "Unavailable" on every family but
  S3; it now uses the BIOS's total. Its DirectDraw line claimed a page
  flip for every engine; it now reports the engine's own capability
  bits, which the driver publishes at every Enable. The rows no family
  publishes yet are listed in an
  [issue](docs/issues/2026-10-09-settings-page-data-gaps.md).
- **Send report** on the Velocity9x Advanced tab. It runs `V9XTRACE.EXE`,
  lists the `C:\V9XDIAG` files it would send with their sizes and what
  they contain, takes an optional one-line description, and on **Send**
  uploads them to the update server at michaeldale.com.au. The reply is
  a report code such as `V9X-4F7K2Q` to quote in a GitHub issue, shown
  with a Copy button and included in **Copy report**. Nothing is sent
  without the click. Without a network it says so and points at the web
  form, which works from any computer. One report from the 86Box guest
  reached the server with all six files intact.
- **Check for updates** on the Velocity9x tab. It asks before contacting
  the server, sends only the installed version and card family, and
  offers a newer release with its notes. **Update now** downloads it,
  checks it against the release's signature, keeps the current files and
  settings in `C:\V9XDIAG\UPDATE\BACKUP`, and installs it at the next
  restart. The new INF's files and registry settings are applied the way
  a Device Manager reinstall applies them, and a newer Glide from a 3dfx
  card is kept. After the restart it checks every file and says how the
  update went once the desktop is up. Measured with locally signed test
  releases: three updates on the 86Box ViRGE guest and one on A8U4I5's
  Matrox card, each reaching the desktop unattended with the new build
  running. No update has yet gone through the real server; this release
  is the first that carries one. Installations older than 0.15.0 have
  no updater and update by hand once.
  [Decision](docs/decisions/2026-10-09-updater-cycles-on-the-virge-guest.md).
- **Signed releases.** Each release now carries `SIGNED.TXT`, the size and
  SHA-256 of every family zip under an Ed25519 signature. The updater
  trusts only that file, because plain HTTP can be altered on the way.
  The signature check, SHA-256 and the zip unpacking are written in this
  repository and tested against the standards' own test vectors and every
  past release zip. [Releasing](docs/RELEASING.md).
- The INF now installs `V9XTRACE.EXE` and `V9XUPD.EXE` into the Windows
  system folder, so both buttons work after the install disk is gone. The
  build number moved from the Velocity9x tab to **About**.

- **OpenGL fog.** `glFogf`, `glFogfv`, `glFogi` and `glFogiv` were stubs
  and now work. LINEAR, EXP and EXP2 fog factors are computed per vertex
  from the eye distance and passed to the 3D engine as vertex fog. The
  software engine, the Mach64 and Gen3 draw it, and the fog state can be
  read back with `glGet`. In V9XGLP a red quad under blue fog read
  the same pixels on all three (86Box software guest, Rage XL, GMA 950).
  On an engine with no fog and no software fallback, such as the ViRGE,
  a fogged batch is drawn unfogged and counted as `fog-dropped` in
  `V9XGL.LOG`. Nothing has measured that path yet.
  [Decision](docs/decisions/2026-10-09-icd-fog-and-immediate-forms.md).
- **97 more OpenGL entry points.** These are the integer and double forms of
  `glColor`, `glTexCoord`, `glVertex` and `glNormal`, plus `glRect`,
  `glEdgeFlag` and the colour-index calls. Integer colours and normals
  convert as the vertex arrays already did. The ICD now implements 204
  of OpenGL 1.1's 336 entry points, up from 103. The rest are still stubs
  that set `GL_INVALID_OPERATION`: lighting, display lists, attribute
  stacks, raster position and pixel paths, evaluators, feedback and
  selection, stencil, texgen.
- Half-Life's OpenGL renderer on the GMA 950 drew `mwd5` correctly through
  this ICD with no stub calls (15.8, 17.8 and 17.4 fps). No installed test
  game has been seen calling `glFog`.
- **Attribute stacks and texture queries.** `glPushAttrib`, `glPopAttrib`,
  `glPushClientAttrib` and `glPopClientAttrib` work, as do
  `glGetTexParameter`, `glGetTexLevelParameter` and `glGetTexEnv` (both
  forms each) and `glClearStencil`. These are the stubs Half-Life, Quake 2
  and Serious Sam had been seen calling. The ICD now implements 215 of
  336 entry points.
- **A call census in `V9XGL.LOG`.** At exit the ICD logs how many times
  each implemented entry point was called. On the GMA 950, Half-Life,
  Quake 2, GLQuake and UT99 together used 52 entry points, called no
  stub, and did not reach the attribute stacks in these sessions.
  [Decision](docs/decisions/2026-10-09-icd-call-census-and-attrib-stacks.md).

## 0.14.0 - 2026-10-09

A Glide 2.x `GLIDE2X.DLL` that draws through the render interface, so
a game written for a 3dfx Voodoo runs on a Velocity9x 3D engine. Need
for Speed II SE races on the HP Mini 110 netbook (GMA 950). No other
machine or engine has run it, and no Voodoo was used for comparison.

### Glide

- **`GLIDE2X.DLL`** exports all 130 Glide 2.x entry points. It opens an
  exclusive fullscreen DirectDraw flip chain and draws into it through
  `V9xRenderInterface`, the same path as the OpenGL ICD. It covers
  everything NFS II SE calls: a W-buffer, table fog, ARGB1555, RGB565,
  ARGB4444 and P_8 paletted textures, chroma key, alpha blending, the
  clip window, lines and LFB locks. The game's 50 calls were measured
  with a census build first.
  [Plan](docs/plans/glide-2x-wrapper.md),
  [census](docs/decisions/2026-10-08-nfs2se-glide-census.md).
- The vertex, texture memory, texel format and state mapping are
  host-tested C. A probe, `V9XGLIDP.EXE`, reads back 21 checks
  (clears, flips, depth, textures, palettes, chroma key, blending, fog).
  They read back alike on Gen3 and the software engine.
  [Flip chain](docs/decisions/2026-10-08-glide-phase2-flip-chain.md),
  [textures and fog](docs/decisions/2026-10-08-glide-phase3-textures-fog.md).
- **NFS II SE on the GMA 950**, 640x480: menus and a race with
  everything drawn, nothing refused, at about 18.5 frames a second.
  [Race](docs/decisions/2026-10-09-nfs2se-races-on-glide-gen3.md),
  [frame rate](docs/decisions/2026-10-09-glide-frame-rate-netbook.md).
  - The clip window is applied in geometry, because the hardware
    engines refuse a partial scissor.
  - The clear is drawn as a quad, because the CPU clear took half of
    each frame.
  - The chain has three buffers, because with two the game flickered
    as Final Reality does.
  - The pause dialog draws correctly.
- **Every package with a 3D engine installs `GLIDE2X.DLL` to SYSTEM**
  (all but the Matrox, which has none), except over a newer one: a
  Voodoo 1 or 2 beside the 2D card keeps its own 3dfx Glide. Measured on Windows 98 SE guests: through SetupX, 3dfx 1.00,
  2.56 and 2.61 kept and an older Velocity9x Glide replaced; through a
  full Have Disk install of the S3 package, 3dfx 2.56 kept and an older
  Velocity9x Glide replaced at the restart. Windows 95 was not run.
  `V9XCOPY.BAT` replaces only a `GLIDE2X.DLL` of ours (untested in DOS).
  [Decision](docs/decisions/2026-10-09-glide-packaging-copy-flag.md).
- Known problems:
  - Gamma is not applied.
  - A small flicker remains in the menus.
  - Only Gen3 falls back to the software engine when a draw is refused.
    The Rage, SiS and ViRGE engines have not run Glide.

## 0.13.0 - 2026-10-08

Single-pass multitexture on the ATI Rage Pro class, and an OpenGL ICD
that draws a two-texture batch in passes when no engine takes it whole.
Measured on A8U4I5 (Rage XL PCI) and the HP Mini 110 netbook (GMA 950).
The Gateway's Rage Mobility-M runs the same Mach64 code and was not run.
The Intel GMA 900 (915GM) is claimed, untested on hardware.

### ATI Rage Pro class (Mach64)

- **GL_SGIS_multitexture.** The Rage Pro's second texture (the TEX_CNTL
  composite) is driven as Mesa 7.10's driver drove it, and the engine
  now reports two texture units. Unit 1 MODULATE over unit 0 REPLACE or
  MODULATE draws in one pass, the lightmap case Quake 2 and Half-Life
  use. The coordinates go premultiplied by W under TEX_ST_DIRECT. With
  the engine multiplying them itself, unit 0's mip level came out log2 W
  too coarse under the composite, and Quake 2's walls drew from their
  smallest levels.
  [Decision](docs/decisions/2026-10-08-rage-pro-composite-direct.md).
- Unit 0 loses trilinear under the composite (it takes the blend
  function), and unit 1 samples one level.

| On the Rage XL PCI | 0.12 (one unit) | 0.13 (composite) |
|---|---|---|
| Quake 2 demo1 spawn `timerefresh`, 640x480 window | 15.4 fps | 17.9 fps |
| Half-Life `timedemo mwd5`, OpenGL, 400x300 window | 14.8 fps | 16.0-16.6 fps |
| GLQuake v0.97 `timedemo demo1`, default settings | 21.5 fps | 21.5 fps (no multitexture) |

### OpenGL ICD

- **A two-unit batch no engine takes is drawn in single-unit passes**,
  as the application would have drawn it without the extension: unit 0,
  then unit 1's texture blended to apply its combine (MODULATE, REPLACE,
  DECAL, and BLEND, which takes one pass or two). Without it, on the
  Rage, which has no software fallback, such a batch drew nothing.
  GLQuake and QuakeWorld draw their single-pass lightmaps with unit 1
  GL_BLEND (`gl_texsort 0`). On the Rage XL all 41,772 such batches in
  GLQuake's demo1 drew this way at 21.6 fps, against 21.5 for its default
  path. Declined, as before, when the application blends or an alpha
  test has no depth writes to confine the passes.
  [Decision](docs/decisions/2026-10-08-icd-two-unit-split.md),
  [GLQuake](docs/decisions/2026-10-08-glquake-blend-lightmaps-split.md).
- On the GMA 950 the split never runs: Gen3 draws every two-unit batch
  itself. GLQuake's unit-1 BLEND path runs there at 59.7 fps against 52.8
  for its default path, and Half-Life OpenGL at 640x480 at 17.1-17.6 fps,
  as on 0.12.
  [Decision](docs/decisions/2026-10-08-netbook-multitexture-recheck.md).

### Intel GMA 900 (915GM)

- **The 915GM's GMA 900 (`8086:2592`) is claimed by the intel-gma
  package**, with the GMA 950's Gen3 Direct3D, blits and ring flips.
  From a DOS survey sent with GitHub issue 3; no 915 has run this
  driver. Its BARs, stolen memory, BSM and VBE size match the netbook's.
  Mip chains are placed in the 915's stacked layout rather than the
  945's (Mesa's `is_i945`). The 1024x576 rows are the netbook's; on the
  915GM the BIOS's own modes, to 1024x768, come from the VBE scan.
  `IntelRuntime3D=0` leaves a 2D desktop. OpenGL names it "Velocity9x
  GMA 900".
  [Decision](docs/decisions/2026-10-08-intel-915gm-gma900-support.md).
- The mini-VDD mapped the runtime ring through the netbook's aperture
  address, a constant. It now takes the aperture the driver reads from
  BAR2, and refuses one it cannot trust before writing a register. On
  the 915GM survey the constant was the register BAR. The descriptor
  also withholds the ring when the reported video memory would put the
  DirectDraw heap over it.
- The netbook ran this build for regressions: V9XDDP's checks as on
  2026-09-26 or better, the ring and ring flips up through the new path,
  V9XGLP passing, and Quake 2 at 34.8 fps against 34.5.
- The GMA 4500 surveys from the same issue are recorded against the
  [Gen4 plan](docs/plans/intel-gen4-gen5-bringup.md); nothing changes for
  them.

### Diagnostics

- V9XTRACE reports the Mach64 draw's cost split (`R2Cycles*`, `R2Work`),
  and the render interface's entry timer runs on the Mach64. On the Rage
  XL at 640x480 a draw's time is the chip filling pixels, not the driver.
  [Decision](docs/decisions/2026-10-08-rage-xl-draw-cost.md).
- V9XGLP's `SgisMip` section names the mip level unit 0 samples, with
  unit 1 off and on, in perspective too.

### Known

- REPLACE with the fragment's alpha has no Direct3D op in the core, so
  such draws are refused: on the Rage they draw nothing, on the GMA 950
  they draw in software. GLQuake's `-nomtex +gl_texsort 0` lightmaps are
  the measured case; its default path does not draw this way.
  [Issue](docs/issues/2026-10-08-mach64-replace-fragment-alpha-refused.md).
- Direct3D still sees one texture stage on every engine
  ([plan](docs/plans/d3d-two-stage-multitexture.md)).

## 0.12.2 - 2026-10-07

- **Intel GMA 950: triangles crossing the screen edge are clipped.** The
  Gen3 engine was the one engine the core did not clip for, on the
  reading that its hardware clips inside a 4096 guard band, but its vertex
  builders take only vertices on the target, so a single off-screen vertex
  refused its whole batch. UT99 on the netbook lost 207,032 batches in one
  intro and DM-Morpheus run on 0.12.1 (holes, walls in flat colour); with
  the core clipping, 77, and the scene draws as on the Rage XL, 33 to 35
  fps. Half-Life (DDI 5 and 6) and Quake 2 OpenGL unchanged.
  [Decision](docs/decisions/2026-10-07-gen3-clip-in-core.md).

## 0.12.1 - 2026-10-07

- **DX5 fans and strips are drawn.** The single-primitive entry point took
  triangle lists only, and the batched one refused strips and stopped the
  whole call at any record it did not take, so the triangles after it were
  lost as well. Deksor's Rage Mobility-M traces for GitHub issue 2 (0.11.0)
  showed UT99 sending all 25,629 of its DrawOnePrimitive calls as fans,
  every one refused: the dropped polygons the issue reported. A fan and
  strip read-back on the 0.12.0 code drew 1600 of 1600 pixels for a small
  fan and 0 for a small strip, a 98-vertex fan and a 98-vertex strip; with
  the fix all four draw 1600 on the Rage XL (DirectX 9.0c) and the ViRGE/DX
  (DirectX 6.1). Lines and points on the DX5 path are skipped on their own
  instead of ending the call. Half-Life unchanged (Rage XL 18.4 fps).
  UT99 (Direct3D, A8U4I5 Rage XL): 8,122 DrawOnePrimitive calls and 1.39M
  DrawPrimitives records over an intro and DM-Morpheus run, none refused,
  14 to 17 fps average in the match.
- **OpenGL survives a display mode change.** UT99's OpenGL renderer
  started windowed on the 800x600 desktop and switched to 640x480
  fullscreen (the same path as changing the video driver in its
  preferences) died on its first frame with `Assertion failed:
  SwapBuffers( hDC )`. The ICD's primary surface had been made in the old
  mode, `Restore` fails there (`DDERR_WRONGMODE`), and the present gave
  up. The primary is now made again when it cannot be restored; the same
  switch on A8U4I5 logs `primary restore hr=8876024B, made again` and UT
  keeps rendering at 640x480.
- **OpenGL windows larger than the screen.** The back and depth buffers
  are held to the screen size. UT99 windowed on a 640x480 desktop asked for
  644x465, the video-memory surface was refused (`DDERR_INVALIDPARAMS`),
  no context was made and UT asserted in `wglMakeCurrent`; it now renders.

## 0.12.0 - 2026-10-07

The Direct3D 8 release. Direct3D 8 runs on the hardware through
the DirectX 6 driver interface (DDI 6), measured on A8U4I5 (Rage XL PCI)
and the HP Mini 110 netbook (GMA 950), with the S3 ViRGE/DX checked in
86Box. GitHub issue 2's `GetDeviceCaps` `0x8876086A` no longer happens on
either physical machine. Families not named were not run.

### Direct3D 8 (DDI 6)

- DrawPrimitives2, Clear2 and the DX6 caps, behind the new **DDI 6**
  selector (Automatic, Never, Always; `[Velocity9x] Direct3DDdi`, and
  `[Velocity9x.Direct3DDdi]` per program). Automatic, the default, gives
  DDI 6 to programs that load Direct3D 8 and DDI 5 to the rest.
- Flexible vertex formats are converted, so Direct3D 8's audit no longer
  refuses the device (it rejected `FVFCaps` zero).
- Points and lines are drawn at DDI 6, one pixel wide, and line caps are
  published to DDI 6 programs.
- Clear2 clears depth and colour on the engine; on the CPU the depth clear
  cost the Rage XL a third of each Half-Life frame at DDI 6.
- Non-square textures are offered on the Mach64 and GMA 950
  (`SQUAREONLY` dropped): Direct3D 8 refused every non-square texture
  before, which stopped 3DMark 2001 SE and garbled its HUD.

| At 640x480 | Rage XL PCI | GMA 950 |
|---|---|---|
| 3DMark 2001 SE | 111 3DMarks | 244 3DMarks |
| Half-Life `mwd5`, Direct3D (DDI 5) | 18.6 fps | 41.0 fps |

Diagnostic: `[Velocity9x] Dp2Capture=1` records the next DDI 6 program's
DrawPrimitives2 calls to `C:\V9XDIAG\V9XDP2R.BIN`
(`tools/diag/dp2ring.py`).

Limits: Direct3D 8 is offered 16-bit colour only (16-bit display modes,
R5G6B5, A1R5G5B5 and A4R4G4B4 textures, D16, no stencil); Direct3D 9
needs a DDI 7 driver and is not available. 3DMark 2001 SE's game tests
run at 1 to 5 fps on both machines; why is not measured yet.

On the ViRGE/DX (86Box, DirectX 6.1) the DDI 5 probe results are
identical to the 0.11.0-era driver, and with DDI 6 forced for one program
lines, points and the engine colour clear read back exact on the S3D. On
the Rage IIC (A8U4I5, 0.12.0 build) Half-Life `mwd5` runs 5.47 / 5.55 /
5.54 fps (0.10.0: 5.57) and Quake 2's OpenGL timedemo 4.5 / 4.6 fps
(4.4), pictures correct; Direct3D 8 gets the HAL there and DDI 6 lines,
points and the colour clear read back exact. Not run at DDI 6: the SiS
and software families.

### OpenGL

- GL_SGIS_multitexture: a second texture unit through the render
  interface, combined on the CPU by the software engine and in one
  fragment program on the GMA 950. Correct on the netbook (V9XGLP's seven
  cases in tolerance; Half-Life `-gl` uses it).
- Texture copies refill only the rectangles `glTexSubImage2D` changed:
  Quake 2 `timerefresh` 21.0 to 35.3 fps with multitexture on the GMA 950.
- Texture state (bind, parameters, environment) no longer draws the held
  batch: Half-Life `-gl` 427 to 48 draws a frame, 17.6 fps with or
  without multitexture.
- `glPolygonOffset` is implemented; Half-Life's decals called a stub.

### Diagnostics

- A `V9XTRACE` snapshot names the driver builds, the boot, the card and
  the programs that used Direct3D or OpenGL, so two snapshots can be
  matched.

## 0.11.0 - 2026-10-05

The SiS 6326 release. A sixth family, `sis`, runs the SiS 6326
(`1039:6326`): modes through the BIOS, DirectDraw on its 2D engine, and
Direct3D and OpenGL on its 3D engine. The S3 Trio3D (`5333:8904`) joins
the s3 family. 0.10.1 was never published, so its changes, the Rage XL
and fog release, are part of this one and listed after the SiS sections.

Measured on A8U4I5 (SiS 6326 card 2, rev 0Bh, 4 MB AGP; earlier a Rage XL
PCI and a Trio3D) and the HP Mini 110 netbook (GMA 950). Changes in
shared code reach every chip of the same design, per the rule recorded
with the Rage Pro flip below. Families not named were not run.

| On A8U4I5 at 640x480, SiS 6326 | Velocity9x 0.11.0 |
|---|---|
| Half-Life `mwd5` timedemo, Direct3D | 8.6 fps |
| Half-Life `mwd5` timedemo, OpenGL | 6.5 fps |
| Quake 2 demo `timerefresh`, OpenGL | 7.8 fps |
| 3DMark 99 Max (800x600) | 274 3DMarks |
| Final Reality | 3.51 Reality marks |

| On A8U4I5 at 640x480, Rage XL PCI | Velocity9x 0.10.1 |
|---|---|
| Half-Life `mwd5` timedemo, Direct3D | 18.1 fps |
| Quake 2 demo timedemo, OpenGL | 11.6 fps |
| 3DMark 99 Max | 1225 3DMarks |

Recorded, not chased
([SiS evidence](docs/probe/a8u4i5-sis6326-opengl-2026-10-05/README.md),
[Rage XL record](docs/decisions/2026-10-03-rage-xl-pci-benchmarks-a8u4i5.md)).

### SiS 6326 (new family)

- **Tier-0 first:** the BIOS sets 22 modes at 8 and 16 bpp, enable-ok on
  the first boot, GDI and mode switching pass
  ([record](docs/decisions/2026-10-05-sis6326-first-velocity9x-bind.md)).
  The datasheet was reconciled against Mesa and Xorg first
  ([register reference](docs/specifications/sis6326-registers.md)).
- **DirectDraw on the 2D engine:** fill and copies, each encoding
  measured byte-exact by a write probe before the driver used it; every
  V9XDDP blit lands on the engine
  ([record](docs/decisions/2026-10-05-sis6326-engine-under-directdraw.md)).
  **Page flips** program the start address
  ([record](docs/decisions/2026-10-05-sis6326-page-flips.md)).
- **Direct3D on the 3D engine.** It was probed in phases before any
  driver code:
  - the first triangle;
  - shading and Z16;
  - textures, whose pitch field is a small float;
  - vertex fog and specular;
  - the colour blend factors.

  V9XDDP fails no check SiS's own driver passes on the same card, and
  passes three it fails
  ([records](docs/plans/sis-6326-hardware-3d.md)). Three engine traps,
  each measured:
  - **an untextured batch before a textured one stalls the engine**, so
    every draw is textured
    ([record](docs/decisions/2026-10-05-sis6326-d3d-engine.md));
  - **the Z test compares 15 bits**, so depth fills are halved
    ([record](docs/decisions/2026-10-05-sis6326-z-compares-15-bits.md));
  - **TEND must follow every triangle.** Without it, Final Reality's
    first Z-tested batch hung the engine
    ([record](docs/decisions/2026-10-05-sis6326-tend-after-each-triangle.md)).

  A stalled engine is quarantined and logged, and the machine stays up.
- **Half-Life's 2x modulate** (DESTCOLOR/SRCCOLOR) drawn, and a mip
  filter set for magnification folded to its in-level half: Half-Life,
  Final Reality and 3DMark 99 run with no refused batch
  ([blend record](docs/decisions/2026-10-05-sis6326-colour-blend-factors.md),
  [filter record](docs/decisions/2026-10-05-sis6326-mag-filter-fold.md)).
- **OpenGL on the 3D engine.** Changes:
  - render-interface draws taken;
  - the scissor in the clip registers, measured;
  - VRAM textures for the ICD;
  - the ICD's depth clears halved like Direct3D's.

  V9XGLP passes; Half-Life's OpenGL renderer went from 0.8 fps on the CPU
  to 6.5 fps
  ([record](docs/decisions/2026-10-05-sis6326-opengl.md)).
- **GDI is still drawn by the CPU.**

### S3 Trio3D (`5333:8904`)

- **Bound to the ViRGE path** as a `virge-dx` alias with the Trio3D/2X's
  engine descriptor. It boots, passes GDI acceleration, and draws
  hardware Direct3D
  ([record](docs/decisions/2026-10-04-trio3d-8904-on-the-virge-path.md)).
- **The S3 BIOS's own mode list is merged,** adding 320x240, 400x300 and
  512x384 at 16 bpp. The 2 MiB board's memory was measured by a VRAM
  walk.
- **Half-Life refuses Direct3D on the 2 MiB board.** Parked as an issue.

### Shared core: DirectDraw modes and the render interface

- **DirectDraw modes publish the default refresh (0), not 60.** At 60,
  DirectDraw dropped every mode below 640x480 from its list and refused
  to set it. Measured on the SiS 6326, applied to every family.
- **The render interface's describe** names the SiS engine (ABI engine
  6), and its depth clear follows the engine's depth-fill rule.

### ATI Rage Pro class (Rage XL measured)

- **Bound and running on a Rage XL PCI** through the Mach64 engine path,
  with every V9XDDP triangle, texture, fog and mip check passing
  ([probe](docs/probe/a8u4i5-rage-xl-pci-2026-10-03/README.md)).
- **Page flips** through `CRTC_OFF_PITCH` in the vertical blank, the Rage
  IIC's flip unchanged. Before, every flip on this class was declined and
  full-screen programs flickered. Watched on the monitor; now declared
  for the whole Rage Pro class through its shared hook
  ([record](docs/decisions/2026-10-04-rage-xl-page-flips.md)).
- **Half-Life's HUD drawn**: an alpha test that passes for every alpha
  from the batch's least vertex alpha up is dropped rather than refused,
  which took about 15,000 refusals a run to none
  ([record](docs/decisions/2026-10-04-mach64-alpha-test-that-cannot-discard.md)).
- **4x4 and 2x2 textures**, measured; 357 Half-Life draws a run were
  refused for a 4x4
  ([record](docs/decisions/2026-10-04-rage-xl-small-textures-sync-version-and-oversize.md)).

### ATI Rage IIC

- **The alpha test on ARGB4444**, through texels whose alpha LSB holds
  the test's answer, and on RGB565 where the vertex alpha cannot fail
  it. Blended textured sprites with Z off stay refused: drawn, they
  hard-locked the machine four times
  ([record](docs/decisions/2026-10-03-rage-iic-alpha-test.md)).
- **MASKZ advertised**, being true; it was not why ATI's driver draws a
  Z-off pass that ours does not
  ([record](docs/decisions/2026-10-03-rage-iic-ati-driver-sampled-texture-cache.md)).

### Intel GMA 950

- **Vertex fog**, in the fragment program as Mesa's i915 does it, now
  advertised. Without it 3DMark 99 drew Game 1's fog as a second
  untextured pass, 19,696 batches a run, now about 300
  ([record](docs/decisions/2026-10-04-gen3-vertex-fog.md),
  [the finding](docs/decisions/2026-10-04-netbook-3dmark-untextured-draws-are-a-fog-pass.md)).
- **The settings page reports DirectDraw acceleration**: the chip
  published no DirectDraw word, so the page said software emulation
  while blits have run on the Gen3 blitter since 0.8.1.

### Shared core (all engines)

- **4,096 texture handles instead of 256.** 3DMark 99's Game 2 keeps
  about 2,700 alive; past 256 TextureCreate failed silently and the
  lightmap passes drew black. A full table is now counted
  ([record](docs/decisions/2026-10-04-texture-handle-table-full.md)).
- **A VSync setting** on the settings page: Game decides (the default),
  Always on, Always off. It applies at the next DirectDraw program with
  no restart. Off took 20 flips from 332 ms to 0 on the Rage XL, and from
  324 ms to 4 on the netbook once an unsynced Intel flip writes the plane
  base instead of the ring
  ([record](docs/decisions/2026-10-04-vsync-setting.md)).
- **OpenGL textures over a device's size limit** are fitted from the
  chain's smaller levels or a box-filtered copy, not dropped
  (host-tested; no hardware run has bound one).
- **The settings page's Direct3D and DirectDraw rows.** The Direct3D
  line read the colour layout's value and always said "Not advertised";
  the ATI DirectDraw words had no sentence.
- **The mode-list sync** picks the display instance present this boot
  when a swapped card left a second one marked.
- **DxDiag shows the driver version**: Win16 version resources are now
  laid out by the build, which fails if Windows cannot read them.

### Diagnostics

- V9XTRACE gains `D3dTextureTableFull`, `FlipVSyncOverridden`, a census
  of untextured batches (`NoHandle*`: blend, varying coordinates, white,
  last state and colour), and per-reason counts for the Mach64 passive
  check and the Rage IIC's alpha test (shared ABI 2026100409; DRV, HAL
  and V9XTRACE deploy together).
- The SiS 3D engine:
  - counts its refusals by reason in the same counters: `M64PolicyNN` for
    Direct3D, `M64AcceptPolicyNN` for the render interface;
  - on a stall, writes the stalled batch, every triangle of it, its
    texture and its Z buffer to `C:\V9XDIAG` (`V9XSIS3D.TXT`,
    `V9XSIS3T.BIN`, `V9XSIS3Z.BIN`).
- SIS3D, the 3D write probe, replays such a log from a file
  (`/phase6 /file`).

Still open:
- the Rage IIC's blended Z-off sprites hard-lock it and stay refused;
- texel-times-vertex alpha is refused on the Rage IIC;
- Image Quality's Game 1 capture is washed white on the Rage XL, not
  looked at;
- Intel table fog and specular colour are not drawn;
- the GMA 950's fog was seen only in 128x72 captures;
- the SiS 6326 has no software fallback for the draws it refuses;
- the SiS 6326's GDI is not accelerated;
- why the SiS 6326 needs TEND is not established;
- the Trio3D's Half-Life refusal is parked.

## 0.10.0 - 2026-10-03

The Rage IIC release: hardware Direct3D and the first OpenGL this card has
had, on the ATI 3D Rage IIC AGP (`1002:4757`, 4 MB), a chip with no setup
engine, so every triangle is set up on the CPU into the trapezoids its
engine draws. Measured on one physical machine, A8U4I5 (Pentium III
1 GHz, 440BX, Windows 98 SE); the plan and its phases are
[here](docs/plans/ati-rage-iic-hardware-3d.md). Other families carry only
the shared-core changes marked below, untested on their hardware.

| On A8U4I5 at 640x480 | Velocity9x 0.10.0 | ATI 4.11.2474 / 2611 |
|---|---|---|
| Half-Life `mwd5` timedemo, Direct3D | 5.57 fps | 6.63 fps |
| Quake 2 demo timedemo, OpenGL | 4.4 fps | does not start (`GLimp_InitGL failed`) |
| Half-Life, OpenGL | not run | refused |
| Final Reality, 3DMark 99 | render correctly (watched on the monitor) | 3.81 Reality Marks; 484 3DMarks |

Recorded, not chased; ATI's figures are the
[native baseline](docs/decisions/2026-10-02-rage-iic-native-baseline-a8u4i5.md),
re-measured under ATI's driver on 2026-10-03.

### ATI Rage IIC (new)

- **Bound and verified at tier-0**, then the 2D engine for DirectDraw
  fill and copy, from a known engine state
  ([survey](docs/decisions/2026-10-02-rage-iic-register-survey.md),
  [first boot](docs/decisions/2026-10-02-rage-iic-first-velocity9x-bind.md)).
- **The trapezoid engine, measured to the pixel** before anything was
  built on it: rows, spans and the edge walk, Gouraud and Z16, texture
  coordinates, bilinear filtering, formats, blend modes and fog, each
  scene exact on the card
  ([edge model](docs/decisions/2026-10-02-rage-iic-trapezoid-edge-model.md),
  [triangles](docs/decisions/2026-10-02-rage-iic-triangles-and-gouraud.md),
  [texturing](docs/decisions/2026-10-02-rage-iic-texture-addressing.md),
  [filtering and blending](docs/decisions/2026-10-02-rage-iic-filtering-formats-blending-fog.md)).
- **Direct3D**: CPU triangle setup behind the shared engine ops, with
  perspective as a quadratic fitted per triangle and split where it would
  stray by half a texel; RGB565, ARGB1555 and ARGB4444 textures, each edge
  a power of two from 8 to 256; nearest and bilinear filtering, mip-mapping
  (MIPNEAREST, MIPLINEAR, LINEARMIPNEAREST); modulate, decal and copy; a
  16-bit Z buffer with every comparison; the blend factors the engine
  measured; fog; alpha test on 1555's alpha bit. Final Reality and 3DMark
  99 render correctly
  ([record](docs/decisions/2026-10-02-rage-iic-direct3d-first-runs.md)).
- **Page flips** through `CRTC_OFF_PITCH`, written in the vertical blank;
  before, every flip was declined and full-screen applications drew
  unseen ([record](docs/decisions/2026-10-03-rage-iic-scanout-start.md)).
- **OpenGL** through `V9XGL.DLL` on the same engine: Quake 2 draws
  textured and lit ([record](docs/decisions/2026-10-03-rage-iic-opengl-first-runs.md)).
- **Triangle setup halved**, ~41 k to ~20 k CPU cycles a piece: a closed-form
  bound on the fit's error settles most split decisions without sampling,
  the sampling itself by forward differences, and one fit per piece
  ([record](docs/decisions/2026-10-03-rage-iic-setup-cost.md)).
- **The texture cache on.** Phase 4 measured with it off and the setting
  stayed; on, bilinear with Z costs 11.0 engine clocks a pixel instead of
  17.1, it never returned a stale texel, and ATI's driver runs with it on.
  Half-Life 4.29 to 5.57 fps, Quake 2 3.6 to 4.4
  ([record](docs/decisions/2026-10-03-rage-iic-ati-driver-sampled-texture-cache.md)).
- **Mip-mapping**: the engine picks its level per pixel from its own
  increments; chains are placed a level to a block, which the 4 MB card
  needs under Direct3D's texture management. Correct pictures; no
  measurable speed in either demo
  ([record](docs/decisions/2026-10-03-rage-iic-mip-mapping.md)).

- **The rest of the Rage II and Rage Pro lines bound**, at Michael's
  request, from ATI's own driver list: the Rage II class (Rage II, II+, the
  other Rage IIC ids, LT, LT-G) on the Rage IIC's path, the Rage Pro class
  (Rage Pro, LT Pro, XL, XC, the other Mobility parts) on the Mobility-M's
  Mach64 engine path, and the Mach64 VT3/VT4 on the VT2's tier-0 path - 29
  ids, each "not validated" in the INF; none has run anywhere. The engine is
  driven only once `CONFIG_CHIP_ID` names the class its path expects.

### Shared core (all engines)

- **The render interface's draw saves the FPU** as the Direct3D entries
  do. The OpenGL ICD's draws ran under the application's control word -
  Quake 2's 24-bit precision - and on the Rage IIC 68,270 of its pieces
  failed setup that the same inputs pass at full precision.
- **DirectDraw is no longer emulated on chips without Direct3D.** Narrowing
  the surface callbacks for such a family dropped `SETCOLORKEY` while its
  pointer stood, so `SetInfo` failed and the runtime ran with no heap, on
  every such chip since 0.7.0 (bisected on the Rage IIC at tier-0).

### Diagnostics

- V9XTRACE gains the Rage IIC's skipped pieces by stage, its draw cost by
  part (prepare, split, build, emit), the pixels, trapezoids, register
  writes and FIFO reads behind it, and its texture placement outcomes
  (shared ABI 2026100304; DRV, HAL and V9XTRACE deploy together).
- `ATIRX` (development tool, not packaged) gains read-only clock and
  register sampling (`/pll`, `/sample`, which also ran under ATI's own
  driver) and fill, mip and texture-cache scenes.

Still open: ATI's driver is 16% ahead in Half-Life, and draws a large
textured pass with Z disabled that ours does not (it advertises MASKZ;
we do not); ARGB4444 alpha test and texel-times-vertex alpha are refused
and not drawn (10,371 of Half-Life's batches in a session, 2,807 of
Quake 2's 32,071); a few thousand pieces a demo fail the texture fit,
unanalysed; A8U4I5 has three times dropped off the network while idle with
Windows still running
([issue](docs/issues/2026-10-03-a8u4i5-drops-off-network-when-idle.md)).

## 0.9.2 - 2026-10-02

The submission release: on the netbook (Intel GMA 950) the CPU no longer
waits for the GPU after every batch, and the OpenGL driver's per-vertex
cost is down by a quarter. Found running Final Reality,
Half-Life, Quake 2, Serious Sam and 3DMark 99 on the netbook and the
Gateway (Rage Mobility-M), with the instruments that found them. Each item
names the machine it was measured on; other families carry the shared-core
changes untested.

3DMark 99 Max at 640x480, 16-bit, triple buffering, all tests: the netbook
1253 3DMarks (852 on 2026-09-30, 717 on 2026-09-25), the Gateway 604 (589
and 643 in 0.9.1's runs); per-test results in the
[record](docs/decisions/2026-10-02-3dmark99-640-both-machines.md). Recorded,
not attributed among the changes below.

### Shared core (all engines)

- **Final Reality no longer dies on its first draws.** Three 6 KB draw
  buffers lived on the calling application's stack; Final Reality's render
  thread arrives with 16 KB committed and was killed silently on the first
  push past it. The buffers are file-scope, and the HAL build now fails on
  any function frame over 2 KB. Full Final Reality runs complete on both
  machines ([issue](docs/issues/2026-09-30-final-reality-robots-faults-inside-drawprimitives-on-the-netbook.md),
  [record](docs/decisions/2026-10-01-final-reality-full-runs-netbook-and-gateway.md)).
- **An undefined texture address is taken as CLAMP.** Half-Life's
  Direct3D renderer sets TEXTUREADDRESSU/V to 0 for its sky on the
  Mach64; every engine refused it and a refused batch is not drawn, so the
  sky showed old frames. Fixed on the Gateway
  ([record](docs/decisions/2026-10-01-hl1-d3d-sky-was-texture-address-zero.md)).
- **List calls merge the batches culling and clipping split**, within one
  call. Measured to change nothing in Quake 2 or Half-Life on the netbook,
  whose batches are small DrawPrimitives records instead.

### Intel GMA 950

- **A flip completes at the first frame tick**, not the second: Final
  Reality's fill rate 8.18 to 16.68 Mpixels/s and its 3D score 2.19 to
  2.71 on the netbook. The fill test had been waiting for the display,
  not drawing ([record](docs/decisions/2026-10-01-intel-flip-completes-at-one-tick.md)).
- **The CPU no longer waits for the GPU after every batch.** Asynchronous
  submission is the default. Its 2026-09-29 corruption was draws refused
  whenever a busy ring's head sat on a dword rather than a qword; with that
  fixed, Half-Life's Direct3D timedemo went from 29.06 to 42.42 fps on the
  netbook, and Half-Life and 3DMark 99 were correct. `V9X3D SYNC` turns it
  off ([record](docs/decisions/2026-10-01-intel-async-corruption-was-the-dword-head.md)).
- **Textures to 1024 texels**, from 256: every size from 256 to 1024 and
  full 512 and 1024 chains read back exactly by the V9XTSHP probe. Serious
  Sam's 512 textures had all been drawn by the CPU; its demo on the
  netbook 1.81 to 2.73 fps
  ([record](docs/decisions/2026-10-02-serious-sam-netbook-profile.md)).

### ATI Mach64 (Rage Mobility-M)

- **MODULATEALPHA** is drawn as MODULATE wherever the alpha it would
  differ in is not read: Half-Life's refusals 74% to 1.7%
  ([record](docs/decisions/2026-10-01-mach64-modulatealpha-where-it-cannot-show.md)).
- **Rectangular textures**, each edge a power of two from 8 to 256, with
  mip chains; the row pitch is the width, measured exact by a new probe
  (V9XTSHP). Half-Life's OpenGL texture churn on the 4 MB card halved; the
  frame rate did not move ([record](docs/decisions/2026-10-01-mach64-rectangular-textures.md)).

### OpenGL (all engines)

- **Lower per-call CPU cost in the ICD:** RDTSC instead of
  QueryPerformanceCounter (the PIT) around each draw, a texture lookup
  hint, and per-primitive viewport and colour caches. Half-Life OpenGL on
  the netbook 10.75 to 14.75 fps
  ([record](docs/decisions/2026-10-01-hl1-opengl-icd-cpu-costs.md)).
- **A cheaper vertex path**, with output held byte-identical by a hashed
  host test: the clipper skips planes a polygon is wholly inside and keeps
  each plane's distances, the inside test is written out, and the vertex
  history is a ring instead of copies. Quake 2's `glVertex*` 2.47 to 1.86
  us a vertex on the netbook; and the environment colour packed when set
  rather than per polygon, and glBegin's setup kept while viewport,
  scissor and depth range are unchanged. Quake 2's timedemo 18.4-19.1 to
  21.3-21.9 fps with the profiling timers on
  ([record](docs/decisions/2026-10-01-gl-vertex-path-profile-quake2-netbook.md)).

### Diagnostics

- V9XTRACE gains flip-wait and frame-interval timers, Mach64 refusals by
  reason, the Gen3 submission's make-up and repeated state, the render
  interface's draw time, the texture-address values applications set, and
  the Gen3 ring plans refused as invalid (shared ABI 2026100106; DRV, VXD, HAL, SETP, ICD and V9XTRACE deploy
  together). The first use is a profile of the netbook's draw submission:
  about two thirds of it waits for the GPU
  ([record](docs/decisions/2026-10-01-hal-submission-profile-on-the-netbook.md)).

Still open: refused Direct3D batches on the Mach64 are dropped rather
than drawn on the CPU; Gen3 still refuses a vertex whose Z is a rounding
step outside [0, 1] (about 0.1% of 3DMark's batches); the Gateway
occasionally hard-locks during Direct3D, unexplained; Serious Sam on the
netbook is limited by video memory, and write-combining and textures in
system memory are planned and parked
([note](docs/plans/netbook-performance-parked.md)).

## 0.9.1 - 2026-09-29

The Rage Mobility release: hardware Direct3D and OpenGL on the ATI Rage
Mobility-M (Mach64LM), measured on one Gateway Solo 2150 laptop. Other
families carry only the shared-core and ICD changes marked below, which
have not been re-measured on their hardware.

### ATI Mach64 (Rage Mobility-M)

- **A Direct3D engine** for the Mach64 3D unit, behind a draw-acceptance
  policy that refuses every state without a passing hardware scene
  ([plan](docs/plans/ati-rage-mobility-hardware-3d.md)). Each state and
  setup word was measured on the Gateway first: fills, Gouraud triangles,
  Z16 test and write, the 36 blend factor pairs, alpha test, fog,
  scissor, texture environments and TEX_CACHE_FLUSH.
- **Textures** from 8 to 256 texels, square, in RGB565, ARGB1555 and
  ARGB4444: DECAL, DECALALPHA, MODULATE, wrap and clamp, nearest and
  bilinear; mip chains with level selection, nearest or bilinear within
  the level (MIPNEAREST, MIPLINEAR), and trilinear (LINEARMIPLINEAR);
  specular; fog over a texture. The HAL places
  textures itself, because DirectDraw's pitch for them is 4096 bytes.
- **Perspective:** the engine multiplies S and T by W itself under
  `TEX_ST_MULT_W`; wrapped coordinates are rebased per triangle, because
  its range ends between u of 1000 and 10000. The rebase centres each
  triangle's coordinates, because the engine's mip level under
  perspective errs by the coordinate itself; the tunnel's walls were 1
  to 1.5 levels too coarse.
- **Every engine fill clears the 3D state first**; a fill carrying the
  previous draw's Z and alpha-test state hard-locked the Gateway
  ([issue](docs/issues/2026-09-28-mach64-hal-first-draw-hard-hang.md)).
- **3DMark 99 Max** completes all 26 tests at 640x480x16 with no
  refusals from 3DMark; a triangle with NaN coordinates or a W not above
  zero is skipped alone, not its batch
  ([record](docs/probe/ati-rage-mobility-m-3dmark99-2026-09-29/README.md)).
- **OpenGL on the Mach64:** Quake 2 runs with hardware textures on the
  Rage Mobility-M, at about 8 fps at 640x480 fullscreen (the stock ATI
  driver: 22). Textures pack on 64 bytes; DirectDraw Blt uses the engine's
  screen copy; MIPLINEAR and LINEARMIPNEAREST are read the DDK's way.
- **Vertex registers are reused within a batch**: a fan's next triangle
  writes only its new vertex. Pixel-exact on the Gateway, and no faster:
  the register writes were not the limit
  ([record](docs/decisions/2026-09-29-mach64-vertex-register-reuse-physical.md)).

### OpenGL (all engines)

- Render interface ABI 3: describe states the smallest surface texture,
  and names the Mach64. The ICD squares textures for an engine that
  samples only squares, drops alpha tests that cannot discard, takes
  unclipped fan triangles without re-clipping, and logs frames and time
  per report. Measured on the Gateway only; on the ViRGE the square copies
  now apply to its non-square textures, untested there.

### Packaging

- **Version resources:** every shipped DLL, EXE and display driver
  carries a VERSIONINFO (Explorer's Properties > Version), its numbers from
  `build.h` and the build id in FileVersion. The two VxDs do not yet: the
  DDK's ADRC2VXD is a 16-bit tool.

### Survey and BIOS

- Every INT 10h call goes through one routine; VBE 4F11h had been writing
  to DS:0000.
- The ROM is summed over both lengths when the header and PCIR disagree,
  and the PCIR class code and CPUID model are read where they are.

Still open on the Mach64: mip level boundaries a third of a level
coarser than Direct3D's (the engine's own, with no working bias), an
occasional lock after a cold boot, LINEARMIPNEAREST, fog with blend, no
CPU fallback for refused draws (Quake 2's particles), and Quake 2 at a
third of the stock driver's frame rate.

## 0.9.0 - 2026-09-27

The OpenGL release. Velocity9x now has an OpenGL 1.1 installable client
driver, V9XGL.DLL, loaded by Windows' own OPENGL32.DLL and drawing
through the same render core as Direct3D. Quake 2 runs on the Intel
GMA 950 with hardware textures and on the software engine, Serious Sam on
the GMA 950, and the ViRGE draws what its S3D can express. Measured on the netbook
(945GSE), A8U4I5 (physical ViRGE/DX) and the 86Box guests.

**Packaging:** every family's package carries V9XGL.DLL, and its INF
copies it and registers it under
`HKLM\Software\Microsoft\Windows\CurrentVersion\OpenGLDrivers`
(`Velocity9x` = `v9xgl.dll`), so an install through Display Properties
sets up OpenGL too. With the ICD the families no longer fit one floppy
together, so the transfer disk is now one disk per family,
`build\floppy\<family>`, each about 810 KB
([issue](docs/issues/2026-09-26-floppy-over-capacity-with-opengl-icd.md)).

### OpenGL

- **The ICD** answers OPENGL32's contract: the display driver's
  OPENGL_GETINFO escape, the `Drv*` exports and a 336-slot dispatch table
  generated from one manifest. It offers a double-buffered 16-bit colour,
  16-bit depth format on 16 bpp desktops the engine can draw into (565 or
  555; 555 only on the ViRGE), and OPENGL32's generic formats otherwise
  ([record](docs/decisions/2026-09-26-phase3-icd-clears-and-presents-through-opengl32.md)).
- **Implemented:** the matrix stacks, immediate mode, vertex arrays with
  all fourteen interleaved layouts, clipping in clip space (the scissor
  box included, as geometry), texture objects and images with the table
  3.18 environments, state queries, glReadPixels for the colour formats,
  front-buffer drawing, and GL_NONE. Every pure module is host-tested; the
  remaining entry points set GL_INVALID_OPERATION and are listed in the
  [requirements inventory](docs/plans/opengl-1.1-requirements.md).
- **Quake 2 on the GMA 950:** 1.2 fps with every texture drawn by the CPU,
  11 with square textures sampled by Gen3, 22 with non-square ones, 28
  with triangles held across glEnd (`timerefresh` at demo1's spawn point;
  [record](docs/decisions/2026-09-26-phase5-quake2-gen3-hardware-textures.md)).
  Fullscreen and `vid_restart` work.
- **Serious Sam** on the GMA 950: its letterbox scissor, 1x1 textures and
  the part's 5 MB of free video memory no longer send it to the CPU -
  1.78 million triangles on Gen3 in a demo session, none refused
  ([record](docs/decisions/2026-09-26-phase5-serious-sam-on-gen3.md)).
- **The ViRGE** samples OpenGL textures where the S3D can express them
  exactly: square 1555 and 4444 maps (RGB images converted at upload).
  Non-square textures, textures under 4 texels and blends other than
  source alpha over its inverse are refused, not drawn wrong; the Quake 3
  demo's menu model draws, its text does not
  ([record](docs/decisions/2026-09-26-phase5-virge-hardware-textures-for-opengl.md)).
- **Logging** for bring-up: the ICD writes `C:\V9XDIAG\V9XGL.LOG`, with
  per-path batch and texture counters every ten seconds and each state an
  engine refuses.

### Render core and HAL (all engines)

- **A neutral render core** (`src/display32/r3d/`): the clipper, list
  builder and cull decision moved out of the Direct3D front end, and every
  engine draws from a neutral description. The V9XDDP probes and 3DMark 99
  measured the same before and after on the ViRGE, the software engine
  and the netbook.
- **Render interface v2**, exported by V9XHAL.DLL: versioned, stateless,
  validated before anything is read or emitted, taking the Win16 mutex
  itself. Describe states each engine's hardware texture limits.
- **Draining is three-state** (done, busy, abandoned): a Gen3 drain that
  times out is reported instead of treated as finished, and DestroySurface
  no longer leaks a Gen3 block after a slow drain.
- **A refused draw falls back to the software engine** on Gen3, after a
  drain; not on the ViRGE, where the shared 555 frame is unmeasured.

### CPU rasterizer

Non-square textures, texel alpha and the alpha test, the whole blend
factor set, a scissor and colour mask, perspective-correct texture
coordinates, mip chains and post-texture fog - for the render interface.
Direct3D on the software engine keeps the caps it advertised.

### Gen3 (GMA 950)

- Textures that are non-square or down to one texel are placed, bound
  and sampled. The Direct3D caps still say square and at least 8, so
  Direct3D applications are offered nothing new.
- 3DMark 99 after the render-core move: 669 at 1024x576 (643 in 0.8.1),
  the difference unattributed.

Still open: the ViRGE's missing blends
and non-square textures; a Gen3 and CPU-fallback disagreement in one
Quake 2 frame ([issue](docs/issues/2026-09-26-software-engine-quake2-sky.md));
and video memory, which Serious Sam's textures outgrow on the netbook.

## 0.8.1 - 2026-09-25

An Intel GMA 950 release. Everything here was measured on the netbook
(945GSE, MICHAEL-NETBOOK); the S3 and other families carry no functional
change beyond the shared Direct3D core items marked below.

- **3DMark 99 renders on the Intel**, and fast enough to score: 717 at
  640x480 once mip chains are laid out by the HAL
  ([record](docs/decisions/2026-09-25-mip-trees-on-gen3.md)), and 244 to
  643 at 1024x576 across a padded Z pitch, a 2112-byte scanout stride,
  flip buffers at the display pitch and textures placed at their own pitch
  ([record](docs/decisions/2026-09-25-placing-plain-textures-on-gen3.md)).
- **3D WinBench 98's quality suite** runs to the end on the Intel:
  WRAPU/WRAPV wrap the short way, and Decal, Mirror, Add and Modulate
  blending pass. Back faces are culled in the Direct3D core, which is
  shared by every engine
  ([record](docs/decisions/2026-09-25-back-face-culling-in-the-d3d-core.md)).
- **DirectDraw blits run on the Gen3 blitter.** Half-Life's per-frame
  present went from about 33 ms on the CPU to 0.6 ms; fills from 6 ms to
  0.3 ms ([record](docs/decisions/2026-09-25-gen3-engine-blits-half-life.md)).
- **The application's alpha test is drawn**, so Half-Life's see-through
  fences are see-through instead of black
  ([record](docs/decisions/2026-09-25-gen3-runtime-alpha-test.md)).
- NaN is detected from the bits in the Direct3D core (every engine),
  because Watcom's float compares never see one.
- **Installing:** the intel-gma package is for `8086:27AE` only. Its mode
  list is the video BIOS's: 640x480 and 1024x576 at 8 and 16 bpp. The
  panel has 576 lines, so there is no 800x600.

Still open: the Gen3 heap may fragment under repeated benchmarks
(`docs/issues/`), 4% of Half-Life's draws are untextured for a non-square
texture, and the CPU still waits on the GPU after every batch
([plan](docs/plans/intel-gen3-async-submission.md)).

## 0.8.0 - 2026-09-23

The first packaged release since 0.7.1. The version number was set on
2026-09-17, when the Intel GMA 950 first ran Final Reality textured; that
bring-up is the last subsection below. Everything after it falls into three
groups:
- Direct3D core fixes shared by every engine, found by running 3DMark 99 and
  Final Reality against S3's own driver on the emulated ViRGE/DX.
- Perspective texturing and edge clipping on the S3D, measured on a physical
  Trio3D/2X (A8U4I5).
- Presentation and depth work on the Intel GMA 950, measured on the netbook.

Two problems stay open: the intermittent Final Reality flicker on both
cards, and 3DMark 99 drawing nothing on the Intel. See **Still open** below.

### Direct3D core (all engines)

- **DrawPrimitives state changes are applied.** `V9xD3dDrawPrimitives`
  stepped over the state pairs interleaved in its data, so 3DMark 99 ran a
  whole benchmark without ever setting a filter, and state blocks longer
  than 64 were discarded outright
  ([record](docs/decisions/2026-09-20-the-state-changes-were-being-stepped-over.md)).
- **DirectDraw is told `GetDriverInfo` exists, and extended caps are
  answered.** `DDHALINFO_GETDRIVERINFOSET` was never set, so the runtime
  never called the entry point the driver published. With it set, the
  runtime asks for twelve GUIDs; `GUID_D3DExtendedCaps` is now answered with
  each engine's own texture limits instead of limits the runtime invented
  ([record](docs/decisions/2026-09-20-the-runtime-asks-for-twelve-guids-and-now-gets-extended-caps.md)).
- **Indexed primitives, strips, fans and long lists are drawn.**
  - DrawOneIndexedPrimitive was advertised and stubbed; it now draws lists
    and strips
    ([record](docs/decisions/2026-09-20-the-indexed-path-was-advertised-and-stubbed.md)).
  - The single-primitive path takes a list of any length.
  - DrawPrimitives takes triangle fans. Fans were every test Final Reality
    runs, so it went from a black screen to a rendered scene
    ([record](docs/decisions/2026-09-20-final-reality-renders-a-scene-triangle-fans-were-the-whole-benchmark.md)).
- **A refused triangle no longer aborts the frame, and an unsupported shape
  is declined rather than failed.** Final Reality quit on
  `DDERR_INVALIDPARAMS` from shapes the driver should have handed back to
  the runtime
  ([record](docs/decisions/2026-09-20-a-refused-triangle-was-aborting-the-frame.md)).
- **The DX5 draw paths clip.** DrawOnePrimitive, DrawPrimitives and
  DrawOneIndexedPrimitive passed unclipped vertices, and the S3D declines any
  triangle with a vertex off the screen. So every full-screen quad was
  dropped: 3DMark 99's fill-rate test drew black and its filtering tunnel
  lost its walls. One run declined 66,990 triangles; after the fix, none.
  - The clipper now cuts at the viewport edge, 640 rather than 639, and
    lets the hardware clip rectangle discard the last pixel.
  - It interpolates texture coordinates perspective-correctly.
  - Gen3 is left on its measured unclipped path
  ([record](docs/decisions/2026-09-23-the-dx5-draw-paths-never-clipped.md)).
- **Flat shading is honoured in the core**, by copying the provoking
  vertex's colour, instead of being claimed and ignored.
- **Alpha-test render states are recorded and counted**, so a capture shows
  whether an application asks. None of the engines draws an alpha test.
  3DMark 99 asks for none, even with alpha-compare caps published
  ([record](docs/decisions/2026-09-23-the-tunnel-texture-has-no-checker.md)).

### S3 ViRGE/DX and Trio3D/2X

- **Perspective-correct texturing.** The device published
  `D3DPTEXTURECAPS_PERSPECTIVE` but drew every texture affine. Triangles
  whose rhw values differ now use the perspective command types and W
  registers, with S3's DDK encoding, checked against 86Box. Their mip level
  is computed at each vertex and interpolated across the triangle.
  Equal-rhw triangles, which covers every probe rung, keep the old encoding
  bit for bit.
  - On the Trio3D: the tunnel walls converge on the vanishing point, and
    texture-rendering tiles and fill-rate lettering are sharper.
  - 3DMark's fill-rate figures fall from 73.6 to 9.2 Mtexels/s (single) and
    from 73.5 to 34.2 (multi). The old figures timed frames in which nothing
    was drawn
    ([record](docs/decisions/2026-09-23-perspective-texturing-on-the-s3d.md)).
- **Flip timing.**
  - The start address is written at the blank-to-active edge.
  - A flip completes when the retrace ends, not when it begins.
  - Direct3D draws wait while a flip is pending, and a batch whose wait
    runs out is refused rather than drawn into the visible buffer.
  - On a part with no 3D-done bit, the engine-idle bit is confirmed over
    consecutive reads, because it was measured settling falsely
    ([record](docs/decisions/2026-09-19-the-virge-idle-bit-lies-and-it-is-still-not-the-flicker.md)).
  - None of this fixed the flicker; see **Still open**.
- **Measured, and not a driver fault:**
  - 3DMark 99's filtering tunnel has no checkerboard on this card. The
    texture in video memory is a smooth gradient: 65,536 texels, no edges.
    3DMark does not send the checker, most likely because it comes from a
    multiplicative blend the S3D lacks.
  - Fill rate is dimmer than on other cards, for want of additive blending.

### Intel GMA 950 (945GSE)

- **Runtime Direct3D and the flip are on by default.** The arm-file keys
  are now off switches
  ([decision](docs/decisions/2026-09-18-intel-runtime-3d-and-flip-on-by-default.md)).
  Of the bring-up items that were open and gated off, the flip is now on,
  and the depth test is resolved below.
- **The depth comparison the application asks for is emitted, not LESS
  alone.** 85% of a 3DMark 99 run had been drawn with no depth test
  ([intel98](docs/decisions/2026-09-20-intel98-eighty-five-per-cent-of-the-run-had-no-depth-test.md),
  [intel99](docs/decisions/2026-09-20-intel99-the-depth-fix-works-and-changes-nothing-visible.md)).
  The fix works, and the picture is unchanged.
- **Blending** is enabled for the one factor pair measured on silicon,
  SRCALPHA/INVSRCALPHA. MODULATE takes its own alpha. ALPHAFLATBLEND is
  withdrawn, because nothing here gives a flat triangle one alpha.
- **The Gen3 flip, rebuilt against i915.**
  - The pending bits were wrong, and every flip was declared done at once
    ([audit](docs/decisions/2026-09-18-intel-gen3-page-flip-audit.md)).
  - The flip is issued through the ring.
  - The plane base is written in active video, since it latches at the start
    of the blank
    ([record](docs/decisions/2026-09-18-intel-plane-base-latches-at-vblank-start.md)).
  - A flip completes on a frame-counter tick.
  - Direct3D draws hold until the pending flip is taken.
- **Batches complete through a status-page breadcrumb**, the way i915 does
  on gen3. Each batch now flushes the render cache first and invalidates the
  texture cache
  ([record](docs/decisions/2026-09-19-intel86-the-completion-channel-works-and-the-flicker-is-not-unfinished-drawing.md)).
- **The display watermark is programmed**, with its depth taken from
  DSPCNTR. The BIOS never reprograms it. The pipe still underruns with it
  correct
  ([record](docs/decisions/2026-09-20-intel96-the-watermark-is-programmed-correctly-and-the-pipe-still-underruns.md)).

### Still open

- **The Final Reality flicker, on both cards.** One frame in eight or nine
  shows only the clear and the sky band. Five mechanisms have been measured
  and closed: premature completion, buffer ownership, draws racing a flip,
  unfinished drawing, and a leaking flip window. The cause is not known
  ([state of play](docs/issues/2026-09-19-the-flicker-state-of-play.md)).
- **3DMark 99 draws nothing on the Intel.** The draws are aimed at the three
  presented buffers and the back buffer still reads black
  ([intel102](docs/decisions/2026-09-20-intel102-the-draws-are-aimed-correctly-and-still-nothing-lands.md),
  [plan](docs/plans/intel-3dmark99-missing-textures.md)).
- **Final Reality renders green speckle on the emulated ViRGE**
  ([issue](docs/issues/2026-09-20-final-reality-renders-green-speckle-on-the-virge.md)).
- **The Trio3D's two blend states.** Every run this week was in the bad one
  (`TexMatrixOk=90`). The last register that seemed to track the state,
  Input Status 0 bit 4, stopped tracking it on boot 104.

### Diagnostics and research

- **Measurement tooling.** A present trace and a submission-count rule,
  host-tested, record which batches actually reached the hardware. The
  capture adds a draw-target census, back-buffer and retired-buffer
  readbacks, uptime and client stamps, and a trace file that rolls over, so
  two games in one boot can be told apart.
- **Documents only:** a Gen3 document inventory, an OpRegion review against
  the 945
  ([record](docs/decisions/2026-09-22-the-945-has-the-opregion-registers-the-spec-does-not-name-it.md)),
  a GMA 950 2D plan and a Voodoo3 engine plan.
- **The survey reads a linear window above 16 MB.** INT 15h AH=87h builds a
  24-bit descriptor base, so the aperture step could never reach where VLB
  windows are actually placed - the Trio64V+ VLB report submitted to the
  Vogons thread on 2026-08-28 put the window at `62000000h` and came back
  `skipped`. `V9XSURV` now reads through a read-only unreal-mode excursion
  above the ceiling and keeps the BIOS service below it, naming in the report
  which produced the bytes, and refusing outright without a confirmed 386 in
  real mode. It proves the flat read against the BIOS ROM first, because
  otherwise a dead window and a flat read that never worked are the same
  report. Measured in 86Box on a ViRGE/DX at `E0000000h`
  ([evidence](docs/decisions/2026-09-22-survey-flat-read-86box.md)).
- **The survey's safety contract was relaxed, narrowly and on purpose.**
  `lgdt` and a write to `cr0` were banned outright; they are now permitted for
  that one sequence, and in exchange the build gate pins the `cli`, the
  clearing of PE, the reset of FS and the self-test comparison, newly refuses a
  store through the flat segment, and keeps `lidt`, CR1-4 and the debug
  registers banned. Eighteen gate mutations are rejected where fifteen were.
- **`parse-vga-survey.ps1` no longer blames a memory manager for a flat read.**
  Its virtual-8086 caveat named INT 15h AH=87h unconditionally, which would
  have discredited reports produced by a path that refuses to run in V86 at
  all. It is method-aware, and warns when a flat read's self-test did not pass.
- **The ViRGE's VL defect is named, and it is not the MMIO window.** mkarcher
  answered this project's question in the thread: the fault needs framebuffer
  access and certain MMIO regions to be *mixed*, the damage is silent
  configuration corruption that locks the bus later, and the exact pattern is
  under NDA. That retires the DOS-box mode-transition theory, rejects the
  B-window and any ViRGE/DX substitution, and puts the configuration the VLB
  plan had chosen inside the susceptible set - the mixing he describes is this
  driver's own architecture
  ([decision](docs/decisions/2026-09-22-virge-vl-mmio-framebuffer-mixing.md),
  [revised plan](docs/plans/virge-vlb-old-mmio.md)).

### Intel GMA 950 bring-up (2026-09-17)

Hardware Direct3D on the Intel GMA 950 (945GSE), the first engine outside
S3, as it stood when the version was set. The flip and the depth comparison
were open then; both are covered in the Intel subsection above. Run
nowhere but `MICHAEL-NETBOOK` (captures `C:\temp\intel52` through
`intel60`).

- **A 32-bit ring submission path for the Gen3.** The HAL builds each
  batch from the application's geometry, runs it through the same decoder
  allowlist the armed diagnostics use, writes the ring and waits for the
  head. Runtime 3D is permitted per machine by `V9X3D ON` writing
  `IntelRuntime3D=1`, and refused by default
  ([amendment](docs/decisions/2026-09-15-intel-phase5-errata-gate.md),
  [first application draw](docs/decisions/2026-09-16-intel-gen3-first-application-draw.md)).
- **Textures, depth and modulate on the runtime path**, each measured on
  the armed scenes first: RGB565 sampling, a 16-bit Z buffer with LESS and
  optional writes, and texel-times-vertex-colour through a constant
  fragment program
  ([audit](docs/decisions/2026-09-16-intel-gen3-modulate-and-depth-audit.md),
  [measured](docs/decisions/2026-09-16-intel-gen3-depth-test-and-modulate.md)).
- **Final Reality black screen: three faults, one record.** The texture
  table dereferenced a released surface wrapper on teardown (resolved once
  at creation now); an 11,644-byte stack frame in the draw path, in a DLL
  built without stack probes, stopped the first RenderPrimitive of every
  boot from returning (arrays moved to file scope); and the game's
  textures were ARGB4444 against a driver offering RGB565 alone, then
  placed in system memory because the device caps never said the engine
  textures from video memory. ARGB1555 and ARGB4444 are built, decoded
  and published; `TEXTUREVIDEOMEMORY` is claimed. The game runs textured
  ([record](docs/issues/2026-09-16-final-reality-renders-black-and-the-hal-faults.md)).
- **The Gouraud scene** replaces the alpha-test scene in the armed table:
  one triangle, a primary at each corner, interior probes that must not
  read the fill
  ([flat shading issue](docs/issues/2026-09-17-flat-shading-is-claimed-and-the-provoking-vertex-is-not-programmed.md),
  [fill rule](docs/decisions/2026-09-17-intel-gen3-fill-rule-documented-by-intel.md)).
- **The probe rolls its results over** at 24,000 bytes; KRNL386 copies the
  whole file through one segment on every write and faulted at 34,020
  ([issue](docs/issues/2026-09-17-the-probe-kills-itself-writing-its-own-results.md)).
- **Open, and gated off.** The Intel flip path is in the binary behind
  `IntelFlip=1` (`V9X3D FLIP`) and has never written the plane base on
  silicon; a read-only scanline watch runs first. The depth test is skipped
  on draws asking for a comparison other than LESS. Both are in the record
  above.

## 0.7.1 - 2026-09-11

Bug fixes for the software Direct3D engine. The main one was found by running
Final Reality rather than the probe: it drew the Robots scene with no textures
at all through this driver, and fully textured through Microsoft's software
rasterizer, on the same guest in the same boot. Every call returned success.
The rest of the release is that fix, a related blending fix, and a
measurement of an option added here.

- **Textures created without an explicit pixel format were never sampled.**
  When `DDRAWISURF_HASPIXELFORMAT` is clear, the surface uses the primary's
  format. That is why `ddpfSurface` must not be read without the flag, but it
  is not a reason to refuse the surface. Both texture classifiers refused it
  anyway, so a texture created the ordinary way - letting the runtime pick
  the format - was silently never sampled. Final Reality's Robots scene
  refused 39,793 of them and drew the whole scene in untextured Gouraud.
  `v9x_d3d_target_layout` has handled the same flag correctly for the render
  target since 2026-09-02, falling back to `vmiData.ddpfDisplay`; the texture
  functions never got that fallback, and they have it now in both engines.
  After the fix the same scene runs textured, with every refusal counter at
  zero across 466 textures and 8,753 primitive calls
  ([record](docs/decisions/2026-09-11-a-texture-with-no-pixel-format-is-in-the-displays-format.md),
  [issue](docs/issues/2026-09-11-every-final-reality-texture-is-refused-for-having-no-pixel-format.md)).
  The hardware path is changed too. Its comment claimed the refusal was
  correct because the display is 5:6:5, which is wrong on the 5:5:5 desktop
  this driver selects for hardware Direct3D. That change has not been run on
  a ViRGE.

- **The software engine now supports DESTCOLOR blending.** It is implemented
  as a source factor and advertised, so `dwSrcBlendCaps` reads
  `ONE | SRCALPHA | DESTCOLOR`. The four factors it had were copied from
  S3's own ViRGE driver, which describes the S3D unit rather than a CPU
  rasterizer; DESTCOLOR costs this engine one multiply per channel, on pixels
  that are already being blended. It uses the exact divide rather than a
  shift, because a lightmap that lights nothing multiplies by white, and an
  approximate divide would darken the frame by one level everywhere the pass
  touched. It is advertised as well as implemented because applications check
  the caps before attempting the pass. Tested on the Trio64 guest with a new
  probe cell that can tell a multiply from a skip - green over magenta, which
  share no channel - and there is no measurable cost: the benchmark's alpha
  rung is identical in RAM and VRAM
  ([record](docs/decisions/2026-09-11-the-lightmap-pass-now-draws.md)). The
  hardware engine keeps its four factors and its own `describe_caps`.

- **`D3DSoftSysMem` makes no difference to Final Reality.** The option is new
  in this release - the entry below adds it - and it was added on the
  assumption that texel reads across the aperture are the main cost in a
  textured draw, without timing it. A new test guest confirmed that
  assumption: aperture reads do not get faster with a faster CPU, and a 2.7x
  faster CPU gives only 1.4x on a video-memory target. The option still makes
  no difference, because DirectDraw never puts Final Reality's textures in
  system memory, and advertising `D3DDEVCAPS_TEXTURESYSTEMMEMORY` does not
  change that. The run with the option **off** shows it: system-memory
  textures are refused and counted when it is off, and a full run of the
  scene created 349 textures with every refusal counter at zero
  ([record](docs/decisions/2026-09-11-d3dsoftsysmem-buys-final-reality-nothing.md)).
  No code changed. The 2026-09-10 record is amended so it no longer implies
  an unmeasured speed-up. The option stays off by default, and still works
  for an application that asks for system-memory textures itself.

- **Two new probe cells.** `BlendMultiply` draws green over magenta, so a
  correct multiply, a skipped draw and an opaque draw each produce a
  different colour. The older `BlendModulate` cell draws white, and white
  multiplied by anything is that thing, so it cannot tell a multiply from a
  skip. `DisplayFmtTex*` creates a texture without specifying a format - the
  case real applications hit, which no cell covered - and reports what
  DirectDraw chose for it. **That cell has no pass/fail key.** With the fix
  in place, and Final Reality rendering the same kind of texture correctly,
  its own draw still writes nothing. A pass/fail key would report a failure
  the driver does not have, so the cell reports its readings and nothing else
  until that is explained.

- **New test guest for benchmarking the software rasterizer.**
  `Win98SE-Fast-D3D` on agent port 9878: ASUS CUBX 440BX, Celeron Mendocino
  533 - the fastest CPU in this 86Box build, whose binary has no Pentium III
  family at all - 256 MiB, and a Voodoo3 3500 AGP driven through the `vbe`
  package. Against the Pentium MMX 200 guest the rasterizer runs 3.6x to 5.0x
  faster on a RAM target and 1.4x to 1.8x on a video-memory one, with all 24
  pixel and depth hashes identical. Swapping the ViRGE/DX for the Voodoo3 on
  the same CPU isolates the bus: AGP reads are 2.09x quicker and writes 0.61x,
  so textured and depth-tested scenes gain 1.5x to 1.8x while untextured
  fills get slower ([guest](docs/vm-environment.md)). Also recorded there:
  build 9001 does have `trio3d2x_agp`, so the `8A13` alias's note that no
  emulator profile exists for it is out of date.

- **The software engine skips blend factor pairs it cannot express, instead
  of drawing them opaque.** `DESTCOLOR` over `ZERO` is the multiplicative
  pass a lightmap uses. The destination is what the frame has already drawn,
  so a correct multiply by white leaves it unchanged, and drawing the pass
  opaque paints white over the scene. That is what produced 3DMark 99's
  saw-toothed panels on the hardware path, which has skipped and counted
  these pairs ever since. The software engine deliberately did the opposite,
  on the argument that refusing would report failure for a legal draw; but
  skipping the triangles is not refusing, and the HRESULT stays zero either
  way. The probe's `BlendModulate` cell read 992 on the emulated ViRGE and
  65535 here, and now reads 992 on both, with `D3dBlendSkipped=1` and
  `D3dBlendLastPair=0x00090001` naming the pair
  ([record](docs/decisions/2026-09-11-the-software-engine-drew-an-inexpressible-blend.md)).
  Superseded the same day for this pair, which is now implemented rather than
  skipped - see the DESTCOLOR entry above. The skip and its counter remain,
  and still cover every factor pair outside the five.

- **The software engine can sample textures in system memory, with
  `[Velocity9x] D3DSoftSysMem=1`.** It used to refuse any
  `DDSCAPS_SYSTEMMEMORY` texture, because it reaches textures through the
  framebuffer aperture - which meant every textured pixel read its texels
  across the PCI bus, the cost the scalar plan identifies as probably
  dominant. The setting publishes `D3DDEVCAPS_TEXTURESYSTEMMEMORY` beside the
  video-memory cap and gives the engine a second addressing path: an offset
  into the aperture as before, or the surface's own linear address. Off by
  default, because that second path can only bounds-check a surface against
  its own extent, where the first checks it against the aperture. Tested
  through the installed driver on the Trio64 guest: the test pixel is white
  when refused and green when allowed
  ([record](docs/decisions/2026-09-10-software-d3d-system-memory-textures.md)).
  It went in with no speed claim through the driver, because the benchmark
  that measured the RAM-versus-VRAM gap never loads the HAL. That timing was
  taken before this release shipped and came out at zero for Final Reality,
  for a reason the option cannot fix - see the entry above.

- **The software engine now counts its texture refusals**, in the diagnostics
  the ViRGE path has used since 3DMark 99. It used to refuse silently, and a
  refused texture draws as untextured Gouraud in the vertex colour, which
  looks exactly like a texture full of that colour. The counters immediately
  identified a capability bit that was being erased within one boot.

- **`engine_caps` is written in two places, and the second erased the new
  capability bit.** `v9x_dd_refresh_framebuffer` runs on every DirectDraw
  session setup and rewrites the word from scratch, so the new system-memory
  permission - added to `v9x_dd_stamp_engine_caps` alone - never reached the
  engine. `V9XHW.INI` reported it as allowed, because that reads the setting
  rather than the word. Both sites set it now.

- **`fail-hardware-aperture` covered four different failures, two of which
  set no stage code.** A failed DPMI selector allocation, and a live selector
  whose aperture has moved, both returned from `V9XMAPAPERTURE` without
  touching the stage code, so they inherited the aperture read's 3 and were
  reported as that. The helper's own header says it owns codes 4 to 7, but
  two of its exits set nothing. The allocation failure now sets 4, the moved
  aperture sets a new 11, and the PCI BAR read numbers its own four refusals:
  12 index, 13 the configuration read, 14 an I/O-flagged BAR, 15 out of range
  or misaligned, with 3 restored on success so that a later failure cannot be
  blamed on a read that worked. Any earlier investigation that trusted that
  stage name could have been looking at any of four faults.

- **Diagnostics written during a failing Enable never reach the disk.**
  Measured three times on a Millennium guest - the aperture value, a pre-call
  marker, and both again with an explicit profile flush - while the coarse
  stage, written later from `ddi.c`, lands every time. The serial trace is no
  alternative there: that guest's COM1 reads 0xFF from the driver's port
  check through both the File and named-pipe devices. So the stage code is
  the only way to get information out of that window, and `v9x_write_ini_key`
  now flushes anyway
  ([record](docs/decisions/2026-09-10-the-2064w-in-a-guest.md)).

- **Windows 98 has an inbox driver for the MGA-2064W**, and it is the one
  this family replaces: `DXMGA.INF` binds `PCI\VEN_102B&DEV_0519` to
  `MGAPDX64.DRV` with its own `mgapdx64.vxd` mini-VDD. So the guarded
  candidate's install route exists on a stock machine with no vendor
  download, and the accepted mixed-pair boundary is reachable. Observed in an
  86Box Millennium guest, where the candidate loads, refuses, and leaves
  Windows to fall back to VGA without corruption - the designed behaviour,
  seen on this chip for the first time. It is not a working driver on that
  card yet, and the refusal is not localised.

- **The Matrox candidate now covers a second chip: the original Millennium,
  MGA-2064W (`102B:0519`).** Its own BIOS, executed on an emulated CPU with
  I/O passed through to the card, advertises `0101h`, `0111h`, `0114h` and
  `0117h` with a linear framebuffer at `FD000000h` - this card's BAR1 base,
  which confirms the BAR inversion the chip was suspected of and settles
  whether the family's VBE path can reach it. `V9X_HW16_DEVICE` gains a
  per-chip `framebuffer_bar` (appended last and zero, so every other family
  reads BAR0 exactly as before), `V9XPCIREADBAR0` becomes `V9XPCIREADBAR` and
  computes its configuration offset from the index, and the policy backend
  takes both Millennium ids. The manifest claims three modes rather than
  four: this BIOS pads `0114h` to 1920 bytes per scan line where the driver's
  table asks for 1600, so that mode is not claimed until someone has set it.
  `VideoMemoryBytes` is a 2 MiB floor, because the BIOS's 8 MiB figure is its
  own BAR window and the aperture probe refused to call anything installed
  memory
  ([record](docs/decisions/2026-09-10-the-2064w-is-drivable-by-the-vbe-path.md)).
  Guarded candidate only: no mode has been set on this card by anything, and
  its framebuffer aperture accepted only 2-byte accesses in the mode it was
  measured in.

  **Corrected 2026-09-11, after this release shipped.** The last two
  sentences are wrong and the memory reasoning with them. The aperture is
  gated by CRTCEXT3 bit 7, which the firmware leaves clear; with that one bit
  set it round-trips 32-bit accesses, and the card measures 8 MiB by an alias
  probe that never touches the BIOS. The kit has since set `0117h` and
  restored it. The manifest is unchanged - 8 MiB is this card, and the part
  shipped in several memory sizes
  ([record](docs/decisions/2026-09-11-the-2064w-aperture-opens-with-mgamode.md)).

- **The DirectDraw probe's chain rung works; the faults it reported were its
  own.** It clears its Z surface through `DDBLT_DEPTHFILL` before attaching
  it and gives the wall and the sprite two different depths, so the depth
  test is exercised rather than degenerated by `sz = 0` everywhere. It also
  re-fetches its texture handles after a target switch, because the runtime
  retires them when it destroys the context to perform one, and re-creates
  them on the next `GetHandle`. With both, `ChainWallRaw` reads its texture's
  green and `Chain_x12..x48` read `930 806 682 620 464 341 217`, matching
  `Solo_x12..x48`: a depth-tested, alpha-blended, textured draw onto the
  primary chain's back buffer, drawn correctly. The suspicion that the driver
  was losing every texture at a switch was
  [filed and refuted the same day](docs/issues/2026-09-10-a-target-switch-loses-every-texture.md):
  a build that kept the records past `ContextDestroy` changed nothing, and
  `ChainTexDestroys=2` on it showed the runtime retiring the handles itself.
  The DDK's (handle, context) pairing stands.

- **The rasterizer changes are tested through the installed driver, not only
  through the benchmark.** The Trio64 guest has no S3D engine, so under
  `Direct3D=2` - `Direct3DMode=software` - every Direct3D draw goes through
  the CPU rasterizer. The DirectDraw probe on that guest reports zero
  differences across 1117 keys between the preceding HAL and the one carrying
  both rasterizer commits, `Result=COMPLETE` on both, with the four
  texel-alpha and mip rungs reading 0 in each as they have since 2026-09-07
  ([artefacts](docs/probe/software-d3d-2026-09-10-sampler/README.md)). The
  benchmark links the rasterizer directly and never loads the driver, so this
  is the run that shows the engine's own vertex conversion and caps still
  agree with it.

- **Destroying a surface now clears its texture records.**
  `V9xHalDestroySurface` forgets texture records by surface, alongside the
  colour-key equivalent it already called. Handles are retired with their
  context rather than with their surface, so an application that released a
  texture's surface without a `TextureDestroy` left the sampler a freed
  `lpLcl` to read. Found while investigating the entry above, and reachable
  before it.

- **Two reported Direct3D defects were both an uncleared depth buffer in the
  probe.** `IDirect3DDevice2::SetRenderTarget` never reaches
  `V9xD3dSetRenderTarget` on this runtime - measured, `ChainSetTargetCalls=0`
  - because the runtime destroys the context and creates another one on the
  new surface, after which the engine is pointed exactly at the back buffer
  (614400, pitch 1280, the offset the `Solo_*` rung's pixels land on). The
  black back buffer was the probe's own doing: it attaches a Z surface,
  nothing clears it, and every vertex carries `sz = 0`, which loses
  `D3DCMP_LESS` against a stored zero. A two-by-two over depth and the
  viewport identified depth as the cause, and with depth off the same blend
  produced the alpha ramp the chain was said to be unable to draw. Both
  [SetRenderTarget](docs/issues/2026-09-05-setrendertarget-is-accepted-and-ignored.md)
  and the [primary-chain blend](docs/decisions/2026-09-05-a-blend-onto-the-primary-chain-draws-nothing.md)
  are withdrawn
  ([record](docs/decisions/2026-09-10-the-render-target-switch-and-the-uncleared-depth-buffer.md)).
  No driver behaviour changed: the escape that serves the probe's counters
  gained six fields, of which the SetRenderTarget call count comes from the
  trace ring, because two more DWORDs in the diagnostics block took the
  shared block past the 4096 bytes the 16-bit side allocates.

- **The software sampler does its per-draw setup once per triangle instead of
  per pixel, and its bilinear weights now take one multiply.** The texture
  size, wrap mask, bilinear bias and format resolve once per triangle into a
  sampler object; a bilinear pixel's four texel decodes are inline rather
  than four calls with four format tests, and two of its four pitch
  multiplies are gone. All three texel formats share one decode path, because
  each is a field of w bits replicated to eight. Texture coordinates reach
  the sampler in texel units, scaled by a shift, and a WRAP coordinate is
  folded into the first repeat unconditionally. Measured against the commit
  below, on the Trio64 guest in RAM: bilinear 1.28x, depth-tested 1.25x,
  alpha-blended 1.19x, point-sampled 1.19x, untextured scenes unchanged.
  Cumulative on one boot against `c4988fe`: point 1.75x, bilinear 1.53x,
  depth 1.48x, alpha 1.41x, and about half of each in emulated video memory.
  Hashes and the host table unchanged, plus a new host test for bilinear
  ARGB4444, which the corpus did not cover
  ([record](docs/decisions/2026-09-10-rasterizer-texel-units-and-bilinear.md)).
  Two of the plan's proposals were declined on evidence: the nested lerp is
  not pixel-identical in its cheap form and costs more in its exact one, and
  packed two-channel arithmetic does not fit 32 bits at these weights.

- **The software rasterizer's inner loop drops three divides, two clamps and
  two per-pixel dispatches.** `(texel * channel + 127) / 255` becomes a
  multiply and a shift, exact for every value the modulate arm can form and
  asserted against that bound. The colour clamp happens once after the
  interpolator, instead of before modulate, before the blend and inside the
  packer. The depth comparison is a three-bit relation mask resolved per span
  - D3DCMP's own numbering, minus one - and the pixel format resolves to a
  pack and unpack pointer per span. Measured in the existing 86Box guests
  against `c4988fe`: point-sampled modulate 1.45x, bilinear 1.19x,
  depth-tested 1.18x, alpha-blended 1.17x, Gouraud 1.14x in RAM, each smaller
  on the VRAM target, with the host pixel table and all eighteen guest
  colour/Z hashes unchanged
  ([record](docs/decisions/2026-09-10-rasterizer-scalar-fixes.md)). The first
  attempt wrote the clamp and the divide as `static` helpers and was
  **slower** on every untextured scene: nothing in this build inlines - the
  HAL passes no `-o` option - so each helper became a call per pixel. Both
  ship as macros, and that cost is now recorded for the rest of the plan. No
  physical timing: BARRY has not answered since 2026-09-06.

- **Host builds work under Windows PowerShell 5.1 and PowerShell 7.** Shared
  setup preserves the compiler argument quoting; both compilers now use one
  portable source list, including the idle-wait tests. MSVC explicitly skips
  the Watcom-only x87 depth-conversion group, which Watcom still runs.

- **Current support and roadmap status** lives in [docs/STATUS.md](docs/STATUS.md),
  separating release defaults, opt-in work, recorded hardware coverage and
  pending validation. README and active plans now point to the current evidence.

Two faults found on physical Trio64 silicon, on both boards, 2026-09-06. They
arrived tangled and turned out to be separate:

- **A windowed DOS box drops the Trio64 out of enhanced mode.** The DOS VM's
  video BIOS writes 02H to ADVFUNC_CNTL, which the system VDD does not trap.
  The "doubled desktop" is the same DRAM seen as VGA planes, and a text
  command caught mid-transfer never completes. Measured with a new V86 port
  trace in the mini-VDD (`-IoTrace`, `V9XIOTR`); a mini-VDD that swallows
  that one write fixed it six times out of six
  ([issue](docs/issues/2026-09-06-dos-box-doubles-the-desktop-on-physical-trio64.md)).
  **Fixed: the mini-VDD now traps `4AE8H` in every build** and swallows a V86
  VM's write while passing the System VM and all reads through
  (`-NoShieldAdvFunc` for the A/B; `-ShieldAdvFunc` is gone). Measured
  harmless on a physical ViRGE/DX - whose BIOS, the trace shows, never
  touches the 8514/A ports - and on the 86Box Trio64 guest. Its first boot on
  a physical Trio64 is still owed
  ([record](docs/decisions/2026-09-06-advfunc-shield-ships.md)).

- **A8U4I5 with the PCI Trio64 hard-locks on framebuffer read-after-write
  under any driver**, Microsoft's included. Not a Velocity9x defect; the
  board's PCI configuration is the open variable
  ([issue](docs/issues/2026-09-06-a8u4i5-trio64-hard-locks-on-framebuffer-readback.md)).

Instruments from the same day, all kept: `V9XGDI /accel` bisection switches
(`/kinds`, `/ops`, `/nocompare`, `/noescape`) and a per-operation progress key;
`V9XTC32` register capture and `pump` arms; `GdiAccelSync`; `V9X_GDITEXTPROBE`
and `V9XGDI /textprobe`, built for BARRY and not yet run.

Added: **GDI acceleration build `gdi-accel-005` - text on the Trio64**,
compiled in and **off by default** (`GdiAccelText=0`)
([record](docs/decisions/2026-09-06-gdi-accel-005-text.md)). Ordinal 14 is a C
dispatcher now: a plain screen `ExtTextOut` goes through `DIB_ExtTextOutExt`
with two driver callbacks, and the DIB Engine's realized string bitmap is
expanded by the engine as a CPU-data rectangle fill through `PIX_TRANS`, mix
selected per pixel, clipped by the scissors. A callback that cannot draw flags
the string, and the dispatcher has the DIB Engine redraw it in software, so
text is never lost to the engine path. The ViRGE takes the same callbacks with
a `MONOSRCBLT` primitive, **verified on a physical ViRGE/DX** (A8U4I5,
`/accel` PASS, 84 of 84 strings by the engine, none fallen back) after a first
build that swapped the colours: the swap build 004's monochrome upload does is
GDI's mono-BitBlt convention, not the chip's, and a string bitmap must not be
swapped. The 86Box ViRGE mode matrix then passed 11/11 with text on. The
`/accel` harness draws text - opaque, transparent and clipped - and asserts
that bitmaps fire, that nothing falls back, and that ordinal 14 is reached on
every family. `V9X_GDI_STATS` grows eight text counters, to 220 bytes, and a
`V9X_GDITEXTDUMP` escape with a `V9XGDI /textdump` mode returns what the DIB
Engine handed the callback. **Verified on the 86Box Trio64 guest**, where the
first build drew half of every string: the command word lacked the 8514/A
plane-mode bit that declares CPU data as one bit per pixel, and the fix is
`53B3H` for `53B1H`. The Trio64 mode matrix then passed 11/11 with every 8-
and 16-bpp string expanded by the engine, and CrystalMark Retro's Text score
doubled on that guest, 2 to 4, with the other 2D scores unmoved
([record](docs/decisions/2026-09-06-crystalmark-86box-trio64-text.md)).
**Not yet run on a card**: the FIFO pacing is recorded as a hypothesis only
BARRY can answer, and the default stays off until it has.

## 0.7.0 - 2026-09-05

Two things in one release: the S3 Trio3D/2X on the hardware path, and
software Direct3D for every chip that has no 3D engine. The second was cut
as 0.7.0 on 2026-09-02 and never published beyond the private remote; the
three days of Trio3D work that followed are folded in here rather than
numbered separately, so this is the first 0.7.0 anyone downloads.

**The S3 Trio3D/2X works on the hardware path, on real silicon.** The part is
bound to the ViRGE/DX's engine and gets hardware Direct3D; Final Reality
completes at 3.21 overall and 3DMark 99 completes for the first time - 335
marks on its first clean run, and 313 against 475 in a same-boot comparison of
800x600 with 640x480. Six engine defects were found by that card and fixed, two
capabilities became chip-conditional, the DirectDraw probe grew from a checklist
into an instrument, and one of this release's own findings was measured,
published and then retracted.

### Three differences from the ViRGE

`5333:8A13` is an S3D part despite the Trio name, so it runs the ViRGE's hooks
and its register file. Three differences turned up under measurement, and none
of them were in 86Box's model - that emulator has no chip-conditional code in
its S3D unit at all, which is why every one of these needed the card.

- **The Trio3D's 3D stride register needs the texture pitch, not the screen
  pitch.** The ViRGE/DX derives texel addresses from the command word's size
  field and never consults it; the Trio3D's texture unit does. With the screen
  pitch there, 64-texel textures survived and 128- and 256-texel ones read as
  scrambled noise - which is why Final Reality, whose textures are all 64
  across, drew correctly on this card while 3DMark 99 drew every texture as
  static.
- **The Trio3D never sets SUBSYS_STAT bit 1**, the 3D-done bit the idle wait
  had been requiring since the emulator needed it. All 117 matrix cells and both
  render targets wrote a `_Dmiss` delta.
- **Two-pass trilinear filtering does not work on this part.** That is how
  trilinear was synthesised, and here every step came out carrying the channel
  neither of its two mip levels has.

### Six defects the hardware path had

Found on the card or on the emulated ViRGE and fixed:

- a texture bounded by a full mip chain it may not have, refusing surfaces near
  the top of VRAM for room they never used;
- the S3D wrap bit taken from `WRAPU`/`WRAPV` rather than the texture address
  mode, which drew Final Reality's tiled walls as black wedges;
- a flip reported complete before the CRTC had latched it, which is what made
  Final Reality flicker;
- the render target's address read once rather than per draw, so a flipping
  chain rendered into the page on the monitor from the second frame on;
- the mip-level gradients `TdDdX`/`TdDdY` never written, leaving the level
  index to drift with whatever the registers last held;
- blend pairs the S3D cannot express drawn opaque rather than skipped, which
  set 3DMark's lightmap pass fighting its own base pass for depth.

### 5:5:5 desktop support

The S3D writes ZRGB1555 and can write nothing else, so a 5:6:5 desktop
guarantees a mismatch on every 3D pixel. Three places had to agree - the mode
table, the DirectDraw pixel format and GDI - and the third was found last.
16-bit now resolves to 5:5:5 automatically under hardware Direct3D and is
offered on the settings page. Eight colour keys flipped on silicon and nine on
the emulator.

### Two chip-conditional capabilities

Both are negatives, both measured, and both leave the ViRGE/DX byte-identical
in the probe as the control:

- `V9X_DD_ENGINE_CAP_S3D_TWO_PASS` - trilinear degrades to bilinear on the
  level the chip selected rather than emitting a second pass whose result is
  wrong.
- `V9X_DD_ENGINE_CAP_S3D_UNLIT_ALPHA` - a blended `UNLIT` draw is expressed as
  `LIT` with `MODULATE` and a forced flat-white Gouraud colour, which is the
  same fragment through the pairing this card gets right. In a 3DMark run the
  whole population moved: 25,439 such draws before, zero after, at 472 marks
  against 475.

### The idle wait adapts to the part

`src/common/donewait.c` decides, in host-tested arithmetic, whether to keep
spinning for a 3D-done bit. One sighting retires the question for good; only 64
consecutive misses with nothing ever seen decide against a part. On A8U4I5 the
matrix block halves, 840 ms to 435 ms, with no pixel changed, and one 3DMark run
skips 483,491 full spins. On the emulator the rule never fires.

### The probe now sweeps ranges instead of sampling points

`V9XDDP.EXE` now walks spaces rather than sampling points: a 117-cell texture
matrix over size, format, layout and filter with the driver's own counters
beside every cell; an alpha transfer curve over three rotated operand pairs; a
forced-encoding sweep that puts all four encodings of the command word's alpha
field through one draw; a four-level mip ladder that says which level was read;
a sprite rung crossing depth, filter and shade mode; an alpha ramp interpolated
across one triangle over a textured destination; a census of every distinct S3D
command word a run used; and stage markers that survive the process dying.

`docs/probe/README.md` records what three of them taught: **an `*Ok` key that
tests the ends of a range is a regression check, not a measurement.** The
matrix's alpha cells drew over black, where "kept the destination" and "wrote an
opaque black box" are the same reading. `AlphaCurveOk` tested A=0, A=15 and
monotonicity, and passed a part whose every interior step was wrong. In both
cases the raw values were correct and present, and only the verdict was weak.

### Two findings retracted

Two decision documents of 2026-09-04 concluded that the Trio3D/2X performs no
alpha blend under any encoding of the command word's alpha field. **That is
wrong**, and both now carry a banner saying so. A controlled A/B - one variable,
two boots, the trilinear two-pass restored and nothing else touched - showed the
two-pass was not the cause. The correction that followed, that a power cycle
clears the state and a warm restart does not, was also wrong and is also
retracted in place: a deliberate power cycle left the card in the bad state,
and an A/B of the driver against the pre-diagnostics pair left the driver out
of it too.

What stands is that **the card has two states, and blending is correct in one
and wrong in the other.** `TexMatrixOk` reads 108 of 117 in the good state
and 90 in the bad one, with only the alpha cells and the sprite rung moving;
mip selection and every unblended cell are the same in both. The trigger is
not known. All three transitions on record coincide with the machine going
away and coming back, once with nothing executing. A VGA survey was taken in
the bad state and in the good state on two boots each side of a warm restart:
nothing it reaches distinguishes the states - the two bytes that differed
between the first pair differ again between two good boots - and it does not
reach the S3D engine, where a blend fault would live
(`docs/decisions/2026-09-05-a-register-capture-of-both-trio3d-blend-states.md`).

Every Trio3D alpha measurement this project took before 2026-09-04 was made in
the bad state, and a Trio3D result is only meaningful beside its `TexMatrixOk`.
The documents stay where they are, because a wrong decision is still evidence
about how it was reached.

### 4 MiB limits the usable resolution

At 800x600x16 with a triple frame buffer and a 16-bit depth buffer, 3DMark 99's
own page reports 3,750 KB of 4,096 KB consumed before a single texture. Under
that pressure DirectDraw cannot lay mip chains out contiguously, the engine's
contiguity guard correctly falls back to a plain filter, and the card is barely
mipmapping at all - 2,861 mip-filtered draws against 418,390 at 640x480. That
is what the race scene's chevron-noise ground was, and it was never a sampler
defect. **640x480 is the resolution for a 4 MiB Trio3D**, and any number taken
at 800x600 on this card was taken with mipmapping mostly disabled.

### Not established

- **3DMark 99's sprites still draw in opaque black rectangles.** Four rungs
  built to reproduce that have failed; each found a real defect and none of them
  was this one.
- **`SetRenderTarget` is accepted and ignored** on the emulated ViRGE/DX: it
  returns success and the driver keeps drawing on the previous target. Filed,
  unfixed, and not chip-specific.
- **A blended draw onto the primary chain's back buffer writes nothing**, while
  an opaque draw onto the same surface through the same registers writes
  correctly. The destination base, pitch, size and stride register were all
  measured correct. On silicon it has run only in the bad blend state, where a
  blend that draws nothing cannot be told from the blend fault.
- The partial-alpha fault on the Trio3D has an exact shape - destination term
  exact, source channel saturated, destination value duplicated into whichever
  channel neither operand uses - and no explanation.

### Software Direct3D on cards with no 3D engine

`Direct3D=2` on the Velocity9x page serves Direct3D from a CPU rasterizer on any
supported chip - depth-tested, textured, Gouraud-shaded triangles on a Trio64
and an ATI Mach64 VT2, neither of which has a 3D engine, and the second of
which has no 2D engine this driver drives either. It is slow, its capabilities
advertise exactly what it renders and nothing more, and **it has never been
timed on a period machine.**

### The rasterizer

`src\display32\d3d\d3d_raster.c` is a leaf translation unit: it includes nothing
but `velocity9x\types.h`, holds no state, touches no register, and takes its
render target as a pointer, a pitch and an extent. The host suite
(`tests\host\test_d3d_raster.c`) holds it to properties rather than to a
picture, because a rasterizer can look right long before it is right - two
triangles sharing an edge cover it exactly once, nothing is written outside the
target, a flat-coloured triangle is exactly one colour, the same triangle in all
six vertex orders draws identical pixels, and a refused triangle draws nothing.

Three numbers in it are decisions rather than mechanics, and each one is an
overflow bound:

- **Integer arithmetic only, with 28.4 screen coordinates.** The float-to-fixed
  conversion stays in the engine, where the `#pragma aux` fistp lives, so the
  arithmetic compiles under both host passes.
- **The render target is capped at 2048 pixels.** Every interpolation product is
  bounded by coordinate squared; 32752 squared is 1,072,693,504 and fits, 4096
  pixels would not, and the failure would be wrong spans on large modes only,
  with nothing reported. `d3d_soft.c` asserts its own limit against it at
  compile time.
- **Only one texture repeat, so the sampler clamps and the caps publish CLAMP
  but not WRAP.** The edge interpolator's denominator is at most 32752, so
  anything it carries must stay under 65566; depth already sits at 65535 against
  that bound.

Coverage is pixel centres with half-open intervals in both axes. Depth is
16-bit with all eight comparison functions and a write mask, numbered as
`D3DCMP_*` numbers them so the engine passes the render state through
untranslated - and asserts that equality at compile time, because the ViRGE
needs a real table there and using the wrong order draws a scene inside out
rather than one that is missing.

### Capabilities that match the engine

Published: RGB, float TL vertices, execute buffers from system memory, texturing
from device memory, 16-bit target and Z, all eight comparison functions, flat
and Gouraud RGB, specular Gouraud, subpixel, NEAREST and LINEAR, DECAL and
MODULATE, POW2 and SQUAREONLY, CLAMP.

Withheld, each with its reason in the file: WRAP, the four mip filters, texture
alpha and every alpha blend cap, PERSPECTIVE, COPY, and the fog caps - fog
works, but on one probe rung, and it goes in when there is a ladder behind it.

### The mode selector now offers Software

The Display Properties selector offered Hardware and Disabled, and greyed itself
out entirely on any chip without an S3D unit - which is every chip the software
mode exists for. **The mode worked; only the page could not reach it.**

It now offers Software everywhere, and Hardware only where there is hardware
Direct3D to select: a ViRGE sees three entries, a Trio64, ATI or VESA card sees
Software and Disabled. A card on the default shows what it is actually doing -
"Not advertised on this chip" - and carries the loaded value, so opening the
page and pressing OK still writes nothing.

### The probe was checking against the wrong pixel format

Every "did it draw the right colour" verdict compared against a ZRGB1555
literal, against an RGB565 render target. Those constants were written to match
what the ViRGE's triangle engine writes rather than what the surface declares,
and with a second engine present they reported a correct engine as failing -
three keys at once, each needing to be read back out of its raw value by hand.

The probe now reads the target's `ddpfPixelFormat` and derives its expectations,
comparing channels as 0..255 where a range is wanted, and records what it used.
Six Trio64 keys went from 0 to 1 with no driver change, specular Gouraud among
them - the core folds specular into the vertex colour, so the software engine
had it without a line written for it.

**Six ViRGE keys went the other way, and that one is a real defect rather than
a probe fault.** Every ViRGE key whose expected colour is blue still passes,
blue being `0x001F` in both formats and the one colour they agree on; every key
carrying red, green or white fails. The S3D unit writes ZRGB1555 into a surface
described as RGB565, which README has recorded as an unresolved mismatch since
the first Direct3D work and which is now measured from outside instead of noted
in a comment. 86Box cannot settle whether the chip has a destination-format
control its model omits, because the emulator is the model. The physical ViRGE
can.

### Found by measuring

- **Both engines lose the last row and column** of a full-target triangle. The
  chip-neutral clipper cuts to `extent - 1` - the last row's top edge, not its
  bottom - so under a pixel-centre coverage rule that row is never covered. On a
  full-screen target it is a one-pixel dark line down the right and along the
  bottom. Filed, not fixed: widening it changes what every engine receives and
  moves the rasterizer's overflow bound with it. A host test pins the count at
  2047 rows of 2048 so it cannot change silently.

### Measured

`Win98SE-Trio64` (S3 Trio32/64, 800x600x16) and `Win98SE-Mach64VT2` (ATI Mach64
VT2, 1024x768x16, `Acceleration=none`), with `Win86SE` (ViRGE/DX) as the
hardware control on the same binaries. Both software-mode guests report
`Result=COMPLETE` with `TexFormatCount=2` and every functional key passing:
flat and subpixel triangles, triangle shape, all four depth-compare rungs, the
write mask, specular, fog, both texture formats and a context cycle. Published
caps identical on both.

**Ironfield 1.2**, 279 frames at 16 FPS, 640x480, Video + BltFast on the Trio64
in software mode - matching that machine's own benchmark history of 16 FPS and
274-280 frames on every run since 2026-08-14. The software Direct3D mode costs
the DirectDraw path nothing.

### Not established

No performance measurement of the rasterizer exists at all. Every run above is a
64x64 render target on an emulated guest; the plan's first work-order item - the
VRAM versus system-memory write cost on BARRY - has still never been run, and no
period machine has drawn a triangle through this. No VBE guest ran either: that
image is in a broken display configuration and reviving it is its own task, so
the VBE claim rests on the ATI run plus a code chain rather than on the card.

### The download page understates what was tested (software-mode packages)

Every package's `MANIFEST.TXT` still carries the hardcoded
`Status: HOST-AUDITED; GUEST ACTIVATION NOT YET TESTED`, and the release index's
`Tested` column reads it back, so the 0.7.0 download page says that of the `s3`
and `ati` zips as well - both of which were installed, activated and probed on
guests for this release. The error is in the safe direction and it is filed:
[`docs/issues/2026-08-30-package-status-string-is-hardcoded.md`](docs/issues/2026-08-30-package-status-string-is-hardcoded.md).

It is not fixed here for the reason that issue gives - changing what goes inside
the published artifacts on the run that publishes them is how a release ends up
not being the thing that was audited - and because what each family should claim
is a statement about its testing history rather than a mechanism. The section
above is the accurate record for this release.

## 0.6.5 - 2026-08-30

**A Direct3D selector, a depth-clear path, and the measurements behind both.**
Direct3D can now be turned off from Display Properties, and depth-buffer clears
go through the blitter instead of a CPU pass over the aperture. Both are backed
by guest runs rather than by code reading — and the depth-clear measurement did
not come out the way the plan predicted, which is recorded below rather than
smoothed over: it is a trade-off that leaves Final Reality's composite score
unchanged, not the improvement it was argued to be. It is kept because S3's own
ViRGE driver in the Windows 98 DDK serves the same call, and because every bias
in the environment it was measured in favours the arm it lost to.
**`DDBLT_DEPTHFILL` is implemented, and a controlled A/B shows it is a
trade-off rather than a win.** Nothing in `src/` handled it and `DDCAPS_BLTDEPTHFILL` was
not advertised, so DirectDraw locked the Z buffer and wrote every word itself.
That clear is now one blit.

Final Reality on `Win86SE`, same guest and session, the only difference being
which `V9XHAL.DLL` was installed - the control built by checking the five HAL
sources out at the parent commit, so it is this branch minus the depth fill:

| Test | No depth fill | With depth fill | Change |
|---|---|---|---|
| 25 pixel | 23.42 Kpolys/s | 23.35 | -0.3% |
| Robots | 9.41 images/s | 11.54 | **+22.6%** |
| Fill rate | 67.66 Mpixels/s | 42.09 | **-37.8%** |
| City scene | 11.38 images/s | 15.50 | **+36.2%** |
| **3D performance** | **1.97 marks** | **1.96** | flat |

The control reproduces the previously recorded run to within 1% on every test,
which is what makes this a measurement rather than two sessions compared; the
depth-fill column was run twice.

**This refutes two of our own claims.** The Final Reality plan said the
28.54 to 23.62 Kpolys/s drop was the cost of depth work "and partly of
DirectDraw clearing the depth buffer on the CPU every frame", separable only by
implementing the fill and re-running. They are separated now and **25 pixel does
not move** - essentially none of that 17% was the clear. And the argument for
the change was that one blit per frame must beat one CPU pass per frame. For
two scenes it does, by a lot; for the fill-rate test it loses by a lot. A
plausible mechanism is that the clear now waits on engine FIFO slots and
serialises against queued S3D work where the CPU pass touched no engine at all,
but that is a hypothesis and nothing here measures it. This is the failure mode
the hybrid-3D plan's own "honest reading of where the wins are" warns about.

**A third arm rules out the obvious fix.** Routing the clear through the
driver's own `v9x_cpu_fill` - keeping the single callback, skipping the
blitter - tests whether the gains came from avoiding DirectDraw's Lock/Unlock
round trip rather than from the engine. They did not: Robots falls to 9.29 and
City scene to 11.13, both slightly *below* the control, and the composite is
**1.92, the worst of the three arms**. A driver-side CPU clear is worse than
not implementing the path at all. The gains are the engine's, the fill-rate
cost is the engine's, and the two cannot be separated by choosing a different
fill path. 25 pixel reads 23.4 in all three arms, which is a third independent
confirmation that the clear was never part of that figure.

**The fill-rate figure may not be about this chip at all.** Published
per-cycle rates for the family put a 55 MHz ViRGE 325 at 44 Mpixels/s on
non-textured polygons with no Z buffer, 23 with Z, and single figures for
perspective-correct textured pixels, the DX improving only the textured path.
Both the control's 67.74 and the depth fill's 42.09 sit above that
non-textured, Z-disabled ceiling. Not a like-for-like comparison - Final
Reality's `Fill rate` is its own composite and the source is a secondary one -
but the order of magnitude says 86Box is not reproducing this part's fill
throughput, and a 38% swing in a figure the silicon could not produce is not a
performance result about the silicon.

**It is kept, for reasons other than this benchmark.** S3's own ViRGE driver
in the Windows 98 DDK advertises `DDCAPS_BLTDEPTHFILL` and handles
`DDBLT_DEPTHFILL` in the same branch as its colour fill, from `dwFillDepth`,
which is the design this driver reached independently - so serving it is what
the vendor's driver for this exact chip does. And every bias in the environment
these numbers come from favours the arm that won: 86Box's framebuffer is host
RAM, so the CPU clear never pays the uncached-aperture cost the MTRR work
identifies as dominant on real targets, and its shared 2D/3D command FIFO runs
on a host thread woken by status reads, which is a scheduling artefact with no
silicon analogue. On silicon the CPU arm gets worse and the engine arm's
advantage grows. No physical ViRGE is recorded on this project, so that cannot
be checked.

**Two claims from other people's ViRGE drivers are recorded as issues rather
than acted on.** XFree86's S3V documentation states that "32bpp is limited to a
width of < 1024 pixels. (1024x768 is not possible, even if you have the memory.)
This is a hardware limit of ViRGE chips" - and this driver ships 1024x768x32 on
both S3 chips. Four guest runs and a host-side `PrintWindow` capture say the
mode is clean, which is good evidence against a driver fault and none at all
against a silicon limit 86Box need not model; this driver also sets modes
through the VBE BIOS rather than programming the CRTC as XFree86 does, so the
limit could be real and not reached this way. No physical ViRGE exists here to
settle it. The same page describes how XFree86 accelerates 32 bpp despite the
chip having no 32-bpp engine support - run the engine in 16-bpp mode over the
same rectangle at double width - which is a shipped-driver route to lifting
this driver's "no acceleration above 16 bpp" limitation, and is now recorded as
such. Both in `docs/issues/2026-08-30-virge-1024x768x32-xfree86-limit.md`.

The XFree86 and X.org `fifo_aggressive` / `fifo_moderate` /
`fifo_conservative` options are **not** about the engine command FIFO this
driver waits on - they set the threshold at which the *pixel* FIFO takes over
the internal memory bus to refill scanout. Worth stating because the names
invite the confusion.

**One thing S3's driver does that this one does not**, found in the same
reading: it prefixes every blit with a dummy 1x1 screen-to-screen blit, "in
case we were called right after a 3D command". A depth clear is exactly that
case. Nothing has gone wrong here, but that is one emulated chip and 86Box need
not model an erratum the vendor handled in software. Filed as
`docs/issues/2026-08-30-virge-2d-after-3d-dummy-blit.md` rather than copied,
because seven FIFO slots on every blit is a real cost and one comment is not
evidence the fault exists on silicon.

No engine code changed. A depth clear is a solid fill of a 16-bit surface, and
`v9x_virge_fill` was already parameterised on the destination offset, that
surface's own pitch and the bytes per pixel, taking its value from
`bltFX.dwFillColor` - a union member sharing its DWORD with `dwFillDepth`. The
new `v9x_depthfill_body` validates the request and hands the existing fill a
different offset and width. It requires `DDSCAPS_ZBUFFER` and refuses system
memory, does not consult the screen depth (a Z surface is 16-bit whatever the
desktop is), and gets the depth width from
`v9x_d3d_depth_bytes_per_pixel()` rather than writing a literal 2 in a
chip-neutral file - which is the mistake the comment on `depth_bits_per_pixel`
already records having made once. Zero from that call means the chip has no D3D
engine, so the fill declines everywhere but the ViRGE.

**`DDBLT_DEPTHFILL` is `0x02000000`, not `0x00002000`.** Its neighbours in the
flag list suggest the latter and that is what writing it from memory produces;
the `DDBLT_` flags are not densely packed. Both it and
`DDCAPS_BLTDEPTHFILL` (`0x10000000`) were read out of the Win98 DDK's
`DDRAW.H` before either was used. A wrong flag would have made the driver
decline every depth fill while advertising a capability it never received a
request for, and the symptom would have been "no change" rather than anything
resembling a bug.

**The fill is pixel-verified on `Win86SE`.** The probe gained a depth-fill test
that fills a 64x64 video-memory Z surface twice and reads the words back:
`ZDepthFillRaw=4660` (0x1234), then `ZDepthFill2Raw=43981` and
`ZDepthFillCornerRaw=43981` (0xABCD at both the origin and 63,63),
`ZDepthFillOk=1`. Two values because freshly allocated video memory holds
whatever the last owner left and a single-value test passes by accident; two
positions because a rectangle blit with the wrong pitch writes the first row
and nothing else.

**That test proves less than it appears to, and that was measured rather
than assumed.** A HAL built without any of this work - no depth-fill
body, no cap - passes it identically: DirectDraw emulates `DDBLT_DEPTHFILL`
when the driver declines and returns `S_OK` either way. So the test establishes
that the fill is *correct*, not that the driver *performed* it. A path that
returned `DDHAL_DRIVER_NOTHANDLED` on every call would pass unchanged. The
discriminator is counting `Blt` callbacks across the two builds, and that has
now been done.

**The driver does perform the fill, and it goes to the blitter.** The probe issues exactly two
depth fills; the control build reports `CountBlt=7` / `CountBltEngine=7` and
the depth-fill build `CountBlt=9` / `CountBltEngine=9`. So without the cap
DirectDraw never dispatches the call to the driver at all - the control is a
true "driver does nothing" arm - and with it, both fills reach the engine
rather than falling through to the CPU fallback, since that counter only rises
where `ops->fill` returned `V9X_BLT_DONE`. With `EngineFifoTimeouts`,
`EngineIdleTimeouts` and `EngineResets` all zero, the fill-rate cost is not
timeouts or recovery: it is the ordinary price of reserving FIFO slots and
writing seven registers per clear, where the CPU pass it replaced touched no
engine state at all.

The new cap goes into the word DriverInit publishes for the whole binary, so it
is a regression risk on the three families that cannot serve it. Checked on
`Win98SE-Trio64` rather than reasoned about: the 16-bit clamp reassigns
`dwCaps` and drops the bit with the rest, `D3DHalFound=0` and
`D3DDeviceCount=3` as before, and DirectDraw stayed fully working through fill,
copy, all four overlap cases and `RestoreHr`.


**A SYSTEM.INI key now decides whether the driver advertises Direct3D at all.**
`[Velocity9x] Direct3D=1` clears `V9X_DD_ENGINE_CAP_D3D` before the engine
descriptor is stamped into the DirectDraw shared block, and the clamp in
`V9xDdCreateDriverObject` - which has nulled `GetDriverInfo` and both `lpD3D*`
pointers for a family without that capability since the Trio64 shipped - does
the rest unchanged. No 32-bit code was touched and the shared-block ABI did not
move.

**Measured on `Win86SE`**, nine probe runs across seven boots
(`docs\decisions\2026-08-30-d3d-mode-disabled-gate.md`). On a fresh boot with
the key set: `D3DHalFound=0`, `TexFormatCount=0`, `D3DDeviceCount=3` - Ramp,
RGB and MMX, with no `Direct3D HAL` entry offered at all - while DirectDraw
stays fully working, overlap cases and `RestoreHr` included. Setting the key
back to 0, after three reboots on 1 and 2, reproduced the baseline ladder with
**no differing `D3D*` or `Tex*` key**, texture handles included.

This is mode 1 of the four in
`docs\plans\s3-trio64-voodoo2-hybrid-3d.md`, done first because it is the only
one that exercises the whole settings-to-published-caps chain with a back end
that already works. The key takes the plan's own mode numbers, with 0 for the
chip's own engine: 0 hardware (the default, and what an absent key means), 1
disabled, 2 software, 3 hybrid, 4 offload. The last three are recognised and
not implemented, and resolve to advertising nothing rather than silently
falling back to hardware - a mode selector that answered "software" with the
chip's own engine would be the same defect as advertising a capability that is
not implemented, which this driver has already shipped once.

The decision is `src\common\d3dmode.c`: pure arithmetic, no OS, no DirectDraw
header, host-tested over the whole 16-bit request space against both values of
"does this chip claim D3D". The property that test exists for is that no
setting can make a card without a 3D engine advertise one. `dd16.c` reads the
key at Enable, in the same section and by the same call as the `GdiAccel` keys,
and `check-tree.ps1` now asserts the two readers agree on the file and the
section and that the header still spells the mode numbers the plan does.

**Two of that plan's open questions were closed by reading the tree**, and the
plan records both with citations. `V9X_DD_ENGINE_VALID` is set only for a chip
with a `fill_engine_descriptor` hook, which the VBE, both ATI and the Matrox
devices do not have - so a software engine selected on `engine_type` could
never resolve on any of the families it is meant for, and the selector will
have to test the mode first. And the shared block is allocated and stamped on
the `DDGET32BITDRIVERNAME` escape, before DriverInit, so there is already a
place to put a mode where the 32-bit side can see it at publish time.

**A mode change only takes effect after a restart.** A
re-enable does move the driver - `Direct3DMode=` republishes and
`TexFormatCount` drops to 0 - but DDRAW keeps offering the `Direct3D HAL`
device it enumerated from the previous session, and every attempt to use it
then fails with `E_NOINTERFACE`. That is worse for an application than either
end state: it selects a hardware device and dies instead of falling back to
`RGB Emulation`. Only a fresh boot removes the entry, so that is what the
settings page will say.

**The Display Properties page now sets it.** The Direct3D row became a combo
box - replacing the value label in place, because the page's height budget has
no room for a new row and the 640x480 case is the binding one. It reports the
resolved state from a new `Direct3DMode=` key in `V9XHW.INI`, kept separate
from the chip module's existing `Direct3D=` because it has to tell "this card
has none" apart from "you turned it off".

The list holds only the modes this build implements. Offering "Software"
before it renders a pixel would be the same defect as publishing a Direct3D
capability the engine does not serve, in a different surface. `Apply` stays
greyed until the selection differs from the file, so opening the page and
pressing `OK` writes nothing - including on a machine whose key somebody set by
hand to a value this build does not know, which gets its own list entry rather
than being silently replaced by the default. On a chip with no 3D engine the
control shows the card's answer and is disabled, since no value could change
anything.

Driven on the guest at both 1024x768x16 and 640x480x16: select Disabled,
Apply, reboot, `D3DHalFound=0` / `D3DDeviceCount=3` / `TexFormatCount=0`;
select Hardware, Apply, reboot, back to `1` / `4` / `2` with no functional key
differing from the baseline. The page fits at 640x480 with OK/Cancel/Apply on
screen, and the dialog is 211 dialog units before and after.

**The no-3D path was run on `Win98SE-Trio64` and `Win98SE-Mach64VT2`**, the
second having no engine descriptor at all, so `engine.flags` never gets
`V9X_DD_ENGINE_VALID`. Absent, `0` and `1` all report `none` - a setting cannot
change what the card cannot do - while `2` reports `mode-unimplemented`,
because the unwritten modes exist for exactly that card and answering them with
the card's answer would be the wrong report the day one of them lands. The
control is greyed on all four, `Stage=enable-ok` throughout, and DirectDraw
stays fully working through fill, copy, overlap and `RestoreHr`. That leaves
`vbe` as the only family not booted with this change; it takes the identical
code path to `ati` and differs only in which chip module is linked.

Those runs also shortened the `mode-unimplemented` wording, which was longer
than the 166-dialog-unit selector and rendered as "...none adver" on the guest.
Nothing but looking at a guest would have caught it - the string fits every
buffer it passes through.

**A fourth probe hygiene rule: one `V9XDDP.EXE` run per boot.** A second run in
the same boot reported `ExitCode=0` and `Result=COMPLETE` and three false
failures - `D3DZWriteMaskOk`, `D3DDepthFogOk` and `D3DVertexAlphaBlendOk` all
went 1 to 0 against a run that had just passed. Rebooting and running once more
restored every key. The three existing rules - delete the result, check the
exit code, check `Build=` - all pass on that bad run, so none of them catches
it.

**Hardware Z-buffering works on the ViRGE. One expression was stopping
it.** The driver had advertised depth testing since the first Direct3D
work - `D3DPRASTERCAPS_ZTEST`, all eight compare functions, `DDBD_16`, and a
fully validated attached depth surface - and then written `Z_BASE = 0`, set no
depth bits in the command word and never read `context->zbuffer`. That was
implemented earlier on this branch and could not be shown to work on any guest.
It now does: both probe designs pass on `Win86SE` with `D3DZCompareOk=1` and
`D3DZWriteMaskOk=1`, and Final Reality renders its Robots and City scenes
through it.

The emit reserved `z_active ? 18 : 15` FIFO slots. Eighteen is the right count -
fifteen writes from `COMMAND` through `Y01_Y12` plus the depth triple - but
`SUBSYS_STAT` carries the free-slot count in five bits at 12:8, and 86Box's
model sets bit 12 on both arms of that read, so the count it reports is always
exactly 16. Fifteen is satisfiable and eighteen never is. Every depth-enabled
draw spun out the full FIFO spin limit, counted a timeout, reset the engine
through CR66 bit 1 and abandoned the triangle - while `SetRenderState`,
`BeginScene`, `DrawPrimitive` and `EndScene` all returned `S_OK`. Measured
before the fix: seven FIFO timeouts and seven engine resets for the seven rungs
of the two depth ladders, against zero on the same run's depth-off draws.
Sixteen is also the S3D FIFO's own depth in that model, so a reservation larger
than the FIFO holds was not going to work on the chip either. The fix takes the
eighteen in two bites, in both the main emit and the trilinear second pass,
leaving the depth-off reservation and its stall behaviour untouched.

**Three hypotheses were disproved on the guest**, all of them from the handoff written
the day before, and all of them plausible readings of the same symptom -
`S_OK` everywhere, no pixels, driver counters that did not move. The runtime
*does* hand the driver a depth surface, once, in `lpDDSZ` at context creation,
and the driver accepts it: `D3dDepthOffered=1`, `D3dDepthAccepted=1`, caps
`0x10024000`, above the visible page. `CreateDevice` *does* create a context -
`D3dContextCreates` is 3, not the 2 recorded, which had been read off a run of
a different probe design. And the device it returns *is* the HAL:
`IDirect3DDevice2::GetCaps` fills the hardware half for both devices. The one
finding that survives is the negative one, that `SetRenderTarget` never reaches
`V9xD3dSetRenderTarget` - but the conclusion drawn from it does not, because
the runtime binds the depth surface on that path by creating a new context
instead.

Instruments added, because each of those wrong readings was cheap to make and
expensive to undo: `d3d_diagnostics` gains `depth_offered`, `depth_accepted`,
`depth_reject`, `depth_caps`, `depth_offset` and `depth_pitch`, with every arm
of the depth validation recording its own reason, so "the runtime never passed
a depth surface" and "the driver refused the one it passed" stop looking alike;
the probe reports which device `CreateDevice` actually returned rather than the
GUID it asked for; and the private-target-and-device probe design is restored
behind `/zprivate` with its own key prefix, mutually exclusive with the
working-device design so the cumulative counters belong to one of them.
`V9X_DD_TRACE_ID_COUNT` goes 50 to 51 in the same ABI bump (`2026083001`):
`v9x_trace_count` indexes `counters[]` with the trace id and the highest id is
50, so `D3dPrimitiveReject` was silently never counted.

**Final Reality's Robots and City scenes ran for the first time.** All four 3D
tests at five repeats, 14.5 minutes, 1024x768x16:

| Test | Raw speed | R marks |
|---|---|---|
| 25 pixel | 23.62 Kpolys/s | 0.76 |
| Robots | 9.45 images/s | 2.45 |
| Fill rate | 67.74 Mpixels/s | 14.66 |
| City scene | 11.46 images/s | 2.84 |
| 3D performance | | 1.97 Reality marks |

Zero FIFO timeouts, idle timeouts, engine resets and context rejects across
2,697,602 primitive calls, with 8 context creates against 8 destroys. FR
attached a depth surface five times and the driver accepted all five, at a
pitch of 1280 - 640 x 2, matching FR's fullscreen 640x480 - above the front and
back buffers. That is what says the reservation fix holds under real load
rather than only on a seven-rung ladder.

25 pixel fell 28.54 -> 23.62 Kpolys/s, and the plan predicted it: the 28.54 was
measured while the driver advertised depth and did none, so those triangles
paid for no depth registers, reads or writes. How much of the 17% is the
still-absent `DDBLT_DEPTHFILL` (DirectDraw clears depth on the CPU every frame)
and how much is the depth work itself is not separated by this run.

Two things in FR's results screen mislead, and both are now written down:
`Save results to file` does not save your run - it writes the database entry
currently selected, which with `Compare to: <none>` is FR's built-in
`Baseline system (Pentium 150 MHz + S3 Virge/VX)` reference, whose numbers are
plausible enough to be mistaken for a measurement. And `Visual appearance` read
exactly `74.07 %` both before and after hardware depth started working, as does
that same ViRGE reference entry; the consistent reading is that it is derived
from the advertised capability set rather than from rendered pixels. Not
proven, and flagged as not proven - the plan had been treating it as a
correctness indicator.

The procedure for driving Final Reality, whose absence blocked this step for
two weeks, is now `docs/specifications/final-reality-101-runbook.md`.

Filed rather than fixed: `V9XTRACE.EXE` faults in KRNL386 once DirectDraw has
run on the boot, hanging or dying in the profile-write path. It costs the exit
code and sometimes the counter and ring tail; the scalar counters are written
first, and every value quoted here came from a file a faulting run had already
produced. Whether it predates this work is not established.

Still open, and stated as open: **depth gradients are exercised and
unverified.** Both probe ladders hold `sz` constant, so `dZdX`/`dZdY` are
written but never checked against a slope, and 86Box doubles a triangle's start
depth but not its per-pixel X gradient, so a sloped pixel test on that guest
would measure the emulator. Final Reality did drive them across sloped scenes
for 2.7 million primitives without a fault, but "it did not fault" is not "it
computed the right depth". Closing it needs a 86Box fix or a second ViRGE
target.

**The Direct3D block is split into a chip-neutral core and one engine, with
no change the probe can see.** Roadmap Track B, executed ahead of its scheduled slot -
the plan puts it immediately before the 3dfx D3D phase precisely so the
abstraction is drawn around two engines rather than one, and that caveat still
stands.

`d3d_virge.c` was 1,733 lines carrying both halves. It is now `d3d_core.c`
(context pool, texture handle table, render state, software clipper, all
sixteen DDHAL entry points), `d3d_virge.c` (the S3D triangle emitter, the
sampler's format test, the device caps) and `d3d_internal.h` (the boundary).
All 63 MMIO writes are in the engine; the core names no chip's register
vocabulary. Every moved body is byte-identical - the split was done by a script
copying verified line ranges, not by retyping.

The vtable draws at `draw_triangles(context, vertices, count)` and never at
register level, which is the one rule fixed hardest: the ViRGE is immediate-mode
and would take either, but every plausible next engine is a command-stream
engine that needs a run of work to build one packet from. All three call sites
mapped onto it without contortion.

The part the roadmap did not anticipate: most of what was chip-specific here is
*numbers*, not code - target depth, pitch ceiling and alignment, dimension and
texture-size caps, the coordinate guard band - all literals sitting inside
otherwise chip-neutral routines. They became a `V9X_D3D_ENGINE_LIMITS` struct
rather than five more function pointers, on the grounds that a second engine
changes the values and nothing about their use.

**The first attempt broke Direct3D and the gate said it had not**, which is
the part worth recording. Selecting the engine inside `v9x_d3d_publish` looked
right - it is the selector the draw path uses - but DriverInit runs before the
16-bit side fills the engine descriptor, so it resolved null, published
nothing, and DDRAW enumerated no hardware Direct3D device at all. The first
gate run missed it because the 2026-08-13 handoff's probe path is stale: the
result moved to `C:\V9XDIAG\V9XDD.INI`, and fetching the old path returned an
untouched file from an old build, so "byte-identical" was two reads of the same
stale bytes. The `Build=` stamp is what exposed it. The gate is now run by
deleting the result first, checking the exit code, and requiring the `Build=`
key to match.

Caps publication cannot be chip-selected at DriverInit, so it publishes the one
D3D engine the binary carries - exactly the pre-split behaviour, with the
16-bit clamp remaining the capability authority. `v9x_d3d_engine()` survives as
the draw-time selector, where the descriptor is valid and every entry point
declines for a chip with no engine; that second gate is real, it just cannot
also be the publish-time one. A second D3D engine has to fix publish-time
selection on the 16-bit side, by stamping `engine_type` before DriverInit.

Measured on the corrected gate, against a pre-split baseline built from a clean
worktree: every functional key identical, rendered pixels included
(`D3DTrianglePixelRaw=31744`, `D3DBaseTextureRaw=992`, `D3DMipmapLevelRaw=31`,
`D3DTrilinearRaw=495`, `Tex4444Raw=992`). What differs is the build stamp, six
kernel heap addresses, two texture handles, a HAL code pointer, a vblank
sampling race and three millisecond timings. On `Win98SE-Mach64VT2`:
`Stage=enable-ok`, `D3DHalFound=0`, `TexFormatCount=0`, and DirectDraw fully
working. `check-tree.ps1` holds the core/engine boundary in both directions
rather than leaving it to review.

**Two S3 chips were supported all along and nobody had checked.** Reading the
PCI Data Structure out of all 41 S3 option ROMs in the local 86Box tree showed
that the Trio64V+ 86C765 and the Trio32 86C732 both publish `5333:8811` - the
id the `trio64` chip has bound since the family merge. An 86Box Trio32 guest
then confirmed it end to end: the driver enables, CR36 decodes the card's
2 MiB, and eight of the nine declared modes that fit pass with GDI
acceleration. Neither part needed a line of code.

The same survey corrected two labels the field-report tool had been printing
from documentation - 86C765 is at `8811`, not `8814`, and `8814` is the 86C767
Trio64UV+.

Added, off the back of it, a **device-id alias** to the family manifest: a PCI
id a chip's own code drives unchanged, bound so it installs but deliberately
not a chip. Chips must carry a VM target and are covered by the mode matrix;
an alias has run nowhere and the schema keeps that distinction rather than
leaving it to prose. Five are declared on the Trio64 - `8810`, `8812`, `8813`,
`8814` and `8901` (Trio64V2/DX, the one of the five confirmed as an id here) -
and each produces its own INF model line, its own backend-registry row and its
own 16-bit device entry, with the diagnostics naming the part the machine has
rather than the sibling driving it.

Three checks came with it, because aliases multiply the ways the manifest and
the driver can disagree: `check-tree.ps1` holds the manifest's id count against
`V9X_PCI_ID_LIMIT` (raised 8 -> 16, since the array truncates silently),
`test_hw16_modes.c` asserts every family's device list has one entry per
manifest id, and `test_family_matrix.c` asserts every alias resolves to its own
chip's backend rather than merely to some backend.

Found on the way, and **not** fixed: the Trio32's BIOS has no VBE `0115h`, so
that card is offered 800x600x32 and refuses it at 4F02h. It is not a
regression - the Trio32 has inherited the Trio64's mode list since the merge -
and it is the second ROM to show the failure the shared mode table's own
comment predicts. Filed as
`docs/issues/2026-08-29-trio32-lacks-vbe-0115.md`.

Deliberately not added: the Vision864/868/964/968 and 86C928 ids, all measured
and all unclaimed. Those are external-RAMDAC boards whose CR36 encoding differs
from the Trio line, so binding them would be a claim rather than an alias.

## 0.6.1 - 2026-08-28

**A third party ran 0.6.0 on hardware nobody here owns.** An Acer NAV50 - Intel Pineview, a class of chip this
project had never seen - produced four defects and one measurement. The
defects: the hardware survey was pointing the video BIOS at its own null
pointer zone and corrupting the machine it promised not to touch; a
full-screen DOS box came back with a corrupt band and froze the machine; the
driver installed itself twice, onto both of the IGD's display-class functions;
and the settings page was reporting a hardcoded lie about which mini-VDD
callbacks it installs. The measurement is that this video BIOS lists
thirty-six modes and will describe only six of them, which is why the panel's
native 1024x600 is out of reach for Velocity9x, SoftGPU and Bear Windows VBEMP
alike - and the reason for the opt-in mode sweep that tries setting what it
will not describe.

Three of those four are addressed below. **The full-screen DOS box is not.**
A stride re-assert was written for it and is in this build, but nothing has
shown it stops the freeze, so 0.6.1 makes no claim about that defect and
[the issue](docs/issues/2026-08-28-fullscreen-dos-scanout.md) stays open.

None of the fixes has run on hardware yet. The version number exists so that
the reports that come back can be attributed to the right driver.

### Added

- **The Acer NAV50's video BIOS lists thirty-six modes and describes six, and
  the driver now says so.** A third-party Pineview netbook cannot reach its
  panel's native 1024x600 under Velocity9x, SoftGPU or Bear Windows VBEMP
  alike. Three independent measurements agree - a survey from a Windows Me DOS
  box, the driver's own boot-time scan, and a survey from real DOS whose mode
  rows `diff` byte-identical to the first - that this BIOS answers `4F01h`
  with the mode-supported bit clear for thirty of the modes in its own
  `VideoModePtr` list, including all eighteen Intel OEM numbers. The panel's
  EDID is read correctly and the settings page reports
  `EDID recommendation: 1024x600 reason=edid-unpublished`, which is the
  diagnostic working: the driver knows what the panel wants and has nothing to
  offer for it ([decision](docs/decisions/2026-08-28-pineview-vbe-mode-list.md)).

- **An opt-in mode sweep that sets a mode the BIOS will not describe, then
  asks again.** VBE lets a BIOS refuse to describe a mode that is in its table
  but not available in the current hardware configuration, and some will
  describe it once it is the active one. `V9xMini_Vbe_Sweep` tries that inside
  the existing `Device_Init` collection for every listed mode the query pass
  could not describe, capturing the entry mode with `4F03h` first and putting
  it back at the end; a record that comes back usable enters the same cache as
  any other and the host-tested admit rules judge it on content. It is
  **off by default** - `4F02h` runs the real BIOS with no timeout and can hang
  a boot - and is assembled in only by `build-minivdd-skeleton.ps1 -ModeSweep`
  or `build-active-package.ps1 -ModeSweep`, with the image audit refusing a
  build whose symbol presence disagrees with the switch.

- **The full EDID detailed timing, and the VBE 3.0 CRTC block built from it.**
  `v9x_edid_parse_timing` reads the blanking and sync figures the mode table
  never needed, recording sync polarity only where the descriptor says digital
  separate - on an analog composite descriptor those bits mean serration and
  sync-on-green. `v9x_vbe_crtc_build` turns one into a CRTCInfoBlock, written
  as bytes at specification offsets because it crosses into the BIOS and both
  memory models share the header. Host-tested against the NAV50 panel's real
  EDID. This does **not** reach an unlisted resolution: the block carries no
  active width or height, so bit 11 of `4F02h` buys a refresh rate at a
  geometry the BIOS already has. It is kept for that, and for driving a panel
  at its own pixel clock if a mode number for it ever turns up.


- **The framebuffer aperture's memory type is now measured, and a
  write-combining range planned for it — but not written.** Tier-0 draws with
  the CPU into a framebuffer that is uncached wherever the BIOS leaves the PCI
  hole at the MTRR default type, and one variable-range MTRR set to WC is the
  standard fix. It is also global CPU state that corrupts unrelated memory
  when it is wrong, so this ships as Stage A: the mini-VDD reads `MTRRCAP`,
  `DEF_TYPE` and the variable pairs at `Device_Init` and reports them through
  two new API functions, host-tested policy in `src\common\mtrr.c` decides
  from them, and `enable16.c` writes the decision and the raw pairs to the
  boot INI as `Mtrr=` and `Mtrr0`..`Mtrr7`. Nothing writes an MTRR, and
  `check-tree.ps1` asserts the mini-VDD contains no `WRMSR` so that cannot
  change unnoticed. The rule the policy rests on — MTRRs enabled, default type
  uncached, no valid range over the aperture — is reasoning until the `Mtrr=`
  lines from real machines say otherwise, which is what Stage A exists to
  find out ([decision](docs/decisions/2026-08-28-mtrr-stage-a-inspect-only.md),
  plan item D1 of `docs/plans/tier0-quality.md`).

- **The intel-gma Phase 0 evidence and survey tooling, salvaged to main.**
  The Gen3 hardware audit, the measured DOS and Windows Phase 0 evidence, the
  bring-up runbook, the query-only DOS survey (`tools\diag\intel_survey_dos.c`
  + `scripts\build-intel-survey.ps1`) and the Windows capture script now live
  on main with provenance notes; the never-merged family scaffolding is
  archived at tag `archive/intel-gma-tier0` and the branch deleted. The
  branch's build-time mini-VDD mode cache was judged superseded by the
  dynamic-VBE runtime mode walk and not ported. Roadmap Track A4.

### Fixed

- **The survey no longer points the video BIOS at its own null pointer zone.**
  `V9XSURV.EXE` exited with the Open Watcom runtime's
  `*** NULL assignment detected` and froze on an Acer NAV50, on two separate
  Windows installs. `vbe_call` set `ES:DI` only when the caller supplied a
  buffer; `segread` returns the caller's `ES`, which in the small model is
  `DS`, and the register block is zeroed - so the two bufferless calls
  (`4F03h`, and `4F15h` BL=00h) handed the BIOS `DS:0000`. Neither is
  documented to write there and this one evidently does. `ES:DI` is now always
  a named scratch buffer. The safety gate gains its first *required* rules to
  go with its banned ones, and the self-test a `Remove` mutation shape to
  exercise them, because a rule about an omission cannot be tested by
  appending ([issue](docs/issues/2026-08-28-survey-null-assignment.md)).

- **The settings page no longer claims the mini-VDD installs nothing.**
  `Mini-VDD callbacks: master VDD defaults` was a string literal, wrong since
  the monitor-power callbacks landed, and it was read as evidence of what the
  mini-VDD hooked. It now names the set the build gate asserts.

### Changed

- **Built packages are committed to the repository under `releases/<version>`
  instead of living only in the git-ignored `build/`.** Four family zips and
  the DOS hardware survey, a `SHA256SUMS.txt` over the zips, and a generated
  index saying which zip matches which PCI id. `scripts\build-release.ps1`
  assembles the folder from what the package builders already produced: it
  compiles nothing, reads the version and build id back out of
  `build\packages.json`, refuses a version that disagrees with
  `include\velocity9x\build.h`, and refuses a build id from a dirty tree, so a
  published folder is attributable to one commit. The card-owner-facing
  wording is generated from each package's own `MANIFEST.TXT`, including the
  not-yet-tested-on-a-guest status line, so it cannot drift a version behind.
  The survey download link in `README.md` pointed at a `survey-v1` GitHub
  release tag and now points at the in-repo folder.

- **The generic VBE package binds by PCI class code instead of only by
  Have Disk.** The id-less model line now carries `PCI\CC_0300` as a
  *compatible* id, so Windows selects the driver where no vendor driver claims
  the device - a compatible id ranks below every hardware-id match, which
  makes this the fallback rather than the default. It also matches class 0300
  only, so the second display-class function a Pineview IGD presents at
  `038000` no longer receives a forced copy of the driver: the duplicate
  Device Manager entry on the NAV50, and the frozen Settings tab that came
  with it, have one cause and one fix. Reported by CentaurHauls.

- **The backend registry's PCI dispatch is generated from the family
  manifests.** `src\common\backend_registry.c` was a hand-written if-chain
  restating PCI ids the manifests already declare — the file every new family
  had to edit to exist. Each manifest now carries a `Backend` section (getter,
  header, host-testable sources), `scripts\update-backend-registry.ps1` emits
  the checked-in `src\common\backend_registry_table.inc` from it, and
  `check-tree.ps1` regenerates and compares so the table cannot silently
  drift from the manifests. The allowlist semantics are byte-for-byte the
  same six ids as before; tier-0 still refuses unlisted hardware.
- **The host builds derive their chipset source list from the manifests.**
  `build-host.ps1` and `build-host-msvc.ps1` compile the union of every
  family's `Backend.Sources` instead of hand-listing the policy backends —
  the section of the two lists that had already drifted apart once. With the
  family-manifest paths also dropped from `check-tree.ps1`'s required-file
  list (manifests are glob-discovered and schema-validated), adding a family
  now touches no script: manifest, evidence, regenerated table. Track A2/A3
  of `docs\plans\family-structure-and-next-d3d-roadmap.md`.

## Earlier releases

0.6.0 (2026-08-27) back to 0.1 (2026-08-08) are in
[docs/changelog/0.1-to-0.6.0.md](docs/changelog/0.1-to-0.6.0.md).
