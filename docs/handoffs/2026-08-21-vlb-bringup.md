# S3 Trio64 VLB bring-up handoff

Date: 2026-08-21
Branch: `vlb-survey-tool`, 10 commits ahead of `main`, pushed to `origin`.
Working tree clean at handoff.

Scope: get the Velocity9x display driver running on a physical 486 with an S3
Trio64 on VESA Local Bus. The survey and probe work is **done and the aperture
question is answered**; what remains is making the driver installable and
running it.

Related: [the VLB survey plan](../plans/vlb-survey-tool.md),
[schema 2 and the survey runs](../decisions/2026-08-20-vlb-survey-schema2.md),
[the aperture answer](../decisions/2026-08-21-vlb-aperture-answered.md).

---

## 1. The machine, and the one thing about it that trips people up

A physical 486 that is **two targets in one box**, and the tooling for each is
useless to the other.

| | DOS side | Windows side |
|---|---|---|
| What | DOS 6.22, clean boot | Windows 95 retail, build 4.00.950 |
| Reached by | Michael, at the keyboard | v9x-remote-agent, `10.0.1.217:9869` |
| Runs | `V9XSURV`, `V9XAPER` | the driver |

**The remote agent cannot run the DOS probes.** `V9XAPER` refuses in
virtual-8086 mode by design - unreal mode cannot be entered from it - and the
survey's Tier 2 wants a clean boot too. The agent runs under Windows. So the
agent is available exactly when the probes are not. Any request to "just re-run
the probe" is a request to Michael, not something to automate.

**A clean boot needs F5, not F8.** F8 with "Command prompt only" still ran
CONFIG.SYS on this machine: the report came back with EMM386 resident, the CPU
in V86 mode, `INT 15h AH=88h` answering 0 KB, and a VBE TSR standing in front of
the card's BIOS. That is most of what the clean run exists to avoid. F5 skips
CONFIG.SYS and AUTOEXEC.BAT outright.

### Hardware, measured

- **S3 Trio64.** CR2D/CR2E = `88`/`11`, CR30 = `E1`, revision `02`. Diamond
  Stealth 64 DRAM, BIOS v2.02 dated 01/19/95, 32 KB, checksum ok.
- **2 MiB of video RAM.** CR36 = `99`, and VBE agrees independently.
- **32 MiB of system RAM.** `INT 15h AH=88h` = 31744 KB, on a clean boot.
- **No PCI bus at all.** `INT 1Ah AX=B101h` fails.
- The card's ROM carries a valid `PCIR` header reporting `5333:8811`, because
  Diamond shipped one image for both bus variants. A read-only identification
  route that needs no bus.

### The Windows 95 guest, and three non-guessable facts

- **There is no DirectX.** `DDRAW.DLL` and `DSOUND.DLL` are both absent. The
  entire `display32` side - the DirectDraw HAL, `ddhal_core.c`, the engine and
  blitter work - is unreachable on this machine; nothing will load it.
  `DIBENG.DLL` (201136) and `VMM32.VXD` are present, so the 16-bit driver and
  the minivdd are fine. **The aperture mapping happens on the 16-bit side at
  Enable, so the VLB question is testable without DirectDraw.**
- **The display device is root-enumerated as `*PNP0913`**, not on any bus:
  `HKLM\Enum\Root\*PNP0913\0000`, `Class=Display`,
  `DetFunc=*:DETECTS3801`. Win95 found it with its S3-801-family detection
  routine.
- **Win95 names its display-class INF `MSDISP.INF`** where 98 uses
  `DISPLAY.INF`. A copy pulled off the machine is worth keeping to hand.

Currently running the Win95 built-in S3 driver (`S3.DRV`, 57632 bytes).
`C:\V9XHW.INI` does not exist - Velocity9x has never run here.

Michael chose to **target Win95 as-is** rather than reinstall 98 SE, having been
told the risk (debugging a new bus and an untested OS at once, with no Win95
evidence anywhere in the tree).

### S3VBE 3.18

A third-party VBE 2.0 TSR by Dietmar Meschede, loaded from AUTOEXEC.BAT. It is
why the machine reports VBE 2.00 with a linear framebuffer when **the card's own
ROM reports VBE 1.02 with none**. Its `/CHECK` and `/?` will not report the
framebuffer address; `/UNLOAD` removes it without a reboot. Extracted at
`C:\Users\michael\Downloads\s3vbe318`.

---

## 2. What is proven

**The linear aperture works.** Both candidate addresses, by the strongest test
the probe has - a 32-byte marker written into video memory through the banked
`A0000h` window and read back at the linear base, which establishes the two are
the same memory rather than merely that something answered.

