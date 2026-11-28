# Gen3: GDI copies and the hardware cursor stay off until a mode change

Date: 2026-11-24. Status: fixed before the GDI copies and the hardware
cursor were offered upstream, confirmed on the netbook the same day
(below); no release had either. Machine: the GMA 950 netbook (945GSE,
Atom), Windows 98 SE. Builds: the author's Gen3-only branch, from the
first build with the copies and the cursor.

## What was seen

`C:\V9XDIAG\V9XHW.INI` from a normal boot, copied into the log of
V9XTXOFF.EXE, a one-off tool that set `GdiAccelText=0`
([evidence](2026-11-24-gen3-gdi-cursor-off-until-mode-change-v9xtxoff.txt)):

```
EngineStatusEnable=map-refused
GdiAcceleration=none
EngineCaps=00000000
HwCursor=no-engine
```

The three V9XGDIB.EXE reports of the same day
(`docs/decisions/2026-11-24-gen3-gdi-netbook-gpu-*.txt`) agree:

- GPU-NOTEXT and GPU-OFF: `enabled=0`, every BitBlt of the boot declined
  as `disabled`. GPU-NOTEXT was meant to have copies on
  (`GdiAccelText=0` alone); the decision record took it for a second
  all-off run and could not say why.
- GPU-ALL: `enabled=26`, but 1,863 BitBlts declined as `disabled` before
  the path came on partway through that boot.

## Cause

`Enable` ran `v9x_gdi_accel_configure` and the cursor's configure before
`v9x_publish_hardware_diagnostics`. On Gen3 the engine descriptor refuses
until the mini-VDD has mapped BAR0 and BAR3, which its capture paths do
inside that publish (`intel_hw16.c`, the comment at
`EngineStatusEnable`). So both configures find no engine at boot, and
nothing calls them again until the next `Enable` - a mode change. Direct3D
had the same problem, met in September (`dd16.c`, the re-resolve in its
driver-object path); the GDI and cursor configures copied the Enable-time
placement without it.

What turned the copies on partway through the GPU-ALL boot is not known;
a mode change is the only path that would.

## Fix

In the intel-gma family, `Enable` runs `v9x_gdi_accel_configure`, which
now configures the cursor too, again after the publish and the hardware
event, and writes `GdiAcceleration=`, `EngineCaps=` and `HwCursor=` again
(`ddi.c`). The configure was already safe to repeat: a mode change runs it
at every `Enable`. Other families are unchanged.

## On the netbook with the fix

A plain boot, 640x480 at 8 bpp ([V9XHW.INI](2026-11-24-gen3-gdi-cursor-off-until-mode-change-fixed-v9xhw.ini),
[V9XGDIB](2026-11-24-gen3-gdi-cursor-off-until-mode-change-fixed-gdib.txt)).
The branch named the state `gen3-copy`; `gdi_accel.c`'s upstream naming
makes it `gdi-copy-overlap`:

- `GdiAcceleration=gen3-copy`, `EngineCaps=00800614`,
  `HwCursor=on base=3FFAC000 pipes=2` - the cursor on pipe B, the panel's,
  at a physical base below 4 GiB.
- "stats before": `enabled=6` (copy and overlap, no text), `disabled 0`
  over the boot's first 4,431 BitBlts, 17 of them already on the blitter.
- All checks PASS; text declined throughout (`reasons=2`, not enabled).
- At 8 bpp a 256x256 copy took 104 us, a 16-line scroll of 512x384
  467 us. There is no CPU run at this depth to compare with.

What the cursor looked like and the DOS-box round trip were not reported
with these files.
