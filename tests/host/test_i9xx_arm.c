#include <stdio.h>

#include "velocity9x/intel_gma.h"

static unsigned int failures = 0u;

#define CHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++failures; \
    } \
} while (0)

static void test_crc(void)
{
    static const v9x_u32 stream[2] = {
        V9X_I9XX_MI_NOOP, V9X_I9XX_MI_FLUSH
    };
    static const v9x_u32 blt[8] = {
        0x54300004ul, 0x03f00020ul, 0ul, 0x00080008ul,
        0x006c1100ul, 0x55aa33ccul, V9X_I9XX_MI_FLUSH,
        V9X_I9XX_MI_NOOP
    };
    v9x_u32 changed[8];
    v9x_u16 index;
    CHECK(v9x_i9xx_crc32_dwords(stream, 2ul) == 0x8b2cbe45ul);
    CHECK(v9x_i9xx_crc32_dwords(0, 2ul) == 0ul);
    CHECK(v9x_i9xx_phase4_execution_crc(stream, blt) == 0xa0da64a1ul);
    CHECK(v9x_i9xx_phase4_execution_crc(0, blt) == 0ul);
    for (index = 0u; index < 8u; ++index) { changed[index] = blt[index]; }
    changed[5] ^= 1ul;
    CHECK(v9x_i9xx_phase4_execution_crc(stream, changed) != 0xa0da64a1ul);
}