| Run | Base | Linear addressing | Result |
|---|---|---|---|
| AP1 | `7F000000` | off, as the BIOS leaves it | all `FF` |
| AP6 | `7F000000` | on, CR58[4] set by us | **marker found** |
| AP7 | `04000000` | on, after relocating there | **marker found** |
| AP5 | `04000000` | S3VBE placed it, linear mode | round trip ok |

AP1 and AP6 are one bit apart and give opposite answers.

**The driver needs no aperture change.** `Cr58ReadBackHonoured=yes`, so the
read-back guard in `v9x_s3_enable_linear_aperture` passes; `0x7F000000` is inside
`v9x_s3_read_aperture`'s accepted range; and the card's ROM closing the extended
lock behind every mode set cannot bite the driver, which unlocks before each
extended access.

**Tier-0 is closed off on this card by measurement.** The card's ROM reports VBE
1.02 with bit 7 clear on all 18 modes and `PhysBasePtr` zero throughout.

**Two register facts worth not re-deriving.** On S3 the CR38/CR39 locks gate
**writes, not reads** - this BIOS holds `59`/`BD` there rather than the unlock
keys and the identity registers read true anyway. And CR38/CR39 do not read back
what is written to them; the save-and-restore of that pair is a gesture, though
the end state is provably unchanged across four runs.

---

## 3. What is not proven

- **The driver has never run on this machine.** Nothing below this line has been
  observed; it is all inference from the survey and probe.
- **The 32-bit DirectDraw path is untestable here** until DirectX is installed.
  Not needed for the VLB question. Ask before installing it.
- **The manifest declares 4 MiB for the Trio64 chip entry and this card has
  2 MiB.** `VideoMemoryBytes = 4194304` in `packaging/families/s3/family.psd1`,
  and the host family-matrix test validates every declared mode against it.
  `read_video_memory` reads CR36 at runtime so the heap should size correctly,
  but the **INF advertises modes validated against 4 MiB** - 1280x1024x8 needs
  1.25 MiB and fits, but check the 16- and 32-bpp rows before installing rather
  than after.
- **`identify_without_pci` has never executed.** It compiles, links and passes
  the host tests; it has not run on silicon.

---

## 4. Where the code is

Ten commits on `vlb-survey-tool`, oldest first:

```
be8ff43  Survey at schema 2: identify a card on a bus with no PCI
884d7a8  Record what the 486 VLB Trio64 returned, and fix what it exposed
a441a96  The VBE 2.00 was S3VBE 3.18, and it puts the window somewhere else
4057185  Clean boot: the card's own BIOS is VBE 1.02 with no linear framebuffer
b877ceb  Tell testers F5, not F8, for the clean-boot run
ed41a70  A VLB aperture probe, because the survey structurally cannot answer this
7d118b9  The VLB aperture answers at 64 MiB; three runs measured nothing
79d17b0  Record the aperture result in the changelog too
934f7c3  The BIOS's own aperture base works too, so the driver needs no change
9294274  The S3 family can identify its own silicon when there is no PCI to scan
```

### The tools

| Tool | Build | For whom |
|---|---|---|
| `tools/diag/vga_survey_dos.c` | `scripts/build-vga-survey.ps1` | **strangers** - read-mostly, gated |
| `tools/diag/vlb_aperture_dos.c` | `scripts/build-vlb-aperture.ps1` | **us only** - sets modes, writes the card |

Keep that boundary. `build-vga-survey.ps1` refuses to compile a mode set, a PCI
write, a port constant outside an audited list, or an unlisted raw opcode byte,
and `-GateSelfTest` asserts it rejects nine deliberate violations (a `run-checks`
step). The aperture probe could not pass that gate and should not; its build
script asserts the opposite kind of property - that the V86 check, the unreal-mode
self-test, the top-of-RAM guard and every restore path are present by name, and
that the binary has not been dropped into the tester distribution folder.

### The last commit, which is half of the remaining work

`9294274` added a family hook `identify_without_pci` to `V9X_HW16_OPS`. It is
**not** `pci_match_optional`, and the distinction is the point: that flag means
"proceed without knowing which card this is", which only a tier-0 family can
afford. The hook means "find out another way". `v9x_s3_identify_without_pci`
reads CR2D/CR2E - which on S3 hold the same device id the PCI parts publish -
matches against `v9x_pci_device`, and sets `v9x_pci_match` exactly as
`V9xFindPciDevice` would, so every per-chip dispatch downstream is unchanged.
Reads only; accepts only ids the family already names (two of 65536). A host
test asserts the two strategies are mutually exclusive.

