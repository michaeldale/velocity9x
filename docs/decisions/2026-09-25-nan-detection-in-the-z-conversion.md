# NaN detection in the Direct3D path: comparisons do not see NaN under Watcom

2026-09-25, host only (`scripts\build-host.ps1`, Open Watcom `wcl386`,
`-bt=nt -wx -we`). No guest was run.

## The question

The culling work (`2026-09-25-back-face-culling-in-the-d3d-core.md`) found
that Open Watcom's x87 compares do not honour an unordered result, and left
open whether the other comparison-based NaN guards behave as their comments
say. `src\display32\d3d\d3d_zfixed.c` was the one it named: its comments
promise that NaN resolves to the far plane (`v9x_d3d_z_to_1_31_depth`) and to
a zero slope (`v9x_d3d_z_to_1_31_signed`).

## What the test found

`tests\host\test_d3d_zfixed.c` gained `test_non_finite_inputs`, fed from
bits: quiet NaN 0x7fc00000, its negative 0xffc00000, and +/- infinity. Three
of its eight cases failed against the unchanged file. A scratch probe linked
against the same source printed the results:

| input      | `depth`      | `signed`     | documented        |
|------------|--------------|--------------|-------------------|
| +NaN       | `7fffff80`   | `7fffff80`   | MAX / 0           |
| -NaN       | `80000000`   | `80000080`   | MAX / 0           |
| +infinity  | `7fffff80`   | `7fffff80`   | MAX / MAX         |
| -infinity  | `00000000`   | `80000080`   | 0 / -MAX          |

`depth(-NaN)` is `0x80000000`, the x87 integer indefinite - the one value
the file exists to keep out of the Z start register, because 86Box shifts it
to zero and the far plane becomes the near plane. `depth(+NaN)` came out
right by accident. Infinities were already correct.

## Why, from the disassembly (`wdis` of the host object)

Two faults, not one:

- `value >= 1.0f` is compiled as a signed **integer** compare of the bit
  pattern, `cmp dword ptr [esp+4],0x3f800000` / `jge`. That is exact for
  ordered floats, but +NaN (0x7fc00000) reads as >= 1 and -NaN as negative.
  This is what made `depth(+NaN)` right and `signed(+NaN)` wrong.
- The remaining compares are `fcomp` / `fnstsw ax` / `sahf` followed by
  `jb`, `ja`, `je` or `jne`. PF is never tested. An unordered compare sets
  ZF, PF and CF, so NaN reads as both less-than and equal: `0 < value` is
  true (so `!(value > 0.0f)` is false and -NaN reaches `fistp`), and
  `value == value` is true.

The culling note's two observations - `area > 0.0f` TRUE, `nan != nan`
FALSE - are the second fault.

## Hypotheses the evidence killed

- "`!(value > 0.0f)` is true for NaN, `value >= 1.0f` is false for it"
  (the file's own header comment). Both false under this compiler, for
  different reasons.
- "`value == value` / `value != value` detects NaN." It never does here.

## What changed

`d3d_zfixed.c` gained a static `v9x_d3d_z_is_nan`: all-ones exponent and a
non-zero mantissa, read through a union - the approach `d3d_cull.c` uses,
narrowed to NaN because the two infinities must clamp to opposite ends and
the ordered compares already do that correctly. Both converters test it
first; the `value == value` and `value != value` arms are gone. After the
change the probe prints `7fffff80` / `00000000` for both NaNs and the
infinity rows are unchanged.

The HAL compiles this file with the host flags plus `-bd -zl -s`, none of
which touch floating-point code generation. `wdis` of
`build\ddraw-hal\d3d_zfixed.obj` after the change shows the same integer
`cmp ...,0x3f800000` and `fcomp`/`sahf`/`jb`/`ja` sequences as the host
object, each now preceded by the `and eax,0x7f800000` /
`test ...,0x007fffff` NaN check. The shipped DLL was not run on a guest.

## Measured

`build-host.ps1`: 3 failures before the fix (`depth(-NaN)`, `signed(+NaN)`,
`signed(-NaN)`), all host tests passing after. `run-checks.ps1`: green
with all five files changed. No guest was run; the soft, ViRGE and core
changes are verified by disassembly only.

## The other comparison-based NaN guards

A grep of `src\` for self-comparisons and NaN comments found four more
guards that relied on comparisons. None of their files is in the host build
(each includes the DDHAL), so these were not executed. What each one did
was read from `wdis` of the HAL objects that `run-checks.ps1` built before
the change, tracing an unordered compare (ZF=PF=CF=1) through each branch:

| site | NaN before | NaN after |
|------|------------|-----------|
| `d3d_soft.c` `v9x_d3d_soft_depth` | +NaN `ffff`; -NaN to `fistp`, the indefinite | `ffff`, far plane |
| `d3d_soft.c` `v9x_d3d_soft_texcoord`, clamped | +NaN `ffff`; -NaN to `fistp` | 0 |
| `d3d_soft.c` `v9x_d3d_soft_texcoord`, wrapping | clamped to -LIMIT (`jbe` taken, `jae` not) - not "falls through to the conversion" as its comment said | 0 |
| `d3d_virge.c` `v9x_d3d_fixed_scaled` | `je` taken on the self-compare, then `0x80000080`, the most negative step its comment says it avoids | 0 |
| `d3d_core.c` `v9x_d3d_triangle_on_target` | every test is `ja`/`jbe`; a NaN vertex passed all four and went to the engine unclipped | not on target, so to the clipper |

The clipper's guard band in `v9x_d3d_clip_triangle` did refuse a NaN, but
only because `sx < -limit` happened to compile to `jb`, which is taken on
unordered. RenderPrimitive calls the clipper directly, so it is the only
NaN refusal on that path; it now tests the bits first too.

Each file got its own static predicate - a NaN test in `d3d_soft.c`, an
inline one in `v9x_d3d_fixed_scaled`, and `v9x_d3d_coordinate_finite` (NaN
or infinity) in `d3d_core.c` - rather than one exported helper, because
exporting a symbol is a design change. `wdis` of the rebuilt objects shows
each routine testing `0x7f800000` before its first `fcomp`.

Remaining comparisons in these files are ordered once NaN is excluded, and
are unchanged. `d3d_i9xx.c` and `i9xx_float.c` already work from bits.
