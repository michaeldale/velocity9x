# Intel GMA Phase 5: split the code segment, then the first triangle

## Context

Phase 4 closed on 2026-09-14: on MICHAEL-NETBOOK (HP Mini 110, 945GSE, GMA 950,
`8086:27AE` rev 03) the GPU consumed a ring in stolen memory and executed an
`XY_COLOR_BLT`, with pre and post MMIO snapshots identical to the Phase 1
baseline and no error register set. Record:
`docs/decisions/2026-09-14-intel-phase4-first-write-the-gpu-executed-a-blit.md`.
Phase 5 — the first triangle — is unblocked.

Two things stand between here and that triangle, and the first is not about
Intel at all.

**The 16-bit driver has no room left.** From the checked-in map of the current
build, `build/win16-ddi-intel-gma/v9xdisp.map`:

```
_TEXT   CODE   AUTO   0001:0000   0000f7b2
```

63,410 bytes of the Win16 64 KiB hard limit. **2,126 bytes free.** The project's
own review rule (`docs/plans/multi-chip-restructure.md:135`) fails a step that
grows code more than 2 KiB, so one ordinary Phase 5 step exhausts the segment.
This was written down a day earlier
(`docs/issues/2026-09-13-a-user-import-stops-the-display-driver-loading.md:101`)
and `src/display16/intel_exec16.c:185` already moves documentation out of the
driver for this reason. Intel is the only family at risk — s3 43,695, vbe
41,503, ati 41,133, mga2 36,055, trio64 13,985. Nothing warns before it fails:
`audit-family-binary.ps1` asserts a DGROUP budget but parses `_TEXT` nowhere,
and `golden-baseline.ps1:44-46` tracks s3 and mga2 only — not the map at risk.

**And there is no 3D reference to build from.** A repo-wide search for
`3DSTATE`, `3DPRIMITIVE`, `LOAD_IMMEDIATE`, "pixel shader" or "fragment shader"
across `docs/`, `src/`, `include/` and `scripts/` returns nothing. The Gen3
audit (`2026-08-17-intel-gma-gen3-hardware-audit.md` §5) stops at the blitter:
four opcodes and five ring registers, all from Linux i915 — which has no Gen3
render support at all. Gen2/Gen3 3D only ever existed in Mesa's classic `i915`
DRI driver (deleted from Mesa in 2023) and xf86-video-intel's i830 code.
Establishing that reference is Phase 5's first deliverable, not an assumption
buried inside it.

## Decisions taken in planning

| Question | Choice |
|---|---|
| Render target | Enlarge the reserve to 1 MiB so a true 640x480x16 target fits. |
| Verification | New read-only mini-VDD hash verb (API v7), not 153,600 far calls. |
| Segment work | Second CODE segment, *and* measure what dropping `-zc` buys. |

---

# Part 1 — Split the 16-bit code segment

## The headline: this is not just a compiler flag

`wcc -mc` compiles every C call **near**, and a near call cannot cross a
segment — it is a silent crash, not a link error. I disassembled the existing
objects to measure the real boundary, and the blocker is not the Intel code
calling itself, it is the moved code calling the **C runtime**:

```
i9xx_ring.obj:   call near ptr __U4M      (×2)
i9xx_gtt.obj:    EXTRN __U4M
intel_exec16:    EXTRN strcmp_ strcpy_ strlen_ strcat_
intel_boot16:    EXTRN strcmp_ strcpy_ strcat_
```

Those live in `clibc.lib`, which declares segment `_TEXT`, and there is no
linker mechanism to place a second copy elsewhere. A compiler-generated helper
like `__U4M` cannot be declared `__far` at all. Measured across all 31 objects:

- **`strcmp/strcpy/strlen/strcat` are referenced only by `intel_exec16` and
  `intel_boot16`** — both movers. Replacing them with project-local functions
  removes those library objects from the link entirely, a small extra saving.
- **`__U4M` is referenced by `dd16`, `ddi`, `edid`, `enable16`, `gdi_accel`,
  `mode`, `vbe_modes`** — all staying. We cannot supply our own, so the movers'
  three 32-bit multiplies must be eliminated at source. No `__U4D`/`__I4M` in
  any mover.

Everything else the movers reference is either `FAR PASCAL` (`runtime.asm`,
KERNEL) or far data through DGROUP — both unaffected, because compact model
already addresses all data far.

## Mechanism, verified against the installed compiler

