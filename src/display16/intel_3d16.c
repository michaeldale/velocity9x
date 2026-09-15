/*
 * Phase 5's sequencer: fill the target, submit one triangle, report what
 * landed.
 *
 * Behind its OWN positive guard, V9X_I9XX_PHASE5_EXECUTOR, separate from
 * Phase 4's. That separation is what lets Phase 5 be built dark - every host
 * gate green, the code compiled and audited, and no path by which it can
 * reach the ring - which is how every Intel phase has been staged and is why
 * Phase 4 cost eight boots rather than more.
 *
 * Entered by the two-phase dispatcher ONLY when Phase 4 passed in the same
 * boot. That makes "revalidate the layout before drawing" a precondition the
 * code enforces rather than an instruction an operator remembers. Phase 4 is
 * the only thing that can distinguish "the layout move broke the ring" from
 * "the 3D packets hung the parser", and it costs milliseconds.
 *
 * An unarmed boot still produces a complete INTEL3D0.TXT. It writes nothing:
 * it hashes the untouched target twice, samples it through V9xGmadrRead to
 * prove the address path, and records both stream plans. A decoder error is
 * therefore caught on B1 and costs no armed boot.
 */
#define SetCursor V9xUserSetCursor
#include <windows.h>
#undef SetCursor

#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"
#include "velocity9x/intel_gma.h"
#include "velocity9x/intel_gen3_3d.h"
#include "velocity9x/intel16.h"

extern DWORD v9x_i9xx_bsm;
extern DWORD v9x_i9xx_hash_pass_a;
extern DWORD v9x_i9xx_hash_pass_b;
extern DWORD v9x_i9xx_hash_fail;
extern DWORD v9x_i9xx_gmadr_bar2;
extern DWORD v9x_i9xx_gtt_backed_prefix;
extern DWORD v9x_i9xx_gtt_reserve_first;
extern DWORD v9x_i9xx_gtt_reserve_count;
extern DWORD v9x_i9xx_ring_stage_fail;
extern DWORD v9x_i9xx_ring_exec_failure;
extern DWORD v9x_i9xx_ring_exec_head;
extern DWORD v9x_i9xx_ring_exec_tail;
extern DWORD v9x_i9xx_ring_exec_polls;
extern DWORD v9x_i9xx_ring_exec_elapsed;
/*
 * The chained arm transaction, begun by intel_exec16.c when the consumed token
 * claims Phase 5. This unit is the only thing that may complete it: the draw
 * result is the single point at which the token is retired.
 */
extern struct v9x_i9xx_chain v9x_intel_arm_chain;
extern WORD v9x_intel_boot_arm_phase;
extern WORD v9x_intel_boot_arm_retire(const char *result);

extern void FAR PASCAL V9xEnsureDiagDir(void);
extern DWORD FAR PASCAL V9xGmadrRead(DWORD offset);
extern WORD FAR PASCAL V9xMiniI9xxRingHash(DWORD offset, WORD count_high,
                                           WORD count_low);
extern WORD FAR PASCAL V9xMiniI9xxP5Stage(DWORD physical, WORD index,
                                          DWORD value);
extern WORD FAR PASCAL V9xMiniI9xxRingExecute(DWORD crc, WORD step);
/* Phase 6. Same returns, same globals, plus the scene it belongs to. */
extern WORD FAR PASCAL V9xMiniI9xxSceneExecute(DWORD crc, WORD step,
                                               WORD scene);
extern WORD FAR PASCAL V9xMiniI9xxSceneStage(DWORD base, WORD index,
                                             DWORD value, WORD scene);
extern WORD FAR PASCAL V9xMiniI9xxRingDiag(WORD index);
extern DWORD v9x_i9xx_ring_diag_value;

#define V9X_P5_SECTION "Intel3D"

/*
 * Preflight refusals. Its own reason space, deliberately disjoint from
 * Phase 4's: a capture that says "refused 4" must never be ambiguous about
 * which phase refused.
 */
#define V9X_P5_PRE_OK              0u
#define V9X_P5_PRE_NOT_ARMED       1u
#define V9X_P5_PRE_PHASE4_MISSING  2u
#define V9X_P5_PRE_LAYOUT          3u
#define V9X_P5_PRE_BSM             4u
#define V9X_P5_PRE_RESERVE_BACKING 5u
#define V9X_P5_PRE_BUILD           6u
#define V9X_P5_PRE_DECODE          7u
#define V9X_P5_PRE_CRC             8u
#define V9X_P5_PRE_TARGET_RANGE    9u
/*
 * The mini-VDD cannot yet stage or submit a Phase 5 stream.
 *
 * Staging arms 20-24 belong inside V9xMini_I9xx_Ring_Stage, with a second
 * table and a second counter so a Phase 4 dword can never land in a Phase 5
 * slot. That work is not done, so there is no path from here to the ring.
 *
 * The armed path refuses on this rather than filling the target and stopping.
 * A fill with no draw would consume the one-shot token, write 600 KiB, and
 * prove nothing - it would burn an armed boot to produce a uniform rectangle.
 * Refusing costs nothing and says exactly why.
 */
#define V9X_P5_PRE_SUBMIT_MISSING 10u
/*
 * The consumed token does not claim Phase 5.
 *
 * The boot latch records that SOME valid one-shot token was transferred, not
 * what it authorises, so this is the check that stops a Phase 4 stick reaching
 * a 3D draw. The errata decision covering a Phase 5 draw is dated 2026-09-15;
 * the Phase 4 one, deliberately, does not cover it.
 */
#define V9X_P5_PRE_PHASE          11u
/*
 * The chain is not in the state a draw may be reported from.
 *
 * Reaching here means the token claimed Phase 5 but the replay did not
 * complete - the chained arm refused, or Phase 4 failed, or the chain was
 * never begun at all. A Phase 5 token cannot skip a failed replay, and this is
 * where that is enforced on the driver side; v9x_i9xx_chain_draw_done enforces
 * it again in the pure logic the host tests cover.
 */
#define V9X_P5_PRE_CHAIN          12u

/*
 * Steps 20-29, disjoint from Phase 4's 1-12 so a hang is attributable to a
 * phase from the number alone.
 *
 * 22-26 are ALSO the mini-VDD's execute selectors, and that is deliberate:
 * IntentStep carries the same number the driver is about to ask the mini-VDD
 * for, exactly as Phase 4 aligns S05-S12 with its own selectors. A capture
 * saying IntentStep=24 and a mini-VDD failure at step 24 therefore name the
 * same thing. They did not, briefly, and that ambiguity is the whole reason
 * the numbering is written down here.
 *
 * 20, 21 and 27-29 are driver-side work with no mini-VDD step behind them.
 */
#define V9X_P5_STEP_PREFLIGHT     20u
#define V9X_P5_STEP_STAGE         21u
#define V9X_P5_STEP_VERIFY        22u
#define V9X_P5_STEP_PROGRAM       23u
#define V9X_P5_STEP_PROBE         24u
#define V9X_P5_STEP_DRAW          25u
#define V9X_P5_STEP_TEARDOWN      26u
/*
 * 27 and 28 are RETIRED, not free. They were the full-target hash and the 480
 * row CRCs, removed on 2026-09-15 after the hash hard locked the netbook. The
 * numbers are kept reserved because IntentStep values appear in captures that
 * already exist, and reusing 27 for something else would make an old capture
 * read as a new step.
 */
#define V9X_P5_STEP_HASH          27u
#define V9X_P5_STEP_ROWS          28u
#define V9X_P5_STEP_PIXELS        29u

