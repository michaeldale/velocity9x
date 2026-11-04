# The HD 7660D's first HAL Direct3D run hung the machine: a CF_ALU count read as end of program

Date: 2026-11-03 (fixed and measured 2026-11-04)
Status: **closed.** M-D7TFW4's HD 7660D (`1002:9901`, Trinity). The
Direct3D build from `d3aee6b7` hung the machine on its first V9XD32 run
(boot 19). The relocator `v9x_cm_shader_from_eg` read bit 21 of a CF_ALU
word as END_OF_PROGRAM; it is the count's bit 3. With that fixed, V9XD32
passes at 32 and 16 bpp on boot 22, and the Quake 3 demo runs on
the hardware ICD on boot 23.
Plan: [ati-trinity-hd7660d.md](../plans/ati-trinity-hd7660d.md).
Capture: [`docs/probe/m-d7tfw4-trinity-d3d-2026-11-03/`](../probe/m-d7tfw4-trinity-d3d-2026-11-03/).

## What was on the machine (boot 19)

The ati set (DRV, VxD, HAL, ICD) of `d3aee6b7`'s tree, swapped in by
WININIT renames after every staged hash matched. `V9XHW.INI`:
`Direct3D=hardware-evergreen-cp`, `EngineCaps=04000017`,
`Direct3DMode=hardware`. Over `7517eac2` (V9XCM3D's six byte-exact
copies) it adds the Evergreen-to-Cayman shader relocator, the Aruba
microcode in the HAL, the Cayman CP bring-up in `eg_cp.c` and the Cayman
state in `d3d_eg.c`.

## What happened

V9XD32 ran at 32 bpp. About a minute later the relay lost the agent;
the portal last saw it at 01:05:57Z. The owner power-cycled it; boot 20
came up at about 14:15Z.

The files written before the hang (`boot19-V9XD32.TXT`,
`boot19-V9XEGCP.TXT`) show:

- **V9XD32 ran to its end.** Every stage through T17 was reached, but
  every pixel check read 0 (T1 to T10, T16 `FAIL`, T13 `FAIL`). The
  machine went down after the run, not in it.
- **The HAL gave the CP up on the first draw.** `V9XEGCP.TXT` is the
  quarantine record (`eg_cp.c`, written once): `CP_RB_RPTR=000002F0`, 64
  dwords into the third submission, the second draw batch; the first
  draw batch (`0130`-`02B0`) had been fetched. `GRBM_STATUS=F5703828`:
  SPI, SH and SX busy, VGT and PA idle, the CB and DB clean bits set. A
  shader had been started and had not finished; the CP's next batch
  waited behind it.

What took the machine down after the quarantine was not measured. The
quarantine soft-resets the CP blocks with a shader still running; that
is the likely candidate and is not pursued, because the stuck shader
is gone.

## The cause

A host dump of every relocated D3D shader against its Evergreen source
showed the lit pixel shaders cut short. The specular lit PS is two ALU
clauses (16 slots of interpolation, then 3 slots of add) and a pixel
export. Relocated, it was one ALU clause of **8** slots and `CF_END`: no
second clause and no export.

`v9x_cm_shader_from_eg` tested `END_OF_PROGRAM` (bit 21) on every CF
word. A CF_ALU word has no such bit: its `COUNT` is bits 24:18
(`eg_sq.h`), so bit 21 is the count's bit 3, set for every clause of 9
to 16 slots. The 16-slot clause's word `A03C0000` read as the end of the
program; the relocator stopped there, then cleared "EOP", which halved
the count to `A01C0000`. A pixel shader with no export never completes,
which is the SPI/SH-busy state the record shows.

The host test did not catch it: `check_relocated` asserted bit 21 clear
on ALU words too, and never compared the CF entry count with the
source's. The Linux blit pair and the unlit D3D shaders have no ALU
clause over 8 slots, so they relocated correctly. V9XCM3D's draws, which
used only the blit pair, could not have shown it.

## The fix

- `cm_shader.c`: `END_OF_PROGRAM` is read and cleared only on non-ALU CF
  words.
- `test_cm_shader.c`: `check_relocated` counts the Evergreen CF entries
  (EOP on non-ALU words only) and requires the same number before
  `CF_END`, each CF_ALU word unchanged. It failed 8 checks on the three
  lit PSs and the keyed PS before the fix, and passes after it.

`run-checks.ps1` green.

## Measured on boot 22

Before the fix went on, boot 21 ran the `58a3b6aa` set (DirectDraw only,
`Direct3D=not-advertised`, HAL sha256 `40c8d44a...`), so a game could not
restart the CP.

Boot 22: the fixed ati set, HAL sha256 `ead9d8f0...` read back from
`C:\WINDOWS\SYSTEM`. `Direct3DMode=hardware`.

| Run | Result | Notes |
|---|---|---|
| V9XD32, 32 bpp | `PASS` in 6.4 s | T1 `0013579B`, T3 7, T5 `0000FF00`, T6/T8/T9 `F`, T7 `00807F00`, T10 `3F`, T11 list `3F`, T4 `002468AC`, T13, T14, T15, T16 `PASS` |
| V9XD32, `/bpp16` | `PASS` | T13, T14, T15, T16 `PASS` |

T12 (one texture stage), T18 (no T&L device) and T19 (no pixel shaders)
are skipped by design, as on the HD 7470. `V9XEGCP.TXT` is unchanged
from boot 19's: no quarantine in either run.

## Quake 3 on boot 23

The machine had restarted to boot 23 (not by us) with the same HAL
(`ead9d8f0...` read back). Its files report `d3aee6b7-dirty`: the fixed
build was made from the working tree before `7bc6cae6` was committed.

The owner's `q3config.cfg` had changed since the HD 7470 run: `r_mode 8`
(1280x1024), 32-bit colour, 24-bit Z requested, and
`r_lastValidRenderer "Velocity9x Radeon HD 7660D"`, so Quake 3 had
already run on this build. The run used those settings and the
config was left as found; `C:\V9XDIAG\Q3CFG.BAK` is older (640x480,
"Velocity9x Software") and was not restored over it.

| | Result |
|---|---|
| `+timedemo 1 +demo demo001`, 1280x1024 fullscreen | **1346 frames, 40.3 s: 33.4 fps**; `GL_RENDERER` "Velocity9x Radeon HD 7660D"; PIXELFORMAT 32-bit colour, 16-bit Z (the 24-bit Z request failed `GLW_ChoosePFD`, 16-bit was taken) |
| ICD paths (`boot23-V9XGL.LOG`) | hw 215,731 batches / 5,532,281 triangles; cpu 0; hw-refused 0; 355 textures, 6,286 KB, no failures or evictions |
| V9XTRACE after (`boot23-V9XSNA5.INI`) | `Ok=1`, `BuildsAgree=1`, `EngineFlags=9` |
| `V9XEGCP.TXT` | unchanged from boot 19's: no quarantine |

The 33.4 fps is recorded, not compared. The texture count and size match
the HD 7470's run of the same demo; the batch count differs with the
resolution.

## Not measured

- What the monitor showed during Quake 3; the flip counters in
  V9XTRACE read 0 for this run and were not looked into.
- What hung the machine after the quarantine.
