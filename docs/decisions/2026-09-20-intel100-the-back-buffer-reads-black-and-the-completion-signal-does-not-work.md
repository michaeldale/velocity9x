# intel100: the back buffer reads black, and the completion signal does not work

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
at all". That reading is **not safe here**, and the reason is in the same
capture:

```
HwsPgaBefore=0x1FFFF000   HwsPgaWritten=0x7FEC0000   HwsPgaAfter=0x1FFFF000
HwsValueLast=0x00000000   BreadcrumbSubmits=178615
BreadcrumbLagPollsTotal=0 BreadcrumbOutstanding=0    RenderDrainWaits=0
```

The hardware status page address does not stick: the driver writes
`0x7FEC0000` and the register reads back the BIOS's `0x1FFFF000`. So the
engine's completion breadcrumbs are written into a page this driver does not
own, `HwsValueLast` stays zero through 178,615 submits, and every wait built
on it returns immediately - `RenderDrainWaits=0`, and not one lag poll.

**The driver cannot tell whether the engine has executed anything.** The flip
drains before presenting and that drain is a no-op, so a black buffer at flip
time has two live explanations and this capture separates neither:

- nothing is being rendered into it, or
- the read happens before the engine has drawn, and the mechanism that should
  have made us wait is broken.

The second is not a technicality. If the drain is a no-op then the FLIP is not
waiting either, which is a defect in its own right whatever the sampler sees.

## What it does settle

An overwrite between the draw and the scanout becomes hard to hold: the
buffer is black in its own memory, before anything downstream touches it.
That was one of the three explanations standing after intel99.

And it found the HWS_PGA failure, which no capture had been read against
before.

## Next, in this order

1. **Establish whether HWS_PGA can be programmed on this part at all**, and
   what the driver must do to own a status page. Every completion wait
   depends on it, and one that always returns immediately is worse than none
   because it looks like success.
2. **Sample the retiring buffer as well as the incoming one.** The retiring
   buffer has been scanned out, so whatever it holds was finished. Content
   there against black here means the sample is simply early and the
   renderer is fine.
3. Only then ask about fragment rejection or transforms.

No rendering change should follow from this capture. The instrument that
would say whether such a change helped is the one that is broken.
