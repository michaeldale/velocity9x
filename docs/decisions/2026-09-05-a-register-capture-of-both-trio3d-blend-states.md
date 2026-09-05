# A register capture of both Trio3D blend states, and what it does not reach

Date: 2026-09-05
Status: measured on A8U4I5, boots 38 and 39, with the driver at HEAD's pair
untouched throughout. Continues
`2026-09-05-a-power-cycle-does-not-clear-the-trio3d-blend-state.md`.

## What was asked

The card was sitting in the bad state (`TexMatrixOk=90`, every alpha rung 0)
and nothing on record described the card's registers in either state. Capture
them while the bad state was on the card, so there is something to diff when
it next reads 108.

## What happened

The card flipped to the good state in the middle of the job, on its own, so
both captures exist and the diff is already taken.

Guest clock (it runs a year behind and in another zone; only the intervals
matter):

| Guest time | Boot | Event | Evidence |
|---|---|---|---|
| ~05:38 | 38 | probe: `TexMatrixOk=90`, alpha rungs 0 | `trio3d-a8u4i5-v9x-2026-09-05e-b38-before-survey.ini` |
| 05:55:50 | 38 | survey written, `Complete=yes` | `trio3d-a8u4i5-vgasurv-b38-bad-2026-09-05.ini` |
| 05:56:09 | 38 | last command the agent answered (a window inventory) | `AGENT.LOG` |
| 05:58:14 | 39 | `agent-start` - the machine had reset itself | `AGENT.LOG` |
| ~06:03 | 39 | probe: `TexMatrixOk=108`, alpha rungs 1, `SpriteOk=1` | `trio3d-a8u4i5-v9x-2026-09-05f-b39-good.ini` |
| 06:04:31 | 39 | survey written, `Complete=yes` | `trio3d-a8u4i5-vgasurv-b39-good-2026-09-05.ini` |
| ~06:05 | 39 | probe: still 108 | `trio3d-a8u4i5-v9x-2026-09-05g-b39-after-survey.ini` |

Each survey is bracketed by a probe on both sides in the same boot, and the
probe reading is the same on both sides. The survey did not move the state in
either direction.

**Nothing was executing when the machine reset.** The agent log shows the
window-inventory exec complete at 05:56:09 and then nothing until
`agent-start` on boot 39. Five hidden DOS VMs from failed survey launches (see
below) were alive at `Abort, Retry, Fail?` prompts; the display driver was
idle at the 800x600x16 desktop. This is the third transition on record and
the first one with a timeline: like the other two it coincides with the
machine going away, and this time the "going away" is a reset with no command
in flight. Whether the reset is the cause of the flip or merely the occasion
for it is not known. Whether the machine resets on its own without anyone
present is now a question worth asking of the hardware, not the driver.

## The diff

`diff` of the two survey files, CRs stripped, is eight lines. Three are the
header (`Time`, `CommandLine`, `Note`). The rest:

```
                  b38 (bad, 90)                        b39 (good, 108)
InputStatus0      19                                   09
Crtc.30           E109000010001E9F5BBD157542002400     E109008010001E9F5BBD157542002A00
                        ^^                    ^^             ^^                    ^^
                        CR32=00               CR3E=24        CR32=80               CR3E=2A
```

Everything else in the two files is byte-identical: all 256 bytes of PCI
configuration space including the AGP capability at `80h` and the power
management capability at `DCh`, the video BIOS header, PCIR and strings, the
VBE controller block and all 63 modes, and every standard VGA bank.

What can and cannot be read from that:

- **CR32 bit 7 and CR3E bits 1-3 differ between the two files.** Two samples,
  one per state, on consecutive boots. Whether they track the state or the
  boot cannot be separated with one sample of each; a second boot in either
  state would settle it. Neither register is decoded anywhere in this tree,
  and the Trio3D databook is not on disk; DB014-B (Trio32/64) is the nearest
  thing cited here and is not the right part. No meaning is assigned.
- **Input Status 0 bit 4** is the VGA switch-sense bit, a comparator reading
  rather than a latched state. It is recorded; it is not to be read as a
  finding.

## What the capture reaches, and what it does not

This is a VGA/CRTC sweep run from a windowed DOS box under Windows 98, and
the fault is in the S3D engine. Plainly:

**Not reached: the S3D engine.** Its registers are memory-mapped in the
aperture, and the survey does port I/O and BIOS calls only; Tier 1 has no
memory read at all and `/aperture`, which reads framebuffer bytes and not
engine registers, was not requested. If the state that separates 108 from 90
lives in the engine - the natural place for a blend fault - **it is not in
these files**. An instrument that can read it has to run as a Win32 or ring-0
component with the aperture mapped, which is a new tool, not a survey switch.

**Not reached: the extended bank behind the locks.** Tier 2 was deliberately
not run. It writes the CR38/CR39/SR08 unlock keys to the live card and then
writes back what it read before, and on S3 parts the readback of CR38/CR39 is
a latch value (`5B`/`BD` here, as on the 486 Trio64), not the unlock key -
so the "restore" leaves the extended bank *locked* under a display driver that
unlocked it once at enable and relies on it staying open. That is a
perturbation of the running driver, which the task said to leave alone. So
CR40-CR6F and SR09-SR1F, including the MCLK PLL and the linear window
registers, are absent.

