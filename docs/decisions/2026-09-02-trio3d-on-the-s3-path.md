# Binding the Trio3D/2X to the ViRGE path, and a preflight that did not answer

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

## What did not happen: the install

**The s3 package was not installed.** Its own preflight was run first and did
not return an answer:

| | `V9XSTAGE.EXE` from the vbe package | from the s3 package |
|---|---|---|
| Elapsed | 111,092 ms | **240,024 ms, killed at the timeout** |
| Exit | 0 | 0xFFFFFFFD |
| `C:\V9XDIAG\V9XBOOT.INI` | `Stage=query-ok` | **not written** |

The machine was healthy throughout and after: desktop up, agent answering,
`V9XDISP.DRV` and `V9XMINI.VXD` still carrying the VBE install's timestamp, so
nothing was copied or changed.

## Why that result does not convict the card

**The two runs were not the same experiment**, and the difference is in the
setup rather than in the chip:

- The **vbe** preflight ran while Microsoft's `vga.drv` was the active display
  driver. No Velocity9x driver was loaded.
- The **s3** preflight ran while the **Velocity9x VBE driver was the active
  display driver**. It loads a second Velocity9x `V9XDISP.DRV` and mini-VDD
  beside one already running.

A hang in that configuration may be about loading two Velocity9x display
drivers at once and say nothing about the Trio3D. Concluding "the s3 path hangs
on this card" from it would be exactly the kind of confident statement about
silicon this project keeps a rule against.

There is a second unexplained observation from the same window, recorded rather
than theorised about: the machine's boot counter moved from 3 to 4 and its
resolution changed from 640x480 to 1024x768 without either being asked for. The
sequence puts that reboot *before* the preflight ran, so it is probably not the
preflight's doing, but it is not accounted for.

## What would settle it

Two clean experiments, cheapest first:

1. **An 86Box guest with `gfxcard = s3_trio3d2x`.** The ROM
   (`roms/video/s3virge/TRIO3D2X_8mbsdr.VBI`) is present on this host and the
   device exists. It exercises the whole s3 path, including the FIFO depth
   difference, and risks nothing. It cannot answer whether real silicon agrees -
   the emulator is the model - but it will find driver-side faults for free.
2. **On A8U4I5, from the stock VGA driver.** Revert the display adapter to
   Standard PCI Graphics Adapter, reboot, then run the s3 preflight with no
   Velocity9x driver loaded. That is the same experiment the vbe package
   passed, and it is what makes the two comparable.

Only after one of those should the s3 driver be installed on this machine.
Recovery from a bad display install there needs someone at the keyboard with
F8, and the card currently has a working driver.
