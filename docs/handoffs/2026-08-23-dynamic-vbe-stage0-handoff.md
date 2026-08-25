# Dynamic VBE pipeline: Stage 0 handoff — contracts frozen, evidence captured

Date: 2026-08-23
Branch: `dynamic-vbe-stage0`, two commits on top of `main` at `c25b9a5`, pushed
to `origin` and not merged:

| Commit | What it is |
|---|---|
| `87bbac8` | Stage 0 proper: contracts, build gates, fixtures |
| `3218338` | The QEMU std-vga and GMA950 captures, as host fixtures |

No other tracked changes at handoff. `run-checks` green across all four
families; the host suite passes under both Open Watcom and MSVC `/W4 /WX`.
Not pushed to the `github` remote — `origin` is what every other branch tracks.

Scope: Stage 0 of [the dynamic VBE pipeline plan](../plans/dynamic-vbe-pipeline.md),
which is complete. Stage 1 has not been started.

---

## 1. The one-paragraph version

Stage 0 asked for contracts, build gates, negative fixtures and evidence, and
all four are done. The mini-VDD API contract now exists once instead of twice,
with a tree check that fails if the assembly and C halves disagree; the
rescue-probe mode list is generated from the family manifest instead of
hand-written; DGROUP occupancy is asserted at every build; the parser
understands the VBE 3 linear colour fields and derives significant depth; and
24 bpp is refused with a test behind the refusal. The evidence half is captured
from two real BIOSes, and it changed one rule from a precaution into a
requirement: **on QEMU std-vga, `VideoModePtr` points into the caller's own
4F00h block**, so Stage 1's staging copy is mandatory on the first target rather
than defensive. No driver behaviour changed — the mini-VDD is byte-identical to
its previous build.

## 2. What is done, and what proves it

| Claim | Evidence |
|---|---|
| One definition of the mini-VDD API contract | `include\asm\V9XMAPI.INC`, included by both `src\minivdd32\loader.asm` and `src\display16\runtime.asm`; both assemble (MASM 6.11 and Open Watcom wasm) |
| The two halves of that contract cannot drift | `scripts\check-tree.ps1` compares 26 constants against `include\velocity9x\vbe_cache.h`; a deliberately mismatched value and a locally shadowed constant were both checked to fail the tree check |
| No product behaviour changed | Built both images against a clean `HEAD` worktree at a pinned build id: `v9xmini.vxd` byte-identical; `v9xdisp.drv` `_TEXT` grew 354 bytes with DGROUP, `_DATA` and `_BSS` unchanged to the byte |
| The rescue-probe list is generated, not written | `build-minivdd-skeleton.ps1 -Family <id>` emits `V9XPROBE.INC`; for both scan-enabled families the output is byte-for-byte the seven numbers `loader.asm` hard-codes today |
| DGROUP has room for the runtime table | `audit-family-binary.ps1` reports and asserts `DGROUP 2014 + heap 1024 = 3038 of 32768`; the runtime table needs 1728 more |
| The VBE 3 colour rule works on both generations | `test_colour_field_source` in `tests\host\test_vbe_parse.c`, including the stale-scratch case |
| 24 bpp is refused, quietly | `test_24bpp_is_omitted_not_fatal`; the QEMU capture shows 19 such modes on the first target |
| The panel-filtered case behaves as the plan claims | `test_gma950_survey`, transcribed from the netbook survey |
| The DirectDraw list needs the active-row rule | `test_qemu_stdvga_list`; see section 4 |

## 3. Where it actually stands, with numbers

**QEMU std-vga (SeaBIOS VBE), captured 2026-08-23.** VBE 3.0, `Capabilities`
`00000001`, `OemSoftwareRev=0000`, 16 MiB reported, **93 modes listed and
properly terminated**. Admission accepts **49** (9 at 8 bpp, 20 at 16, 20 at 32)
and refuses 44: **19 at 24 bpp**, 18 with no linear framebuffer, 7 at 15 bpp.
After the merge, **48 distinct rows** — modes `0013h` and `0146h` both describe
320x200x8. All seven baseline rows are corroborated, so nothing is contradicted
there. EDID is present and valid: EDID 1.4, "QEMU Monitor", preferred timing
**1280x800**, which is also an admitted geometry.

