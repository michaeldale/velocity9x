# The survey at schema 2: what a VLB machine forced, and what it has not yet said

Date: 2026-08-20, measured 2026-08-21
Status: **run on the 486. The card is identified and the platform is mapped; the
aperture question the run existed to settle is still open, for a reason the tool
predicted**

Stage 1 of `docs\plans\vlb-survey-tool.md`. The card in hand is an S3 Trio on
VESA Local Bus in a 486, and the survey we shipped would have come back from it
nearly empty, because almost everything it collected was reached through PCI.
This records what was changed and why, and — separately, and honestly — what has
and has not been measured.

Read this note with `docs\specifications\vga-survey.md`, which is the report
contract and now describes schema 2 key by key. This note is the reasoning; that
one is the interface.

## What schema 2 adds

### 1. The chipset can be identified without PCI

`run_tier2` used to return immediately with `no-pci-display-device-identified`
when the PCI walk found nothing, so the S3 register probe — the thing we
actually want from a VLB card — never ran. It now falls back to
`identify_non_pci`, and the whole design of that function is the order of
operations:

**Read first, write second.** The identity registers are read with the locks
exactly as the BIOS left them, and the CR38/CR39 unlock keys are written only
after those reads have already spelled S3. The unlock becomes a confirmation of
an answer rather than a guess that could be wrong on hardware where 48h and A5h
in CR38/CR39 mean something else entirely.

Two accept signals, weighted by how specific each one is:

| Signal | Specificity | Accepts alone? |
|---|---|---|
| CR2D/CR2E high byte in {56, 88, 89, 8A, 8C, 91} | 6 of 256 | yes |
| CR30, one of 19 documented values | 19 of 256, over 7 of 16 nibbles | no |
| An S3 string in the ROM image at C000 | corroboration only | never |

The CR30-only case needing corroboration is the addition to the plan, and it is
there because CR30 is what the oldest VLB parts have — the 86C801/805 and 86C928
predate CR2D/CR2E — and a 19-value accept set on its own is a weaker gate than a
6-value one. Requiring the ROM to also name S3 costs nothing: the ROM is already
dumped verbatim into `[VideoBios]`, so this adds no information a host-side
reader could not find. It just has to be available *before* the unlock to gate
it.

Both signals are reported either way, under `[Chipset.Identify]`, so a refusal
is diagnosable from the report instead of being a dead end.

**The residual risk, stated plainly.** CR38/CR39 on a non-S3 card mean something
else. The locked-read-first ordering shrinks the exposure but does not remove
it, and the Tier 2 prompt now says so out loud on a machine with no PCI: it
tells the tester that the check is trusting their word about the card as much as
it is trusting the registers.

**The case this does not solve.** If a part genuinely gates *reads* behind
CR38/CR39 — which the plan's premise says S3 does not, and which every S3
capture so far supports, because the video BIOS leaves CR38/CR39 holding 48h/A5h
at POST — then the locked read returns nothing usable and the tool stops before
writing. On an 86C801/805 that would mean a report that cannot name the card.
That is the honest outcome of the safety ordering, and it is left as an open case
rather than papered over with an override switch. The parser now reports the
locked-versus-unlocked disagreement over CR30-CR3F explicitly, so the first run
that hits it will say so rather than quietly returning a wrong name.

### 2. The whole extended register file, not a list chosen for the PCI parts

Tier 2 used to read eleven CRTC and five sequencer registers. It still reports
those under their own names — nothing in the schema-1 output regressed — and
adds CR30-CR6F and SR08-SR1F as blobs behind the same unlock. CR30-CR3F and
SR08-SR1F overlap the locked Tier 1 dump on purpose: where the two disagree, the
disagreement is the finding, and the parser looks for exactly that.

Which registers differ on a VLB part is not yet known. Capturing all of them is
what stops finding out from costing another round trip to a tester. The
likely candidates — CR5D/CR5E extended overflow, and the CR40/CR53/CR55 group —
were none of them in the old list.

