# Ironlake: a free tier-0 desktop, and why the engine does not come with it

Status: **proposed; nothing coded, nothing measured on a Windows guest.** The
only Ironlake evidence in the tree is two DOS surveys
(`docs\probe\references\chinaboard-ironlake-vgasurv-2026-09-28*.ini`). No
machine in the fleet runs Windows 98 on one.

## What arrived

A board reporting DOS hostname "chinaboard-bulldozer" - a Clarkdale, not an
AMD part - surveyed 2026-09-28 with the display on `8086:0042`, class
`030000`, at 00:02.0. Its VBIOS identifies as *Intel(R) Ironlake Desktop
Graphics Chipset Accelerated VGA BIOS*, build 1931, 11/11/2009. That is Gen5:
one generation after the 965 and four after the Gen3 the intel-gma family was
written for.

What the survey establishes, and it is all BIOS-level:

| | |
|---|---|
| MMIO | BAR0 `FBC00004` - 64-bit, so BAR0/BAR1 are one register |
| Aperture | BAR2 `D000000C` - 64-bit prefetchable, GMADR |
| I/O | BAR4 `0000DC01` |
| **No BAR3** | the 64-bit BAR2 consumes it; there is no separate GTTADR |
| Stolen | ~32 MB (`TotalMemoryBytes=33488896`) |
| VBE | 3.0, LFB at `D0000000`, matching BAR2 |
| Usable modes | 12 of 27 described, to 1280x1024 at 8/16/32 bpp |
| EDID | DDC level 1, block 0 valid |

## The gate that decides everything

**Does Windows 98 SE install and boot on a Clarkdale board?** Nothing in this
project has tried. The survey ran from a DOS 7.10 floppy with
`WindowsPresent=no`, which says nothing either way.

This is not a driver question and no driver work can answer it. Until a
machine boots Win98SE on Gen5 silicon, every phase below is unbuildable-on,
and a plan that skipped this gate would be planning against an assumption.

## Phase 0: tier-0, which is already written

The `vbe` family carries `CompatibleId = PCI\CC_0300` - any VGA-class
adapter. The Ironlake IGP is class `030000`, so it already matches. The VBIOS
offers a VBE 3.0 linear framebuffer at an address that agrees with the
prefetchable BAR, which is the one cross-check tier-0 depends on.

So an unaccelerated Ironlake desktop at up to 1280x1024x32 needs an INF
install and **no code**. For most of what anyone would want from this chip,
that is the whole deliverable.

Two things to watch on the first install:

- **Pitch is not width times bpp.** Mode `0103` reports 832 bytes for
  800x600x8. The path must take `LinearBytesPerScanLine` from 4F01h rather
  than computing it - the same class of defect that needed a padding flag on
  Gen3 (`1024x576x16` scans out at 2112 bytes, not 2048, per
  `docs\decisions\2026-09-25-padding-the-z-pitch-on-gen3.md`).
- **Fifteen of the 27 listed modes are undescribed** - `4F01h` returns
  `0x004F` with the block zeroed, the VBE-sanctioned way to say a mode is
  unavailable in the present configuration. The mode scan already drops these
  at parse; the Pineview measurement in
  `docs\decisions\2026-08-28-pineview-vbe-mode-list.md` is the precedent.

Nothing in Phase 0 is Ironlake-specific. If it fails, it fails for a reason
tier-0 needs fixing for generally.

## Phase 1: what an accelerated path would have to re-derive

The intel-gma family is one chip. `8086:27AE` is hardcoded in four places -
[`intel_gma.h:9`](../../include/velocity9x/intel_gma.h),
[`intel_hw16.c:131`](../../src/chipsets/intel/intel_hw16.c),
[`i9xx_arm.c:129`](../../src/chipsets/intel/i9xx_arm.c) and
[`intel_exec16.c:332`](../../src/display16/intel_exec16.c) - plus the
manifest and its INF. Adding an ID there is trivial. What the ID would reach
is not.

Each of the following is an assumption the Gen3 code makes that Gen5 is not
obliged to honour. **None has been measured on Ironlake.** Each needs a probe
and a decision doc before a line of code moves, because none can be
host-tested.

### The GTT moved

Gen3 reaches the GTT through its own BAR3 (`v9x_i9xx_gtt_bar3`,
`V9X_I9XX_GTT_BYTES` = 256 KB). The survey shows no BAR3 on Ironlake at all.
The GTT is inside the MMIO BAR; **at what offset, and how large, is unknown
here**. BAR0's size is not in the survey either - the report records the BAR
value, not a size probe.

