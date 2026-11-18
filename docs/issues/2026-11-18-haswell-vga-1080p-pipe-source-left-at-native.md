# HD 4600 on its VGA port into a 1080p capture: the picture is drawn twice across and wraps

Date: 2026-11-18. Machine: the i5-4570 (HD 4600, 8086:0412), boot 276,
intel-gma `park/hsw-rcs-clears` build (ade25330). Status: cause measured
to the register; fix not written.

## What was seen

Michael's three OBS recordings of 2026-11-18 22:43-22:44 (Y:\MWD\videos\obs)
show 3DMark 2001 SE Nature and the Quake 3 demo's menu with the frame drawn
twice side by side, the first copy about half the width, and the lower part
of the screen showing the top of another buffer or striped garbage. Colours
are right. Frames from two of them are in
`docs/probe/hd4600-vga-pipesrc-2026-11-18/`. Recordings from 2026-11-17 (a
different machine) look normal.

## Registers

V9XHSWMM, read-only, taken while Quake 3 was full screen
(`boot276-quake3-V9XHSWMM.INI`; the probe's window list gained 68000h-69000h
and four offsets for this):

| Register | Value | Reading |
|---|---|---|
| PIPE_HTOTAL_A / VTOTAL_A | 0897077F / 04640437 | 1920x1080 active |
| PIPE_SRCSZ_A | 077F0437 | source 1920x1080 |
| TRANS_DDI_FUNC_CTL_A (i915) | C4034002 | enabled, DDI E, FDI mode: the analog VGA port through the PCH |
| PF_CTL_A (i915) | 80800000 | panel fitter on, medium filter |
| PF_WIN_POS_A / SZ_A (i915) | 00000000 / 07800438 | window 0,0 1920x1080 |
| PRI_CTL_A | 98000000 | plane on, 32 bpp |
| PRI_STRIDE_A | 00001000 | 4096 bytes: a 1024-pixel row |

The pipe runs 1920x1080 timing with the fitter on, but its source size is
also 1920x1080, so the fitter scales 1:1 and the plane fetches 1920 pixels a
line from a surface whose rows are 1024 pixels: one row and most of the
next on every line (two copies, the first 1024/1920 = 53 % of the width),
and 768 rows filling only part of the 1080 lines (the wrap at the bottom).

On 2026-11-10 the same machine, then on a monitor, ran the desktop at true
800x600 timing: HTOTAL 041F031F, SRCSZ 031F0257, stride 1600
(`docs/probe/hd4600-dos-box-2026-11-10/hswdos1-after-dos-V9XHSWMM.INI`).
So the VBIOS chose native 1080p timing plus the fitter only now, with what
is on the VGA port now; it did not set the source size to the mode.
Nothing in the driver writes PIPE_SRCSZ or the fitter (i9xx_cover.c only
reads PIPE_SRCSZ).

## Not known

- What sits on the VGA port: by the 1080p capture, a VGA-to-HDMI converter
  whose EDID offers 1080p. Not confirmed with Michael.
- Whether the desktop is doubled the same way (the agent's desktop is
  1024x768 and stride 4096 matches it; Quake 3 may be at the desktop's
  mode). Not seen.
- Whether writing PIPE_SRCSZ to the mode's size, as i915 does when the
  fitter is on, gives a right picture with the VBIOS's 1920x1080 window
  (stretched) - the obvious fix - or whether the window should be
  pillarboxed. Untested.

## Cause, and the fix so far (same day)

Michael: the monitor (capture) was not connected when the machine booted.
That fits: with no display at POST the VBIOS fell back to native 1080p
timing plus the fitter, and its later mode sets never set the source size.

Branch `hsw-panel-fit`: mini-VDD API 33h HSW_PIPE_SOURCE writes
PIPE_SRCSZ_A to the mode's size when PF_CTL_A is on, the source differs
and the mode fits the pipe's active area; hsw_hw16.c calls it on every
engine open (Enable and each DirectDraw session) and publishes
`PipeSource=` / `PipeSourceFound=` in V9XHW.INI. run-checks green;
installed on the 4570 by run-release-checks (9 of 9 probes exit 0).

Boot 278, booted with the capture connected: the VBIOS set true 800x600
timing (HTOTAL 041F031F, SRCSZ 031F0257) with the fitter on at a 1:1
800x600 window, and `PipeSource=matches`: the call works, but the write
path has not run on hardware, and the fault does not reproduce on a boot
with the display connected (`boot278-*` in the probe folder). Owed: a boot
with the display unplugged, then the capture, then a full-screen game.

Boot 279, booted with nothing on the VGA port (Michael): the desktop came
up at true 800x600 timing, and Quake 3 started headless set true 1024x768
timing (HTOTAL 053F03FF, SRCSZ 03FF02FF, fitter on at a 1:1 window);
`PipeSource=matches` both times. Connecting the capture while Quake 3 ran
reprogrammed the pipe without any mode set from Windows (by the VBIOS's
hotplug handling, presumably): 1920x1080 timing, window 1920x1080, and the
source left at 1024x768, which is right, the fitter scaling the frame to
16:9. Restarting Quake 3 kept it (`matches`). Michael's recording
2026-11-18 23-34-16 shows a whole menu per frame. So the 22:43 state, a
1920x1080 source under a 1024-pixel plane, did not reproduce from either a
headless boot or a hotplug; the sequence that produced it is unknown. The
fix's write path is still unexercised on hardware.
