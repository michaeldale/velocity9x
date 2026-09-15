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
extern DWORD v9x_i9xx_gtt_backed_prefix;
extern DWORD v9x_i9xx_gtt_reserve_first;
extern DWORD v9x_i9xx_gtt_reserve_count;
extern DWORD v9x_i9xx_ring_stage_fail;
extern DWORD v9x_i9xx_ring_exec_failure;
extern DWORD v9x_i9xx_ring_exec_head;
extern DWORD v9x_i9xx_ring_exec_tail;
extern DWORD v9x_i9xx_ring_exec_polls;
extern DWORD v9x_i9xx_ring_exec_elapsed;

extern void FAR PASCAL V9xEnsureDiagDir(void);
extern DWORD FAR PASCAL V9xGmadrRead(DWORD offset);
extern WORD FAR PASCAL V9xMiniI9xxRingHash(DWORD offset, WORD count_high,
                                           WORD count_low);
extern WORD FAR PASCAL V9xMiniI9xxP5Stage(DWORD physical, WORD index,
                                          DWORD value);
extern WORD FAR PASCAL V9xMiniI9xxRingExecute(DWORD crc, WORD step);

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
#define V9X_P5_STEP_HASH          27u
#define V9X_P5_STEP_ROWS          28u
#define V9X_P5_STEP_PIXELS        29u

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
static DWORD v9x_p5_stream[160];
static DWORD v9x_p5_stream_dwords;

static void v9x_p5_text(const char *key, const char *value)
{
    WritePrivateProfileString(V9X_P5_SECTION, key, value,
                              V9X_DIAG_INTEL3D0_TXT);
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
static void v9x_p5_intent(WORD step)
{
    v9x_p5_hex("IntentStep", (DWORD)step);
}

static WORD v9x_p5_preflight(const struct v9x_i9xx_sandbox_layout *layout,
                             WORD phase4_passed, WORD armed)
{
    DWORD built_crc;
    DWORD rejected_index = 0ul;
    WORD reason;

    if (armed == 0u) { return V9X_P5_PRE_NOT_ARMED; }
    /* The hard precondition: Phase 4 must have passed in THIS boot. */
    if (phase4_passed == 0u) { return V9X_P5_PRE_PHASE4_MISSING; }

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
    v9x_p5_hex("OffsetState", 0ul);
    v9x_p5_hex("LengthState", v9x_i9xx_3d_state_extent());
    v9x_p5_hex("OffsetShader", v9x_i9xx_3d_state_extent());
    v9x_p5_hex("LengthShader", v9x_i9xx_fragment_program_extent());
    v9x_p5_hex("OffsetProbe",
               v9x_i9xx_3d_state_extent() +
               v9x_i9xx_fragment_program_extent());
    v9x_p5_hex("LengthProbe", 2ul);
    v9x_p5_hex("OffsetVertices",
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
    DWORD base = v9x_i9xx_3d_state_extent() +
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
static WORD v9x_p5_hash_target(const struct v9x_i9xx_sandbox_layout *layout,
                               const char *key_a, const char *key_b,
                               const char *key_fail)
{
    DWORD dwords = layout->target_bytes / 4ul;

    if (V9xMiniI9xxRingHash(layout->target_offset - layout->reserve_offset,
                            (WORD)(dwords >> 16), (WORD)(dwords & 0xfffful))
            == 0u) {
        v9x_p5_hex(key_fail, v9x_i9xx_hash_fail);
        return 0u;
    }
    v9x_p5_hex(key_a, v9x_i9xx_hash_pass_a);
    v9x_p5_hex(key_b, v9x_i9xx_hash_pass_b);
    v9x_p5_hex(key_fail, 0ul);
    return 1u;
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
        v9x_p5_indexed_hex("PX", index, V9xGmadrRead(offset));
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
    WORD row;
    DWORD row_base;

    V9xEnsureDiagDir();
    v9x_p5_text("SchemaVersion", "1");
    v9x_p5_text("Access", armed != 0u ? "armed-one-shot" : "no-hardware-writes");
    v9x_p5_text("CaptureBuildId",
                v9x_intel_bridge_build_identity()->build_id);
    v9x_p5_hex("Phase4Passed", (DWORD)phase4_passed);

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

    v9x_p5_publish_heap_probe(layout, "HeapProbeBefore");
    v9x_p5_publish_guards(layout, "0");

    if (reason != V9X_P5_PRE_OK) {
        v9x_p5_rejection = reason;
        v9x_p5_hex("Precondition", (DWORD)reason);
        /*
         * The unarmed path stops here, having written nothing. It still proves
         * two things B1 needs: that two read-only hash passes over the
         * untouched target agree, and that the address path works at all.
         */
        if (reason == V9X_P5_PRE_NOT_ARMED) {
            (void)v9x_p5_hash_target(layout, "UnarmedHashA", "UnarmedHashB",
                                     "UnarmedHashFail");
            v9x_p5_hex("UnarmedSample0", V9xGmadrRead(layout->target_offset));
            v9x_p5_hex("UnarmedSample1",
                       V9xGmadrRead(layout->target_offset +
                                    layout->target_bytes - 4ul));
            v9x_p5_text("Result", "NO-WRITE");
        } else {
            v9x_p5_text("Result", "REFUSED");
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
    v9x_p5_intent(V9X_P5_STEP_STAGE);
    for (index = 0u; index < (WORD)v9x_p5_stream_dwords; ++index) {
        if (V9xMiniI9xxP5Stage(layout->reserve_physical, index,
                               v9x_p5_stream[index]) == 0u) {
            v9x_p5_hex("StageFailIndex", (DWORD)index);
            v9x_p5_hex("StageFail", v9x_i9xx_ring_stage_fail);
            v9x_p5_text("Result", "STAGE-REFUSED");
            return;
        }
        /* A marker every sixteen dwords, so a hang inside a sixty-six dword
         * loop says roughly where rather than only that it was staging. */
        if ((index & 0x0fu) == 0u) {
            v9x_p5_hex("P5Marker", (DWORD)index);
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
            v9x_p5_text("Result", "EXECUTE-REFUSED");
            return;
        }
        v9x_p5_indexed_hex("EX", step, v9x_i9xx_ring_exec_elapsed);
    }
    v9x_p5_hex("ExecFailure", 0ul);
#endif

    v9x_p5_intent(V9X_P5_STEP_HASH);
    (void)v9x_p5_hash_target(layout, "DrawHashA", "DrawHashB",
                             "DrawHashFail");

    v9x_p5_intent(V9X_P5_STEP_ROWS);
    row_base = layout->target_offset;
    for (row = 0u; row < (WORD)V9X_I9XX_TARGET_HEIGHT; ++row) {
        /*
         * One CRC per row, so a wrong picture says WHERE it went wrong. A
         * whole-target hash can only say that something did.
         *
         * row_base accumulates rather than being computed as row * pitch: the
         * multiply would be a call to __U4M, which this segment cannot reach.
         */
        DWORD crc = 0xfffffffful;
        DWORD column;

        for (column = 0ul; column < layout->target_pitch; column += 4ul) {
            crc ^= V9xGmadrRead(row_base + column);
        }
        v9x_p5_indexed_hex("R", row, crc);
        row_base += layout->target_pitch;
    }

    v9x_p5_intent(V9X_P5_STEP_PIXELS);
    v9x_p5_publish_pixels(layout);
    v9x_p5_publish_guards(layout, "1");
    v9x_p5_publish_heap_probe(layout, "HeapProbeAfter");
    v9x_p5_text("Result", "PASS");
}