```
-nt=<name>   set name of text segment
-nc=<name>   set code class name
-zc          place const data into the code segment
```

Add `-nt=I9XXCODE` to the eleven moved sources. **Do not pass `-nc=`** — the
class must stay `CODE` so the map rows, linker ordering and the auditor's
class-based parsing keep working. Linker script gains, beside line 163:

```
segment 'I9XXCODE' preload fixed shared
```

`FIXED` is required, not stylistic: an internal far call is fixed up to a real
selector at load time, and a `MOVEABLE` segment may be moved afterwards, leaving
that selector stale. NE allows far more segments than we need; the 64 KiB limit
is **per segment**, which is the whole point.

## What moves, what does not

**Moves** (~19.6 KiB, leaving ~44 KiB in `_TEXT` and ~20 KiB in `I9XXCODE`):
`intel_diag16`, `intel_gtt16`, `intel_event16`, `intel_ring16`, `intel_exec16`,
`intel_boot16`, `i9xx_mmio`, `i9xx_gtt`, `i9xx_ring`, `i9xx_arm`, plus new
`intel_str16.c`.

**Stays:** `intel_hw16.c` and `gma950_hw16.c` — they own the `V9X_HW16_OPS` /
`V9X_HW16_DEVICE` tables, whose pointers are near by the deliberate four-family
convention at `multi-chip-restructure.md:36`.

`i9xx_ring.c` **does** move, with `v9x_i9xx_sandbox_calculate` far-ised — the
one crossing (`gma950_hw16.c:12`). Keeping it in `_TEXT` would give back 2,069
of the bytes being removed *and* raise the boundary from one far declaration to
nine.

## The cross-segment surface, in full

- **Into `I9XXCODE`:** `v9x_intel_publish_{mmio_fingerprint,gtt_inventory,ring_plan,event}`
  (from `intel_hw16.c:61-63,76`), `v9x_intel_boot_arm_prepare` (`loader.c:42` —
  the only reference to any mover anywhere outside the Intel files),
  `v9x_i9xx_sandbox_calculate`.
- **Out of `I9XXCODE`:** `v9x_get_build_identity` and `v9x_selected_mode_geometry`,
  both shared with every family, so both stay near and are reached through far
  wrappers in a new `src/display16/intel_bridge16.c` (~30 bytes of `_TEXT`).

## The one broken function pointer

`intel_hw16.c:76` stores `v9x_intel_publish_event` — a mover — directly in the
near `V9X_HW16_OPS.publish_event` slot (`hw16.h:203`). Audited every table and
pointer in the tree: **this is the only break.** No mover is an NE export and no
mover's address is taken by Windows. Fix is a `static` near forwarder in
`intel_hw16.c` that makes the far call; ten bytes, and the four-family hook
contract is untouched.

The rule, for the header comment so it cannot be re-broken by accident:

> The hook tables are near and live in `_TEXT`; `intel_hw16.c` and
> `intel_bridge16.c` own the Intel hook and shared-service bridges; `loader.c`
> also calls `v9x_intel_boot_arm_prepare` across the segment boundary. Thus
> a hook slot never holds the address of a function in another code segment — it
> holds a near forwarder that makes the far call.

## Making a mismatch structurally impossible

New `include/velocity9x/intel16.h` declares **every** crossing function exactly
once with `V9X_I9XX_FAR`, which expands to `__far` only under
`__WATCOMC__ && _M_I86` and to nothing elsewhere — the `i9xx_*` units are also
compiled 32-bit into the host suite by both Watcom and MSVC. Because the
definition and every caller read that one header, a mismatch becomes a compiler
diagnostic instead of a boot hang.

The scattered bare `extern`s are deleted and replaced by the include:
`intel_hw16.c:7-11`, `intel_exec16.c:45-47`, `intel_ring16.c:19-22`,
`intel_diag16.c:16-17`, `loader.c:19`. That cleanup is what converts the split
from "careful" to "checked".

## The C-runtime rewrites

- `i9xx_gtt.c:11` — FNV prime multiply becomes the shift-add decomposition of
  16777619 (`2^24 + 2^8 + 2^7 + 2^4 + 2^1 + 1`). **Re-run the `wdis` check
  afterwards** to confirm Watcom inlines the constant 32-bit shifts rather than
  calling a shift helper.
- `i9xx_ring.c:151,211` — one `static v9x_u32 v9x_i9xx_mul32(...)` shift-add
  used by both. Validation arithmetic run a handful of times per attempt, so the
  cost is unobservable.
