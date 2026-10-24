# Rage 128: what dispbench says against ATI's driver, and the work it implies

Date: 2026-10-24. Status: review only. No driver code changed, and nothing
on A8U4I5 or any guest was touched while it was written. Item 1
(write-combining) was done afterwards and measured in
[`../decisions/2026-10-24-rage128-write-combining.md`](../decisions/2026-10-24-rage128-write-combining.md).

Source: dispbench (`C:\everything\dispbench`, main `c68d365`), A8U4I5
(Pentium III 1 GHz, 440BX AGP, Rage Fury Pro, 1002:5046), 800x600x16;
Direct3D tests full-screen at 640x480x16. The comparisons were regenerated
from the recorded runs with `scripts\compare-bench.ps1` (read-only):

| Report | Baseline (ATI 4.13.7192) | Candidate (Velocity9x) |
|---|---|---|
| GDI and DirectDraw (`-Runs`) | `a8u4i5-rage128-ati\20261024-171143` | `a8u4i5-rage128-v9x\20261024-162442` |
| DB3D through DirectX 7 | `...ati\20261024-171143` | `...v9x\20261024-162442` |
| DB3D through DirectX 5 | `...ati\20261024-171506` | `...v9x\20261024-150543` |
| DB3D8 through DirectX 8 | `...ati\20261024-171506` | `...v9x\20261024-170222` |

"Confirmed" below means read in the source at the lines given, on main
`a19476c9`. "Inferred" means it is the likely explanation and was not
proved.

## 0. Which Velocity9x was measured - read this first

The Velocity9x runs were **not** of a committed build, and not of the
boot-543 CCE-ring build. Every one ran at **boot 573**
(`RUN.TXT GuestBootCounter=573`), and the `V9XDISP.DRV` that DBCAPS
recorded - 56,040 bytes, CRC32 `0x423FDE0E` - is byte-for-byte the
package of the H4 experiment that was deployed then and reverted before
`a19476c9` (`docs\plans\d3d-core-throughput.md`, H4). The trace tool on
the guest reports `5ef364f9-dirty`, the same build.

That build was `5ef364f9` plus, uncommitted:

- fills of colour targets and depth buffers drawn by the 3D engine in the
  CCE ring (a flat quad) instead of the 2D engine after a drain;
- the engine "validated" flag kept across ring starts, with a private
  "2D registers stale" flag making every register path re-validate;
- the Lock-by-surface counters (since committed).

So these numbers describe code that no longer exists, and are not evidence
about the committed driver:

- **Clear.Target / Clear.Both** (234.6 / 117.3 Mpix/s): exactly the 3D fill
  rate, because H4 cleared with a 3D quad. The committed driver clears with
  the 2D engine, after draining the chip (`src\display32\engines\eng_r128.c:222-255`,
  reached from `d3d_core.c:5803-5874`).