/*
 * Phase 6 steps, one set per scene, matching the mini-VDD's 32-36 exactly
 * as Phase 5's 22-26 match its own. IntentStep carries the same number the
 * executor is about to be asked for, so a capture that stops names the step
 * without a cross-reference.
 */
#define V9X_P6_STEP_VERIFY        32u
#define V9X_P6_STEP_PROGRAM       33u
#define V9X_P6_STEP_PROBE         34u
#define V9X_P6_STEP_DRAW          35u
#define V9X_P6_STEP_TEARDOWN      36u
/* Staging is its own step, before the five, so a hang inside the staging
 * loop is not reported as a verify that never ran. */
#define V9X_P6_STEP_STAGE         31u

/*
 * Shift-add 32-bit multiply. Open Watcom would otherwise call __U4M, which
 * lives in clibc.lib's _TEXT and cannot be reached by a near call from the
 * I9XXCODE segment this unit is compiled into. The linker refuses it (E2052),
 * which is the fourth time that gate has caught a helper reference no grep
 * would have found.
 */
static DWORD v9x_p5_mul32(DWORD left, DWORD right)
{
    DWORD result = 0ul;

    while (right != 0ul) {
        if ((right & 1ul) != 0ul) {
            result += left;
        }
        left <<= 1;
        right >>= 1;
    }

    return result;
}

static WORD v9x_p5_rejection;

/*
 * The scene under construction, and the running aperture-read count.
 *
 * File-static rather than a local: the struct carries five triangles and
 * fourteen probes, and this is a Win16 driver whose stack is not somewhere to
 * put a hundred and fifty bytes casually.
 *
 * v9x_p5_reads is CUMULATIVE across every scene, because that is the number
 * the aperture hazard is about. Three single reads succeeded on this part and
 * 153,600 hung; a per-scene count would say nothing about where a five-scene
 * boot actually stopped.
 */
static struct v9x_i9xx_scene v9x_p6_scene;

/*
 * Every scene's dwords, in execution order, as the driver actually built
 * them.
 *
 * This exists so the combined CRC handed to the chain is a REAL comparison.
 * The obvious version - passing v9x_i9xx_scene_combined_crc() as both the
 * staged value and the expected one - compares a number with itself and
 * passes for any stream whatsoever, which is the defect this file keeps
 * finding in its own checks. Accumulating the bytes is what makes the two
 * sides independent: one is what was built and staged here, the other is the
 * constant generated from the builders.
 *
 * Sized for five scenes at the longest a scene can be. A scene that would
 * overflow it stops the run rather than silently CRCing a truncated record
 * of what was submitted.
 */
#define V9X_P6_ALL_DWORDS 512u
static DWORD v9x_p6_all[V9X_P6_ALL_DWORDS];
static DWORD v9x_p6_all_used;
static DWORD v9x_p5_reads;
static DWORD v9x_p5_stream[160];
static DWORD v9x_p5_stream_dwords;

static void v9x_p5_text(const char *key, const char *value)
{
    WritePrivateProfileString(V9X_P5_SECTION, key, value,
                              V9X_DIAG_INTEL3D0_TXT);
}

/*
 * Commit the profile cache to disk.
 *
 * Every write above sits in Windows' cache until this is called, so a boot
 * that does not finish leaves NOTHING in INTEL3D0.TXT - not even the file's
 * first key. Measured on the netbook 2026-09-15: a hang inside this sequencer
 * left a 32-byte file holding an unrelated stale cluster, and IntentStep, the
 * entire mechanism for locating a hang on a machine with no serial port, had
 * never reached the disk on any boot.
 *
 * v9x_boot_trace in ddi.c carries a comment explaining exactly this, written
 * after it cost two boots in September 2026. This unit was built without it.
 *
 * Not called from v9x_p5_text itself: the row-CRC and stream tables write
 * several hundred keys, and committing after each would turn a capture into a
 * disk-bound crawl. It is called where a hang has to be locatable - every
 * intent marker, every progress marker, and every terminal result.
 */
static void v9x_p5_flush(void)
{
    WritePrivateProfileString(0, 0, 0, V9X_DIAG_INTEL3D0_TXT);
}

/* A result, then a commit: the last thing written must survive the boot. */
static void v9x_p5_result(const char *value)
{
    v9x_p5_text("Result", value);
    v9x_p5_flush();
}

/*
 * A committed progress marker inside a single IntentStep.
 *
 * IntentStep names the step; between step 20 and the next commit the
 * sequencer builds the stream, publishes several hundred keys, and then makes
 * four separate hardware touches - the heap probe, the two guard reads, the
 * 614 KiB reserve hash through the mini-VDD, and two target samples. A hang
 * anywhere in that run leaves IntentStep=20 and says nothing about which.
 *
 * Measured on the netbook 2026-09-15: that is exactly what happened, and the
 * capture could not distinguish a pure-computation fault from an aperture read.
 * These markers cost one profile commit each on a path that already writes
 * hundreds of keys.
 */
static void v9x_p5_progress(const char *where)
{
    v9x_p5_text("B1Step", where);
    v9x_p5_flush();
}

static void v9x_p5_hex_into(char *text, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    WORD index;

    for (index = 0u; index < 8u; ++index) {
        text[index] = digits[(WORD)((value >> (28 - index * 4)) & 0xful)];
    }
    text[8] = '\0';
}

static void v9x_p5_hex(const char *key, DWORD value)
{
    char text[9];

    v9x_p5_hex_into(text, value);
    v9x_p5_text(key, text);
}

/* Key plus a four-digit index, for the per-dword and per-row tables. */
static void v9x_p5_indexed_hex(const char *prefix, WORD index, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char key[24];
    WORD at = 0u;

    while (prefix[at] != '\0' && at < 16u) {
        key[at] = prefix[at];
        ++at;
    }
    key[at++] = digits[(index >> 12) & 0xfu];
    key[at++] = digits[(index >> 8) & 0xfu];
    key[at++] = digits[(index >> 4) & 0xfu];
    key[at++] = digits[index & 0xfu];
    key[at] = '\0';
    v9x_p5_hex(key, value);
}

/*
 * IntentStep is flushed BEFORE each action, never after. A hang therefore
 * leaves the number of the step that was about to run, not the last one that
 * finished - which is the difference between naming the cause and naming its
 * predecessor.
 */
/*
 * The read-only error and ring registers, through the mini-VDD's diagnostic
 * verb. The same nine offsets Phase 4 reads: EIR, EMR, ESR, PGTBL_ER, the
 * instruction-error trio, then RING_CTL and RING_START.
 *
 * Phase 5 did not read these, and on the first successful draw - 2026-09-15,
 * build 4628b66 - that was the gap that mattered. The GPU consumed all 66
 * dwords, the fill landed, and no triangle appeared, and the capture could not
 * say whether the parser had rejected the primitive or accepted it and
 * rasterised nothing. Those are different faults with different fixes.
 *
 * Nine reads. Bounded, and nowhere near the scale that hangs this part.
 */
/* prefix + suffix, for the per-capture status keys. */
static void v9x_p5_status_key(char *key, const char *prefix,
                              const char *suffix)
{
    v9x_intel_str_copy(key, prefix);
    v9x_intel_str_append(key, suffix);
}

