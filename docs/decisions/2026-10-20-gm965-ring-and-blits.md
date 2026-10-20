# GM965: the Gen3 ring and blits run on Gen4, once the GTT is found in BAR0

Date: 2026-10-20
Status: **measured once, on one machine, at 640x480x8.** Step 4 of the
Order in `docs\plans\intel-gen4-gen5-bringup.md`, as far as identity, GTT,
ring and blits. Flips and Direct3D are withheld.

Machine: TOMMOHP, remote help `M-KYHT4Q`
(`2026-10-20-gm965-tommohp-tier0-and-mmio.md`). Captures:
`docs\probe\tommohp-gm965-igma-2026-10-20\`.

## What changed

- `intel-gma` claims `8086:2A02` as `gma-x3100-gm965`, with
  `v9x_gm965_fill_engine` (`gma950_hw16.c`). That is the 945's descriptor,
  then D3D, FLIP and FLIP_RING cleared; ASYNC_SUBMIT is kept.
  `Direct3D = 'not-advertised'`.
- `V9xPciReadIntelGttConfig` (`runtime.asm`) refused the GM965 outright. It
  wanted BAR2 to be a 32-bit BAR and read 1Ch as BAR3, which on Gen4 is
  BAR2's high dword. For `2A02` it now requires BAR0 and BAR2 to be 64-bit
  BARs below 4 GiB, and hands the GTT window over as BAR0 + 512 KiB
  (datasheet 316273 21.1.10). The rest of the driver maps 256 KiB of that
  window, which is the 65,536 entries covering the 256 MiB aperture. The
  other 65,536 entries of the 512 KiB table go unread.
- The mini-VDD's event journal (`loader.asm`), which also runs on DPMS
  calls at ring 0, read the Gen3 fence registers at 2000h-201Ch. Neither
  i915 nor the 965 PRM names that range on Gen4. When the GTT it is handed
  lies at BAR0 + 512 KiB, it reads the Gen4 fences at 3000h-301Ch
  instead.

## Boot 6: identity and read-only captures (IntelRuntime3D=0)

Installed by Have Disk on function 0 over the `vbe` driver. Setup's
`WININIT.INI` held three `dest=src` renames and no `NUL=`. Restarted with
the agent's `reset`, at Michael's request; plain `reboot` came back once,
at boot 5. After each reset Windows stopped at "Enter Network Password",
which boot 5 never showed. It was cancelled with Escape; no credentials
were entered.

- `V9XHW.INI`: "Intel GMA X3100 (GM965)", `DeviceId=2A02`,
  `Direct3D=not-advertised`. `EngineStatusEnable=map-refused` is the status
  as of the descriptor's first call, which comes before the captures map the
  windows.
- `INTELGTT.TXT`: `Bar3=E4280000`, which is BAR0 + 512 KiB. Two hashes
  agree. Runs: entries 0-0x77E linear from `7F800000`; 0x77F-0xFFFE all
  `7FF7E000`; 0xFFFF maps `7FF7F000`. All uncached. The ring reserve at
  `0x670000` is backed (`7FE70000`). `Result=REVIEW` because the inventory
  still expects the GTT storage at stolen top less 256 KiB (`7FFC0000`);
  PGTBL_CTL says `7FF80000`, the 512 KiB table.
- `INTELEVT.TXT`: one `boot-enable` event captured, fences 0. The ring was
  untouched.

## Boot 7: the ring and the blits (IntelRuntime3D=1)

The first command this driver has sent to a Gen4 part.

- The DirectDraw session stamp: `EngineStamp=type=00000003 caps=00000400
  ring=E5C77000`. That is the Gen3 engine type, ASYNC_SUBMIT only, and a
  ring mapped.
- `V9XDDF.EXE` at 640x480x8 passed all seven stages: create, cooperative
  level, an off-screen surface, GetBltStatus, a colour fill read back, then
  a rectangle fill and a copy read back against a CPU reference. 0
  mismatches.
- Afterwards, read-only (`V9XG4MM.EXE`, `boot7-V9XG4MM-after-ddf.INI`):

| Register | Value | Reading |
|---|---|---|
| RING_START (2038h) | `00670000` | the reserve |
| RING_CTL (203Ch) | `0000F001` | 64 KiB, enabled |
| RING_HEAD / RING_TAIL | `000000E8` / `000000E8` | 232 bytes submitted and consumed |
| ACTHD (2074h) | `000000E8` | equals HEAD |
| IPEIR / IPEHR / EIR / ESR | `0` | no error |

**ACTHD at 2074h tracks the head on Gen4,** as `intel_gma.h` noted from
i915 and the 965 PRM lists. Gen3's 20C8h was not read.

The pixels prove the blits landed. TAIL and ACTHD prove they went through
the ring rather than the CPU fallback: the fallback submits nothing.

V9XTRACE run afterwards reported `EngineType=0` and `LiveRegError=not-gen3`.
It ran in its own process with no DirectDraw session, so its HAL had no
stamped engine. Read nothing into it.

## What this does not answer

- 16 and 32 bpp. The desktop stayed at 640x480x8, the mode the driver came
  up in.
- Sustained load: one fill, one fill and one copy.
- Flips. FLIP is withheld until a DSPSURF path exists. The HAL's flip
  writes 70184h, which is DSPxLINOFF on Gen4, and FW_BLC.
- Why every reset now stops at the network logon while the one agent
  reboot did not.
