# Four tier-0 backend defects

Status: **all four fixed** (D1/D2 2026-08-16, D3 and D4 2026-08-17). All four
live in chip-agnostic code belonging to the tier-0 backend rather than in
anything ATI-specific; the first three were found while designing the ATI Mach64
family, and D4 came out of verifying D3 on its guest.

D1 and D2 were blocking for stage 4, the first install on the physical laptop.

**What the set adds up to:** tier-0 now drives a card its family does not name.
Verified on the 86Box Mach64 VT2 with the `vbe` package, whose INF claims only
QEMU std-vga: D3 removed the second, invisible allowlist that made the
documented Have-Disk route inert, and D4 gave the tier a working way to ask the
BIOS where the framebuffer is. Before D3 the driver refused at stage 1 on
identity; before D4 it refused at stage 3 with no aperture.

**Full mode matrix, 2026-08-17**, `vbe` package on `Win98SE-Mach64VT2`:

| Mode | Stage | GDI | Palette |
|---|---|---|---|
| 640x480x8 | enable-ok | PASS | PASS |
| 800x600x8 | enable-ok | PASS | PASS |
| 1024x768x8 | enable-ok | PASS | PASS |
| 640x480x16 | enable-ok | PASS | n/a |
| 800x600x16 | enable-ok | PASS | n/a |
| 1024x768x16 | enable-ok | PASS | n/a |

Results in `build\driver-results\mode-matrix-vbe-std-vga-20260817-164318`. That
is the phase 9 exit gate, met on hardware the family does not claim - which is
the strongest form the claim could take.

D4 is the one to read for method rather than mechanism. Three hypotheses about
it were wrong, each argued from documentation, and the one that held was settled
by instrumenting the guest instead.

**Measured effect of the fixes:**

| Family | D1 (`CR69`) | D2 (VRAM floor) |
|---|---|---|
| `s3` | declares `FLIP` - **unchanged**, still writes CR69 | non-NULL `read_aperture`, never reaches the tier-0 default - **no effect** |
| `matrox-m2` | no caps - **stops** writing S3 CR69 on a Matrox | non-NULL `read_aperture` - **no effect** |
| `vbe` | no caps - **stops** writing S3 CR69 on std-vga | applies |
| `ati` | no caps - never writes CR69 | applies |

S3's `publish_diagnostics` lives in `s3_regs16.c` and never references the VRAM
variables, so its `V9XHW.INI` output is unchanged.

Code cost: **+54 bytes** on the S3, VBE and ATI images and +56 on Matrox, all
from the shared `enable16.c` change. Well inside the 2 KiB per-step budget.

**One caveat worth stating.** The Matrox behaviour change is a fix - writing an
S3 extension register on an MGA-2164W was never right - but `matrox-m2` is
real-hardware-only and this has not been run on that card. `V9xHalFlip` will now
decline a primary flip there rather than report success it could not deliver.

---

## D1 - `set_display_start` writes an S3 extension register on any chip

`src\display32\engines\vga_scanout.c:34-45` programs the display start through
VGA `CR0C`/`CR0D` **and S3 extension register `CR69`**. It is called from three
places:

- `V9xHalFlip` - `src\display32\ddhal_core.c:313`
- `V9xHalFlipToGDISurface` - `ddhal_core.c:407`
- `V9xHalSetExclusiveMode` - `ddhal_core.c:435`

None of them go through the engine vtable, and none are gated on `engine_type` or
on `V9X_DD_ENGINE_CAP_FLIP`. Grepping confirms `V9X_DD_ENGINE_CAP_FLIP` and
`_VBLANK` appear **only** in the two S3 chip modules and the manifests - nothing
in the driver reads them, so they currently mean nothing.

**Impact today:** the shipping `vbe` tier-0 package already writes S3 `CR69` on
QEMU std-vga hardware.

**Impact on the ATI work:** it would write an undefined register on an ATI part,
on a laptop whose only display is an internal LCD with no recovery path.

**Fix**, already anticipated by `docs\plans\multi-chip-restructure.md:52` -
*"`set_display_start`, `in_vblank` and `build_caps` join the table when the files
move"*: move `set_display_start` and `in_vblank` into `V9X_ENGINE32_OPS` and have
the three call sites do nothing when `v9x_engine32()` returns null. A cheaper
interim fix is to gate the three call sites on
`engine.engine_caps & V9X_DD_ENGINE_CAP_FLIP`, which would finally give that
capability bit a meaning.

