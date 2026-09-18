# Final Reality flicker: independent review and outstanding issues

Date: 2026-09-18  
Reviewed revision: `273bfa46f5776bd914f984c6016f2a8937655585`  
Scope: commits `d530edf` through `273bfa4`, surrounding HAL/mini-VDD paths, and the recorded intel80–83 results.  
Status: unresolved. This document proposes corrections and experiments; it does not claim a hardware fix.

## Assessment

The immediate priority is to establish a trustworthy GPU-completion measurement. The current breadcrumb implementation has defects in its counters, initialization, validation, and failure handling. A breadcrumb never observed is a failed measurement, not evidence that rendering necessarily took longer than the wait.

The CPU-only `/reuse` result is useful evidence against a simple buffer-reuse error under that probe's conditions. It does not establish that every game presentation is correct, identify the actual display fetch configuration, or prove late GPU rendering is the only remaining explanation.

The original black rendering and teardown fault should remain separate: the historical record attributes their resolution to moving the large draw scratch arrays off the stack and retaining the texture's local-surface identity without dereferencing a freed wrapper. Nothing reviewed here establishes a recurrence of those defects.

Primary local records:

- [Current flicker report](2026-09-18-final-reality-flicker-is-the-buffer-under-construction.md).
- [Historical rendering and fault report](2026-09-16-final-reality-renders-black-and-the-hal-faults.md).
- [Latch decision record](../decisions/2026-09-18-intel-plane-base-latches-at-vblank-start.md).

## Evidence and its limits

| Observation | Supported conclusion | Not established |
|---|---|---|
| Video is described as clear, sky, then ground becoming visible | The symptom is consistent with an unfinished frame being exposed | Which allocation was scanned or which operation caused exposure |
| `DrawsToFront=0` | Submitted target offsets differed from the sampled plane-base register | Disjoint memory ranges, actual live scanout identity, or completion before presentation |
| Base reads the requested value at completion | Register readback agrees with the request | Pixels have been displayed, or the register is the live latch |
| CPU `/reuse` showed no green | No visible retired-buffer contamination was detected in those trials | Sub-frame exclusion, identical GPU workload behavior, or exact latch timing |
| intel83 has 1,460 breadcrumb timeouts and zero successes | Expected values were not observed within the polling windows | Stores never executed, stores executed late, or reads observed the correct destination |
| `HwsPgaWritten == HwsPgaAfter` | The register read back the programmed address | A working GPU-to-CPU completion channel |

Raw files inspected in addition to the reports: `C:\temp\intel83\V9XSNAP.INI`, `INTELMM.TXT`, `INTELGTT.TXT`, and `V9XBOOT.INI`. They confirm the reported 1,460 timeouts, `HWS_PGA=0x7FEC0000`, ring start `0x006B0000`, and GMADR `0xD0000000`. The camera results above are taken from the issue record; this review did not independently classify the videos.

## Confirmed code findings

### R1 — P1: a completion timeout still releases the submission as successful

Locations: `src/display32/d3d/d3d_i9xx.c`, `v9x_d3d_i9xx_ring_submit` (approximately lines 424–449); `src/display32/ddhal_core.c`, `v9x_blt_drain`, `v9x_engine32`, `V9xHalLock`, and `v9x_flip_body`.

After the breadcrumb wait expires, submit increments its timeout counter and returns success. The draw path then clears `breadcrumb_expected` and counts the draw as submitted. No persistent completion obligation is retained for later CPU access or presentation.

This matters beyond the current batch:

- `v9x_engine32()` selects only the S3 engines; Intel gets no engine ops.
- `v9x_blt_drain()` therefore returns success for Intel without draining GPU work before CPU colour/depth fills and copies.
- Lock and Flip use `v9x_engine_status_validated()`, whose implementation checks specifically for the ViRGE. These waits do not provide Intel completion protection.
- Waiting for a pending *display flip* in the D3D path is a different dependency from waiting for previous *rendering* to finish.