static void test_arm_contract(void)
{
    struct v9x_i9xx_arm_request request;
    v9x_u16 rejection;
    v9x_u32 parsed_crc;

    CHECK(v9x_i9xx_token_valid("phase4-test-001") == V9X_TRUE);
    CHECK(v9x_i9xx_token_valid("bad token") == V9X_FALSE);
    CHECK(v9x_i9xx_token_valid("") == V9X_FALSE);
    CHECK(v9x_i9xx_parse_crc_hex("A0DA64A1", &parsed_crc) == V9X_TRUE);
    CHECK(parsed_crc == 0xa0da64a1ul);
    CHECK(v9x_i9xx_parse_crc_hex("A0DA64A", &parsed_crc) == V9X_FALSE);
    CHECK(v9x_i9xx_parse_crc_hex("A0DA64A1G", &parsed_crc) == V9X_FALSE);
    CHECK(v9x_i9xx_parse_crc_hex("00000000", &parsed_crc) == V9X_FALSE);

    request.token = "phase4-test-001";
    request.in_flight = "phase4-test-001";
    request.configured_crc = 0x8b2cbe45ul;
    request.packet_crc = 0x8b2cbe45ul;
    request.enable_this_boot = 1u;
    request.safe_mode = V9X_FALSE;
    request.errata_gate = V9X_TRUE;
    request.vendor_id = 0x8086u;
    request.device_id = 0x27aeu;
    request.revision = 3u;
    request.phase = 4u;
    request.expected_phase = 4u;
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) == V9X_STATUS_OK);
    CHECK(rejection == V9X_I9XX_ARM_REJECT_NONE);

    request.errata_gate = V9X_FALSE;
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(rejection == V9X_I9XX_ARM_REJECT_ERRATA);
    request.errata_gate = V9X_TRUE;

    request.enable_this_boot = 0u;
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) ==
          V9X_STATUS_INVALID_STATE);
    CHECK(rejection == V9X_I9XX_ARM_REJECT_DISABLED);
    request.enable_this_boot = 1u;

    request.safe_mode = V9X_TRUE;
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) ==
          V9X_STATUS_INVALID_STATE);
    CHECK(rejection == V9X_I9XX_ARM_REJECT_SAFE_MODE);
    request.safe_mode = V9X_FALSE;

    request.device_id = 0x27a2u;
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(rejection == V9X_I9XX_ARM_REJECT_IDENTITY);
    request.device_id = 0x27aeu;

    /*
     * A Phase 5 token must not arm a Phase 4 caller, and a Phase 4 token
     * must not arm a Phase 5 caller. This is the check that keeps the
     * 2026-09-13 errata-gate decision - which covers Phase 4 only - from
     * silently authorising a 3D draw.
     */
    request.phase = 5u;
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(rejection == V9X_I9XX_ARM_REJECT_PHASE);
    request.phase = 4u;
    request.expected_phase = 5u;
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(rejection == V9X_I9XX_ARM_REJECT_PHASE);
    request.expected_phase = 4u;
    /* A phase neither side knows is still refused. */
    request.phase = 6u;
    request.expected_phase = 6u;
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(rejection == V9X_I9XX_ARM_REJECT_PHASE);
    request.phase = 4u;
    request.expected_phase = 4u;
    /* Phase 5 arming Phase 5 is accepted by the evaluator; whether the
     * chain lets it RUN is a separate gate, tested below. */
    request.phase = 5u;
    request.expected_phase = 5u;
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) == V9X_STATUS_OK);
    request.phase = 4u;
    request.expected_phase = 4u;

    request.in_flight = "phase4-test-002";
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) ==
          V9X_STATUS_INVALID_STATE);
    CHECK(rejection == V9X_I9XX_ARM_REJECT_TOKEN);
    request.in_flight = request.token;

    request.configured_crc ^= 1ul;
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) ==
          V9X_STATUS_INVALID_STATE);
    CHECK(rejection == V9X_I9XX_ARM_REJECT_CRC);
    request.configured_crc ^= 1ul;

    request.token = "bad token";
    request.in_flight = request.token;
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) ==
          V9X_STATUS_INVALID_STATE);
    CHECK(rejection == V9X_I9XX_ARM_REJECT_TOKEN);
    CHECK(v9x_i9xx_arm_evaluate(0, &rejection) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_i9xx_arm_evaluate(&request, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_phase4_sequence(void)
{
    struct v9x_i9xx_phase4_sequence state;
    v9x_u16 step;
    CHECK(v9x_i9xx_phase4_sequence_commit(0, V9X_I9XX_P4_PREFLIGHT) ==
          V9X_STATUS_INVALID_ARGUMENT);
    v9x_i9xx_phase4_sequence_begin(&state);
    CHECK(v9x_i9xx_phase4_sequence_commit(&state, V9X_I9XX_P4_BLT_DRAINED) ==
          V9X_STATUS_INVALID_STATE);
    CHECK(state.poisoned == V9X_TRUE);
    CHECK(v9x_i9xx_phase4_sequence_commit(&state, V9X_I9XX_P4_PREFLIGHT) ==
          V9X_STATUS_INVALID_STATE);
    v9x_i9xx_phase4_sequence_begin(&state);
    for (step = V9X_I9XX_P4_PREFLIGHT;
         step <= V9X_I9XX_P4_POST_SNAPSHOT; ++step) {
        CHECK(v9x_i9xx_phase4_sequence_commit(&state, step) == V9X_STATUS_OK);
    }
    CHECK(state.completed_step == V9X_I9XX_P4_POST_SNAPSHOT);
    CHECK(v9x_i9xx_phase4_sequence_commit(&state, V9X_I9XX_P4_POST_SNAPSHOT) ==
          V9X_STATUS_INVALID_STATE);
    v9x_i9xx_phase4_sequence_begin(&state);
    for (step = V9X_I9XX_P4_PREFLIGHT;
         step <= V9X_I9XX_P4_PROBE_DRAINED; ++step) {
        CHECK(v9x_i9xx_phase4_sequence_commit(&state, step) == V9X_STATUS_OK);
    }
    v9x_i9xx_phase4_sequence_poison(&state);
    CHECK(v9x_i9xx_phase4_sequence_commit(&state, V9X_I9XX_P4_WRAP_DRAINED) ==
          V9X_STATUS_INVALID_STATE);
}


/*
 * The two-phase arm transaction.
 *
 * The plan requires three properties be proved here, and each maps to one
 * group below: a Phase 4 token cannot reach Phase 5, a Phase 5 token cannot
 * skip a failed replay, and a power cut between the phases leaves the token
 * in flight.
 */
static void v9x_chain_request(struct v9x_i9xx_arm_request *request,
                              v9x_u16 phase, v9x_u16 expected_phase,
                              v9x_u32 crc)
{
    request->token = "phase5-test-001";
    request->in_flight = "phase5-test-001";
    request->configured_crc = crc;
    request->packet_crc = crc;
    request->enable_this_boot = 1u;
    request->safe_mode = V9X_FALSE;
    request->errata_gate = V9X_TRUE;
    request->vendor_id = 0x8086u;
    request->device_id = 0x27aeu;
    request->revision = 3u;
    request->phase = phase;
    request->expected_phase = expected_phase;
}

static void test_chain_token_phases(void)
{
    struct v9x_i9xx_arm_request request;
    struct v9x_i9xx_chain chain;
    const v9x_u32 p4 = 0xa0da64a1ul;
    const v9x_u32 p5 = 0x78780722ul;
    const v9x_u32 combined = v9x_i9xx_combined_arm_crc(p4, p5);

    /* A Phase 4 token cannot reach Phase 5, however well-formed. */
    v9x_chain_request(&request, V9X_I9XX_PHASE4, V9X_I9XX_PHASE5, combined);
    CHECK(v9x_i9xx_chain_begin(&chain, &request, combined, p4, p5) ==
          V9X_I9XX_CHAIN_REJECT_PHASE);
    CHECK(chain.state == V9X_I9XX_CHAIN_STATE_FAILED);
    CHECK(chain.token_retired == V9X_FALSE);
    CHECK(chain.in_flight_cleared == V9X_FALSE);

    /* Nor can a Phase 5 token arm a caller that thinks it is doing Phase 4. */
    v9x_chain_request(&request, V9X_I9XX_PHASE5, V9X_I9XX_PHASE4, combined);
    CHECK(v9x_i9xx_chain_begin(&chain, &request, combined, p4, p5) ==
          V9X_I9XX_CHAIN_REJECT_PHASE);

    /*
     * A token carrying only the Phase 4 CRC is refused even with both phase
     * fields set to 5 - the armed CRC must cover BOTH streams in execution
     * order, which is what makes "what was reviewed" and "what runs" the same
     * question.
     */
    v9x_chain_request(&request, V9X_I9XX_PHASE5, V9X_I9XX_PHASE5, p4);
    CHECK(v9x_i9xx_chain_begin(&chain, &request, combined, p4, p5) ==
          V9X_I9XX_CHAIN_REJECT_COMBINED);
    v9x_chain_request(&request, V9X_I9XX_PHASE5, V9X_I9XX_PHASE5, p5);
    CHECK(v9x_i9xx_chain_begin(&chain, &request, combined, p4, p5) ==
          V9X_I9XX_CHAIN_REJECT_COMBINED);

    /* The correct combined token is accepted. */
    v9x_chain_request(&request, V9X_I9XX_PHASE5, V9X_I9XX_PHASE5, combined);
    CHECK(v9x_i9xx_chain_begin(&chain, &request, combined, p4, p5) ==
          V9X_I9XX_CHAIN_OK);
    CHECK(chain.state == V9X_I9XX_CHAIN_STATE_ARMED);
    CHECK(chain.token_retired == V9X_FALSE);
}

/*
 * Which gate a consumed token must pass.
 *
 * This covers the decision the driver got wrong: until 2026-09-15 nothing read
 * IntelArmPhase, so the Phase 4 boot latch was the only thing standing between
 * a Phase 4 stick and a 3D draw.
 */
static void test_arm_gate_for(void)
{
    /* No token: nothing to gate, whatever the other inputs say. */
    CHECK(v9x_i9xx_arm_gate_for(0u, V9X_I9XX_PHASE5, 1u) ==
          V9X_I9XX_GATE_NONE);
    CHECK(v9x_i9xx_arm_gate_for(0u, V9X_I9XX_PHASE4, 1u) ==
          V9X_I9XX_GATE_NONE);

    /*
     * A Phase 4 token gets Phase 4's own gate and can never get the chain's -
     * which is the same thing as saying it cannot reach a draw.
     */
    CHECK(v9x_i9xx_arm_gate_for(1u, V9X_I9XX_PHASE4, 1u) ==
          V9X_I9XX_GATE_STANDALONE);

    /*
     * An absent IntelArmPhase reads as zero and must keep meaning Phase 4:
     * arm-intel-phase4.ps1 has never written the key, so every stick already
     * in existence reads this way. It must NOT mean Phase 5.
     */
    CHECK(v9x_i9xx_arm_gate_for(1u, 0u, 1u) == V9X_I9XX_GATE_STANDALONE);
    CHECK(v9x_i9xx_arm_gate_for(1u, 0u, 1u) != V9X_I9XX_GATE_CHAINED);

    /* A Phase 5 token gets the chain, but only in a build that has Phase 5. */
    CHECK(v9x_i9xx_arm_gate_for(1u, V9X_I9XX_PHASE5, 1u) ==
          V9X_I9XX_GATE_CHAINED);
    /*
     * Without Phase 5 built it refuses rather than arming. Arming would spend
     * the token on a draw that cannot happen and then leave it in flight
     * forever, since only the draw result may retire it.
     */
    CHECK(v9x_i9xx_arm_gate_for(1u, V9X_I9XX_PHASE5, 0u) ==
          V9X_I9XX_GATE_REFUSE);

    /* A phase this build has no vocabulary for refuses rather than guessing. */
    CHECK(v9x_i9xx_arm_gate_for(1u, 6u, 1u) == V9X_I9XX_GATE_REFUSE);
    CHECK(v9x_i9xx_arm_gate_for(1u, 0xffffu, 1u) == V9X_I9XX_GATE_REFUSE);
}

static void test_chain_cannot_skip_replay(void)
{
    struct v9x_i9xx_arm_request request;
    struct v9x_i9xx_chain chain;
    const v9x_u32 p4 = 0xa0da64a1ul;
    const v9x_u32 p5 = 0x78780722ul;
    const v9x_u32 combined = v9x_i9xx_combined_arm_crc(p4, p5);

    /* Straight to the draw, with no replay reported at all. */
    v9x_chain_request(&request, V9X_I9XX_PHASE5, V9X_I9XX_PHASE5, combined);
    CHECK(v9x_i9xx_chain_begin(&chain, &request, combined, p4, p5) ==
          V9X_I9XX_CHAIN_OK);
    CHECK(v9x_i9xx_chain_draw_done(&chain, V9X_TRUE, p5, p5) ==
          V9X_I9XX_CHAIN_REJECT_REPLAY);
    CHECK(chain.token_retired == V9X_FALSE);

    /* A replay that failed cannot be followed by a draw. */
    CHECK(v9x_i9xx_chain_begin(&chain, &request, combined, p4, p5) ==
          V9X_I9XX_CHAIN_OK);
    CHECK(v9x_i9xx_chain_replay_done(&chain, V9X_FALSE, p4, p4) ==
          V9X_I9XX_CHAIN_REJECT_REPLAY);
    CHECK(v9x_i9xx_chain_draw_done(&chain, V9X_TRUE, p5, p5) ==
          V9X_I9XX_CHAIN_REJECT_REPLAY);
    CHECK(chain.token_retired == V9X_FALSE);

    /*
     * A replay that "passed" but produced the wrong CRC is also a failure:
     * chaining widens what may be armed, it does not relax what each half has
     * to prove.
     */
    CHECK(v9x_i9xx_chain_begin(&chain, &request, combined, p4, p5) ==
          V9X_I9XX_CHAIN_OK);
    CHECK(v9x_i9xx_chain_replay_done(&chain, V9X_TRUE, p4 ^ 1ul, p4) ==
          V9X_I9XX_CHAIN_REJECT_P4_CRC);
    CHECK(chain.token_retired == V9X_FALSE);

    /* Reporting the replay twice is out of order and refused. */
    CHECK(v9x_i9xx_chain_begin(&chain, &request, combined, p4, p5) ==
          V9X_I9XX_CHAIN_OK);
    CHECK(v9x_i9xx_chain_replay_done(&chain, V9X_TRUE, p4, p4) ==
          V9X_I9XX_CHAIN_OK);
    CHECK(v9x_i9xx_chain_replay_done(&chain, V9X_TRUE, p4, p4) ==
          V9X_I9XX_CHAIN_REJECT_REPLAY);
}

static void test_chain_power_cut_leaves_in_flight(void)
{
    struct v9x_i9xx_arm_request request;
    struct v9x_i9xx_chain chain;
    const v9x_u32 p4 = 0xa0da64a1ul;
    const v9x_u32 p5 = 0x78780722ul;
    const v9x_u32 combined = v9x_i9xx_combined_arm_crc(p4, p5);

    v9x_chain_request(&request, V9X_I9XX_PHASE5, V9X_I9XX_PHASE5, combined);

    /*
     * A power cut is modelled as simply stopping: whatever state the chain is
     * in, the token must not be retired and in-flight must not be cleared
     * unless the draw completed. A token that silently became reusable after a
     * hang would defeat the one-shot arm, which is the whole safety model.
     */
    CHECK(v9x_i9xx_chain_begin(&chain, &request, combined, p4, p5) ==
          V9X_I9XX_CHAIN_OK);
    CHECK(chain.state == V9X_I9XX_CHAIN_STATE_ARMED);
    CHECK(chain.in_flight_cleared == V9X_FALSE);   /* cut here */

    CHECK(v9x_i9xx_chain_replay_done(&chain, V9X_TRUE, p4, p4) ==
          V9X_I9XX_CHAIN_OK);
    CHECK(chain.state == V9X_I9XX_CHAIN_STATE_REPLAYED);
    /* The interesting one: Phase 4 has completed successfully, and the token
     * is STILL in flight because the draw it authorised has not happened. */
    CHECK(chain.token_retired == V9X_FALSE);
    CHECK(chain.in_flight_cleared == V9X_FALSE);   /* cut here */

    /* A failed draw leaves it in flight too. */
    CHECK(v9x_i9xx_chain_draw_done(&chain, V9X_FALSE, p5, p5) ==
          V9X_I9XX_CHAIN_REJECT_P5_CRC);
    CHECK(chain.state == V9X_I9XX_CHAIN_STATE_FAILED);
    CHECK(chain.in_flight_cleared == V9X_FALSE);

    /* Only a completed draw retires the token, and it does both together. */
    CHECK(v9x_i9xx_chain_begin(&chain, &request, combined, p4, p5) ==
          V9X_I9XX_CHAIN_OK);
    CHECK(v9x_i9xx_chain_replay_done(&chain, V9X_TRUE, p4, p4) ==
          V9X_I9XX_CHAIN_OK);
    CHECK(v9x_i9xx_chain_draw_done(&chain, V9X_TRUE, p5, p5) ==
          V9X_I9XX_CHAIN_OK);
    CHECK(chain.state == V9X_I9XX_CHAIN_STATE_DREW);
    CHECK(chain.token_retired == V9X_TRUE);
    CHECK(chain.in_flight_cleared == V9X_TRUE);
}

static void test_phase5_sequence_range(void)
{
    struct v9x_i9xx_phase4_sequence state;

    /* Phase 5 numbers from 20, so a log line names its phase unambiguously. */
    v9x_i9xx_phase5_sequence_begin(&state);
    CHECK(v9x_i9xx_phase4_sequence_commit(&state, 20u) == V9X_STATUS_OK);
    CHECK(v9x_i9xx_phase4_sequence_commit(&state, 21u) == V9X_STATUS_OK);
    /* A Phase 4 step number cannot be committed to a Phase 5 sequence. */
    CHECK(v9x_i9xx_phase4_sequence_commit(&state, 3u) !=
          V9X_STATUS_OK);

    v9x_i9xx_phase5_sequence_begin(&state);
    CHECK(v9x_i9xx_phase4_sequence_commit(&state, 1u) != V9X_STATUS_OK);

    /* And the Phase 4 wrapper still behaves exactly as it did. */
    v9x_i9xx_phase4_sequence_begin(&state);
    CHECK(v9x_i9xx_phase4_sequence_commit(&state, 1u) == V9X_STATUS_OK);
    CHECK(v9x_i9xx_phase4_sequence_commit(&state, 20u) != V9X_STATUS_OK);
}

unsigned int v9x_run_i9xx_arm_tests(void)
{
    test_crc();
    test_arm_contract();
    test_arm_gate_for();
    test_chain_token_phases();
    test_chain_cannot_skip_replay();
    test_chain_power_cut_leaves_in_flight();
    test_phase5_sequence_range();
    test_phase4_sequence();
    return failures;
}