*Probe:* size BAR0 by the standard write-ones/read-back, then look for the
GTT by its content - a table of page-aligned entries with a valid bit is
self-identifying against surrounding registers.

### Stolen memory is decoded from the wrong register, with the wrong table

[`intel_gtt16.c:130`](../../src/display16/intel_gtt16.c) reads bits 6:4 of GGC
and indexes `{0,1,4,8,16,32,48,64}` MB. That is the 945 encoding. Gen5's GMS
field is wider and encoded differently, and the register may not be at the
same config offset - on Gen3 it is on the host bridge at 00:00.0, and
Clarkdale moves the memory controller onto the CPU package. `BSM` has the same
question.

*Probe:* read the candidate offsets on both 00:00.0 and 00:02.0 and check the
decode against the ~32 MB the VBE controller info already reports. Two
independent numbers agreeing is the standard this project uses.

### PTE attributes

`v9x_i9xx_decode_pte` accepts four attribute patterns as `known_attributes`.
Gen5 cache bits differ. An unknown pattern is reported rather than trusted,
so this fails visibly rather than silently - but it fails.

### Ring and MI commands: mostly, but not entirely, portable

`MI_NOOP`, `MI_FLUSH`, `MI_STORE_DWORD_IMM` and `MI_BATCH_BUFFER_START` all
survive into Gen5. One difference is already recorded in the tree:
[`intel_gma.h:409`](../../include/velocity9x/intel_gma.h) notes ACTHD moved
from `0x20c8` to `0x2074` on gen4+. Others will exist; that one was found
because it was needed.

### The display fingerprint

`v9x_i9xx_analyze_fingerprint` reads DSPCNTR and pipe timing in the `0x70000`
block, which Ironlake keeps. But Gen5 splits the display engine across the FDI
link to the PCH, so what a live panel does to those registers is a question,
not a given.

### The 2D blitter

`XY_COLOR_BLT` and its relatives exist on Gen5. Identity, GTT, ring and blits
is a coherent stopping point, and the one worth aiming at if Phase 1's probes
come back clean.

## Phase 2: Gen5 3D is a different project

The 3D stack in this tree is Gen3 fixed-function:
`_3DSTATE_LOAD_STATE_IMM1`, `_3DSTATE_PIXEL_SHADER`, `_3DSTATE_MAP_STATE`,
`_3DPRIMITIVE` inline - see
[`intel_gen3_3d.h`](../../include/velocity9x/intel_gen3_3d.h). Gen4 deleted
that command set and Gen5 is Gen4's architecture. A Gen5 draw needs URB
allocation, binding tables, `SURFACE_STATE`, and VS/SF/WM kernels as
assembled Gen5 ISA. There is no fixed-function pixel pipeline to configure,
so `i9xx_fragprog.c` has no counterpart at all.

`i9xx_3d.c`, `i9xx_fragprog.c`, `i9xx_vertex.c`, `i9xx_texture.c`,
`i9xx_scene.c`, `i9xx_3d_stream.c` and `i9xx_3d_decode.c` would all be new
files, and the Phase 4/5/6 arm-and-verify ladder would run again from the
first write. That is most of the Intel work of the last month, redone against
a documentation set the project does not yet have -
`docs\plans\intel-3dmark99-missing-textures.md` records which Intel volumes
are findable, and the Gen5 ones are not among the ones already used.

**This plan does not propose that work.** It records why it is not an
extension of the Gen3 engine.

## Test hardware

| Machine | State |
|---|---|
| chinaboard (Clarkdale) | The only board where an Ironlake IGP is the primary display. No Windows install. |
| P7H55-M | Has a Clarkdale IGP at 00:02.0, but class `038000` with `Command=0000` and every BAR unassigned - enabled in the BIOS as a secondary, given no resources. A second test bed only if the firmware can be made to assign them. |

Neither is a machine anyone is relying on, which is the right shape for this.

## Order

1. Windows 98 SE on a Clarkdale board. Everything is blocked here.
2. Install the `vbe` family; confirm the desktop and the 12 described modes.
   No code. If this is all that ever happens, it is still the answer for most
   users of the chip.
3. A `/rom` survey plus an Ironlake-specific probe: BAR0 size, GTT offset
   within it, GGC and BSM location, the stolen-size encoding. One decision
   doc, with the INIs.
4. Identity, GTT, ring, blits - only if (3) comes back clean.
5. Gen5 3D as its own project, if ever.
