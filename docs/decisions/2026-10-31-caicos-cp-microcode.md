# The HD 7470's command processor runs AMD's Caicos microcode

Date: 2026-10-31
Status: **Phase 4 ring test passes, three runs of three**, on M-D7TFW4's
HD 7470 (`1002:6778`, Caicos), boot 5, with VRAM at GPU `F_00000000`. The
microcode is Caicos's own, not Cedar's; shipping it is a decision for
Michael. Plan:
[ati-evergreen-juniper-caicos.md](../plans/ati-evergreen-juniper-caicos.md).
Captures: [`docs/probe/m-d7tfw4-caicos-cp-2026-10-31/`](../probe/m-d7tfw4-caicos-cp-2026-10-31/).

## The microcode differs from Cedar's

Fetched from linux-firmware (`gitlab.com/kernel-firmware`, `main`) into
`build\external\evergreen\firmware\` beside a fresh copy of Cedar's,
whose SHA-256s match the ones `gen-evergreen-microcode.ps1` records:

| File | Bytes | SHA-256 |
|---|---|---|
| `CAICOS_pfp.bin` | 4480 (1120 dwords) | `210b9c42cf7866fcacf9239016142e799f52901ef3b001f7081fc92ff2c43401` |
| `CAICOS_me.bin` | 5504 (1376 dwords) | `37fa5fb7cdb13df94c9a64d7ba752b63119f063f64326b67ec54656cd184363b` |

The sizes are Evergreen's (`radeon_ucode.h`; `ni.c` requests BTC's PFP
and ME at those sizes). They are not Cedar's images: 883 of 1120 PFP
dwords and 1177 of 1376 ME dwords differ. Linux loads them per chip
(`ni.c` `CAICOS_pfp.bin`, `CAICOS_me.bin`), so the driver will need
Caicos's arrays beside Cedar's: 9,984 bytes more in `V9XHAL.DLL`. No
board needs `CAICOS_mc.bin` here (DDR3,
[register record](2026-10-31-caicos-register-probe.md)).

Whether Cedar's microcode would also run on Caicos was not tried. A ring
test passing would not make it right for every packet.

## The probe

V9XEGCP and its EGCP.VXD (shared with V9XEG3D) accepted only `68F9` and
refused VRAM above 4 GiB. Now the VxD also accepts `6778`. The EXE writes
the window's 40-bit address as `evergreen_cp_resume` does: `CP_RB_BASE`
and `SCRATCH_ADDR` take the address shifted right by 8, with bits 39:32
in their top byte, and `CP_RB_RPTR_ADDR_HI` takes bits 39:32. The
microcode is read from `PFP.BIN` and `ME.BIN` in the working directory,
here Caicos's images under those names (`C:\V9XDIAG\EGCP`). The VxD's
write allow-list checks only `GRBM_SOFT_RESET`'s value, so the high
bytes pass unchanged.

## Runs

The ati set from `899658c5` is installed. The HAL never starts the CP on
this card (`eg_cp.c` refuses VRAM above 4 GiB), so the CP was halted as
POST left it (`CP_ME_CNTL` `14000000`) before every run. The window is GPU
`F_0F100000`, BAR0 + 241 MiB, past the DMA ring's 64 KiB at 240 MiB.

| | Every run of three |
|---|---|
| `PfpFirst` / `MeFirst` (first dword as loaded) | `7C408000` / `7C408000`, which both Caicos images and both Cedar images open with |
| `ME_INITIALIZE` | PASS: the read pointer reached it (`WaitInit` 0 polls) |
| Ring test (`SET_CONFIG_REG` of `DEADBEEF` to `SCRATCH_REG0`) | PASS, `SCRATCH_REG0` `DEADBEEF` after 1 poll |
| During | `CP_RB_RPTR` `20h`, `CP_STAT` `80000042`, `GRBM_STATUS` `A0003828`, `CP_ME_CNTL` 0 (running) |
| After | `CP_ME_CNTL` `14000000` (halted again), `GRBM_STATUS` `00003828` |

Afterwards V9XDDP's fill, copy and overlap checks still pass on the DMA
engine, and the desktop screenshot is clean.

## Not established

- Clear state, `evergreen_gpu_init` for Caicos, and any draw: Phase 5.
- Juniper's microcode.

## Decision for Michael

Plan decision 2: Caicos's PFP and ME arrays add 9,984 bytes to
`V9XHAL.DLL`, which rides the floppy set's disk 1. Disk 1 was 3,919 bytes
over when Cedar's microcode joined (2026-10-27), and the trace tool moved
to disk 3. Where Caicos's microcode goes needs deciding before the HAL
can start this CP.
