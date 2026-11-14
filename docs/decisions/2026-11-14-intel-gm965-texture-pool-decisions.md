# Intel GM965 texture pool: decisions and gate

Date: 2026-11-14
Plan: `docs/plans/intel-gen3-system-memory-textures.md`, carried to the
first Gen4 chip. The Gen3 record this follows:
`2026-11-12-intel-gen3-texture-pool-decisions.md`.
Measurements it rests on: Phase 0
(`2026-11-13-intel-gm965-texture-pool-phase0.md`) and Phase 2
(`2026-11-14-intel-gm965-texture-pool-phase2.md`).

Michael, 2026-11-14: start the port, on the `gm965-texture-pool` branch,
with the pool not on by default for the GM965.

## Decisions

1. **The Gen3 pool, unchanged in size and placement:** 32 MiB at GMADR
   8 MiB (GTT entries `800`-`27FF`), on a machine with at least 512 MiB,
   textures only, DirectDraw's heap 1. The policy (`i9xx_pool.c`) is not
   changed; Phase 0 ran it on this machine's numbers.
2. **Gate: TOMMOHP only**, device `8086:2A02` with subsystem `103C:30C0`
   (config 2Ch = `30C0103C`), checked in the mini-VDD, which a caller
   cannot talk out of it. Any other GM965 answers `refused-alloc`, reason
   3. This is the netbook's kind of gate, a machine identity, not a user
   setting (the 2026-09-18 rule against gated defaults stands). It lifts
   to every GM965 when the conditions below are met.
3. **Every GM965 maps and write-combines 64 MiB of GMADR**, not 16: the
   device row cannot tell machines apart. Measured on TOMMOHP only.
4. **Recovery: `V9X3D OFF` from DOS turns the pool off**, as on Gen3.
5. **No Phase 2 probe is needed at boot:** the mini-VDD's map path keeps
   the checks the probe proved (uniform entries before, every PTE read
   back, the scratch PTE restored on any mismatch).

## What is different from Gen3, and how it is handled

- **The GTT is in BAR0** (GTTMMADR + 512 KiB). The mini-VDD already maps
  it there for the inventory (`V9xI9xxGttInBar0`); the pool path refused
  that layout and now accepts it for the gated machine only.
- **PTE format.** Gen4 entries carry physical address bits 35:32 in bits
  7:4. Windows 98 hands out frames below 4 GiB, and the mini-VDD writes
  `frame | valid` from a 32-bit page-table entry, so those bits are zero
  by construction. Phase 2 wrote and read back such entries.
- **The Gen4 HAL** (`d3d_g4.c`) samples a texture by its aperture offset;
  the texture unit now takes `v9x_surface_sample_offset`, which admits the
  pool. Render targets and depth keep `v9x_surface_offset`. Placement is
  `d3d_place.c`'s, shared with Gen3.

## Errata

Spec update 316274-009 lists nothing that hangs the graphics engine.
Erratum 4 is the relevant one: visual corruption with integrated graphics
under Windows Vista's paging model with Flex Memory in dual-channel
asymmetric mode, worked around in Intel's driver in a way the document
does not describe. Vista's paging model is the GPU reading ordinary RAM
pages through the GTT, which is what the pool does. TOMMOHP's DIMM layout
is not known. The exposure is wrong texels, not a hang; the conditions
below look for it.

## The open risk

The first Phase 2 probe run on this machine ended in a blue screen whose
text nobody read (`../issues/2026-11-14-gm965-pool-map-probe-machine-
stops.md`). Fifty runs of the staged probe since, idle and awake, pass.
The driver maps the pool at every boot, so a fault of that kind would be
a blue screen at startup. That is why the gate is one machine whose owner
knows, and why it does not lift on probes alone.

## Conditions for lifting the gate to every GM965

- `[IntelPool] Result=mapped` on TOMMOHP across at least ten boots, with
  no boot lost.
- V9XD32 at 16 and 32 bpp with its textures in the pool reading back
  exactly; V9XGLP; the release probe set unchanged.
- A game or 3DMark run that fills the pool, with no engine timeout and
  frames that look right.
- A mode change and a full-screen DOS box with the pool mapped.
- The blue screen either explained or not seen again in that time.
