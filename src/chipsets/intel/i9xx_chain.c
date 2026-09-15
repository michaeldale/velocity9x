/*
 * The two-phase arm transaction.
 *
 * Phase 5 replays Phase 4 first, because Phase 4 is the only thing that can
 * distinguish "the layout move broke the ring" from "the 3D packets hung the
 * parser", and it costs milliseconds. But a replay inside a Phase 5 run is not
 * a Phase 4 run: the standalone Phase 4 path records its result and clears
 * IntelInFlight on success, and doing that here would retire the token before
 * the draw it was issued for had happened.
 *
 * So this is one transaction over two executions, with three properties the
 * host tests assert directly:
 *
 *   - a Phase 4 token cannot reach Phase 5;
 *   - a Phase 5 token cannot skip a failed replay;
 *   - a power cut between the phases leaves IntelInFlight set.
 *
 * The third is the one that matters most and the easiest to lose. It is why
 * the token is retired in exactly one place - the draw result - and why every
 * failure path marks the chain failed WITHOUT clearing in-flight. A token that
 * silently became reusable after a hang would defeat the one-shot arm that is
 * the whole safety model.
 */
#include "velocity9x/intel_gma.h"

/*
 * The combined CRC covers both streams in execution order, so the thing armed
 * is the whole reviewed sequence rather than either half of it. A token
 * carrying only the Phase 4 CRC therefore cannot authorise the chain even if
 * its phase field were forged, because it would not match this.
 */
v9x_u32 v9x_i9xx_combined_arm_crc(v9x_u32 phase4_crc, v9x_u32 phase5_crc)
{
    v9x_u32 pair[2];

    pair[0] = phase4_crc;
    pair[1] = phase5_crc;
    return v9x_i9xx_crc32_dwords(pair, 2ul);
}

/*
 * Which gate a consumed token must pass.
 *
 * The absent-key case is the one that needs explaining. arm-intel-phase4.ps1
 * has never written IntelArmPhase, so every Phase 4 stick reads back as zero,
 * and zero must therefore keep meaning Phase 4 or those sticks stop working -
 * including ones already written and sitting on a USB key. That is a
 * compatibility reading, not a default to be relied on, and it costs nothing
 * in safety: the property that matters is that zero is not 5, so an absent key
 * can never authorise a draw. Phase 5's armer writes the key explicitly and
 * its own self-test refuses to ship a stick without it.
 *
 * A Phase 5 token in a build with no Phase 5 refuses rather than arming.
 * Arming would consume the token for a draw that cannot happen, and since only
 * the draw result may retire it, the token would then stay in flight forever -
 * a stick that needs editing by hand to recover.
 */
v9x_u16 v9x_i9xx_arm_gate_for(v9x_u16 armed, v9x_u16 arm_phase,
                               v9x_u16 phase5_built)
{
    if (armed == 0u) {
        return V9X_I9XX_GATE_NONE;
    }
    if (arm_phase == 0u || arm_phase == V9X_I9XX_PHASE4) {
        return V9X_I9XX_GATE_STANDALONE;
    }
    if (arm_phase == V9X_I9XX_PHASE5) {
        return phase5_built != 0u ? V9X_I9XX_GATE_CHAINED
                                  : V9X_I9XX_GATE_REFUSE;
    }
    /* A phase this build has no vocabulary for. Refusing is the only honest
     * answer: guessing which phase was meant is what costs an armed boot. */
    return V9X_I9XX_GATE_REFUSE;
}

static void v9x_i9xx_chain_fail(struct v9x_i9xx_chain *chain)
{
    chain->state = V9X_I9XX_CHAIN_STATE_FAILED;
    /*
     * Deliberately does NOT clear in-flight and does NOT retire the token.
     * A failed chain leaves the arm consumed-but-unresolved, which is what
     * makes the next boot refuse rather than silently retry.
     */
}

