# HD 4600: the pipe-source fix's write path, run on purpose

Date: 2026-11-28

Issue: [2026-11-18-haswell-vga-1080p-pipe-source-left-at-native.md](../issues/2026-11-18-haswell-vga-1080p-pipe-source-left-at-native.md).
Evidence: [`../probe/hd4600-pipe-source-write-2026-11-28/`](../probe/hd4600-pipe-source-write-2026-11-28/)
(the `[PipeSource]` section of each V9XHSWMM run, in order).

## The branch was already in main

`hsw-panel-fit` holds one commit, `2e189d5a` (2026-11-18), and it sits on
main's own first-parent history: it was committed to main that night and
has shipped since 0.56.0 (its changelog entry is under 0.56.0), 1.0.0
included. The 1.0 plan's "left for 1.1, not merged" (decided
2026-11-26), STATUS.md and the issue's 1.0 re-read were wrong about
that. What the issue still owed was a run of the write itself: on every
boot since, the VBIOS had set the source to the mode and the driver
reported `PipeSource=matches`.

## How it was run

V9XHSWMM gains `/pipesrc:XXXXXXXX` (DIOC 7 in `hsw_mmio_probe.asm`): it
reads PF_CTL_A, the totals and PIPE_SRCSZ_A, and with a non-zero value
writes PIPE_SRCSZ_A, refused unless the panel fitter is on. A test aid:
it puts the source where the VBIOS once left it.

The i5-4570 (W9QI5A83), boot 300, its 1.0.0 intel-gma set (`82112c9c`),
desktop 800x600, pipe A at 800x600 timing with the fitter on
(`PF_CTL_A=80800000`, `HTOTAL_A=041F031F`). The source written was
640x480 (`027F01DF`), not the field's 1920x1080: a source larger than
the pipe is a downscale this pipe was never seen to take, while 640x480
is an ordinary upscale that differs from the mode, which is all the
driver's check (HSW_PIPE_SOURCE in `loader.asm`) looks at.

| Step | PIPE_SRCSZ_A |
|---|---|
| Read | 031F0257 (800x600) |
| `/pipesrc:027F01DF` | 027F01DF, read back |
| Read again (another probe run, no DirectDraw) | 027F01DF |
| V9XGLP run (windowed OpenGL: a DirectDraw session, no mode set) | 031F0257 |
| Again: write 027F01DF | 027F01DF |
| Read after 60 s, nothing else run | 027F01DF |
| V9XGLP run | 031F0257 |

Nothing but the driver's HSW_PIPE_SOURCE writes this register, and in
the 60 s control the injected value stayed. So the driver's engine open
saw the fitter on, the source different from its 800x600 mode and the
mode inside the pipe, and wrote the mode's size: the write path works on
the chip.

## Not seen

The picture. The agent's screenshot reads the frame buffer, not the
scanout, so neither the stretched 640x480 nor the restored frame was
looked at. The field's own sequence (1080p timing with a 1920x1080
source under a 1024-pixel surface) has still not been reproduced; this
run covers the driver's half of it.

The 4570 keeps the new `HSWMM.VXD` in `C:\WINDOWS\SYSTEM` (the old one
as `C:\V9XDIAG\HSWMM.OLD`; DIOCs 1-6 are unchanged) and the new probe
as `C:\V9XDIAG\V9XHSWPS.EXE`.