**Until this is fixed, the `ati` family must not claim `FLIP` or `VBLANK`.**

---

## D2 - VRAM arithmetic underflows when the BIOS reports less memory than the mode needs

`src\display16\dd16.c` takes `vram_bytes` verbatim from the tier-0 default
(`v9x_vbe_default_aperture()` in `enable16.c:85`, clamped only *downward* against
the mapping). When a BIOS under-reports, three things break:

| Site | Breakage |
|---|---|
| `dd16.c:257-259` | `fpStart = base + visible_bytes`, `fpEnd = base + vram_bytes - 1`. At 1024x768x16 with a 512 KiB report, `fpStart > fpEnd` - a heap whose end precedes its start. |
| `dd16.c:266-267` | `dwVidMemTotal = vram_bytes - visible_bytes` on `DWORD`s **underflows to ~4.29 GB**. DirectDraw is told there are 4 GB of off-screen video memory past the end of the framebuffer. |
| `ddhal_core.c:605-608` | Every blit rectangle is bounds-checked against `fb.vram_bytes`. At 640x480x16 (614,400 bytes visible) that is already larger than 512 KiB, so even CPU blits to the bottom of the primary would be refused. |

**This is not hypothetical on our hardware.** The Rage Mobility's VBE reports
`TotalMemory64K = 8` (512 KiB) on a panel running 1024x768x16.

**Fix:** keep the raw BIOS answer for the `V9XHW.INI` `VbeVramBytes` diagnostic -
"this BIOS claims 512 KiB" is exactly what a bug report from an untested card
needs - but floor the *usable* value at `visible_bytes`, rounded up to the next
64 KiB. Roughly 20 bytes of code in `enable16.c`.

Prefer `visible_bytes` over falling through to `dd16.c`'s existing 4 MiB
fallback: on an unknown card, believing in off-screen memory you have not proven
exists hands DirectDraw surfaces that alias the visible framebuffer. Tier-0 does
CPU blits and barely uses the off-screen heap, so "no off-screen memory" costs
almost nothing, whereas a wrong ceiling costs corruption.

**Regression gate:** the S3 and Matrox families never set the variable, so their
`V9XHW.INI` and `DDGETTRACE` output must be **byte-identical** after the change.

**Follow-on to check:** when `vram_bytes == visible_bytes` the heap is empty
(`fpEnd == fpStart - 1`, `dwVidMemTotal == 0`). Confirm DirectDraw copes; if not,
set `dwNumHeaps = 0` in that case rather than inventing memory.

---

## D3 - "other VBE 2.0 cards via Have-Disk" does not actually work

**FIXED 2026-08-16.** Option 1 and option 2 were both rejected in favour of a
narrower change: the `vbe` family sets a new `pci_match_optional` flag in its
`v9x_hw16` table, and `enable16.c` stage 1 treats a PCI miss as fatal only for
families that do not set it. The scan still runs - it is what decides whose
hooks execute - but tier-0 no longer vetoes a card the INF never claimed to
match, which is the whole point of a Have-Disk override.

Every other family leaves the flag zero, including `ati`, which is itself
tier-0: the generic package is the single answer to "my card is not listed", and
a vendor package accepting anything would publish its vendor's identity for a
card that is not one. The field sits last in the struct so that zero - the
strict behaviour - is also what an initializer that forgets it gets.

`v9x_vbe_publish_diagnostics` now reports `unmatched` for the adapter and ids
when `v9x_pci_match` is `0xFFFF`, rather than the fallback entry's std-vga
strings. Rationale and the rejected alternatives are recorded in
`docs\decisions\2026-08-16-vbe-tier0-family.md`.

Code cost: **+34 bytes** on `s3`, `matrox-m2` and `ati` - the shared
`enable16.c` condition and the struct field - and **+150** on `vbe`, which also
carries the diagnostics check and its two strings. Inside the 2 KiB per-step
budget.

