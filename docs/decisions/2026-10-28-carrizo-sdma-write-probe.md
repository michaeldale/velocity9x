# Carrizo's SDMA0 runs AMD microcode loaded through its own port: copies and fills byte-exact

Date: 2026-10-28

Status: **measured, one machine, three runs on one boot.** M-XP7053's
Radeon R7E (`1002:9874`), boot 17, desktop at 1024x768x16 under the signed
0.32.0 `vbe` driver. Phase 3 of [the plan](../plans/ati-carrizo-gfx8.md).
Reports in
[`../probe/m-xp7053-carrizo-sdma-write-probe-2026-10-28/`](../probe/m-xp7053-carrizo-sdma-write-probe-2026-10-28/).

## The question

Linux master loads Carrizo's microcode only through the SMU. v4.9 could
also write it straight into each engine's ucode port (`smc_load_fw=0`). The
reason the direct path was dropped was not found. If the port takes the
image, the driver needs no SMU message protocol and no firmware table of
contents.

## The probe

`CZDMA.VXD` + `V9XCZDMA.EXE` (`scripts/build-carrizo-dma-probe.ps1`), the
Evergreen DMA probe's design over SDMA0 alone. It has two new ops: a
read-modify-write of an allow-listed register, and a bulk write of
caller-supplied dwords to `SDMA0_UCODE_DATA` (D004h), the only port that op
reaches. The microcode is linux-firmware's `amdgpu/carrizo_sdma.bin`
(SHA-256 `c8d3eada...10ac`, header 1.1, IP 3.0, ucode version 22h,
10,368 bytes at offset 100h). It was copied to the guest as `SDMA.BIN` and
is not in the tree.

The order is v4.9 `sdma_v3_0.c` with `smc_load_fw=0`, for SDMA0 only:
1. Halt the engine and stop its ring and IBs.
2. Apply the Carrizo golden settings.
3. Set `SDMA0_UCODE_ADDR` = 0, write 2,592 dwords, set `SDMA0_UCODE_ADDR`
   = 22h.
4. Set up the ring in `gfx_resume`'s order: a 1,024-dword ring and its
   read-pointer writeback in a 64 KiB scratch window at BAR0 + 240 MiB
   (GPU `F4_0F000000`), doorbell off.
5. Flush HDP, enable the ring and IBs, unhalt.

Afterwards the probe halts the engine again, puts every register back as
the read-only call found it, flushes and invalidates HDP, reads back and
restores the window. Before any of that, a CPU-only call checked that BAR0
reaches the window: a pattern written and read back.

## What it found

- **The direct load works.** After the unhalt `SDMA0_F32_CNTL` read 0. The
  ring test (Linux's one-dword `WRITE_LINEAR`) landed `DEADBEEF`, and both
  fences landed within 9 polling reads. `SDMA0_VERSION` reads `300h`
  before and after the load, so it reports the hardware, not the
  microcode.
- **Copies.** A 256-dword copy and a 37-byte copy between odd offsets
  (source +1, destination +3) were byte-exact, with no guard dword
  touched. A forward-overlapping copy 4 dwords up came out as memmove.
- **Read-pointer writeback** worked: `F4h` bytes, the second
  submission's end, in the window.
- **Fill size.** `CONSTANT_FILL`'s header bits 31:30 matter:
  - Run 1 sent 0, as Linux's `sdma_v3_0_emit_fill_buffer` does. The engine
    filled with the value's low byte: 1,024 bytes of `44h` for `11223344h`.
    The 10-byte fill at byte offset 2 wrote exactly bytes 2-11 with `D4h`
    and nothing outside. 0 is a byte fill, byte-granular.
  - Run 2 sent 2 for the 1,024-byte fill: every dword `11223344h`, guards
    intact. 2 is a dword fill.
  - Run 2 sent 1 for the 10-byte fill of `A1B2C3D4h`. Bytes 2-11 came out
    `11h`, the high byte of the previous fill's value, and nothing outside
    changed. 1 is not a 16-bit fill of the value sent. What it does was
    not worked out.
- **Sessions.** Three runs in a row, each loading the microcode into a
  halted engine and halting it again at the end, with no soft reset
  between them. All three completed. Runs 2 and 3 match except for
  poll counts and the status reads. Evergreen's DMA engine needed a soft
  reset around each session; this one did not.
- The desktop was undisturbed and the machine stayed on boot 17.

## What it settles for the plan

- The direct ucode port path works on this silicon for SDMA. The CP's and
  RLC's ports are the same mechanism in v4.9 and are the next to try.
- DirectDraw fills at 16 bpp: a dword fill of the replicated colour, with
  a byte fill or the CPU for a ragged edge. Not a fill size of 1.
- Copies are byte-granular and overlap-safe upward, so screen-to-screen
  scrolls can be one packet per scanline.

## Not checked

- SDMA1.
- A copy overlapping downward, and copies or fills longer than 1,024
  bytes (the master limit is `3FFFE0h` bytes, "HW limitation").
- Whether a VBE mode set or the DOS box disturbs the loaded engine.
- What fill size 1 and 3 do.
- Engine reads or writes outside the carve-out (the GART is off).
