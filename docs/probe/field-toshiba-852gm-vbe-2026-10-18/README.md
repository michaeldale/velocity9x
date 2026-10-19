# Field report: Toshiba laptop, Intel 852GM/855GM, 0.19.0 on VBE-generic

A tester's `C:\V9XDIAG` folder, received 2026-10-19 (downloaded as
`V9XDIAG (4)`). The eight INI files beside this note are copied unmodified.
No accompanying symptom description came with them; this note says what the
files show, not what the tester saw.

Collected 2026-10-18: `V9XSURV /rom` from DOS 7.10 at 00:43, then Windows,
then `V9XTRACE` at 00:52:01, 104 s after boot.

## The machine

| | |
|---|---|
| Graphics | PCI 8086:3582 rev 01, subsystem 1179:0002 (Toshiba), bus 0 dev 2 fn 0 + fn 1 |
| Host bridge | 8086:3580 rev 01 (shared by 852GM/852GME/855GM/855GME) |
| South bridge | ICH4-M (8086:24CC); NIC 8086:103D; CardBus 1179:0617 |
| CPU | GenuineIntel family 15 model 2 stepping 9 (Northwood-class Pentium 4) |
| RAM | 512 MiB fitted; Windows sees 506 656 KB |
| OS | Windows 98 SE (4.10.2222 A), DirectX 4.06.03.0518 (6.1a) |
| VBIOS | "TOSHIBA Video BIOS V1.00", Intel build 2894, 02/18/2003; VBE 3.0, 253 x 64 KiB = 16 580 608 bytes |
| Panel | EDID unavailable (4F15 returned 014F); 4F11 FlatPanelInfo=0000 |

The CPU suggests the 852GM/852GME rather than the 855GM: the 855 parts pair
with Pentium M, the 852 parts with mobile Pentium 4. Inferred from the CPU
signature, not read from the chip; the PCI ID does not separate them.

Either way it is Gen2 graphics. The `intel` family claims only 945GSE
(0x27AE), 915GM (0x2592) and HD 4600 (0x0412), so the fall-through to
`vbe` (VBE-generic) is the designed outcome, not a misdetection.

## What works

- Boot reached `Stage=enable-ok`, `DriverInitResult=ok`; DirectDraw shared
  state reached `shared-ready` (`V9XDDH.INI`).
- Desktop at 1024x768x32 through the LFB at 0xD8000000, pitch 4096.
  `ColourLayout=565-auto`, `is555=0`, matching the BIOS's 5:6:5 masks.
- Mode sync `Status=ok`: six baseline modes kept, 640/800/1024 x32 added
  (`V9XSYNC.INI`).
- Mode filtering is right. The BIOS lists 21 modes; the 12 with attribute
  0x009A (bit 0 clear: not supported by the current hardware configuration -
  1024x600, 1280x1024, 1600x1200, 1920x1440) were dropped, the nine 0x009B
  modes published, and 640x400x8 hidden as `scan-contradicted`. 1024x768 is
  the largest supported mode, so the panel is probably 1024x768 (inferred).

## Findings

### 1. Function 1 of the graphics device has a second display driver bound

`V9XSNAP.INI [Installed]`/`[Card]`:

- `Display\0001` - Velocity9x VBE-generic on fn 0 (correct).
- `Display\0002` - "VBE Miniport - Standard PCI Graphics Adapter"
  (`vbemp.drv` + `vbe.vxd`, `ANAPAC~1.INF`, dated 10-18-2026) bound to
  **fn 1** (class 038000), `Present=1 Problem=0`.
- `Display\0000` - stale `vga.drv` entry with no card behind it.

Fn 1 on these chips is the secondary-head function with no resources of its
own (`Command=0000`, BAR0 unassigned). With a display driver started on it,
Windows 98 treats the laptop as having two display adapters, and a second
mini-VDD (`vbe.vxd`) is loaded beside `v9xmini.vxd`, both of which talk to
the same VBE BIOS. Nothing in these files shows it causing a fault; it is a
configuration the driver was not tested with. The netbook has the same
two-function layout and we install on fn 0 and leave fn 1 alone.

