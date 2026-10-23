# A DOS VM's video BIOS reprograms the Intel display, so the mini-VDD no longer lets a windowed VM run a mode set

Date: 2026-10-23
Machines:
- MICHAEL-NETBOOK (945GSE / GMA 950, `8086:27AE`), Win98 SE, agent 0.12.0
  at 10.0.1.6:9869 (new wifi card), boots 222-228, display set merged main
  `cd04a7b8`, desktop 1024x576x16. Only `V9XMINI.VXD` changed across these
  boots.
- W9QI5A83, the Dell i5-4570 (HD 4600, `8086:0412`), Win98 SE, agent at
  10.0.1.254:9869, boots 65-70, intel-gma dev set from boot 62-65, desktop
  800x600x16. Only `V9XMINI.VXD` changed.

Issue: [`2026-09-12-netbook-dos-box-return-hardlock.md`](../issues/2026-09-12-netbook-dos-box-return-hardlock.md),
whose 2026-09-24 section established that any DOS VM - windowed, even
hidden - leaves the panel showing the VGA plane while GDI readback stays
correct, and whose 2026-10-22 section saw the same on Ironlake under `vbe`.

**Status: windowed DOS boxes fixed on the GMA 950 and the HD 4600, measured
by live display registers, the DOS window's contents, and on the HD 4600 the
captured video output. Every family's mini-VDD carries the fix; no non-Intel
card and no other Intel generation was measured. Full-screen DOS is unchanged
(its return still hard-locks the netbook; not retried).**

## Who writes the registers

[`2026-09-24-a-dos-vm-switches-the-netbook-to-the-vga-plane.md`](2026-09-24-a-dos-vm-switches-the-netbook-to-the-vga-plane.md)
left this open. Two sources agree on the mechanism:

- The Win3.1 DDK's VDD (the ancestor of Win9x `VDD.VXD`, whose own source
  was never published) runs `Exec_Int 10h` with AX=0003h in every non-system
  VM at `VM_Init`, windowed or not, with only the standard VGA ports trapped.
- The Win98 DDK's S3 and ATI mini-VDDs trap their BIOS's extended ports for
  exactly this reason ("written to during ROM BIOS mode changes", ATI.ASM's
  1CEh/1CFh setup) and decide each access on `VDD_Get_VM_Info`'s CRTC owner
  (S3MINI.ASM `MiniVDD_Virtual4AE8H`).

A real-mode BIOS reaches an Intel IGD's 32-bit registers through the I/O
BAR's `MMIO_INDEX`/`MMIO_DATA` pair ("indirectly accessible via the
MMIO_INDEX and MMIO_DATA I/O registers", G45 PRM vol. 1a, 5.1.3). The main
VDD does not trap it.

## Instrument

`-IoTrace` now also finds the display adapter - the first PCI function of
class 030000h with I/O decoding on, buses 0-15 - sizes its I/O BARs and traps
every port of them for V86 VMs, logging up to 4096 accesses. `V9XIOTR.EXE`
(`tools\diag\io_trace_win32.c`, `scripts\build-io-trace.ps1`) reads the log
to `C:\V9XDIAG\V9XIOTR.TXT`, naming the register behind each Intel data
access and the INT 10h guard's count. The netbook's I/O BAR is `DC80+0008`,
the HD 4600's `F000+0040`.

Live registers: the netbook through `V9XTRACE -dd` (current tree; the older
`V9XTRCN.EXE` there now stops at `abi-mismatch`), the HD 4600 through
`V9XHSWMM.EXE` (VGA_CONTROL, PRI_CTL_A, PIPE_SRCSZ_A). The HD 4600's video
output is also captured by OBS on another PC.

## Measured: the writer

One windowed `command` from Run, writes passing through:

