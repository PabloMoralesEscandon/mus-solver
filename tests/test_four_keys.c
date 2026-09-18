#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mus_solver/mus_simplified.h"

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
static size_t betting_expansions;
static size_t largest_betting_walk;
static bool wager_masks[256];

typedef struct {
    uint64_t hash;
    MusFourGameModel model;
} BettingModelEntry;

typedef struct {
    BettingModelEntry *entries;
    size_t capacity;
    size_t count;
} BettingModels;

static uint64_t betting_model_hash(const MusFourGameModel *model) {
    const unsigned char *bytes = (const unsigned char *)model;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < sizeof(*model); ++i) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash == 0 ? 1 : hash;
}

static size_t betting_model_slot(const BettingModels *models,
                                  const MusFourGameModel *model, uint64_t hash) {
    size_t slot = (size_t)hash & (models->capacity - 1);
    while (models->entries[slot].hash != 0) {
        if (models->entries[slot].hash == hash &&
            memcmp(&models->entries[slot].model, model, sizeof(*model)) == 0)
            break;
        slot = (slot + 1) & (models->capacity - 1);
    }
    return slot;
}

static bool betting_model_seen(BettingModels *models, const MusFourGameModel *model) {
    if (models->capacity == 0 || models->count >= models->capacity / 2) {
        const size_t previous_capacity = models->capacity;
        BettingModelEntry *previous = models->entries;
        models->capacity = previous_capacity == 0 ? 1024 : previous_capacity * 2;
        models->entries = calloc(models->capacity, sizeof(*models->entries));
        CHECK(models->entries != NULL);
        for (size_t i = 0; i < previous_capacity; ++i) {
            if (previous[i].hash == 0)
                continue;
            const size_t slot = betting_model_slot(models, &previous[i].model,
                                                    previous[i].hash);
            models->entries[slot] = previous[i];
        }
        free(previous);
    }
    const uint64_t hash = betting_model_hash(model);
    const size_t slot = betting_model_slot(models, model, hash);
    if (models->entries[slot].hash != 0)
        return true;
    models->entries[slot].hash = hash;
    models->entries[slot].model = *model;
    models->count += 1;
    return false;
}

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
    CHECK(a->pending_response == b->pending_response);
    CHECK(a->last_response_player == b->last_response_player);
    CHECK(memcmp(a->last_responses, b->last_responses, sizeof(a->last_responses)) == 0);
    CHECK(memcmp(a->used_team_wager_action_masks, b->used_team_wager_action_masks,
                  sizeof(a->used_team_wager_action_masks)) == 0);
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
    CHECK((mus_score_game_matches_any(&game->game)
        ? mus_four_score_game_state_init(state, game)
        : mus_four_game_state_init(state)) == CFR_STATUS_SUCCESS);
    for (unsigned int player = 0; player < 4; ++player)
        for (unsigned int card_class = 0; card_class < 8; ++card_class)
            for (unsigned int copy = 0; copy < hands[player][card_class]; ++copy)
                apply(game, state, (Action)card_class);
    CHECK(state->model.phase == MUS_FOUR_PHASE_MUS);
    apply(game, state, MUS_ACTION_NO_MUS);
    CHECK(state->model.phase == MUS_FOUR_PHASE_GRANDE);
}