The DAC identity was added the same way: six consecutive reads of 3C6h,
bracketed at both ends by a read of 3C8h that resets the DAC's internal access
counter. Which read in the sequence returns the hidden command register differs
between parts, so all six are reported and the decoding is left host-side.
Nothing in it writes.

### 3. The linear window is interrogated

This is the question the run exists to answer. On the PCI parts the window is a
BAR and the host bridge routes it. On VLB the position is programmed into
CR58/CR59/CR5A and the 486 chipset has to decode it — on a machine that may have
less RAM than the window is wide and may not decode high addresses at all.

`/aperture` is a third opt-in, separate from `/tier2` and implying it (the
window base is something only Tier 2 can read). It copies 32 bytes out of the
window's physical base with INT 15h AH=87h — the BIOS extended-memory block
move: a service, not a mechanism this tool implements, and a copy *from* the
address in question *to* a buffer in the program's own data segment.

Three limits, and all three are in the report rather than only in this note:

- **16 MB.** The AH=87h descriptor base is 24 bits. If CR59/CR5A point higher,
  the base is reported and the read is skipped with a stated reason.
- **No mode was set.** On these parts the window may only answer once a mode has
  been set with linear addressing enabled, so a dead result is suggestive and
  not conclusive. Settling that needs a mode set, which belongs in a later
  bring-up probe on our own card and not in a tool handed to strangers.
- **A base at or below the top of installed RAM returns RAM.** That looks alive
  and proves nothing. The tool reports the base and the memory figures; the
  parser compares them and returns `unreadable-by-this-method` rather than
  `window-responds`.

Under EMM386 the CPU is in virtual-8086 mode and AH=87h is intercepted and
emulated by the memory manager rather than executed by the BIOS. The result is
usually still the physical bytes, but it is a different code path, so
`[Aperture]` repeats the HIMEM/EMM386/V86 facts from `[Platform]` beside its own
verdict, the parser attaches a caveat when it sees them, and the tester
instructions ask for a clean boot (F8, command prompt only) for the `/aperture`
run.

### 4. Platform facts a 486 makes relevant

New `[Platform]` section: CPU class, installed RAM three ways, A20, and the
memory managers. None of it matters on the PCI targets; all of it constrains
where an aperture can go.

The CPU part needed care. The binary is 8086 code — the build now passes `-0`
explicitly rather than inheriting it from `wcl`'s default — and the AC and ID
bit tests that separate 386 from 486 from CPUID need 32-bit PUSHFD/POPFD, which
are 386-only encodings. So the 32-bit probes sit behind a 16-bit pre-check:

1. FLAGS bits 12-15 stick set after being cleared → 8086 or 186. Stop.
2. FLAGS bits 12-14 will not stick when set → 286. Stop.
3. Only now execute PUSHFD. AC (bit 18) separates 386 from 486; ID (bit 21)
   says whether CPUID exists.
4. Only now execute CPUID.

Step 2 can answer "286" wrongly on a 386 under a memory manager, because POPF in
V86 mode is emulated by the monitor rather than executed. That costs detail,
never safety: what gates the 32-bit encodings is the probe result alone, never
an inference, and the report carries the probe answer and any inference from a
V86 host under separate keys so the two cannot be confused. A machine that lands
in that branch loses the CPUID detail and the E820 map, and says so.

A20 is detected read-only, by comparing the 256 bytes of the interrupt vector
table against their alias at FFFF:0010. The usual test writes a marker; this one
does not need to.

### 5. The build gate grew, and admits what it cannot see

`scripts\build-vga-survey.ps1` is the reason this tool can be handed to a
stranger, so it grew with the tool. It now also refuses the VBE setter functions
(4F05h-4F0Bh), and — because schema 2 introduced inline assembly — checks two
new surfaces:

- **Every literal port constant at an `inp`/`outp` call site** must be one of
  the audited VGA ports, and every non-literal must be one of the two derived
  index ports. A new port cannot be introduced without editing the list.
