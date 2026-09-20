# intel100: the back buffer reads black

> **Corrected 2026-09-20, same day.** This was filed as "and the completion
> signal does not work", on the strength of `HwsPgaWritten` differing from
> `HwsPgaAfter`. **That was wrong.** The driver deliberately does not write
> HWS_PGA - `d3d_i9xx.c` says so in the comment beside the reads: the
> breadcrumb goes through the GTT like every other GPU write and the page's
> physical address is only recorded. `hws_pga_written` is the address the
> driver would use, not one it wrote, so `before == after` is correct and
> expected.
>
> The claim was read off a field name. `HwsSelfTest=1` in the same capture
> is positive evidence the engine executed a blit into the status page, so
> the zero waits may be genuine rather than broken, and the conclusion that
> no measurement of this path can be trusted is withdrawn with the rest.
>
> What stands: the back buffer sampled black, and that has two explanations
> this capture does not separate.

2026-09-20, MICHAEL-NETBOOK (945GSE), build `8f34bc8-dirty`,
`D3dPidDistinct=1`, `CountDriverInit=1`. Attached:
`2026-09-20-intel100-V9XSNA1.txt` and `2026-09-20-intel100-V9XFRAME.ppm`,
the first frame image this project has ever taken off a guest.

## The readback

Four identified records, all from session 1:

| record | seq | offset | sampled | drawn | box |
|---|---|---|---|---|---|
| 0 | 1 | `0x00120000` | 9,216 | **0** | - |
| 1 | 98 | `0x00240000` | 9,216 | **17** | 776,432 - 864,456 |
| 2 | 196 | `0x00120000` | 9,216 | **0** | - |
| 3 | 293 | `0x00240000` | 9,216 | **0** | - |

All 1024x576, pitch 2048, two bytes a pixel, step 8 - the presented
surface's own geometry - and `D3dTargetOffset` is `0x00120000`, so these are
the buffers Direct3D renders into.

The image is blunter: **9,216 pixels, one distinct colour, pure black.**

Against 178,615 draws submitted in the same run.

## What that does NOT establish

The pre-registered reading of a near-zero count was "nothing reaching memory
at all". That reading is still **not safe**, but not for the reason first
given here. The relevant registers are:

```
HwsPgaBefore=0x1FFFF000   HwsPgaWritten=0x7FEC0000   HwsPgaAfter=0x1FFFF000
HwsValueLast=0x00000000   BreadcrumbSubmits=178615
BreadcrumbLagPollsTotal=0 BreadcrumbOutstanding=0    RenderDrainWaits=0
```

`HwsPgaWritten` and `HwsPgaAfter` differing is NOT a failed register write -
the driver does not write that register at all, by design. What remains is
that 178,615 breadcrumbs were submitted and every wait on them returned
without polling once. That is consistent with work completing faster than the
wait, and it is equally consistent with a comparison that always matches.
This capture does not tell them apart.

So a black buffer at flip time still has two live explanations:

- nothing is being rendered into it, or
- the read happens before the engine has drawn.

`HwsSelfTest=1` with zero polls says the engine DID execute the round-trip
blit into the status page at least once, which is the only direct evidence
here that anything executes at all.

## What it does settle

An overwrite between the draw and the scanout becomes hard to hold: the
buffer is black in its own memory, before anything downstream touches it.
That was one of the three explanations standing after intel99.

## Next, in this order

1. **Read the same buffer twice** - added in the commit after this
   correction. A buffer flipped to is sampled again on the very next flip,
   at which point it is the one being scanned out and the application is
   drawing into the other, so nothing has reused it. A higher count the
   second time means the engine wrote after the driver had already sampled:
   the first read was early. Equal counts are consistent with the buffer
   holding what was read and prove nothing on their own.

   A first attempt compared the incoming buffer against the RETIRING one.
   That was doubly wrong: the branch tested a value the caller had already
   overwritten so it never ran at all, and comparing two different frames
   could not have isolated timing anyway - the incoming frame could have
   been cleared, drawn wrongly, or meant to be blank.
2. Only then ask about fragment rejection or transforms.

No rendering change should follow from this capture, because what it
measures is not yet distinguishable from when it measures it.
