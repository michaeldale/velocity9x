# NVIDIA RIVA 128ZX: a seventh family, 2D then Direct3D

Date: 2026-11-02

Status: Phase 0 done. The signed 0.34.0 `vbe` package runs on the card on
M-7V636C: `enable-ok` at 800x600x16, V9XGDI passes (boot 10). Phase 1
done: the tier-0 `nvidia` family runs there since boot 11, same modes,
V9XGDI passes. Phase 2's reads done
([record](../decisions/2026-11-02-riva128-register-probe.md)): PMC_BOOT_0
`00030122` as the sources say; under VBE, PGRAPH is off, PFB_CONFIG_0 is
stale (640x8 after an 800x600x16 mode set), and RAMIN is the top 32 KiB of
video memory, reversed by 16 bytes, seen at BAR1 + C00000h, and wiped of
the BIOS's tables by the time Windows runs. Phase 3's write probe done
([record](../decisions/2026-11-02-riva128-2d-engine-writes.md)): rivafb's
bring-up runs clean; fills and blits are exact at 8 bpp and right in every
colour bit at 16 (X1R5G5B5) and 32 bpp, but the engine sets bit 15 at
16 bpp and rewrites the top byte at 32 bpp; a VBE mode set wipes RAMIN.
Decision 1 taken (Michael: A, then B); A is now measured dead, so 16 bpp
acceleration needs B. Next: B's display side, the RAMDAC switched to
5:5:5 after the mode set, which needs the owner's eyes on the monitor.
Register facts below come from open-source and reverse-engineered sources
unless a record says the card confirmed them.

Target: the RIVA 128ZX (NV3T), PCI `12D2:0018` revision 22, as a new
`nvidia` family. NV3 shares nothing with the NV4 (TNT) and later below
PCI/VGA: different PGRAPH offsets, no PCRTC or PRAMIN in BAR0, typed RAMHT
entries instead of object classes. So the family starts as one chip, and a
TNT later is a second generation in the same family
(`one-family-per-vendor`), not a variant of this one.

## The machine

M-7V636C, computer SPACETUG, reached only through the remote-help relay.
Owner "Mac", who asked for Riva 128 support and has many other cards of the
period. A hang means the owner power-cycles it.

- Pentium II 333 on an Intel 440LX, Windows 98 **first edition**
  (4.10.2222 A). The README validates 98 SE only.
- STB Velocity 128 ZXV, subsystem `10B4:222A`, AGP (bus 1), 8 MiB.
  BAR0 `1F000000` (16 MiB registers), BAR1 `21000008` (prefetchable
  frame buffer). BIOS 32 KiB at C000, OEM "STB Velocity 128 (RIVA ZXV)",
  revision 210-0276-003.
- VBE 3.0, 8 MiB reported, 46 modes, every one linear-capable (attributes
  `0F9F`), 320x200 to 1920x1200. 16 bpp is **5:6:5** in every 16 bpp mode.
  32 bpp up to 1600x1200; 1920 widths are 8 and 16 bpp only.
- Dell 1907FP monitor (EDID).
- A Voodoo 1 (`121A:0001`) and a Chromatic Mpact (`110B:0002`) are also in
  the box. Neither is in scope.
- Windows shows a network logon prompt at every boot. Escape continues
  without a network logon; no password is entered.

Evidence: `build\driver-results\V9XSURV-M-7V636C.INI` (gitignored; run in a
Windows DOS box, where every mode's PhysBasePtr read `FFFF0000`; the driver
at boot got `21000000` from the same BIOS) and boot 10's `V9XBOOT.INI` /
`V9XHW.INI` / `V9XMODES.INI`. Both go into the Phase 0 record.

## Sources

