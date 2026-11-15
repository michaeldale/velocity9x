# GT218: pixel shader 1.1 as Tesla code, held to the interpreter on the card

Date: 2026-11-15

Status: steps 1, 2 and 3 of
[nvidia-gt218-ps11.md](../plans/nvidia-gt218-ps11.md) done. Step 3
measured on W9QI0DAD (ASUS GeForce 210, GT218 `10DE:0A65`, FX-6300,
Win98 SE), boot 21, `V9XN5CH /3d /3d:ps11` built from 67e17cd8 plus the
probe stage (`67e17cd8-dirty` in the report). Nothing is in the HAL:
`draw_shader` and DDI 8 are steps 4 and 5.

## Step 1: the encoder (5966206c)

`nv50_shader.c` gained source NEG and ABS and destination SAT as operand
bits, fmin/fmax, set.f32 writing a register or only a flags register, a
predicate for the next long instruction, kil, and tex with one to four
coordinates or the cube target. Each word in `test_nv50_shader.c` is
worked out from Mesa's `CodeEmitterNV50` (`nv50_ir_emit_nv50.cpp`) and
read against envydis' `g80.c` tables, field positions cited per case.
envydis came from the envytools repository (`envydis/g80.c`) into
`build/external/nv50/envytools/`; the plan assumed it was already there.

Two facts the sources settled that the plan did not list:

- There is a select instruction (`slct`, opcode C with word 1 = 4xxxxxxxh
  in envydis), but Mesa's nv50 emitter never writes it and envydis does
  not name its condition. CND is a set into a flags register and two moves
  predicated on it (NE, then EQ), which is Mesa's own predicate pattern
  (`nv50_ir_lowering_nv50.cpp`, `setPredicate(CC_NE, pred)` after a set
  with a flags definition).
- fmul's saturate bit exists only from SM 1.2 (envydis `F_SM12`). The
  GT218 is SM 1.2, but the translator clamps with min/max anyway, which
  also covers PS 1.1's [-1, 1] clamp that no saturate bit gives.

## Step 2: the translator (67e17cd8)

`v9x_nv50_ps11_build` (`nv50_ps11.c`) lowers `V9X_PS11_PROGRAM` to scalar
Tesla code: each written channel computed into a result register and
moved after (co-issue and destination-is-source then read old values);
constants reach registers as 0 + c, against a zero made from w - w,
because mov cannot read a constant and only slots 1 and 2 can; kills go
last. 64 registers. `test_nv50_ps11.c` decodes the emitted words and runs
them per fragment from garbage registers: all 15 programs of the Gen3 and
Haswell corpus match `v9x_ps11_run` within 2e-4 with the same kills
(largest program 358 words). Mutating SUB to ADD and CND's GT to LT
failed 61 checks.

The model found one encoder limit, now enforced and tested: short
instructions have six-bit register fields (envydis `sdst_bf`, `ssrc_bf`;
bits 8, 15 and 22 beside them are saturate, negate and the 32-bit flag),
so `$r64` and up need the long form. The first model run stopped on a
short `mov $r0, $r49` whose source it had read as 7 bits, including the
32-bit flag (113).

## Step 3: on the card

The stage draws each program as one 96x56 rectangle (5,376 pixels) into
a pattern-filled 128x72 surface, its six attributes planes over it with
w 1, four 8x8 A8R8G8B8 textures nearest and clamped, and judges every
pixel against the interpreter (linked into the probe) given the same
inputs and nearest samples. Each program runs as written and again with
`mov r0.rgb, r0.a` appended, since the targets keep no alpha. A judged
pixel must be within 2 steps at 8 bits and 1 at 5 or 6 bits; samples
within 0.05 texel of an edge and kill coordinates within 0.01 of zero are
not judged. Report:
[`V9XN5CH.INI`](../probe/w9qi0dad-gt218-ps11-2026-11-15/V9XN5CH.INI).

| Program (words, colour/alpha pass) | judged, bad | unjudged | killed | worst step, 32 / 16 bpp |
|---|---|---|---|---|
| Arith (358 / 380) | 5376, 0 | 0 | 0 | 1 / 1 colour, 0 / 0 alpha |
| FourTex (146 / 168) | 2332, 0 | 3044 | 0 | 1 / 1 |
| Kill (78 / 100) | 3424, 0 | 1068 | 884 | 0 / 1 |
| Bem (74 / 96) | 3656, 0 | 1720 | 0 | 0 / 1 |
| Cnd (98 / 120) | 5376, 0 | 0 | 0 | 0 / 0 |

Counts are the same at both depths and for both passes; the pattern
outside the rectangle was intact in every draw. Every kick ran clean:
`PgraphIntr=0`, `PgraphTrap=0`, no VM trap pending, `Result=PASS` at
`Ps11Bpp32` and `Ps11Bpp16`. `PgraphTrapStatus=80000000` is in every
stage from `[PreGr]` on, so it is the register's resting value, not a
trap.

Arith exercises on the card every source modifier (NEG, BIAS, BIASNEG,
SIGN, SIGNNEG, COMP through long fadd with negated sources), the shifts,
both clamps (fmin/fmax against constants), co-issue, DP3, LRP, a def,
and CND (set into `$c0`, predicated moves). Kill exercises a predicated
kil with `FP_CONTROL` `USES_KIL`. FourTex binds four TIC/TSC entries
(the TSC limit raised from the setup's 0 to 3, `LINKED_TSC`). Bem reads
through stage 1's bump matrix from the constant buffer.

Not exercised on the card: the saturate and ABS bits and negation in the
short form (the translator does not emit them), cube and 3D fetches (cube
placement and TIC types are step 6), projected coordinates, texbeml and
the texm3x3 family. The host model covers those through the corpus; the
card has not seen them. The judge's tolerances were not themselves
mutation-tested on the card; the Arith colour pass matching all 5,376
pixels within one step of planes that vary per pixel is what says the
drawing is the program's.

The probe re-initialises PGRAPH under the installed driver, whose GDI
uses the engine from logon (V9xEngineStart), so the machine was restarted
with `reset` straight after the run (boot 22).

## Gates

`build-host.ps1` (encoder words, the 15-program model run) and
`run-checks.ps1` passed before each commit. Step 3's probe built with
`build-nv50-channel-probe.ps1`, which now links `nv50_ps11.c`,
`d3d_ps11.c` and `d3d_ps11_ref.c`.

## What it means for step 4

- The translator's code runs as Mesa's encodings predict; the program
  sizes (largest corpus program 358 words, 1,432 bytes) confirm the plan's
  concern that the 4 KiB fragment area cannot hold a program cache beside
  the 12 fixed-function variants. The probe used its own `FP_ADDRESS` and
  `CODE_CB_FLUSH` after each upload, which a HAL cache can do too.
- 28 vertex results and 24 interpolants (`FP_INTERPOLANT_CTRL`
  08180418h) worked as nv50_ps11.h lays them out.