/*
 * The read-only error and ring registers, through the mini-VDD's diagnostic
 * verb. Nine offsets: EIR, EMR, ESR, PGTBL_ER, the instruction-error trio,
 * then RING_CTL and RING_START.
 *
 * Phase 5 did not read these, and on the first successful draw - 2026-09-15,
 * build 4628b66 - that was the gap that mattered. The GPU consumed all 66
 * dwords, the fill landed, and no triangle was observed at the probed points,
 * and the capture could not say whether the parser had rejected the primitive
 * or accepted it. Those are different faults with different fixes.
 *
 * The COMPLETENESS is published, not just the values. An earlier version wrote
 * FFFFFFFF and returned silently when the verb refused, so a capture with four
 * registers in it looked like a capture with nine, and the checker had no
 * field to test. A diagnostic that degrades quietly is worse than one that is
 * absent, because absence is visible.
 *
 * Nine reads. Bounded, and nowhere near the scale that hangs this part.
 */
static void v9x_p5_capture_errors(const char *prefix)
{
    WORD index;
    char key[24];

    for (index = 0u; index < 9u; ++index) {
        if (V9xMiniI9xxRingDiag(index) == 0u) {
            v9x_p5_indexed_hex(prefix, index, 0xfffffffful);
            v9x_p5_status_key(key, prefix, "Ok");
            v9x_p5_text(key, "0");
            v9x_p5_status_key(key, prefix, "Count");
            v9x_p5_hex(key, (DWORD)index);
            v9x_p5_status_key(key, prefix, "FailIndex");
            v9x_p5_hex(key, (DWORD)index);
            v9x_p5_flush();
            return;
        }
        v9x_p5_indexed_hex(prefix, index, v9x_i9xx_ring_diag_value);
    }
    v9x_p5_status_key(key, prefix, "Ok");
    v9x_p5_text(key, "1");
    v9x_p5_status_key(key, prefix, "Count");
    v9x_p5_hex(key, 9ul);
    v9x_p5_status_key(key, prefix, "FailIndex");
    v9x_p5_hex(key, 0xfffffffful);
    v9x_p5_flush();
}

/*
 * Every aperture read goes through here, and the running total reaches disk
 * before the read rather than after.
 *
 * That ordering is the whole point. If the read locks the machine, the
 * capture already says which read it was - and the count is what the next
 * boot's budget is set from. A total written afterwards would be missing
 * exactly the entry that mattered.
 */
static DWORD v9x_p5_read_counted(DWORD offset)
{
    v9x_p5_reads++;
    v9x_p5_hex("ApertureReads", v9x_p5_reads);
    v9x_p5_flush();
    return V9xGmadrRead(offset);
}

/*
 * "S<scene><name>" - the capture key for a per-scene value.
 *
 * Scenes share probe names by design: the three edge scenes probe the same
 * pixels so their coverage can be compared, and without a per-scene prefix
 * the later ones would overwrite the earlier in the profile and the
 * comparison would silently be a scene against itself.
 */
static void v9x_p6_key(char *key, WORD scene, const char *name)
{
    WORD at = 0u;

    key[at++] = 'S';
    key[at++] = (char)('0' + (scene % 10u));
    key[at] = '\0';
    v9x_intel_str_append(key, name);
}

static void v9x_p6_hex(WORD scene, const char *name, DWORD value)
{
    char key[32];

    v9x_p6_key(key, scene, name);
    v9x_p5_hex(key, value);
}

static void v9x_p6_text(WORD scene, const char *name, const char *value)
{
    char key[32];

    v9x_p6_key(key, scene, name);
    v9x_p5_text(key, value);
}

/*
 * What a probe expected, as the word the capture carries.
 *
 * MEASURE is published as "measure" rather than left blank: a probe with no
 * expectation and a probe whose expectation was lost look identical in a
 * capture, and only one of them is evidence.
 */
static const char *v9x_p6_expectation(WORD expect)
{
    if (expect == V9X_I9XX_PROBE_MEASURE) { return "measure"; }
    if (expect == V9X_I9XX_PROBE_FILL) { return "outside"; }
    if (expect == V9X_I9XX_PROBE_TRIANGLE0) { return "inside"; }
    if (expect == V9X_I9XX_PROBE_TRIANGLE1) { return "inside1"; }
    return "unknown";
}

static void v9x_p5_intent(WORD step)
{
    v9x_p5_hex("IntentStep", (DWORD)step);
    /* Committed, or the marker names the step on a boot that completes and
     * says nothing at all on the only boot that needed it. */
    v9x_p5_flush();
}

/*
 * Mapping, backing and bounds - everything that must hold before ANY aperture
 * access, armed or not.
 *
 * Extracted from the preflight so it can run first on both paths. It used to
 * live behind the preflight's `armed` test, so an unarmed boot read the
 * aperture without any of it having been checked: the no-write path was not
 * the no-validation path, but it was written as though writing were the only
 * thing worth gating.
 *
 * Reads nothing. Every check here is against a value already in memory.
 */
static WORD v9x_p5_validate_mapping(
    const struct v9x_i9xx_sandbox_layout *layout)
{
    if (layout->reserve_offset != 0x006b0000ul ||
        layout->target_offset != 0x006c2000ul ||
        layout->target_pitch != V9X_I9XX_TARGET_PITCH ||
        layout->target_bytes != V9X_I9XX_TARGET_BYTES ||
        layout->guard_upper_offset != 0x00758000ul) {
        return V9X_P5_PRE_LAYOUT;
    }
    if (v9x_i9xx_bsm != 0x7f800000ul) { return V9X_P5_PRE_BSM; }

    /* Re-derived every boot rather than inherited from the Phase 2 capture. */
    if (v9x_i9xx_gtt_reserve_count == 0ul ||
        v9x_i9xx_gtt_reserve_first >
            0xfffffffful - v9x_i9xx_gtt_reserve_count ||
        v9x_i9xx_gtt_reserve_first + v9x_i9xx_gtt_reserve_count >
            v9x_i9xx_gtt_backed_prefix) {
        return V9X_P5_PRE_RESERVE_BACKING;
    }

    /* The target and its guard must sit inside the reserve. */
    if (layout->target_offset < layout->reserve_offset ||
        layout->guard_upper_offset + V9X_I9XX_SANDBOX_PAGE_BYTES >
            layout->reserve_offset + V9X_I9XX_GTT_RESERVE_BYTES) {
        return V9X_P5_PRE_TARGET_RANGE;
    }
    return V9X_P5_PRE_OK;
}