- `intel_exec16.c` / `intel_boot16.c` — drop `<string.h>`, use `v9x_intel_str_*`
  from the new `intel_str16.c`.

All three are behaviour-preserving and already covered by `test_i9xx_gtt.c` and
`test_i9xx_ring.c`, which are the proof of equivalence.

## Build plumbing

Optional per-source manifest key, following the `MiniVddVbeCollect`
optional-key precedent:

```powershell
@{ Name = 'intel_exec16'; Path = 'src\display16\intel_exec16.c'; CodeSegment = 'I9XXCODE' }
```

`build-win16-ddi-skeleton.ps1` turns it into `-nt=`, and emits one `segment`
line per distinct value — so families declaring none link **byte-identically**
and the s3/mga2 goldens stay valid. Schema validation in `scripts/lib/family.ps1`
(name pattern, and refuse a name the compiler already owns); documentation in
`docs/specifications/family-manifest.md:201-241`, including a plain warning that
moving a source is not local.

## The three gates that make this safe

This is the part that stops the problem recurring, and none of it exists today.

**a. CODE-segment budget** in `audit-family-binary.ps1`, beside the DGROUP one:
parse every CODE-class row, log each one's occupancy on every build, fail over
budget. Use **57,344** — 8 KiB below the hard limit, i.e. four maximum-sized
steps of the 2 KiB review rule, so the warning arrives with room to act. Unlike
DGROUP there is no hidden stack in a code segment, so the map number *is* the
number and halving it would fail every family today. Also assert the CODE-row
**count** matches the manifest, so a `-nt=` that silently failed to apply is
caught here rather than by a link failure three steps later.

**b. The near-call gate — the one that actually makes the split safe.** For each
moved object, run `wdis -a`, take every `call near ptr <sym>` whose target is in
that object's `EXTRN` list, resolve the target's segment frame from the map, and
assert it equals the object's own. This is the backstop that catches
compiler-generated calls no grep can see — it is what would have caught
`__U4M` — and it is not optional.

**c. Generalise the attribute check.** `audit-family-binary.ps1:104` matches one
`CODE|FIXED|SHARE|PRELOAD` line anywhere in the dump, which with two CODE
segments passes if either is right. Parse the `wdump -e` segment table and
assert **every** CODE segment is FIXED, SHARE and PRELOAD.

Plus `golden-baseline.ps1:44-46`: add the Intel map. Its regex at `:211-214`
already handles multiple CODE rows, so it is one line plus a re-baseline.

## The `-zc` measurement

**Moved out of this plan.** The split has landed, so this measurement no longer
belongs to Phase 5 and is tracked on its own in
`docs\plans\zc-const-placement-measurement.md`. It is not started, and nothing
in Phase 5 depends on it. The reasoning below is kept here for the record.

`-zc` places **all** const data in the code segment — which is why `CONST` is
`00000000` in every family's map — and `intel_exec16.c` alone carries 121 string
literals. Intel's DGROUP is 11,032 + 1,024 heap of a 32,768 budget, so there is
~20.7 KiB to receive them.

It is on the shared argument line, so it is global by construction; measure it
by deleting it on a throwaway branch and comparing `_TEXT`, `CONST`, `CONST2`
and `DGROUP` per family before and after, on one pinned `BuildId`.

**Recommendation, to be confirmed or killed by those numbers: measure it, record
the four numbers per family in the decision doc, and keep `-zc` on.** It trades
the segment that just gained 22 KiB of headroom for the one whose hard limit
also has to hold the stack, it changes all five families and forces an s3/mga2
re-baseline, and it does not remove the structural problem — the family grows
again next phase and the split has to happen anyway. Do the measurement **after**
the split lands, so the two are never in the same diff.

## Verification for Part 1

1. `check-tree.ps1` — the new two-sided far-declaration rules and manifest schema.
2. `build-host.ps1` **and** `build-host-msvc.ps1` — the multiply and string
   rewrites are only proven equivalent by the existing host tests, and
   `V9X_I9XX_FAR` must vanish under both compilers.
3. `build-win16-ddi-skeleton.ps1 -Family intel-gma` — invokes the auditor; the
   three new gates fire here. Expect (b) to find anything missed.
4. The same for s3, vbe, ati, mga2 — proves the shared change is inert.
5. `golden-baseline.ps1 -Compare` for s3/mga2 unchanged, then re-baseline once.
6. `run-checks.ps1`, then `run-family-enable-gate.ps1` (shared 16-bit layer).

