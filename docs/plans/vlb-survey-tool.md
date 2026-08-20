# Survey tool v2: a 486 with an S3 Trio VLB

## Context

We want VLB support. The card in hand is an S3 Trio on VESA Local Bus in a 486,
and the first step is a survey run from that machine. **The survey tool we ship
today would come back nearly empty from it**, because almost everything it
collects is reached through PCI.

That is not a small gap. VLB removes the one thing every existing family relies
on to know what card it is looking at, so the survey has to answer questions the
PCI targets never had to ask.

## What breaks on VLB, and why the survey has to change

Established by reading the tree, not assumed:

1. **There is no PCI configuration space.** `run_tier2`
   (`tools/diag/vga_survey_dos.c:1227-1234`) returns immediately with
   `no-pci-display-device-identified` when the PCI walk found nothing, so the S3
   register probe at `:1085` — the thing we actually want — never runs. The
   `[Pci.*]` sections are empty too. A tester on this machine sends back a
   report consisting of the VGA register file, the ROM image and VBE, and
   nothing that identifies the chip.

2. **The driver cannot identify the card.** Identification is a PCI scan:
   `v9x_pci_vendor` / `v9x_pci_device` / `v9x_pci_match`
   (`src/display16/ddi.c:95-104`), matched by `V9xFindPciDevice`. The `s3`
   family sets its PCI-miss-tolerated flag to `0` (`src/chipsets/s3/s3_hw16.c`,
   last field: *"The card must be one of ours"*), so a VLB Trio is refused at
   stage 1 even though every register the driver then wants is present. Only the
   `vbe` family tolerates a miss — and see (4).

3. **Installation has no hardware ID to match.** The INF is generated from the
   family manifest's `VendorId`/`DeviceId` pairs — `5333`/`8A01` and
   `5333`/`8811` in `packaging/families/s3/family.psd1` — so it advertises
   `PCI\VEN_5333&DEV_8811` and `&DEV_8A01`. SetupX cannot bind that to a device
   which is not on a bus it enumerates.

4. **Tier-0 is unavailable, so this has to be a native path.** Tier-0 learns the
   framebuffer address from VBE 4F01h `PhysBasePtr`, which requires VBE 2.0.
   Every S3 BIOS measured so far reports **VBE 1.2 with no linear framebuffer on
   any mode** (`docs/decisions/2026-08-20-vbe-mode-inventory.md`), and a VLB
   board's BIOS will be older, not newer. So the aperture must come from the
   chip — which is what the `s3` family's CR59/CR5A hook already does — and the
   survey has to tell us whether that window exists and where it can live.

**The central unknown is the linear aperture.** On the PCI parts it is a BAR the
host bridge routes for us. On VLB the linear address window is programmed into
CR58/CR59/CR5A and the 486 chipset has to route it — on a machine that may have
less RAM than the window is wide, and may not decode high addresses at all.
Everything else here is legwork; this is the question that decides whether VLB
support is possible at all.

## Scope

A new schema revision of the existing tool, not a new tool.
`tools/diag/vga_survey_dos.c` keeps its shape — real-mode DOS, one INI report,
almost no interpretation, blobs decoded host-side by
`scripts/parse-vga-survey.ps1` — and keeps both safety properties: the two-tier
split, and the build-time source gate in `scripts/build-vga-survey.ps1:36-48`
that refuses to compile a mode set or a PCI write.

Bump `V9X_SURVEY_SCHEMA` from `"1"` to `"2"`. The parser accepts both.

## Changes

### 1. Identify the chipset without PCI

The core fix. Today the vendor comes from PCI; on VLB it has to come from the
chip.

- The bus fact is **already captured**: `survey_pci_bios`
  (`tools/diag/vga_survey_dos.c:441-457`) writes a `[PciBios]` section recording
  whether `INT 1Ah AX=B101h` answered, with `int1a-b101-failed` when it did not,
  and the display-device count follows from the walk. No new `[Bus]` section is
  needed; what schema 2 adds is the *parser* drawing the "this is a non-PCI
  machine" conclusion from keys the report already holds.
- `run_tier2` gains a fallback: when the PCI walk found nothing, attempt **S3
  identification by register — from the locked reads first**. Tier 1 already
  dumps CR00-CR3F and SR00-SR1F without unlocking anything (`:1075-1076`), and
  its own comment records that several families leak a usable identity through
  the lock; on S3 the CR38/CR39 locks gate *writes*, not reads, so CR2D/CR2E
  and CR30 are expected to read true while still locked. So: read the id
  locked, and only if it already spells S3, unlock with the documented
  CR38/CR39 keys (`0x48`, `0xA5` — the same pair `tier2_s3` and the driver use)
  to cross-check and take the full tier-2 capture. Report both the locked and
  unlocked id values so a disagreement is visible host-side. Anything else
  reports `unidentified-non-pci-display` and stops before any write.