static WORD v9x_p5_preflight(const struct v9x_i9xx_sandbox_layout *layout,
                             WORD phase4_passed, WORD armed)
{
    DWORD built_crc;
    DWORD rejected_index = 0ul;
    WORD reason;

    if (armed == 0u) { return V9X_P5_PRE_NOT_ARMED; }

    /*
     * Authorise THIS phase rather than inheriting the boot latch. The latch
     * says a token was transferred; these two say it was transferred for a
     * draw, and that the replay it depends on actually happened.
     */
    if (v9x_intel_boot_arm_phase != V9X_I9XX_PHASE5 &&
        v9x_intel_boot_arm_phase != V9X_I9XX_PHASE6) {
        return V9X_P5_PRE_PHASE;
    }
    if (v9x_intel_arm_chain.state != V9X_I9XX_CHAIN_STATE_REPLAYED) {
        return V9X_P5_PRE_CHAIN;
    }

    /* The hard precondition: Phase 4 must have passed in THIS boot. */
    if (phase4_passed == 0u) { return V9X_P5_PRE_PHASE4_MISSING; }

    {
        WORD mapping = v9x_p5_validate_mapping(layout);

        if (mapping != V9X_P5_PRE_OK) { return mapping; }
    }

    if (v9x_i9xx_build_phase5_stream(
            v9x_p5_stream,
            (DWORD)(sizeof(v9x_p5_stream) / sizeof(v9x_p5_stream[0])),
            &v9x_p5_stream_dwords) != V9X_STATUS_OK) {
        return V9X_P5_PRE_BUILD;
    }

    /*
     * Two comparisons that close the last drift path inside the driver: the
     * stream we are about to submit must decode under our own allowlist, and
     * its CRC must equal the constant generated from the same builders. If
     * either fails, what would run is not what was reviewed.
     */
    reason = v9x_i9xx_decode_phase5_stream(
        v9x_p5_stream, v9x_p5_stream_dwords,
        layout->target_offset, layout->target_bytes, &rejected_index);
    if (reason != V9X_I9XX_P5_OK) {
        v9x_p5_hex("PreDecodeReason", (DWORD)reason);
        v9x_p5_hex("PreDecodeIndex", rejected_index);
        return V9X_P5_PRE_DECODE;
    }
    built_crc = v9x_i9xx_crc32_dwords(v9x_p5_stream, v9x_p5_stream_dwords);
    if (built_crc != v9x_i9xx_phase5_execution_crc()) {
        v9x_p5_hex("PreBuiltCrc", built_crc);
        return V9X_P5_PRE_CRC;
    }
    return V9X_P5_PRE_OK;
}

/* The stream, one dword per key, plus an index a reader can navigate by. */
static void v9x_p5_publish_stream(void)
{
    WORD index;

    v9x_p5_hex("StreamDwords", v9x_p5_stream_dwords);
    for (index = 0u; index < (WORD)v9x_p5_stream_dwords; ++index) {
        v9x_p5_indexed_hex("S", index, v9x_p5_stream[index]);
    }
    /*
     * Packet offsets, so a reader can find the drawing rectangle without
     * counting dwords. Derived from the builders' own extents rather than
     * from constants typed here, so they cannot disagree with the stream
     * above.
     */
    v9x_p5_hex("OffsetFill", 0ul);
    v9x_p5_hex("LengthFill", v9x_i9xx_phase5_fill_extent());
    v9x_p5_hex("OffsetState", v9x_i9xx_phase5_fill_extent());
    v9x_p5_hex("LengthState", v9x_i9xx_3d_state_extent());
    v9x_p5_hex("OffsetShader",
               v9x_i9xx_phase5_fill_extent() + v9x_i9xx_3d_state_extent());
    v9x_p5_hex("LengthShader", v9x_i9xx_fragment_program_extent());
    v9x_p5_hex("OffsetProbe",
               v9x_i9xx_phase5_fill_extent() +
               v9x_i9xx_3d_state_extent() +
               v9x_i9xx_fragment_program_extent());
    v9x_p5_hex("LengthProbe", 2ul);
    v9x_p5_hex("OffsetVertices",
               v9x_i9xx_phase5_fill_extent() +
               v9x_i9xx_3d_state_extent() +
               v9x_i9xx_fragment_program_extent() + 2ul);
    v9x_p5_hex("LengthVertices", v9x_i9xx_vertex_run_extent());
}

/*
 * The vertices twice: raw bits and decoded integers.
 *
 * Deliberately redundant. A float-transport fault would otherwise show up as a
 * wrong picture, which is the hardest thing to diagnose from a capture; with
 * both, the artefact itself says whether the bits or the geometry were wrong.
 */
static void v9x_p5_publish_vertices(void)
{
    WORD vertex;
    /*
     * The first vertex dword: the fill prefix, the state block, the fragment
     * program, the two probe dwords and the _3DPRIMITIVE header.
     *
     * The fill prefix was missing here until 2026-09-15, so this read dwords
     * 44-47 of the stream - fragment-program and probe words - and published
     * them as vertex bits. The capture said the geometry was wrong when the
     * stream was correct.
     */
    DWORD base = v9x_i9xx_phase5_fill_extent() +
                 v9x_i9xx_3d_state_extent() +
                 v9x_i9xx_fragment_program_extent() + 3ul;

    for (vertex = 0u; vertex < (WORD)V9X_I9XX_VERTEX_COUNT; ++vertex) {
        DWORD decoded = 0ul;
        WORD field;

        for (field = 0u; field < 4u; ++field) {
            DWORD bits = v9x_p5_stream[base + field];
            v9x_p5_indexed_hex("VB", (WORD)(vertex * 8u + field), bits);
            if (v9x_i9xx_float_to_int(bits, &decoded) == V9X_I9XX_FLOAT_OK) {
                v9x_p5_indexed_hex("VD", (WORD)(vertex * 8u + field), decoded);
            } else {
                v9x_p5_indexed_hex("VD", (WORD)(vertex * 8u + field),
                                   0xfffffffful);
            }
        }
        v9x_p5_indexed_hex("VC", vertex, v9x_p5_stream[base + 4ul]);
        base += V9X_I9XX_VERTEX_DWORDS;
    }
}

/* Two read-only passes, both published. They are not compared here: an
 * unstable read must be visible as two different numbers in the artefact. */
/*
 * An explicit, small set of target points.
 *
 * This replaces a 153,600-dword two-pass hash that hard locked the netbook on
 * 2026-09-15. Reading each point twice establishes SAMPLE stability - that
 * these addresses return the same value twice - and nothing whatever about the
 * rest of the target. The old capture keys claimed more than that and the
 * names here are deliberately narrower.
 *
 * Eight points: the four corners, the centre, and three interior points. They
 * cover the extremes of the mapping rather than characterising a picture; the
 * named pixel probes are what say anything about what was drawn.
 *
 * Sixteen reads. That is a conservative starting figure, not a proven-safe
 * one - see plans\intel-phase5-bounded-readback.md. Nothing has established
 * where the safe bound actually lies.
 */
struct v9x_p5_sample {
    WORD x;
    WORD y;
};
static const struct v9x_p5_sample v9x_p5_samples[8] = {
    { 0u,   0u   }, { 638u, 0u   },
    { 0u,   479u }, { 638u, 479u },
    { 320u, 240u }, { 160u, 120u },
    { 480u, 360u }, { 320u, 400u }
};

static DWORD v9x_p5_point_offset(
    const struct v9x_i9xx_sandbox_layout *layout, WORD x, WORD y)
{
    /* Two pixels per dword at 16 bpp, so the dword address is the row origin
     * plus the column pair. */
    return layout->target_offset +
           v9x_p5_mul32((DWORD)y, layout->target_pitch) +
           (((DWORD)x & ~1ul) << 1);
}

/*
 * Each point read twice, with a committed marker before and after, so a lock
 * names the exact sample rather than the whole set.
 */
static WORD v9x_p5_sample_target(
    const struct v9x_i9xx_sandbox_layout *layout)
{
    WORD index;
    WORD stable = 1u;
    const WORD count =
        (WORD)(sizeof(v9x_p5_samples) / sizeof(v9x_p5_samples[0]));

    for (index = 0u; index < count; ++index) {
        DWORD offset = v9x_p5_point_offset(layout, v9x_p5_samples[index].x,
                                           v9x_p5_samples[index].y);
        DWORD first;
        DWORD second;

        v9x_p5_hex("SampleNext", (DWORD)index);
        v9x_p5_hex("SampleNextOffset", offset);
        v9x_p5_flush();
        first = V9xGmadrRead(offset);
        second = V9xGmadrRead(offset);
        v9x_p5_indexed_hex("SA", index, first);
        v9x_p5_indexed_hex("SB", index, second);
        if (first != second) { stable = 0u; }
        v9x_p5_flush();
    }
    v9x_p5_hex("SampleCount", (DWORD)count);
    v9x_p5_hex("SampleReads", (DWORD)(count * 2u));
    /* Named for what it is. It is not target stability. */
    v9x_p5_text("SampleStable", stable != 0u ? "1" : "0");
    v9x_p5_flush();
    return stable;
}