- **GMA 950** (boot 222, [log](2026-10-23-netbook-dos-vm-iobar-trace-passthrough.txt)):
  534 I/O BAR accesses by the new VM, 44 data writes. They are the 2026-09-24
  corrupt state register for register: `DSPBCNTR` 15000000 (plane B off),
  `VGACNTRL` 22C4008E (VGA plane on), `PIPEBSRC` 027F018F and `DSPBSIZE`
  018F02CF, `PFIT_CONTROL` 80002668, `LVDS` C0300300.
- **HD 4600** (boot 68, [log](2026-10-23-haswell-dos-vm-iobar-trace-passthrough.txt)):
  2705 accesses. With the original mini-VDD (boot 65) the box turned
  `VGA_CONTROL` 8000298E into 0000298E (VGA plane on), `PRI_CTL_A`
  94000000 into 0, and `PIPE_SRCSZ_A` 031F0257 into 02CF018F (720x400).

**The writer is the DOS VM's video BIOS, through the I/O BAR, at VM
creation.** That answers the 2026-09-24 record's "who wrote these
registers", on two generations.

## Tried first and rejected: virtualizing the I/O BAR

1. **Dropping the VM's `MMIO_DATA` writes** while Windows owns the CRTC (the
   DDK's S3 pattern). GMA 950, boots 223-224: live registers correct, but the
   BIOS reads back registers that no longer match what it wrote, and the text
   mode it then programs into the VM's virtual VGA lays the DOS window out
   wrongly - every row wraps mid-line
   ([screenshot](2026-10-23-netbook-dos-vm-window-writes-dropped.png),
   [log](2026-10-23-netbook-dos-vm-iobar-trace-writes-dropped.txt)). The
   original mini-VDD's window is correct, so the drop caused it.
