# Changelog

All notable Velocity9x changes are recorded here. The project uses semantic
version numbers for product milestones; diagnostic builds retain a separate
build identifier so exact guest-tested binaries remain traceable.

## 0.8.0 - 2026-09-17

Hardware Direct3D on the Intel GMA 950 (945GSE), the first engine outside
S3. Version bumped at the point where Final Reality runs textured on the
netbook; the flip and the depth comparison are still open below, and
nothing in this release has been packaged or run anywhere but that one
machine (`MICHAEL-NETBOOK`, captures `C:\temp\intel52` through `intel60`).

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