/*
 * Named per-pixel assertions. The centroid, one point inside each vertex, and
 * each edge midpoint pulled toward the centroid; then four corners and three
 * outside points that must still hold the fill.
 *
 * Deliberately away from the edges themselves: the plan licenses a one-pixel
 * band along an edge to disagree with the software reference, because the fill
 * rule is not something this audit established, so sampling an edge would
 * manufacture a failure the plan already excused.
 */
struct v9x_p5_probe {
    const char *name;
    WORD x;
    WORD y;
    WORD inside;
};

static const struct v9x_p5_probe v9x_p5_probes[] = {
    { "Centroid",   320u, 213u, 1u },
    { "NearV0",     175u, 128u, 1u },
    { "NearV1",     465u, 128u, 1u },
    { "NearV2",     320u, 385u, 1u },
    { "MidTop",     320u, 130u, 1u },
    { "MidLeft",    250u, 250u, 1u },
    { "MidRight",   390u, 250u, 1u },
    { "Corner00",     0u,   0u, 0u },
    { "CornerX0",   639u,   0u, 0u },
    { "Corner0Y",     0u, 479u, 0u },
    { "CornerXY",   639u, 479u, 0u },
    { "OutsideTop", 320u,  40u, 0u },
    { "OutsideLeft", 40u, 400u, 0u },
    { "OutsideRight",600u,400u, 0u }
};

static void v9x_p5_publish_pixels(const struct v9x_i9xx_sandbox_layout *layout)
{
    WORD index;
    const WORD count =
        (WORD)(sizeof(v9x_p5_probes) / sizeof(v9x_p5_probes[0]));

    for (index = 0u; index < count; ++index) {
        /* Two pixels per dword at 16 bpp, so the dword address is the row
         * origin plus the column pair. */
        DWORD offset = layout->target_offset +
                       v9x_p5_mul32((DWORD)v9x_p5_probes[index].y,
                                    layout->target_pitch) +
                       (((DWORD)v9x_p5_probes[index].x & ~1ul) << 1);
        v9x_p5_text(v9x_p5_probes[index].name,
                    v9x_p5_probes[index].inside != 0u ? "inside" : "outside");
        /*
         * Committed either side of the read. These fourteen reads are the
         * whole of the draw evidence now, so losing the set to a lock on the
         * eleventh would waste an armed boot that had already succeeded.
         */
        v9x_p5_hex("PixelNext", (DWORD)index);
        v9x_p5_flush();
        v9x_p5_indexed_hex("PX", index, V9xGmadrRead(offset));
        v9x_p5_flush();
    }
    v9x_p5_hex("PixelProbes", (DWORD)count);
    v9x_p5_hex("ExpectedInside", V9X_I9XX_TRI_COLOR_RGB565);
    v9x_p5_hex("ExpectedOutside", V9X_I9XX_FILL_RGB565);
}

/*
 * HeapProbe: one dword just below the reserve.
 *
 * The most important boundary observation in the capture, because the reserve
 * moved 896 KiB down into memory the heap previously published. It is
 * READ-ONLY - that dword still belongs to the published heap and must not be
 * overwritten with a canary.
 *
 * Recorded before and after, and a difference is NOT by itself classified as
 * GPU corruption: nothing here proves the heap is quiescent or exclusively
 * ours for the interval. Writable guard patterns go only INSIDE the reserve,
 * where we do own every byte.
 */
static void v9x_p5_publish_heap_probe(
    const struct v9x_i9xx_sandbox_layout *layout, const char *key)
{
    v9x_p5_hex(key, V9xGmadrRead(layout->reserve_offset - 4ul));
}

static void v9x_p5_publish_guards(
    const struct v9x_i9xx_sandbox_layout *layout, const char *suffix)
{
    char key[24];
    WORD at = 0u;

    /* The scratch page below the target and the guard page above it. Both are
     * inside the reserve, so both may legitimately carry a canary. */
    key[at++] = 'G'; key[at++] = 'L'; key[at++] = 'o'; key[at++] = 'w';
    key[at++] = suffix[0]; key[at] = '\0';
    v9x_p5_hex(key, V9xGmadrRead(layout->scratch_offset));
    at = 0u;
    key[at++] = 'G'; key[at++] = 'U'; key[at++] = 'p'; key[at++] = 'p';
    key[at++] = suffix[0]; key[at] = '\0';
    v9x_p5_hex(key, V9xGmadrRead(layout->guard_upper_offset));
}

/*
 * Not V9X_I9XX_FAR, despite every other exported function in this file's
 * neighbours carrying it.
 *
 * The qualifier belongs on a call that crosses the _TEXT/I9XXCODE boundary,
 * and intel16.h is the only place allowed to declare one. This is not such a
 * call: the sole caller is intel_ring16.c, which the family manifest places in
 * I9XXCODE alongside this unit, so the call is intra-segment and near - the
 * same shape as its sibling v9x_intel_phase4_maybe_run.
 *
 * It carried the qualifier until 2026-09-15, against a near extern in
 * intel_ring16.c. Watcom returns from a __far definition with retf while a
 * near call has pushed no segment, so the ret would have popped the caller's
 * frame as CS - a wild jump on entry to Phase 5, armed or not. Nothing caught
 * it: the two declarations are in different translation units, and check-tree
 * polices only the crossings intel16.h names.
 */

/*
 * One scene: stage it, run its five steps, read its probes.
 *
 * Returns V9X_TRUE only when every step completed. The caller stops at the
 * first scene that does not, and does NOT retire the token - a run whose
 * result nobody can vouch for leaves the token in flight, and the next boot
 * reports INCOMPLETE.
 *
 * Everything this scene learned is on disk before the next one submits
 * anything. That is what makes a hang on scene four cost only scene four:
 * scenes zero to three are complete, and the capture names the fourth.
 */
