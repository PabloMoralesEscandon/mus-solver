/* Compare the optimized shortlist with exhaustive grid enumeration.
 * Include the private implementation to check all 64 candidates, including
 * candidates which a populated strategy would never need to query. */
#include "../src/mus_adapter.c"
#include <stdio.h>

/* Frozen pre-J18 enumeration, without distance pruning. Keep this exhaustive
 * oracle independent of optimizations in the production search. */
static void reference_candidates(const MusAdapter *a, const MusAdapterObservation *o,
                               const MusOrdagoInformationSet *base, const RealBet *real,
                               Candidate best[MUS_ADAPTER_MAX_LOOKUPS], size_t *count) {
    *count = 0;
    if ((base->score_enabled || a->public_generic) && o->pending_wager_total > 40)
        return;
    bool response = real->stage == MUS_BET_RESPONSE;
    size_t templates = response ? a->template_count : 1;
    for (size_t t_index = 0; t_index < templates; ++t_index) {
        BetTemplate t = response ? a->templates[t_index] : (BetTemplate){0};
        if (response && (t.player != o->player || t.ordago != real->ordago ||
                         ((t.covered == 1) !=
                          (real->ordago ? real->count == 0 : real->count == 1))))
            continue;
        Candidate c = {.information = *base, .mismatch = mismatch(real, &t)};
        if (response) {
            c.information.bet_stage = MUS_BET_RESPONSE;
            c.information.bettor = t.bettor;
            c.information.current_wager = t.total;
            c.information.previous_wager = t.covered;
            c.information.current_wager_is_ordago = t.ordago;
            c.information.numeric_wager_count = t.count;
            memcpy(c.information.used_wager_action_masks, t.masks, sizeof(t.masks));
        } else
            c.information.bet_stage = real->stage;
        int active =
            response ? (t.ordago ? (t.covered == 1 ? 0 : t.covered) : t.total) : 0;
        if (base->score_enabled || a->public_generic) {
            /* Scores and pending contracts are public. Never move them to find
             * another node. Only approximate the current betting sequence. */
            c.distance = difference(real->ordago ? 40 : real->total, t.total) +
                         difference(real->covered, t.covered);
            if (encode_information(a, &c.information, &c.key) == CFR_STATUS_SUCCESS)
                insert_candidate(best, count, c);
            continue;
        }
        int upper = o->previous_strength_count ? 40 : 0;
        for (int prior = 0; prior <= upper; ++prior) {
            for (int score = 0; score <= upper; ++score) {
                if (active && score + prior + active >= 40)
                    continue;
                c.distance = difference(real->ordago ? 40 : real->total, t.total) +
                             difference(real->covered, t.covered) +
                             difference(o->prior_wager_total, prior) +
                             difference(base->max_settled_score, score);
                c.information.prior_wager_total = (uint8_t)prior;
                c.information.max_settled_score = (uint8_t)score;
                if (encode_information(a, &c.information, &c.key) ==
                    CFR_STATUS_SUCCESS)
                    insert_candidate(best, count, c);
            }
        }
    }
}


int main(void) {
    InfoStore store = {0};
    MusAdapter *adapter = NULL;
    if (mus_adapter_create(&store, NULL, 7, &adapter) != CFR_STATUS_SUCCESS)
        return 1;
    const MusGamePhase phases[] = {MUS_PHASE_GRANDE, MUS_PHASE_CHICA,
        MUS_PHASE_PARES, MUS_PHASE_JUEGO, MUS_PHASE_PUNTO};
    const int64_t boundaries[] = {0, 1, 19, 20, 38, 39, 40, 41,
                                  MUS_ADAPTER_MAX_AMOUNT};
    size_t cases = 0;
    for (int format = 0; format < 3; ++format) {
        adapter->public_generic = format == 1;
        for (size_t phase = 0; phase < sizeof(phases) / sizeof(phases[0]); ++phase) {
            for (size_t sample = 0; sample < 90; ++sample) {
                BetTemplate t = adapter->templates[(sample * 37) % adapter->template_count];
                RealBet real = {.stage = sample % 9 == 0 ? MUS_BET_FIRST_TO_ACT
                    : sample % 9 == 1 ? MUS_BET_SECOND_AFTER_PASS : MUS_BET_RESPONSE,
                    .total = t.total + (int64_t)(sample % 4), .covered = t.covered,
                    .count = t.count, .ordago = t.ordago};
                memcpy(real.masks, t.masks, sizeof(real.masks));
                if (sample % 11 == 0) {
                    real.total = MUS_ADAPTER_MAX_AMOUNT;
                    real.covered = MUS_ADAPTER_MAX_AMOUNT - 1;
                }
                MusAdapterObservation observation = {.player = t.player,
                    .previous_strength_count = (uint8_t)(phase > 3 ? 3 : phase),
                    .prior_wager_total = boundaries[sample % 9],
                    .pending_wager_total = sample % 5 == 0 ? 41 : (int64_t)(sample % 7)};
                MusOrdagoInformationSet base = {.phase = phases[phase],
                    .player = t.player, .score_enabled = format == 2,
                    .mus_stopped = true,
                    .previous_strength_count = observation.previous_strength_count,
                    .max_settled_score = (uint8_t)((sample * 13) % 41),
                    .prior_wager_total = (uint8_t)(sample % 7)};
                if (format) {
                    base.scores[0] = (uint8_t)(sample % 39);
                    base.scores[1] = (uint8_t)((sample * 7) % 39);
                    base.max_settled_score = base.scores[0] > base.scores[1]
                        ? base.scores[0] : base.scores[1];
                }
                uint8_t hand[8] = {0};
                hand[phase == 4 ? 0 : 7] = 4;
                if (mus_ordago_game_information_set_set_hand(&base, hand) != CFR_STATUS_SUCCESS)
                    return 1;
                Candidate actual[64], expected[64];
                size_t actual_count, expected_count;
                nearest_candidates(adapter, &observation, &base, &real, actual, &actual_count);
                reference_candidates(adapter, &observation, &base, &real, expected, &expected_count);
                if (actual_count != expected_count) {
                    fprintf(stderr, "Count differs: format=%d phase=%zu sample=%zu\n", format, phase, sample);
                    return 1;
                }
                for (size_t i = 0; i < actual_count; ++i)
                    if (compare_candidates(&actual[i], &expected[i]) != 0) {
                        fprintf(stderr, "Candidate differs: format=%d phase=%zu sample=%zu index=%zu\n", format, phase, sample, i);
                        return 1;
                    }
                ++cases;
            }
        }
    }
    mus_adapter_destroy(adapter);
    printf("Candidate enumeration: %zu cases passed\n", cases);
    return 0;
}