Ask the tester to remove or disable the fn 1 device (Device Manager) and the
VBEMP package before any further report, so results are not confounded.

### 2. The trace snapshot carries no driver activity

`V9XSNAP.INI` has `HalBuild=` empty, `DriverInitDone=0`, `ModeWidth=0`,
`EnableCount=0` and every counter zero, while `V9XBOOT.INI` records a
successful enable. `V9XHW.INI` grew from 532 to 621 bytes after the snapshot
(`VSync`, `Direct3DDdi=A`, `EngineStamp` appended), so the HAL loaded later.
Most likely the snapshot was taken before anything had used DirectDraw, and
the counters it reads live in the HAL's shared block. That reading is not
verified against the trace code.

Consequence: this report shows a good boot and nothing about DirectDraw,
Direct3D or OpenGL. If the tester has a symptom, a second `V9XTRACE` dump
taken right after reproducing it is needed.

### 3. A write-combining range is available and would be placed

`Mtrr=cpu=000f cap=00000508 def=00000c00 n=8 r=0 s=4 b=d8000000 z=00800000`:
the inspect-only policy (`src/common/mtrr.c`,
`docs/decisions/2026-08-28-mtrr-stage-a-inspect-only.md`) found MTRRs
enabled, default type UC, WC permitted, slots 4-7 free, and planned an
8 MiB WC range at the LFB in slot 4 (reason 0, `V9X_MTRR_OK`). Nothing was
written, as designed.

This is a second machine with real MTRRs beside A8U4I5, and the first P4
one. The window is 8 MiB rather than 16 because the BIOS reports
16 580 608 bytes, 192 KiB short of 16 MiB, so the largest aligned
power-of-two window is 8 MiB. The 3 MiB visible surface fits inside it.

Existing pairs, for the record: 512 MiB WB from 0 (Mtrr3), 16 MiB UC at
0x1F000000 (stolen graphics memory: E820 RAM stops at 0x1EF40000, ACPI and
reserved run to 0x1F000000), 1 MiB UC at 0xFFF00000, and 128 KiB WB at
0xFEDA0000 (an E820-reserved BIOS region).

### 4. Software Direct3D, system-memory surfaces refused

`Direct3D=not-advertised`, `Direct3DMode=software`, `D3DSoftSysMem=refused`,
`Acceleration=none`, `EngineCaps=00000000`. Expected for VBE-generic.
`VddReserve=vdd=3145728`: 3 MiB of the 16 MiB is the visible surface; the
rest is offscreen for DirectDraw.

### 5. DirectX 6.1a

The runtime is 4.06.03.0518. The other test machines run DX7 or later. If a
D3D or DDraw symptom is reported on this laptop, the old runtime is a
variable to rule out before blaming the driver.

## Not a finding

- `V9XESC.INI`: QUERYESCSUPPORT for 0x0C01 (DCICOMMAND), 0x0027 and 0x0008,
  plus one 0x0027 call. Ordinary shell traffic.
- `Settings Present=0`: no Velocity9x user settings saved.
- `V9XSETP.DLL` and `V9XUPD.EXE` report `build=unknown`; they always have.
- The VBIOS names Chrontel CH7009 encoders (TV/DVI). That is BIOS support
  for them, not evidence one is fitted.

## Possible follow-ups

- Gen2 (830/845/852/855/865) has no family. Scanout, GTT and blitter
  programming differ from Gen3 enough that this is new work, not a device ID
  added to `intel`. Nothing here argues for or against starting it.
- `docs/specifications/dos-vbe-conformance.md` could take this BIOS (Intel
  build 2894, VBE 3.0, no PM interface, 4F15 unsupported) as a data point.