- **The raw opcode bytes a `#pragma aux` emits** are allowlisted (there is one:
  SMSW AX, spelled out by hand because Open Watcom's inline assembler will not
  accept the mnemonic at any CPU setting it offers for a 16-bit DOS target), and
  the state-changing instructions a pragma could otherwise smuggle in — LGDT,
  LIDT, LMSW, OUT, MOV to a control register, any software interrupt other than
  INT 15h — are named and refused.

What it still cannot do, and the script says so where the rules live: it cannot
check that no `outp` reaches a port outside the audited set, because most calls
take a variable and the port value is simply not in the source text.

`-GateSelfTest` runs the gate against nine deliberately broken copies of the
source and asserts every one is rejected. It needs no compiler, so it is a
check, not a ceremony.

## What has been verified, and how

| Check | Result |
|---|---|
| `build-vga-survey.ps1 -GateSelfTest` | 9 of 9 mutations rejected, clean source accepted |
| `build-vga-survey.ps1` | builds clean at `-0 -wx`, no warnings |
| `run-checks` | green, with the gate self-test now a step in it |
| every 386-only encoding confined to its gated function | confirmed by disassembling the object: `platform_cpu` and `platform_e820`, nowhere else |
| parser over hand-built schema-1 and schema-2 reports | every new branch exercised |

## The 486 run, 2026-08-21

Three reports, committed beside this file, taken in the order the safety property
requires. Build `be8ff43`.

| Run | Command | Report |
|---|---|---|
| 1 | `/notier2 /rom` | `...-486-trio64-tier1.ini` |
| 2 | `/tier2 /rom` | `...-486-trio64-tier2.ini` |
| 3 | `/aperture /rom` | `...-486-trio64-aperture.ini` |

**The machine.** Intel 486, CPUID signature `00000480` (family 4, model 8),
DOS 6.22, no Windows. No PCI BIOS: `INT 1Ah AX=B101h` failed, so the
`display_device_count == 0` branch executed for the first time on any target.

**The card.** S3 Trio64. CR2D/CR2E `88`/`11`, CR30 `E1`, revision `02`, and the
ROM at C000 is a Diamond Stealth 64 DRAM BIOS v2.02 dated 01/19/95, 32 KB,
checksum ok. CR36 `99` decodes to 2 MiB and VBE independently reports 2 MiB, so
the memory decode agrees with the BIOS on a third chip.

### 1. The locked-read identification worked, and it was not redundant

This is the finding that justifies the whole ordering, and it is stronger than
expected.

`LockedCR38=59`, `LockedCR39=BD`. **Not `48`/`A5`.** Both PCI S3 test machines
show `48`/`A5` sitting in those registers before the survey runs, which is why
`vga-survey.md` used to say a declined Tier 2 costs little on S3 parts *because
the video BIOS leaves the extended bank unlocked at POST*. This BIOS does not
leave it unlocked - and the locked read of CR2D/CR2E/CR30 returned the true
`88`/`11`/`E1` anyway.

So the premise the fallback was built on is now measured rather than assumed: on
S3 the CR38/CR39 locks gate **writes, not reads**. Run 1, which unlocked nothing
at all, is enough to name the card - the parser identifies it from the Tier 1
locked dump alone. That reasoning has been corrected in the spec, which had the
right conclusion for the wrong reason.

The locked and unlocked reads agree exactly, and the parser found no
disagreement anywhere in the CR30-CR3F range the two dumps share.

### 2. CR38 and CR39 do not read back what is written to them

Writing `48` to CR38 and reading it back inside the unlocked window returns
`5B`, not `48`. Two locked reads of CR38 in the same run returned `59` and `5B`.
CR39 reads `BD` whether or not `A5` has just been written to it.

So they are key latches, and Tier 2's "save CR38/CR39 and put them back"
restores a value that was never really in them. The discipline still delivered
its outcome, and this is provable rather than hopeful: **run 1's Tier 1 CRTC bank
is byte-for-byte identical to run 3's**, across a run 2 that unlocked and
re-locked in between. The machine ended where it started. But the mechanism is
not the one the code's comment assumes, and the next person reading that code
should know it.