static void walk_betting_phase(const MusScoreGame *game, MusFourGameState *state,
                               unsigned int phase, BettingModels *models) {
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
    CHECK((state->model.used_wager_action_masks[0] & state->model.used_wager_action_masks[2]) == 0);
    CHECK((state->model.used_wager_action_masks[1] & state->model.used_wager_action_masks[3]) == 0);
    unsigned int mask = 0;
    for (unsigned int p = 0; p < 2; ++p)
        mask |= (unsigned int)observation.used_team_wager_action_masks[p] << (4U * p);
    CHECK(mask < sizeof(wager_masks) / sizeof(wager_masks[0]));
    wager_masks[mask] = true;
    betting_observations += 1;
    /* Check every arrival before deduplicating successors. In particular,
     * distinct models with colliding information-set keys must both be checked.
     * Undo history is deliberately excluded; this set belongs to one fixture
     * and one phase, so equal complete models have equal future transitions. */
    CHECK(betting_observations <= 1000000);
    if (betting_model_seen(models, &state->model))
        return;
    betting_expansions += 1;
    CHECK(betting_expansions <= 750000);
    CHECK(models->count <= 100000);
    for (size_t i = 0; i < count; ++i) {
        const MusFourGameModel before = state->model;
        const size_t undo_count = state->undo_count;
        apply(game, state, actions[i]);
        walk_betting_phase(game, state, phase, models);
        CHECK(cfr_game_undo_action(&game->game, raw(state)) == CFR_STATUS_SUCCESS);
        CHECK(state->undo_count == undo_count);
        CHECK(memcmp(&state->model, &before, sizeof(before)) == 0);
    }
}