static WORD v9x_p6_run_scene(
    const struct v9x_i9xx_sandbox_layout *layout, WORD scene, DWORD stage_base)
{
    WORD index;
    DWORD stream[160];
    DWORD dwords = 0ul;
    DWORD crc;

    if (v9x_i9xx_scene_at((v9x_u32)scene, &v9x_p6_scene) != V9X_STATUS_OK) {
        v9x_p6_text(scene, "Scene", "TABLE-REFUSED");
        return V9X_FALSE;
    }
    if (v9x_i9xx_build_scene_stream(
            &v9x_p6_scene, stream,
            (v9x_u32)(sizeof(stream) / sizeof(stream[0])),
            &dwords) != V9X_STATUS_OK) {
        v9x_p6_text(scene, "Scene", "BUILD-REFUSED");
        return V9X_FALSE;
    }
    crc = v9x_i9xx_crc32_dwords(stream, dwords);

    v9x_p6_hex(scene, "Id", (DWORD)v9x_p6_scene.id);
    v9x_p6_hex(scene, "Dwords", dwords);
    v9x_p6_hex(scene, "Crc", crc);
    v9x_p6_hex(scene, "Tris", (DWORD)v9x_p6_scene.triangle_count);
    v9x_p6_hex(scene, "Probes", (DWORD)v9x_p6_scene.probe_count);
    /*
     * The CRC the driver built against the CRC the generated table carries.
     * A real comparison, not a restatement: the mini-VDD gates on its own
     * copy, and if the two ever disagreed the arm would refuse with nothing
     * in the capture to say why.
     */
    v9x_p6_hex(scene, "GenCrc", v9x_i9xx_scene_crc((v9x_u32)scene));
    if (crc != v9x_i9xx_scene_crc((v9x_u32)scene)) {
        v9x_p6_text(scene, "Scene", "CRC-MISMATCH");
        return V9X_FALSE;
    }

    /* Into the accumulator before staging, so what is CRCed at the end is
     * exactly what was submitted. */
    if (v9x_p6_all_used + dwords > (DWORD)V9X_P6_ALL_DWORDS) {
        v9x_p6_text(scene, "Scene", "ACCUMULATOR-FULL");
        return V9X_FALSE;
    }
    for (index = 0u; index < (WORD)dwords; ++index) {
        v9x_p6_all[v9x_p6_all_used + (DWORD)index] = stream[index];
    }
    v9x_p6_all_used += dwords;
    v9x_p5_hex("SceneDwordsSoFar", v9x_p6_all_used);
    v9x_p5_flush();

    v9x_p5_intent(V9X_P6_STEP_STAGE);
    for (index = 0u; index < (WORD)dwords; ++index) {
        if (V9xMiniI9xxSceneStage(stage_base, index, stream[index],
                              scene) == 0u) {
            v9x_p6_hex(scene, "StageFailIndex", (DWORD)index);
            v9x_p6_hex(scene, "StageFail", v9x_i9xx_ring_stage_fail);
            v9x_p5_capture_errors("PostErr");
            v9x_p6_text(scene, "Scene", "STAGE-REFUSED");
            return V9X_FALSE;
        }
        /* A marker every sixteen dwords, so a hang inside the loop says
         * roughly where rather than only that it was staging. */
        if ((index & 0x0fu) == 0u) {
            v9x_p6_hex(scene, "Marker", (DWORD)index);
            v9x_p5_flush();
        }
    }
    v9x_p6_hex(scene, "StageFail", 0ul);

    for (index = 0u; index < 5u; ++index) {
        static const WORD steps[5] = {
            V9X_P6_STEP_VERIFY, V9X_P6_STEP_PROGRAM, V9X_P6_STEP_PROBE,
            V9X_P6_STEP_DRAW, V9X_P6_STEP_TEARDOWN
        };
        WORD step = steps[index];

        v9x_p5_intent(step);
        if (V9xMiniI9xxSceneExecute(crc, step, scene) == 0u) {
            v9x_p6_hex(scene, "ExecFailStep", (DWORD)step);
            v9x_p6_hex(scene, "ExecFailure", v9x_i9xx_ring_exec_failure);
            v9x_p6_hex(scene, "ExecHead", v9x_i9xx_ring_exec_head);
            v9x_p6_hex(scene, "ExecTail", v9x_i9xx_ring_exec_tail);
            v9x_p6_hex(scene, "ExecPolls", v9x_i9xx_ring_exec_polls);
            /* The error registers on the failure path too: a parser fault
             * that hangs the drain is exactly when this is wanted. */
            v9x_p5_capture_errors("PostErr");
            v9x_p6_text(scene, "Scene", "EXECUTE-REFUSED");
            return V9X_FALSE;
        }
    }
    v9x_p6_hex(scene, "ExecFailure", 0ul);

    /*
     * Execution evidence reaches disk BEFORE the first pixel read. If the
     * probes lock the machine, the capture still says this scene was
     * submitted and drained.
     */
    v9x_p5_capture_errors("PostErr");
    v9x_p5_flush();

    v9x_p5_intent(V9X_P5_STEP_PIXELS);
    for (index = 0u; index < (WORD)v9x_p6_scene.probe_count; ++index) {
        char key[32];
        DWORD offset;

        offset = v9x_p5_point_offset(layout,
                                     v9x_p6_scene.probes[index].x,
                                     v9x_p6_scene.probes[index].y);
        /*
         * The probe's own name, prefixed by its scene. What it EXPECTED is
         * published beside what it read, because the same value means
         * different things in different scenes - the fill is a correct
         * result where a triangle was not drawn, and a regression where one
         * was.
         */
        v9x_p6_key(key, scene, v9x_p6_scene.probes[index].name);
        v9x_p5_text(key, v9x_p6_expectation(
                             v9x_p6_scene.probes[index].expect));
        v9x_p6_hex(scene, "PixelNext", (DWORD)index);
        v9x_p6_hex(scene, "PixelOffset", offset);
        v9x_p5_flush();

        v9x_p6_key(key, scene, "PX");
        v9x_p5_indexed_hex(key, index, v9x_p5_read_counted(offset));
        v9x_p5_flush();
    }
    v9x_p6_hex(scene, "PixelProbes", (DWORD)v9x_p6_scene.probe_count);
    v9x_p5_flush();
    return V9X_TRUE;
}

