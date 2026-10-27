# HD 5450: a 128 MiB DirectDraw heap instead of the VBE's 16 MiB

Date: 2026-10-27

Status: **measured, one card.** M-BXMFA8's HD 5450 (`1002:68F9`, Cedar),
boots 36 and 37. Follows [page flips](2026-10-27-cedar-page-flips.md).
Captures: `qconsole-b3*`, `V9XGL-b3*` and `owner-*` in
[`../probe/m-bxmfa8-cedar-quake3-2026-10-27/`](../probe/m-bxmfa8-cedar-quake3-2026-10-27/);
`V9XSNA7-b3*` in
[`../probe/m-bxmfa8-cedar-3dmark2000-2026-10-27/`](../probe/m-bxmfa8-cedar-3dmark2000-2026-10-27/).

## What the owner saw, and the count behind it

At Quake 3's maximum texture detail (`r_picmip 0`, 32-bit textures,
800x600x32), the owner's screenshot shows the floor and a tower's light
panels flat pinkish-white, while walls, sky, weapon and HUD are right.
Re-run at their settings on boot 36 (16 MiB heap), the ICD counted:

- 801 texture creations failed and 798 textures were evicted;
- every failure was placement reason 4, "the heap had no room";
- 15,755 batches were refused by the hardware path, and only 6 went to
  the CPU.

The VBE reports 16 MiB of video memory (4F00h, `VbeVramBytes=16777216`).
The ati family mapped 16 MiB, sized for the Rage Mobility's BAR0, and
built the heap from that, on a card with 1 GiB.

## The change

- `cedar_hw16.c` sets the chip's `map_bytes` to 128 MiB. That is the
  per-chip mapping the HD 4600 uses for its heap beyond 16 MiB.
- The ati family gains a `measure_video_memory` hook. It answers with the
  whole mapping for the HD 5450 and 0 for every other chip, so they keep
  the VBE's figure exactly as before.
- **Why 128 MiB is safe here:**
  - it lies inside the card's 256 MiB BAR0;
  - it ends below the engine window at BAR0 + 240 MiB;
  - the card is known to hold at least 256 MiB, because the mini-VDD
    checks `CONFIG_MEMSIZE` before it maps that window.
- A host that refuses 128 MiB is asked again for less
  (`v9x_vbe_map_smaller`).

## Boot 37

- **The mapping:** `V9XBOOT.INI` reads `DpmiMap=08000000:ok` and
  `vram=134217728`. The desktop came up and the engine was claimed.
- **Quake 3 at the owner's settings:**
  - 0 failed creations, 0 evictions, 0 refused batches;
  - all 210,876 batches on the hardware;
  - 18.9 MB of textures uploaded, more than the whole old heap;
  - 62.6 fps.
- **3DMark 2000 default:**
  - completes at 4879 (4883 on boot 36; recorded, not compared);
  - 20,386 flips, engine valid;
  - texture creations 1,629, against 7,782 in boot 36's run.

## Not checked

- Whether the owner's disappearing trees in Game 1 are gone. They were
  seen on the 16 MiB build. The drop in texture creations fits memory
  pressure as the cause, but only the monitor can show it.
- No other ati chip has run this build. The hook answers 0 for all of
  them, which leaves their path unchanged by the code; untested.
