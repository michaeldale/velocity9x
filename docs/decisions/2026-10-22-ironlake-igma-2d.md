# Ironlake on intel-gma: one hung boot, a diagnostic reading past its window, then blits on the ring

Date: 2026-10-22
Status: **measured on one machine.** Step 4 of the Order in
`docs\plans\intel-gen4-gen5-bringup.md` for Gen5, as far as identity,
GTT, ring and blits. Flips and Direct3D are withheld.

Machine: W9QI2F4F, remote help `M-J0F0SJ`
(`2026-10-22-ironlake-w9qi2f4f-audit.md`,
`2026-10-22-ironlake-gen5-read-only-mmio.md`). Captures:
`docs\probe\w9qi2f4f-ironlake-igma-2026-10-22\`.

## What changed

The GM965's first step (`2026-10-20-gm965-ring-and-blits.md`), for `0042`:

- `intel-gma` claims `8086:0042` as `hd-ironlake`, with
  `v9x_ilk_fill_engine` (`gma950_hw16.c`): the 945's descriptor with D3D,
  FLIP and FLIP_RING cleared. No Direct3D word.
- `V9xPciReadIntelGttConfig` (`runtime.asm`) takes Gen4's 64-bit BAR path
  for `0042` and hands over the GTT at BAR0 + 2 MiB, the measured place
  (`2026-10-22-ironlake-gen5-read-only-mmio.md`), after checking BAR0's
  4 MiB alignment.
- The mini-VDD (`loader.asm`) treats a GTT at BAR0 + 2 MiB as in BAR0, so
  its event journal reads the 965 fences at 3000h, not Gen3's 2000h range.
- The GTT inventory (`intel_gtt16.c`) decodes GGC's GMS as four bits, as
  Linux `intel-gtt.c` does with `I855_GMCH_GMS_MASK`. Three bits read
  Ironlake's `0380` as no stolen memory at all.

## Boot 44: the first install hung, and why

Installed by Have Disk over `vbe` with `INTELARM.TXT`
`IntelRuntime3D=0`, so no ring. `WININIT.INI` held four `dest=src`
renames and no `NUL=`. Agent `reset` at 02:11Z. **The machine did not
come back within the agent's 600 seconds**; it was online again later, at
boot 44, up 65 seconds when first asked. Whether someone restarted it by
hand is not recorded, nor what the screen showed.

The files in `C:\V9XDIAG` at boot 44 were not boot 44's. Boot 44 ran
`vga.drv` (below), and nothing on it wrote them. They are the hung boot's,
and they place where it stopped:

- `INTELMM.TXT`: complete, `Result=PASS`. The display fingerprint ran.
- `INTELGTT.BIN`: complete, all 262,144 bytes. The GTT stream ran and the
  file was closed.
- `INTELGTT.TXT`: 34 bytes of junk with `PCI\VEN_8` in it. Windows 98
  caches profile writes for one file and flushes when the next file is
  written. This one was never flushed, so nothing after the GTT inventory
  wrote a profile file. The ring plan's first write would have.

Between the end of the GTT stream and the ring plan, the inventory does
one thing to hardware: it samples two GMADR dwords, at offset 0 and at
the reserve. **The reserve was computed from the BIOS's 128 MiB (VBE
`mem=2047`), so the sample read GMADR + ~127 MiB, through the 16 MiB
selector the family maps.** Every Gen3 and Gen4 BIOS before reported
under 16 MiB, so the two figures had always agreed. The four-bit GMS fix
above is what let the inventory get this far: with three bits it had
returned early, with no stolen memory, and never sampled.

This is a reading of the code and the files, not a capture of the fault.
What followed supports it: the fix below changed nothing else, and the
next boot that ran the driver passed.

**Fix:** the inventory, its reserve and both samples are bounded by
`v9x_vbe_vram_bytes`, the BIOS's figure clamped to the mapped window
(`enable16.c`), which is also what places the runtime ring. On every
earlier machine the two are equal. The inventory now also writes
`WindowBytes`. To fit `I9XXCODE`'s budget, the GMS table is WORDs.

## Boots 44 and 45: Windows on VGA

Boot 44's `BOOTLOG.TXT` (Windows logged it after the failure) shows
`V9XMINI.VXD` loaded and the adapter started, then `vga.drv` loaded as
the display. The fixed `V9XDISP.DRV` went in by a plain WININIT rename
and an agent `reset`; boot 45 came back, and was on `vga.drv` too.

The cause was `HKEY_CURRENT_CONFIG\Display\Settings` `BitsPerPixel`
`"4"`, which picks `MODES\4\640,480` = `vga.drv` from the adapter's own
key (`boot45-HKCCDISP.REG`, `boot45-DISPCLS.REG`). Windows left it there
after the failed boot, as on the A8U4I5 after a card swap. Set to `"8"`
by `regedit /S` (`boot45-BPP8.REG`).

## Boot 46: identity and read-only captures (IntelRuntime3D=0)

- `V9XBOOT.INI` `Stage=enable-ok`, 640x480x8, and the desktop draws
  (`boot46-desktop.png`).
- `V9XHW.INI`: "Intel HD Graphics (Ironlake)", `DeviceId=0042`,
  `Direct3D=not-advertised`, `EngineCaps=00000000`.
- `INTELMM.TXT` `Result=PASS`: pipe A live at 640x480, plane A on at
  8 bpp, stride 640, address 0. The Gen3 fingerprint's pipe and plane
  offsets are Ironlake's CPU display's.
- `INTELGTT.TXT`: `Bar3=FBE00000` (BAR0 + 2 MiB), two hashes agree,
  `StolenBytes` 128 MiB, `WindowBytes` 16 MiB. Entries 0-0x7FFE map
  stolen memory linearly from `C8000000`. Every entry from 0x7FFF to the
  end of the 64 Ki read, but one, is `FFFFFFFF` (`UnknownAttrs=8001`), so
  `Result=REVIEW`. The reserve at `0xF00000` is backed (`C8F00000`), and
  both samples read.
- `INTELEVT.TXT`: one `boot-enable` event, fences 0, ring untouched.

The top half of the aperture's GTT is not mapped to anything this driver
uses. What the BIOS meant by `FFFFFFFF` there is not known. The file
keeps the hung boot's 34 junk bytes ahead of its section; Windows appended
after them.

## Boot 47: the ring and the blits (IntelRuntime3D=1)

The first command this driver has sent to a Gen5 part.

- `EngineStamp=type=00000003 caps=00000400 ring=E5F25000`: the Gen3
  engine type, ASYNC_SUBMIT only, a ring mapped.
- **`V9XDDF.EXE` at 640x480x8 passed all seven stages**: create,
  cooperative level, an off-screen surface, GetBltStatus, a colour fill
  read back, then a rectangle fill and a copy read back against a CPU
  reference. 0 mismatches.
- Afterwards (`boot47-V9XG5MM.INI`): RING_START `00F00000`, the reserve
  in the 16 MiB window; RING_CTL `0000F001`, 64 KiB, enabled; HEAD = TAIL
  = ACTHD = `E8`. IPEIR, IPEHR, EIR and ESR `0`.
- Then `V9XMSW /set:1024x768x32` (live) and V9XDDF again: all seven stages,
  0 mismatches at 32 bpp; HEAD = TAIL = ACTHD = `1B0`, no error.

The same 232 bytes as the GM965's first run, consumed the same way. The
pixels prove the blits landed; TAIL and ACTHD prove they went through the
ring, since the CPU fallback submits nothing.

## What this does not answer

- Flips. The Gen4 flip writes DSPSURF and DSPLINOFF and reads the live
  pipe's DSL, all at the same offsets on Ironlake; it has not run here.
- 16 bpp, sustained load, and DirectDraw games.
- Whether the hung boot faulted or stopped some other way, and who
  restarted the machine after it.
- The ring plan (`INTELRNG.TXT`) still places its Phase 4 layout from the
  BIOS's figure (`ReserveOffset=07EF0000`). It reads and writes nothing,
  and the armed Phase 4 path is the netbook's alone.