void v9x_intel_phase5_run(
    const struct v9x_i9xx_sandbox_layout *layout,
    WORD phase4_passed, WORD armed)
{
    WORD reason;
#ifdef V9X_I9XX_PHASE5_SUBMIT
    /* Only the staging and execute loops use it, and both are behind the
     * same guard. Declared here rather than in a block so the two loops can
     * share it, and scoped so a build without submit has no unused local. */
    WORD index;
#endif
    WORD mapping;
#ifdef V9X_I9XX_PHASE5_SUBMIT
    DWORD stage_base;
#endif

    V9xEnsureDiagDir();
    /*
     * 2: adds PreErr/PostErr with their completeness status.
     * 3: adds the Phase 6 scene sections - per-scene stream figures, probe
     *    sets carrying their own expectations, and the cumulative aperture
     *    read count. Bumped so a checker can REQUIRE those of a Phase 6
     *    capture without rejecting the schema-1 and schema-2 captures already
     *    preserved under docs\probe.
     */
    v9x_p5_text("SchemaVersion", "3");
    v9x_p5_reads = 0ul;
    v9x_p6_all_used = 0ul;
    v9x_p5_hex("ApertureReads", v9x_p5_reads);
    v9x_p5_text("Access", armed != 0u ? "armed-one-shot" : "no-hardware-writes");
    v9x_p5_text("CaptureBuildId",
                v9x_intel_bridge_build_identity()->build_id);
    v9x_p5_hex("Phase4Passed", (DWORD)phase4_passed);
    /* What the token claimed and how far the transaction got, so a refusal
     * here is readable without cross-referencing INTELRNG.TXT. */
    v9x_p5_hex("ArmPhase", (DWORD)v9x_intel_boot_arm_phase);
    v9x_p5_hex("ChainStateOnEntry", (DWORD)v9x_intel_arm_chain.state);

    v9x_p5_intent(V9X_P5_STEP_PREFLIGHT);
#ifdef V9X_I9XX_PHASE5_SUBMIT
    reason = v9x_p5_preflight(layout, phase4_passed, armed);
#else
    /*
     * Built without the submit path. An armed run is refused before it can
     * write anything; an unarmed run still takes its own branch below and
     * produces the full no-write capture, which is what B1 needs.
     */
    reason = (armed == 0u) ? V9X_P5_PRE_NOT_ARMED : V9X_P5_PRE_SUBMIT_MISSING;
    (void)v9x_p5_preflight;
#endif

    /*
     * The stream plan is published whatever the preflight said, including on
     * an unarmed boot. A decoder error is then caught on B1 and costs no armed
     * boot, which is the entire reason B1 exists.
     */
    if (v9x_p5_stream_dwords == 0ul) {
        (void)v9x_i9xx_build_phase5_stream(
            v9x_p5_stream,
            (DWORD)(sizeof(v9x_p5_stream) / sizeof(v9x_p5_stream[0])),
            &v9x_p5_stream_dwords);
    }
    v9x_p5_progress("stream-built");
    v9x_p5_publish_stream();
    v9x_p5_publish_vertices();
    v9x_p5_hex("StreamCrc",
               v9x_i9xx_crc32_dwords(v9x_p5_stream, v9x_p5_stream_dwords));
    v9x_p5_hex("GeneratedCrc", v9x_i9xx_phase5_execution_crc());
    v9x_p5_hex("RefTargetOffset", layout->target_offset);
    v9x_p5_hex("RefTargetPitch", layout->target_pitch);
    v9x_p5_hex("RefTargetBytes", layout->target_bytes);
    v9x_p5_hex("RefGuardUpper", layout->guard_upper_offset);
    v9x_p5_hex("P5RefBackedPrefix", v9x_i9xx_gtt_backed_prefix);
    v9x_p5_hex("P5RefReserveFirst", v9x_i9xx_gtt_reserve_first);
    v9x_p5_hex("P5RefReserveCount", v9x_i9xx_gtt_reserve_count);

    /*
     * Mapping, backing and bounds BEFORE the first aperture access, on every
     * path including the unarmed one. These checks used to sit behind the
     * preflight's `armed` test, so B1 read the aperture with none of them
     * having run.
     */
    mapping = v9x_p5_validate_mapping(layout);
    v9x_p5_hex("MappingCheck", (DWORD)mapping);
    v9x_p5_flush();
    if (mapping != V9X_P5_PRE_OK) {
        v9x_p5_rejection = mapping;
        v9x_p5_hex("Precondition", (DWORD)mapping);
        v9x_p5_result("MAPPING-REFUSED");
        return;
    }

    /* First hardware touch of the boot: one aperture read just below the
     * reserve. If the capture stops here, the aperture read is the fault and
     * nothing about the stream or the layout arithmetic matters. */
    v9x_p5_progress("heap-probe");
    v9x_p5_publish_heap_probe(layout, "HeapProbeBefore");
    v9x_p5_progress("guards");
    v9x_p5_publish_guards(layout, "0");
    v9x_p5_progress("guards-done");

    if (reason != V9X_P5_PRE_OK) {
        v9x_p5_rejection = reason;
        v9x_p5_hex("Precondition", (DWORD)reason);
        /*
         * The unarmed path stops here, having written nothing. It still proves
         * two things B1 needs: that two read-only hash passes over the
         * untouched target agree, and that the address path works at all.
         */
        if (reason == V9X_P5_PRE_NOT_ARMED) {
            /* The 614 KiB read-only hash of the reserve, through the
             * mini-VDD's own physical mapping. The heaviest thing the
             * no-write path does, and the leading suspect. */
            /*
             * Eight points, each read twice: sixteen aperture reads in place
             * of the 307,200 that hard locked this machine. The omission is
             * recorded rather than left as an absence, so a reader of the
             * capture is not left wondering whether the hash failed or was
             * never attempted.
             */
            v9x_p5_text("HashOmitted", "bulk-aperture-read-hang");
            v9x_p5_progress("unarmed-samples");
            (void)v9x_p5_sample_target(layout);
            v9x_p5_result("NO-WRITE");
        } else {
            v9x_p5_result("REFUSED");
        }
        return;
    }
    v9x_p5_hex("Precondition", 0ul);

    /*
     * Everything below this line writes. It is reached only with an armed,
     * one-shot token whose CRC covers this exact stream, only after Phase 4
     * passed in this boot, and only after the built stream decoded.
     */
    /*
     * Everything below this line writes. It is reached only with an armed,
     * one-shot token whose CRC covers this exact stream, only after Phase 4
     * passed in this boot, and only after the built stream decoded.
     *
     * The CPU does not write the render target at all - the GPU fills it with
     * the XY_COLOR_BLT at the head of this stream. The only thing the CPU
     * writes is the ring, one dword at a time, each checked against the
     * generated table on the way in.
     */
#ifdef V9X_I9XX_PHASE5_SUBMIT
    /*
     * The staging base is the GMADR APERTURE address, not the stolen-memory
     * physical one: the mini-VDD compares it against
     * V9X_I9XX_GMADR_BASE + V9X_I9XX_RESERVE_OFFSET and refuses with stage
     * reason 1 otherwise. Phase 4 has always passed gmadr_bar2 + ring_offset.
     *
     * This passed layout->reserve_physical - 7FEB0000, the BSM-relative
     * address - and was refused on the first dword of the first armed boot,
     * 2026-09-15. The two names sound interchangeable and address different
     * spaces; the capture publishes the value so a future mismatch is visible
     * rather than inferred from a refusal code.
     */
    /* Before any submission, so the after-reading is a difference rather than
     * an absolute nobody can interpret. */
    v9x_p5_capture_errors("PreErr");

    stage_base = v9x_i9xx_gmadr_bar2 + layout->reserve_offset;
    v9x_p5_hex("StageBase", stage_base);
    v9x_p5_hex("StageBaseExpected", 0xd0000000ul + layout->reserve_offset);
    v9x_p5_flush();

    /*
     * PHASE 6 forks here, after the ring is mapped and before anything is
     * staged, and it never rejoins: its scenes carry their own streams,
     * their own CRCs and their own probes.
     *
     * The Phase 5 path below is left exactly as it was rather than being
     * generalised into scene 0. It is the path two armed boots have actually
     * run, its capture is what C:\temp\intel42 is, and until Phase 6 has
     * produced a capture of its own that is evidence worth keeping able to
     * reproduce. Retiring it is a separate change for after that boot.
     */
    if (v9x_intel_boot_arm_phase == V9X_I9XX_PHASE6) {
        WORD scene;
        WORD scenes = (WORD)v9x_i9xx_scene_count();

        v9x_p5_hex("Scenes", (DWORD)scenes);
        v9x_p5_hex("ScenesAuthorised",
                   (DWORD)v9x_i9xx_scene_authorised_draws());
        v9x_p5_hex("SceneCombinedCrc", v9x_i9xx_scene_combined_crc());
        v9x_p5_hex("SceneProbeBudget", v9x_i9xx_scene_total_probes());
        v9x_p5_flush();
        /*
         * Zero means the build defines more scenes than the errata decision
         * authorises. It is a refusal, not an empty set - executing none is
         * correct, and saying so is what stops it looking like a pass.
         */
        if (scenes == 0u) {
            v9x_p5_result("SCENES-UNAUTHORISED");
            return;
        }

        for (scene = 0u; scene < scenes; ++scene) {
            v9x_p5_hex("SceneNext", (DWORD)scene);
            v9x_p5_flush();
            if (v9x_p6_run_scene(layout, scene, stage_base) == 0u) {
                /*
                 * Guards and the heap probe even on the failing path. A
                 * scene that wrote outside its target is exactly the case
                 * where they matter, and the earlier scenes' results are
                 * already on disk.
                 */
                v9x_p5_publish_guards(layout, "1");
                v9x_p5_publish_heap_probe(layout, "HeapProbeAfter");
                v9x_p5_hex("SceneFailed", (DWORD)scene);
                v9x_p5_result("SCENE-REFUSED");
                return;
            }
            /* Between scenes, not only at the end: a scene that damaged the
             * guards must not have it attributed to a later one. */
            v9x_p5_publish_guards(layout, "1");
            v9x_p5_flush();
        }
        v9x_p5_hex("ScenesCompleted", (DWORD)scenes);
        v9x_p5_publish_heap_probe(layout, "HeapProbeAfter");

        {
            /*
             * What was actually built and staged, against the constant the
             * generator produced. Two independent quantities: passing the
             * generated CRC on both sides would compare a number with itself
             * and accept any stream at all.
             */
            DWORD staged = v9x_i9xx_crc32_dwords(v9x_p6_all,
                                                 v9x_p6_all_used);
            WORD verdict;

            v9x_p5_hex("SceneStagedCrc", staged);
            v9x_p5_hex("SceneStagedDwords", v9x_p6_all_used);
            verdict = v9x_i9xx_chain_draw_done(
                &v9x_intel_arm_chain, V9X_TRUE,
                staged,
                v9x_i9xx_scene_combined_crc());

            v9x_p5_hex("ChainDrawVerdict", (DWORD)verdict);
            v9x_p5_hex("ChainState", (DWORD)v9x_intel_arm_chain.state);
            if (verdict != V9X_I9XX_CHAIN_OK) {
                v9x_p5_result("CHAIN-DRAW-REFUSED");
                return;
            }
            if (v9x_intel_boot_arm_retire("pass:phase6") == 0u) {
                v9x_p5_result("RETIRE-FAILED");
                return;
            }
            v9x_p5_text("TokenRetired", "1");
        }
        v9x_p5_result("PASS");
        return;
    }

    v9x_p5_intent(V9X_P5_STEP_STAGE);
    for (index = 0u; index < (WORD)v9x_p5_stream_dwords; ++index) {
        if (V9xMiniI9xxP5Stage(stage_base, index,
                               v9x_p5_stream[index]) == 0u) {
            v9x_p5_hex("StageFailIndex", (DWORD)index);
            v9x_p5_hex("StageFail", v9x_i9xx_ring_stage_fail);
            v9x_p5_capture_errors("PostErr");
            v9x_p5_result("STAGE-REFUSED");
            return;
        }
        /* A marker every sixteen dwords, so a hang inside a sixty-six dword
         * loop says roughly where rather than only that it was staging. */
        if ((index & 0x0fu) == 0u) {
            v9x_p5_hex("P5Marker", (DWORD)index);
            v9x_p5_flush();
        }
    }
    v9x_p5_hex("StageFail", 0ul);

    /*
     * The five execute steps, in order, each refusing if the previous did not
     * complete. IntentStep is flushed before each, carrying the same number
     * the mini-VDD is about to be asked for.
     */
    for (index = 0u; index < 5u; ++index) {
        static const WORD execute_steps[5] = {
            V9X_P5_STEP_VERIFY, V9X_P5_STEP_PROGRAM, V9X_P5_STEP_PROBE,
            V9X_P5_STEP_DRAW, V9X_P5_STEP_TEARDOWN
        };
        WORD step = execute_steps[index];

        v9x_p5_intent(step);
        if (V9xMiniI9xxRingExecute(v9x_i9xx_phase5_execution_crc(), step)
                == 0u) {
            v9x_p5_hex("ExecFailStep", (DWORD)step);
            v9x_p5_hex("ExecFailure", v9x_i9xx_ring_exec_failure);
            v9x_p5_hex("ExecHead", v9x_i9xx_ring_exec_head);
            v9x_p5_hex("ExecTail", v9x_i9xx_ring_exec_tail);
            v9x_p5_hex("ExecPolls", v9x_i9xx_ring_exec_polls);
            /*
             * The error registers on the FAILURE path too. A parser fault that
             * hangs the drain is exactly when this evidence is wanted, and the
             * loop returns here without reaching the post-draw capture - so
             * the commit this instrumentation exists for was being lost on the
             * only runs that needed it.
             */
            v9x_p5_capture_errors("PostErr");
            v9x_p5_result("EXECUTE-REFUSED");
            return;
        }
        v9x_p5_indexed_hex("EX", step, v9x_i9xx_ring_exec_elapsed);
    }
    v9x_p5_hex("ExecFailure", 0ul);
#endif

    /*
     * The two bulk read-backs that used to run here are gone, and their
     * absence is recorded rather than left silent.
     *
     * Step 27 hashed the whole target twice (307,200 aperture reads) and step
     * 28 took 480 row CRCs of 320 reads each (153,600). The first of those
     * hard locked this machine on an UNARMED boot, 2026-09-15. On an armed
     * boot they ran after the draw, so the triangle would have been drawn and
     * the machine would then have hung reading it back - losing the evidence
     * the boot was spent on.
     *
     * What is lost with them: per-scanline localisation of a wrong picture.
     * The fourteen named probes say whether the draw is right; they cannot say
     * where it went wrong. That is a deliberate trade recorded in
     * plans\intel-phase5-bounded-readback.md and not a temporary measure to
     * be quietly reverted.
     */
    v9x_p5_text("HashOmitted", "bulk-aperture-read-hang");
    v9x_p5_text("RowCrcOmitted", "bulk-aperture-read-hang");
    v9x_p5_hex("RowCrcRowsOmitted", (DWORD)V9X_I9XX_TARGET_HEIGHT);
    /*
     * Execution evidence reaches the disk BEFORE the first pixel read. If the
     * probes lock the machine, the capture still says the draw was submitted
     * and drained.
     */
    v9x_p5_flush();

    /*
     * Error state after the draw, before any pixel is read. If the parser
     * rejected the primitive this is where it says so, and it must reach the
     * disk even if the probe reads then lock.
     */
    v9x_p5_capture_errors("PostErr");

    v9x_p5_intent(V9X_P5_STEP_PIXELS);
    v9x_p5_publish_pixels(layout);
    v9x_p5_publish_guards(layout, "1");
    v9x_p5_publish_heap_probe(layout, "HeapProbeAfter");

#ifdef V9X_I9XX_PHASE5_SUBMIT
    /*
     * Close the transaction. Reaching here means every execute step returned
     * without refusing, which is what "the draw happened" can mean from inside
     * the driver - the picture itself is judged off the capture, by the row
     * CRCs and pixel probes above, deliberately not asserted here.
     *
     * The CRC pair is a real comparison: the stream actually staged against
     * the constant generated from the same builders.
     *
     * This is the only point in either phase at which a token is retired. If
     * the chain refuses, or the retire writes fail, the token stays in flight
     * and the next boot reports INCOMPLETE - which is the correct outcome for
     * a run whose result nobody can vouch for.
     */
    {
        WORD verdict = v9x_i9xx_chain_draw_done(
            &v9x_intel_arm_chain, V9X_TRUE,
            v9x_i9xx_crc32_dwords(v9x_p5_stream, v9x_p5_stream_dwords),
            v9x_i9xx_phase5_execution_crc());

        v9x_p5_hex("ChainDrawVerdict", (DWORD)verdict);
        v9x_p5_hex("ChainState", (DWORD)v9x_intel_arm_chain.state);
        if (verdict != V9X_I9XX_CHAIN_OK) {
            v9x_p5_result("CHAIN-DRAW-REFUSED");
            return;
        }
        if (v9x_intel_boot_arm_retire("pass:phase5") == 0u) {
            v9x_p5_result("RETIRE-FAILED");
            return;
        }
        v9x_p5_text("TokenRetired", "1");
    }
#endif
    v9x_p5_result("PASS");
}