This was workable only to the extent that every Intel draw really finished synchronously. If the late-rendering hypothesis is true, the timeout path permits precisely the CPU/GPU and presentation races the breadcrumb was meant to eliminate. The behavior is deliberately diagnostic in the current comments; it must not be treated as a correctness fix.

Resolution: retain the last outstanding GPU sequence and implement a common Intel completion/drain operation used before presentation and relevant CPU access. A timeout must leave completion unknown/pending or enter an explicit failure/recovery state. Returning failure from one draw alone is insufficient if later Flip or Lock can still proceed. Design this without an unbounded wait under the Win16 lock. Keep a diagnostic fail-open mode separately identifiable if it is needed to collect evidence.

### R2 — P2: `BreadcrumbLate` counts ordinary successful submissions

Location: `src/display32/d3d/d3d_i9xx.c:1297`.

Before the next batch, the code compares memory with the preceding sequence and increments `breadcrumb_late` on equality. It does not check whether that sequence timed out. A perfectly healthy run can therefore report roughly one late arrival per batch after the first.

Resolution: retain an explicit timed-out sequence and count its later observation exactly once. Track issued, completed-in-wait, timed-out, completed-after-timeout, and still-unresolved separately. Do not label a sequence late merely because it remains in memory. Also record the final batch after the workload stops: the present next-batch-only check cannot see its later arrival.

### R3 — P2: the status-page baseline and lifetime are not established

Location: `src/display32/d3d/d3d_i9xx.c:330`, `v9x_d3d_i9xx_hws_open`.

The CPU probe writes byte offset `0x84`; the breadcrumb is at `0x80`. The breadcrumb itself is not initialized. A stale page value can match a restarted software sequence, and an arbitrary nonzero value cannot automatically be classified as an older GPU completion.

The static `hws_ready` flag is never invalidated by DriverInit, mode changes, or descriptor changes. It also becomes true without requiring the HWS readback to match or the CPU probe to succeed. A later hardware reset/reprogramming would therefore leave software believing that setup is still valid. Such a reset was not demonstrated in intel83; the lifecycle omission is nevertheless visible in code.

Resolution: define ownership of the page and setup/teardown explicitly. Establish a baseline while the relevant engine is quiescent; do not clear a page that outstanding commands may still update. Validate the mapping and programmed address before marking it ready. Revalidate on session/mode/engine changes and preserve sequence identity across any outstanding work. A failed CPU probe or HWS readback should produce a setup failure, not thousands of identical per-draw waits.

### R4 — P2: the decoder does not enforce a completion marker

Location: `src/chipsets/intel/i9xx_3d_decode.c:1005`.

The decoder checks store length and offset, but not presence, uniqueness, position after the final drawing flush, or the absence of rendering after the store. A focused host probe against the current decoder returned `V9X_I9XX_P5_OK` (0) for all three cases:

```text
Licensed but missing: 0
Store before drawing: 0
Duplicate stores: 0
```

The missing case is compatible with the existing field's wording, which says the stream *may* contain a store. It is insufficient for a submission that will wait for one. The duplicate and early cases contradict the stated one-trailing-store contract. The current runtime builder emits the intended order; this finding concerns the validation boundary, not evidence that it presently emits a malformed order.

Resolution: distinguish permission from requirement. For a fenced submission, require exactly one expected sequence store to the approved offset after the final drawing flush; allow only harmless padding afterwards. Test absent, duplicate, early, wrong-value, wrong-offset, truncated, and post-store-rendering cases. Initialize appended limits in the 16-bit scene callers too: those callers currently leave `breadcrumb_offset` unset, so their intended no-store policy is not explicit.

### R5 — P2: the visual probe does not record enough failure information

Location: `tools/diag/ddraw_probe_win32.c`, `/reuse` loop near line 8470 and `v9x_fill_surface_marked`.