Collected 2026-11-02 into `build\external\nv3\` (outside version control):

- Linux `drivers/video/fbdev/riva/` (`riva_hw.c`, `riva_hw.h`,
  `riva_tbl.h`, `fbdev.c`, `nv_driver.c`): the most complete NV3 init.
- xf86-video-nv `riva_*` (its riva sub-driver supports only the RIVA 128).
- envytools docs and rnndb: `mmio.rst`, `nv1-pfifo.rst`, `puller.rst`,
  `pmc.rst`, `nv3_pgraph.xml`, `nv3_3d.xml`, `nv3_pfb.xml`, `nv3_gdi.xml`,
  and `hwtest/pgraph_class_d3d0.cc`.
- 86Box, starfrost013's fork, branch `nv3_reintegration`: an NV3 emulator,
  reverse-engineered, not checked against hardware.

nouveau has no NV3 support. NVIDIA published nothing. Every register write
this family makes has to cite one of the above, and the probes have to
confirm it before the driver relies on it.

## What the sources settle

| | NV3 per the sources | Consequence |
|---|---|---|
| BAR0 | 16 MiB of registers: PMC 0, PFIFO 0x2000, PTIMER 0x9000, PFB 0x100000, PSTRAPS 0x101000, PGRAPH 0x400000, PRAMDAC 0x680000, USER channels 0x800000 (`mmio.rst`) | A new mini-VDD map of BAR0; the USER area is where commands go |
| RAMIN | Not in BAR0. Linux maps it at BAR1 + 0xC00000, 32 KiB; the emulator places it at the top of VRAM (`fbdev.c` `rivafb_probe`, 86Box `nv3_pramin.c`) | **Measured: both.** BAR1 + C00000h shows the top 32 KiB of video memory, reversed in 16-byte units ([record](../decisions/2026-11-02-riva128-register-probe.md)). The DirectDraw heap stops below 7F8000h on 8 MiB |
| Identity | PMC_BOOT_0 at 0x0: revision, chip 3, foundry (`pmc.rst`) | The probe's first read |
| VRAM size | PFB_BOOT_0 0x100000, decoded per `nv3GetConfig` (`riva_hw.c`) | A `read_video_memory` hook; VBE's 8 MiB agrees with the card already |
| Command path | PIO into USER channel 0: subchannel *s* at 0x800000 + s·0x2000, free count at +0x10 in bytes (`riva_hw.h` `RIVA_FIFO_FREE`) | No DMA, no microcode; a CPU-written FIFO |
| Idle | PGRAPH STATUS 0x4006B0 bit 0, and the free count back at its starting value (`riva_hw.c` `nv3Busy`) | `wait_idle` |
| Objects | RAMHT entries (handle, then context with a 7-bit type and an instance), built in RAMIN by the driver; rivafb's set: ROP, clip, pattern, image-from-CPU, blit, GDI rectangle/text, line, D3D, surfaces (`riva_tbl.h`) | The 2D engine needs PFIFO and RAMIN set up by us: the BIOS does not do it for VBE |
| Fill | GDI object, colour at +0x3FC, then (x<<16)\|y, (w<<16)\|h (`nv3_gdi.xml`) | Note x is the high half here, unlike the blit |
| Blit | Blit object: (sy<<16)\|sx, (dy<<16)\|dx, (h<<16)\|w; the last write starts it | Screen-to-screen copies |
| Surfaces | PGRAPH offsets 0x400630-0x40063C, pitches 0x400650-0x40065C, SURF_CONFIG 0x4006A8; PFB 0x100200 holds width and depth | Must match the VBE mode exactly |
| 16 bpp | 2D is **5:5:5 only**; rivafb forces 5:5:5 on NV3, X accepts depths 8, 15 and 24 (`fbdev.c`, `riva_driver.c`) | **Conflict with this BIOS, whose 16 bpp modes are all 5:6:5** (see below) |
| 3D | D3D0 textured-triangle object (type 0x17): perspective, Z, alpha test and blend, fog; textures A1R5G5B5, X1R5G5B5, A4R4G4B4, R5G6B5, power-of-two (`nv3_3d.xml`). rivafb enables it only at 15/16 bpp | Direct3D at 16 bpp only, DX5 level |
| Scanout | CRTC 0C/0D, CR19 bits 0-4, CR2D bits 5-6, after SR06 = 0x57 unlocks (`riva_hw.c` `SetStartAddress3`) | Flips through VGA-style extended CRTC registers |
| Vblank | PGRAPH INTR 0x400100 bit 8 (`nv3_pgraph.xml`) | Or VGA input status 1; to be measured |
| Hazards | PGRAPH DEBUG_2 bit 21 and DEBUG_3 bit 17 "tend to hang things"; methods 0x180-0x1FC trap to software on NV3; an engine with its PMC enable off vanishes from MMIO (`nv3_pgraph.xml`, `puller.rst`, `pmc.rst`) | Never set those bits; write object instances in RAMIN directly, as rivafb does |

## The 16 bpp problem

Every 16 bpp mode this BIOS lists is 5:6:5, and the sources say NV3's 2D
and 3D draw 5:5:5. There are three ways to go:

- **A. Use the engine at 5:6:5 anyway, for raw copies and fills only.** A
  blit moves bits; a solid fill may pass the colour through untranslated
  if the surface is declared 16 bpp. That is a hypothesis for the Phase 3
  write probe, cheap to test. Patterns, text and 3D would stay on the CPU
  at 5:6:5.
- **B. Run 16 bpp desktops at 5:5:5.** Set the VBE mode, then switch the
  RAMDAC to 5:5:5 (PRAMDAC general control) and tell Windows 5:5:5. The
  driver already handles 5:5:5 (`is555`). This is how NVIDIA's own driver
  and rivafb behave, and it is the only way 3D at 16 bpp works.
- **C. Accelerate 8 and 32 bpp only.** The smallest change, but 3D needs
  16 bpp.

The recommendation is A first, as a measurement, then B for 3D.

**Taken and measured (2026-11-02).** Michael chose A, then B. A is dead:
the engine sets bit 15 on every 16 bpp pixel it writes, fills and blits
alike, and the one other 16-bit format (Y16) converts colour to luminance
([record](../decisions/2026-11-02-riva128-2d-engine-writes.md)). So 16 bpp
acceleration is B or nothing; 8 and 32 bpp need neither.

## Phases

0. **Baseline (done).** `vbe` on M-7V636C: `enable-ok` 800x600x16, 42 of 46
   modes cached, LFB `21000000`, V9XGDI PASS
   ([record](../decisions/2026-11-02-m-7v636c-riva128-first-look.md)).
   Still owed: a V9XMSW mode cycle.
1. **Identity (done).** The `nvidia` family at tier-0, claiming
   `12D2:0018` exactly, every hw16 hook NULL, as the `sis` and `3dfx`
   families began. No NVIDIA register written. Installed on boot 11: the
   adapter is named, the 42 mode rows match `vbe`'s, the map is 8 MiB,
   V9XGDI passes.
2. **Read-only register probe**, a Win32 tool with a mini-VDD map of BAR0.
   Read PMC_BOOT_0, PMC_ENABLE, PFB_BOOT_0, PFB 0x100200, PSTRAPS, PFIFO
   (RAMHT/RAMFC/RAMRO pointers, CACHE1 push/pull, PUT/GET), PGRAPH STATUS,
   DEBUG_0-3, SURF_CONFIG, the surface offsets and pitches, and the USER
   free count, at each VBE depth. Find RAMIN: read BAR1 + 0xC00000 and the
   top of VRAM. The question it answers is what the BIOS left set up, and
   whether any of it survives a VBE mode set.
3. **2D write probe (done, overlap owed).** RAMIN built in rivafb's layout
   (in its own top-of-VRAM home), PFIFO and PGRAPH reset as rivafb does,
   two fills and a blit checked byte for byte at 8, 16 and 32 bpp: exact at
   8, right in every colour bit at 16 (X1R5G5B5) and 32, the unused bits
   rewritten; a VBE mode set wipes RAMIN and leaves PMC_ENABLE and
   PFB_CONFIG_0. Still owed: overlapping blits in both directions, and
   B's RAMDAC switch to 5:5:5 watched on the monitor.
4. **DirectDraw on the engine (done, seen at 8, 16 and 32 bpp).** Engine32 ops
   (`fill`, `copy`, `wait_idle`) under engine type NV3 (13), at 8 and 32
   bpp only: builders in `src\chipsets\nvidia\nv3_2d.c` (host-tested
   against rivafb's tables and the probe's values), the HAL engine
   `engines\eng_nv3.c` (bring-up repeated whenever RAMIN's mark or
   PFB_CONFIG_0 shows a mode set wiped it, RAMIN written through the
   framebuffer at the measured reversed layout), BAR0 mapped by a new
   mini-VDD call (`V9XMINI_FN_NV3_MMIO_MAP`, 2Ah), the top 32 KiB kept off
   the DirectDraw heap. run-checks passed. On M-7V636C boot 12 the engine
   was claimed (EngineCaps 3); V9XDDP at 800x600x32 then stopped the
   machine within seconds
   ([issue](../issues/2026-11-02-riva128-hang-under-v9xddp-at-32bpp.md)).
   Found 2026-11-03: at 16 bpp the engine failed validation, so the core's
   CPU fill and Lock waited on it without end; the engine never drew. Now
   it validates as idle at 16 bpp and declines per operation. Boot 15:
   V9XDDP `COMPLETE` from 16 and 32 bpp desktops, V9XDDF PASS at both
   (engine at 32, CPU at 16). 8 bpp: V9XDDF and V9XDDP pass on boot 15
   ([record](../decisions/2026-11-03-riva128-crtc-start.md)).
5. **Flips and vblank (done, not watched on the monitor).** CR0D, CR0C and
   CR19 bits 4-0 behind SR06, measured by V9XN3MM `/crtc`; vblank from
   input status 1, as every VGA-path chip. Boot 16: 69 flips handled,
   none declined, the start seen at the back buffer during V9XDDP and at 0
   after ([record](../decisions/2026-11-03-riva128-crtc-start.md)). Open:
   whether the start latches at the retrace or applies at once (tearing),
   which only the monitor shows.
6. **GDI on the engine (done at 8 and 32 bpp).** gdi_accel.c's NV3 arm,
   with the HAL's builder and its own bring-up after each Enable; BAR0
   reached through a 16 MiB selector. Boot 18: V9XGDI `/accel` PASS at 32
   and 8 bpp with the engine drawing, 16 bpp PASS with acceleration off,
   V9XDDP and GDI each fine after the other
   ([record](../decisions/2026-11-03-riva128-gdi-on-the-engine.md)). 16
   bpp waits for B.
7. **Direct3D**: its own plan, [nvidia-riva128-d3d.md](nvidia-riva128-d3d.md).
   The D3D0 object is DX5-level, so this is the S3 ViRGE and Rage Pro class
   of HAL, not the Evergreen one. Step 1 done: a textured, fogged triangle
   drawn off screen
   ([record](../decisions/2026-11-03-riva128-first-triangle.md)).

## Unknowns that need a measurement

- Whether the BIOS leaves PFIFO and PGRAPH enabled and RAMIN anywhere
  usable, or whether a full rivafb-style reset is needed (and survives).
- Whether the page table rivafb writes overlaps its RAMRO and RAMFC (the
  research brief's arithmetic says it does). Where RAMIN is: answered.
- Which event wipes RAMIN (a VBE mode set is the candidate).
- Whether a raw 16 bpp fill and blit are correct at 5:6:5.
- Whether the surface offset is limited to 4 MiB (an rnndb comment's mask).
- Whether a new start address latches at vblank.
- Whether the DOS box and its return disturb the engine state.

## Decisions for Michael

1. The 16 bpp route: A, B or C above.
2. Whether to build the 86Box NV3 fork as an emulator for the
   loop. It is unverified against hardware, but a hang there costs nothing,
   and every hang on M-7V636C needs its owner.
3. Windows 98 first edition is not a validated target. This bring-up would
   make it one, for this machine at least.
