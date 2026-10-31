# HD 7470 page flips through D1, and Quake 3 lost on the ati build

Date: 2026-10-31
Status: **flips handled and confirmed by the chip; vblank pacing not
shown.** M-D7TFW4's HD 7470 (`1002:6778`, Caicos), boot 5, code
`899658c5`. **Regression found:** on the ati build Quake 3 does not start
on this card, where it ran in software under the vbe package.
Plan: [ati-evergreen-juniper-caicos.md](../plans/ati-evergreen-juniper-caicos.md),
Phase 3. Captures:
[`docs/probe/m-d7tfw4-caicos-flips-2026-10-31/`](../probe/m-d7tfw4-caicos-flips-2026-10-31/).

## The change

`eg_scanout.c` took the heap's GPU address from the CP
(`v9x_eg_cp_addresses`). Since Phase 2 the CP refuses VRAM above 4 GiB,
so the HD 7470 could not flip. The address now comes from the DMA engine,
with its high byte: `v9x_eg_heap_address` in `eng_eg.c`, the twin of
Carrizo's `v9x_vi_heap_address`. It is a new HAL-internal symbol (Michael
agreed, 2026-10-31) and serves both Evergreen chips. Cedar's address is
unchanged: high byte 0, and the same 256 MiB BAR0 mask as the CP's. The
CP drain and DMA settle before the write stay as they were. Linux flips
BTC parts with the same `evergreen_page_flip` (`radeon_asic.c`
`btc_asic`), so the registers are Cedar's: 6810h, 681Ch, 6844h, 6848h.
`cedar_hw16.c` adds `FLIP` to Caicos's claim. `run-checks.ps1` passed.

## Boot 5

The three binaries went in by WININIT renames and `reset` (boot 4 to 5),
checked by hash afterwards.

| | Value |
|---|---|
| `V9XHW.INI` | `EngineCaps=00000007` (fill, copy, flip), engine claimed |
| V9XDDP, two runs | `Result=COMPLETE`; fill and copy checks 1 |
| V9XTRACE after both | 46 flips handled, 0 declined, `FlipTakenAtDone` 46 of 46, `FlipNotTakenAtDone` 0, `FlipStillDrawing` 3,625, `FlipDoneFirstPoll` 42, `ScanoutUnresolved` 0 |

The chip confirmed every flip it was given: `GRPH_UPDATE`'s pending bit
was clear when the core called each flip done. Not measured: whether the
new address was taken at the vertical blank. V9XDDP's `Flip20Ms` (16 and
6 ms) and `FlipPixelOk` (0) are the instrument defect in
[the 2026-09-02 issue](../issues/2026-09-02-flippixelok-is-uninterpretable.md);
Cedar's V9XDDP read `Flip20Ms=1` after its flips were confirmed in 3DMark.
`FlipDoneFirstPoll` 42 of 46 says the previous flip was usually done by
the next one, which V9XDDP's back-to-back flips would show either way.

The installed V9XTRACE (0.33.0) refused this HAL's snapshot
(`Error=abi-mismatch`); this build's copy was put in `C:\V9XDIAG`.

## Quake 3 does not start on this build

The plan was to measure flips under a real swap chain: the owner's Quake
3 demo, at its saved fullscreen 640x480, with `+set logfile 2 +set
nextdemo quit +timedemo 1 +demo demo001`. Only non-archived cvars were
set, the owner's `q3config.cfg` was backed up first, and it was put back
afterwards (SHA-256 matches the backup).

It never drew. `qconsole.log`: "23 PFDs found ... GLW_ChoosePFD failed
... failed to find an appropriate PIXELFORMAT" at 16-bit colour and
depth, then the same in fullscreen, then a fall back to `3dfxvgl`, which
is absent. The only Velocity9x GL log written was `V9XGLD3.LOG`, the
0.33.0 package's GL DLL (`build=0f45aa6f`), with an attach line and
nothing else. This build's `V9XGL.DLL` was installed and matched by hash
before the run, but did not log.

On 2026-10-29, under the 0.33.0 vbe package, the owner ran the same demo
and `V9XGL.LOG` shows the ICD drawing it in software (`engine=1`,
400 batches on the CPU path). The difference on the ati build is that an
engine is claimed without `D3D`, which leaves `Direct3DMode=none`: no
software Direct3D, and no render interface for the ICD. Cedar was in the
same state at its boot 32 ("device: no render interface") until its CP
drew GL. Which DLL Quake 3's `opengl32` reached, and why `V9XGLD3`
attached, was not followed up.

So for the owner, this build trades software GL for engine fills, copies
and flips. Until Caicos draws Direct3D on the CP (Phase 5, which needs
the CP's streams to carry 40-bit addresses), Quake 3 runs only on the vbe
package.

## Not established

- Vertical-blank pacing, and what the monitor shows. Only the owner can
  see it, and no fullscreen title runs on this build.
- Cedar on this build.

## Next

A decision for Michael: leave M-D7TFW4 on this build, or put the vbe
package back so the owner's Quake 3 works until Phase 5.