After Flip returns something other than WASSTILLDRAWING, the loop continues without verifying success. It likewise continues after GetFlipStatus errors or its 500 ms deadline. The fill helpers do not return a result to the stage controller. A failed green fill can therefore resemble a clean reuse result. The Flip retry itself has no elapsed-time bound.

Resolution: record each stage's successful flips, successful status completions, successful green writes, failures, and timeouts; abort or mark a stage inconclusive on failure. Measure the time from reported completion to the actual first green write, including Lock latency. A zero requested Sleep is not a zero-delay write. Correlate these results with before/after flip counters so a runtime fallback cannot silently stand in for the path under investigation.

This does not prove intel80's probe failed. It limits what can safely be concluded from that run.

## Hardware and interpretation questions still open

### H1 — Establish that GPU writes and CPU polls reach the same page

The current address chain is internally consistent for the captured machine:

```text
GPU ring GTT offset             0x006B0000
Status-page GTT offset          0x006C0000
CPU aperture physical address   0xD06C0000 = GMADR + status offset
GTT page index                 0x000006C0
Expected backing page          0x7FEC0000
HWS_PGA readback                0x7FEC0000
Breadcrumb byte offset         0x00000080
```

The mini-VDD maps the entire reserve through GMADR, not directly through the stolen-memory physical address. Its mapping covers the status page. The GTT capture's contiguous prefix covers this offset. There is no demonstrated arithmetic mismatch in the current INDEX path.

However, a CPU write/read through one mapping proves only that this CPU access reads back its own write. It does not establish that HWS DMA reaches that location. Start with a store-only GPU test, no rendering or flips, and a known initial sentinel. Capture the expected value, observed value, relevant GTT entry, HWS register, ring head/tail, and error registers. Only investigate a second CPU alias if the minimal test fails; account for the existing evidence that direct stolen-memory CPU access was problematic on this machine.

### H2 — Correct the cache argument without inventing a cache fault

The issue cites the UC MTRR at `0x7F800000` as proof that the CPU breadcrumb mapping is uncached. That MTRR covers the backing RAM, whereas the CPU accesses `0xD06C0080` through the aperture.

The inspected intel83 boot capture nevertheless supports UC at the aperture for a different reason: `def=00000c00` enables MTRRs with default type UC, the reported WB range ends below the aperture, and the remaining variable ranges do not cover it. The GTT capture independently reports uncached PTE attributes. These are separate layers and should be recorded separately. This review found no affirmative evidence of a cached breadcrumb mapping.

If further visibility investigation is needed, capture the effective CPU mapping attributes and their lifetime rather than adding speculative flushes. Intel's [memory-type documentation](https://www.intel.com/content/dam/support/us/en/documents/processors/pentium4/sb/25366821.pdf) describes MTRRs in terms of processor physical address ranges.

### H3 — Re-audit the retired IMM experiment by generation

The current INDEX opcode and byte-offset convention match Linux v4.4's `i9xx_add_request`. Its physical-status-page setup allocates and clears a page before use. See [intel_ringbuffer.c](https://raw.githubusercontent.com/torvalds/linux/v4.4/drivers/gpu/drm/i915/intel_ringbuffer.c).

The historical assertion that IMM is four dwords on this Gen3 device is not adequately established by a generic pre-Gen8 example. Linux v4.4's [i915_reg.h](https://raw.githubusercontent.com/torvalds/linux/v4.4/drivers/gpu/drm/i915/i915_reg.h) distinguishes `MI_STORE_DWORD_IMM` with length 1 from `MI_STORE_DWORD_IMM_GEN4` with length 2. That is a concrete source discrepancy requiring the exact 945 command definition and applicable generation branch to be checked. Do not silently treat the intel82 packet as proven correct, or use its failure to eliminate command encoding as a category.

This is not a recommendation to restore IMM now. Validate the current INDEX path first. The incorrect original address arithmetic is independently established; the precise instruction-level explanation of the intel81 lock was not captured on hardware.

### H4 — The display-path contradiction remains unresolved

