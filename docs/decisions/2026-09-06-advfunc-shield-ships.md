# The mini-VDD swallows a DOS box's write to ADVFUNC_CNTL, in every build

Date: 2026-09-06
Status: **shipped in the default mini-VDD; measured working as the traced
variant on the physical Trio64 (6/6) and harmless on a physical ViRGE/DX and
two 86Box guests as the shipping form; not yet re-measured as the shipping
form on a physical Trio64, because none is reachable tonight.**

## The finding this rests on

[The DOS-box issue](../issues/2026-09-06-dos-box-doubles-the-desktop-on-physical-trio64.md)
carries the measurement: on A8U4I5 with the PCI Trio64, the `-IoTrace`
mini-VDD logged every V86 access to the 8514/A register file across a
windowed DOS box and found exactly one - a byte write of `02H` to `4AE8H`,
ADVFUNC_CNTL, from the DOS VM's video BIOS. That write clears ENB EHFC. The
chip leaves enhanced mode with the desktop still displayed, the same DRAM is
then addressed as VGA planes by CPU and CRTC alike, and the desktop appears
twice side by side at half size: the "doubled desktop" of both physical
Trio64 boards. A CPU-data text command caught mid-transfer by the same write
never completes, which is the BARRY hang
([issue](../issues/2026-09-06-text-acceleration-hangs-physical-trio64.md)).

The system VDD traps the standard VGA ports for a V86 VM and not this one, so
the write reaches the hardware. A `-ShieldAdvFunc` variant of the trace
mini-VDD that swallowed it fixed six DOS boxes of six with acceleration on,
against five doubled of five without. It was held back that afternoon
because the same machine then hard-locked, and until that was traced to the
board ([issue](../issues/2026-09-06-a8u4i5-trio64-hard-locks-on-framebuffer-readback.md))
nothing new could be trusted.

## What ships

`build-minivdd-skeleton.ps1` now builds the shield into every mini-VDD.
Two forms, because `Install_IO_Handler` admits one handler per port:

- **Shipping form** (`V9X_ADVFUNC_SHIELD`, selected when neither `-IoTrace`
  nor `-NoShieldAdvFunc` is given): one trap on `4AE8H`. The System VM's
  accesses pass through at their width by handle comparison, so the display
  driver's own mode set lands unchanged. Any other VM's write is swallowed
  and counted in `V9xAdvFuncShieldCount`; its reads still return the real
  register, so a BIOS that reads back sees the hardware's state and not a
  shadow. String and repeated forms return to `Simulate_IO`. The install is
  reported on the serial line as `V9X-MINI advfunc-shield on`, written only
  when it took, and the build script asserts that marker in the image.
- **Trace form** (`-IoTrace`): the shield is a branch of the 21-port trace
  handler, as measured, and is logged like every other access so `V9XIOTR`
  counts how often it fired. It is now on by default there too.
- `-NoShieldAdvFunc` removes it from either form, for the A/B. The old
  `-ShieldAdvFunc` switch is gone.

The System VM is told apart by handle on each access rather than by
`Disable_Local_Trapping` at `Sys_VM_Init`, because that call was measured not
to take on the trace build (`SysVmOff=0` with `Installed=21`).

**The mini-VDD is one binary for every family**, so the trap is installed on
ati, matrox-m2 and vbe machines as well, where nothing decodes `4AE8H`. A
V86 write there would have reached nothing; swallowing it changes nothing.
The one card class where it would matter, an 8514/A-compatible driven by its
own DOS software, is not one this project drives. Not measured on those
families tonight; the reasoning is stated so it can be checked.

## Measured tonight, shipping form

| Machine | Build | Result |
|---|---|---|
| A8U4I5, physical ViRGE/DX `5333:8A01`, 800x600x16 | `-IoTrace` (shield on) | one DOS box: `Count=0`, `Logged=0`, `Installed=21` - **the ViRGE's BIOS makes no 8514/A port access at all**, which is why the ViRGE never doubled |
| A8U4I5, same | shipping | boot 82: three DOS boxes, desktop intact by screenshot; `V9XGDI /accel` with text **PASS** (500 operations, 84 strings, 0 fallbacks, 0 poisoned) |
| 86Box Trio64 guest (`:9871`), 800x600x16 | shipping | boot 332: three DOS boxes, desktop intact; `V9XGDI /accel` **PASS** (fills 160, copies 131, `GdiAccelText=0` on that guest) |
| gates | | `check-tree`, `run-checks` (host tests, four family packages) pass; all four mini-VDD variants (`-IoTrace`, `-NoShieldAdvFunc`, both, neither) assemble |

Neither of tonight's targets can show the fix working, only that it does no
harm: the emulator never doubled and the ViRGE's BIOS never writes the port.
The 6/6 on the physical Trio64 was the trace form of the same branch. The
shipping form's first physical Trio64 boot is the measurement still owed,
and BARRY is where it belongs once it is back; the record to update is the
DOS-box issue.

## What this does not fix

The A8U4I5 hard lock with the Trio64 fitted. That is the board against that
card, under Microsoft's driver as much as this one, and no port trap reaches
it.
