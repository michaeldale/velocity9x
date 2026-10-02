# Rage IIC scanout start: CRTC_OFF_PITCH moves the picture, mid-frame

Date: 2026-10-03. Machine: A8U4I5 (10.0.1.172), ATI 3D Rage IIC AGP,
Velocity9x `ati` (`eb3f3e4` HAL), VBE mode 1024x768x16, boot 141.
Harness: `ATIRX.EXE /crtcread` then `/crtc` (`653a5e7`). Evidence:
`docs/probe/a8u4i5-rage-iic-registers-2026-10-02/BOOT141-ATIRX-CRTCREAD.TXT`,
`BOOT141-ATIRX-CRTC.TXT`, and Michael watching the monitor.
For: [flips never presented](../issues/2026-10-02-rage-iic-flips-never-presented.md).

## Read

| Register | Value | Meaning |
|---|---|---|
| CRTC_H_TOTAL_DISP (+0x400) | `0x007F00A7` | 1024 displayed |
| CRTC_V_TOTAL_DISP (+0x408) | `0x02FF0325` | 768 displayed, 806 total |
| CRTC_OFF_PITCH (+0x414) | `0x20000000` | offset 0, pitch 1024 pixels |
| CRTC_VLINE_CRNT_VLINE (+0x410) | 26:16 read 210, 494, 736, 171, ... | the beam, moving through 0..805 |

## Watched

- **A**: a test picture (red/blue halves, white diagonal, green frame)
  drawn at VRAM 0x200000, CRTC_OFF_PITCH written `0x20040000` (read back
  so) for 10 s, then `0x20000000`. Michael: the picture showed, correctly,
  and the desktop came back.
- **B**: for 10 s, the picture's start written when the beam was at line
  384 and the desktop's at line 600, every frame (596 frames, no line
  missed). Michael: a band of the picture across the middle, **seen for
  about 0.5-1 s** rather than the whole 10 s.

## Conclusions

1. CRTC_OFF_PITCH's offset field (8-byte units, 19:0) is the scanout
   start in the VBE modes; the pitch and upper bits are kept as read.
2. A mid-frame write showed mid-frame, so the start is not held for the
   next frame. Flips are therefore written inside the vertical blank
   (lines 768..805 here, short of the last four) and completed when that
   blank ends, through the flip state machine's existing issued-in-blank
   path. Written there, an immediate start and a frame-latched one look
   the same, so the brief band does not change the design.
3. The blank is read from CRTC_VLINE against CRTC_V_TOTAL_DISP, not the
   VGA status port, and the VGA start-address write (S3's CR0C/CR0D/CR69)
   is never used on this chip.

## Not explained

Why the band was visible for only 0.5-1 s of the 10. The log shows
switches through the whole phase. Possibly the start latches at some
point in the frame most of the time and only some writes landed before
it; not measured, and not needed for an in-blank write.
