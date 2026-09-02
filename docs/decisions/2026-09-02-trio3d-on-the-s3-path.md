# The Trio3D/2X runs the ViRGE path, with hardware Direct3D

Date: 2026-09-02
Branch: `main`
Machine: **A8U4I5**, `10.0.1.172`, physical, S3 Trio3D/2X `5333:8A13`.

## The claim

`8A13` is now an **alias of `virge-dx`**, not of `trio64`. That is the whole
substance of the change: the Trio3D/2X gets the ViRGE's CR53[3] new-MMIO enable
and the ViRGE's engine descriptor, and therefore `Direct3D = hardware-s3d`.

The evidence is 86Box's, and it is emulator evidence rather than silicon:

- That emulator implements the Trio3D/2X **inside its ViRGE driver**,
  `S3_TRIO3D2X` in the same chip enum as the ViRGE parts
  (`build\reference-vid_s3_virge.c:113`).
- Its setup writes `virge_id_high = 0x8a`, `virge_id_low = 0x13` - this exact
  id.
- **The S3D triangle engine in that file carries no `S3_TRIO3D2X` branch at
  all.** Every per-chip conditional in the 3D and streams region is about the
  VirgeVX or the GX2 overlay. In that model the 3D unit is the ViRGE's.

Two modelled differences the alias does **not** account for, and they are the
first suspects if it misbehaves:

- **`fifo_slots_num = 16`** where the ViRGE/DX arm gets 8. A wrong FIFO slot
  reservation is what cost this project two weeks on the ViRGE depth path, with
  every HRESULT reporting success throughout
  ([2026-08-30](2026-08-30-virge-depth-fifo-reservation.md)).
- **an 8 MiB decode mask** where the ViRGE/DX gets 4 MiB.

Its CR36 memory encoding also differs from the ViRGE's, which is known and not
fatal: `v9x_s3_virge_decode_memory_size` returns 0 for a code it does not know,
and `enable16.c` falls back to the VBE `4F00h` total.

## What landed

- `packaging\families\s3\family.psd1`: `8A13` as a `virge-dx` alias.
- `src\chipsets\s3\virge\virge_hw16.c`: `v9x_trio3d2x_device`, same two hooks.
  It lives in the **ViRGE's** object, which is the point - the name misleads.
- `src\chipsets\s3\s3_hw16.c`: extern and scan-order entry.
- `tests\host\test_hw16_modes.c`: the linker stub the device list needs.
- `src\common\backend_registry_table.inc`: regenerated.

`run-checks.ps1` is green, and the packaged INF binds it:

```
"Velocity9x S3 Trio3D/2X"=Velocity9x.Install.virge-dx,PCI\VEN_5333&DEV_8A13
```

## The preflight, twice, and why the first run lied

`V9XSTAGE.EXE` from the s3 package was run before installing anything. It was
run twice, and the difference between the two runs is the lesson:

| Run | Active display driver | Elapsed | Result |
|---|---|---|---|
| First | **Velocity9x VBE** | 240,024 ms, killed | nothing written |
| Second | Microsoft `vga.drv` | **29,882 ms** | **`Stage=query-ok`** |

The first run loaded a second Velocity9x `V9XDISP.DRV` and mini-VDD beside one
already running. **That hang was the test setup and said nothing about the
card.** Reverting the adapter to Standard PCI Graphics Adapter first - which is
the configuration the vbe package's own preflight passed in - made the two runs
comparable, and the s3 preflight then passed in a quarter of the vbe one's time.

Worth keeping because the wrong reading was available and tempting: a 240-second
hang on an unbound card looks exactly like evidence the chip is not compatible.

## It works

Installed by hardware id - Windows offered "Velocity9x S3 Trio3D/2X" under
**Show compatible devices**, matching `PCI\VEN_5333&DEV_8A13` directly rather
than through a manual-select model.

