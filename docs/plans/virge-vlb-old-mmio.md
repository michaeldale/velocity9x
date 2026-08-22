# ViRGE on VESA Local Bus, by way of old MMIO

## Context

[The Vogons MK-765VL thread](https://www.vogons.org/viewtopic.php?t=76647) spends
six years failing to make an S3 ViRGE work on VESA Local Bus, and the reason it
gives is one this driver is currently on the wrong side of. mkarcher's reply 206
states it plainly: a ViRGE in VL mode receives only A2 to A22, an 8 MiB window,
and SAUP1/SAUP2 select which 8 MiB it is. The "new MMIO" window every retail
ViRGE driver uses sits at the linear base plus 16 MiB and is therefore
unreachable. Madao's reply 205 draws the same line: the Trio64V+ works on VL
because its drivers use old MMIO, and the ViRGE does not because its drivers use
new MMIO.

Velocity9x is exactly that driver. [virge_hw16.c:52](../../src/chipsets/s3/virge/virge_hw16.c:52)
hardcodes the control window:

    *control_linear_base = framebuffer_linear_base + 0x01000000ul;

opened with CR53[3] at [virge_hw16.c:33](../../src/chipsets/s3/virge/virge_hw16.c:33),
and every S3D register access in
[eng_s3_virge.c:35-46](../../src/display32/engines/eng_s3_virge.c:35) reaches the
chip through `control_linear_base + offset` and through nothing else. That single
addend is the whole of the incompatibility. By contrast
[eng_s3_trio.c:32](../../src/display32/engines/eng_s3_trio.c:32) drives the
8514/A engine with `inpw`/`outpw` alone, which is why the Trio64 target is the
one already running on the 486.

The favourable half of this is that the engine layer is bus-agnostic as written.
It takes a base and adds offsets. Point the base somewhere else and it does not
care.

## What is already true, and what is not

| Piece | State | Evidence |
| --- | --- | --- |
| VLB, Trio64 | Aperture proven at `0x7F000000` and `0x04000000` on the real 486 | [aperture decision](../decisions/2026-08-21-vlb-aperture-answered.md) |
| VLB, install path | Manual-select INF installs; devnode will not start, Code 24 | [manual-select handover](../handoffs/2026-08-22-vlb-manual-select-handover.md) |
| ViRGE backend | Complete, PCI only | [virge_hw16.c](../../src/chipsets/s3/virge/virge_hw16.c) |
| S3D used for 2D | **Already built.** Hardware fill and screen-to-screen BitBLT | [eng_s3_virge.c](../../src/display32/engines/eng_s3_virge.c) |
| Old MMIO | Not implemented anywhere in the tree | grep: only CR53[3] appears |

So "S3D for 2D" is not new work. The ViRGE has no legacy 8514/A engine and does
its 2D through the S3D core, which this driver already does on PCI; thread
replies 352 and 353 restate the same architecture. The task is to make that
existing path reachable over a bus that cannot see 16 MiB up.

## The finding this plan rests on

Checked against the vendored 86Box ViRGE model rather than from memory.
[vid_s3_virge.c:1290-1297](../../build/reference/86box/src/video/vid_s3_virge.c:1290):

    if ((svga->crtc[0x53] & 0x10) || (virge->advfunc_cntl & 0x20)) { /*Old MMIO*/
        mem_mapping_disable(&svga->mapping);
        if (svga->crtc[0x53] & 0x20)
            mem_mapping_set_addr(&virge->mmio_mapping, 0xb8000, 0x8000);
        else
            mem_mapping_set_addr(&virge->mmio_mapping, 0xa0000, 0x10000);
    }

Both windows feed the same handler, and that handler decodes on `addr & 0xfffc`
([vid_s3_virge.c:2107](../../build/reference/86box/src/video/vid_s3_virge.c:2107)).
Two consequences carry this plan:

1. **Old MMIO reaches the entire ViRGE register map.** The S3D 2D block at
   `0xA000` to `0xA4FF` and the 3D blocks at `0xB000` and above are all at or
   above offset `0x8000`, so they land inside either window. Register offsets are
   identical between the two MMIO forms.
2. **`0xA0000` is in the low megabyte,** which a VL card decodes unconditionally
   inside its 8 MiB reach. No aperture placement question arises at all.

Enable is CR53[4], with CR53[5] choosing `0xB8000`/32 KiB over `0xA0000`/64 KiB.
`advfunc_cntl` bit 5 (port `4AE8h`) is a second enable path that touches no
extended CRTC register, worth keeping in reserve if the CR53 write proves
awkward under a locked bank.

**The cost:** old MMIO disables the `0xA0000` VGA banked window while it is on.
The driver runs its framebuffer through the linear aperture so the desktop does
not care, but the mini-VDD V86 paths and the banked cross-check in the aperture
probe both use that window today. Stage 2 must establish which of them break.

This is a model of a PCI part. It is a strong indication about the register map
and no evidence at all about VL silicon behaviour.

## Design decisions

- **A third `fill_engine` variant, not a branch inside the ViRGE one.** The
  family already separates per-chip hooks this way
  ([s3_hw16.c:85-95](../../src/chipsets/s3/s3_hw16.c:85),
  [trio_hw16.c:46-48](../../src/chipsets/s3/trio64/trio_hw16.c:46)) and the
  per-object audit asserts on which code lands in which object. Old MMIO gets its
  own hook and its own audited signature.
- **`eng_s3_virge.c` is not modified.** If the stage 2 work needs to touch it,
  the premise of this plan is wrong and that is worth stopping over.
- **Old MMIO is exercised on PCI first.** It works on PCI ViRGE too, so it can be
  proved in 86Box today against a target that already runs. Nothing about stage 2
  needs a 486 or a card that does not exist.
- **`0xA0000`/64 KiB is the default,** `0xB8000`/32 KiB the fallback. The 64 KiB
  form maps offset zero and needs no offset arithmetic.
- **No 3D on VL in scope.** D3D stays a PCI capability. 2D through S3D is the
  goal; the D3D path may follow the same base for free, but it is not claimed.

## Stage 1 - Code 24

Nothing here reaches Enable on VL until the devnode starts. Per the handover,
three attempted fixes did not move it and the stock-driver control that
distinguishes a causal fault from ordinary `DetFunc` state has not been run.
That control is the next step, and it is a prerequisite for stages 3 and 4 but
not for stage 2.

This stage is scoped by the existing handover, not re-specified here.

## Stage 2 - the old-MMIO variant, proved on PCI

Independent of the 486 and independently useful: it gives every ViRGE a fallback
for when the new-MMIO window cannot be mapped.

1. Add `v9x_virge_enable_aperture_oldmmio` beside
   [the existing one](../../src/chipsets/s3/virge/virge_hw16.c:26): the shared S3
   linear-aperture sequence, then CR53[4] set and read back, with the same
   read-back guard discipline the CR53[3] path uses.
2. Add `v9x_virge_fill_engine_oldmmio`: `control_linear_base = 0x000A0000`,
   `mapped_aperture_bytes = 0x00010000`.
3. Select between them. Preference order is a decision to take with measurements
   in hand, not now; the shape is a device-list entry or a runtime probe that
   tries new MMIO and falls back.
4. Prove on the 86Box PCI ViRGE target: fills and blits still land on the engine,
   `SUBSYS_STAT` at `0x8504` still reads, the DirectDraw regression stays green.
5. Establish what breaks with the `0xA0000` window closed. Specifically the
   mini-VDD V86 scratch path and the aperture probe's banked cross-check.
6. Audit signature for the new object, in the pattern of
   [the family merge decision](../decisions/2026-08-16-s3-family-merge.md).

**Exit:** a PCI ViRGE running its full 2D path with the control window at
`0xA0000`, and a written answer on the VGA-window casualties.

## Stage 3 - a VLB ViRGE in 86Box

There is no such device to test against and none to buy. Every ViRGE in 86Box is
`DEVICE_PCI` or AGP ([vid_s3_virge.c:6823](../../build/reference/86box/src/video/vid_s3_virge.c:6823)
onward), while `vid_s3.c` carries a dozen `DEVICE_VLB` Trio and Vision entries
and a `s3->vlb` flag at [vid_s3.c:11017](../../build/reference/86box/src/video/vid_s3.c:11017)
to model from.

The patch: derive a VL device from `s3_virge_dx_pci_device`, flag it
`DEVICE_VLB`, suppress PCI config space, clamp the decode to A2 to A22 so
anything above 8 MiB does not answer, and model SAUP1/SAUP2 as the selector
between the low 8 MiB and the LFB 8 MiB. The old-MMIO path already exists in
that file and needs nothing.

This is a test platform for hardware nobody has. It will not reproduce the
silicon bugs in stage 4, which is the point at which it stops being useful.

**Exit:** stage 2's driver runs 2D through S3D on an emulated VL ViRGE, with
new MMIO demonstrably unreachable on the same device.

## Stage 4 - real hardware, only if 1 to 3 are clean

Requires an MK-765VL built from [Madao's published design](https://github.com/matt1187/765VL),
a donor ViRGE 325, and mkarcher's SAUP2 delay modification from reply 155: a
74ACT74 or 74F74 flip-flop delaying SAUP2 by one VL clock, run in 1 WS late
decode, which forfeits 0WS operation.

What the thread says is waiting there, none of it addressable in a driver:

- VL access patterns that corrupt ViRGE control registers or fire spurious
  busmaster DMA and lock the local bus (reply 155)
- Madao's "ghost write" characterisation and his verdict of a disappointed
  adventure (reply 227)
- VESA mode initialisation clearing video memory through the acceleration
  engine, so the broken path is hit before any driver runs (reply 203)

Two people with the hardware, the datasheets and six years did not get past
this. Stage 4 is a research bet, and stages 1 to 3 are worth doing whether or
not it is ever taken.

## File-level changes

Stage 2:

- `src/chipsets/s3/virge/virge_hw16.c` - the two new hooks
- `include/velocity9x/s3_regs16.h` - CR53[4] and CR53[5] constants
- `src/chipsets/s3/s3_hw16.c` - device-list wiring for the selection
- `scripts/audit-family-binary.ps1` and the family manifest - new object signature
- `docs/decisions/` - the stage 2 result, in the house pattern
- `CHANGELOG.md`

Stage 3 touches only vendored 86Box source and is not shipped.

Not modified in any stage: `src/display32/engines/eng_s3_virge.c`.

## Open items

1. **Selection policy.** Static per device id, or probe new MMIO and fall back.
   Decide with stage 2 measurements.
2. **The VGA window casualties.** Unknown until stage 2 measures them, and the
   answer may push the default to the `0xB8000` form.
3. **`advfunc_cntl` as the enable.** Untried. Reserve.
4. **The Windows 98 DDK.** Thread reply 329 reports it ships full S3 ViRGE
   modesetting and Direct3D source. Not a dependency and not a source this
   driver derives from, but worth knowing what it says about old MMIO before
   stage 4.
5. **Whether stage 4 is ever worth funding.** Revisit after stage 3, with a
   working emulated VL ViRGE in hand and a better idea of what the silicon has
   to get right.