**Re-measured on the Mach64 VT2 guest, and it took three goes.** Worth
recording, because each failure was a different allowlist or mechanism and only
the hardware showed them:

1. `enable16.c` stage 1 alone was not enough. `ddi.c` has **two more** calls to
   `V9xHardwarePresent` - its own check at the Enable entry point, which is the
   one that produced the original `fail-hardware-present`, and `ValidateMode`,
   which rejected every mode and made `V9X16LD.EXE` exit 4. All three now share
   `v9x_hardware_acceptable()`, because a driver that enables for a card must
   also validate modes for it.
2. With that fixed the guest reached **stage 3** rather than stage 1 - the PCI
   veto was gone - but refused with `fail-hardware-aperture`.
3. The cause was not the card. The DOS VBE inventory tool
   (`scripts\build-vbe-inventory.ps1`) run in the guest reports a perfectly good
   BIOS: VESA 2.0, 4 MiB, and for mode 0101h attributes `00BB`, 640x480x8,
   memory model 4, **BytesPerScanLine 640** - exactly the family table's pitch -
   and PhysBasePtr `E6000000`. Every check in `vbe_parse.c` passes on that data.

The failure was the **mechanism**: `VbeDetail=4f01-no-dos-buffer`, meaning
DPMI 0100h returned failure. Windows' DPMI host does not serve that function in
this context, which is what Microsoft's own guidance says to expect - Windows
manages DOS memory and applications are told to use `GlobalDosAlloc`. The
decision record had already named this as the fallback if the DPMI path
misbehaved, so the allocation moved into `enable16.c`, one of the six files
`check-tree` allows `<windows.h>`. DPMI 0300h, the simulated interrupt itself,
the host does support and is unchanged.

That is also why `VbeDetail` now exists. The stage code is the right
granularity for the boot-trace contract but the wrong granularity to act on:
`fail-hardware-aperture` cannot distinguish a BIOS reporting an unusable stride
from a BIOS call that never ran, and on a tier whose whole purpose is untested
cards that distinction is the first thing a bug report needs.

The original finding follows.

**Measured 2026-08-16** on the `Win98SE-Mach64VT2` guest (86Box Mach64 VT2,
`1002:5654`), by doing exactly what the manifest advertises.

`packaging\families\vbe\family.psd1` says:

> `HardwareIdHint = 'PCI 1234:1111; other VBE 2.0 cards via Have-Disk'`

and the backend registry comment says unlisted cards "reach tier-0 by
Have-Disk". The Have-Disk install itself works: Windows offers
"Velocity9x VBE-generic display (QEMU std-vga)", warns that the driver was not
written for this hardware, accepts it, copies the files, and binds the class to
`v9xdisp.drv` / `v9xmini.vxd`.

**Then the driver refuses to enable.** `C:\V9XBOOT.INI` reads:

```
Stage=fail-hardware-present
```

which is `ddi.c:613` - `V9xHardwarePresent()` returning 0, serial
`stage=device-id`. The cause is that a family carries **two independent
allowlists**, and Have-Disk only satisfies one of them:

| Allowlist | Where | Satisfied by Have-Disk? |
|---|---|---|
| INF hardware ids | generated `[Velocity9x.Models]` | **No** - Have-Disk bypasses the match entirely, which is the point of the mismatch warning |
| Runtime PCI device list | `v9x_hw16.devices[]`, stamped into DGROUP by `ddi.c` and scanned by `V9xFindPciDevice` | **No** - it is compiled into `v9xdisp.drv` and never consulted the INF |

So Have-Disk gets you a bound-but-inert driver on any card the family does not
name. The card falls back to VGA and the only evidence is a stage code.

This is not academic: it is the documented route for owners of untested cards,
and it cannot work for any of them.

**Options**, none of which should be chosen without a decision record:

1. Say so honestly - change `HardwareIdHint` to state that tier-0 requires the
   card's PCI id to be added to the family, and that Have-Disk alone is not
   enough. Smallest change, and it stops the docs promising something false.
2. Give the `vbe` family a wildcard runtime device entry (say vendor `0xFFFF`)
   that `V9xFindPciDevice` treats as "match anything". This makes the advertised
   behaviour real, but it deliberately reintroduces the catch-all that
   `backend_registry.c:21-26` argues against - though note the argument there is
   about the *backend registry*, which is a different allowlist from this one.