### 3. The linear window is configured and switched off

`CR58=03`: window size code 3 (4 MiB), and **bit 4 clear - linear addressing is
disabled**. `CR59`/`CR5A` = `7F`/`00`, so the window is positioned at
`0x7F000000`, just under 2 GiB.

That is a coherent picture: the BIOS has placed and sized a 4 MiB window and left
it disabled, which is exactly what the driver's `v9x_s3_enable_linear_aperture`
exists to turn on.

### 4. The aperture question is still open, for the reason the tool predicted

`0x7F000000` is above 16 MiB, and `INT 15h AH=87h` has a 24-bit descriptor base.
The probe reported the base and skipped the read:
`Reason=base-above-int15h-ah87h-16mb-limit`.

The limitation was written into the tool before the run and it is the first thing
the run hit. **Whether the 486 chipset decodes anything at `0x7F000000` remains
unknown**, and no amount of re-running this tool will answer it - `AH=87h` cannot
reach there. Settling it needs a probe that can address above 16 MiB, which means
unreal mode or DPMI, which means a different tool with a different safety
argument. That is now the top of the VLB work, not a footnote.

### 5. The VBE 2.00 was a TSR, not the card - and it moved the window

The plan said tier-0 was closed off because every S3 BIOS measured reports VBE
1.2, and "a VLB board's BIOS will be older, not newer". This machine reported
**VBE 2.00**, with `OemVendorName=Dietmar Meschede`,
`OemProductName=S3 VBE/Core 2.0`, and every graphics mode carrying attribute
`009B` - bit 7 set, linear framebuffer available - with `PhysBase=04000000`.

**Resolved: that is S3VBE 3.18, a third-party VBE 2.0 TSR, resident on the
machine.** Michael identified it directly, and the documentation confirms it -
"Copyright (c) 1994,97 Dietmar Meschede", supporting Vision864 through ViRGE/GX -
matching `OemVendorName` byte for byte.

The report already argued for that conclusion before it was confirmed, and the
reasoning is worth keeping because it is the reasoning a future report will need.
The full 32 KB ROM was dumped and searched: `S3 Inc` and `Vision864` are present
as plaintext and `OemString=S3 Incorporated. Vision864` matches that plaintext
exactly, so the OEM string provably came from the ROM - while `VESA`, `VBE/Core`
and `Dietmar Meschede` appear nowhere in it. The card's own VBE version is
therefore still unmeasured, and **the plan's premise stands unchallenged**: no S3
ROM has yet been seen reporting anything but VBE 1.2.

`Int10Vector` and `Int42Vector` were added to `[BiosData]` because of this. A
segment inside C000-C7FF is the ROM answering for itself; anything else is
something in between. The next report of this shape settles it without needing
the tester to know what they have loaded.

**The lead this leaves behind is worth more than the puzzle was.** S3VBE
advertises the linear framebuffer at `0x04000000` - 64 MiB - while the card's own
CR59/CR5A hold `0x7F000000`, just under 2 GiB. Those cannot both describe where
the window is, and S3VBE is a program that actually drives linear modes on these
chips rather than merely parking a default. Two things follow:

- **64 MiB is a far more plausible address for a 486 VLB board to decode than
  2 GiB.** The first needs A26; the second needs A31. The BIOS's `0x7F000000` may
  simply be a parking spot that nothing on this machine answers at, which would
  make a negative aperture result at that address expected rather than
  informative.
- **A VLB driver may have to *program* the window base, not just enable it.**
  `v9x_s3_enable_linear_aperture` today sets the size and enable bits in CR58 and
  leaves CR59/CR5A alone, and `v9x_s3_read_aperture` takes whatever base it finds
  - which on the PCI parts is a base the host bridge has already routed. There is
  no host bridge here. If S3VBE has to move the window to make linear addressing
  work, so will we.

Neither is established. Both are testable, and the test does not need the
survey: set a linear VBE mode through S3VBE and read CR59/CR5A afterwards. If
they read `04`/`00` rather than `7F`/`00`, S3VBE moves the window and we know
where to. That is a bring-up probe on our own machine, not something to hand to
a stranger.