- Accept **CR30 chip ids as well as CR2D/CR2E device ids**. CR2D/CR2E exist on
  Trio and later; the VLB era skews older (86C801/805, 928, Vision864/868/964,
  Trio32/64 VLB), and on the oldest of those CR30 is the only id register.
  `tier2_s3` already reads CR30; the accept set has to include its known
  values, not just the CR2D/CR2E pairs.
- Guard rails, for the unlock-confirmation step: it stays **tier 2 (opt-in)**,
  runs only after the tier-1 report is closed on disk, restores CR38/CR39
  before reading anything else, and restores the CRTC index last. That is the
  existing discipline applied one step earlier in the sequence.

The real risk, stated plainly: CR38/CR39 on a non-S3 card mean something else.
The locked-read-first ordering shrinks it — the unlock writes happen only after
the locked registers have already answered "S3", so they are a confirmation, not
a leap — but it is not zero. The tester is telling us they have an S3, and the
tool should say in its prompt that this is what it is trusting.

### 2. Dump the whole extended register file, not a hand-picked list

`tier2_s3` reads eleven CRTC and five sequencer registers, chosen for the PCI
parts. We do not yet know which registers differ on VLB, and finding out must
not cost another round trip to the tester.

- Tier 1 already dumps CR00-CR3F and SR00-SR1F **locked**
  (`vga_survey_dos.c:1075-1076`). What schema 2 adds is the **unlocked**
  complement in tier 2: CR30-CR6F and SR08-SR1F as hex blobs behind the same
  CR38/CR39/SR08 unlock `tier2_s3` already performs. Where the locked and
  unlocked values disagree, that disagreement is itself data — record both.
- Keep the existing named single-register lines so current parser output does
  not regress.
- Add the DAC id (the RS2 hidden-register read sequence — repeated reads of
  0x3C6). The single 0x3C6 pixel-mask and 0x3C2 input-status reads are already
  in tier 1 (`:1068-1070`); the repeated-read id sequence is the new piece.

This follows the tool's own philosophy — interpret host-side, so a decoding
mistake is a script fix rather than a re-ship. The CR5D/CR5E extended-overflow
bits and the CR40/CR53/CR55 group are the likely VLB differences, and none of
them are in the current list.

### 3. Interrogate the linear address window

The question the run exists to answer. From the state the BIOS left:

- CR58 (window size and enable), CR59/CR5A (window base) — read today, but now
  with the size field decoded host-side rather than the base alone.
- CR53 (MMIO / new-mode enables) and CR40 (engine enable) — already read.
- Whether the window is enabled at boot at all, and where the BIOS put it.

Then the one genuinely intrusive addition:

- **An opt-in, read-only aperture decode test** (`/aperture`, separate from
  `/tier2`). Read a few dwords from the window's physical base via
  `INT 15h AH=87h` extended-memory block move — a BIOS service, read-only, and
  reachable from real mode without unreal mode or DPMI. Report the bytes.
  All-`FF` or all-`00` means nothing decodes there; VGA-looking contents mean
  the window is live.
- **A limitation that must appear in the report itself, not just here.** The
  survey sets no mode, and on these parts the window may only respond once a
  mode has been set with linear addressing enabled. So a negative result is
  suggestive, not conclusive. Settling it needs a mode set, which is out of
  scope for a tool handed to strangers — that belongs in a later bring-up probe
  on our own card.
- `INT 15h AH=87h` only reaches the first 16 MB. If CR59/CR5A point above that,
  report the base and skip the read with a stated reason rather than pretending.
- **A false positive the verdict has to rule out:** if the window base sits at
  or below the top of installed RAM, `AH=87h` returns whatever RAM holds there —
  a live-looking result that says nothing about the card. The parser must
  compare the base against the RAM answer from section 4 and report a sub-RAM
  base as `unreadable-by-this-method`, not as live.
- **Memory managers change the path.** Under EMM386 the CPU is in V86 mode and
  `INT 15h AH=87h` is intercepted and emulated by the manager rather than
  executed by the BIOS. The result is usually still the physical bytes, but it
  is a different code path, so the report must place the HIMEM/EMM386 facts from
  section 4 next to the aperture verdict, and the tester instructions should ask
  for a clean boot (F8, command prompt, no config.sys) for the `/aperture` run.

### 4. Platform facts a 486 makes relevant