3. Read the accepted hardware id out of the registry at Enable time rather than
   compiling it in.

**A second, smaller finding from the same run.** Hot-swapping the `ati`
binaries onto that vbe-bound class with `update-associated-driver.ps1` produced
`Stage=query-ok` - the driver was loaded and its GDIINFO queried, but Enable
never ran, so the guest stayed on VGA. That is correct behaviour rather than a
bug: the script's own scope is `already-associated-driver-only`, and the
registry association still carried the vbe INF's `MatchingDeviceId` of
`1234:1111`. Swapping the binary does not re-associate the device. The `ati`
family has to be installed through its **own** INF.

---

## S3 regression for D1-D3

All of D1, D2 and D3 changed shared code that links into every family image, so
the risk worth checking was the hooked path they were not aimed at. Run
2026-08-17 against the current build on both S3 guests, six modes each:

| Guest | Modes | Result |
|---|---|---|
| `Win86SE` (ViRGE/DX, 9869) | 640x480, 800x600, 1024x768 at 8 and 16 bpp | all `enable-ok`, GDI PASS, palette PASS at 8 bpp |
| `Win98SE-Trio64` (9871) | same six | all `enable-ok`, GDI PASS, palette PASS at 8 bpp |

Both chips from the one binary, which is the claim the S3 family merge exists to
make. `v9x_hardware_acceptable` leaves the S3 path semantically unchanged -
`pci_match_optional` is zero, so a PCI miss is still fatal - and the tier-0
aperture default it now shares is never reached, because the family supplies
`read_aperture`.

Results: `build\driver-results\mode-matrix-s3-virge-dx-20260817-120432` and
`...-s3-trio64-20260817-152007`.

**Re-run after D4, and this one was the load-bearing check.** The mini-VDD is
shared by every family, so its new init-time BIOS collection runs on the S3
guests too - which had no reason to want it and everything to lose from it. Both
matrices were run again against the D4 build: `Win86SE` all six modes
`enable-ok` with GDI and palette passes (boot counters 254-259,
`...-s3-virge-dx-20260817-164910`), and `Win98SE-Trio64` the same (120-125,
`...-s3-trio64-20260817-165403`).

Eighteen modes across three chips on the same build, then: two S3 parts that
supply `read_aperture` and one card that supplies nothing and is not even in its
family's device list.

---

## D4 - tier-0 has no working way to hand the BIOS a buffer

**FIXED 2026-08-17.** The mini-VDD makes the calls at ring 0 now, and tier-0
drives the Mach64 VT2 end to end - `Stage=enable-ok`, `VbeDetail=ok`, desktop up,
`VbeVramBytes=4194304` matching what the DOS inventory measured on that BIOS.

Ring 0 was the answer because it replaces **both** broken halves rather than one:
the VMM allocates the V86 scratch and nested execution runs the interrupt, so no
DPMI host is involved at any point. `V9XMINI` gained a private device id and a
protected-mode API, reached through `INT 2Fh AX=1684h` exactly as `runtime.asm`
already reaches the master VDD, guarded by a magic handshake because the id is
unallocated and a collision would otherwise hand the driver a stranger's entry
point. The queries run once at `Device_Init` - 4F00h and 4F01h describe the
adapter, not the current mode, so they are cacheable - which keeps the BIOS call
in init context and leaves the API a table lookup that cannot fault a display
driver mid-initialisation.

**The bug that took three hypotheses.** Every `Client_*` reference is an offset
off `EBP`, and at `Device_Init` nothing sets `EBP` up: event handlers receive it,
init code does not. The first version therefore wrote the BIOS request wherever
`EBP` happened to point, the interrupt ran with whatever registers the VM already
had, and every call returned nothing usable. `Get_Cur_VM_Handle` plus
`CB_Client_Pointer` fixes it, with `Begin_Nest_V86_Exec` rather than
`Begin_Nest_Exec` since this is a V86 BIOS interrupt.

