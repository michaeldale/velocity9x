# Mullins' SDMA0 runs Carrizo's probe byte-exact with CIK's microcode and golden values

Date: 2026-11-08

Status: **measured, one machine, one full run.** M-JJN1ZZ's Radeon R3E
(`1002:9854`), boot 13, desktop at 1024x768x16 under the `vbe` package.
Phase 3 of [the plan](../plans/ati-mullins-gfx7.md), after
[the register probe](2026-11-08-mullins-register-probe.md). Reports in
[`../probe/m-jjn1zz-mullins-sdma-write-probe-2026-11-08/`](../probe/m-jjn1zz-mullins-sdma-write-probe-2026-11-08/).

## The probe

`CKDMA.VXD` + `V9XCKDMA.EXE` (`scripts/build-cik-dma-probe.ps1`), a copy of
the Carrizo SDMA probe ([its record](2026-10-28-carrizo-sdma-write-probe.md))
with what Linux's CIK code does differently (`cik_sdma.c`, `cik.c`):

- Golden values: `SDMA0_CHICKEN_BITS` clear 00010000h, set 00810001h
  (`godavari_golden_registers`, entry 3405h); `SDMA0_CLK_CTRL` clear
  FF000FF0h, set 100h (`kalindi_mgcg_cgcg_init`, entry 3403h). Carrizo's
  `POWER_CNTL` and `IB_CNTL` golden writes have no CIK entry and are left
  out.
- No `SDMA0_GFX_DOORBELL` and no `SDMA0_VERSION`: CIK has neither
  (`oss_2_0_d.h`), so the VxD's allow-lists drop them.
- `RLC_GPM_STAT` read at C400h.
- The microcode is linux-firmware's `amdgpu/mullins_sdma.bin` (SHA-256
  `63860215...662a`, header version 1, ucode version 4Ch, 4,200 bytes at
  offset 100h), copied to the guest as `SDMA.BIN`.

Everything else is Carrizo's sequence unchanged: ring registers at the
same offsets in `gfx_resume`'s order, the ring and buffers in a 64 KiB
scratch window at BAR0 + 240 MiB (GPU `F4_0F000000`), the same packets.

## Result

`/readonly` first: SDMA0 halted (`F32_CNTL` 1), ring off, out of reset;
`SDMA0_CHICKEN_BITS` already read 00800001h and `SDMA0_CLK_CTRL`
FF000100h, so the golden writes change nothing the BIOS left.

The full run, `Result=COMPLETE`:

| | |
|---|---|
| Microcode | 1,050 dwords written |
| Ring test (`WRITE_LINEAR`) | DEADBEEF, PASS |
| Dword copy, 256 dwords | 0 mismatches |
| Byte copy, 37 bytes, odd offsets | 0 mismatches |
| Overlapping copy 4 dwords up | memmove |
| Constant fill 1,024 bytes, dword size | 0 mismatches |
| 16-bit fill, 10 bytes at offset 2 | exact; 0 bytes touched outside |
| Fences | 11111111h and 22222222h landed, waits 5 and 5 |

Read pointers stopped at 94h and E8h, short of the write pointers in the
padding NOPs, as on Carrizo; the writeback read FCh. Afterwards SRBM,
GRBM and SDMA status read as before the run.

## What it settles

SDMA0 on Mullins takes its microcode through its own port, as Linux's
CIK code always does, and runs Carrizo's packets at Carrizo's registers.
So the Carrizo DirectDraw engine (`engines\eng_vi.c`) serves Mullins with
three changes: the microcode image, the two golden values, and no
doorbell write. Those are keyed on `V9X_DD_ENGINE_CAP_VI_MULLINS`, which
only `mullins_hw16.c` stamps.

Not measured: the engine under the driver (Phase 4), page flips through
DCE 8, more than one run, and anything after a mode change.