**Virtualised: the standard VGA banks.** `Trust=virtualized` is correct for
`Seq.`, `Crtc.00`-`Crtc.18`, `Gdc.`, `Atc.`, `Misc` and the DAC state: they
read as an 80x25 text mode (`Crtc.00=5F4F5082...`), which is the DOS VM's
virtual adapter, not the 800x600x16 desktop the card was actually scanning
out. Identical in both files and meaningless for this diff.

**Reached, and real: the extended CRTC indices `2D`-`3F`.** The VDD does not
model these, and what comes back is the silicon's: CR2D/CR2E/CR2F read
`8A`/`13`/`02`, which is the PCI device id and revision the same file reports
from configuration space, and CR30 reads `E1`. The same leak was seen on
BARRY's Trio64 on 2026-08-26 (`Crtc.20=...881102`, `Crtc.30=E1...`). It is
the only part of the register sweep that says anything about the card, and it
is where the two differences sit. Whether every index in `30`-`3F` is passed
through, or some are answered by the VDD, is not established; the device-id
triple is the evidence that at least part of the range is real.

**Reached, and real: PCI configuration space and the ROM.** Read through the
PCI BIOS and the `C000` shadow, not through the VDD. Identical in both states,
so whatever flips is not a configuration-space write - AGP command, PM state
and the BAR are the same in both files.

## Running a DOS tool through the agent

Recorded because it cost four failed launches and five orphaned VMs:

`V9XSURV.EXE` launched with the agent's pipe capture - `exec` direct, or
`shell` through `COMMAND.COM` - prints its banner and then stops at a DOS
critical error, `General failure reading device` when stdout is the agent's
pipe and `General failure reading drive C` when stdout is redirected to a
file or `NUL`. The report is created and stays at zero bytes. `V9XSURV /?`,
which only prints, runs fine through the same pipes, and an 8.3 path changes
nothing. The same symptom was seen once on BARRY on 2026-08-26 and attributed
there to a live mode switch; that attribution is now doubtful.

Launched with `exec -Detach` - no pipes, the VM's own hidden console - the
same command line completes in a few seconds. The report is then read with
`get`, which is safe once the tool has exited. The failing VMs sit at
`Abort, Retry, Fail?` with the agent's `TerminateProcess` having no effect on
them, and only a reboot clears them; they hold nothing on the card, because
the fault is before the first port access.

## Boot 40: the third sample, and what it takes away

A deliberate warm restart through the agent, later the same day. The desktop
came up on boot 40, the probe read 108 with every alpha rung passing, the
survey was taken detached, and the probe read 108 again after it
(`trio3d-a8u4i5-v9x-2026-09-05h-b40-before-survey.ini`,
`trio3d-a8u4i5-vgasurv-b40-good-2026-09-05.ini`,
`trio3d-a8u4i5-v9x-2026-09-05i-b40-after-survey.ini`). The good state
survived a warm restart, as it did across boots 29-33.

```
             b38 (bad)   b39 (good)   b40 (good)
CR32         00          80           00
CR3E         24          2A           2D
```

**Neither byte tracks the state.** CR32 reads `00` in a bad boot and in a good
one, and CR3E has taken three values on three boots. Both are boot-to-boot
noise as far as this sweep can tell. With Input Status 0 already set aside,
the two good-state files differ from each other only in those bytes and the
header, and the bad-state file differs from both by no more than they differ
from each other. **Nothing this sweep reaches distinguishes the two states.**
That is the answer the capture was taken to get, and it points where the
reach section already pointed: at the engine, which the survey cannot read.

## What to do with this

- **Stop looking in the VGA register file.** Three boots say the sweep's
  reachable registers do not carry the state. Taking the survey beside a probe
  still costs five seconds and would catch a fourth value, but it is no longer
  where the answer is expected.
- **The engine is still unread.** A Win32 instrument that reads the S3D
  setup and status registers through the aperture - or an escape into the HAL
  that dumps them, in the style of `V9X_DDGETCOUNTS` - is what a capture of
  the *fault* needs. The survey cannot be extended to do it from real mode.
- **Ask whether A8U4I5 resets on its own.** Three transitions, three
  disappearances, one of them now known to have happened with nothing
  running.

## Gates

`./scripts/check-tree.ps1` passed (documentation and reference files only;
no code changed). The survey was built from `9a7b665` by
`scripts/build-vga-survey.ps1`, whose safety gate passed. The driver on the
machine was not touched: `V9XHW.INI` on boot 38 reads
`Adapter=S3 Trio3D/2X`, `Direct3D=hardware-s3d`, `Direct3DMode=hardware`,
and boot 39 came up `Stage=enable-ok` with the same MTRR line.

Left on the guest: `C:\V9XDIAG\V9XSURV.EXE`, the two reports beside it, a
zero-byte `V9XSURV.INI` from a failed launch, and `C:\V9XREMOTE\JOBS\SURV0905`
holding the survey, the probe, the window tool and two zero-byte files.
Nothing there runs at boot.
