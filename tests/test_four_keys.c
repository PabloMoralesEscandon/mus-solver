#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mus_solver/mus_four.h"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, __LINE__,  \
                    #condition);                                                \
            exit(EXIT_FAILURE);                                                 \
        }                                                                       \
    } while (0)

static InfoSetKey exact_keys[330 * 4 * 2];
static size_t exact_key_count;
static size_t hand_count;
static size_t betting_observations;
static bool wager_masks[65536];

static GameState *raw(MusFourGameState *state) {
    return (GameState *)state;
}

static void compare_observations(const MusFourInformationSet *a,
                                 const MusFourInformationSet *b) {
    CHECK(a->phase == b->phase && a->player == b->player);
    CHECK(a->hand_code == b->hand_code);
    CHECK(memcmp(a->hand, b->hand, sizeof(a->hand)) == 0);
    CHECK(a->mus_count == b->mus_count && a->mus_stopped == b->mus_stopped);
    CHECK(a->mus_stop_player == b->mus_stop_player);
    CHECK(memcmp(a->discard_counts, b->discard_counts, sizeof(a->discard_counts)) == 0);
    CHECK(memcmp(a->scores, b->scores, sizeof(a->scores)) == 0);
    CHECK(a->prior_wager_total == b->prior_wager_total);
    CHECK(memcmp(a->previous_strengths, b->previous_strengths,
                  sizeof(a->previous_strengths)) == 0);
    CHECK(a->eligible_mask == b->eligible_mask && a->bettor == b->bettor);
    CHECK(a->current_wager == b->current_wager && a->previous_wager == b->previous_wager);
    CHECK(a->current_wager_is_ordago == b->current_wager_is_ordago);
    CHECK(a->teammate_rejected == b->teammate_rejected);
    CHECK(memcmp(a->used_wager_action_masks, b->used_wager_action_masks,
                  sizeof(a->used_wager_action_masks)) == 0);
}

static void check_exact_hand(const uint8_t hand[8]) {
    hand_count += 1;
    for (unsigned int player = 0; player < 4; ++player) {
        for (unsigned int discard = 0; discard < 2; ++discard) {
            MusFourInformationSet information = {
                .phase = discard != 0 ? MUS_FOUR_PHASE_DISCARD : MUS_FOUR_PHASE_MUS,
                .player = (uint8_t)player,
            };
            memcpy(information.hand, hand, sizeof(information.hand));
            if (discard != 0)
                for (unsigned int p = 0; p < player; ++p)
                    information.discard_counts[0][p] = 1;
            InfoSetKey key, encoded;
            MusFourInformationSet decoded;
            CHECK(mus_four_score_information_set_encode(&information, &key) == CFR_STATUS_SUCCESS);
            CHECK(exact_key_count < sizeof(exact_keys) / sizeof(exact_keys[0]));
            exact_keys[exact_key_count++] = key;
            CHECK(mus_four_score_information_set_decode(key, &decoded) == CFR_STATUS_SUCCESS);
            CHECK(memcmp(decoded.hand, hand, sizeof(decoded.hand)) == 0);
            CHECK(decoded.player == player && decoded.phase == information.phase);
            CHECK(mus_four_score_information_set_encode(&decoded, &encoded) == CFR_STATUS_SUCCESS);
            CHECK(encoded == key);
            Action actions[16], decoded_actions[16];
            size_t count, decoded_count;
            CHECK(mus_four_score_information_set_legal_actions(
                      &information, actions, 16, &count) == CFR_STATUS_SUCCESS);
            CHECK(mus_four_score_information_set_legal_actions(
                      &decoded, decoded_actions, 16, &decoded_count) == CFR_STATUS_SUCCESS);
            CHECK(count == decoded_count);
            CHECK(memcmp(actions, decoded_actions, count * sizeof(Action)) == 0);
            CHECK(count != 0);
        }
    }
}

static void enumerate_hands(uint8_t hand[8], unsigned int card_class,
                             unsigned int remaining) {
    if (card_class == 7) {
        hand[7] = (uint8_t)remaining;
        check_exact_hand(hand);
        return;
    }
    for (unsigned int count = 0; count <= remaining; ++count) {
        hand[card_class] = (uint8_t)count;
        enumerate_hands(hand, card_class + 1, remaining - count);
    }
}

static int compare_keys(const void *a, const void *b) {
    const InfoSetKey left = *(const InfoSetKey *)a;
    const InfoSetKey right = *(const InfoSetKey *)b;
    return (left > right) - (left < right);
}

static void apply(const MusScoreGame *game, MusFourGameState *state, Action action) {
    CHECK(cfr_game_apply_action(&game->game, raw(state), action) == CFR_STATUS_SUCCESS);
}

static void deal(const MusScoreGame *game, MusFourGameState *state,
                  const uint8_t hands[4][8]) {
    CHECK(mus_four_score_game_state_init(state, game) == CFR_STATUS_SUCCESS);
    for (unsigned int player = 0; player < 4; ++player)
        for (unsigned int card_class = 0; card_class < 8; ++card_class)
            for (unsigned int copy = 0; copy < hands[player][card_class]; ++copy)
                apply(game, state, (Action)card_class);
    CHECK(state->model.phase == MUS_FOUR_PHASE_MUS);
    apply(game, state, MUS_ACTION_NO_MUS);
    CHECK(state->model.phase == MUS_FOUR_PHASE_GRANDE);
}