It is called **only when there is no PCI BIOS at all** (`V9xPciBiosPresent`,
`INT 1Ah AX=B101h`). `V9xFindPciDevice` cannot tell "no PCI on this machine"
from "PCI present, none of our cards in it" - `B102h` fails identically for both
- and those want opposite answers. The first is the VLB case. The second is this
package bound to somebody else's card, where reading that card's extended
registers is the one thing not to do.

---

## 5. The plan

### Step 1 - the INF binding (the actual blocker)

The driver is now capable on VLB but **not installable**: the generated INF
advertises only `PCI\VEN_5333&DEV_xxxx`, which SetupX cannot bind on a bus it
does not enumerate.

**Design, settled by evidence rather than inference.** Add a second model per
family with **no hardware ID at all**. That is Windows' own pattern - eight such
models in this machine's `MSDISP.INF`, e.g. `%Vortek%=AGX` and
`%SuperVGA.DriverDesc%=SVGA`. Manual-select only, installable over a device that
has an ID (display class permits the override, which is how any generic SVGA
driver gets on), and zero risk of claiming hardware we do not support.

**Do not bind `*PNP0913`**, even though it is what this card presents and would
make installation automatic. It covers every S3 ISA/VLB card `DETECTS3801`
finds - 86C801, 805, 928 - and we have code for none of them. Claiming it
reproduces exactly the failure already documented from the Mach64 (D3): a Have
Disk install onto an untested card that binds and then refuses at stage 1 with
only a stage code to say why. The INF stays the honest claim of what is
supported.

Three coordinated pieces:

1. A manifest field declaring the manual-select model and its description.
2. `scripts/lib/inf.ps1` emitting it.
3. `Assert-V9xInf` widened **deliberately**. It currently requires the INF's
   hardware-ID set to match `Get-V9xFamilyHardwareIds` exactly, found by a
   `PCI\\VEN_[0-9A-Fa-f]{4}&DEV_[0-9A-Fa-f]{4}` regex over the generated text,
   and `check-tree.ps1` asserts unique PCI ownership across families. The no-ID
   slot needs its own uniqueness story rather than a hole in the check.

### Step 2 - install and observe

The agent turns this from a round-trip per attempt into an iterable loop. The
verbs that matter: `exec`, `shell`, `put`, `get`, `stat`, `screenshot`, `input`,
`reboot -JobId`, `wait-desktop`. Add `-Json` to every call - **without it
`v9xctl.ps1` emits pre-formatted output and property access silently yields
nothing**, which wasted several turns.

A reboot is proven, never assumed: require the old connection to end, a new one
with the exact `-JobId` from `PENDING.DAT`, and `wait-desktop`. Call
`wait-desktop` before any GUI `exec` or `screenshot`. Never screenshot during a
mode transition or a suspected wedge - it invokes the display driver through GDI.

What to read after the first boot: the boot trace, `C:\V9XHW.INI` (absent today,
so its appearance is itself the first proof), and a screenshot.

The question this answers: does `identify_without_pci` fire, does the aperture
map, does a mode set land. All 16-bit, all reachable without DirectDraw.

### Step 3 - outstanding regardless

- **The schema-2 survey regression on the 86Box PCI targets** (ViRGE `:9869`,
  Trio64 `:9871`), confirming the report is a superset of the schema-1 one. Needs
  no 486, still not done, and it is the check that would catch schema 2 having
  broken something that already worked.
- **Whether `0x04000000` is a rule or a coincidence.** One board. S3VBE computes
  its answer rather than fixing it; worth understanding from what before copying
  the number. Not needed while `0x7F000000` works.

---

## 6. Two mistakes already made here, so they are not made twice

**A confident reading of nothing.** Aperture runs 2 to 4 reported the marker
absent from `0x42420000` - an address that exists nowhere. The card's ROM closes
the extended register lock behind a mode set, the probe did not re-open it, and
every window register then read the constant `42h`. Worse, the report recorded
`Cr58ReadBackHonoured=no`, which reads as a finding about the card refusing a
write and would have been cited as one. Three registers agreeing is what gave it
away; the probe now refuses to proceed when CR58, CR59 and CR5A read identically.

**A PCI-shaped model applied to a bus that is not PCI.** This work predicted
`0x7F000000` would fail because it needs address line A31 where `0x04000000`
needs A26. VESA Local Bus is the 486's own local bus brought out to a slot: the
card sees A31-A2 directly and decodes them itself. Nothing routes anything, and
the chipset only has to refrain from claiming the range.

Both lessons from 2026-08-20 earned their place again. Read the whole report
before concluding from one field - that is what turned "VBE 2.00, tier-0 is
back" into "something on this machine reports VBE 2.00 and its own numbers
contradict the hardware". And a value that agrees with expectation is not
thereby correct - CR58's window size looked right, and the enable bit next to it
was clear.
