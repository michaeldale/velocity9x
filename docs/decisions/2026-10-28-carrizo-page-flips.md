# Carrizo page flips through DCE 11's D1 surface address, taken at the blank

Date: 2026-10-28

Status: **measured, one machine, one boot.** M-XP7053's Radeon R7E
(`1002:9874`), boot 19, the `ati` DRV, VXD and HAL from this tree. Phase 5
of [the plan](../plans/ati-carrizo-gfx8.md). Captures in
[`../probe/m-xp7053-carrizo-page-flips-2026-10-28/`](../probe/m-xp7053-carrizo-page-flips-2026-10-28/).

## The change

Carrizo's DCE 11 display controller D1 has the same flip registers as
Evergreen's DCE 4, at the same byte offsets and with the same fields:
`GRPH_PRIMARY_SURFACE_ADDRESS` 6810h (bits 31:8), `_HIGH` 681Ch (bits
39:32), `GRPH_UPDATE` 6844h with update-pending at bit 2, and
`GRPH_FLIP_CONTROL` 6848h (Linux `dce_11_0_d.h`, `_sh_mask.h`). Linux's
`dce_v11_0_page_flip` writes the same sequence as `evergreen_page_flip`:
flip control, the high address, the low address (which triggers the
update), and a read to post it. So `eg_scanout.c` now serves both engine
types instead of a second copy joining the flip chain. On Carrizo:

- the high address is bits 39:32 of the heap's GPU address (F4h), where
  Evergreen writes 0;
- the heap's address comes from the SDMA engine (`v9x_vi_heap_address`);
- the drain before a flip is SDMA's (`v9x_vi_dma_settle`), and Evergreen's
  CP drain does not apply.

On Evergreen the code path is the same as before. The one difference is
order: its drain now runs before the address check rather than after, so
a flip refused on its address still waits for the engine first.
`carrizo_hw16.c` adds `V9X_DD_ENGINE_CAP_FLIP`.

## Boot 19

- DRV, VXD and HAL by WININIT rename, SHA-256 checked. `EngineCaps=7`,
  `CarrizoEngine=claimed`.
- **V9XDDP**, four runs: two at 1024x768x16, one at 32 bpp after
  `V9XMSW /set`, one after `V9XMSW /cycle:10` (10 of 10). Fill, copy and
  the four overlap cases pixel-correct in every run, as on boot 18.
- **Flips are taken by the controller.** V9XTRACE: `FlipDeclined` 0
  (boot 18, without the capability: 23). `FlipTakenAtDone` 46 after the
  16 bpp runs and 92 at the end, `FlipNotTakenAtDone` 0: every flip's
  update-pending bit had cleared by the time the flip state machine called
  it done. No idle timeouts, no resets, `CountBltEngine` = `CountBlt`.
- **They wait for the blank.** V9XDDP's 20 flips took 319, 331, 322 and
  323 ms, about 16 ms each at the reported 60 Hz; `FlipMaxMs` 17. On
  boot 18 the emulated flips took 1 ms for all 20.
- `FlipPixelOk` read 0 in every run. With real flips this cannot pass:
  the probe reads the screen back through GDI, which sees only GDI's page
  (V9XDDP's own comment at the test). It is not evidence either way.
- After the runs `V9XCZMM` read D1 back on the desktop page (GPU
  `F4_00000000`, nothing pending), and the desktop was intact.

## Not checked

- What the monitor shows during a flipping program: no owner report.
- A game or benchmark that flips continuously. None is installed on
  M-XP7053.
- D2, which the BIOS programs but leaves off.