- **Dd.ColorFill.Vid** (4.31 cyc/px against ATI's 1.09 at 256x256): 4.26
  cycles a pixel is the 3D fill rate again; H4 sent colour fills of
  non-depth surfaces through the same quad when the ring was in use.
- **Dd.Lock.*** (about 5,600 cycles against ATI's 1,660) and
  **Dd.LockAfterBlt.VidToVid** (5.7 million against 575,000): under H4
  every Lock after a ring start re-validated the 2D engine - a drain,
  a configuration-space read, nine register writes and two idle polls.
  The committed Lock path is different (`ddhal_core.c:1345-1372`).

Everything else - GDI, the frame-buffer mapping, the Direct3D draw paths,
DirectDraw blits - was the same in H4 as on main, and stands. **The four
items above need re-measuring on a committed build before anyone acts on
them.**

## 1. The CPU's path to video memory (FbWrite 49.9 against 242 MB/s)

**Confirmed: Velocity9x leaves the frame buffer uncached on every machine.**

- The frame buffer is mapped by DPMI 0800h with no cache type
  (`src\display16\runtime.asm:3867-3898`, called from
  `src\display16\enable16.c:1175-1177`); the Lock pointer is that mapping
  (`src\display16\dd16.c:709`, `:889`).
- No MTRR is ever written. `include\velocity9x\mtrr.h:24-31`: Stage A
  computes a plan, logs it and "NOT executed"; there is no WRMSR in `src`.
- On A8U4I5 the plan is ready and approved: `V9XBOOT.INI`
  `Mtrr=... r=0 s=3 b=dc000000 z=01000000` - write-combining over 16 MiB
  at `DC000000` in free pair 3, nothing overlapping. The BIOS's own ranges
  are WB RAM (pair 0), UC `D8000000`-`D9FFFFFF` (1) and WC
  `DA000000`-`DBFFFFFF` (2, the AGP window); nothing covers the frame
  buffer, so it is UC by default (`enable16.c:670-710` writes those keys).
- The Stage B plan exists, unstarted, and needs two decisions from
  Michael: `docs\plans\write-combining-stage-b.md`.

**Inferred: ATI's driver sets WC over the aperture.** 242 MB/s against
49.9 is the UC-to-WC ratio, and ATI's CPU-only paths show it everywhere:
`BitBltDibToScreen` 10.0 cyc/px against Velocity9x's 40.3,
`SetDIBitsToDevice` 10-25 against 40-43, both `CPU` under both drivers.
**Every Velocity9x CPU path to the screen sits at about 40 cycles a pixel,
which is FbWrite** (2 bytes a pixel at 50 MB/s on a 1 GHz CPU). WC does
not speed reads; the FbRead gap (10.4 against 15.2 MB/s) is something else,
perhaps the Rage 128's BUS_CNTL read settings - not looked at.

What inherits it: software GDI (every fill, text, line and DIB transfer
below), DirectDraw Lock writes, Direct3D texture upload by Lock (24.9
against 248.9 MB/s), DirectDraw's own CPU blits into video memory.

The risk on this machine is lower than the Stage B plan assumes for it:
ATI's driver runs this card write-combined on this 440BX. The posted-write
caution in `docs\handoffs\2026-09-08-a8u4i5-trio64-cross-platform-evidence.md:88-99`
concerns a Trio64 on the same board.

## 2. GDI on the CPU (71 operations HW under ATI only)

**Confirmed: the Rage 128 has no GDI path at all.**

- `src\display16\gdi_accel.c:1486-1520`: the GDI engine goes live only for
  the Trio64, the ViRGE, the MGA and the VSA-100; any other engine type
  clears `enabled` at `:1520`. The Rage 128 publishes an engine descriptor
  (`src\chipsets\ati\rage128\rage128_hw16.c:41-91`, caps SOLID_FILL,
  SCREEN_COPY, FLIP, VBLANK, D3D), which DirectDraw uses and GDI ignores.
- Output (Rectangle, RoundRect, Polygon, lines), StretchBlt and
  StretchDIBits go to the DIB engine for **every** chip, unconditionally
  (`src\display16\dib_thunks.asm:84-88, 106-107`).
- Even where GDI is accelerated, the ROP set is BLACKNESS, WHITENESS,
  SRCCOPY and solid-brush PATCOPY (`gdi_accel.c:1773-1779`). Pattern and
  hatch brushes, NOTSRCCOPY, SRCAND, SRCINVERT and PSDPXAX are CPU on
  every chip.
- The 2D builders the Rage 128 would need already exist, are host-tested,
  and drive DirectDraw today: `src\chipsets\ati\rage128\r128_2d.c` (init,
  fill, copy). The template for wiring one into GDI is the MGA/VSA-100
  pattern in `gdi_accel.c:2027-2135` (builders), `:794-819` (drawing),
  `:54-64` (per-family guards).

**Confirmed: the hard part is the CCE.** Direct 2D register writes must not
meet a running microengine (`eng_r128.c:229`, the boot-522 hang), and the
16-bit side has no way to know the 32-bit side left the CCE running
(`gdi_accel.c:845-848`; nothing in `src\display16` mentions the CCE). A
Rage 128 GDI path needs either a "CCE running" flag in the shared engine
block that GDI drains through, or GDI's blits submitted as CCE packets.

**Inferred: ATI accelerates fills, copies, patterns, the ROP3 set, stretch
and shapes** on the 2D engine - all `HW` at 1-5 cyc/px.

The ratios split into two groups, which matters for ordering:

- *Writes only* (fills, PatBlt, BLACKNESS/WHITENESS/PATCOPY, shapes): 37x
  today, but about 40 cyc/px of it is FbWrite. Write-combining alone should
  take them to near ATI's own CPU paths (about 8-10 cyc/px, inferred from
  ATI's DIB-to-screen); the engine then wins a further ~8x.
- *Reads of video memory* (screen-to-screen BitBlt, scrolling, SRCCOPY and
  the source ROPs, StretchBlt): 90-230x, bound by FbRead (10.4 MB/s), which
  write-combining does not help. **Only the engine closes these.**

## 3. DirectDraw keyed and stretched video-to-video Blt

**Confirmed: the caps are never offered.** `ddhal_core.c:2388-2390`
publishes `3D | GDI | BLT | BLTCOLORFILL | BLTDEPTHFILL` only - no
DDCAPS_COLORKEY, DDCAPS_BLTSTRETCH, dwCKeyCaps or dwFXCaps - and the copy
path takes no keying or stretching (`ddhal_core.c:1790-1810`;
`r128_2d.c` builds init, fill and copy only). DirectDraw's HEL does both on
the CPU, reading video memory: 349 cyc/px keyed against ATI's 2.2 (160x),
90 stretched against 5.5 (16x). Not CLAIMED-NOT-DELIVERED, correctly: they
are not claimed.

**Inferred:** the Rage 128's 2D engine does source colour-compare
(`CLR_CMP_CNTL` and friends, which `r128_2d.c` already disables) and ATI
uses it. Colour-keyed sprite blits are what 2D DirectDraw games are made
of, so this one is closer to games than the GDI list.

## 4. Direct3D: the chip's work

| Finding | Cause (confirmed unless marked) | Work? |
|---|---|---|
| One texture stage; FillMultiTex refused | DX7 caps hard-code `wMaxTextureBlendStages = 1`, `wMaxSimultaneousTextures = 1` for every engine (`d3d_core.c:4633-4634`); stage-1 states are dropped (`:3869-3876`); the Rage 128 mapper refuses a second unit (`d3d_r128_map.c:279-285`); the vertex format carries one S/T pair (`r128_3d.c:114`, 0xC9). The chip's second unit (SEC_TEX_*) is never programmed. | Yes - see the ranking |
| 1024x1024 textures refused | No size check refuses them (`d3d_r128.c:73`, `:958-963`; `ddhal_core.c:986-999`). The ATI family maps 16 MiB for every chip (`src\chipsets\ati\ati_hw16.c:258-261`), the Rage 128 sets no `map_bytes` (`rage128_hw16.c:116`), and VRAM is clamped to the mapping (`enable16.c:817-827`): half the card is unused. Inferred: a 1024^2 chain (about 5.3 MiB at 32 bpp) does not fit beside the flip chain and Z in what is left. | Yes |
| Linear mip filter twice as slow; ATI pays nothing | Velocity9x programs the chip's real trilinear mode (`d3d_r128_map.c:121-148` maps LINEARMIPLINEAR to code 5). Inferred: ATI substitutes a cheaper filter; see section 7. | No, not on this evidence |
| 32-bit textures 1.7x faster under Velocity9x; FillTexSize.512 likewise | Velocity9x places every texture in local video memory (`d3d_place.c:207-266`). Inferred: ATI puts 32-bit and large textures in AGP memory, whose fetch is slower. A choice, not a Velocity9x gap. | No |
| 16-bit textures 12-13% slower at 256x256, faster at 32-128 | Texture cache setup is fixed: SCALE_3D_CNTL 4-quadword lines (`r128_3d.c:61-64`), TEX_CACHE_FLUSH every state (`:391`), linear (untiled) textures. Inferred: ATI tiles textures or sizes the cache differently. | Low priority |
| Clear 4x slower | Measured on H4, not main (section 0). | Re-measure |
| DDFLIP_NOVSYNC ignored (60 fps) | NOVSYNC reaches `v9x_set_display_start_now` (`ddhal_core.c:954`), which on the Rage 128 is the same latch-at-blank write (`i9xx_scanout.c:1585-1596`, `r128_scanout.c:65-94`); the scanout counts only FLIP_CNTL clear as live (`r128_crtc.c:7-12`); a queued flip waits for the previous one in the ring (`r128_cce.c:625-643`). Tearing mode (FLIP_CNTL=1) is never used. | Yes, small - a policy call for Michael (the VSync setting) |
| FillSpecular fails read-back under ATI | ATI's gap, not Velocity9x work. | No |

**Inferred, and the most useful line here:** the fill tests show no
per-pixel feature that costs the Rage 128 much more under Velocity9x,
except trilinear. Yet `d3d-core-throughput.md` found 3DMark 2000's game
frames cost the chip more under Velocity9x. A single-stage driver makes a
multitextured application draw every lightmapped surface twice - twice the
geometry, a blended second pass of fill. 3DMark 2000's own multi-texture
fill rate is 129.6 Mtexels/s under Velocity9x against 178.2 under ATI
(`docs\decisions\2026-10-22-rage128-ati-baseline-and-tier0.md`). Two
texture stages is the most likely remaining cause of that gap; it is not
measured.

## 5. Direct3D: the CPU's work

| Finding | Cause |
|---|---|
| 1.3x per triangle in batches of 8-512 (DX7, DX5) | **Confirmed:** DrawOnePrimitive cuts every call into 64-triangle batches (`d3d_core.c:2908`, `V9X_D3D_INDEXED_BATCH` 64 at `d3d_internal.h:80`) although the Rage 128 takes runs of any length (`d3d_r128.c:658-664`). Each batch pays the whole pipeline: an uncached CRTC_STATUS read for vblank statistics (`d3d_core.c:760`), describe_draw and texture resolve, the mip-chain walk (`d3d_r128.c:309-388`), the full ~40-dword state rebuilt (`r128_3d.c:331`), vertex-space reservation, a GUI flush per packet (`d3d_r128.c:638`), and a ring submit with an MMIO read and write (`r128_cce.c:380-429`). Several hot files are still unoptimised: `d3d_dp2.c`, `r3d_records.c`, `ddhal_core.c` (trace), `d3d_select.c`, the scanouts (`build-ddraw-hal-dll.ps1:255`). |
| 2.1x on one 4096-triangle call (491 KB) | **Inferred:** the same 64-triangle cap, plus DX7 routing large calls through DrawPrimitives2, where the vertices are probed with `IsBadReadPtr` over the whole range when it moves (`d3d_core.c:4192`) and copied into staging by the unoptimised `r3d_records.c` - two or three passes over an array larger than the L2. DX5 costs 769 cycles a triangle on the same call against DX7's 1,743. |
| DX8 2.4x (1,813 against 966 cycles a triangle; ATI equal across DX7/8) | **Confirmed:** no DX7/8-era DP2 opcode is handled natively - everything beyond the DX6 set goes to the runtime's parser in up to 64 rounds (`d3d_dp2.c:499`, `d3d_core.c:4447-4484`); `dwFVFCaps = 1` (`d3d_core.c:4627`), so DX8's FVF layouts are converted vertex by vertex, byte-wise, in unoptimised code (`d3d_dp2.c:600-633`); every indexed DP2 op is expanded to single triangles, each through its own settle and append (`d3d_dp2.c:404-427`); DP2 never reaches `ops->draw_indexed`, so DX8 loses the hardware index path DX7 has. |
| Texture switch or state toggle per draw: 2.9x | **Confirmed** that the cost lands on the next draw as a new batch: texture re-resolved and mip chain re-walked, state rebuilt and re-sent in full, a TEX_CACHE_FLUSH and pixel-cache flush every time (`r128_3d.c:391, 424`). **Inferred** that this is the 6,000-7,400 cycles. H2 measured that re-sending the state costs the *chip* nothing; this is the CPU's cost of building it, a different thing. |
| Texture upload: Lock 10x, Blt 14x (DX7); UpdateTexture 3.8x (DX8) | **Confirmed:** every Lock drains the chip and stops the CCE (`ddhal_core.c:1364` to `r128_cce.c:511-553`), and the CPU then writes uncached (section 1). A system-to-video Blt is NOTHANDLED (`ddhal_core.c:1521`; no `dwSVBCaps`), so the HEL copies on the CPU. **Inferred:** ATI's 1,373 MB/s is above the CPU's own ceiling, so its upload is the engine (a host-data or bus-master blit); DX8 has no TEXBLT opcode here, so UpdateTexture takes the same CPU route. |

## 6. Cosmetic

**Inferred:** "Velocity9x ATI Rage XL PCI" on the 5046 is the display class
key's `DriverDesc` left from when A8U4I5 carried the Rage XL. The driver
has no identifier code; the 5046's INF model reads
`Velocity9x ATI Rage 128 Pro` (`packaging\families\ati\family.psd1:288-296`)
and "Rage XL PCI" belongs to the Rage Pro alias (`:181-182`). Reinstalling
from the INF should fix it; V9XUPD does not write `DriverDesc`
(`tools\diag\update_win32.h:15-22`). Check `Class\Display\0007\DriverDesc`
on the guest.

DirectX 9 offering no HAL is **parked** (Michael, 2026-10-24): not looked at.

## 7. Where the benchmark is wrong, or says less than it seems

1. **The configuration is mislabelled** (section 0): the Velocity9x runs
   are of an uncommitted, since-reverted build. dispbench recorded the
   evidence correctly (DRV CRC, boot counter); the reading of it as the
   boot-543 build is what is wrong. Clear, video colour fill and both Lock
   tests must be re-run.
2. **HW? on Velocity9x's system-to-video blits** (`Dd.Blt.SysToVid.*`,
   `Dd.BltFast.SysToVid.*`, 21 cyc/px) cannot be hardware: Velocity9x
   declines every Blt with a system-memory surface (`ddhal_core.c:1521`).
   They exceed FbWrite (40 cyc/px) only because the HEL's copy beats the
   REP STOSD reference on uncached memory, so D3 (beating the CPU's own
   best path) is not a proof against this FbWrite.
3. **`Dd.BltFast.VidToSys.*` under Velocity9x** (1.7-13 cyc/px, `HW?`)
   runs at system-to-system speed, while `Dd.Blt.VidToSys` in the same run
   reads video memory at 134-167 and ATI's BltFast does too (130). Its
   record lacks the `Direction=` key Blt writes. Most likely the source
   surface was not in video memory (113 256x256 surfaces fit; these tests
   run later). The tool should assert placement (`DDSCAPS_VIDEOMEMORY`
   after creation) before timing.
4. **FillTexMipLinear's "gap"** charges Velocity9x for doing what was
   asked. ATI's linear-mip result equals its bilinear one (167.3 both),
   which real trilinear on this chip cannot do; read-back passes both, so
   the verification does not tell trilinear from nearest-mip bilinear. A
   test whose mip levels differ in colour, sampled where two levels blend,
   would settle which filter each driver runs.
5. **FillTex32 and FillTexSize.512 favour Velocity9x** for a reason the
   test does not record: where each driver placed the texture. The report
   should carry the surface's caps (local, AGP, system).
6. **Velocity9x's own profiling agrees with the rest:** the fill tests are
   chip-bound and equal on untextured paths, as `d3d-core-throughput.md`
   found 3DMark's game tests chip-bound under both drivers; the geometry
   tests are CPU-bound, as its CPU-speed and high-polygon profiles were.

## 8. The work, ranked

Impact names the ratios each closes and who notices; effort and risk are
rough. Nothing here is scheduled; each needs Michael's go.

| # | Work | Closes | Matters for | Effort | Risk |
|---|---|---|---|---|---|
| 1 | **Write-combine the frame buffer** (MTRR Stage B, `write-combining-stage-b.md`), A8U4I5 first; map 32 MiB on the Rage 128 at the same time (item 5) so one range covers the card. | FbWrite 5x; every CPU write path (software GDI fills, text, lines, DIB transfers ~4x; Lock texture upload; DirectDraw Lock writes; HEL blits into video memory) | Everything on the desktop; software-rendered and Lock-heavy games; texture streaming | Small-medium: the policy and plan exist; the MSR write in the mini-VDD, the rounding rule, two decisions | Medium in general (440BX posted writes, the netbook first in the plan); lower on this machine, where ATI runs it |
| 2 | **Rage 128 GDI: screen-to-screen copy, scrolling, solid fill**, with the CCE coordination (a shared "CCE running" flag the 16-bit side drains through, or GDI blits as CCE packets) | Screen-to-screen BitBlt, ScrollDC, SRCCOPY (90-120x, not helped by item 1); fills and BLACKNESS/WHITENESS/PATCOPY (the ~8x left after item 1) | Window moves, scrolling, every repaint | Medium: builders exist (`r128_2d.c`), MGA/VSA-100 template in `gdi_accel.c`; the CCE handshake is new | Medium-high: the CCE-meets-register-write hang class |
| 3 | **Two texture stages on the Rage 128** (SEC_TEX_*, a second S/T pair in the vertex format, stage-1 DP2 state, caps from the engine instead of `d3d_core.c:4633-4634`) | FillMultiTex refusal; inferred the bulk of 3DMark's chip-side game-test gap (multi-texture fill 129.6 against 178.2) | Lightmapped games (Quake 3-era engines, Unreal), 3DMark 2000/2001 | Large: new state, vertex format, the shared caps | Medium |
| 4 | **DirectX 8 DP2 path**: handle the DX7/8 opcodes natively, route indexed DP2 ops to `draw_indexed`, a word-wise FVF fast path, `-ox` for `d3d_dp2.c` and `r3d_records.c` | DX8 2.4x (1,813 against 966 cycles a triangle) | Every DirectX 8 game; 3DMark 2001 | Medium; host-testable | Low-medium |
| 5 | **Map 32 MiB on the Rage 128** (`rage128_device.map_bytes`) | 1024x1024 refusal; the heap doubles | Large-texture games; high resolutions with Z | Small | Low-medium (BAR0 is 64 MiB with two views; check the second view is untouched) |
| 6 | **DirectDraw source colour key, then stretch**, on the 2D engine with caps published | Keyed vid-to-vid 160x; stretched 16x | 2D DirectDraw games (sprites); video playback scaling | Medium | Low-medium |
| 7 | **DX7 per-call CPU cost**: lift the 64-triangle batch cap for engines that take any length; drop the per-batch vblank MMIO read; cache the resolved texture and mip chain per handle; `-ox` the remaining hot files | DX7/DX5 1.3x per triangle, 2.1x on large calls, state/texture switch 2.9x | CPU-bound scenes; games that switch textures every few triangles | Medium | Low |
| 8 | **Texture upload by the engine** (system-to-video Blt as a host-data or bus-master blit; Lock without a full drain where the texture is not in flight) | TexUpload.Blt 14x, UpdateTexture 3.8x, part of Lock 10x | Level loads, streaming | Medium-large | Medium |
| 9 | **GDI breadth**: pattern and hatch brushes, the source ROPs, StretchBlt, Output (shapes, lines), text | 11-230x on the rest of the opportunity list | Desktop applications | Large, and shared with every family (`dib_thunks.asm` punts Output and stretches for all) | Medium |
| 10 | **Re-measure Clear, video colour fill and Lock on a committed build** (section 0) | Settles whether the per-clear drain still costs what H4 found it does not | - | Small (a dispbench run) | None |
| 11 | **NOVSYNC**: tearing flips (FLIP_CNTL=1, no ring wait) when the application or the VSync setting asks | Flip.NoVsync; the fill-rate and texture tests' 59 Hz cap in 3DMark | Benchmarks | Small | Low; a policy call |
| 12 | **DriverDesc** for the 5046 | Cosmetic | - | Trivial | None |

On the guess that the frame-buffer mapping and the fills and screen
copies are the biggest wins: for the desktop, yes, and in that order - item
1 is small, touches every CPU path, and is the precondition for judging
how much of item 2's fill gain is left; item 2's screen copies are the
largest ratios item 1 cannot touch. For games, item 3 (two texture stages)
is the one the evidence points at for the remaining 3DMark gap, with item
4 for anything DirectX 8. Item 10 costs one run and should come before any
work on clears or Lock.