**945GM netbook (Intel GMA 950), `run2` captured 2026-08-23 by Michael.** VBE
3.0, `Capabilities=00000001`, `OemSoftwareRev=0100`, 7.69 MiB reported, 36 modes
listed. Six are live — 1024x576 at 8/16/32 (`0160h`/`0161h`/`0162h`) and 640x480
at 8/16/32 (`0101h`/`0111h`/`0112h`). Five of the seven baseline rows are dead:
`0100h` is absent from the list entirely, and `0103h`, `0105h`, `0114h`, `0117h`
answer `4F01h` with `Attributes=0000`. Row zero is alive, so fallback selection
is unaffected.

Both captures live outside the repository with the other survey artifacts:
`personal\v9x-qemu-stdvga\` (with a README recording the method) and
`personal\v9x-intel950\run2\`.

## 4. Two findings that changed the plan

**`VideoModePtr` points into the caller's buffer on the first target.** Two DOS
programs on the same guest reported different mode-list pointers — `0E38:25F8`
from the survey, `0B46:06D4` from the inventory — both in low DOS RAM, neither
near the C000h ROM. A pointer that moves with the caller is a pointer into the
caller's own 4F00h block. Without the staging copy, Stage 1's walk would read
its mode numbers out of a block the first `4F01h` had already rebuilt. The plan
now states this as measured rather than anticipated.

**An ordinary 1024x768x32 desktop is not in the DirectDraw list.** 48 rows into
32 slots: the subset fills 28 with 8- and 16-bpp rows and has four left for high
colour, which the four smallest 32-bpp modes take. Nothing is wrong with the
subset policy — the list is simply longer than the block — but it means
`dd16.c` substituting the active row is load-bearing on the first target, not a
nicety. `test_qemu_stdvga_list` pins both this and the duplicate-geometry case.

**A third, smaller one.** BIOS identity does not live where this project assumed.
SeaBIOS reports `OemSoftwareRev=0000`, the netbook `0100`; neither names a build.
The informative source is a string in both cases but a different one each time —
the ROM build stamp (*Build Number: 1585 PC 14.34 01/08/2008*) on the Intel part,
the VBE OEM strings (*SeaBIOS VBE(C) 2011*) on QEMU. The plan and
[the conformance spec](../specifications/dos-vbe-conformance.md) now say read
both eventually rather than prefer either now; S5 there is marked partly landed.

## 5. What to do next, in order

### 5.1 Stage 1 — the bounded mini-VDD walk

All of it can be written now; nothing is blocked. Per the plan: replace the
seven-number loop with the bounded `VideoModePtr` staging walk, zero the scratch
before every query and require `AX=004Fh` before reading it, populate the packed
records with counts and reason flags, probe the generated rescue list into its
reserved cache without exposing those records to indexed enumeration, add the v2
`MODE_AT`/`MODE_MASKS`/`CONTROLLER` operations, and extend the serial status.
Rendering stays on the static family table; it ships diagnostic-only, enabled for
the `vbe` package alone.

Two decisions worth taking deliberately rather than discovering:

- **Bumping `V9XMINI_API_VERSION` to v2 makes the pair mandatory.**
  `runtime.asm` refuses a contract newer than it knows, so a v2 mini-VDD beside a
  v1 driver falls back to no-API — clean per invariant 9, but a mixed pairing
  loses the VBE cache silently rather than erroring. Packages ship both files
  together, so this is acceptable; it should be said out loud, not found in a
  boot trace.
- **This is where the boot-hang risk lives.** `Exec_Int 10h` has no timeout and
  the walk turns 8 BIOS calls into up to 128. Keep the query count clamped well
  below the bound for the first drop, and keep the pre-call serial markers.

### 5.2 Stage 1's exit gate needs a guest run

The gate is "the guest dump matches the DOS inventory record-for-record", and the
inventory half now exists for QEMU. The guest half needs a Win98 boot with the
new mini-VDD, which is a physical/VM step: from a Claude session 86Box is
undriveable and the QEMU guest can be booted but the comparison is a judgement
call better made by eye the first time.

### 5.3 The netbook re-run, when it lands

Michael was re-running the current survey build on the GMA950 while this session
captured QEMU. The field to look for is **`ModeListPointer`** — absent from the
`run2` capture, which was missing every section header after `[VBE]` (see
section 6). If the Intel BIOS puts the list in ROM where SeaBIOS puts it in the
caller's buffer, that is worth recording: it means the staging copy is protecting
against a hazard that varies by vendor, and the diagnostic should say which case
a given machine is.

### 5.4 Not yet started, and deliberately

Stages 2 through 6 are untouched. The publication flag — hiding
scan-contradicted baseline rows — is specified in the plan and has no code;
`test_gma950_survey` pins the admission half and says so in its comment.

## 6. Gotchas this session paid for

**The QEMU guest could not be driven by keyboard.** `sendkey f8` never reached
IO.SYS across three attempts, including one from a `-S` cold start with a
controlled t=0, and ScanDisk was blocking every boot because the image had been
left unclean. What worked: copy the qcow2, then patch two characters in
`MSDOS.SYS` — `BootGUI=1`→`0` and `AutoScan=1`→`0` — which boots straight to a
real-mode DOS prompt. The patch is byte-length-preserving so no FAT or directory
entry changes. **The original `win98.qcow2` was not modified.**

**`C:\QemuVMs\Win98SE-QEMU-StdVGA\make-floppy.ps1` produces a broken image.**
The FAT chain terminates at the first cluster and the second file's start cluster
is out of range (observed: `V9XVBE.EXE` at cluster 1010 where 68 was correct).
It was not debugged; the capture used a Python builder that verifies a
byte-exact round trip of every file before writing. If that script is wanted for
future work, it needs a look at `Set-Fat12Entry` and the `$nextCluster`
arithmetic.

**Reading the guest screen: use the monitor, not a screendump.** `xp /512xb
0xb8000` and decoding character/attribute pairs is a decode rather than a guess;
OCR of a PPM was useless. One trap: a scrolling screen sampled across several
`xp` requests interleaves rows from different moments and looks like corruption.

**The tool default output paths differ.** `V9XSURV.EXE` takes `/out:`, so it can
write straight to `A:`; `vbe_inventory_dos` hardcodes `C:\V9XVBE.TXT` and needs a
`COPY` afterwards. Both were run from one `RUN.BAT` on the floppy, which is also
how the output came back out — QEMU writes the floppy image through, and the
files were extracted host-side with a FAT12 reader.

**Watcom rejects a run-time check of a constant expression.** `-we` turns the
"unreachable code" warning into an error, so a `CHECK(V9X_VBE_CACHE_MAX == 64u)`
will not compile. Use the `typedef char x[cond ? 1 : -1]` idiom that
`win9x_ddraw_abi.h` already uses; `tests\host\test_vbe_cache.c` has examples.

**`V9X_DD_MODE_COUNT` is not available to the host suite.** It lives in
`win9x_ddraw_abi.h`, which the host tests deliberately stay out of, so
`test_qemu_stdvga_list` mirrors the value locally with a comment naming the
source.

## 7. Still open elsewhere

- The 60 Hz refresh rate `dd16.c` publishes is a known falsehood that this
  pipeline makes worse before anything fixes it, since the modes it adds are
  disproportionately the high-resolution ones where real BIOSes run 87 Hz and
  above. Fixing it is deliberately out of scope: anything ending in *and then set
  a refresh rate* can put a mode on the only monitor a machine has. Recorded in
  the plan's DirectDraw section and as improvement S2 in the conformance spec.
- The per-family distrust predicate (S3's 360-wide FIFO defect) is specified and
  unimplemented. S3 collection is disabled at build time, so the first entry
  would ship unexercised.
- `prepare-vm-probe.ps1` still builds the mini-VDD with the default `vbe` probe
  list. Correct for a generic VM probe, worth knowing if it is aimed at another
  family.
