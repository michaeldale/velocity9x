# GT218 on Linux: a working channel kick to compare against

Date: 2026-11-12

Status: planned, nothing run. Michael approved the plan's direction
(2026-11-12). It follows step 3 of
[nvidia-gt218-pgraph-2d.md](nvidia-gt218-pgraph-2d.md), paused after 21
runs on M-BXMFA8. The record is
[2026-11-11-gt218-channel-probe-run1-hang.md](../decisions/2026-11-11-gt218-channel-probe-run1-hang.md).

## Why

Measured on M-BXMFA8: within a millisecond of the IB PUT write, with an
empty push, the card sets PCI *received master abort*. Within a second
BAR1 stops answering, and then the card or the machine is lost. Every
difference from nouveau's init visible in the saved sources has been
tried and ruled out (runs 15-20). The PCIe logs reachable from Win98 hold
no address (run 21). Reading nouveau has nothing left to give. What is
missing is the register sequence of a channel that **works on this chip**.

Michael has a GT210 (also GT218) locally. The BringupKit target
(`C:\everything\bringupkit`, Debian 13 on a GA-78LMT-USB3 with an
FX-6300 and an onboard Radeon console) can run Linux nouveau on it. A
local freeze costs Michael a power button press, not a remote owner's
time. The kit's target has no independent reset hardware yet
(bringupkit `docs/CARD-SWAP.md`), so this plan needs Michael at the
machine for any step that can hang it.

The fix applies beyond this card. Every Tesla-generation chip (G80,
G84-G98, GT200, GT215/216/218, MCP7x) feeds its engines through the same
PFIFO channels, and nouveau gives A8 the G84/G98 channel code the probe
ports.

## Steps

Each step ends in a record in docs/decisions/ with its log, the way the
M-BXMFA8 runs did. Logs go under `build/driver-results/`.

### Step 0: the card in the target, measured as found

Michael swaps the GT210 into the GA-78LMT's x16 slot. Then, with no
driver bound to it:

- the kit's one-shot collector (`sudo bash deploy/collect-card.sh`):
  identity (expected 10DE:0A65 or another GT218 id), BARs, ROM,
  configuration space;
- `lspci -vvv` for the card and its root port: whether the root port has
  an AER capability, and whether the kernel's `pcieport` AER service
  claimed it (`dmesg | grep -i aer`);
- whether the firmware POSTed the card. It is not the boot VGA device, and
  M-BXMFA8's card is. Read through the kit's `bar-read` the registers
  M-BXMFA8's probe recorded: PMC_BOOT_0 (0h), PMC_ENABLE (200h), 1540h,
  1700h-170Ch, 100C08h. An unPOSTed card makes nouveau run the VBIOS
  init scripts itself, which is fine for step 1. A later replay of the
  probe would then start from a different state than M-BXMFA8's, and
  must be recorded as such;
- `grep MMIOTRACE /boot/config-$(uname -r)`. Whether Debian 13's stock
  kernel has `CONFIG_MMIOTRACE` is not known. If it does not, a kernel
  built with it is a decision for Michael (step 1 needs it).
- The console stays on the Radeon: `cat /proc/fb` must show fb0 as the
  Radeon's before nouveau loads (bringupkit CARD-SWAP, the s3fb hang).

### Step 1: nouveau's own bring-up, traced

With `CONFIG_MMIOTRACE`: mount debugfs, set the tracer to `mmiotrace`,
stream `trace_pipe` to a file on the target, `modprobe nouveau`, wait
for its DRM init lines in `dmesg`, then set the tracer back to `nop`. At
load nouveau creates a kernel channel of its own for buffer moves unless
`noaccel` is given, so a working kick should be in the trace without any
user program. mmiotrace takes the other CPUs offline while it runs; this
is its documented behaviour.

Pass condition: nouveau reports no PFIFO or PGRAPH error, and dmesg shows
its channel created. If nouveau itself hangs or faults on this card, that
is the finding. The problem would then be the chip or board, and the
M-BXMFA8 work would be judged against it.

Caveat, unmeasured: mmiotrace sees accesses through `ioremap` mappings.
BAR0 is one. Whether nouveau's BAR1 and BAR3 accesses (instance memory
through the BAR3 VM, `io_mapping`) are traced is not known, and step 2
must check. If they are not, the instance block, RAMFC and page tables
are only in the trace where nouveau writes them through PRAMIN.

### Step 2: the trace against the probe

A host script in `tools/` (Python) reduces the trace to the ordered
register writes and reads in the ranges that matter: PMC (0h-1FFh),
PBUS (1700h-170Ch, 1900h-191Ch, 1540h), PFIFO (2000h-3FFFh), the BAR
flush (70000h), PRAMIN moves (1700h) and the window (700000h-7FFFFFh),
the VM (100C00h-100CFFh), PGRAPH (400000h-40FFFFh) and USERD (C00000h
onwards). It names each one from nouveau's sources where the saved
sources name it. A second listing gives the probe's op list for
`/push:empty /flush:none /stop:fifo` in the same form. It can be
generated on the host from the same nv50_chan.c and nv50_grctx.c output.

The comparison should show at least:

- the channel table entry, runlist and RAMFC words nouveau uses, and
  where it places the instance block;
- everything nouveau writes between its channel bind and its first IB
  PUT, and whether it reads anything (USERD, CACHE1) the probe found to
  time out on M-BXMFA8;
- any register in the trace the probe never touches.

Each difference becomes a candidate for a run. On the local card first,
then on M-BXMFA8 only once it survives locally.

### Step 3 (only if step 2 is not enough): the probe replayed on Linux

The probe's sequence run on the local card under Linux, where the root
port's AER, if step 0 found it, can log the rejected request's header
and so its address. Two ways, a decision for Michael at the time:

- a standalone program under `tools/` that maps BAR0 and BAR1 through
  `/sys/bus/pci/devices/.../resource*`, reusing `nv50_chan.c` and
  `nv50_grctx.c`. Small, but outside BringupKit's recipe, journal and
  allowlist discipline;
- a BringupKit verb with a reviewed profile. It fits the kit's contract,
  costs more, and lands in that repository.

## Decisions owed

- A kernel with `CONFIG_MMIOTRACE`, if Debian's does not have it.
- envytools (`nvapeek`/`nvapoke` read registers and VRAM while nouveau
  is bound, which the kit's `bar-read` refuses by design). It is a
  third-party build on the target; only if step 2 needs live reads.
- Step 3's form, if step 3 is needed.