The reported 2048-byte plane stride and 1024x576 pipe source disagree with the 1280-byte game targets and coherent probe pattern. The clean marker pattern argues against the simplest overlapping-fetch model under the tested conditions. It does not identify the actual route by which that pattern reached the panel.

Capture enabled planes, plane size/position, pipe timing and source, panel fitter, VGA routing/control, and flip operands at the same in-game point. Relate the actual fetch dimensions to allocation ranges; pipe timing height is not automatically framebuffer source height. Avoid writing additional display configuration merely to make readbacks look consistent.

Also correct the latch record's Linux citation. The inspected [upstream pipe-update implementation](https://kernel.googlesource.com/pub/scm/linux/kernel/git/stable/linux-stable/%2B/cc76ee75a9d3201eeacc576d17fbc1511f673010/drivers/gpu/drm/i915/intel_sprite.c) avoids the interval immediately before vblank; it does not deliberately choose the last 100 microseconds before vblank as a write window. This reference supports a timing guard, not the record's specific description of that guard. The exact v4.4 sprite file was unavailable through the web reader during this review.

### H5 — Timing and event order need direct measurement

Poll counts are not elapsed time. `BreadcrumbLagPollsMax=0` with zero successes says nothing about latency. All-timeout results cannot distinguish late, absent, or misobserved stores. The 60 fps camera result cannot exclude every millisecond-scale transient, particularly with unknown exposure timing and delay before the green write.

Add a compact bounded event buffer with: frame identifier, software timestamp, consistent hardware counter/scanline, flip source/target and flags, issued/completed render sequence, clear destination/pitch/rectangle, first draw target, and result/error. Include CPU colour and depth clears, texture Lock/Unlock, and any fallback presentation. Capture a short run; avoid file I/O in the timing-critical path.

The consistent frame-counter reader is an improvement. Its retry-limit exhaustion still returns a potentially inconsistent pair; return validity separately and keep a flip pending on invalid samples. This is a robustness issue, not a plausible explanation for regular flicker by itself.

## Recommended resolution sequence

1. **Repair the instrument on the host.** Fix R2–R5, specify R1's completion state, and test timeout/lifecycle transitions. Preserve the current two-tick setting during this work so presentation policy is not another changing variable.
2. **Prove one store without a game.** Initialize the owned page, submit the audited INDEX store with a unique sentinel transition, and wait with a measured deadline. Repeat a small bounded number of times. On failure, stop and capture state; do not multiply the same failure across thousands of triangles.
3. **Add a small GPU workload.** Render a known triangle or fill through a previously validated path, flush, store a sequence, and check both completion and resulting pixels. Compare CPU-prepared and GPU-prepared frames under otherwise identical presentation conditions.
4. **Test the ownership boundary.** Extend `/reuse` to the GPU-rendered case, requiring confirmed GPU completion before Flip and confirmed display completion before retired-buffer writes. Exercise colour clears, depth clears, and texture uploads as separate variants.
5. **Run Final Reality with a bounded event capture.** Require no unexplained completion timeouts and correlate clear/draw/flip order by frame. If flicker remains with proven GPU completion, reopen rendering/content and scanout-path questions using those events; do not infer another latch model from aggregate counters alone.

| Result | Next action |
|---|---|
| Store-only test fails | Investigate setup, opcode execution, destination and observation path; rendering latency has not been measured |
| Store-only succeeds, render-plus-store fails | Investigate render stream, flush ordering, and GPU error state |
| Completion works, GPU `/reuse` fails | Investigate ownership, CPU/GPU transitions and presentation ordering |
| Both controlled probes pass, game flickers | Use the game event trace and completed-frame content to locate the differing operation |
| Game passes with completion enforced | Repeat across mode/session transitions, then consider reducing excess waits |

## Outstanding work and closure criteria

Code items addressed 2026-09-18 after the review, all host-gated
(`run-checks` green) and none yet run on the netbook:

