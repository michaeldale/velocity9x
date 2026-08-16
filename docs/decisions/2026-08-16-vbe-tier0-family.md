# The VBE tier-0 family

Date: 2026-08-16
Status: accepted

Phase 9 of `docs/plans/multi-chip-restructure.md`. The `generic/vbe` family
package: BIOS mode set through VBE 4F02h, aperture from 4F01h, VRAM from 4F00h,
linear framebuffer, no acceleration. It is the first family with no chip in it.

The interface was already built for this. `hw16.h` says a NULL hook means "use
the chip-agnostic default", so tier-0 is what you get when a family supplies no
hooks at all - a consequence of the interface rather than a special case. What
phase 9 had to add was the other half of that sentence: until now
`enable16.c` had no default to fall back to and returned failure at stage 3
whenever `read_aperture` was NULL.

## Where the work landed

- `src/common/vbe_parse.c` - the 4F00h/4F01h result parsers, byte-composed and
  host-tested. Whether a BIOS answer is credible, and whether it describes the
  mode that was just set, is a judgement worth making somewhere a wrong answer
  costs nothing. The driver cannot try it out: by the time the answer is wrong,
  a mode is set and the framebuffer is mapped at the wrong address or striped at
  the wrong pitch.
- `src/display16/hw/vbe16.c` - the calls themselves.
- `src/display16/enable16.c` - `v9x_vbe_default_aperture()`, the NULL-hook
  default, reporting stage 3 on every refusal like the hook it replaces.
- `src/chipsets/generic/vbe/` - the hw16 table (every hook NULL) and the pure
  policy backend.

## Why the buffered calls need DPMI

4F02h is a plain `int 10h` because it passes no buffer. 4F00h and 4F01h hand the
BIOS a buffer in ES:DI, and the driver runs in 16-bit protected mode, so the
selector would reach the V86 BIOS as a raw paragraph address and it would fill
in whatever memory happened to live there. The buffer therefore has to be real
DOS memory, and the call has to go through the DPMI host's
simulate-real-mode-interrupt service (0300h).

The block comes from DPMI 0100h rather than `GlobalDosAlloc` because
`check-tree.ps1` confines `<windows.h>` to six files and `vbe16.c` is not one of
them. Widening a deliberate architectural boundary to reach one allocator is a
bad trade, and INT 31h through `#pragma aux` is the idiom the file already uses.
Windows documentation prefers `GlobalDosAlloc` for applications; if the DPMI
path turns out to misbehave under a real Windows DPMI host, moving the
allocation into `enable16.c` (which is inside the boundary) is the fallback, not
widening the list.

The block is allocated once and never freed, matching how the driver already
treats its framebuffer selector.

## Why the family declares no required instructions

A family's `Audit.Required` patterns become every other family's `Forbidden`
patterns, and the audit scans every object in the image. The only instructions
that would identify tier-0 are the 4F00h and 4F01h calls - and those live in
`vbe16.c`, which every family links. Claiming them here would fail the S3 and
Matrox audits while proving nothing about this family.

Identity is carried instead by the map symbol `v9x_vbe_device`, the dispatch
symbol, and INF hardware-id set equality. Same reasoning as the Trio64 chip.

**Do not add patterns to this family without first moving the code they match
out of shared objects.**

### The anchoring bug this surfaced

The ViRGE chip declared `'test\s+al,8'` for its CR53 bit-3 sequence, with no
trailing anchor, while its sibling pattern `'or\s+al,8\b'` had one. Unanchored,
it also matches `test al,80H` - which is exactly what the VBE
linear-framebuffer attribute check in `vbe_parse.c` compiles to, in every
family. The Matrox image was convicted of running the ViRGE's MMIO sequence.

Fixed by anchoring the pattern to `'test\s+al,8\b'`. The general hazard is worth
stating: these patterns are regexes over disassembly, so a short one silently
matches longer immediates, and the failure appears in a *different* family from
the one that owns the pattern.

## Cost

The tier-0 default lives in shared code, so the S3 and Matrox images carry it
whether or not they can reach it: **+2048 bytes of `_TEXT` on the S3 image and
+1934 on the Matrox one**. That sits exactly at the plan's 2 KiB per-step
review budget.

It is accepted rather than optimised because the alternative is worse. Making
the tier-0 code conditional means a build-time define around it in `enable16.c`,
which reintroduces the per-family `#ifdef`s that phase 5 spent its whole risk
budget removing from the shared display16 sources. The linker cannot drop the
code either: the NULL-hook branch is a runtime decision, so the call is
genuinely reachable.

If the 64 KiB code segment later gets tight, the lever is to move
`v9x_vbe_default_aperture` and the readers into a per-family object that only
families wanting them link - which is a packaging change, not a source one.

## Scope

- The allowlist is one PCI id, QEMU/Bochs std-vga `1234:1111`. Tier-0 works on
  far more than that, but the INF may only claim what has been tested, and the
  honest way to offer the rest is Have-Disk, where a person decides. A wildcard
  would make Windows bind this driver to every display adapter it finds.
- The registry arm is an exact-id match like every other arm, deliberately not a
  catch-all fallback. `v9x_backend_for_pci` returning null is what the driver
  and the family-matrix tests rely on to mean "not claimed".
- Mode flags are `V9X_HW16_VBE_LINEAR` alone. The no-clear bit is an S3 BIOS
  quirk, and an unknown BIOS should start from a clean framebuffer.
- **Known limit:** the ViRGE/DX BIOS ignores the generic linear-framebuffer bit,
  so this package cannot drive one. It refuses cleanly at stage 3 rather than
  rendering incorrectly. That is the documented behaviour, not a bug to fix.

## A refusal, not an adaptation

If the BIOS reports a stride different from the family table's pitch, the enable
fails at stage 3. GDI and the registry have already agreed on that pitch and the
PDEVICE is built from it, so adapting would leave a display that looks broken
with nothing recorded anywhere. This mirrors the Millennium II's 4F06h check,
which rejects a BIOS that picks its own stride for the same reason.

VBE 3.0 reports the linear stride separately from `BytesPerScanLine`, and where
it is present it is the one that applies. The parser prefers it when non-zero.
QEMU's BIOS reports VBE 3.0, so this is the common path for tier-0 rather than
an exotic one.

A broken 4F00h is treated differently: VRAM only sizes the off-screen heap and
`dd16.c` has a floor to fall back on, so it costs some off-screen surfaces
rather than the whole enable.
