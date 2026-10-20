# HD 4600: stolen memory is writable to 31 MiB, and 1082C0h names the protected top

2026-10-20, i5-4570 (`8086:0412` rev 06), Windows 98 SE, boot 53,
intel-gma `hd4600-hsw` with 3DMark 2001 SE, 3DMark 2000 and Quake 2 run
earlier in the boot. `V9XHSWMM.EXE /stolen` (build `e3c647fb-dirty`),
Phase 0 of [the heap plan](../plans/intel-haswell-heap-beyond-16-mib.md).
Evidence:
[`../probe/hsw-4570-stolen-layout-2026-10-20/`](../probe/hsw-4570-stolen-layout-2026-10-20/).
Only `[Report]` and `[Stolen]` in that INI are from this run. The other
sections are left over from earlier probes.

## Readings

| | |
|---|---|
| GGC | `0209h`: GMS 1 = 32 MiB stolen, GGMS 2 = 2 MiB GTT |
| BDSM | `DD200001` |
| 1082C0h | `DF100017` |
| GTT entries over stolen | 8,192 walked, 8,191 linear from BDSM, 0 without the valid bit |
| the one that is not | entry `1FFFh`, the last page of stolen: `4C3A92CD` |
| the entry past stolen | `4525F8CA` |
| writes, 16-31 MiB | 30 points (MiB 24, the live reserve, skipped), 28 pass |

Every point from `1000000h` to `1EFFFFCh` read the pattern back, and
then its old value after the restore. Both points in the top megabyte
failed:

- `1F00000h` read `E97FAA55` before, after the write and after the
  restore. That is the same value B1's run 1 read there on 2026-10-17.
- `1FFFFFCh` read `42493030` all three times.

## What this settles

- **The protected range is the top megabyte, and 1082C0h names it.**
  `DF100017` has bit 0 set, and its bits 31:20 are `DF100000`. That is
  BDSM + 31 MiB, the start of the megabyte that refuses writes, and it
  ends at TOLUD `DF200000`. The Linux i915 driver reads this register as
  `GEN6_STOLEN_RESERVED`, with bit 0 as "enable". Its low bits (`17h`)
  are i915's size and type fields; how they encode is not in the public
  PRM and was not tested. The base alone gives the size here (top of
  stolen minus base), so the driver should use the base and not those
  low bits.
- **Stolen memory below the protected range is usable**, by the GPU
  (linear PTEs) and the CPU (writes stick through the aperture). On this
  machine that is 16 to 31 MiB, the range the heap would grow into.
- **VBE's figure must not set the top.** It is `1FF0000h`, inside the
  protected range, as the plan assumed.
- **The GTT holds junk with the valid bit set past stolen memory.** The
  last stolen page's entry and the one after it both look random and
  have bit 0 set, so validity alone proves nothing. The heap must never
  reach past the protected base. The planned arithmetic already keeps
  it there, but mapping 64 MiB of aperture on a 32 MiB machine maps
  aperture whose PTEs point at arbitrary memory. Nothing may touch that
  part of the mapping.

## Hypotheses this kills or keeps

- Kept: "Haswell reserves a protected range at the top of stolen
  memory" (B1's hypothesis), now with the register that reports it.
- Killed: that the BIOS maps the whole GTT linearly. It maps stolen
  memory, apart from its last page.
- Not tested: a board where 1082C0h's bit 0 is clear, or where the
  range is not 1 MiB. The tester's board (64 MiB) has not been read.

## For Phase 2

`protected_bytes` = (BDSM base + stolen bytes) − (1082C0h & `FFF00000h`)
when bit 0 is set and the base lies inside stolen memory. If bit 0 is
clear, the protected range is 0, which is what i915 does. If the base
lies outside stolen memory, the reading is not understood: keep today's
16 MiB heap and the fixed reserve, and publish why. That decode is pure
arithmetic and belongs beside `v9x_intel_hsw_plan_heap`, with a host
test built from this machine's readings.