2. **A per-VM shadow** (the DDK's ATI pattern): the VM's index and writes go
   to its control block, its reads of a register it wrote return that value.
   GMA 950, boots 226-227: registers correct and the window correct
   ([screenshot](2026-10-23-netbook-dos-vm-window-shadowed.png),
   [log](2026-10-23-netbook-dos-vm-iobar-trace-shadowed.txt),
   [snapshot](2026-10-23-netbook-dos-vm-live-display-shadowed-V9XSNA7.ini)).
   **HD 4600, boot 66: the machine hung** at the DOS box - output still the
   desktop, the DOS window blank, the clock stopped, the agent gone
   ([capture](2026-10-23-haswell-dos-vm-shadow-hang-obs.png)); Michael
   power-cycled it. The pass-through trace says why: this BIOS waits on
   hardware. It clears `PIPE_CONF_A` and `TRANS_CONF_A` bit 31 and polls bit
   30 (state) until it drops (29 and 27 reads); it requests `PWR_WELL_CTL1`
   and checks bit 30; it drives the PCH sideband (`C6000`-`C6008`), setting
   a busy bit and polling for the hardware to clear it; it clears a display
   interrupt bit (`44008`) and polls for the next. A shadow returns what was
   written and none of those loops end. Emulating them would be chip
   knowledge per generation, and an Ironlake BIOS would add FDI training.

## What ships: the INT 10h mode-set guard

The mini-VDD hooks INT 10h with `Hook_V86_Int_Chain`, which also sees
interrupts a VxD simulates ("whenever ... a virtual device calls the
Simulate_Int service", DDK `Hook_V86_Int_Chain`) - the VDD's VM-creation
mode set included. An AH=00h or AX=4F02h call from a VM other than the System
VM, while `VDD_Get_VM_Info` says the System VM owns the CRTC, is returned as
serviced without reaching the BIOS; 4F02h answers AX=034Fh. A full-screen VM
owns the CRTC and the System VM is always passed, so this driver's own mode
sets and the mini-VDD's VBE collection run as before. The idea is
vmdisp9x's (JHRobotics), which blocks the same calls from windowed VMs. It
needs no chip knowledge, so every family's mini-VDD carries it.
`-NoInt10Guard` removes it for an A/B; boot line `V9X-MINI int10-guard on`,
written only when the hook took, and asserted in the image by the build.

| machine, boot | build | windowed DOS boxes | registers | window |
|---|---|---|---|---|
| HD 4600, 69 | trace + guard | one | correct; `Int10Guarded=1`; the VM's only I/O BAR writes left are 175 `PWR_WELL_CTL1` requests for a well already on ([log](2026-10-23-haswell-dos-vm-iobar-trace-int10-guard.txt)) | correct; captured output correct ([capture](2026-10-23-haswell-dos-vm-int10-guard-obs.png)) |
| HD 4600, 70 | shipping | two together, both closed | correct throughout | correct, `mem` runs |
| GMA 950, 228 | shipping | two together, both closed | correct throughout | correct, `dir` runs |

"Correct" registers: GMA 950 `DSPBCNTR` 95000000, `VGACNTRL` A1C4008E,
`PIPEBSRC` 03FF023F, `PFIT_CONTROL` 00002668; HD 4600 `VGA_CONTROL`
8000298E, `PRI_CTL_A` 94000000, `PIPE_SRCSZ_A` 031F0257 - the desktop's own
values, read with the boxes open.

### Full screen: the skipped mode set is deferred, not dropped

Michael tried Alt+Enter on the netbook under the guard: the panel went
blue with a single cursor dash instead of a DOS prompt. Measured with the
trace build:

- **The VDD runs no BIOS mode set at the switch to full screen.** HD 4600,
  boot 72: no INT 10h and no I/O BAR access across Alt+Enter. On the netbook
  (boot 229) also none.
- So on the netbook nothing turned the VGA plane on: before the guard, the
  VM-creation mode set had done it, as the side effect that broke windowed
  boxes. The HD 4600 reached text mode at the switch anyway (`VGA_CONTROL`
  0000298E, 720x400, read in full screen before any output) - through the
  System VM, which the guard passes; which call does it was not traced.

The guard now keeps a skipped AH=00h per VM (a word in its control block,
`_Allocate_Device_CB_Area`, AL with bit 7 set so the BIOS keeps the text).
The VM's first INT 10h once it owns the CRTC runs that mode set first,
nested, then its own call; DOS writes its console through INT 10h. Netbook,
boot 229: after Alt+Enter and `ver`, `Int10Deferred=1`, 403 I/O BAR
accesses (the BIOS's mode set), and the panel showed the DOS prompt with
`ver`'s output (Michael, by eye; its blue tint is the panel). Between
Alt+Enter and the box's first output the panel still shows the blue
picture. HD 4600, boot 73: `Int10Deferred=1` at `ver`, display state
unchanged (already text).

Shipping build with the deferral, netbook boot 232 and HD 4600 boot 74: the
windowed test above repeated - two boxes together, both closed - with every
register at the desktop's values and the windows' text correct.

**Alt+Enter back to the window is unchanged and still broken:** with the
original mini-VDD on the HD 4600 (boot 71) the desktop comes back frozen
with a striped band along the top, GDI reads black, and the display driver
is never re-entered - the open part of this issue, independent of the
guard.

The shadow code is not in the tree. The adapter discovery and I/O BAR
trapping stay, in `-IoTrace` builds only, so shipping builds do not walk or
size PCI configuration space.

## Not established

- **The GMA 950's panel by eye.** Its readings are registers and GDI
  screenshots; the HD 4600's video output was captured.
- **Other chips.** The guard is chip-agnostic, but the defect was measured
  only on these two Intel generations. Ironlake fits the mechanism. A card
  that broke for another reason gets nothing from it.
- **What a skipped mode set costs inside the VM.** The VM keeps the text mode
  the VDD gave it. A DOS program that sets a graphics mode, or clears the
  screen with a mode set, in a window will not get it; not tried.
- **Full-screen DOS.** Entry shows text after the box's first output; the
  return to a window freezes as it did before this change.
- **Win95.** Measured on Win98 SE only.

## Gates

`check-tree` and `run-checks` pass with this change.
