# Carrizo: flip waits poll the display controller, re-reading its state each time

Date: 2026-11-18

Seen on: M-XP7053, Radeon R7E (Carrizo, GCN 3), 0.54.0, 3DMark 2001 SE
under V9XPROF ([record](../decisions/2026-11-18-carrizo-3dmark2001se-profile.md)).
The `eg_scanout.c` code is shared by the Evergreen, Trinity and GCN rows.
Status: open, cause of the time not separated.

## What was measured

6.0 % of all samples in a whole benchmark run, all in `eg_scanout.c`:

| Function | Share |
|---|---|
| `v9x_eg_scanout_live` | 4.5 % |
| `v9x_eg_flip_pending` | 1.1 % |
| `v9x_eg_in_vblank` | 0.3 % |

`v9x_eg_scanout_live` reads two display registers, `CRTC_CONTROL` and
`GRPH_ENABLE`, on every call, through `v9x_eg_scanout_active`. The
flip-status and vblank checks reach it on every poll.

## Reading

Most of this is probably time spent waiting for a flip or a vertical
blank, and the time would go somewhere else if the poll were cheaper.
DirectDraw loops on the HAL while a flip is pending, and 3DMark may wait
for vertical blank. Two things are not known:
- whether the waits are the application's (expected) or the driver's
  own;
- how much the per-poll register reads of `scanout_live` add on top of
  the waiting.

## Next

- Count flip-status polls and vblank queries per flip (V9XTRACE has
  flip counters; a poll counter would be new).
- Cache the scanout's live state across a flip's polls, since the CRTC
  and graphics enable do not change between them. Then rerun the same
  V9XPROF profile and compare this share.
