# The ViRGE's VL defect is mixing framebuffer and MMIO access, not the MMIO window

Date: 2026-09-22
Status: **answered, by the primary investigator, on the record.** The trigger
is named; the exact access pattern is under NDA and will not be disclosed. This
kills the B-window fallback, the ViRGE/DX substitution, and the premise that a
DIB-Engine driver with DirectDraw acceleration can be made safe on a VL ViRGE.

Closes the question this project
[asked in the thread](https://www.vogons.org/viewtopic.php?t=76647) on
2026-08-22 (quoted back in reply 363) and supersedes the two secondhand
reports recorded in
`docs/plans/virge-vlb-old-mmio.md` on 2026-08-22 and 2026-08-23. Source is
mkarcher, replies 363 and 365 of the S3 ViRGE VLB thread, 2026-09-21 and
2026-09-22, answering this project's question directly.

## The answer

The MMIO window is not the variable. mkarcher: "It does not matter what mode
you use." The fault occurs when framebuffer memory access and certain regions
of MMIO space are **mixed**. He states plainly that he cannot disclose the
details, so no further refinement of the pattern is available by asking.

Consequences he gives:

- One outcome is corruption of the hardware configuration.
- The worst case is accidentally enabling bus interface features such as PCI
  retries while the chip is in VL mode, which locks up the VL bus **at some
  time later**.
- A PCI-connected ViRGE handles the same access patterns perfectly. The defect
  is in the VL interface alone.

The delayed, silent nature of the corruption explains the 2026-08-23 report
that no reliable reproduction pattern could be found, and it retires the
hypothesis recorded that day. The clustering on Win9x DOS-box text/graphics
transitions was a symptom of where framebuffer and accelerator traffic
interleave most densely, not a cause. **Nothing in this project should treat
the mode transition as the thing to fix.**

## What each MMIO variant is worth

| Variant | Layout | Verdict |
|---|---|---|
| A, no LFB | MMIO at `A0000`, framebuffer unreachable | Immune - and unable to draw |
| A, with LFB | MMIO at `A0000`, LFB at CR59/CR5A | **Susceptible.** This was the plan's chosen configuration |
| B | 64 KiB framebuffer window at `A000`, MMIO at `B800` | **Worse.** Susceptible with no high LFB at all |
| new MMIO | LFB + 16 MiB | Architecturally impossible in VL mode (reply 206) |

Variant A without the LFB is immune only because there is then no way to reach
the framebuffer at all. The immunity comes from the absence of a drawing path,
not from the window choice, so it is not a configuration a display driver can
ship.

Variant B is not the fallback this project recorded it as. It places a
framebuffer window and an MMIO window in low memory simultaneously, which is
the mixing condition by construction. **Reject `0xB8000`; do not implement it
as a measured fallback.**

## The ViRGE/DX cannot stand in for a 325

mkarcher modded the strap resistors on a PCI ViRGE/DX to select VL. Two straps
encode the bus type; Trio64V+ and the classic ViRGE document `01` for VL and
`10` for PCI. The DX kept responding as a PCI device whatever the straps were
set to, and the low strap always read back as zero, giving `00` or `10` and a
perfectly working PCI interface - `00` is not a specified setting. He expects
the DX core still contains the same buggy VL interface with it shut down
deliberately.

So Stage 4's requirement of a real 86C325 is now evidenced rather than
cautious, and no DX-based experiment can measure the VL defect.

## Why this lands on Velocity9x in particular

Asked about protecting the MMIO range with the VMM, mkarcher answered that the
range can indeed be protected, but that his own failures came from the driver
mixing accelerator MMIO access with unaccelerated framebuffer drawing done
where acceleration makes no sense.

That is a description of this driver. Velocity9x is a DIB Engine driver: GDI
rasterises straight into the framebuffer through `V9xScreenSelector`, while the
32-bit HAL issues DirectDraw fills and screen copies through MMIO. The pattern
that corrupts the VL interface is not an edge case this driver might avoid - on
VL it would be the steady state, from the first mouse move over an accelerated
surface.

It also explains Terminal Velocity, which mkarcher did get accelerated on his
VL 325. A full-screen S3D game owns the display and draws through the engine;
there is no unaccelerated GDI framebuffer traffic to interleave with. The
immune configuration was reached by architecture, not by a trick.

## Hypotheses this kills

- That the old-MMIO A-window would be safe where new MMIO was not. The window
  was never the variable.
- That `0xB8000` is a fallback worth measuring.
- That the DOS-box mode transition is the defect's cause and that sequencing
  CR53 around it is a mitigation.
- That a ViRGE/DX could substitute for a 325 in any VL experiment.
- That asking for more detail could narrow the access pattern further.

## What this leaves

The defect is now specific enough to design against, which is more than this
project had before. Two things follow.

**A diagnostic.** Because the configuration is corrupted silently and the bus
lockup arrives later, a hardware test must not wait for a hang. Read back the
bus-interface configuration registers after mixed access and compare against
what was written. That detects the fault where it happens. 86Box cannot help:
it models the register layout, not the corruption, so the emulator remains an
oracle for sequencing only.

**A bounded design, if acceleration on VL is attempted at all.** Any safe
configuration must have no CPU framebuffer access while MMIO is live. See
`docs/plans/virge-vlb-old-mmio.md` for why running all 2D through the engine
does not achieve this, and for the full-screen-exclusive variant that is the
only version of the idea worth measuring.