What actually found it was instrumentation, not reasoning. An empty cache has two
causes whose fixes live in different files, so the API grew a status function and
the trace grew a `VbeCache` key; the guest printing `s=1829 m=0 c=0` - buffer
allocated fine, not one usable answer - eliminated the allocation in a single
boot. Three hypotheses about this defect were wrong before that, and each wrong
one had been argued from documentation rather than measured. The lesson is cheap
to state and was expensive to learn: when a mechanism cannot be observed, add the
observation before adding the next fix.

The original finding and the two ring-3 dead ends follow.

**Originally measured 2026-08-17 on the `Win98SE-Mach64VT2` guest.**

4F00h and 4F01h take a buffer in ES:DI. The driver runs in 16-bit protected
mode, so that buffer has to be real-mode addressable and the call has to be
made through the DPMI host. Getting the buffer is the unsolved part, and both
mechanisms tried are measured failures on the same guest:

| Mechanism | Result |
|---|---|
| DPMI 0100h (allocate DOS memory block) | Returns failure. `VbeDetail=4f01-no-dos-buffer`, tier-0 refuses at stage 3, Windows falls back to VGA. Consistent with Microsoft telling applications to use GlobalDosAlloc because Windows manages DOS memory itself. |
| `GlobalDosAlloc` called from Enable | **Fatal exception 0D at 031F:000009DE.** Boot trace never advanced past `Stage=libmain`. Guest needed a reboot. |
| `GlobalDosAlloc` called from `DriverInit` instead | **Fatal exception 0D at 031F:000009DE - the same address.** Tried because the first failure looked like a timing problem. It is not: see the correction below, which establishes that this allocator returns normally and the fault is further down. |

**Correction, 2026-08-17.** This entry first concluded from the shared fault
address that `GlobalDosAlloc` itself was at fault - probably unresolvable for an
NE display driver. **That was wrong**, and the guest disproved it on the next
boot:

- The `DriverInit` build is the one installed, and it comes up writing
  `Stage=libmain`. In that build the allocation happens *before* the libmain
  trace is written. The trace exists, so `GlobalDosAlloc` returned normally.

So the allocator works. What actually correlates with the fault is the
allocation **succeeding**, because that is the only condition under which the
code after it runs at all. Two things are reachable only then:

1. Zeroing the block through the returned protected-mode selector.
2. The DPMI 0300h simulated interrupt.

Whenever DPMI 0100h is wired up the allocation fails, both are skipped, and
nothing crashes - which is why the "clean failure" build looks stable. It is not
that DPMI 0100h is safe; it is that it never gets far enough to run the unsafe
part. **DPMI 0300h has never successfully executed on hardware in any build.**

The lesson about the shared fault address: it was suggestive, not conclusive,
and it was read as conclusive. Two builds calling the same faulting helper from
different places will also fault at one address.

DPMI 0100h is what is currently wired up. It does not work either, but it fails
cleanly - refusal, VGA fallback, a boot trace saying why - and a driver that
refuses is worth a great deal more than one that faults.

**Consequence:** tier-0 is inert on any card that needs 4F01h for its aperture,
which is every card the tier exists for. D3 is genuinely fixed - the driver now
reaches stage 3 on a card its family does not name, where before it was vetoed
at stage 1 - but stage 3 is as far as it gets.

**The likely answer is the mini-VDD.** `V9XMINI.VXD` runs at ring 0, where
neither the DPMI host nor the Win16 global heap is in the way, and it already
handles VESA DPMS calls (`MiniVDD_VESASupport`). Doing 4F00h/4F01h there and
handing the parsed result to the 16-bit side would sidestep the whole problem.
That is a design change, not a patch, and it should not be attempted by
guessing at a third allocator on a live guest.

**Do not test the next attempt on a guest without a disk snapshot first.** The
two GlobalDosAlloc attempts cost two bluescreens on a VM that was in use, and
the second one was avoidable: it was a variation on a mechanism that had already
faulted once, deployed on the theory that the timing was the problem. When a
ring-3 mechanism has already taken a machine down, the next step is to change
approach, not to retry it from a different line.

## Why these are not ATI bugs

Both are reached by any family whose `read_aperture` hook is NULL - which is the
definition of tier-0 - and D1 is reached by every family regardless. The ATI port
is simply the first hardware to make them visible. Fixing them on `ati-mach64`
would leave the `vbe` family broken and create a merge conflict in shared files.