**One cheap experiment worth doing first.** The largest unprovable risk is not
the Intel code — it is whether Win98's NE loader loads, fixes up and keeps FIXED
a *second* code segment in this driver at all. That risk can be moved onto
emulated hardware: on a throwaway branch give the **s3** family a `CodeSegment`
on one leaf unit, far-ise its callers behind the same macro, and run
`run-family-enable-gate.ps1` against the S3 guest. A green `Stage=enable-ok`
proves the loader mechanism before a single netbook boot is spent.

**The netbook boot is a regression test, not an experiment — do not arm.** The
whole test is that the driver loads and every existing capture is still produced
byte-identically modulo `BuildId`: desktop appears (proving both segments loaded
and `DriverInit`'s cross-segment call returned), `V9XBOOT.INI` `Stage=enable-ok`,
`V9XHW.INI` complete, then `INTELMM.TXT`, `INTELGTT.TXT` (its hash is the
regression test on the rewritten FNV multiply — must equal the 2026-09-12
capture exactly), `INTELEVT.TXT` and `INTELRNG.TXT` through their validators.
Two cold boots, per the Phase 2 done-criterion, because one cannot distinguish a
working far fixup from a lucky one.

---

# Part 2 — Phase 5, the first triangle

## Step 1: the 3D packet audit (nothing else may start first)

`docs/decisions/<date>-intel-gen3-3d-packet-audit.md`, following the existing
audit's structure exactly: source/licence table, the `docs/ddk-inputs.md` usage
rule ("implement from this document, not by transcribing driver code"), and the
marker **"documentation-derived and unconfirmed on this machine"** on every
claim — because unlike the 2026-08-17 audit, none of this has a second venue.

Sources: Mesa classic `i915` (`i915_reg.h`, `i915_state.c`, `i915_fragprog.c`,
`i915_program.h`, `intel_tris.c`) at a pinned `mesa-amber`/≤23.0 tag, MIT; and
xf86-video-intel i830 (`i915_reg.h`, `i915_3d.h`, `i830_render.c`) as the
**independent** cross-check partner. Record both commit hashes, and record the
2023 deletion so the next reader does not grep current Mesa and conclude it
never existed.

It must establish, in emission order, for a flat-shaded untextured un-Z'd
triangle: whether `MI_FLUSH` needs bits beyond bit 0 on Gen3;
`_3DSTATE_BUF_INFO` (pitch encoding, and the **tiled/fence bit that must be
clear**); `_3DSTATE_DST_BUF_VARS` (RGB565, and the `DSTORG_*` subpixel bias that
decides whether a software reference can ever agree); `_3DSTATE_DRAW_RECT`
(inclusive or exclusive); scissor-disabled; `LOAD_STATE_IMMEDIATE_1` words S0-S7
— above all **S4's vertex format and flat-shade select, which must agree exactly
with the vertex dwords and is the most likely silent hang**; whether
`LOAD_STATE_IMMEDIATE_2` may be omitted; **whether the pixel shader may be
emitted inline or whether the `LOAD_INDIRECT` PSP pointer is mandatory**; the
minimal two-instruction fragment program (declare diffuse, move to output
colour); and `3DPRIMITIVE` inline form with its vertex layout.

**The cross-check rule:** nothing enters a builder on one source. Opcodes and
lengths must match between the two trees; every bitfield must appear in both, or
in one plus a consistent *use site* in the other; the packet set and order must
be corroborated by two independent use sites, not two copies of a header.
Single-sourced values go in a table headed "must not be used". **Any value that
would have to be guessed or swept kills the phase then and there** — which is
the cheapest possible kill and why this is step 1.

## Step 2: the memory layout move

`V9X_I9XX_GTT_RESERVE_BYTES` `0x20000` → `0x100000`. At the measured inputs
(`vbe 0x7b0000`, `bsm 0x7f800000`):

| Region | Aperture | Bytes |
|---|---|---|
| DirectDraw heap | `0` | `0x6b0000` (6.6875 MiB, was 7.5625) |
| ring | `0x6b0000` | `0x10000` |
| HWS (carved, never written) | `0x6c0000` | `0x1000` |
| scratch / lower guard | `0x6c1000` | `0x1000` |
| **render target** | `0x6c2000` | `0x96000` (640x480x16, pitch 1280) |
| upper guard | `0x758000` | `0x1000` |

Scratch stays at `reserve + 0x11000` regardless of reserve size, so only the
base moves — preserve that deliberately, since it leaves `loader.asm:1292` and
the guard offsets `+0x11000`/`+0x11ffc` unchanged.

`struct v9x_i9xx_sandbox_layout` gains six appended members. **Verified safe:**
every use is a declared automatic zeroed by `v9x_i9xx_zero_layout`, with no
positional initialiser anywhere — but that function walks the struct as a
`v9x_u32` array, so **every appended member must be `v9x_u32`**.

Constants that move — the complete list, all verified present:
`intel_gma.h:14`; `i9xx_ring.c:18-51`; `intel_exec16.c:271-276`;
`loader.asm:1258`, `:1286` (address **and** the `_MapPhysToLinear` size
`00020000h`→`00100000h`), `:169-171` (the Phase 4 BLT destination `007a1100h`),
`:1389` (**the Phase 4 execution CRC changes as a consequence — the currently
armed stick is invalidated and must be re-armed**), `:1723` (the `RING_MEMORY`
bound `0001fffch`→`000ffffch`, **without which the 16-bit side cannot read the
target at all**); `check-intel-ring-plan.ps1:18-20,234` and its self-test
fixture; `test_i9xx_ring.c:18-41`; `test_i9xx_gtt.c:92-113`. Note
`i9xx_gtt.c:41` derives the reserve **page count** from the same constant, so
the Phase 2 backing check goes 32 → 256 pages.

Review grep: `grep -rn "0079\|7ff9\|007a1" src scripts tests` should return
nothing but historical decision records.

**Why it is safe:** Phase 2 measured all 65,536 PTEs present and linear with a
stable hash across every Phase 3 event, and the reserve is carved from the *top*
of the VBE-reported region, so growing it downward moves into memory the same
inventory already proved backed. No PTE is written. Rather than lean on that,
add a preflight that re-derives it every boot: assert
`reserve_first_entry + reserve_entry_count <= backed_prefix_entries` with the
operands published.

**The heap shrink** is verified three ways: a host test asserting all four
published modes still get `0x6b0000` (largest visible is 1024×2×576 = `0x120000`,
five times under) plus the fallback boundary; the unarmed boot bringing up a
1024x576x16 desktop and reporting `HeapBytes=006B0000`; and the honest statement
that nothing currently exercises DirectDraw allocation on this family
(`EngineType NONE`), so the heap is published and unused.

## Step 3: new host-testable leaf units

New `include/velocity9x/intel_gen3_3d.h` (separate from the already-300-line
`intel_gma.h`), every constant carrying a one-line comment citing the audit
section that licenses it. Then, all pure C89 on the `i9xx_ring.c` pattern:

- **`i9xx_float.c`** — `v9x_i9xx_float_from_int` / `_to_int`, IEEE-754 by
  construction with no FPU, exact below 2^24, refusing NaN/inf/denormal/negative
  zero/fractional with distinct reasons. This is what makes "bit-preserving float
  transport" tested rather than hoped, and why nothing 16-bit needs a `float`.
- **`i9xx_3d.c`** — `v9x_i9xx_build_3d_state` (the complete block in one reviewed
  order, sub-emitters `static`) plus `v9x_i9xx_3d_state_extent` so tests and the
  capture writer can address packets without exporting eleven symbols.
- **`i9xx_fragprog.c`** — `v9x_i9xx_build_fragment_program`, no arguments because
  it is a constant, and a constant because Phase 6 forbids a shader compiler.
- **`i9xx_vertex.c`** — `v9x_i9xx_build_vertex_run`, refusing any coordinate that
  is not a finite integer inside the drawing rectangle.
- **`i9xx_3d_stream.c`** — stream assembly, `v9x_i9xx_phase5_execution_crc`, and
  `v9x_i9xx_phase5_parameters` as the single source of truth the generator calls.
  Triangle (160,120),(480,120),(320,400) — deliberately asymmetric so a
  transposed X/Y is visible; colour with no repeated bytes so a swap is visible.
- **`i9xx_3d_decode.c`** — `v9x_i9xx_decode_phase5_stream` returning **a numbered
  reason and the rejected dword index**, with `_INDIRECT_FORBIDDEN`,
  `_TEXTURE_FORBIDDEN`, `_DEPTH_FORBIDDEN` and `_TILED_FORBIDDEN` among them.
  `v9x_i9xx_decode_phase4_stream` is untouched — two decoders, two allowlists.

`tests/host/test_i9xx_3d.c` with nine case groups. The highest-value one is **the
golden stream**: the exact expected `v9x_u32` array written out in full, plus its
CRC, because it is what the generator, the asm table and the validator all
ultimately agree with. Then the decoder accepting it and rejecting ~15 targeted
single-dword mutations by reason *and* index.

Reused: **`v9x_i9xx_ring_plan` becomes load-bearing for the first time** (it is
host-tested but no shipping path calls it); `v9x_i9xx_crc32_dwords`;
`v9x_i9xx_ring_free_space`; `v9x_i9xx_build_mi_probe`; and `d3d_raster.c` as the
software reference, run **host-side in the validator**, never on the netbook.

## Step 4: widen the arm gate without relaxing it

Today the mini-VDD pins a literal ten-dword table (`loader.asm:169-171`) and one
literal CRC (`:1389`), both hand-typed — which is exactly why the layout move
above silently changes both. Phase 5's ~130-dword stream is still entirely
compile-time constant, so the gate's *shape* survives; what does not survive is
maintaining it by hand.

**Generate it.** Add `--emit-intel-3d-stream` to the existing host test binary
(`test_main.c:608` is `int main(void)` and needs an argv signature) so the
**compiled C builders** are the source of truth — not a fourth PowerShell
reimplementation, which is what `check-intel-ring-plan.ps1 -ComputeArm` is today
and is the copy that will drift. `scripts/gen-intel-3d-stream.ps1` renders
`src/minivdd32/i9xx3d.inc` (MASM table + CRC + ring start, included by
`loader.asm`, replacing hand-maintained constants for the new stream) and
`scripts/data/intel-3d-stream.psd1`
(consumed by both validators). Both checked in, so a netbook trip is reproducible
from a clean checkout and a human can read the gate.

The layout move also changes Phase 4's BLT destination and execution CRC.
Generate or separately verify those revised Phase 4 constants from its compiled
builder before retaining the standalone Phase 4 gate; do not let the Phase 5
include silently replace the Phase 4 table or make its CRC refer to the old
reserve. The combined arm CRC must cover both revised streams in execution
order, including Phase 4's wrap/probes and Phase 5's pre-draw probe.

Split the reproduce check to respect "check-tree needs no compiler":
`check-tree.ps1` does the compiler-free half — the `.inc` exists with its
generated banner, `loader.asm` has no literal table left, and **PowerShell
recomputes CRC-32 over the parsed table and asserts it equals the EQU literal in
the same file**, which catches a hand edit of either. `run-checks.ps1` adds
`gen-intel-3d-stream.ps1 -Verify` right after `build-host.ps1`, regenerating and
diffing byte for byte.

Also: `V9X_I9XX_PHASE5`, with `expected_phase` added to the arm request so
`v9x_i9xx_arm_evaluate` stops hardcoding `V9X_I9XX_PHASE4` (`i9xx_arm.c:134`);
`IntelArmPhase` in the arm file so a Phase 4 token can never arm Phase 5;
generalise the sequence machine to take a step range, with the Phase 4 names
kept as thin wrappers so `intel_exec16.c` and its twelve tests are untouched;
Phase 5 steps numbered from 20 so a log line is never ambiguous.

**Phase 4 stays alongside**, because it is the only thing that can distinguish
"the layout move broke the ring" from "the 3D packets hung the parser", and it
costs milliseconds.

**Resolve the two-phase arm transaction before implementing the chain.** The
current `v9x_p4_preflight` compares `IntelArmCrc` with the Phase 4 execution
CRC, and the current Phase 4 success path records the final result and clears
`IntelInFlight`. A Phase 5 token carrying a Phase 5 CRC therefore cannot pass
that preflight or retain its authority until the draw. Define a dispatcher that
validates `IntelArmPhase`, build ID, token and a CRC covering the reviewed
*combined* Phase 4 replay and Phase 5 execution before any write. The internal
Phase 4 replay must still pass its own generated Phase 4 stream/CRC gate, but
must defer final token completion and in-flight clearing to the Phase 5 result.
Keep the standalone Phase 4 arm path working with its own CRC. Host tests must
prove that a Phase 4 token cannot reach Phase 5, a Phase 5 token cannot skip a
failed replay, and a power cut between phases leaves `IntelInFlight` set.

## Step 5: mini-VDD API v7

`V9XMINI_FN_I9XX_RING_HASH`, read-only, two passes both returned (an unstable
read must be visible, exactly as the GTT capture returns hash A and B), reusing
`V9xMini_I9xx_Hash_Dword` (`loader.asm:1058`, FNV-1a) unchanged. Every bound a
distinct numbered refusal, with the range check done on carry rather than a
signed compare. It ships in the **unarmed** build, behind the fingerprint guard
rather than the executor guard, so the unarmed boot proves the hash path before
any armed boot depends on it.

New `check-tree.ps1` assertion on the same pattern as the existing ones: exactly
two read sites, no store through any register-indirect destination.

Executor gains arms 20-24 inside the **existing single pinned store procedure** —
no second store procedure, so "every store lives in one place" holds. Two
refinements that reduce risk: make step 5 load START from the generated constant
too (removing a literal and one edit site), and make the stream comparison its
own step, because Phase 4 folded it into step 5 and that is why `S05Failure=12`
was ambiguous. Staging uses **two tables and two counters**, so a Phase 4 dword
can never be staged into a Phase 5 slot.

## Step 6: the sequencer and `INTEL3D0.TXT`

New `src/display16/intel_3d16.c` behind its own positive guard
`V9X_I9XX_PHASE5_EXECUTOR` (separate from Phase 4's, so Phase 5 can be built
dark), entered by the two-phase dispatcher **only when Phase 4 passed in the
same boot** — which makes "revalidate the layout" a hard
precondition in code rather than an operator instruction. An unarmed boot still
produces a complete no-write `INTEL3D0.TXT`.

Specify the target initialization before defining the expected hashes: the
640x480 RGB565 target must be filled with one fixed background word before the
triangle, and the full target must read back as that word. Use a bounded bulk
GMADR fill through the existing framebuffer selector after the armed intent is
durable, with start, length, pitch and reserve bounds checked on both sides of
the call; this avoids 153,600 individual far writes. It must not touch the
published heap or MMIO. Hash the filled target twice before submitting 3D, and
include the fill pattern and its expected hash in the generated golden. The
unarmed B1 boot does not fill: it checks that two read-only hash passes over
the unchanged target agree, then samples through `V9xGmadrRead` to validate
the address path. Keep the fill out of the unarmed path and record its duration
and first mismatch in the armed capture.

Steps 20-30 with `IntentStep` flushed **before** each action, `P5Marker` bumped
every 16 staged dwords, and a two-dword `MI_NOOP`/`MI_FLUSH` probe submitted
immediately before the 3D stream — worth its two dwords, because it is what
separates "the ring is dead" from "the packets are wrong".

Preflight gets its own reason space and its own operand keys (`P5Ref*`),
including four new checks: Phase 4 did not pass this boot; the reserve's GTT
entries are outside the measured backed prefix; **the built stream did not
decode**; and **the built stream's CRC differs from the compiled-in generated
constant** — two comparisons that close the last drift path in the driver itself.

`INTEL3D0.TXT` carries: the full stream one dword per key plus a packet
offset/length index so a reader can find the drawing rectangle without counting;
the vertices both as raw bits and decoded integers (redundant on purpose, so a
float-transport bug shows in the artefact rather than in a wrong picture);
per-step results; whole-target hash ×2 plus the redraw hash; 480 row CRCs; named
per-pixel assertions (centroid, one inside each vertex and edge midpoint; all
four corners and three outside-edge points still at the pre-fill pattern —
deliberately avoiding the edges, whose fill rule the plan licenses to differ);
guard regions; and **`HeapProbe`**, one dword just below the reserve, which is
the most important boundary observation because the reserve just moved 896 KiB
down into memory the heap previously published. The probe is read-only: that
dword remains in the published heap and must not be overwritten with a canary.
Record its value before and after the draw, but do not classify a difference
alone as GPU corruption unless the heap is proved quiescent or exclusively
owned for the interval. Place writable guard patterns only inside the reserve;
the new lower boundary needs an in-reserve guard or another independently
validated way to detect an overrun into the heap.

`scripts/check-intel-3d-capture.ps1` with `-SelfTest` mutating a good fixture,
and `scripts/arm-intel-phase5.ps1` as a **separate** script — a single armer that
can arm either phase is one flag away from arming the wrong one. Both wired into
`run-checks.ps1`. The software-reference comparison is *reported*, not failed,
until a golden is promoted.

## Ordering and boots

Eleven host-only steps (audit → headers → layout → leaf units → generator →
sequence machine → mini-VDD → sequencer → validators → docs → flip the guard on),
each gated green before the next, on the Phase 4 slice precedent. Then:

- **B1 unarmed** — validates the layout move, the heap shrink, the 1 MiB
  mapping, two stable read-only v7 hash passes, and both stream plans. A
  decoder error is caught here and costs no armed boot.
- **B2 armed** — Phase 4 replayed at the new base, chained into Phase 5.
  Chaining uses the combined arm transaction above. `IntentStep` is flushed
  before every step and the phases use disjoint step namespaces (`S05`-`S12`
  vs `S23`-`S30`) and failure ranges, so a hang is attributable from the file
  alone.
- **B3 armed, cold, identical** — required by "stable across cold boots"; all
  hashes and row CRCs must match B2.
- **B4 unarmed** — clean return, `IntelLastResult=pass:<token>`.

**Four boots, two armed**, against Phase 4's eight (seven of them failures). The
instrumentation that buys that is designed in from the start, because the Phase 4
record names it as the fix: every refusal is a number *with its operands*.

## Risks and kill criteria

The standing rule from the risk decision is inherited unchanged: AC only; first
hang → record and repeat once; reproducible → ours → kill; not reproducible →
ambiguous with erratum 12 named; third hang of any kind → stop.

**One governance item to settle before B2.** The 2026-09-13 risk decision opens
the errata gate for **Phase 4 specifically**, and its central argument is that
Phase 4 "is close to the minimum possible processor-to-graphics interaction".
That argument does not transfer to a 3D draw, and the Phase 4 record says so
plainly: the workload "did not provoke it, which says nothing about a heavier
one." Phase 5 therefore needs its own dated decision extending the gate, not an
inherited `errata_gate = V9X_TRUE` at `intel_exec16.c:260`. It will likely go the
same way — the same assessment notes unfixed drivers ran full 3D workloads on
millions of laptops for eighteen months — but it must be **taken**, not assumed.

Hang meaning by step is tabulated in the design: a hang during staging is not a
GPU hang at all (no MMIO written yet) and points at the enlarged mapping; a hang
with the probe drained but the 3D stream stuck at its first packet is the
reproduce-once-then-kill case, since fixing it means trying packet variants,
which is verbatim the plan's stated kill. One clarification worth writing down:
**if the audit concludes the indirect PSP pointer is mandatory, that is not
automatically a kill** — an indirect buffer is still a fixed, reviewed, CRC'd
array in the same reserve. The kill fires only if its contents cannot be
determined from the sources.

Non-hang kills: any **in-reserve** guard touched; independently established
corruption of the published heap (a `HeapProbe` change alone is only an
observation); a changed GTT hash; the audit failing to double-source a
required field.

Not failures: a software-vs-hardware mismatch confined to a one-pixel band along
the edges (explicitly licensed); and **no visible change on the panel** — the
target is offscreen, so the photograph's only job is to prove the display was
never ours to touch. Say that in the runbook, because "nothing happened" is
otherwise a confusing result.

---

## Documentation defects to fix in passing

Both found while verifying the above:

- `hardware-diagnostics.md:193` and `include/asm/V9XMAPI.INC:213-216` say staging
  refusal `09` is "the store did not read back". `loader.asm:1319` now uses `10`
  and treats it as **non-fatal**, because the bytes the GPU fetches go through
  GMADR. Both texts are stale.
- `intel-gma-bringup-runbook.md:349` points at `hardware-diagnostics.md` for the
  `IntentStep` codes. The `PreconditionCode` and `StageFail` tables are there
  (`:144`, `:175`), but the `IntentStep` / `S05`–`S12` table is not. Phase 5 adds
  eleven more step codes, so this wants writing before it grows.

## Critical files

`scripts/build-win16-ddi-skeleton.ps1`, `scripts/audit-family-binary.ps1`,
`scripts/check-tree.ps1`, `scripts/lib/family.ps1`,
`packaging/families/intel-gma/family.psd1`,
`include/velocity9x/intel16.h` (new), `include/velocity9x/intel_gen3_3d.h` (new),
`include/velocity9x/intel_gma.h`, `include/asm/V9XMAPI.INC`,
`src/chipsets/intel/intel_hw16.c`, `src/chipsets/intel/i9xx_ring.c`,
`src/chipsets/intel/i9xx_gtt.c`, `src/display16/intel_bridge16.c` (new),
`src/display16/intel_str16.c` (new), `src/display16/intel_3d16.c` (new),
`src/display16/intel_exec16.c`, `src/minivdd32/loader.asm`.