None of these matter on the PCI targets; all of them constrain where an aperture
can go.

- **CPU class without CPUID.** Early 486s have no CPUID. Detect by the
  documented EFLAGS route — the AC bit distinguishes 386 from 486, the ID bit
  says whether CPUID exists — and only execute CPUID after that. Report the
  class, and the CPUID vendor/family when available. One build constraint: the
  tool compiles as 8086 code (`wcl` in `build-vga-survey.ps1` passes no CPU
  flag), and the AC/ID tests use 32-bit `pushfd`/`popfd`, which are 386-only
  opcodes. They must sit in inline asm behind a pre-check that separates
  8086/286/386 by the 16-bit FLAGS high-bit behaviour, so a pre-386 CPU never
  reaches the 32-bit encodings, and the rest of the tool stays 8086-clean.
- **Installed RAM**, via `INT 15h AH=88h` and `AX=E801h`, plus `E820h` if the
  BIOS has it. An aperture must not overlap RAM, and on a 4 MB or 8 MB 486 the
  choice of where it can live is narrow.
- **A20 state**, and whether HIMEM/EMM386 is loaded — both affect what a driver
  can later map.
- **Video BIOS ROM strings and date** — already collected, and worth keeping,
  because on a VLB board the ROM string is what identifies the exact card and
  its default window placement.
- **VBE version and the full mode list with per-mode attributes** — already
  collected. We expect VBE 1.2 and no linear-framebuffer attribute anywhere;
  recording that definitively is what closes off tier-0 in writing rather than
  by inference.

### 5. Host-side parser

`scripts/parse-vga-survey.ps1` gains schema-2 awareness, the `[Bus]` section,
the register-blob decoders (CR58 window size, CR36 memory size, CR2D/CR2E to a
chip name), the aperture verdict and the platform section. Schema-1 reports must
still parse.

## What this run does *not* decide

Worth writing down so the survey is not over-read:

- Whether the driver's DPMI mapping can reach the window on a 486. That needs a
  build, not a survey.
- Whether the 8514/A blitter behaves the same on the VLB part.
- The install path. A VLB card has no PCI hardware ID, so the INF needs a
  different binding — most likely a hand-chosen generic display ID plus Have
  Disk. Worth thinking about before the driver work, but the survey says nothing
  about it.

## Deliverables

1. `tools/diag/vga_survey_dos.c` at schema 2, with the sections above.
2. `scripts/build-vga-survey.ps1`: extend the banned-pattern list to match the
   new surface. It already bans `0x4f02` and PCI writes. A ban on "any `outp`
   to a port outside the documented ranges" is not expressible there — the gate
   is a regex over source text, and most `outp` calls take a variable
   (`index_port`), so the port value is invisible to it. The enforceable form
   is: ban any **new literal port constant** outside the audited list the
   source already uses, and keep all port I/O going through the existing
   helpers (`read_indexed`/`write_indexed`/`dump_indexed_range`) so every call
   site stays auditable by eye. That gate is the reason this tool can be handed
   to strangers, and it should grow with the tool — honestly about what a
   textual gate can and cannot see.
3. `scripts/parse-vga-survey.ps1` updated, schema-1 compatible.
4. A `docs/decisions/` note recording what the 486 run actually returned, shaped
   like `2026-08-20-vbe-mode-inventory.md`, with the raw report committed beside
   it.

## Verification

- Host: `run-checks` green; the build gate rejects a deliberately added
  `0x4f02` and a deliberate PCI write.
- **On PCI hardware we already have:** run schema 2 on the 86Box ViRGE and
  Trio64 and confirm the report is a superset of the schema-1 one — identical
  values for every key that existed before. This is the regression check, and it
  needs no 486.
- **On an emulated 486 VLB machine first.** The `display_device_count == 0`
  branch has never executed on any target — everything surveyed so far has PCI.
  86Box can build a 486 VLB machine with an S3 VLB card; run the full tier-1 →
  tier-2 → `/aperture` sequence there before the tool goes to a stranger. That
  exercises the no-PCI-BIOS path, the locked-read identification fallback and
  the aperture probe on a machine we control, where a wedge costs a reboot of an
  emulator instead of a round trip to a tester.
- **On the 486 VLB target:** tier 1, then tier 2, then `/aperture`, in that
  order, keeping each report. The ordering *is* the safety property — if a later
  stage wedges the machine, the earlier reports survive on disk.
- Read the whole report before drawing conclusions from any single field. Both
  screenshot lessons from 2026-08-20 apply to surveys too: a value that agrees
  with what we expected is not thereby correct.