v9x_u16 v9x_i9xx_chain_begin(
    struct v9x_i9xx_chain *chain,
    const struct v9x_i9xx_arm_request *request,
    v9x_u32 combined_crc, v9x_u32 phase4_crc, v9x_u32 phase5_crc)
{
    v9x_u16 rejection = V9X_I9XX_ARM_REJECT_NONE;

    if (chain == 0 || request == 0) {
        return V9X_I9XX_CHAIN_REJECT_ARM;
    }
    chain->state = V9X_I9XX_CHAIN_STATE_IDLE;
    chain->token_retired = V9X_FALSE;
    chain->in_flight_cleared = V9X_FALSE;

    /* The token must claim Phase 5 and the caller must be arming Phase 5. */
    if (request->phase != V9X_I9XX_PHASE5 ||
        request->expected_phase != V9X_I9XX_PHASE5) {
        v9x_i9xx_chain_fail(chain);
        return V9X_I9XX_CHAIN_REJECT_PHASE;
    }
    /* Everything the standalone path checks - build id, token, identity,
     * errata gate, safe mode - still applies. */
    if (v9x_i9xx_arm_evaluate(request, &rejection) != V9X_STATUS_OK) {
        v9x_i9xx_chain_fail(chain);
        return V9X_I9XX_CHAIN_REJECT_ARM;
    }
    /*
     * And the armed CRC must be the COMBINED one, covering the Phase 4 replay
     * and the Phase 5 draw in execution order. This is the check that a Phase
     * 4 token fails even with its phase field altered.
     */
    if (phase4_crc == 0ul || phase5_crc == 0ul || combined_crc == 0ul ||
        combined_crc != v9x_i9xx_combined_arm_crc(phase4_crc, phase5_crc) ||
        request->packet_crc != combined_crc) {
        v9x_i9xx_chain_fail(chain);
        return V9X_I9XX_CHAIN_REJECT_COMBINED;
    }

    chain->state = V9X_I9XX_CHAIN_STATE_ARMED;
    return V9X_I9XX_CHAIN_OK;
}

v9x_u16 v9x_i9xx_chain_replay_done(
    struct v9x_i9xx_chain *chain, v9x_u16 replay_passed,
    v9x_u32 observed_phase4_crc, v9x_u32 expected_phase4_crc)
{
    if (chain == 0) {
        return V9X_I9XX_CHAIN_REJECT_ARM;
    }
    if (chain->state != V9X_I9XX_CHAIN_STATE_ARMED) {
        /* Out of order, which includes trying to report a replay twice. */
        v9x_i9xx_chain_fail(chain);
        return V9X_I9XX_CHAIN_REJECT_REPLAY;
    }
    if (replay_passed == V9X_FALSE) {
        v9x_i9xx_chain_fail(chain);
        return V9X_I9XX_CHAIN_REJECT_REPLAY;
    }
    /*
     * The replay must still satisfy Phase 4's OWN generated stream and CRC
     * gate. Chaining widens what may be armed; it does not relax what each
     * half has to prove.
     */
    if (expected_phase4_crc == 0ul ||
        observed_phase4_crc != expected_phase4_crc) {
        v9x_i9xx_chain_fail(chain);
        return V9X_I9XX_CHAIN_REJECT_P4_CRC;
    }

    chain->state = V9X_I9XX_CHAIN_STATE_REPLAYED;
    return V9X_I9XX_CHAIN_OK;
}

v9x_u16 v9x_i9xx_chain_draw_done(
    struct v9x_i9xx_chain *chain, v9x_u16 draw_passed,
    v9x_u32 observed_phase5_crc, v9x_u32 expected_phase5_crc)
{
    if (chain == 0) {
        return V9X_I9XX_CHAIN_REJECT_ARM;
    }
    /* A Phase 5 token cannot skip a failed replay: the only state from which
     * a draw may be reported is one where the replay passed. */
    if (chain->state != V9X_I9XX_CHAIN_STATE_REPLAYED) {
        v9x_i9xx_chain_fail(chain);
        return V9X_I9XX_CHAIN_REJECT_REPLAY;
    }
    if (draw_passed == V9X_FALSE) {
        v9x_i9xx_chain_fail(chain);
        return V9X_I9XX_CHAIN_REJECT_P5_CRC;
    }
    if (expected_phase5_crc == 0ul ||
        observed_phase5_crc != expected_phase5_crc) {
        v9x_i9xx_chain_fail(chain);
        return V9X_I9XX_CHAIN_REJECT_P5_CRC;
    }

    /*
     * The one place a chained token is retired and in-flight cleared. Both
     * happen together and only here, so there is no reachable state in which
     * the token is spent but the driver believes the run completed.
     */
    chain->state = V9X_I9XX_CHAIN_STATE_DREW;
    chain->token_retired = V9X_TRUE;
    chain->in_flight_cleared = V9X_TRUE;
    return V9X_I9XX_CHAIN_OK;
}
