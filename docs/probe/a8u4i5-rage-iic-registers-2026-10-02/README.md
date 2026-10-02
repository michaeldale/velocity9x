# Rage IIC read-only register probe, A8U4I5

- Machine: A8U4I5, 10.0.1.172, Windows 98 SE, boot 125, agent 0.8.0
- Adapter: PCI `1002:4757`, subsystem `1002:4757`, revision `7A`, 4 MiB,
  bus 1 device 0 (behind the 440BX AGP bridge)
- Driver bound: ATI community build 4.11.2611 (`ati2ddad.drv`), desktop
  1024x768x16. 3DMark 99, Final Reality and Half-Life D3D had run on this
  card earlier the same day; boot 125 followed the 2611 install, and the
  baseline runs on 2611 were made in it.
- Tool: `ATIIC.EXE` + `ATIIC.VXD`, `scripts/build-ati-rage-iic-probe.ps1`.
  No MMIO or PCI configuration write; see the tool's header comment.

| File | Build | Offsets | Notes |
|---|---|---|---|
| `ATIIC-BLOCK0.TXT` | `atiic-20261002-b` | block 0, 239 | DAC_REGS and HOST_DATA0-15 skipped |
| `ATIIC-BLOCK1.TXT` | `atiic-20261002-c` | blocks 1 and 0, 463 | also skips block 1 +180h-+1FCh (bus master) |
| `ATIIC.ROM` | `atiic-20261002-b` | - | 64 KiB from C0000h; the image is 32 KiB, checksum 0 |

Both runs exited 0 in under 100 ms and the agent answered normally
afterwards, same boot. The only repeat-read deltas were `+410`
(CRTC_VLINE_CRNT_VLINE) and, in the second run, `+418` bit 5
(CRTC_INT_CNTL, a vblank status bit).

## Install preflight, same boot

The ati package at commit `397da71` (first with `1002:4757`) was
downloaded into `C:\V9XDIAG`. The display class key was exported first
(`display-class-before-velocity9x.reg`; the live entry is "RAGE IIC AGP
(4757)", `ati2ddad.drv`).

`V9XSTAGE.EXE` was run twice. The second run, waited on by the agent,
exited 0: PASS, the VxD and `V9XDISP.DRV` loaded beside ATI's driver
and unloaded cleanly. The first run's result box was dismissed unread.

During the first run the screen contents were overwritten: two agent
screenshots taken with its result box up came back as unrelated noise,
different each time, one with a band that looks like text-mode memory.
The mode survived. After the box closed, a desktop F5 repaint brought
the desktop back intact (screenshots, 17:09 and 17:10 guest time). What
wrote to video memory, whether the DRV load, its VBE queries or the
probe VxD, was not determined. The second run was not screenshotted.
Earlier screenshots on this driver, from the baseline runs, were clean.

Interpretation: [`../../decisions/2026-10-02-rage-iic-register-survey.md`](../../decisions/2026-10-02-rage-iic-register-survey.md).