static void test_public_response_keys(void) {
    static const uint8_t hands[4][8] = {
        {4,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,4},
        {0,4,0,0,0,0,0,0}, {0,0,4,0,0,0,0,0}};
    static const Action responses[] = {MUS_ACTION_REJECT, MUS_ACTION_ACCEPT,
        MUS_ACTION_BET_TWO, MUS_ACTION_BET_FOUR, MUS_ACTION_BET_TEN, MUS_ACTION_ORDAGO};
    static const uint8_t proposals[] = {MUS_FOUR_RESPONSE_REJECT, MUS_FOUR_RESPONSE_ACCEPT,
        MUS_FOUR_RESPONSE_BET_TWO, MUS_FOUR_RESPONSE_BET_FOUR,
        MUS_FOUR_RESPONSE_BET_TEN, MUS_FOUR_RESPONSE_ORDAGO};
    MusScoreTable *table = malloc(sizeof(*table));
    MusFourGameState *state = malloc(sizeof(*state));
    CHECK(table != NULL && state != NULL);
    CHECK(mus_score_table_init(table, 40) == CFR_STATUS_SUCCESS);
    table->four_player = true;
    MusScoreGame game;
    CHECK(mus_score_game_init(&game, table, 0, 0) == CFR_STATUS_SUCCESS);
    deal(&game, state, hands);
    apply(&game, state, MUS_ACTION_BET_TWO);
    const MusFourGameModel original = state->model;
    InfoSetKey keys[6];
    for (unsigned int r = 0; r < 6; ++r) {
        apply(&game, state, responses[r]);
        MusFourInformationSet observed, decoded;
        CHECK(mus_four_score_game_observe(state, &observed) == CFR_STATUS_SUCCESS);
        CHECK(observed.player == 3 && observed.pending_response == proposals[r]);
        CHECK(cfr_game_information_set_key(&game.game, raw(state), &keys[r]) == CFR_STATUS_SUCCESS);
        CHECK(mus_four_score_information_set_decode(keys[r], &decoded) == CFR_STATUS_SUCCESS);
        compare_observations(&observed, &decoded);
        for (unsigned int previous = 0; previous < r; ++previous)
            CHECK(keys[r] != keys[previous]);
        CHECK(cfr_game_undo_action(&game.game, raw(state)) == CFR_STATUS_SUCCESS);
        CHECK(memcmp(&state->model, &original, sizeof(original)) == 0);
    }
    /* A proposal is valid only for the second eligible teammate, and must
     * have been legal for the first teammate against the unchanged offer. */
    MusFourInformationSet invalid;
    CHECK(mus_four_score_game_observe(state, &invalid) == CFR_STATUS_SUCCESS);
    invalid.pending_response = MUS_FOUR_RESPONSE_ACCEPT;
    InfoSetKey untouched = 123;
    CHECK(mus_four_score_information_set_encode(&invalid, &untouched) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(untouched == 123);
    CHECK(cfr_game_undo_action(&game.game, raw(state)) == CFR_STATUS_SUCCESS);
    apply(&game, state, MUS_ACTION_BET_FOUR);
    apply(&game, state, MUS_ACTION_ACCEPT);
    CHECK(mus_four_score_game_observe(state, &invalid) == CFR_STATUS_SUCCESS);
    invalid.pending_response = MUS_FOUR_RESPONSE_BET_TWO;
    CHECK(mus_four_score_information_set_encode(&invalid, &untouched) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(untouched == 123);
    CHECK(cfr_game_undo_action(&game.game, raw(state)) == CFR_STATUS_SUCCESS);
    CHECK(cfr_game_undo_action(&game.game, raw(state)) == CFR_STATUS_SUCCESS);
    apply(&game, state, MUS_ACTION_ORDAGO);
    apply(&game, state, MUS_ACTION_ACCEPT);
    CHECK(mus_four_score_game_observe(state, &invalid) == CFR_STATUS_SUCCESS);
    invalid.pending_response = MUS_FOUR_RESPONSE_BET_TEN;
    CHECK(mus_four_score_information_set_encode(&invalid, &untouched) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(untouched == 123);
    invalid.pending_response = UINT8_MAX;
    CHECK(mus_four_score_information_set_encode(&invalid, &untouched) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(untouched == 123);
    state->model.pending_response = UINT8_MAX;
    const MusFourGameModel malformed = state->model;
    const size_t undo_count = state->undo_count;
    CHECK(cfr_game_apply_action(&game.game, raw(state), MUS_ACTION_ACCEPT) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(memcmp(&state->model, &malformed, sizeof(malformed)) == 0);
    CHECK(state->undo_count == undo_count);
    free(state);
    free(table);
}

static InfoSetKey checked_key(const MusScoreGame *game, MusFourGameState *state) {
    InfoSetKey key;
    MusFourInformationSet observed, decoded;
    CHECK(cfr_game_validate_state(&game->game, raw(state)) == CFR_STATUS_SUCCESS);
    const bool single_hand = state->model.score_target == 0;
    CHECK((single_hand ? mus_four_game_observe(state, &observed)
                       : mus_four_score_game_observe(state, &observed)) == CFR_STATUS_SUCCESS);
    CHECK(cfr_game_information_set_key(&game->game, raw(state), &key) == CFR_STATUS_SUCCESS);
    CHECK((single_hand ? mus_four_information_set_decode(key, &decoded)
                       : mus_four_score_information_set_decode(key, &decoded)) == CFR_STATUS_SUCCESS);
    compare_observations(&observed, &decoded);
    Action actions[16], decoded_actions[16];
    size_t count, decoded_count;
    CHECK(cfr_game_legal_actions(&game->game, raw(state), actions, 16, &count) == CFR_STATUS_SUCCESS);
    if (mus_simplified_game_matches(&game->game)) {
        CHECK(mus_simplified_four_information_set_legal_actions(
                  &decoded, !single_hand, decoded_actions, 16, &decoded_count) == CFR_STATUS_SUCCESS);
    } else {
        CHECK((single_hand ? mus_four_information_set_legal_actions(&decoded, decoded_actions, 16, &decoded_count)
                           : mus_four_score_information_set_legal_actions(&decoded, decoded_actions, 16, &decoded_count))
              == CFR_STATUS_SUCCESS);
    }
    CHECK(count == decoded_count && memcmp(actions, decoded_actions, count * sizeof(Action)) == 0);
    return key;
}

static void test_team_history_abstraction(void) {
    static const uint8_t hands[4][8] = {
        {4,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,4},
        {0,4,0,0,0,0,0,0}, {0,0,4,0,0,0,0,0}};
    MusScoreTable *table = malloc(sizeof(*table));
    MusFourGameState *states[2] = {malloc(sizeof(*states[0])), malloc(sizeof(*states[0]))};
    CHECK(table != NULL && states[0] != NULL && states[1] != NULL);
    for (unsigned int simplified = 0; simplified < 2; ++simplified) {
        CHECK(mus_score_table_init(table, 40) == CFR_STATUS_SUCCESS);
        table->four_player = true;
        table->simplified = simplified != 0;
        for (unsigned int score = 0; score < 2; ++score) {
            MusScoreGame game;
            if (score != 0) CHECK(mus_score_game_init(&game, table, 0, 0) == CFR_STATUS_SUCCESS);
            else game = (MusScoreGame){.game = *(simplified != 0
                ? mus_simplified_game_descriptor(true) : mus_four_game_descriptor())};
            /* Seat 0 or seat 2 opens with +2. Seat 3 raises +4, then seat 0
             * raises +10. The last offer and both proposals are identical. */
            for (unsigned int history = 0; history < 2; ++history) {
                MusFourGameState *state = states[history];
                deal(&game, state, hands);
                if (history != 0) {
                    apply(&game, state, MUS_ACTION_PASS);
                    apply(&game, state, MUS_ACTION_PASS);
                }
                apply(&game, state, MUS_ACTION_BET_TWO);
                apply(&game, state, history == 0 ? MUS_ACTION_REJECT : MUS_ACTION_BET_FOUR);
                apply(&game, state, history == 0 ? MUS_ACTION_BET_FOUR : MUS_ACTION_REJECT);
                CHECK(state->model.bettor == 3 && state->model.current_player == 0);
                apply(&game, state, MUS_ACTION_BET_TEN);
                apply(&game, state, MUS_ACTION_REJECT);
                CHECK(state->model.current_wager == 16);
                CHECK(state->model.used_wager_action_masks[0] == (history == 0 ? 9 : 8));
                CHECK(state->model.used_wager_action_masks[2] == (history == 0 ? 0 : 1));
            }
            const InfoSetKey shared = checked_key(&game, states[0]);
            CHECK(checked_key(&game, states[1]) == shared);
            MusFourInformationSet observed;
            CHECK((score != 0 ? mus_four_score_game_observe(states[0], &observed)
                              : mus_four_game_observe(states[0], &observed)) == CFR_STATUS_SUCCESS);
            CHECK(observed.used_team_wager_action_masks[0] == 9);
            CHECK(observed.used_team_wager_action_masks[1] == 2);

            /* The losing proposal still distinguishes replies inside a lance. */
            const MusFourGameModel before = states[0]->model;
            CHECK(cfr_game_undo_action(&game.game, raw(states[0])) == CFR_STATUS_SUCCESS);
            apply(&game, states[0], MUS_ACTION_ACCEPT);
            CHECK(checked_key(&game, states[0]) != shared);
            CHECK(cfr_game_undo_action(&game.game, raw(states[0])) == CFR_STATUS_SUCCESS);
            apply(&game, states[0], MUS_ACTION_REJECT);
            CHECK(memcmp(&states[0]->model, &before, sizeof(before)) == 0);
            CHECK(checked_key(&game, states[0]) == shared);

            /* Pending accept and reject differ. Their resolved team action
             * is accept in both histories, so the next lance shares a key. */
            apply(&game, states[0], MUS_ACTION_ACCEPT);
            apply(&game, states[1], MUS_ACTION_REJECT);
            const MusFourGameModel pending = states[0]->model;
            const InfoSetKey pending_key = checked_key(&game, states[0]);
            CHECK(checked_key(&game, states[1]) != pending_key);
            apply(&game, states[0], MUS_ACTION_REJECT);
            apply(&game, states[1], MUS_ACTION_ACCEPT);
            CHECK(states[0]->model.phase == MUS_FOUR_PHASE_CHICA);
            CHECK(states[0]->model.last_response_player == 0);
            CHECK(states[0]->model.last_responses[0] == MUS_FOUR_RESPONSE_NONE);
            CHECK(states[0]->model.last_responses[1] == MUS_FOUR_RESPONSE_NONE);
            CHECK(checked_key(&game, states[0]) == checked_key(&game, states[1]));
            CHECK(cfr_game_undo_action(&game.game, raw(states[0])) == CFR_STATUS_SUCCESS);
            CHECK(memcmp(&states[0]->model, &pending, sizeof(pending)) == 0);
            CHECK(checked_key(&game, states[0]) == pending_key);
            apply(&game, states[0], MUS_ACTION_REJECT);
            for (unsigned int history = 0; history < 2; ++history) {
                apply(&game, states[history], MUS_ACTION_PASS);
                apply(&game, states[history], MUS_ACTION_BET_TWO);
            }
            CHECK(checked_key(&game, states[0]) == checked_key(&game, states[1]));
        }
    }
    free(states[1]);
    free(states[0]);
    free(table);
}

static void test_resolved_response_memory(void) {
    static const uint8_t hands[4][8] = {
        {4,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,4},
        {0,4,0,0,0,0,0,0}, {0,0,4,0,0,0,0,0}};
    static const Action proposals[] = {MUS_ACTION_REJECT, MUS_ACTION_ACCEPT, MUS_ACTION_BET_FOUR};
    static const uint8_t ranks[] = {MUS_FOUR_RESPONSE_REJECT, MUS_FOUR_RESPONSE_ACCEPT,
                                   MUS_FOUR_RESPONSE_BET_FOUR};
    MusScoreTable *table = malloc(sizeof(*table));
    MusFourGameState *state = malloc(sizeof(*state));
    CHECK(table != NULL && state != NULL);
    CHECK(mus_score_table_init(table, 40) == CFR_STATUS_SUCCESS);
    table->four_player = true;
    MusScoreGame game;
    CHECK(mus_score_game_init(&game, table, 0, 0) == CFR_STATUS_SUCCESS);
    InfoSetKey resolved[3], pending[3], replaced[3];
    for (unsigned int r = 0; r < 3; ++r) {
        deal(&game, state, hands);
        for (unsigned int seat = 0; seat < 3; ++seat) apply(&game, state, MUS_ACTION_PASS);
        apply(&game, state, MUS_ACTION_BET_TWO);  /* 3 opens; 0 and 2 respond. */
        apply(&game, state, proposals[r]);
        const MusFourGameModel before_resolution = state->model;
        apply(&game, state, MUS_ACTION_BET_TEN);
        CHECK(state->model.current_player == 3 && state->model.bettor == 2);
        CHECK(state->model.current_wager == 12);
        CHECK(state->model.last_response_player == 0);
        CHECK(state->model.last_responses[0] == ranks[r]);
        CHECK(state->model.last_responses[1] == MUS_FOUR_RESPONSE_BET_TEN);
        resolved[r] = checked_key(&game, state);
        CHECK(cfr_game_undo_action(&game.game, raw(state)) == CFR_STATUS_SUCCESS);
        CHECK(memcmp(&state->model, &before_resolution, sizeof(before_resolution)) == 0);
        apply(&game, state, MUS_ACTION_BET_TEN);
        CHECK(checked_key(&game, state) == resolved[r]);

        /* The next pair's first proposal must not erase the resolved pair. */
        apply(&game, state, MUS_ACTION_ACCEPT);
        CHECK(state->model.current_player == 1);
        CHECK(state->model.last_responses[0] == ranks[r]);
        pending[r] = checked_key(&game, state);
        for (unsigned int earlier = 0; earlier < r; ++earlier) {
            CHECK(resolved[r] != resolved[earlier]);
            CHECK(pending[r] != pending[earlier]);
        }
        apply(&game, state, MUS_ACTION_REJECT);
        CHECK(state->model.phase == MUS_FOUR_PHASE_CHICA);
        CHECK(state->model.last_response_player == 0);
        CHECK(state->model.last_responses[0] == MUS_FOUR_RESPONSE_NONE);
        CHECK(state->model.last_responses[1] == MUS_FOUR_RESPONSE_NONE);
        replaced[r] = checked_key(&game, state);
        if (r != 0) CHECK(replaced[r] == replaced[0]);
    }

    /* Three ways of accepting the same offer share a key in the next
     * lance, including after opening passes and an opening numeric wager. */
    InfoSetKey next_lance[3], after_pass[3], after_open[3];
    static const Action accepted[3][2] = {
        {MUS_ACTION_REJECT, MUS_ACTION_ACCEPT},
        {MUS_ACTION_ACCEPT, MUS_ACTION_REJECT},
        {MUS_ACTION_ACCEPT, MUS_ACTION_ACCEPT}};
    for (unsigned int r = 0; r < 3; ++r) {
        deal(&game, state, hands);
        apply(&game, state, MUS_ACTION_BET_TWO);
        apply(&game, state, accepted[r][0]);
        apply(&game, state, accepted[r][1]);
        CHECK(state->model.phase == MUS_FOUR_PHASE_CHICA);
        next_lance[r] = checked_key(&game, state);
        apply(&game, state, MUS_ACTION_PASS);
        after_pass[r] = checked_key(&game, state);
        apply(&game, state, MUS_ACTION_BET_FOUR);
        after_open[r] = checked_key(&game, state);
        for (unsigned int earlier = 0; earlier < r; ++earlier) {
            CHECK(next_lance[r] == next_lance[earlier]);
            CHECK(after_pass[r] == after_pass[earlier]);
            CHECK(after_open[r] == after_open[earlier]);
        }
        MusFourInformationSet invalid;
        CHECK(mus_four_score_game_observe(state, &invalid) == CFR_STATUS_SUCCESS);
        invalid.last_response_player = 4;
        InfoSetKey untouched = 123;
        CHECK(mus_four_score_information_set_encode(&invalid, &untouched) == CFR_STATUS_INVALID_ARGUMENT);
        CHECK(untouched == 123);
    }
    /* An opening ordago rejection can make public scores plus accepted
     * wagers reach 40 before those wagers are counted. Empty masks must
     * retain the full score range in the compact board encoding. */
    CHECK(mus_score_game_init(&game, table, 29, 29) == CFR_STATUS_SUCCESS);
    deal(&game, state, hands);
    apply(&game, state, MUS_ACTION_BET_TEN);
    apply(&game, state, MUS_ACTION_ACCEPT);
    apply(&game, state, MUS_ACTION_REJECT);
    CHECK(state->model.phase == MUS_FOUR_PHASE_CHICA);
    CHECK(state->model.prior_wager_total == 10);
    apply(&game, state, MUS_ACTION_ORDAGO);
    apply(&game, state, MUS_ACTION_REJECT);
    apply(&game, state, MUS_ACTION_REJECT);
    CHECK(state->model.phase == MUS_FOUR_PHASE_PARES);
    CHECK(state->model.initial_scores[0] + state->model.paid_scores[0] +
          state->model.prior_wager_total == 40);
    (void)checked_key(&game, state);
    apply(&game, state, MUS_ACTION_ORDAGO);
    (void)checked_key(&game, state);
    free(state);
    free(table);
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
                BettingModels models = {0};
                walk_betting_phase(&game, state, phase, &models);
                if (models.count > largest_betting_walk)
                    largest_betting_walk = models.count;
                free(models.entries);
            }
        }
    }
    size_t masks = 0;
    for (size_t mask = 0; mask < sizeof(wager_masks) / sizeof(wager_masks[0]); ++mask)
        masks += wager_masks[mask] ? 1U : 0U;
    CHECK(masks == 51);
    CHECK(betting_observations > 60000);
    CHECK(betting_expansions < betting_observations);
    printf("Four-player keys passed: %zu exact hands, %zu betting observations, "
           "%zu distinct models expanded (largest walk %zu), %zu wager masks.\n",
           hand_count, betting_observations, betting_expansions, largest_betting_walk, masks);
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
    test_public_response_keys();
    test_team_history_abstraction();
    test_resolved_response_memory();
    test_reachable_betting_keys();
    return EXIT_SUCCESS;
}