```
Stage=enable-ok
Adapter=S3 Trio3D/2X          VendorId=5333   DeviceId=8A13
ClockDetector=s3-virge-pll-v1 ClockStatus=unavailable
ModeSwitching=live-any-depth
Acceleration=directdraw-fill-blt
GdiAcceleration=gdi-fill-copy-overlap
Direct3D=hardware-s3d         Direct3DMode=hardware
VideoMemoryBytes=4194304      VideoMemoryStatus=valid
Surface=pitch=1280 bpp=16 w=640 h=480
```

**The 2D engine came on with it.** `BltFillMs` went from 12 on the tier-0 VBE
driver to **1** - the same blitter the Trio64 and ViRGE use, driven by port I/O,
working on this chip.

### Hardware Direct3D, on the S3D unit

The probe at 640x480x16, `Build=e7fd47b-dirty`:

```
Result=COMPLETE       D3DHalFound=1        D3DCreateDeviceHr=0x00000000
TexFormatCount=2      D3DTriangleShapeOk=1 D3DContextCycleOk=1
D3DTrianglePixelRaw=31744    D3DDepthFogOk=1
D3DZInitRaw=31744   D3DZRejectRaw=31744   (rejected, colour unchanged)
D3DZAcceptRaw=31    D3DZUpdateRaw=31      (accepted, then rejected)
D3DZNoWriteRaw=992  D3DZMaskRaw=31744     (the write mask suppressed it)
D3DBaseTextureRaw=992        Tex4444Raw=992
```

Read as ZRGB1555 those are red, red, blue, blue, green, red, green, green -
**every rung of both depth ladders correct, both texture formats sampling, and
the triangle a triangle rather than a bounding box.** They are the same values
the emulated ViRGE produces, rung for rung.

## What this settles about the ZRGB1555 defect

`D3DTrianglePixelRaw=31744` is `0x7C00` - red in ZRGB1555 - written into a
surface the probe queried as **RGB565** (`D3DTargetRMask=63488`).

[The issue](../issues/2026-09-01-virge-3d-writes-zrgb1555.md) named two
hypotheses and said 86Box could not distinguish them, because the emulator is
the model. **This is real silicon**, an S3D-family part, doing exactly what the
model does. It does not prove the ViRGE behaves identically - this is a Trio3D -
but it removes the "86Box's model is incomplete" explanation as the likely one:
an emulator bug would not be reproduced by a physical chip.

## What differs from the ViRGE, measured

Three things, and they are the value of having run it:

1. **`ClockStatus=unavailable`.** The `s3-virge-pll-v1` detector does not decode
   this chip's PLL. Cosmetic - the clock is reported, not used - but it is a
   real per-chip difference and the alias inherits a detector that does not fit.
2. **Mip selection and trilinear do not work.** `D3DMipmapLevelRaw=0` (black,
   nothing sampled) and `D3DTrilinearRaw=16400` where the ViRGE returns a
   green/blue blend. Both pass on the emulated ViRGE. **The caps advertise them
   anyway**, because the alias inherits the ViRGE's `describe_caps` -
   `TriFilter=63` includes all four mip filters. That is the
   advertise-then-not-deliver pattern this driver keeps a rule against, now
   present on this chip through inheritance rather than through a new claim.
3. **The FIFO difference did not bite.** 86Box models this part with 16 command
   FIFO slots against the ViRGE/DX arm's 8, and that was named here as first
   suspect. Depth is where a wrong reservation showed up on the ViRGE, and depth
   passes every rung. Not disproven - `v9x_wait_fifo(15)` may simply be
   conservative enough for both - but it did not manifest.

The clipper defect reproduces again: `D3DEdgeRightRaw=0`, `D3DEdgeBottomRaw=0`
against a drawn centre and top-left. Fourth engine, second machine.

## What this does not establish

- **Nothing about the ViRGE.** A Trio3D writing 1555 is evidence about S3D
  parts, not proof about the 86C375.
- **Nothing about speed** beyond `BltFillMs` 12 to 1. No benchmark was run.
- **Nothing about the 8 MiB decode mask**, the other modelled difference. This
  board has 4 MiB and never approaches it.
- **The mip caps are now knowingly wrong on this chip** and were not corrected
  here. Fixing that means either implementing mip selection for it or giving the
  alias its own `describe_caps`, and both are changes with their own evidence.
