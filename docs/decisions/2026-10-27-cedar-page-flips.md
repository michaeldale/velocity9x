# HD 5450 page flips: display controller D1's surface address

Date: 2026-10-27

Status: **measured by counters on one card; the owner reports the
flicker gone** (chat, after boot 36). M-BXMFA8's HD 5450 (`1002:68F9`, Cedar), boot 36. Captures:
`V9XSNA7-b36-flip*.INI` and `b36-flip-score.png` in
[`../probe/m-bxmfa8-cedar-3dmark2000-2026-10-27/`](../probe/m-bxmfa8-cedar-3dmark2000-2026-10-27/),
`V9XD32-b36-flip-*` in
[`../probe/m-bxmfa8-cedar-d3d-hal-2026-10-27/`](../probe/m-bxmfa8-cedar-d3d-hal-2026-10-27/).

## What the owner saw

The card's owner ran 3DMark 2000 on the build before this one and wrote:
"there's som flickering/corruption going on, seems like it happend every
second frame. Looks normal on screenshots." Of Quake 3: "didn't flicker,
but there's some rendering issues" (not described yet; asked).

## Why

The Evergreen engine did not claim `V9X_DD_ENGINE_CAP_FLIP`, so the core
declined every flip (`v9x_can_set_display_start`). DirectDraw then
emulated the flip by swapping the chain's surface memory. The surface
the monitor scans out took its turn as the back buffer, and the game drew
into it on alternate frames. The agent's screenshots read video memory
after drawing finished, so they looked clean. This is a reading of the
code that matches the report, not a measurement of the picture.

## The change

`eg_scanout.c` follows the Rage 128's `r128_scanout.c`.

- **The write.** A flip sets D1's `GRPH_FLIP_CONTROL` to 0 (taken at the
  vertical blank) and writes `GRPH_PRIMARY_SURFACE_ADDRESS_HIGH` and
  `GRPH_PRIMARY_SURFACE_ADDRESS` with the heap's GPU base plus the
  surface's offset. It then reads the address back to post it. This is
  Linux `evergreen_page_flip`'s sequence, with registers from
  `evergreen_reg.h`: 6810h, 681Ch, 6848h.
- **Pending and vblank.** A flip is pending while `GRPH_UPDATE` (6844h)
  bit 2, SURFACE_UPDATE_PENDING, is set. The vertical blank is
  `CRTC_STATUS` (6E8Ch) bit 0.
- **Before the write.** The CP is drained and the DMA engine settled
  first, so the frame shown is finished.
- **When flips are taken.** The scanout counts as live only while D1's
  `CRTC_CONTROL` master enable and `GRPH_ENABLE` are set. D1 is the
  controller the VBE drove here: the register probe read D1 scanning GPU
  address 0. Otherwise flips are declined as before.
- **The 16-bit side.** `cedar_hw16.c` stamps `V9X_DD_ENGINE_CAP_FLIP`.
- **The address.** `v9x_eg_scanout_address` keeps it inside video memory
  and on 256 bytes. Its host test was written first and failed.

## Boot 36

| Run | Result |
|---|---|
| V9XD32, 16 and 32 bpp | PASS; 8 flips handled, 0 declined, 6 of 6 completions confirmed by the chip |
| 3DMark 2000 default | completes, 4883; 19,949 flips handled, 0 declined, 19,947 of 19,947 confirmed by the chip (`FlipTakenAtDone`), no stalls, no refused devices |

The emulated-flip run scored 5267 (boot 31). Flips now wait for the
vertical blank, and DirectDraw is told to retry until the previous one is
taken (`FlipStillDrawing` 8,333,764). Recorded, not compared.

## What the owner saw after

On this build the owner reported the 3DMark 2000 flicker gone. They also
reported something new in the Helicopter test: "sets of trees randomly
disappear and reappear, even those close to the helicopter." Quake 3's
rendering issue turned out to be the 16 MiB heap
([128 MiB heap](2026-10-27-cedar-heap-128-mib.md)).

## Not checked

- Whether the disappearing trees are another symptom of the 16 MiB heap.