- [x] R1: a timed-out sequence stays outstanding (`BreadcrumbOutstanding`);
  `v9x_d3d_i9xx_render_drain` is called before Flip, Lock and every CPU
  fill or copy on Intel (`v9x_render_drain` in `ddhal_core.c`, the gap
  the review named: Intel had no engine ops and no idle wait at all).
  Each call is one bounded poll and answers WASSTILLDRAWING so the wait is
  DirectDraw's retry loop, never unbounded here. Past 4,000,000 polls on
  one sequence the channel is declared dead (`BreadcrumbAbandoned`),
  breadcrumbs stop, and the fact is in the snapshot. The per-batch
  timeout still returns the draw as submitted; that is the fail-open
  diagnostic mode, separately identifiable by `BreadcrumbTimeouts` with
  `BreadcrumbOutstanding` non-zero.
- [x] R2: `BreadcrumbLate` now counts a timed-out sequence later observed,
  once, by the drain or by the next batch's non-waiting drain; issued,
  completed-in-wait, timed-out, completed-after-timeout, outstanding and
  abandoned are separate fields. The final batch is resolved by the drain
  at the next Flip or Lock.
- [x] R3: `v9x_d3d_i9xx_hws_open` is a state machine (untried, ready,
  failed) reset by DriverInit. It requires the GTT entry, the CPU probe
  read-back, a zero baseline in the breadcrumb dword written while
  nothing is in flight, an HWS_PGA read-back equal to the write, and a
  store-only round trip with a sentinel (`HwsSelfTest`, `HwsSelfTestPolls`,
  bound 2,000,000 polls) before any batch carries a breadcrumb. Any
  failure fails the channel once; batches then go without a breadcrumb.
- [x] R4: a licensed stream must carry exactly one MI_STORE_DWORD_INDEX,
  directly behind an MI_FLUSH, with nothing but MI_NOOP after it; absent,
  duplicate, early, unflushed, wrong-offset and truncated cases are
  refused and tested. `v9x_i9xx_decode_limits_clear` zeroes every field
  and the two 16-bit scene callers use it.
- [x] R5: each `/reuse` stage records flips ok and in error, completions
  ok, timed out and in error, green fills ok and in error, the measured
  time from reported completion to the first green pixel (min and max),
  and a `Conclusive` flag that is 0 on any failure. The Flip retry is
  bounded at two seconds.
- [ ] H1: the store-only round trip is built into `hws_open` and will run
  on the next boot; not yet measured.
- [x] H3: the IMM history is marked unresolved in `intel_gma.h` and the
  issue record; neither the length-1 nor the length-2 form is treated as
  established, and the decoder accepts neither.
- [ ] H4: not addressed; the register capture of intel80 stands as an open
  discrepancy.
- [ ] H5: the frame-correlated event trace is not built. The frame-counter
  reader now reports validity and a flip stays pending on an inconsistent
  sample.
- [x] The latch record's i915 citation is corrected to a guard around the
  vblank start rather than a preferred write window.
- [ ] Final Reality completes repeated runs without construction flicker or unexplained completion failures, including a mode/session transition.
- [ ] Existing CPU probe and known GPU scenes still pass; the original rendering and teardown fixes remain intact.

Prioritize reliable completion and ownership first. The display-layout discrepancy may remain a separately tracked explanation gap if controlled presentation and game testing establish correctness, but it should not be declared understood merely because the picture looks coherent.

## Review validation

The repository host suite (`scripts/build-host.ps1`) and tree check (`scripts/check-tree.ps1`) both passed during this review. A focused decoder probe, stored locally under `build/review-flicker`, also built and ran, reproducing the three acceptance cases above. No GPU commands were executed on the netbook during this review. No driver fixes are included in this document change.

Host decoder tests validate the software's rules. They cannot establish that hardware accepts a packet or that a completion write orders the intended rendering. Passing `run-checks` must therefore remain separate from hardware validation in the boot record.