static void walk_betting_phase(const MusScoreGame *game, MusFourGameState *state,
                               unsigned int phase) {
    if (state->model.phase != phase)
        return;
    CHECK(cfr_game_validate_state(&game->game, raw(state)) == CFR_STATUS_SUCCESS);
    MusFourInformationSet observation, decoded;
    InfoSetKey key;
    Action actions[16], decoded_actions[16];
    size_t count, decoded_count;
    CHECK(mus_four_score_game_observe(state, &observation) == CFR_STATUS_SUCCESS);
    CHECK(cfr_game_information_set_key(&game->game, raw(state), &key) == CFR_STATUS_SUCCESS);
    CHECK(mus_four_score_information_set_decode(key, &decoded) == CFR_STATUS_SUCCESS);
    compare_observations(&observation, &decoded);
    CHECK(cfr_game_legal_actions(&game->game, raw(state), actions, 16, &count) ==
          CFR_STATUS_SUCCESS);
    CHECK(mus_four_score_information_set_legal_actions(&decoded, decoded_actions,
                                                       16, &decoded_count) ==
          CFR_STATUS_SUCCESS);
    CHECK(count == decoded_count);
    CHECK(memcmp(actions, decoded_actions, count * sizeof(Action)) == 0);
    unsigned int mask = 0;
    for (unsigned int p = 0; p < 4; ++p)
        mask |= (unsigned int)observation.used_wager_action_masks[p] << (4U * p);
    CHECK(mask < sizeof(wager_masks) / sizeof(wager_masks[0]));
    wager_masks[mask] = true;
    betting_observations += 1;
    for (size_t i = 0; i < count; ++i) {
        const MusFourGameModel before = state->model;
        const size_t undo_count = state->undo_count;
        apply(game, state, actions[i]);
        walk_betting_phase(game, state, phase);
        CHECK(cfr_game_undo_action(&game->game, raw(state)) == CFR_STATUS_SUCCESS);
        CHECK(state->undo_count == undo_count);
        CHECK(memcmp(&state->model, &before, sizeof(before)) == 0);
    }
}

static void test_reachable_betting_keys(void) {
    /* These hands cover all four eligible seats, a single game declaration,
     * and punto. Every fixture obeys the actual deck capacities. */
    static const uint8_t hands[][4][8] = {
        {{4,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,4},
         {0,4,0,0,0,0,0,0}, {0,0,4,0,0,0,0,0}},
        {{1,0,0,0,0,0,0,3}, {1,0,0,0,0,0,0,3},
         {0,0,0,0,0,3,0,1}, {0,0,0,0,0,0,3,1}},
        {{1,1,1,1,0,0,0,0}, {1,1,1,1,0,0,0,0},
         {1,1,1,1,0,0,0,0}, {1,1,1,1,0,0,0,0}}
    };
    static const unsigned int scores[] = {0, 35, 39};
    MusScoreTable *table = malloc(sizeof(*table));
    MusFourGameState *state = malloc(sizeof(*state));
    CHECK(table != NULL && state != NULL);
    CHECK(mus_score_table_init(table, 40) == CFR_STATUS_SUCCESS);
    table->four_player = true;
    for (size_t h = 0; h < sizeof(hands) / sizeof(hands[0]); ++h) {
        for (size_t score = 0; score < sizeof(scores) / sizeof(scores[0]); ++score) {
            MusScoreGame game;
            CHECK(mus_score_game_init(&game, table, scores[score], scores[score]) ==
                  CFR_STATUS_SUCCESS);
            for (unsigned int phase = MUS_FOUR_PHASE_GRANDE;
                 phase <= MUS_FOUR_PHASE_PUNTO; ++phase) {
                deal(&game, state, hands[h]);
                while (state->model.phase < phase)
                    apply(&game, state, MUS_ACTION_PASS);
                walk_betting_phase(&game, state, phase);
            }
        }
    }
    size_t masks = 0;
    for (size_t mask = 0; mask < sizeof(wager_masks) / sizeof(wager_masks[0]); ++mask)
        masks += wager_masks[mask] ? 1U : 0U;
    CHECK(masks == 755);
    CHECK(betting_observations > 60000);
    printf("Four-player keys passed: %zu exact hands, %zu betting observations, %zu wager masks.\n",
           hand_count, betting_observations, masks);
    free(state);
    free(table);
}

int main(void) {
    uint8_t hand[8] = {0};
    enumerate_hands(hand, 0, 4);
    CHECK(hand_count == 330 && exact_key_count == 330 * 4 * 2);
    qsort(exact_keys, exact_key_count, sizeof(exact_keys[0]), compare_keys);
    for (size_t i = 1; i < exact_key_count; ++i)
        CHECK(exact_keys[i - 1] != exact_keys[i]);
    test_reachable_betting_keys();
    return EXIT_SUCCESS;
}