### 6. The clean boot did not happen, and it cost a real check

`ProtectedOrV86=yes`, `MachineStatusWord=0011`, XMS 3.00, EMS 4.0 at segment
`029E`. All three runs were under HIMEM and EMM386 in virtual-8086 mode.

The visible consequence: `Int1588ExtendedKB=0`, with E801h and E820h both
unsupported. EMM386 hides extended memory from `AH=88h`, so the report carries no
usable RAM figure at all - and that figure is what the aperture false-positive
check compares a window base against. The parser was reporting "1,048,576 bytes"
from `1 MB + 0 KB`, which is not a small error: a wrong small answer silently
disables the check. Fixed, and it now says the figure is unknown and why, and
attaches a caveat to any positive aperture result it could not check.

It did not matter here, because the base is above 16 MiB and no read happened.
It would have mattered on a card with a low window.

### 7. Two smaller findings worth keeping

**A VLB card can publish a PCI id.** The ROM carries a valid `PCIR` structure
reporting `5333:8811`, because Diamond shipped one BIOS image for both the PCI
and the VLB variant of the board. That is a completely read-only identification
route needing no bus at all, and the parser now uses it - it corroborated the
register read here, and on a card whose registers are ambiguous it may be the
only thing that names it.

**The DAC probe found nothing, correctly.** All six reads of 3C6h returned `FF`.
The Trio64 integrates its DAC, so there is no separate part with a hidden
identity register to find. A negative result from a probe that cannot succeed on
this chip, recorded so the next reader does not re-investigate it.

`EDID` is unsupported: `4F15h` returned `4F00`. Expected of a 1995 board.

## Still outstanding

1. **Read the window - at whichever address it really lives.** The question the
   survey existed to answer. Both candidates, the BIOS's `0x7F000000` and
   S3VBE's `0x04000000`, are above the 16 MiB ceiling of `INT 15h AH=87h`, so
   **no further run of this tool can answer it**. It needs a probe that can
   address above 16 MiB - unreal mode or DPMI - and its own safety argument.
   Everything else here is legwork; this decides whether VLB support is
   possible.
2. **Re-run from a genuinely clean boot** (F5, no CONFIG.SYS at all - F8 with
   command-prompt-only still ran CONFIG.SYS on this machine). Two things come
   from it: real `AH=88h`/`E801h` memory figures with EMM386 out of the path,
   and the card's **native** ROM VBE version with S3VBE unloaded. The second is
   what puts "no S3 BIOS offers a linear framebuffer" in writing from
   measurement rather than from inference, which is what the plan asked for and
   what closes off tier-0 for this family properly.
3. **The schema-2 regression on the PCI targets** - 86Box ViRGE `:9869` and
   Trio64 `:9871` - confirming the report is a superset of the schema-1 one.
   Still not done, still needs no 486, and it is the check that would catch a
   schema-2 change having broken something that used to work.
4. **An emulated 486 VLB machine** in 86Box, which the plan wanted ahead of the
   real card and which the real card has now overtaken. Still worth building: it
   is where a probe that writes to CR58 can be tried without risking the only
   physical card.

Two lessons from 2026-08-20 applied here and earned their place. Reading the
whole report is what turned "VBE 2.00, tier-0 is back" into "something on this
machine reports VBE 2.00 and its own numbers contradict the hardware". And a
value that agrees with what we expected is not thereby correct - CR58's window
size looked right, and the enable bit next to it was clear.

## What this schema does not decide

- Whether the driver's DPMI mapping can reach the window on a 486. That needs a
  build, not a survey.
- Whether the 8514/A blitter behaves the same on the VLB part.
- The install path. A VLB card has no PCI hardware ID, and the INF is generated
  from the family manifest's `VendorId`/`DeviceId` pairs, so SetupX has nothing
  to bind. That needs a different binding — most likely a hand-chosen generic
  display ID plus Have Disk — and the survey says nothing about it.
