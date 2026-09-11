#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfr/checkpoint.h"
#include "cfr/evaluation.h"
#include "cfr/game.h"
#include "cfr/info_store.h"
#include "cfr/kuhn_poker.h"
#include "cfr/trainer.h"
#include "baraja_espanola.h"
#include "mus.h"
#include "mus_solver/mus_evaluation.h"
#include "mus_solver/mus_game.h"
#include "mus_solver/mus_strategy.h"
#include "mus_solver/mus_score.h"
#include "mus_solver/mus_training.h"
#include "../app/reservation_internal.h"

static int failures;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: fallo: %s\n", __FILE__, __LINE__,         \
                    #condition);                                                \
            failures += 1;                                                      \
        }                                                                      \
    } while (0)

static const Game *game(void) {
    return mus_game_descriptor();
}

static GameState *game_state(MusGameState *state) {
    return mus_game_state_as_game_state(state);
}

static void apply(MusGameState *state, Action action) {
    CHECK(cfr_game_apply_action(game(), game_state(state), action) ==
          CFR_STATUS_SUCCESS);
}

static void deal(MusGameState *state,
                 const uint8_t player_0[MUS_GAME_CLASS_COUNT],
                 const uint8_t player_1[MUS_GAME_CLASS_COUNT]) {
    CHECK(mus_game_state_init(state) == CFR_STATUS_SUCCESS);
    for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT - 1;
         card_class += 1)
        apply(state, player_0[card_class]);
    for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT - 1;
         card_class += 1)
        apply(state, player_1[card_class]);
    CHECK(state->model.phase == MUS_PHASE_GRANDE);
    CHECK(memcmp(state->model.hands[0], player_0, MUS_GAME_CLASS_COUNT) == 0);
    CHECK(memcmp(state->model.hands[1], player_1, MUS_GAME_CLASS_COUNT) == 0);
}

static const Game *raises_game(void) {
    return mus_raises_to_eight_game_descriptor();
}

static GameState *raises_game_state(MusRaisesToEightGameState *state) {
    return mus_raises_to_eight_game_state_as_game_state(state);
}

static void raises_apply(MusRaisesToEightGameState *state, Action action) {
    CHECK(cfr_game_apply_action(raises_game(), raises_game_state(state),
                                action) == CFR_STATUS_SUCCESS);
}

static void raises_deal(
    MusRaisesToEightGameState *state,
    const uint8_t player_0[MUS_GAME_CLASS_COUNT],
    const uint8_t player_1[MUS_GAME_CLASS_COUNT]) {
    CHECK(mus_raises_to_eight_game_state_init(state) == CFR_STATUS_SUCCESS);
    for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT - 1;
         card_class += 1)
        raises_apply(state, player_0[card_class]);
    for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT - 1;
         card_class += 1)
        raises_apply(state, player_1[card_class]);
    CHECK(state->model.phase == MUS_PHASE_GRANDE);
    CHECK(memcmp(state->model.hands[0], player_0, MUS_GAME_CLASS_COUNT) == 0);
    CHECK(memcmp(state->model.hands[1], player_1, MUS_GAME_CLASS_COUNT) == 0);
}

static const Game *ordago_game(void) {
    return mus_ordago_game_descriptor();
}

static GameState *ordago_game_state(MusOrdagoGameState *state) {
    return mus_ordago_game_state_as_game_state(state);
}

static void ordago_apply(MusOrdagoGameState *state, Action action) {
    CHECK(cfr_game_apply_action(ordago_game(), ordago_game_state(state),
                                action) == CFR_STATUS_SUCCESS);
}

static void ordago_deal(
    MusOrdagoGameState *state,
    const uint8_t player_0[MUS_GAME_CLASS_COUNT],
    const uint8_t player_1[MUS_GAME_CLASS_COUNT]) {
    CHECK(mus_ordago_game_state_init(state) == CFR_STATUS_SUCCESS);
    for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT - 1;
         card_class += 1)
        ordago_apply(state, player_0[card_class]);
    for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT - 1;
         card_class += 1)
        ordago_apply(state, player_1[card_class]);
    CHECK(state->model.phase == MUS_PHASE_MUS_PLAYER_0);
    CHECK(memcmp(state->model.hands[0], player_0, MUS_GAME_CLASS_COUNT) == 0);
    CHECK(memcmp(state->model.hands[1], player_1, MUS_GAME_CLASS_COUNT) == 0);
}

static void ordago_finish_current_draw(MusOrdagoGameState *state,
                                        Action first_class_count) {
    ordago_apply(state, first_class_count);
    for (int card_class = 1; card_class < MUS_GAME_CLASS_COUNT - 1;
         card_class += 1)
        ordago_apply(state, MUS_ACTION_TAKE_0);
}

static InfoSetKey ordago_information_key(MusOrdagoGameState *state) {
    InfoSetKey key = -1;

    CHECK(cfr_game_information_set_key(ordago_game(),
                                       ordago_game_state(state), &key) ==
          CFR_STATUS_SUCCESS);
    return key;
}

static MusOrdagoInformationSet ordago_information(
    MusOrdagoGameState *state) {
    MusOrdagoInformationSet information = {0};
    const InfoSetKey key = ordago_information_key(state);

    CHECK(mus_ordago_game_information_set_decode(key, &information) ==
          CFR_STATUS_SUCCESS);
    InfoSetKey encoded = -1;
    CHECK(mus_ordago_game_information_set_encode(&information, &encoded) == CFR_STATUS_SUCCESS);
    CHECK(encoded == key);
    return information;
}

static uint8_t ordago_test_numeric_wager_count(
    const uint8_t used_action_masks[MUS_GAME_PLAYER_COUNT]) {
    unsigned int count = 0;

    for (int player = 0; player < MUS_GAME_PLAYER_COUNT; player += 1) {
        for (int bit = 0; bit < MUS_ORDAGO_WAGER_ACTION_COUNT; bit += 1)
            count += (used_action_masks[player] >> bit) & 1U;
    }
    return (uint8_t)count;
}

typedef struct {
    size_t player_0_compositions;
    size_t joint_deals;
    double total_probability;
} DealStats;

static const int TEST_CLASS_NUMBERS[MUS_GAME_CLASS_COUNT] = {
    AS, CUATRO, CINCO, SEIS, SIETE, SOTA, CABALLO, REY,
};

static void check_hand_summaries(const MusGameState *state) {
    Carta cards[MUS_GAME_PLAYER_COUNT][MUS_GAME_HAND_SIZE];
    Mano hands[MUS_GAME_PLAYER_COUNT];

    for (int player = 0; player < MUS_GAME_PLAYER_COUNT; player += 1) {
        size_t card_index = 0;

        for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT;
             card_class += 1) {
            for (int count = 0; count < state->model.hands[player][card_class];
                 count += 1) {
                cards[player][card_index] =
                    (Carta){.numero = TEST_CLASS_NUMBERS[card_class],
                            .palo = OROS};
                card_index += 1;
            }
        }
        CHECK(card_index == MUS_GAME_HAND_SIZE);
        hands[player] = (Mano){.cartas = cards[player], .tamano = card_index};
        CHECK(state->hand_summaries[player].grande_key ==
              claveGrande(hands[player]));
        CHECK(state->hand_summaries[player].chica_key ==
              claveChica(hands[player]));
        CHECK(state->hand_summaries[player].pairs_key ==
              clavePar(hands[player]));
        CHECK(state->hand_summaries[player].pairs_points ==
              tantosPares(hands[player]));
        CHECK(state->hand_summaries[player].point_value ==
              sumaMano(hands[player]));
        CHECK((state->hand_summaries[player].game_key >= 0) ==
              (tieneJuego(hands[player]) == 1));
        CHECK(state->hand_summaries[player].game_points ==
              tantosJuego(hands[player]));
    }
    CHECK((state->hand_summaries[1].grande_key >
           state->hand_summaries[0].grande_key
               ? 1
               : 0) == ganadorGrandeConJugadores(hands, 2, 0));
    CHECK((state->hand_summaries[1].chica_key >
           state->hand_summaries[0].chica_key
               ? 1
               : 0) == ganadorChicaConJugadores(hands, 2, 0));
    CHECK((state->hand_summaries[1].pairs_key >
           state->hand_summaries[0].pairs_key
               ? 1
               : 0) == ganadorParConJugadores(hands, 2, 0));
    CHECK((state->hand_summaries[1].point_value >
           state->hand_summaries[0].point_value
               ? 1
               : 0) == ganadorPuntoConJugadores(hands, 2, 0));
    {
        int cached_game_winner;

        if (state->hand_summaries[0].game_key < 0 &&
            state->hand_summaries[1].game_key < 0) {
            cached_game_winner =
                state->hand_summaries[1].point_value >
                        state->hand_summaries[0].point_value
                    ? 1
                    : 0;
        } else {
            cached_game_winner =
                state->hand_summaries[1].game_key >
                        state->hand_summaries[0].game_key
                    ? 1
                    : 0;
        }
        CHECK(cached_game_winner ==
              ganadorJuegoConJugadores(hands, 2, 0));
    }
}

static uint64_t reference_combinations(int n, int k) {
    uint64_t result = 1;

    if (k < 0 || k > n)
        return 0;
    if (k > n - k)
        k = n - k;
    for (int index = 1; index <= k; index += 1) {
        result = result * (uint64_t)(n - k + index) / (uint64_t)index;
    }
    return result;
}

static int reference_available_in_class(const MusGameModel *model, int player,
                                        int card_class) {
    static const int capacities[MUS_GAME_CLASS_COUNT] = {
        8, 4, 4, 4, 4, 4, 4, 8,
    };
    int available = capacities[card_class];

    if (player == 1)
        available -= model->hands[0][card_class];
    return available;
}

static int reference_available_after_class(const MusGameModel *model,
                                           int player) {
    int available = 0;

    for (int card_class = model->deal_class + 1;
         card_class < MUS_GAME_CLASS_COUNT; card_class += 1) {
        available +=
            reference_available_in_class(model, player, card_class);
    }
    return available;
}

static bool reference_deal_count_is_legal(const MusGameModel *model,
                                          int count) {
    const int player =
        model->phase == MUS_PHASE_DEAL_PLAYER_0 ? 0 : 1;
    const int current_available =
        reference_available_in_class(model, player, model->deal_class);
    const int later_available =
        reference_available_after_class(model, player);

    return count >= 0 && count <= model->deal_remaining &&
           count <= current_available &&
           model->deal_remaining - count <= later_available;
}

static Probability reference_deal_count_probability(
    const MusGameModel *model, int count) {
    const int player =
        model->phase == MUS_PHASE_DEAL_PLAYER_0 ? 0 : 1;
    const int current_available =
        reference_available_in_class(model, player, model->deal_class);
    const int later_available =
        reference_available_after_class(model, player);
    const int remaining = model->deal_remaining;
    const uint64_t denominator =
        reference_combinations(current_available + later_available, remaining);
    const uint64_t numerator =
        reference_combinations(current_available, count) *
        reference_combinations(later_available, remaining - count);

    if (denominator == 0)
        return 0.0;
    return (Probability)numerator / (Probability)denominator;
}

static void check_deal_outcomes(
    MusGameState *state, const Action actions[MUS_GAME_MAX_LEGAL_ACTIONS],
    const Probability probabilities[MUS_GAME_MAX_LEGAL_ACTIONS],
    size_t action_count) {
    size_t expected_count = 0;

    for (int candidate = 0; candidate <= MUS_GAME_HAND_SIZE; candidate += 1) {
        Probability expected;
        Probability individual = 0.0;

        if (!reference_deal_count_is_legal(&state->model, candidate))
            continue;
        CHECK(expected_count < action_count);
        if (expected_count >= action_count)
            continue;
        CHECK(actions[expected_count] == candidate);
        expected =
            reference_deal_count_probability(&state->model, candidate);
        CHECK(memcmp(&probabilities[expected_count], &expected,
                     sizeof(expected)) == 0);
        CHECK(cfr_game_chance_probability(game(), game_state(state), candidate,
                                          &individual) == CFR_STATUS_SUCCESS);
        CHECK(memcmp(&individual, &expected, sizeof(expected)) == 0);
        expected_count += 1;
    }
    CHECK(action_count == expected_count);
}

static void enumerate_deals(MusGameState *state, double reach,
                            DealStats *stats) {
    Actor actor;
    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS];
    Probability probabilities[MUS_GAME_MAX_LEGAL_ACTIONS];
    size_t action_count = 0;
    double local_sum = 0.0;

    CHECK(cfr_game_current_actor(game(), game_state(state), &actor) ==
          CFR_STATUS_SUCCESS);
    if (actor.kind == CFR_ACTOR_PLAYER) {
        check_hand_summaries(state);
        stats->joint_deals += 1;
        stats->total_probability += reach;
        return;
    }
    if (state->model.phase == MUS_PHASE_DEAL_PLAYER_1 &&
        state->model.deal_class == 0)
        stats->player_0_compositions += 1;
    CHECK(cfr_game_chance_outcomes(game(), game_state(state), actions,
                                   probabilities,
                                   MUS_GAME_MAX_LEGAL_ACTIONS,
                                   &action_count) == CFR_STATUS_SUCCESS);
    check_deal_outcomes(state, actions, probabilities, action_count);
    CHECK(action_count >= 1 && action_count <= MUS_GAME_MAX_LEGAL_ACTIONS);
    for (size_t index = 0; index < action_count; index += 1)
        local_sum += probabilities[index];
    CHECK(fabs(local_sum - 1.0) < 1e-12);
    for (size_t index = 0; index < action_count; index += 1) {
        apply(state, actions[index]);
        enumerate_deals(state, reach * probabilities[index], stats);
        CHECK(cfr_game_undo_action(game(), game_state(state)) ==
              CFR_STATUS_SUCCESS);
    }
}

static void test_exact_deal_distribution(void) {
    MusGameState state;
    MusGameModel root;
    DealStats stats = {0};

    CHECK(mus_game_state_init(&state) == CFR_STATUS_SUCCESS);
    root = state.model;
    enumerate_deals(&state, 1.0, &stats);
    CHECK(stats.player_0_compositions == 330);
    CHECK(stats.joint_deals == 104820);
    CHECK(fabs(stats.total_probability - 1.0) < 1e-11);
    CHECK(state.undo_count == 0);
    CHECK(memcmp(&state.model, &root, sizeof(root)) == 0);
}

static void test_apply_undo_and_contract(void) {
    MusGameState state;
    MusGameModel root;
    MusHandSummary root_summaries[MUS_GAME_PLAYER_COUNT];
    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS] = {99, 99, 99, 99, 99};
    size_t count = 99;
    bool terminal = true;

    CHECK(mus_game_state_init(NULL) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(mus_game_state_init(&state) == CFR_STATUS_SUCCESS);
    CHECK(game()->trusted_operations != NULL);
    root = state.model;
    memcpy(root_summaries, state.hand_summaries, sizeof(root_summaries));
    CHECK(game()->trusted_operations->apply_action(
              game()->context, game_state(&state), MUS_ACTION_PASS) ==
          CFR_STATUS_ILLEGAL_ACTION);
    CHECK(state.undo_count == 0);
    CHECK(memcmp(&state.model, &root, sizeof(root)) == 0);
    CHECK(memcmp(state.hand_summaries, root_summaries,
                 sizeof(root_summaries)) == 0);
    CHECK(cfr_game_validate_state(game(), game_state(&state)) ==
          CFR_STATUS_SUCCESS);
    state.model.deal_later_available += 1;
    CHECK(cfr_game_validate_state(game(), game_state(&state)) ==
          CFR_STATUS_INVALID_ARGUMENT);
    state.model = root;
    CHECK(cfr_game_is_terminal(game(), game_state(&state), &terminal) ==
          CFR_STATUS_SUCCESS);
    CHECK(!terminal);
    CHECK(cfr_game_legal_actions(game(), game_state(&state), actions, 0,
                                 &count) == CFR_STATUS_BUFFER_TOO_SMALL);
    CHECK(count > 0);
    CHECK(memcmp(&state.model, &root, sizeof(root)) == 0);
    apply(&state, MUS_ACTION_TAKE_0);
    CHECK(state.undo_count == 1);
    CHECK(cfr_game_undo_action(game(), game_state(&state)) ==
          CFR_STATUS_SUCCESS);
    CHECK(state.undo_count == 0);
    CHECK(memcmp(&state.model, &root, sizeof(root)) == 0);
    CHECK(cfr_game_undo_action(game(), game_state(&state)) ==
          CFR_STATUS_INVALID_ARGUMENT);
}

static void test_cached_hand_summary_validation(void) {
    const uint8_t low_a[MUS_GAME_CLASS_COUNT] = {1, 1, 1, 1, 0, 0, 0, 0};
    const uint8_t low_b[MUS_GAME_CLASS_COUNT] = {1, 1, 1, 0, 1, 0, 0, 0};
    MusGameState state;

    deal(&state, low_a, low_b);
    CHECK(cfr_game_validate_state(game(), game_state(&state)) ==
          CFR_STATUS_SUCCESS);
    state.hand_summaries[0].point_value += 1;
    CHECK(cfr_game_validate_state(game(), game_state(&state)) ==
          CFR_STATUS_INVALID_ARGUMENT);
}

static void test_explicit_format_apis_preserve_single_bet(void) {
    const uint8_t low_a[MUS_GAME_CLASS_COUNT] = {1, 1, 1, 1, 0, 0, 0, 0};
    const uint8_t low_b[MUS_GAME_CLASS_COUNT] = {1, 1, 1, 0, 1, 0, 0, 0};
    MusSingleBetGameState state;
    MusGameModel before;
    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS] = {0};
    size_t count = 0;

    CHECK(mus_single_bet_game_descriptor() == mus_game_descriptor());
    CHECK(strcmp(mus_single_bet_game_descriptor()->strategy_schema_id,
                 "mus.heads-up-single-hand/v1") == 0);
    deal(&state, low_a, low_b);
    apply(&state, MUS_ACTION_BET_TWO);
    CHECK(cfr_game_legal_actions(
              mus_single_bet_game_descriptor(),
              mus_single_bet_game_state_as_game_state(&state), actions,
              MUS_GAME_MAX_LEGAL_ACTIONS, &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 2);
    CHECK(actions[0] == MUS_ACTION_REJECT);
    CHECK(actions[1] == MUS_ACTION_ACCEPT);
    before = state.model;
    CHECK(cfr_game_apply_action(
              mus_single_bet_game_descriptor(),
              mus_single_bet_game_state_as_game_state(&state),
              MUS_ACTION_RAISE_DOUBLE) == CFR_STATUS_ILLEGAL_ACTION);
    CHECK(memcmp(&state.model, &before, sizeof(before)) == 0);
}

static void test_raises_to_eight_actions_infosets_and_undo(void) {
    const uint8_t kings[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 0, 0, 0, 0, 4};
    const uint8_t knights[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 0, 0, 0, 4, 0};
    MusRaisesToEightGameState state;
    MusRaisesToEightGameState accepted_four;
    MusRaisesToEightGameState initial;
    MusRaisesToEightInformationSet decoded;
    MusGameModel before;
    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS] = {0};
    size_t count = 0;
    InfoSetKey wager_2_key;
    InfoSetKey wager_4_key;
    InfoSetKey wager_8_key;
    InfoSetKey accepted_4_history_key;
    InfoSetKey accepted_8_history_key;

    CHECK(strcmp(raises_game()->strategy_schema_id,
                 "mus.heads-up-single-hand.raises-to-eight/v1") == 0);
    raises_deal(&state, kings, knights);
    CHECK(cfr_game_legal_actions(raises_game(), raises_game_state(&state),
                                 actions, MUS_GAME_MAX_LEGAL_ACTIONS,
                                 &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 2 && actions[0] == MUS_ACTION_PASS &&
          actions[1] == MUS_ACTION_BET_TWO);
    raises_apply(&state, MUS_ACTION_BET_TWO);
    CHECK(state.model.current_wager == 2);
    CHECK(cfr_game_information_set_key(raises_game(), raises_game_state(&state),
                                       &wager_2_key) == CFR_STATUS_SUCCESS);
    CHECK(cfr_game_legal_actions(raises_game(), raises_game_state(&state),
                                 actions, MUS_GAME_MAX_LEGAL_ACTIONS,
                                 &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 3 && actions[0] == MUS_ACTION_REJECT &&
          actions[1] == MUS_ACTION_ACCEPT &&
          actions[2] == MUS_ACTION_RAISE_DOUBLE);

    raises_apply(&state, MUS_ACTION_RAISE_DOUBLE);
    CHECK(state.model.current_wager == 4);
    CHECK(state.model.bettor == CFR_PLAYER_1);
    CHECK(state.model.current_player == CFR_PLAYER_0);
    CHECK(cfr_game_information_set_key(raises_game(), raises_game_state(&state),
                                       &wager_4_key) == CFR_STATUS_SUCCESS);
    CHECK(wager_4_key != wager_2_key);
    CHECK(mus_raises_to_eight_game_information_set_decode(wager_4_key,
                                                          &decoded) ==
          CFR_STATUS_SUCCESS);
    CHECK(decoded.bet_stage == MUS_BET_RESPONSE);
    CHECK(decoded.bettor == CFR_PLAYER_1);
    CHECK(decoded.current_wager == 4);

    raises_apply(&state, MUS_ACTION_RAISE_DOUBLE);
    CHECK(state.model.current_wager == 8);
    CHECK(state.model.bettor == CFR_PLAYER_0);
    CHECK(state.model.current_player == CFR_PLAYER_1);
    CHECK(cfr_game_information_set_key(raises_game(), raises_game_state(&state),
                                       &wager_8_key) == CFR_STATUS_SUCCESS);
    CHECK(wager_8_key != wager_4_key);
    CHECK(cfr_game_legal_actions(raises_game(), raises_game_state(&state),
                                 actions, MUS_GAME_MAX_LEGAL_ACTIONS,
                                 &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 2 && actions[0] == MUS_ACTION_REJECT &&
          actions[1] == MUS_ACTION_ACCEPT);
    before = state.model;
    CHECK(cfr_game_apply_action(raises_game(), raises_game_state(&state),
                                MUS_ACTION_RAISE_DOUBLE) ==
          CFR_STATUS_ILLEGAL_ACTION);
    CHECK(memcmp(&state.model, &before, sizeof(before)) == 0);
    raises_apply(&state, MUS_ACTION_ACCEPT);
    CHECK(state.model.outcomes[0] == MUS_LANCE_FIRST_BET_ACCEPTED);
    CHECK(state.model.outcome_wagers[0] == 8);
    CHECK(cfr_game_information_set_key(raises_game(), raises_game_state(&state),
                                       &accepted_8_history_key) ==
          CFR_STATUS_SUCCESS);

    raises_deal(&accepted_four, kings, knights);
    raises_apply(&accepted_four, MUS_ACTION_PASS);
    raises_apply(&accepted_four, MUS_ACTION_BET_TWO);
    raises_apply(&accepted_four, MUS_ACTION_RAISE_DOUBLE);
    raises_apply(&accepted_four, MUS_ACTION_ACCEPT);
    CHECK(accepted_four.model.outcomes[0] ==
          MUS_LANCE_FIRST_BET_ACCEPTED);
    CHECK(accepted_four.model.outcome_wagers[0] == 4);
    CHECK(cfr_game_information_set_key(
              raises_game(), raises_game_state(&accepted_four),
              &accepted_4_history_key) == CFR_STATUS_SUCCESS);
    CHECK(accepted_4_history_key != accepted_8_history_key);
    CHECK(mus_raises_to_eight_game_information_set_decode(
              accepted_8_history_key, &decoded) == CFR_STATUS_SUCCESS);
    CHECK(decoded.phase == MUS_PHASE_CHICA);
    CHECK(decoded.outcomes[0] == MUS_LANCE_FIRST_BET_ACCEPTED);
    CHECK(decoded.outcome_wagers[0] == 8);

    CHECK(mus_raises_to_eight_game_state_init(&state) == CFR_STATUS_SUCCESS);
    initial = state;
    for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT - 1;
         card_class += 1)
        raises_apply(&state, kings[card_class]);
    for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT - 1;
         card_class += 1)
        raises_apply(&state, knights[card_class]);
    for (int lance = 0; lance < MUS_GAME_LANCE_COUNT; lance += 1) {
        raises_apply(&state, MUS_ACTION_PASS);
        raises_apply(&state, MUS_ACTION_BET_TWO);
        raises_apply(&state, MUS_ACTION_RAISE_DOUBLE);
        raises_apply(&state, MUS_ACTION_RAISE_DOUBLE);
        raises_apply(&state, MUS_ACTION_ACCEPT);
    }
    CHECK(state.model.phase == MUS_PHASE_TERMINAL);
    CHECK(state.undo_count == 34);
    while (state.undo_count > 0) {
        CHECK(cfr_game_undo_action(raises_game(), raises_game_state(&state)) ==
              CFR_STATUS_SUCCESS);
    }
    CHECK(state.undo_count == initial.undo_count);
    CHECK(memcmp(&state.model, &initial.model, sizeof(state.model)) == 0);
}

static void test_ordago_mus_discards_bets_and_infosets(void) {
    const uint8_t kings[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 0, 0, 0, 0, 4};
    const uint8_t knights[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 0, 0, 0, 4, 0};
    MusOrdagoGameState state;
    MusOrdagoInformationSet decoded;
    MusHandSummary original_summary;
    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS] = {0};
    size_t count = 0;
    InfoSetKey key;

    CHECK(strcmp(ordago_game()->strategy_schema_id,
                 MUS_ORDAGO_STRATEGY_SCHEMA_ID) == 0);
    CHECK(ordago_game()->max_legal_actions == MUS_GAME_MAX_LEGAL_ACTIONS);
    ordago_deal(&state, kings, knights);
    original_summary = state.hand_summaries[0];
    CHECK(cfr_game_legal_actions(ordago_game(), ordago_game_state(&state),
                                 actions, MUS_GAME_MAX_LEGAL_ACTIONS,
                                 &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 2 && actions[0] == MUS_ACTION_MUS &&
          actions[1] == MUS_ACTION_NO_MUS);
    CHECK(cfr_game_information_set_key(ordago_game(),
                                       ordago_game_state(&state), &key) ==
          CFR_STATUS_SUCCESS);
    CHECK(mus_ordago_game_information_set_decode(key, &decoded) ==
          CFR_STATUS_SUCCESS);
    CHECK(decoded.phase == MUS_PHASE_MUS_PLAYER_0);
    CHECK(decoded.mus_count == 0);
    CHECK(memcmp(decoded.hand, kings, MUS_GAME_CLASS_COUNT) == 0);

    ordago_apply(&state, MUS_ACTION_MUS);
    CHECK(state.model.phase == MUS_PHASE_MUS_PLAYER_1);
    ordago_apply(&state, MUS_ACTION_MUS);
    CHECK(state.model.phase == MUS_PHASE_DISCARD_PLAYER_0);
    CHECK(cfr_game_legal_actions(ordago_game(), ordago_game_state(&state),
                                 actions, MUS_GAME_MAX_LEGAL_ACTIONS,
                                 &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 4);
    CHECK(actions[0] == MUS_ACTION_DISCARD_MASK(1));
    CHECK(actions[1] == MUS_ACTION_DISCARD_MASK(3));
    CHECK(actions[2] == MUS_ACTION_DISCARD_MASK(7));
    CHECK(actions[3] == MUS_ACTION_DISCARD_MASK(15));
    CHECK(!mus_ordago_game_discard_mask_is_canonical(kings, 0));
    CHECK(mus_ordago_game_discard_mask_is_canonical(kings, 1));
    CHECK(!mus_ordago_game_discard_mask_is_canonical(kings, 2));
    CHECK(mus_ordago_game_discard_mask_is_canonical(kings, 3));
    CHECK(mus_ordago_game_discard_mask_is_canonical(kings, 7));
    CHECK(mus_ordago_game_discard_mask_is_canonical(kings, 15));
    CHECK(cfr_game_apply_action(ordago_game(), ordago_game_state(&state),
                                MUS_ACTION_DISCARD_MASK(0)) ==
          CFR_STATUS_ILLEGAL_ACTION);
    CHECK(cfr_game_apply_action(ordago_game(), ordago_game_state(&state),
                                MUS_ACTION_DISCARD_MASK(2)) ==
          CFR_STATUS_ILLEGAL_ACTION);

    ordago_apply(&state, MUS_ACTION_DISCARD_MASK(3));
    CHECK(state.model.phase == MUS_PHASE_DISCARD_PLAYER_1);
    CHECK(state.model.hands[0][MUS_CLASS_KINGS] == 2);
    CHECK(state.model.out_of_play[MUS_CLASS_KINGS] == 2);
    CHECK(state.model.discard_counts[0][0] == 2);
    ordago_apply(&state, MUS_ACTION_DISCARD_MASK(1));
    CHECK(state.model.mus_count == 1);
    CHECK(state.model.phase == MUS_PHASE_REDRAW_PLAYER_0);
    ordago_finish_current_draw(&state, MUS_ACTION_TAKE_2);
    CHECK(state.model.phase == MUS_PHASE_REDRAW_PLAYER_1);
    ordago_finish_current_draw(&state, MUS_ACTION_TAKE_0);
    CHECK(state.model.hands[0][MUS_CLASS_PITOS] == 2);
    CHECK(state.model.hands[0][MUS_CLASS_KINGS] == 2);
    CHECK(state.model.out_of_play[MUS_CLASS_KINGS] == 2);
    CHECK(cfr_game_validate_state(ordago_game(), ordago_game_state(&state)) ==
          CFR_STATUS_SUCCESS);

#if MUS_GAME_MAX_MUSES == 2
    CHECK(state.model.phase == MUS_PHASE_MUS_PLAYER_0);
    ordago_apply(&state, MUS_ACTION_MUS);
    ordago_apply(&state, MUS_ACTION_MUS);
    ordago_apply(&state, MUS_ACTION_DISCARD_MASK(1));
    ordago_apply(&state, MUS_ACTION_DISCARD_MASK(1));
    ordago_finish_current_draw(&state, MUS_ACTION_TAKE_1);
    ordago_finish_current_draw(&state, MUS_ACTION_TAKE_0);
#endif
    CHECK(state.model.mus_count == MUS_GAME_MAX_MUSES);
    CHECK(state.model.phase == MUS_PHASE_GRANDE);
    CHECK(!state.model.mus_stopped);
    CHECK(cfr_game_information_set_key(ordago_game(),
                                       ordago_game_state(&state), &key) ==
          CFR_STATUS_SUCCESS);
    CHECK(mus_ordago_game_information_set_decode(key, &decoded) ==
          CFR_STATUS_SUCCESS);
    CHECK(decoded.phase == MUS_PHASE_GRANDE);
    CHECK(decoded.mus_count == MUS_GAME_MAX_MUSES);
    CHECK(decoded.discard_counts[0][0] == 2);
    CHECK(decoded.discard_counts[0][1] == 1);
#if MUS_GAME_MAX_MUSES == 2
    CHECK(decoded.discard_counts[1][0] == 1);
    CHECK(decoded.discard_counts[1][1] == 1);
#endif

    CHECK(cfr_game_legal_actions(ordago_game(), ordago_game_state(&state),
                                 actions, MUS_GAME_MAX_LEGAL_ACTIONS,
                                 &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 6 && actions[0] == MUS_ACTION_PASS &&
          actions[1] == MUS_ACTION_BET_TWO &&
          actions[2] == MUS_ACTION_BET_FOUR &&
          actions[3] == MUS_ACTION_BET_SEVEN &&
          actions[4] == MUS_ACTION_BET_TEN &&
          actions[5] == MUS_ACTION_ORDAGO);
    ordago_apply(&state, MUS_ACTION_BET_TWO);
    CHECK(cfr_game_legal_actions(ordago_game(), ordago_game_state(&state),
                                 actions, MUS_GAME_MAX_LEGAL_ACTIONS,
                                 &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 6 && actions[0] == MUS_ACTION_REJECT &&
          actions[1] == MUS_ACTION_ACCEPT &&
          actions[2] == MUS_ACTION_BET_TWO &&
          actions[3] == MUS_ACTION_BET_FOUR &&
          actions[4] == MUS_ACTION_BET_TEN &&
          actions[5] == MUS_ACTION_ORDAGO);
    CHECK(cfr_game_apply_action(ordago_game(), ordago_game_state(&state),
                                MUS_ACTION_BET_SEVEN) ==
          CFR_STATUS_ILLEGAL_ACTION);
    ordago_apply(&state, MUS_ACTION_BET_TEN);
    CHECK(state.model.current_wager == 12);
    CHECK(state.model.previous_wager == 2);
    CHECK(cfr_game_legal_actions(ordago_game(), ordago_game_state(&state),
                                 actions, MUS_GAME_MAX_LEGAL_ACTIONS,
                                 &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 4 && actions[0] == MUS_ACTION_REJECT &&
          actions[1] == MUS_ACTION_ACCEPT &&
          actions[2] == MUS_ACTION_BET_TEN &&
          actions[3] == MUS_ACTION_ORDAGO);
    CHECK(cfr_game_apply_action(ordago_game(), ordago_game_state(&state),
                                MUS_ACTION_BET_FOUR) ==
          CFR_STATUS_ILLEGAL_ACTION);
    ordago_apply(&state, MUS_ACTION_ORDAGO);
    CHECK(state.model.current_wager == MUS_ORDAGO_WAGER);
    CHECK(state.model.previous_wager == 12);
    CHECK(ordago_test_numeric_wager_count(
              state.model.used_wager_action_masks) == 2);
    CHECK(state.model.current_wager_is_ordago);
    CHECK(cfr_game_information_set_key(ordago_game(),
                                       ordago_game_state(&state), &key) ==
          CFR_STATUS_SUCCESS);
    CHECK(mus_ordago_game_information_set_decode(key, &decoded) ==
          CFR_STATUS_SUCCESS);
    CHECK(decoded.bet_stage == MUS_BET_RESPONSE);
    CHECK(decoded.bettor == CFR_PLAYER_0);
    CHECK(decoded.current_wager == MUS_ORDAGO_WAGER);
    CHECK(decoded.previous_wager == 12);
    CHECK(decoded.numeric_wager_count == 2);
    CHECK(decoded.current_wager_is_ordago);
    ordago_apply(&state, MUS_ACTION_REJECT);
    CHECK(state.model.outcomes[0] == MUS_LANCE_FIRST_BET_REJECTED);
    CHECK(state.model.outcome_wagers[0] == MUS_ORDAGO_WAGER);
    CHECK(state.model.outcome_previous_wagers[0] == 12);
    while (state.model.phase != MUS_PHASE_MUS_PLAYER_0 ||
           state.model.mus_count != 0) {
        CHECK(cfr_game_undo_action(ordago_game(), ordago_game_state(&state)) ==
              CFR_STATUS_SUCCESS);
    }
    CHECK(state.hand_summaries[0].grande_key == original_summary.grande_key);
    CHECK(memcmp(state.model.hands[0], kings, MUS_GAME_CLASS_COUNT) == 0);
}

static void test_ordago_no_mus_scoring_and_export(void) {
    const uint8_t kings[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 0, 0, 0, 0, 4};
    const uint8_t knights[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 0, 0, 0, 4, 0};
    MusOrdagoGameState state;
    MusOrdagoGameState betting_state;
    MusOrdagoGameState ordago_response_state;
    MusOrdagoGameState budget_state;
    MusOrdagoGameState discard_state;
    InfoStore store = {0};
    InfoNode *node = NULL;
    Trainer trainer;
    FILE *text_file;
    char line[4096];
    InfoSetKey key;
    Utility utility = 0.0;
    bool found_mus = false;
    bool found_additive_response = false;
    bool found_filtered_response = false;
    bool found_ordago_response = false;
    bool found_global_budget_response = false;
    bool found_canonical_discards = false;

    ordago_deal(&state, kings, knights);
    ordago_apply(&state, MUS_ACTION_NO_MUS);
    CHECK(state.model.phase == MUS_PHASE_GRANDE);
    CHECK(state.model.mus_count == 0);
    CHECK(state.model.mus_stopped);
    CHECK(state.model.mus_stop_player == CFR_PLAYER_0);
    ordago_apply(&state, MUS_ACTION_ORDAGO);
    ordago_apply(&state, MUS_ACTION_ACCEPT);
    CHECK(state.model.phase == MUS_PHASE_TERMINAL);
    CHECK(cfr_game_terminal_utility(ordago_game(), ordago_game_state(&state),
                                    CFR_PLAYER_0, &utility) ==
          CFR_STATUS_SUCCESS);
    CHECK(utility == 40.0);

    ordago_deal(&state, kings, knights);
    CHECK(cfr_game_information_set_key(ordago_game(),
                                       ordago_game_state(&state), &key) ==
          CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_init(&store) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_get_or_create(&store, key, 2, &node) ==
          CFR_STATUS_SUCCESS);
    CHECK(node != NULL);
    ordago_deal(&betting_state, kings, knights);
    ordago_apply(&betting_state, MUS_ACTION_NO_MUS);
    ordago_apply(&betting_state, MUS_ACTION_BET_TWO);
    key = ordago_information_key(&betting_state);
    CHECK(cfr_info_store_get_or_create(&store, key, 6, &node) ==
          CFR_STATUS_SUCCESS);
    ordago_apply(&betting_state, MUS_ACTION_BET_FOUR);
    key = ordago_information_key(&betting_state);
    CHECK(cfr_info_store_get_or_create(&store, key, 5, &node) ==
          CFR_STATUS_SUCCESS);
    ordago_deal(&ordago_response_state, kings, knights);
    ordago_apply(&ordago_response_state, MUS_ACTION_NO_MUS);
    ordago_apply(&ordago_response_state, MUS_ACTION_BET_TWO);
    ordago_apply(&ordago_response_state, MUS_ACTION_ORDAGO);
    key = ordago_information_key(&ordago_response_state);
    CHECK(cfr_info_store_get_or_create(&store, key, 2, &node) ==
          CFR_STATUS_SUCCESS);
    ordago_deal(&budget_state, kings, knights);
    ordago_apply(&budget_state, MUS_ACTION_NO_MUS);
    ordago_apply(&budget_state, MUS_ACTION_BET_TEN);
    ordago_apply(&budget_state, MUS_ACTION_ACCEPT);
    ordago_apply(&budget_state, MUS_ACTION_BET_TEN);
    ordago_apply(&budget_state, MUS_ACTION_ACCEPT);
    ordago_apply(&budget_state, MUS_ACTION_BET_TWO);
    key = ordago_information_key(&budget_state);
    CHECK(cfr_info_store_get_or_create(&store, key, 5, &node) ==
          CFR_STATUS_SUCCESS);
    ordago_deal(&discard_state, kings, knights);
    ordago_apply(&discard_state, MUS_ACTION_MUS);
    ordago_apply(&discard_state, MUS_ACTION_MUS);
    key = ordago_information_key(&discard_state);
    CHECK(cfr_info_store_get_or_create(&store, key, 4, &node) ==
          CFR_STATUS_SUCCESS);
    CHECK(cfr_trainer_init_mccfr(&trainer, ordago_game(),
                                 ordago_game_state(&state), &store, 9) ==
          CFR_STATUS_SUCCESS);
    text_file = tmpfile();
    CHECK(text_file != NULL);
    if (text_file != NULL) {
        CHECK(mus_ordago_strategy_write_text(text_file, &trainer) ==
              CFR_STATUS_SUCCESS);
        rewind(text_file);
        CHECK(fgets(line, sizeof(line), text_file) != NULL);
        CHECK(strstr(line, MUS_ORDAGO_STRATEGY_SCHEMA_ID) != NULL);
        while (fgets(line, sizeof(line), text_file) != NULL) {
            if (strstr(line, "muses=0") != NULL &&
                strstr(line, " mus=") != NULL &&
                strstr(line, " no_mus=") != NULL) {
                found_mus = true;
            }
            if (strstr(line, "fase=grande") != NULL &&
                strstr(line, "actual=2 cubierto=1 envites=1"
                             " usados_j0=1 usados_j1=0") != NULL &&
                strstr(line, "envidar_2=") != NULL) {
                found_additive_response = true;
            }
            if (strstr(line, "fase=grande") != NULL &&
                strstr(line, "actual=6 cubierto=2 envites=2"
                             " usados_j0=1 usados_j1=2") != NULL &&
                strstr(line, "envidar_2=") == NULL &&
                strstr(line, "envidar_4=") != NULL &&
                strstr(line, "envidar_7=") == NULL &&
                strstr(line, "envidar_10=") != NULL) {
                found_filtered_response = true;
            }
            if (strstr(line, "fase=grande") != NULL &&
                strstr(line, "actual=ordago cubierto=2 envites=1"
                             " usados_j0=1 usados_j1=0") != NULL) {
                found_ordago_response = true;
            }
            if (strstr(line, "fase=pares") != NULL &&
                strstr(line, "tantos_maximos=10") != NULL &&
                strstr(line, "acumulado_previo=20") != NULL &&
                strstr(line, "actual=2 cubierto=1 envites=1"
                             " usados_j0=1 usados_j1=0") != NULL &&
                strstr(line, "envidar_2=") != NULL &&
                strstr(line, "envidar_4=") != NULL &&
                strstr(line, "envidar_7=") == NULL &&
                strstr(line, "envidar_10=") == NULL) {
                found_global_budget_response = true;
            }
            if (strstr(line, "fase=descarte_mano") != NULL &&
                strstr(line, "cambiar_cerdos:1=") != NULL &&
                strstr(line, "cambiar_cerdos:2=") != NULL &&
                strstr(line, "cambiar_cerdos:3=") != NULL &&
                strstr(line, "cambiar_cerdos:4=") != NULL) {
                found_canonical_discards = true;
            }
        }
        CHECK(found_mus);
        CHECK(found_additive_response);
        CHECK(found_filtered_response);
        CHECK(found_ordago_response);
        CHECK(found_global_budget_response);
        CHECK(found_canonical_discards);
        fclose(text_file);
    }
    CHECK(cfr_info_store_destroy(&store) == CFR_STATUS_SUCCESS);
}

static void check_ordago_betting_tree_round_trip(
    MusOrdagoGameState *state, MusGamePhase lance, size_t *visited) {
    Action model_actions[MUS_GAME_MAX_LEGAL_ACTIONS] = {0};
    Action decoded_actions[MUS_GAME_MAX_LEGAL_ACTIONS] = {0};
    MusOrdagoInformationSet information = {0};
    InfoSetKey key = -1;
    size_t model_count = 0;
    size_t decoded_count = 0;

    CHECK(state != NULL && visited != NULL);
    if (state == NULL || visited == NULL)
        return;
    CHECK(state->model.phase == lance);
    CHECK(cfr_game_information_set_key(
              ordago_game(), ordago_game_state(state), &key) ==
          CFR_STATUS_SUCCESS);
    CHECK(mus_ordago_game_information_set_decode(key, &information) ==
          CFR_STATUS_SUCCESS);
    CHECK(information.phase == state->model.phase);
    CHECK(information.player == (Player)state->model.current_player);
    CHECK(information.bet_stage == state->model.bet_stage);
    CHECK(information.bettor == (Player)state->model.bettor);
    CHECK(information.current_wager == state->model.current_wager);
    CHECK(information.previous_wager == state->model.previous_wager);
    CHECK(information.current_wager_is_ordago ==
          state->model.current_wager_is_ordago);
    CHECK(information.numeric_wager_count ==
          ordago_test_numeric_wager_count(
              state->model.used_wager_action_masks));
    CHECK(memcmp(information.used_wager_action_masks,
                 state->model.used_wager_action_masks,
                 sizeof(information.used_wager_action_masks)) == 0);
    CHECK(information.prior_wager_total ==
          state->model.prior_wager_total);
    CHECK(information.max_settled_score ==
          (state->model.settled_scores[0] >
                   state->model.settled_scores[1]
               ? state->model.settled_scores[0]
               : state->model.settled_scores[1]));
    CHECK(cfr_game_legal_actions(
              ordago_game(), ordago_game_state(state), model_actions,
              MUS_GAME_MAX_LEGAL_ACTIONS, &model_count) ==
          CFR_STATUS_SUCCESS);
    CHECK(mus_ordago_game_information_set_legal_actions(
              &information, decoded_actions,
              MUS_GAME_MAX_LEGAL_ACTIONS, &decoded_count) ==
          CFR_STATUS_SUCCESS);
    CHECK(decoded_count == model_count);
    CHECK(memcmp(model_actions, decoded_actions,
                 model_count * sizeof(*model_actions)) == 0);
    *visited += 1;

    for (size_t action_index = 0; action_index < model_count;
         action_index += 1) {
        const MusGameModel before = state->model;
        MusOrdagoGameState trusted = *state;
        CHECK(ordago_game()->trusted_operations->apply_action(
                  ordago_game()->context, ordago_game_state(&trusted),
                  model_actions[action_index]) == CFR_STATUS_SUCCESS);
        const Status apply_status = cfr_game_apply_action(
            ordago_game(), ordago_game_state(state),
            model_actions[action_index]);

        CHECK(apply_status == CFR_STATUS_SUCCESS);
        if (apply_status != CFR_STATUS_SUCCESS)
            continue;
        CHECK(memcmp(&trusted.model, &state->model, sizeof(state->model)) == 0);
        CHECK(memcmp(trusted.hand_summaries, state->hand_summaries,
                     sizeof(state->hand_summaries)) == 0);
        CHECK(memcmp(trusted.ordago_hand_codes, state->ordago_hand_codes,
                     sizeof(state->ordago_hand_codes)) == 0);
        if (state->model.phase == lance) {
            check_ordago_betting_tree_round_trip(
                state, lance, visited);
        }
        /* Exercise public and trusted undo with either kind of backup. */
        if (action_index % 2 == 0) {
            CHECK(cfr_game_undo_action(
                      ordago_game(), ordago_game_state(&trusted)) ==
                  CFR_STATUS_SUCCESS);
            CHECK(ordago_game()->trusted_operations->undo_action(
                      ordago_game()->context, ordago_game_state(state)) ==
                  CFR_STATUS_SUCCESS);
        } else {
            CHECK(ordago_game()->trusted_operations->undo_action(
                      ordago_game()->context, ordago_game_state(&trusted)) ==
                  CFR_STATUS_SUCCESS);
            CHECK(cfr_game_undo_action(
                      ordago_game(), ordago_game_state(state)) ==
                  CFR_STATUS_SUCCESS);
        }
        CHECK(memcmp(&trusted.model, &before, sizeof(before)) == 0);
        CHECK(memcmp(&state->model, &before, sizeof(before)) == 0);
        CHECK(trusted.undo_count == state->undo_count);
        CHECK(memcmp(trusted.hand_summaries, state->hand_summaries,
                     sizeof(state->hand_summaries)) == 0);
        CHECK(memcmp(trusted.ordago_hand_codes, state->ordago_hand_codes,
                     sizeof(state->ordago_hand_codes)) == 0);
    }
}

static void test_ordago_betting_tree_round_trip(void) {
    const uint8_t kings[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 0, 0, 0, 0, 4};
    const uint8_t knights[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 0, 0, 0, 4, 0};
    MusOrdagoGameState state;
    size_t visited = 0;

    ordago_deal(&state, kings, knights);
    ordago_apply(&state, MUS_ACTION_NO_MUS);
    const MusGameModel before = state.model;
    const size_t undo_count = state.undo_count;
    CHECK(ordago_game()->trusted_operations->apply_action(
              ordago_game()->context, ordago_game_state(&state),
              MUS_ACTION_MUS) == CFR_STATUS_ILLEGAL_ACTION);
    CHECK(memcmp(&state.model, &before, sizeof(before)) == 0);
    CHECK(state.undo_count == undo_count);
    for (size_t lance = 0; lance < MUS_GAME_LANCE_COUNT &&
                           state.model.phase != MUS_PHASE_TERMINAL; lance += 1) {
        check_ordago_betting_tree_round_trip(
            &state, (MusGamePhase)state.model.phase, &visited);
        ordago_apply(&state, MUS_ACTION_PASS);
        ordago_apply(&state, MUS_ACTION_PASS);
    }
    CHECK(state.model.phase == MUS_PHASE_TERMINAL);
    const uint8_t mixed_low[MUS_GAME_CLASS_COUNT] = {1, 1, 1, 1, 0, 0, 0, 0};
    const uint8_t mixed_high[MUS_GAME_CLASS_COUNT] = {0, 1, 1, 1, 1, 0, 0, 0};
    ordago_deal(&state, mixed_low, mixed_high);
    ordago_apply(&state, MUS_ACTION_NO_MUS);
    for (size_t lance = 0; lance < MUS_GAME_LANCE_COUNT &&
                           state.model.phase != MUS_PHASE_TERMINAL; lance += 1) {
        check_ordago_betting_tree_round_trip(
            &state, (MusGamePhase)state.model.phase, &visited);
        ordago_apply(&state, MUS_ACTION_PASS);
        ordago_apply(&state, MUS_ACTION_PASS);
    }
    CHECK(state.model.phase == MUS_PHASE_TERMINAL);
    CHECK(visited > 20);
}

static void test_other_formats_reject_ordago_wager_masks(void) {
    const uint8_t kings[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 0, 0, 0, 0, 4};
    const uint8_t knights[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 0, 0, 0, 4, 0};
    MusGameState state;
    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS] = {0};
    size_t count = 0;

    deal(&state, kings, knights);
    CHECK(cfr_game_legal_actions(game(), game_state(&state), actions,
                                 MUS_GAME_MAX_LEGAL_ACTIONS,
                                 &count) == CFR_STATUS_SUCCESS);
    state.model.used_wager_action_masks[CFR_PLAYER_0] = 1;
    CHECK(cfr_game_legal_actions(game(), game_state(&state), actions,
                                 MUS_GAME_MAX_LEGAL_ACTIONS,
                                 &count) == CFR_STATUS_INVALID_ARGUMENT);
    state.model.used_wager_action_masks[CFR_PLAYER_0] = 0;
    CHECK(cfr_game_legal_actions(game(), game_state(&state), actions,
                                 MUS_GAME_MAX_LEGAL_ACTIONS,
                                 &count) == CFR_STATUS_SUCCESS);
}

static void test_ordago_additive_wagers(void) {
    const uint8_t kings[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 0, 0, 0, 0, 4};
    const uint8_t knights[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 0, 0, 0, 4, 0};
    const uint8_t matched[MUS_GAME_CLASS_COUNT] = {1, 1, 0, 0, 0, 0, 1, 1};
    MusOrdagoGameState state;
    MusOrdagoGameState short_history;
    MusOrdagoGameState long_history;
    MusOrdagoInformationSet information;
    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS] = {0};
    Action decoded_actions[MUS_GAME_MAX_LEGAL_ACTIONS] = {0};
    size_t count = 0;
    size_t decoded_count = 0;
    InfoSetKey short_key;
    InfoSetKey long_key;

    ordago_deal(&state, kings, knights);
    ordago_apply(&state, MUS_ACTION_NO_MUS);
    ordago_apply(&state, MUS_ACTION_BET_TWO);
    CHECK(cfr_game_legal_actions(ordago_game(), ordago_game_state(&state),
                                 actions, MUS_GAME_MAX_LEGAL_ACTIONS,
                                 &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 6 && actions[0] == MUS_ACTION_REJECT &&
          actions[1] == MUS_ACTION_ACCEPT &&
          actions[2] == MUS_ACTION_BET_TWO &&
          actions[3] == MUS_ACTION_BET_FOUR &&
          actions[4] == MUS_ACTION_BET_TEN &&
          actions[5] == MUS_ACTION_ORDAGO);
    ordago_apply(&state, MUS_ACTION_BET_TWO);
    CHECK(state.model.current_wager == 4);
    CHECK(state.model.previous_wager == 2);
    CHECK(state.model.used_wager_action_masks[CFR_PLAYER_0] == 1);
    CHECK(state.model.used_wager_action_masks[CFR_PLAYER_1] == 1);
    CHECK(!state.model.current_wager_is_ordago);
    CHECK(cfr_game_legal_actions(ordago_game(), ordago_game_state(&state),
                                 actions, MUS_GAME_MAX_LEGAL_ACTIONS,
                                 &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 5 && actions[0] == MUS_ACTION_REJECT &&
          actions[1] == MUS_ACTION_ACCEPT &&
          actions[2] == MUS_ACTION_BET_FOUR &&
          actions[3] == MUS_ACTION_BET_TEN &&
          actions[4] == MUS_ACTION_ORDAGO);
    CHECK(cfr_game_apply_action(ordago_game(), ordago_game_state(&state),
                                MUS_ACTION_BET_TWO) ==
          CFR_STATUS_ILLEGAL_ACTION);
    information = ordago_information(&state);
    CHECK(information.current_wager == 4);
    CHECK(information.previous_wager == 2);
    CHECK(information.numeric_wager_count == 2);
    CHECK(information.used_wager_action_masks[CFR_PLAYER_0] == 1);
    CHECK(information.used_wager_action_masks[CFR_PLAYER_1] == 1);
    CHECK(information.prior_wager_total == 0);
    CHECK(!information.current_wager_is_ordago);

    ordago_deal(&state, kings, knights);
    ordago_apply(&state, MUS_ACTION_NO_MUS);
    ordago_apply(&state, MUS_ACTION_PASS);
    ordago_apply(&state, MUS_ACTION_BET_SEVEN);
    CHECK(state.model.current_player == CFR_PLAYER_0);
    CHECK(state.model.current_wager == 7);
    CHECK(ordago_test_numeric_wager_count(
              state.model.used_wager_action_masks) == 1);
    CHECK(state.model.used_wager_action_masks[CFR_PLAYER_0] == 0);
    CHECK(state.model.used_wager_action_masks[CFR_PLAYER_1] == 4);
    CHECK(cfr_game_legal_actions(ordago_game(), ordago_game_state(&state),
                                 actions, MUS_GAME_MAX_LEGAL_ACTIONS,
                                 &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 4 && actions[0] == MUS_ACTION_REJECT &&
          actions[1] == MUS_ACTION_ACCEPT &&
          actions[2] == MUS_ACTION_BET_TEN &&
          actions[3] == MUS_ACTION_ORDAGO);
    CHECK(cfr_game_apply_action(ordago_game(), ordago_game_state(&state),
                                MUS_ACTION_BET_SEVEN) ==
          CFR_STATUS_ILLEGAL_ACTION);
    CHECK(cfr_game_apply_action(ordago_game(), ordago_game_state(&state),
                                MUS_ACTION_BET_FOUR) ==
          CFR_STATUS_ILLEGAL_ACTION);
    ordago_apply(&state, MUS_ACTION_BET_TEN);
    CHECK(state.model.current_wager == 17);
    CHECK(ordago_test_numeric_wager_count(
              state.model.used_wager_action_masks) == 2);

    ordago_deal(&state, kings, knights);
    ordago_apply(&state, MUS_ACTION_NO_MUS);
    ordago_apply(&state, MUS_ACTION_BET_TWO);
    ordago_apply(&state, MUS_ACTION_BET_TWO);
    ordago_apply(&state, MUS_ACTION_BET_FOUR);
    ordago_apply(&state, MUS_ACTION_BET_FOUR);
    ordago_apply(&state, MUS_ACTION_BET_TEN);
    CHECK(state.model.current_wager == 22);
    CHECK(state.model.previous_wager == 12);
    CHECK(ordago_test_numeric_wager_count(
              state.model.used_wager_action_masks) ==
          MUS_ORDAGO_MAX_NUMERIC_WAGERS);
    CHECK(state.model.used_wager_action_masks[CFR_PLAYER_0] == 11);
    CHECK(state.model.used_wager_action_masks[CFR_PLAYER_1] == 3);
    CHECK(cfr_game_legal_actions(ordago_game(), ordago_game_state(&state),
                                 actions, MUS_GAME_MAX_LEGAL_ACTIONS,
                                 &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 3 && actions[0] == MUS_ACTION_REJECT &&
          actions[1] == MUS_ACTION_ACCEPT &&
          actions[2] == MUS_ACTION_ORDAGO);
    CHECK(cfr_game_apply_action(ordago_game(), ordago_game_state(&state),
                                MUS_ACTION_BET_TWO) ==
          CFR_STATUS_ILLEGAL_ACTION);
    CHECK(cfr_game_apply_action(ordago_game(), ordago_game_state(&state),
                                MUS_ACTION_BET_TEN) ==
          CFR_STATUS_ILLEGAL_ACTION);
    information = ordago_information(&state);
    CHECK(information.numeric_wager_count ==
          MUS_ORDAGO_MAX_NUMERIC_WAGERS);
    CHECK(information.used_wager_action_masks[CFR_PLAYER_0] == 11);
    CHECK(information.used_wager_action_masks[CFR_PLAYER_1] == 3);
    CHECK(mus_ordago_game_information_set_legal_actions(
              &information, decoded_actions,
              MUS_GAME_MAX_LEGAL_ACTIONS, &decoded_count) ==
          CFR_STATUS_SUCCESS);
    CHECK(decoded_count == count);
    CHECK(memcmp(actions, decoded_actions,
                 count * sizeof(*actions)) == 0);

    ordago_deal(&short_history, kings, knights);
    ordago_deal(&long_history, kings, knights);
    ordago_apply(&short_history, MUS_ACTION_NO_MUS);
    ordago_apply(&long_history, MUS_ACTION_NO_MUS);
    ordago_apply(&short_history, MUS_ACTION_BET_TEN);
    ordago_apply(&short_history, MUS_ACTION_BET_TEN);
    ordago_apply(&long_history, MUS_ACTION_BET_TWO);
    ordago_apply(&long_history, MUS_ACTION_BET_FOUR);
    ordago_apply(&long_history, MUS_ACTION_BET_FOUR);
    ordago_apply(&long_history, MUS_ACTION_BET_TEN);
    CHECK(short_history.model.current_wager == 20);
    CHECK(long_history.model.current_wager == 20);
    CHECK(short_history.model.previous_wager == 10);
    CHECK(long_history.model.previous_wager == 10);
    CHECK(short_history.model.current_player ==
          long_history.model.current_player);
    CHECK(ordago_test_numeric_wager_count(
              short_history.model.used_wager_action_masks) == 2);
    CHECK(ordago_test_numeric_wager_count(
              long_history.model.used_wager_action_masks) == 4);
    short_key = ordago_information_key(&short_history);
    long_key = ordago_information_key(&long_history);
    CHECK(short_key != long_key);

    ordago_deal(&state, kings, knights);
    ordago_apply(&state, MUS_ACTION_NO_MUS);
    ordago_apply(&state, MUS_ACTION_BET_FOUR);
    CHECK(cfr_game_legal_actions(ordago_game(), ordago_game_state(&state),
                                 actions, MUS_GAME_MAX_LEGAL_ACTIONS,
                                 &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 5 && actions[0] == MUS_ACTION_REJECT &&
          actions[1] == MUS_ACTION_ACCEPT &&
          actions[2] == MUS_ACTION_BET_FOUR &&
          actions[3] == MUS_ACTION_BET_TEN &&
          actions[4] == MUS_ACTION_ORDAGO);
    CHECK(cfr_game_apply_action(ordago_game(), ordago_game_state(&state),
                                MUS_ACTION_BET_TWO) ==
          CFR_STATUS_ILLEGAL_ACTION);
    ordago_apply(&state, MUS_ACTION_BET_FOUR);
    CHECK(cfr_game_apply_action(ordago_game(), ordago_game_state(&state),
                                MUS_ACTION_BET_SEVEN) ==
          CFR_STATUS_ILLEGAL_ACTION);
    ordago_apply(&state, MUS_ACTION_BET_TEN);
    CHECK(state.model.current_wager == 18);
    CHECK(state.model.previous_wager == 8);
    CHECK(ordago_test_numeric_wager_count(
              state.model.used_wager_action_masks) == 3);
    CHECK(cfr_game_legal_actions(ordago_game(), ordago_game_state(&state),
                                 actions, MUS_GAME_MAX_LEGAL_ACTIONS,
                                 &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 4 && actions[0] == MUS_ACTION_REJECT &&
          actions[1] == MUS_ACTION_ACCEPT &&
          actions[2] == MUS_ACTION_BET_TEN &&
          actions[3] == MUS_ACTION_ORDAGO);
    CHECK(cfr_game_apply_action(ordago_game(), ordago_game_state(&state),
                                MUS_ACTION_BET_FOUR) ==
          CFR_STATUS_ILLEGAL_ACTION);

    ordago_deal(&state, kings, knights);
    ordago_apply(&state, MUS_ACTION_NO_MUS);
    ordago_apply(&state, MUS_ACTION_BET_TEN);
    ordago_apply(&state, MUS_ACTION_BET_TEN);
    CHECK(cfr_game_legal_actions(ordago_game(), ordago_game_state(&state),
                                 actions, MUS_GAME_MAX_LEGAL_ACTIONS,
                                 &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 3 && actions[0] == MUS_ACTION_REJECT &&
          actions[1] == MUS_ACTION_ACCEPT &&
          actions[2] == MUS_ACTION_ORDAGO);
    CHECK(cfr_game_apply_action(ordago_game(), ordago_game_state(&state),
                                MUS_ACTION_BET_TEN) ==
          CFR_STATUS_ILLEGAL_ACTION);
    ordago_apply(&state, MUS_ACTION_ORDAGO);
    CHECK(state.model.current_wager == MUS_ORDAGO_WAGER);
    CHECK(state.model.previous_wager == 20);
    CHECK(state.model.current_wager_is_ordago);

    ordago_deal(&state, kings, knights);
    ordago_apply(&state, MUS_ACTION_NO_MUS);
    ordago_apply(&state, MUS_ACTION_BET_TEN);
    ordago_apply(&state, MUS_ACTION_ACCEPT);
    information = ordago_information(&state);
    CHECK(information.max_settled_score == 10);
    CHECK(information.prior_wager_total == 10);
    CHECK(state.model.used_wager_action_masks[CFR_PLAYER_0] == 0);
    CHECK(state.model.used_wager_action_masks[CFR_PLAYER_1] == 0);
    ordago_apply(&state, MUS_ACTION_BET_TEN);
    ordago_apply(&state, MUS_ACTION_ACCEPT);
    CHECK(state.model.phase == MUS_PHASE_PARES);
    information = ordago_information(&state);
    CHECK(information.max_settled_score == 10);
    CHECK(information.prior_wager_total == 20);
    CHECK(cfr_game_legal_actions(ordago_game(), ordago_game_state(&state),
                                 actions, MUS_GAME_MAX_LEGAL_ACTIONS,
                                 &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 5 && actions[0] == MUS_ACTION_PASS &&
          actions[1] == MUS_ACTION_BET_TWO &&
          actions[2] == MUS_ACTION_BET_FOUR &&
          actions[3] == MUS_ACTION_BET_SEVEN &&
          actions[4] == MUS_ACTION_ORDAGO);
    CHECK(mus_ordago_game_information_set_legal_actions(
              &information, decoded_actions,
              MUS_GAME_MAX_LEGAL_ACTIONS, &decoded_count) ==
          CFR_STATUS_SUCCESS);
    CHECK(decoded_count == count);
    CHECK(memcmp(actions, decoded_actions,
                 count * sizeof(*actions)) == 0);
    CHECK(cfr_game_apply_action(ordago_game(), ordago_game_state(&state),
                                MUS_ACTION_BET_TEN) ==
          CFR_STATUS_ILLEGAL_ACTION);

    information.max_settled_score = 15;
    CHECK(mus_ordago_game_information_set_legal_actions(
              &information, decoded_actions,
              MUS_GAME_MAX_LEGAL_ACTIONS, &decoded_count) ==
          CFR_STATUS_SUCCESS);
    CHECK(decoded_count == 4 && decoded_actions[0] == MUS_ACTION_PASS &&
          decoded_actions[1] == MUS_ACTION_BET_TWO &&
          decoded_actions[2] == MUS_ACTION_BET_FOUR &&
          decoded_actions[3] == MUS_ACTION_ORDAGO);

    ordago_apply(&state, MUS_ACTION_BET_TWO);
    information = ordago_information(&state);
    CHECK(information.max_settled_score == 10);
    CHECK(information.prior_wager_total == 20);
    CHECK(information.current_wager == 2);
    CHECK(cfr_game_legal_actions(ordago_game(), ordago_game_state(&state),
                                 actions, MUS_GAME_MAX_LEGAL_ACTIONS,
                                 &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 5 && actions[0] == MUS_ACTION_REJECT &&
          actions[1] == MUS_ACTION_ACCEPT &&
          actions[2] == MUS_ACTION_BET_TWO &&
          actions[3] == MUS_ACTION_BET_FOUR &&
          actions[4] == MUS_ACTION_ORDAGO);
    CHECK(cfr_game_apply_action(ordago_game(), ordago_game_state(&state),
                                MUS_ACTION_BET_TEN) ==
          CFR_STATUS_ILLEGAL_ACTION);

    ordago_deal(&state, matched, matched);
    ordago_apply(&state, MUS_ACTION_NO_MUS);
    for (int lance = 0; lance < 2; lance += 1) {
        ordago_apply(&state, MUS_ACTION_BET_TEN);
        ordago_apply(&state, MUS_ACTION_ACCEPT);
    }
    information = ordago_information(&state);
    CHECK(information.max_settled_score == 20);
    CHECK(information.prior_wager_total == 20);
    CHECK(cfr_game_legal_actions(ordago_game(), ordago_game_state(&state),
                                 actions, MUS_GAME_MAX_LEGAL_ACTIONS,
                                 &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 2 && actions[0] == MUS_ACTION_PASS &&
          actions[1] == MUS_ACTION_ORDAGO);
    CHECK(cfr_game_apply_action(ordago_game(), ordago_game_state(&state),
                                MUS_ACTION_BET_TWO) ==
          CFR_STATUS_ILLEGAL_ACTION);

    ordago_deal(&state, kings, knights);
    ordago_apply(&state, MUS_ACTION_NO_MUS);
    ordago_apply(&state, MUS_ACTION_BET_TWO);
    ordago_apply(&state, MUS_ACTION_BET_TWO);
    ordago_apply(&state, MUS_ACTION_BET_FOUR);
    ordago_apply(&state, MUS_ACTION_BET_FOUR);
    ordago_apply(&state, MUS_ACTION_BET_TEN);
    CHECK(state.model.current_wager == 22);
    CHECK(ordago_test_numeric_wager_count(
              state.model.used_wager_action_masks) ==
          MUS_ORDAGO_MAX_NUMERIC_WAGERS);
    ordago_apply(&state, MUS_ACTION_ACCEPT);
    while (state.model.phase != MUS_PHASE_TERMINAL) {
        ordago_apply(&state, MUS_ACTION_PASS);
        ordago_apply(&state, MUS_ACTION_PASS);
    }
    CHECK(state.undo_count > 0 &&
          state.undo_count <= MUS_GAME_UNDO_CAPACITY);
    while (state.undo_count > 0) {
        CHECK(cfr_game_undo_action(ordago_game(), ordago_game_state(&state)) ==
              CFR_STATUS_SUCCESS);
    }
    CHECK(state.model.phase == MUS_PHASE_DEAL_PLAYER_0);
}

static void test_ordago_abstract_hands_and_strengths(void) {
    const uint8_t hand_a[MUS_GAME_CLASS_COUNT] = {1, 1, 0, 0, 0, 0, 1, 1};
    const uint8_t hand_b[MUS_GAME_CLASS_COUNT] = {0, 1, 1, 0, 0, 0, 1, 1};
    const uint8_t hand_c[MUS_GAME_CLASS_COUNT] = {1, 1, 1, 1, 0, 0, 0, 0};
    const uint8_t sixes[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 4, 0, 0, 0, 0};
    const uint8_t sevens[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 0, 4, 0, 0, 0};
    const uint8_t duples[MUS_GAME_CLASS_COUNT] = {2, 0, 0, 0, 0, 0, 0, 2};
    const uint8_t rival_duples[MUS_GAME_CLASS_COUNT] = {0, 2, 2, 0, 0, 0, 0, 0};
    const uint8_t game_31[MUS_GAME_CLASS_COUNT] = {1, 0, 0, 0, 0, 1, 1, 1};
    const uint8_t game_32[MUS_GAME_CLASS_COUNT] = {0, 0, 1, 0, 1, 1, 1, 0};
    const uint8_t point_17[MUS_GAME_CLASS_COUNT] = {1, 1, 1, 0, 1, 0, 0, 0};
    MusOrdagoGameState first;
    MusOrdagoGameState second;
    MusOrdagoInformationSet information;
    InfoSetKey first_key;
    InfoSetKey second_key;

    ordago_deal(&first, hand_a, sixes);
    ordago_deal(&second, hand_b, sixes);
    ordago_apply(&first, MUS_ACTION_NO_MUS);
    ordago_apply(&second, MUS_ACTION_NO_MUS);
    first_key = ordago_information_key(&first);
    second_key = ordago_information_key(&second);
    CHECK(first_key == second_key);
    information = ordago_information(&first);
    CHECK(information.hand_kind == MUS_ORDAGO_HAND_GRANDE);
    CHECK(information.king_count == 1);
    CHECK(information.has_highest_non_king);
    CHECK(information.highest_non_king == MUS_CLASS_KNIGHTS);

    ordago_deal(&first, hand_a, sevens);
    ordago_deal(&second, hand_c, sevens);
    ordago_apply(&first, MUS_ACTION_NO_MUS);
    ordago_apply(&second, MUS_ACTION_NO_MUS);
    ordago_apply(&first, MUS_ACTION_PASS);
    ordago_apply(&first, MUS_ACTION_PASS);
    ordago_apply(&second, MUS_ACTION_PASS);
    ordago_apply(&second, MUS_ACTION_PASS);
    CHECK(ordago_information_key(&first) ==
          ordago_information_key(&second));
    information = ordago_information(&first);
    CHECK(information.hand_kind == MUS_ORDAGO_HAND_CHICA);
    CHECK(information.pito_count == 1);
    CHECK(information.lowest_non_pito == MUS_CLASS_FOURS);
    CHECK(information.previous_strength_count == 1);
    CHECK(information.previous_strengths[0] ==
          MUS_LANCE_STRENGTH_NONE);

    for (int strength = MUS_LANCE_STRENGTH_NONE;
         strength <= MUS_LANCE_STRENGTH_BOTH; strength += 1) {
        ordago_deal(&first, hand_a, sevens);
        ordago_apply(&first, MUS_ACTION_NO_MUS);
        if (strength == MUS_LANCE_STRENGTH_NONE) {
            ordago_apply(&first, MUS_ACTION_PASS);
            ordago_apply(&first, MUS_ACTION_PASS);
        } else if (strength == MUS_LANCE_STRENGTH_PLAYER_0) {
            ordago_apply(&first, MUS_ACTION_BET_TWO);
            ordago_apply(&first, MUS_ACTION_REJECT);
        } else if (strength == MUS_LANCE_STRENGTH_PLAYER_1) {
            ordago_apply(&first, MUS_ACTION_PASS);
            ordago_apply(&first, MUS_ACTION_BET_TWO);
            ordago_apply(&first, MUS_ACTION_REJECT);
        } else {
            ordago_apply(&first, MUS_ACTION_BET_TWO);
            ordago_apply(&first, MUS_ACTION_ACCEPT);
        }
        information = ordago_information(&first);
        CHECK(information.phase == MUS_PHASE_CHICA);
        CHECK(information.previous_strengths[0] ==
              (MusLanceStrength)strength);
    }

    ordago_deal(&first, hand_a, sevens);
    ordago_deal(&second, hand_a, sevens);
    ordago_apply(&first, MUS_ACTION_NO_MUS);
    ordago_apply(&second, MUS_ACTION_NO_MUS);
    ordago_apply(&first, MUS_ACTION_BET_TWO);
    ordago_apply(&first, MUS_ACTION_ACCEPT);
    ordago_apply(&second, MUS_ACTION_BET_FOUR);
    ordago_apply(&second, MUS_ACTION_ACCEPT);
    CHECK(ordago_information_key(&first) !=
          ordago_information_key(&second));
    information = ordago_information(&first);
    CHECK(information.prior_wager_total == 2);
    information = ordago_information(&second);
    CHECK(information.prior_wager_total == 4);

    ordago_deal(&first, duples, rival_duples);
    ordago_apply(&first, MUS_ACTION_NO_MUS);
    for (int lance = 0; lance < 2; lance += 1) {
        ordago_apply(&first, MUS_ACTION_PASS);
        ordago_apply(&first, MUS_ACTION_PASS);
    }
    information = ordago_information(&first);
    CHECK(information.phase == MUS_PHASE_PARES);
    CHECK(information.hand_kind == MUS_ORDAGO_HAND_PARES);
    CHECK(information.pairs_type == MUS_PAIRS_DUPLES);
    CHECK(information.pairs_value == MUS_CLASS_KINGS);
    CHECK(information.has_second_pairs_value);
    CHECK(information.second_pairs_value == MUS_CLASS_PITOS);

    ordago_deal(&first, game_31, game_32);
    ordago_apply(&first, MUS_ACTION_NO_MUS);
    for (int lance = 0; lance < 2; lance += 1) {
        ordago_apply(&first, MUS_ACTION_PASS);
        ordago_apply(&first, MUS_ACTION_PASS);
    }
    information = ordago_information(&first);
    CHECK(information.phase == MUS_PHASE_JUEGO);
    CHECK(information.hand_kind == MUS_ORDAGO_HAND_JUEGO);
    CHECK(information.game_value == 31);
    CHECK(information.previous_strength_count == 3);
    CHECK(information.previous_strengths[2] ==
          MUS_LANCE_STRENGTH_PARES_SKIPPED_NONE);

    ordago_deal(&first, hand_c, point_17);
    ordago_apply(&first, MUS_ACTION_NO_MUS);
    for (int lance = 0; lance < 2; lance += 1) {
        ordago_apply(&first, MUS_ACTION_PASS);
        ordago_apply(&first, MUS_ACTION_PASS);
    }
    information = ordago_information(&first);
    CHECK(information.phase == MUS_PHASE_PUNTO);
    CHECK(information.hand_kind == MUS_ORDAGO_HAND_PUNTO);
    CHECK(information.point_value == 16);
}

static void test_ordago_public_pairs_and_scoring(void) {
    const uint8_t no_pairs[MUS_GAME_CLASS_COUNT] = {1, 1, 1, 1, 0, 0, 0, 0};
    const uint8_t p0_duples[MUS_GAME_CLASS_COUNT] = {2, 0, 0, 0, 2, 0, 0, 0};
    const uint8_t p1_duples[MUS_GAME_CLASS_COUNT] = {0, 2, 2, 0, 0, 0, 0, 0};
    const uint8_t kings[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 0, 0, 0, 0, 4};
    const uint8_t knights[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 0, 0, 0, 4, 0};
    const uint8_t game_31_medias[MUS_GAME_CLASS_COUNT] = {
        1, 0, 0, 0, 0, 0, 0, 3,
    };
    const uint8_t game_34_medias[MUS_GAME_CLASS_COUNT] = {
        0, 1, 0, 0, 0, 0, 3, 0,
    };
    MusOrdagoGameState skipped;
    MusOrdagoGameState skipped_player_0;
    MusOrdagoGameState played;
    MusOrdagoGameState rejected;
    MusOrdagoGameState rejected_numeric_open;
    MusOrdagoGameState rejected_ordago_open;
    MusOrdagoGameState capped;
    MusOrdagoInformationSet information;
    InfoSetKey skipped_key;
    InfoSetKey played_key;
    Utility utility = 0.0;

    ordago_deal(&skipped, no_pairs, p1_duples);
    ordago_deal(&played, p0_duples, p1_duples);
    ordago_apply(&skipped, MUS_ACTION_NO_MUS);
    ordago_apply(&played, MUS_ACTION_NO_MUS);
    for (int lance = 0; lance < 2; lance += 1) {
        ordago_apply(&skipped, MUS_ACTION_PASS);
        ordago_apply(&skipped, MUS_ACTION_PASS);
        ordago_apply(&played, MUS_ACTION_PASS);
        ordago_apply(&played, MUS_ACTION_PASS);
    }
    CHECK(skipped.model.phase == MUS_PHASE_PUNTO);
    CHECK(played.model.phase == MUS_PHASE_PARES);
    ordago_apply(&played, MUS_ACTION_PASS);
    ordago_apply(&played, MUS_ACTION_PASS);
    CHECK(played.model.phase == MUS_PHASE_PUNTO);

    skipped_key = ordago_information_key(&skipped);
    played_key = ordago_information_key(&played);
    CHECK(skipped_key != played_key);
    CHECK(mus_ordago_game_information_set_decode(skipped_key,
                                                  &information) ==
          CFR_STATUS_SUCCESS);
    CHECK(information.previous_strength_count == 3);
    CHECK(information.previous_strengths[2] ==
          MUS_LANCE_STRENGTH_PARES_SKIPPED_PLAYER_1);
    CHECK(mus_ordago_game_information_set_decode(played_key,
                                                  &information) ==
          CFR_STATUS_SUCCESS);
    CHECK(information.previous_strengths[2] == MUS_LANCE_STRENGTH_NONE);

    ordago_deal(&skipped_player_0, p0_duples, no_pairs);
    ordago_apply(&skipped_player_0, MUS_ACTION_NO_MUS);
    for (int lance = 0; lance < 2; lance += 1) {
        ordago_apply(&skipped_player_0, MUS_ACTION_PASS);
        ordago_apply(&skipped_player_0, MUS_ACTION_PASS);
    }
    information = ordago_information(&skipped_player_0);
    CHECK(information.phase == MUS_PHASE_PUNTO);
    CHECK(information.previous_strengths[2] ==
          MUS_LANCE_STRENGTH_PARES_SKIPPED_PLAYER_0);

    ordago_apply(&skipped, MUS_ACTION_PASS);
    ordago_apply(&skipped, MUS_ACTION_PASS);
    CHECK(cfr_game_terminal_utility(ordago_game(),
                                    ordago_game_state(&skipped),
                                    CFR_PLAYER_0, &utility) ==
          CFR_STATUS_SUCCESS);
    CHECK(utility == -2.0);

    ordago_deal(&rejected, kings, knights);
    ordago_apply(&rejected, MUS_ACTION_NO_MUS);
    ordago_apply(&rejected, MUS_ACTION_BET_TWO);
    ordago_apply(&rejected, MUS_ACTION_BET_TWO);
    ordago_apply(&rejected, MUS_ACTION_REJECT);
    CHECK(rejected.model.outcome_wagers[0] == 4);
    CHECK(rejected.model.outcome_previous_wagers[0] == 2);
    information = ordago_information(&rejected);
    CHECK(information.prior_wager_total == 2);
    while (rejected.model.phase != MUS_PHASE_TERMINAL) {
        ordago_apply(&rejected, MUS_ACTION_PASS);
        ordago_apply(&rejected, MUS_ACTION_PASS);
    }
    CHECK(cfr_game_terminal_utility(ordago_game(),
                                    ordago_game_state(&rejected),
                                    CFR_PLAYER_0, &utility) ==
          CFR_STATUS_SUCCESS);
    CHECK(utility == 2.0);

    ordago_deal(&rejected_numeric_open, kings, knights);
    ordago_apply(&rejected_numeric_open, MUS_ACTION_NO_MUS);
    ordago_apply(&rejected_numeric_open, MUS_ACTION_BET_TEN);
    ordago_apply(&rejected_numeric_open, MUS_ACTION_REJECT);
    information = ordago_information(&rejected_numeric_open);
    CHECK(information.prior_wager_total == 1);
    ordago_deal(&rejected_ordago_open, kings, knights);
    ordago_apply(&rejected_ordago_open, MUS_ACTION_NO_MUS);
    ordago_apply(&rejected_ordago_open, MUS_ACTION_ORDAGO);
    ordago_apply(&rejected_ordago_open, MUS_ACTION_REJECT);
    information = ordago_information(&rejected_ordago_open);
    CHECK(information.prior_wager_total == 1);
    CHECK(ordago_information_key(&rejected_numeric_open) ==
          ordago_information_key(&rejected_ordago_open));

    ordago_deal(&capped, game_31_medias, game_34_medias);
    ordago_apply(&capped, MUS_ACTION_NO_MUS);
    ordago_apply(&capped, MUS_ACTION_BET_TWO);
    ordago_apply(&capped, MUS_ACTION_BET_FOUR);
    ordago_apply(&capped, MUS_ACTION_BET_FOUR);
    ordago_apply(&capped, MUS_ACTION_BET_TEN);
    ordago_apply(&capped, MUS_ACTION_BET_TEN);
    CHECK(capped.model.current_wager == 30);
    CHECK(ordago_test_numeric_wager_count(
              capped.model.used_wager_action_masks) ==
          MUS_ORDAGO_MAX_NUMERIC_WAGERS);
    ordago_apply(&capped, MUS_ACTION_ACCEPT);
    while (capped.model.phase != MUS_PHASE_TERMINAL) {
        ordago_apply(&capped, MUS_ACTION_PASS);
        ordago_apply(&capped, MUS_ACTION_PASS);
    }
    CHECK(capped.model.phase == MUS_PHASE_TERMINAL);
    CHECK(cfr_game_terminal_utility(ordago_game(),
                                    ordago_game_state(&capped),
                                    CFR_PLAYER_0, &utility) ==
          CFR_STATUS_SUCCESS);
    CHECK(utility == 36.0);
}

static void test_ordago_decoder_rejects_invalid_keys(void) {
    MusOrdagoInformationSet information;
    InfoSetKey payload;
    InfoSetKey key;
    const int situation_count = MUS_GAME_MAX_MUSES * 4 + 20;
    const int grande_situation = MUS_GAME_MAX_MUSES * 4;
    const int chica_player_0_response_situation = grande_situation + 6;
    int history_radix = 1;
    int public_mus_radix = 0;

    for (int round = 0; round < MUS_GAME_MAX_MUSES; round += 1) {
        public_mus_radix += MUS_GAME_PLAYER_COUNT * history_radix;
        history_radix *= 16;
    }
    public_mus_radix += history_radix;

    CHECK(mus_ordago_game_information_set_decode(-1, &information) ==
          CFR_STATUS_INVALID_ARGUMENT);

    key = (InfoSetKey)330 * situation_count;
    CHECK(mus_ordago_game_information_set_decode(key, &information) ==
          CFR_STATUS_INVALID_ARGUMENT);

    key = ((InfoSetKey)29 * public_mus_radix) * situation_count +
          grande_situation;
    CHECK(mus_ordago_game_information_set_decode(key, &information) ==
          CFR_STATUS_INVALID_ARGUMENT);

    payload = 0;
    payload = payload * 41;
    payload = payload * 41;
    payload = payload * 41 + 40;
    payload = payload * 41 + 30;
    payload = payload * MUS_ORDAGO_WAGER_ACTION_MASK_COUNT + 11;
    payload = payload * MUS_ORDAGO_WAGER_ACTION_MASK_COUNT + 10;
    payload = payload * 2 + 1;
    key = payload * situation_count + grande_situation + 2;
    CHECK(mus_ordago_game_information_set_decode(key, &information) ==
          CFR_STATUS_SUCCESS);
    CHECK(information.current_wager_is_ordago);
    CHECK(information.previous_wager == 30);
    CHECK(information.numeric_wager_count == 5);
    CHECK(information.used_wager_action_masks[CFR_PLAYER_0] == 11);
    CHECK(information.used_wager_action_masks[CFR_PLAYER_1] == 10);

    payload = 0;
    payload = payload * 41;
    payload = payload * 41;
    payload = payload * 41 + 9;
    payload = payload * 41 + 2;
    payload = payload * MUS_ORDAGO_WAGER_ACTION_MASK_COUNT + 1;
    payload = payload * MUS_ORDAGO_WAGER_ACTION_MASK_COUNT + 4;
    payload = payload * 2;
    key = payload * situation_count + grande_situation + 2;
    CHECK(mus_ordago_game_information_set_decode(key, &information) ==
          CFR_STATUS_INVALID_ARGUMENT);

    payload = 0;
    payload = payload * 41;
    payload = payload * 41;
    payload = payload * 41 + 2;
    payload = payload * 41 + 1;
    payload = payload * MUS_ORDAGO_WAGER_ACTION_MASK_COUNT + 1;
    payload = payload * MUS_ORDAGO_WAGER_ACTION_MASK_COUNT;
    payload = payload * 2;
    key = payload * situation_count + grande_situation + 2;
    CHECK(mus_ordago_game_information_set_decode(key, &information) ==
          CFR_STATUS_INVALID_ARGUMENT);

    payload = 0;
    payload = payload * 41;
    payload = payload * 41;
    payload = payload * 41 + 2;
    payload = payload * 41 + 2;
    payload = payload * MUS_ORDAGO_WAGER_ACTION_MASK_COUNT + 1;
    payload = payload * MUS_ORDAGO_WAGER_ACTION_MASK_COUNT + 1;
    payload = payload * 2;
    key = payload * situation_count + grande_situation + 2;
    CHECK(mus_ordago_game_information_set_decode(key, &information) ==
          CFR_STATUS_INVALID_ARGUMENT);

    payload = 0;
    payload = payload * public_mus_radix;
    payload = payload * 4 + MUS_LANCE_STRENGTH_BOTH;
    payload = payload * 41 + 10;
    payload = payload * 41 + 20;
    payload = payload * 41 + 10;
    payload = payload * 41 + 1;
    payload = payload * MUS_ORDAGO_WAGER_ACTION_MASK_COUNT + 8;
    payload = payload * MUS_ORDAGO_WAGER_ACTION_MASK_COUNT + 8;
    payload = payload * 2;
    key = payload * situation_count + chica_player_0_response_situation;
    CHECK(mus_ordago_game_information_set_decode(key, &information) ==
          CFR_STATUS_INVALID_ARGUMENT);
}

static void test_information_boundaries(void) {
    const uint8_t own_hand[MUS_GAME_CLASS_COUNT] = {1, 1, 1, 1, 0, 0, 0, 0};
    const uint8_t rival_a[MUS_GAME_CLASS_COUNT] = {1, 1, 1, 0, 1, 0, 0, 0};
    const uint8_t rival_b[MUS_GAME_CLASS_COUNT] = {1, 1, 0, 0, 0, 1, 1, 0};
    MusGameState first;
    MusGameState second;
    InfoSetKey key_first;
    InfoSetKey key_second;
    MusInformationSet decoded;

    deal(&first, own_hand, rival_a);
    deal(&second, own_hand, rival_b);
    CHECK(cfr_game_information_set_key(game(), game_state(&first),
                                       &key_first) == CFR_STATUS_SUCCESS);
    CHECK(cfr_game_information_set_key(game(), game_state(&second),
                                       &key_second) == CFR_STATUS_SUCCESS);
    CHECK(key_first == key_second);
    CHECK(mus_game_information_set_decode(key_first, &decoded) ==
          CFR_STATUS_SUCCESS);
    CHECK(decoded.player == CFR_PLAYER_0);
    CHECK(decoded.phase == MUS_PHASE_GRANDE);
    CHECK(memcmp(decoded.hand, own_hand, MUS_GAME_CLASS_COUNT) == 0);

    apply(&first, MUS_ACTION_PASS);
    apply(&second, MUS_ACTION_PASS);
    CHECK(cfr_game_information_set_key(game(), game_state(&first),
                                       &key_first) == CFR_STATUS_SUCCESS);
    CHECK(cfr_game_information_set_key(game(), game_state(&second),
                                       &key_second) == CFR_STATUS_SUCCESS);
    CHECK(key_first != key_second);
    CHECK(mus_game_information_set_decode(key_first, &decoded) ==
          CFR_STATUS_SUCCESS);
    CHECK(decoded.player == CFR_PLAYER_1);
    CHECK(decoded.bet_stage == MUS_BET_SECOND_AFTER_PASS);
}

static Utility terminal_utility(MusGameState *state, Player player) {
    Utility utility = 999.0;

    CHECK(cfr_game_terminal_utility(game(), game_state(state), player,
                                    &utility) == CFR_STATUS_SUCCESS);
    return utility;
}

static Utility raises_terminal_utility(MusRaisesToEightGameState *state,
                                       Player player) {
    Utility utility = 999.0;

    CHECK(cfr_game_terminal_utility(raises_game(), raises_game_state(state),
                                    player, &utility) == CFR_STATUS_SUCCESS);
    return utility;
}

static void raises_pass_remaining_lances(MusRaisesToEightGameState *state) {
    while (state->model.phase != MUS_PHASE_TERMINAL) {
        raises_apply(state, MUS_ACTION_PASS);
        raises_apply(state, MUS_ACTION_PASS);
    }
}

static void test_literal_scoring(void) {
    const uint8_t kings[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 0, 0, 0, 0, 4};
    const uint8_t knights[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 0, 0, 0, 4, 0};
    const uint8_t low_a[MUS_GAME_CLASS_COUNT] = {1, 1, 1, 1, 0, 0, 0, 0};
    const uint8_t low_b[MUS_GAME_CLASS_COUNT] = {1, 1, 1, 0, 1, 0, 0, 0};
    MusGameState state;

    deal(&state, kings, knights);
    for (int lance = 0; lance < MUS_GAME_LANCE_COUNT; lance += 1) {
        apply(&state, MUS_ACTION_PASS);
        apply(&state, MUS_ACTION_PASS);
    }
    CHECK(state.model.phase == MUS_PHASE_TERMINAL);
    CHECK(terminal_utility(&state, CFR_PLAYER_0) == 5.0);
    CHECK(terminal_utility(&state, CFR_PLAYER_1) == -5.0);

    deal(&state, low_a, low_b);
    apply(&state, MUS_ACTION_PASS);
    apply(&state, MUS_ACTION_PASS);
    apply(&state, MUS_ACTION_PASS);
    apply(&state, MUS_ACTION_PASS);
    CHECK(state.model.phase == MUS_PHASE_PUNTO);
    CHECK(state.model.outcomes[2] == MUS_LANCE_SKIPPED);
    apply(&state, MUS_ACTION_PASS);
    apply(&state, MUS_ACTION_BET_TWO);
    apply(&state, MUS_ACTION_REJECT);
    CHECK(state.model.phase == MUS_PHASE_TERMINAL);
    CHECK(terminal_utility(&state, CFR_PLAYER_0) == -2.0);

    deal(&state, low_a, low_b);
    for (int lance = 0; lance < 2; lance += 1) {
        apply(&state, MUS_ACTION_PASS);
        apply(&state, MUS_ACTION_PASS);
    }
    apply(&state, MUS_ACTION_PASS);
    apply(&state, MUS_ACTION_BET_TWO);
    apply(&state, MUS_ACTION_ACCEPT);
    CHECK(terminal_utility(&state, CFR_PLAYER_0) == -3.0);
}

static void test_raises_to_eight_scoring(void) {
    const uint8_t kings[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 0, 0, 0, 0, 4};
    const uint8_t knights[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 0, 0, 0, 4, 0};
    MusRaisesToEightGameState state;

    raises_deal(&state, kings, knights);
    raises_pass_remaining_lances(&state);
    CHECK(raises_terminal_utility(&state, CFR_PLAYER_0) == 5.0);

    raises_deal(&state, kings, knights);
    raises_apply(&state, MUS_ACTION_BET_TWO);
    raises_apply(&state, MUS_ACTION_RAISE_DOUBLE);
    raises_apply(&state, MUS_ACTION_RAISE_DOUBLE);
    raises_apply(&state, MUS_ACTION_ACCEPT);
    raises_pass_remaining_lances(&state);
    CHECK(state.model.outcomes[0] == MUS_LANCE_FIRST_BET_ACCEPTED);
    CHECK(state.model.outcome_wagers[0] == 8);
    CHECK(raises_terminal_utility(&state, CFR_PLAYER_0) == 12.0);

    raises_deal(&state, kings, knights);
    raises_apply(&state, MUS_ACTION_BET_TWO);
    raises_apply(&state, MUS_ACTION_RAISE_DOUBLE);
    raises_apply(&state, MUS_ACTION_REJECT);
    raises_pass_remaining_lances(&state);
    CHECK(state.model.outcomes[0] == MUS_LANCE_SECOND_BET_REJECTED);
    CHECK(state.model.outcome_wagers[0] == 4);
    CHECK(raises_terminal_utility(&state, CFR_PLAYER_0) == 2.0);

    raises_deal(&state, kings, knights);
    raises_apply(&state, MUS_ACTION_BET_TWO);
    raises_apply(&state, MUS_ACTION_RAISE_DOUBLE);
    raises_apply(&state, MUS_ACTION_RAISE_DOUBLE);
    raises_apply(&state, MUS_ACTION_REJECT);
    raises_pass_remaining_lances(&state);
    CHECK(state.model.outcomes[0] == MUS_LANCE_FIRST_BET_REJECTED);
    CHECK(state.model.outcome_wagers[0] == 8);
    CHECK(raises_terminal_utility(&state, CFR_PLAYER_0) == 8.0);

    raises_deal(&state, kings, knights);
    raises_apply(&state, MUS_ACTION_PASS);
    raises_apply(&state, MUS_ACTION_BET_TWO);
    raises_apply(&state, MUS_ACTION_RAISE_DOUBLE);
    raises_apply(&state, MUS_ACTION_RAISE_DOUBLE);
    raises_apply(&state, MUS_ACTION_ACCEPT);
    raises_pass_remaining_lances(&state);
    CHECK(state.model.outcomes[0] == MUS_LANCE_SECOND_BET_ACCEPTED);
    CHECK(state.model.outcome_wagers[0] == 8);
    CHECK(raises_terminal_utility(&state, CFR_PLAYER_0) == 12.0);
}

static void compare_metric(double left, double right) {
    CHECK(fabs(left - right) < 1e-11);
}

static void test_streaming_evaluator_on_subgame(void) {
    const uint8_t low_a[MUS_GAME_CLASS_COUNT] = {1, 1, 1, 1, 0, 0, 0, 0};
    const uint8_t low_b[MUS_GAME_CLASS_COUNT] = {1, 1, 1, 0, 1, 0, 0, 0};
    MusGameState state;
    MusGameState generic_state;
    InfoStore store = {0};
    EvaluationMetrics streaming;
    EvaluationMetrics generic;

    deal(&state, low_a, low_b);
    for (int lance = 0; lance < 2; lance += 1) {
        apply(&state, MUS_ACTION_PASS);
        apply(&state, MUS_ACTION_PASS);
    }
    CHECK(state.model.phase == MUS_PHASE_PUNTO);
    generic_state = state;
    CHECK(cfr_info_store_init(&store) == CFR_STATUS_SUCCESS);
    CHECK(mus_evaluation_metrics_from_state(&state, &store, &streaming) ==
          CFR_STATUS_SUCCESS);
    CHECK(cfr_evaluation_metrics_with_unvisited_uniform(
              game(), game_state(&generic_state), &store, &generic) ==
          CFR_STATUS_SUCCESS);
    compare_metric(streaming.profile_value_player_0,
                   generic.profile_value_player_0);
    compare_metric(streaming.best_response_value_player_0,
                   generic.best_response_value_player_0);
    compare_metric(streaming.best_response_value_player_1,
                   generic.best_response_value_player_1);
    compare_metric(streaming.exploitability, generic.exploitability);
    CHECK(cfr_info_store_destroy(&store) == CFR_STATUS_SUCCESS);
}

static void test_raises_to_eight_evaluator_on_subgame(void) {
    const uint8_t low_a[MUS_GAME_CLASS_COUNT] = {1, 1, 1, 1, 0, 0, 0, 0};
    const uint8_t low_b[MUS_GAME_CLASS_COUNT] = {1, 1, 1, 0, 1, 0, 0, 0};
    MusRaisesToEightGameState state;
    MusRaisesToEightGameState generic_state;
    InfoStore store = {0};
    EvaluationMetrics streaming;
    EvaluationMetrics generic;

    raises_deal(&state, low_a, low_b);
    for (int lance = 0; lance < 2; lance += 1) {
        raises_apply(&state, MUS_ACTION_PASS);
        raises_apply(&state, MUS_ACTION_PASS);
    }
    CHECK(state.model.phase == MUS_PHASE_PUNTO);
    generic_state = state;
    CHECK(cfr_info_store_init(&store) == CFR_STATUS_SUCCESS);
    CHECK(mus_raises_to_eight_evaluation_metrics_from_state(
              &state, &store, &streaming) == CFR_STATUS_SUCCESS);
    CHECK(cfr_evaluation_metrics_with_unvisited_uniform(
              raises_game(), raises_game_state(&generic_state), &store,
              &generic) == CFR_STATUS_SUCCESS);
    compare_metric(streaming.profile_value_player_0,
                   generic.profile_value_player_0);
    compare_metric(streaming.best_response_value_player_0,
                   generic.best_response_value_player_0);
    compare_metric(streaming.best_response_value_player_1,
                   generic.best_response_value_player_1);
    compare_metric(streaming.exploitability, generic.exploitability);
    CHECK(cfr_info_store_destroy(&store) == CFR_STATUS_SUCCESS);
}

static void test_streaming_evaluator_error_outputs(void) {
    InfoStore store = {0};
    CHECK(cfr_info_store_init(&store) == CFR_STATUS_SUCCESS);
    for (int raises = 0; raises < 2; raises += 1) {
        Status (*evaluate)(const MusGameState *, const InfoStore *,
                           EvaluationMetrics *) = raises
            ? mus_raises_to_eight_evaluation_metrics_from_state
            : mus_single_bet_evaluation_metrics_from_state;
        MusGameState state;
        CHECK((raises ? mus_raises_to_eight_game_state_init(&state)
                      : mus_single_bet_game_state_init(&state)) == CFR_STATUS_SUCCESS);
        EvaluationMetrics metrics;
        memset(&metrics, 0xa5, sizeof(metrics));
        const EvaluationMetrics sentinel = metrics;
        CHECK(evaluate(NULL, &store, &metrics) == CFR_STATUS_INVALID_ARGUMENT);
        CHECK(evaluate(&state, NULL, &metrics) == CFR_STATUS_INVALID_ARGUMENT);
        CHECK(evaluate(&state, &store, NULL) == CFR_STATUS_INVALID_ARGUMENT);
        CHECK(memcmp(&metrics, &sentinel, sizeof(metrics)) == 0);
        state.model.phase = UINT8_MAX;
        const MusGameState original = state;
        CHECK(evaluate(&state, &store, &metrics) == CFR_STATUS_INVALID_ARGUMENT);
        CHECK(memcmp(&metrics, &sentinel, sizeof(metrics)) == 0);
        CHECK(memcmp(&state, &original, sizeof(state)) == 0);
    }
    CHECK(cfr_info_store_destroy(&store) == CFR_STATUS_SUCCESS);
}

static void test_ordago_evaluator_on_subgame(void) {
    const uint8_t low_a[MUS_GAME_CLASS_COUNT] = {1, 1, 1, 1, 0, 0, 0, 0};
    const uint8_t low_b[MUS_GAME_CLASS_COUNT] = {1, 1, 1, 0, 1, 0, 0, 0};
    MusOrdagoGameState state;
    MusOrdagoGameState generic_state;
    InfoStore store = {0};
    EvaluationMetrics streaming;
    EvaluationMetrics generic;

    ordago_deal(&state, low_a, low_b);
    ordago_apply(&state, MUS_ACTION_NO_MUS);
    for (int lance = 0; lance < 2; lance += 1) {
        ordago_apply(&state, MUS_ACTION_PASS);
        ordago_apply(&state, MUS_ACTION_PASS);
    }
    CHECK(state.model.phase == MUS_PHASE_PUNTO);
    generic_state = state;
    CHECK(cfr_info_store_init(&store) == CFR_STATUS_SUCCESS);
    CHECK(mus_ordago_evaluation_metrics(NULL, &streaming) ==
          CFR_STATUS_INVALID_ARGUMENT);
    CHECK(mus_ordago_evaluation_metrics(&store, NULL) ==
          CFR_STATUS_INVALID_ARGUMENT);
    CHECK(mus_ordago_evaluation_metrics(&store, &streaming) ==
          CFR_STATUS_SUCCESS);
    CHECK(isfinite(streaming.profile_value_player_0));
    CHECK(isfinite(streaming.exploitability));
    CHECK(mus_ordago_evaluation_metrics_from_state(&state, &store,
                                                    &streaming) ==
          CFR_STATUS_SUCCESS);
    CHECK(cfr_evaluation_metrics_with_unvisited_uniform(
              ordago_game(), ordago_game_state(&generic_state), &store,
              &generic) == CFR_STATUS_SUCCESS);
    compare_metric(streaming.profile_value_player_0,
                   generic.profile_value_player_0);
    compare_metric(streaming.best_response_value_player_0,
                   generic.best_response_value_player_0);
    compare_metric(streaming.best_response_value_player_1,
                   generic.best_response_value_player_1);
    compare_metric(streaming.exploitability, generic.exploitability);
    CHECK(cfr_info_store_destroy(&store) == CFR_STATUS_SUCCESS);
}

static bool streams_equal(FILE *left, FILE *right) {
    int left_byte;
    int right_byte;

    rewind(left);
    rewind(right);
    do {
        left_byte = fgetc(left);
        right_byte = fgetc(right);
        if (left_byte != right_byte)
            return false;
    } while (left_byte != EOF);
    return !ferror(left) && !ferror(right);
}

typedef struct {
    pthread_t caller;
    const MusGameState *root;
    size_t initial_iterations;
    TrainerStats initial_stats;
    size_t amount;
    size_t interval;
    size_t completed;
    size_t calls;
    size_t fail_after;
} SharedTrainingProgress;

static Status check_shared_training_progress(const Trainer *trainer,
                                             size_t completed, void *context) {
    SharedTrainingProgress *progress = context;
    const MusGameState *state = (const MusGameState *)trainer->state;
    const size_t remaining = progress->amount - progress->completed;
    const size_t batch = remaining < progress->interval ? remaining : progress->interval;
    CHECK(pthread_equal(pthread_self(), progress->caller));
    CHECK(completed == progress->completed + batch);
    CHECK(trainer->training_iterations == progress->initial_iterations + completed);
    CHECK(trainer->stats.iterations == progress->initial_stats.iterations + completed);
    CHECK(trainer->stats.traversals == progress->initial_stats.traversals +
          completed * trainer->game->strategic_player_count);
    CHECK(trainer->stats.visited_nodes > progress->initial_stats.visited_nodes);
    CHECK(trainer->stats.errors == progress->initial_stats.errors);
    CHECK(state->undo_count == progress->root->undo_count);
    CHECK(memcmp(&state->model, &progress->root->model, sizeof(state->model)) == 0);
    CHECK(state->continuation == progress->root->continuation);
    CHECK(cfr_game_validate_state(trainer->game, trainer->state) == CFR_STATUS_SUCCESS);

    /* Progress callbacks must observe one stable store while workers wait. */
    FILE *first = tmpfile();
    FILE *second = tmpfile();
    CHECK(first != NULL && second != NULL);
    if (first != NULL && second != NULL) {
        CHECK(cfr_checkpoint_write(first, trainer) == CFR_STATUS_SUCCESS);
        CHECK(cfr_checkpoint_write(second, trainer) == CFR_STATUS_SUCCESS);
        CHECK(streams_equal(first, second));
        MusGameState restored_state = *progress->root;
        InfoStore restored_store = {0};
        Trainer restored;
        rewind(first);
        const Status status = cfr_checkpoint_read(
            first, trainer->game, game_state(&restored_state), &restored_store, &restored);
        CHECK(status == CFR_STATUS_SUCCESS);
        if (status == CFR_STATUS_SUCCESS) {
            CHECK(restored.training_iterations == trainer->training_iterations);
            CHECK(restored.stats.iterations == trainer->stats.iterations);
            CHECK(restored.stats.traversals == trainer->stats.traversals);
            CHECK(restored.stats.visited_nodes == trainer->stats.visited_nodes);
            CHECK(restored.stats.errors == trainer->stats.errors);
            CHECK(cfr_info_store_destroy(&restored_store) == CFR_STATUS_SUCCESS);
        }
    }
    if (first != NULL)
        fclose(first);
    if (second != NULL)
        fclose(second);
    progress->completed = completed;
    progress->calls += 1;
    return progress->fail_after != 0 && completed >= progress->fail_after
               ? CFR_STATUS_IO_ERROR : CFR_STATUS_SUCCESS;
}

static void test_shared_training_runner(void) {
    MusScoreTable table;
    MusScoreGame score;
    MusGameState state;
    MusGameState serial_state;
    InfoStore store = {0};
    InfoStore serial_store = {0};
    Trainer trainer;
    Trainer serial;
    CHECK(mus_score_table_init(&table, 3) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_init(&score, &table, 2, 2) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_state_init(&state, &score) == CFR_STATUS_SUCCESS);
    const MusGameState root = state;
    const MusScoreTable frozen_table = table;
    serial_state = root;
    CHECK(cfr_info_store_init(&store) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_init(&serial_store) == CFR_STATUS_SUCCESS);
    CHECK(cfr_trainer_init_mccfr(&trainer, &score.game, game_state(&state), &store, 31) ==
          CFR_STATUS_SUCCESS);
    CHECK(cfr_trainer_init_mccfr(&serial, &score.game, game_state(&serial_state),
                                &serial_store, 31) == CFR_STATUS_SUCCESS);
    CHECK(cfr_trainer_run(&serial, 3) == CFR_STATUS_SUCCESS);
    CHECK(mus_training_run(&trainer, 2, 1, 1, NULL, NULL) == CFR_STATUS_SUCCESS);
    SharedTrainingProgress progress = {
        .caller = pthread_self(), .root = &root,
        .initial_iterations = trainer.training_iterations, .initial_stats = trainer.stats,
        .amount = 1, .interval = 1,
    };
    CHECK(mus_training_run(&trainer, 1, 1, 1, check_shared_training_progress, &progress) ==
          CFR_STATUS_SUCCESS);
    CHECK(progress.calls == 1 && progress.completed == 1);
    FILE *actual = tmpfile();
    FILE *expected = tmpfile();
    CHECK(actual != NULL && expected != NULL);
    if (actual != NULL && expected != NULL) {
        CHECK(cfr_checkpoint_write(actual, &trainer) == CFR_STATUS_SUCCESS);
        CHECK(cfr_checkpoint_write(expected, &serial) == CFR_STATUS_SUCCESS);
        CHECK(streams_equal(actual, expected));
    }
    if (actual != NULL)
        fclose(actual);
    if (expected != NULL)
        fclose(expected);
    CHECK(cfr_info_store_destroy(&serial_store) == CFR_STATUS_SUCCESS);

    const Trainer unchanged = trainer;
    CHECK(mus_training_run(NULL, 1, 1, 1, NULL, NULL) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(mus_training_run(&trainer, 1, 0, 1, NULL, NULL) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(mus_training_run(&trainer, 1, 1, 0, NULL, NULL) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(mus_training_run(&trainer, 0, 0, 1, NULL, NULL) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(mus_training_run(&trainer, 0, 1, 0, NULL, NULL) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(mus_training_run(&trainer, 0, 8, 1, check_shared_training_progress, &progress) ==
          CFR_STATUS_SUCCESS);
    CHECK(mus_training_run(&trainer, 0, 8, 1, NULL, NULL) == CFR_STATUS_SUCCESS);
    CHECK(progress.calls == 1);
    CHECK(memcmp(&trainer, &unchanged, sizeof(trainer)) == 0);

    /* Statistics may start below the checkpoint's training iteration count. */
    CHECK(cfr_trainer_reset_stats(&trainer) == CFR_STATUS_SUCCESS);
    CHECK(cfr_trainer_run(&trainer, 1) == CFR_STATUS_SUCCESS);
    progress = (SharedTrainingProgress){
        .caller = pthread_self(), .root = &root,
        .initial_iterations = trainer.training_iterations, .initial_stats = trainer.stats,
        .amount = 5, .interval = 2,
    };
    CHECK(mus_training_run(&trainer, 5, 3, 2, check_shared_training_progress, &progress) ==
          CFR_STATUS_SUCCESS);
    CHECK(progress.calls == 3 && progress.completed == 5);
    CHECK(trainer.training_iterations == 9);
    CHECK(trainer.stats.iterations == 6 && trainer.stats.traversals == 12);

    progress = (SharedTrainingProgress){
        .caller = pthread_self(), .root = &root,
        .initial_iterations = trainer.training_iterations, .initial_stats = trainer.stats,
        .amount = 5, .interval = 2, .fail_after = 2,
    };
    CHECK(mus_training_run(&trainer, 5, 8, 2, check_shared_training_progress, &progress) ==
          CFR_STATUS_IO_ERROR);
    CHECK(progress.calls == 1 && progress.completed == 2);
    CHECK(trainer.training_iterations == 11 && trainer.stats.iterations == 8);
    CHECK(trainer.stats.traversals == 16 && trainer.stats.errors == 0);

    FILE *checkpoint = tmpfile();
    CHECK(checkpoint != NULL);
    if (checkpoint != NULL) {
        CHECK(cfr_checkpoint_write(checkpoint, &trainer) == CFR_STATUS_SUCCESS);
        CHECK(cfr_info_store_destroy(&store) == CFR_STATUS_SUCCESS);
        state = root;
        rewind(checkpoint);
        const Status status = cfr_checkpoint_read(checkpoint, &score.game,
                                                  game_state(&state), &store, &trainer);
        CHECK(status == CFR_STATUS_SUCCESS);
        if (status == CFR_STATUS_SUCCESS) {
            progress = (SharedTrainingProgress){
                .caller = pthread_self(), .root = &root,
                .initial_iterations = trainer.training_iterations,
                .initial_stats = trainer.stats, .amount = 3, .interval = 100,
            };
            CHECK(mus_training_run(&trainer, 3, SIZE_MAX, 100,
                                    check_shared_training_progress, &progress) ==
                  CFR_STATUS_SUCCESS);
            CHECK(progress.calls == 1 && progress.completed == 3);
            CHECK(trainer.training_iterations == 14 && trainer.stats.iterations == 11);
            CHECK(trainer.stats.traversals == 22 && trainer.stats.errors == 0);
            CHECK(mus_training_run(&trainer, 1, 1, 1, NULL, NULL) == CFR_STATUS_SUCCESS);
            CHECK(trainer.training_iterations == 15 && trainer.stats.iterations == 12);
        }
        fclose(checkpoint);
    }
    CHECK(memcmp(&table, &frozen_table, sizeof(table)) == 0);
    CHECK(state.undo_count == 0 && memcmp(&state.model, &root.model, sizeof(state.model)) == 0);
    CHECK(cfr_info_store_destroy(&store) == CFR_STATUS_SUCCESS);
}

static void test_training_resume_and_export(void) {
    MusGameState continuous_state;
    MusGameState split_state;
    InfoStore continuous_store = {0};
    InfoStore split_store = {0};
    Trainer continuous;
    Trainer split;
    FILE *continuous_file;
    FILE *split_file;
    FILE *corrupt_file;
    FILE *text_file;
    char header[256] = {0};

    CHECK(mus_game_state_init(&continuous_state) == CFR_STATUS_SUCCESS);
    CHECK(mus_game_state_init(&split_state) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_init(&continuous_store) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_init(&split_store) == CFR_STATUS_SUCCESS);
    CHECK(cfr_trainer_init_mccfr(&continuous, game(),
                                 game_state(&continuous_state),
                                 &continuous_store, 17) == CFR_STATUS_SUCCESS);
    CHECK(cfr_trainer_init_mccfr(&split, game(), game_state(&split_state),
                                 &split_store, 17) == CFR_STATUS_SUCCESS);
    CHECK(cfr_trainer_run(&continuous, 50) == CFR_STATUS_SUCCESS);
    CHECK(cfr_trainer_run(&split, 20) == CFR_STATUS_SUCCESS);
    CHECK(cfr_trainer_run(&split, 30) == CFR_STATUS_SUCCESS);

    continuous_file = tmpfile();
    split_file = tmpfile();
    corrupt_file = tmpfile();
    text_file = tmpfile();
    CHECK(continuous_file != NULL && split_file != NULL &&
          corrupt_file != NULL && text_file != NULL);
    if (continuous_file != NULL && split_file != NULL &&
        corrupt_file != NULL && text_file != NULL) {
        MusGameState restored_state;
        InfoStore restored_store = {0};
        Trainer restored_trainer;
        KuhnPokerState kuhn_state;
        InfoStore incompatible_store = {0};
        Trainer incompatible_trainer;

        CHECK(cfr_checkpoint_write(continuous_file, &continuous) ==
              CFR_STATUS_SUCCESS);
        CHECK(cfr_checkpoint_write(split_file, &split) == CFR_STATUS_SUCCESS);
        CHECK(cfr_checkpoint_write(corrupt_file, &continuous) ==
              CFR_STATUS_SUCCESS);
        CHECK(streams_equal(continuous_file, split_file));

        CHECK(mus_game_state_init(&restored_state) == CFR_STATUS_SUCCESS);
        rewind(continuous_file);
        CHECK(cfr_checkpoint_read(continuous_file, game(),
                                  game_state(&restored_state),
                                  &restored_store, &restored_trainer) ==
              CFR_STATUS_SUCCESS);
        CHECK(restored_trainer.training_iterations == 50);
        CHECK(cfr_info_store_destroy(&restored_store) == CFR_STATUS_SUCCESS);

        rewind(corrupt_file);
        CHECK(fputc('X', corrupt_file) != EOF);
        rewind(corrupt_file);
        CHECK(cfr_checkpoint_read(corrupt_file, game(),
                                  game_state(&restored_state),
                                  &restored_store, &restored_trainer) ==
              CFR_STATUS_FORMAT_ERROR);
        CHECK(restored_store.entries == NULL);

        CHECK(cfr_kuhn_poker_state_init(&kuhn_state) == CFR_STATUS_SUCCESS);
        rewind(split_file);
        CHECK(cfr_checkpoint_read(
                  split_file, cfr_kuhn_poker_descriptor(),
                  cfr_kuhn_poker_state_as_game_state(&kuhn_state),
                  &incompatible_store, &incompatible_trainer) ==
              CFR_STATUS_INCOMPATIBLE_GAME);
        CHECK(incompatible_store.entries == NULL);

        CHECK(mus_strategy_write_text(text_file, &continuous) ==
              CFR_STATUS_SUCCESS);
        rewind(text_file);
        CHECK(fgets(header, sizeof(header), text_file) != NULL);
        CHECK(strstr(header, "mus-strategy version=1") != NULL);
        CHECK(fgets(header, sizeof(header), text_file) != NULL);
        CHECK(strstr(header, "posicion=") != NULL);
        CHECK(strstr(header, "lance=") != NULL);
        fclose(continuous_file);
        fclose(split_file);
        fclose(corrupt_file);
        fclose(text_file);
    }
    CHECK(cfr_info_store_destroy(&continuous_store) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_destroy(&split_store) == CFR_STATUS_SUCCESS);
}

static void test_raises_to_eight_training_and_export(void) {
    MusRaisesToEightGameState state;
    InfoStore store = {0};
    Trainer trainer;
    FILE *text_file;
    char line[2048];
    bool found_raise = false;

    CHECK(mus_raises_to_eight_game_state_init(&state) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_init(&store) == CFR_STATUS_SUCCESS);
    CHECK(cfr_trainer_init_mccfr(&trainer, raises_game(),
                                 raises_game_state(&state), &store, 23) ==
          CFR_STATUS_SUCCESS);
    CHECK(cfr_trainer_run(&trainer, 50) == CFR_STATUS_SUCCESS);
    text_file = tmpfile();
    CHECK(text_file != NULL);
    if (text_file != NULL) {
        CHECK(mus_strategy_write_text(text_file, &trainer) ==
              CFR_STATUS_INVALID_ARGUMENT);
        CHECK(mus_raises_to_eight_strategy_write_text(text_file, &trainer) ==
              CFR_STATUS_SUCCESS);
        rewind(text_file);
        CHECK(fgets(line, sizeof(line), text_file) != NULL);
        CHECK(strstr(line, "mus.heads-up-single-hand.raises-to-eight/v1") !=
              NULL);
        while (fgets(line, sizeof(line), text_file) != NULL) {
            if (strstr(line, "subir_a_4=") != NULL ||
                strstr(line, "subir_a_8=") != NULL) {
                found_raise = true;
            }
        }
        CHECK(found_raise);
        fclose(text_file);
    }
    CHECK(cfr_info_store_destroy(&store) == CFR_STATUS_SUCCESS);
}

static void test_ordago_training_and_export(void) {
    MusOrdagoGameState state;
    InfoStore store = {0};
    Trainer trainer;
    FILE *text_file;
    char line[4096];
    bool found_wager = false;
    bool found_ordago = false;
    bool found_discard = false;

    CHECK(mus_ordago_game_state_init(&state) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_init(&store) == CFR_STATUS_SUCCESS);
    CHECK(cfr_trainer_init_mccfr(&trainer, ordago_game(),
                                 ordago_game_state(&state), &store, 29) ==
          CFR_STATUS_SUCCESS);
    CHECK(cfr_trainer_run(&trainer, 2) == CFR_STATUS_SUCCESS);
    text_file = tmpfile();
    CHECK(text_file != NULL);
    if (text_file != NULL) {
        CHECK(mus_ordago_strategy_write_text(text_file, &trainer) ==
              CFR_STATUS_SUCCESS);
        rewind(text_file);
        CHECK(fgets(line, sizeof(line), text_file) != NULL);
        CHECK(strstr(line, MUS_ORDAGO_STRATEGY_SCHEMA_ID) != NULL);
        while (fgets(line, sizeof(line), text_file) != NULL) {
            if (strstr(line, "envidar_") != NULL)
                found_wager = true;
            if (strstr(line, "ordago=") != NULL)
                found_ordago = true;
            if (strstr(line, "cambiar_") != NULL)
                found_discard = true;
        }
        CHECK(found_wager);
        CHECK(found_ordago);
        CHECK(found_discard);
        fclose(text_file);
    }
    CHECK(cfr_info_store_destroy(&store) == CFR_STATUS_SUCCESS);
}

static void test_ordago_reservation_limits(void) {
    CHECK(mus_cli_ordago_reservation(0) == 0);
    CHECK(mus_cli_ordago_reservation(500) == 6300000);
    CHECK(mus_cli_ordago_reservation(1000) == 12600000);
    CHECK(mus_cli_ordago_reservation(1001) == 12601200);
    CHECK(mus_cli_ordago_reservation(2000) == 13800000);
    CHECK(mus_cli_ordago_reservation(20000) == 35400000);
    CHECK(mus_cli_ordago_reservation(20001) == 35400000);
    CHECK(mus_cli_ordago_reservation(SIZE_MAX) == 35400000);
}

static void score_apply(const MusScoreGame *game, MusGameState *state, Action action) {
    CHECK(cfr_game_apply_action(&game->game, game_state(state), action) == CFR_STATUS_SUCCESS);
}

static void score_deal(const MusScoreGame *game, MusGameState *state,
                       const uint8_t a[8], const uint8_t b[8]) {
    CHECK(mus_score_game_state_init(state, game) == CFR_STATUS_SUCCESS);
    for (int c = 0; c < 7; c += 1)
        score_apply(game, state, a[c]);
    for (int c = 0; c < 7; c += 1)
        score_apply(game, state, b[c]);
    score_apply(game, state, MUS_ACTION_NO_MUS);
}

static const uint8_t SCORE_HAND_A[8] = {1, 1, 1, 1, 0, 0, 0, 0};
static const uint8_t SCORE_HAND_B[8] = {1, 1, 0, 0, 1, 1, 0, 0};

static void check_legacy_candidate(const MusScoreGame *score, InfoSetKey key,
                                    InfoSetKey expected) {
    MusOrdagoInformationSet decoded;
    InfoSetKey round_trip = -1;
    CHECK(mus_score_game_information_set_decode(key, score->continuation->target, &decoded) ==
          CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_information_set_encode(&decoded, score->continuation->target,
                                                &round_trip) == CFR_STATUS_SUCCESS);
    CHECK(round_trip == key);
    InfoSetKey candidates[41];
    size_t count = 0;
    CHECK(mus_score_information_set_ordago_candidates(score, key, candidates, 41, &count) ==
          CFR_STATUS_SUCCESS);
    bool found = false;
    for (size_t i = 0; i < count; ++i) {
        MusOrdagoInformationSet information;
        CHECK(mus_ordago_game_information_set_decode(candidates[i], &information) == CFR_STATUS_SUCCESS);
        found |= candidates[i] == expected;
    }
    CHECK(found);
}

static void test_score_preload_key_mapping(void) {
    MusScoreTable table;
    MusScoreGame score;
    MusGameState old_state, new_state;
    unsigned int random = 7, decisions = 0;
    bool phases[MUS_PHASE_REDRAW_PLAYER_1 + 1] = {0};
    CHECK(mus_score_table_init(&table, 40) == CFR_STATUS_SUCCESS);
    /* Compare converted keys against keys of independently advanced states. */
    for (unsigned int hand = 0; hand < 128; hand += 1) {
        CHECK(mus_score_game_init(&score, &table, hand % 9, hand % 5) == CFR_STATUS_SUCCESS);
        CHECK(mus_ordago_game_state_init(&old_state) == CFR_STATUS_SUCCESS);
        CHECK(mus_score_game_state_init(&new_state, &score) == CFR_STATUS_SUCCESS);
        for (unsigned int step = 0; step < 256 && new_state.model.phase != MUS_PHASE_TERMINAL;
             step += 1) {
            Actor actor;
            CHECK(cfr_game_current_actor(ordago_game(), game_state(&old_state), &actor) ==
                  CFR_STATUS_SUCCESS);
            if (actor.kind == CFR_ACTOR_PLAYER) {
                InfoSetKey original, converted, expected;
                CHECK(cfr_game_information_set_key(ordago_game(), game_state(&old_state),
                                                    &original) == CFR_STATUS_SUCCESS);
                const bool transferable = old_state.model.phase == MUS_PHASE_GRANDE ||
                    old_state.model.phase >= MUS_PHASE_MUS_PLAYER_0;
                CHECK(mus_score_information_set_from_ordago(&score, original, 0, 0, &converted) ==
                      (transferable ? CFR_STATUS_SUCCESS : CFR_STATUS_NOT_FOUND));
                CHECK(cfr_game_information_set_key(&score.game, game_state(&new_state),
                                                    &expected) == CFR_STATUS_SUCCESS);
                if (transferable)
                    CHECK(converted == expected);
                check_legacy_candidate(&score, expected, original);
                decisions += 1;
                phases[old_state.model.phase] = true;
            }
            Action actions[MUS_GAME_MAX_LEGAL_ACTIONS];
            size_t count = 0;
            CHECK(cfr_game_legal_actions(ordago_game(), game_state(&old_state), actions,
                                          MUS_GAME_MAX_LEGAL_ACTIONS, &count) == CFR_STATUS_SUCCESS);
            if (count == 0)
                break;
            Action score_actions[MUS_GAME_MAX_LEGAL_ACTIONS];
            size_t score_count = 0, shared_count = 0;
            CHECK(cfr_game_legal_actions(&score.game, game_state(&new_state), score_actions,
                      MUS_GAME_MAX_LEGAL_ACTIONS, &score_count) == CFR_STATUS_SUCCESS);
            for (size_t i = 0; i < count; i += 1) {
                for (size_t j = 0; j < score_count; j += 1) {
                    if (actions[i] == score_actions[j]) {
                        actions[shared_count++] = actions[i];
                        break;
                    }
                }
            }
            CHECK(shared_count > 0);
            if (shared_count == 0) break;
            random = random * 1664525U + 1013904223U;
            const Action action = actions[(random >> 16) % shared_count];
            ordago_apply(&old_state, action);
            score_apply(&score, &new_state, action);
        }
        CHECK(new_state.model.phase == MUS_PHASE_TERMINAL);
    }
    const uint8_t kings_31[8] = {1, 0, 0, 0, 0, 0, 0, 3};
    const uint8_t jacks_31[8] = {1, 0, 0, 0, 0, 3, 0, 0};
    for (int sample = 0; sample < 2; sample += 1) {
        const uint8_t *a = sample == 0 ? kings_31 : SCORE_HAND_A;
        const uint8_t *b = sample == 0 ? jacks_31 : SCORE_HAND_B;
        ordago_deal(&old_state, a, b);
        ordago_apply(&old_state, MUS_ACTION_NO_MUS);
        score_deal(&score, &new_state, a, b);
        while (new_state.model.phase != MUS_PHASE_TERMINAL) {
            InfoSetKey converted, expected;
            const bool transferable = old_state.model.phase == MUS_PHASE_GRANDE;
            CHECK(mus_score_information_set_from_ordago(&score, ordago_information_key(&old_state),
                      0, 0, &converted) == (transferable ? CFR_STATUS_SUCCESS : CFR_STATUS_NOT_FOUND));
            CHECK(cfr_game_information_set_key(&score.game, game_state(&new_state), &expected) ==
                  CFR_STATUS_SUCCESS);
            if (transferable)
                CHECK(converted == expected);
            check_legacy_candidate(&score, expected, ordago_information_key(&old_state));
            phases[old_state.model.phase] = true;
            ordago_apply(&old_state, MUS_ACTION_PASS);
            score_apply(&score, &new_state, MUS_ACTION_PASS);
        }
    }
    CHECK(decisions > 500);
    CHECK(phases[MUS_PHASE_MUS_PLAYER_0] && phases[MUS_PHASE_MUS_PLAYER_1]);
    CHECK(phases[MUS_PHASE_DISCARD_PLAYER_0] && phases[MUS_PHASE_DISCARD_PLAYER_1]);
    CHECK(phases[MUS_PHASE_GRANDE] && phases[MUS_PHASE_CHICA]);
    CHECK(phases[MUS_PHASE_PARES] && phases[MUS_PHASE_JUEGO] && phases[MUS_PHASE_PUNTO]);
    InfoSetKey unchanged = -7;
    CHECK(mus_score_information_set_from_ordago(NULL, 0, 0, 0, &unchanged) ==
          CFR_STATUS_INVALID_ARGUMENT);
    CHECK(mus_score_information_set_from_ordago(&score, -1, 0, 0, &unchanged) ==
          CFR_STATUS_INVALID_ARGUMENT);
    CHECK(mus_score_information_set_from_ordago(&score, 0, 1, 0, &unchanged) ==
          CFR_STATUS_INVALID_ARGUMENT);
    CHECK(unchanged == -7);
}

static void test_score_preload_policy(void) {
    MusScoreTable table;
    MusScoreGame score;
    MusGameState old_state, new_state;
    InfoStore source_store = {0}, destination = {0};
    Trainer source;
    CHECK(mus_score_table_init(&table, 40) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_init(&score, &table, 2, 5) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_init(&source_store) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_init(&destination) == CFR_STATUS_SUCCESS);
    ordago_deal(&old_state, SCORE_HAND_B, SCORE_HAND_A);
    ordago_apply(&old_state, MUS_ACTION_NO_MUS);
    score_deal(&score, &new_state, SCORE_HAND_B, SCORE_HAND_A);
    const InfoSetKey source_key = ordago_information_key(&old_state);
    InfoSetKey target_key;
    CHECK(cfr_game_information_set_key(&score.game, game_state(&new_state), &target_key) ==
          CFR_STATUS_SUCCESS);
    InfoNode *node;
    CHECK(cfr_info_store_get_or_create(&source_store, source_key, 6, &node) == CFR_STATUS_SUCCESS);
    const Probability old_average[6] = {0, 0, 0, 0, 0, 1}; /* Prefer ordago. */
    CHECK(cfr_info_node_accumulate_strategy(node, old_average, 6, 1) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_node_add_regret(node, 0, 1234) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_node_add_regret(node, 5, -987) == CFR_STATUS_SUCCESS);
    ordago_apply(&old_state, MUS_ACTION_PASS);
    ordago_apply(&old_state, MUS_ACTION_PASS);
    const InfoSetKey unsupported_key = ordago_information_key(&old_state);
    CHECK(cfr_info_store_get_or_create(&source_store, unsupported_key, 6, &node) == CFR_STATUS_SUCCESS);
    CHECK(mus_ordago_game_state_init(&old_state) == CFR_STATUS_SUCCESS);
    CHECK(cfr_trainer_init_mccfr(&source, ordago_game(), game_state(&old_state),
                                 &source_store, 123) == CFR_STATUS_SUCCESS);
    source.training_iterations = 123456;
    const uint64_t original_rng = source.mccfr_rng.state;
    FILE *before = tmpfile(), *after = tmpfile();
    CHECK(before != NULL && after != NULL);
    if (before != NULL)
        CHECK(cfr_checkpoint_write(before, &source) == CFR_STATUS_SUCCESS);
    MusScorePreloadOptions options = {.source = &source, .weight = 2, .uniform_mix = 0.12};
    MusScorePreloadStats stats;
    CHECK(mus_score_preload_ordago(&score, &options, &destination, &stats) == CFR_STATUS_SUCCESS);
    CHECK(stats.source_nodes == 2 && stats.imported_nodes == 1 && stats.existing_nodes == 0);
    CHECK(stats.expanded_action_nodes == 0 && stats.skipped_history_nodes == 1);
    CHECK(cfr_info_store_find(&destination, target_key, &node) == CFR_STATUS_SUCCESS);
    CHECK(node->action_count == 6);
    Probability current[16], average[16];
    CHECK(cfr_info_node_current_strategy(node, current, 16) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_node_average_strategy(node, average, 16) == CFR_STATUS_SUCCESS);
    for (size_t i = 0; i < 6; i += 1) {
        const double expected = i == 5 ? 0.9 : 0.02;
        CHECK(fabs(average[i] - expected) < 1e-12);
        CHECK(fabs(current[i] - expected) < 1e-12);
        CHECK(fabs(node->regret_sums[i] - 2 * expected) < 1e-12);
    }
    /* Existing learning survives a repeated preload, including negative regrets. */
    CHECK(cfr_info_node_add_regret(node, 0, -8) == CFR_STATUS_SUCCESS);
    const double existing_regret = node->regret_sums[0];
    CHECK(mus_score_preload_ordago(&score, &options, &destination, &stats) == CFR_STATUS_SUCCESS);
    CHECK(stats.imported_nodes == 0 && stats.existing_nodes == 1);
    CHECK(node->regret_sums[0] == existing_regret);
    CHECK(source.training_iterations == 123456 && source.mccfr_rng.state == original_rng);
    if (after != NULL)
        CHECK(cfr_checkpoint_write(after, &source) == CFR_STATUS_SUCCESS);
    if (before != NULL && after != NULL)
        CHECK(streams_equal(before, after));
    if (before != NULL) fclose(before);
    if (after != NULL) fclose(after);

    /* A numeric offer and an ordago covering it can both be unreachable. */
    ordago_deal(&old_state, SCORE_HAND_B, SCORE_HAND_A);
    ordago_apply(&old_state, MUS_ACTION_NO_MUS);
    ordago_apply(&old_state, MUS_ACTION_BET_TWO);
    const InfoSetKey numeric_key = ordago_information_key(&old_state);
    CHECK(cfr_info_store_get_or_create(&source_store, numeric_key, 6, &node) == CFR_STATUS_SUCCESS);
    const Probability prefer_ten[6] = {0, 0, 0, 0, 1, 0};
    CHECK(cfr_info_node_accumulate_strategy(node, prefer_ten, 6, 1) == CFR_STATUS_SUCCESS);
    ordago_apply(&old_state, MUS_ACTION_ORDAGO);
    const InfoSetKey covered_key = ordago_information_key(&old_state);
    CHECK(cfr_info_store_get_or_create(&source_store, covered_key, 2, &node) == CFR_STATUS_SUCCESS);

    /* Later lances and out-of-budget histories are excluded separately. */
    CHECK(cfr_info_store_destroy(&destination) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_init(&destination) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_init(&score, &table, 39, 39) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_preload_ordago(&score, &options, &destination, &stats) == CFR_STATUS_SUCCESS);
    CHECK(stats.source_nodes == 4 && stats.imported_nodes == 1 &&
          stats.skipped_history_nodes == 1 && stats.skipped_illegal_nodes == 2);
    CHECK(mus_score_information_set_from_ordago(&score, source_key, 0, 0, &target_key) ==
          CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_find(&destination, target_key, &node) == CFR_STATUS_SUCCESS);
    CHECK(node->action_count == 2);
    CHECK(cfr_info_node_average_strategy(node, average, 16) == CFR_STATUS_SUCCESS);
    CHECK(fabs(average[0] - 0.06) < 1e-12 && fabs(average[1] - 0.94) < 1e-12);
    InfoSetKey untouched = -1;
    CHECK(mus_score_information_set_from_ordago(&score, unsupported_key, 0, 0, &untouched) ==
          CFR_STATUS_NOT_FOUND);
    CHECK(untouched == -1);
    CHECK(mus_score_information_set_from_ordago(&score, numeric_key, 0, 0, &untouched) ==
          CFR_STATUS_NOT_FOUND && untouched == -1);
    CHECK(mus_score_information_set_from_ordago(&score, covered_key, 0, 0, &untouched) ==
          CFR_STATUS_NOT_FOUND && untouched == -1);
    CHECK(cfr_info_store_destroy(&destination) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_init(&destination) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_init(&score, &table, 35, 0) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_preload_ordago(&score, &options, &destination, &stats) == CFR_STATUS_SUCCESS);
    CHECK(stats.imported_nodes == 3 && stats.skipped_history_nodes == 1 &&
          stats.skipped_illegal_nodes == 0);
    CHECK(mus_score_information_set_from_ordago(&score, numeric_key, 0, 0, &target_key) ==
          CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_find(&destination, target_key, &node) == CFR_STATUS_SUCCESS);
    CHECK(node->action_count == 4);
    CHECK(cfr_info_node_average_strategy(node, average, 16) == CFR_STATUS_SUCCESS);
    /* The source puts all mass on an excluded raise: fall back to uniform. */
    for (size_t i = 0; i < 4; i += 1)
        CHECK(fabs(average[i] - 0.25) < 1e-12);
    CHECK(mus_score_information_set_from_ordago(&score, source_key, 29, 0, &untouched) ==
          CFR_STATUS_INVALID_ARGUMENT);
    CHECK(mus_score_preload_ordago(&score, &options, &source_store, NULL) ==
          CFR_STATUS_INVALID_ARGUMENT);
    options.weight = NAN;
    CHECK(mus_score_preload_ordago(&score, &options, &destination, NULL) == CFR_STATUS_INVALID_ARGUMENT);
    options.weight = 1;
    options.uniform_mix = 0;
    CHECK(mus_score_preload_ordago(&score, &options, &destination, NULL) == CFR_STATUS_INVALID_ARGUMENT);
    options.uniform_mix = 0.05;
    source.game = cfr_kuhn_poker_descriptor();
    CHECK(mus_score_preload_ordago(&score, &options, &destination, NULL) == CFR_STATUS_INCOMPATIBLE_GAME);
    source.game = ordago_game();
    Game wrong_schema = *source.game;
    wrong_schema.strategy_schema_id = "old-ordago-schema";
    source.game = &wrong_schema;
    CHECK(mus_score_preload_ordago(&score, &options, &destination, NULL) == CFR_STATUS_INCOMPATIBLE_GAME);
    source.game = ordago_game();
    CHECK(cfr_info_store_destroy(&source_store) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_init(&source_store) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_get_or_create(&source_store, source_key, 3, &node) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_preload_ordago(&score, &options, &destination, NULL) == CFR_STATUS_FORMAT_ERROR);
    CHECK(cfr_info_store_destroy(&source_store) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_destroy(&destination) == CFR_STATUS_SUCCESS);
}

static void seed_legacy_action(InfoStore *store, InfoSetKey key, Action action, double mass) {
    MusOrdagoInformationSet information;
    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS];
    Probability policy[MUS_GAME_MAX_LEGAL_ACTIONS] = {0};
    size_t count = 0;
    CHECK(mus_ordago_game_information_set_decode(key, &information) == CFR_STATUS_SUCCESS);
    CHECK(mus_ordago_game_information_set_legal_actions(&information, actions,
        MUS_GAME_MAX_LEGAL_ACTIONS, &count) == CFR_STATUS_SUCCESS);
    bool found = false;
    for (size_t i = 0; i < count; ++i) {
        if (actions[i] == action) { policy[i] = 1; found = true; }
    }
    CHECK(found);
    InfoNode *node;
    CHECK(cfr_info_store_get_or_create(store, key, count, &node) == CFR_STATUS_SUCCESS);
    Utility regrets[MUS_GAME_MAX_LEGAL_ACTIONS] = {0};
    for (size_t i = 0; i < count; ++i) policy[i] *= mass;
    CHECK(cfr_info_node_apply_deltas(node, regrets, policy, count) == CFR_STATUS_SUCCESS);
}

static void test_score_prior_all_lances(void) {
    MusScoreTable table;
    MusScoreGame score;
    MusGameState old_state, state;
    InfoStore source_store = {0}, destination = {0};
    Trainer source;
    MusScorePrior prior;
    MusScorePreloadStats stats;
    CHECK(mus_score_table_init(&table, 40) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_init(&score, &table, 30, 29) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_init(&source_store) == CFR_STATUS_SUCCESS);
    CHECK(mus_ordago_game_state_init(&old_state) == CFR_STATUS_SUCCESS);
    CHECK(cfr_trainer_init_mccfr(&source, ordago_game(), game_state(&old_state),
        &source_store, 17) == CFR_STATUS_SUCCESS);
    const MusScorePreloadOptions options = {.source = &source, .weight = 2, .uniform_mix = .05};
    bool phases[MUS_PHASE_REDRAW_PLAYER_1 + 1] = {false};
    const uint8_t kings[8] = {1, 0, 0, 0, 0, 0, 0, 3};
    const uint8_t jacks[8] = {1, 0, 0, 0, 0, 3, 0, 0};
    for (int sample = 0; sample < 3; ++sample) {
        CHECK(cfr_info_store_init(&destination) == CFR_STATUS_SUCCESS);
        if (sample == 1)
            CHECK(cfr_info_store_prepare_concurrent(&destination) == CFR_STATUS_SUCCESS);
        CHECK(mus_score_prior_attach(&prior, &score, &options, &destination, &stats) == CFR_STATUS_SUCCESS);
        const uint8_t *a = sample == 0 ? SCORE_HAND_A : kings;
        const uint8_t *b = sample == 0 ? SCORE_HAND_B : jacks;
        ordago_deal(&old_state, a, b);
        ordago_apply(&old_state, MUS_ACTION_NO_MUS);
        score_deal(&score, &state, a, b);
        if (sample == 2) {
            /* Collect 1 in grande and 4 in chica. Pending wagers stay zero. */
            const Action history[] = {MUS_ACTION_BET_TWO, MUS_ACTION_REJECT,
                MUS_ACTION_BET_TWO, MUS_ACTION_BET_TWO, MUS_ACTION_BET_FOUR, MUS_ACTION_REJECT};
            for (size_t i = 0; i < sizeof(history) / sizeof(history[0]); ++i) {
                ordago_apply(&old_state, history[i]);
                score_apply(&score, &state, history[i]);
            }
            CHECK(state.model.phase == MUS_PHASE_PARES && state.model.paid_scores[0] == 5);
        }
        while (state.model.phase != MUS_PHASE_TERMINAL) {
            const InfoSetKey old_key = ordago_information_key(&old_state);
            InfoSetKey key;
            CHECK(cfr_game_information_set_key(&score.game, game_state(&state), &key) == CFR_STATUS_SUCCESS);
            check_legacy_candidate(&score, key, old_key);
            seed_legacy_action(&source_store, old_key, MUS_ACTION_ORDAGO, 1);
            Action actions[MUS_GAME_MAX_LEGAL_ACTIONS];
            size_t count = 0;
            CHECK(cfr_game_legal_actions(&score.game, game_state(&state), actions,
                MUS_GAME_MAX_LEGAL_ACTIONS, &count) == CFR_STATUS_SUCCESS);
            InfoNode *node;
            CHECK(cfr_info_store_get_or_create(&destination, key, count, &node) == CFR_STATUS_SUCCESS);
            Probability average[MUS_GAME_MAX_LEGAL_ACTIONS], current[MUS_GAME_MAX_LEGAL_ACTIONS];
            CHECK(cfr_info_node_average_strategy(node, average, count) == CFR_STATUS_SUCCESS);
            CHECK(cfr_info_node_current_strategy(node, current, count) == CFR_STATUS_SUCCESS);
            for (size_t i = 0; i < count; ++i) {
                const double expected = .05 / (double)count + (actions[i] == MUS_ACTION_ORDAGO ? .95 : 0);
                CHECK(fabs(average[i] - expected) < 1e-12);
                CHECK(fabs(current[i] - expected) < 1e-12);
            }
            const size_t imports = stats.imported_nodes;
            CHECK(cfr_info_node_add_regret(node, 0, -7) == CFR_STATUS_SUCCESS);
            const double regret = node->regret_sums[0];
            CHECK(cfr_info_store_get_or_create(&destination, key, count, &node) == CFR_STATUS_SUCCESS);
            CHECK(node->regret_sums[0] == regret && stats.imported_nodes == imports);
            phases[state.model.phase] = true;
            ordago_apply(&old_state, MUS_ACTION_PASS);
            score_apply(&score, &state, MUS_ACTION_PASS);
        }
        CHECK(stats.imported_later_nodes > 0 && stats.missing_source_nodes == 0);
        CHECK(cfr_info_store_destroy(&destination) == CFR_STATUS_SUCCESS);
    }
    CHECK(phases[MUS_PHASE_GRANDE] && phases[MUS_PHASE_CHICA] && phases[MUS_PHASE_PARES]);
    CHECK(phases[MUS_PHASE_JUEGO] && phases[MUS_PHASE_PUNTO]);
    CHECK(cfr_info_store_destroy(&source_store) == CFR_STATUS_SUCCESS);

    /* Merge hidden old outcomes: after two passes, the old maximum is 1 or 2. */
    CHECK(cfr_info_store_init(&source_store) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_init(&destination) == CFR_STATUS_SUCCESS);
    score_deal(&score, &state, kings, jacks);
    for (int i = 0; i < 4; ++i) score_apply(&score, &state, MUS_ACTION_PASS);
    InfoSetKey key, candidates[41];
    size_t count = 0;
    CHECK(cfr_game_information_set_key(&score.game, game_state(&state), &key) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_information_set_ordago_candidates(&score, key, candidates, 41, &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 2);
    seed_legacy_action(&source_store, candidates[0], MUS_ACTION_PASS, 3);
    seed_legacy_action(&source_store, candidates[1], MUS_ACTION_ORDAGO, 1);
    FILE *before = tmpfile(), *after = tmpfile();
    CHECK(before != NULL && after != NULL);
    if (before != NULL) CHECK(cfr_checkpoint_write(before, &source) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_prior_attach(&prior, &score, &options, &destination, &stats) == CFR_STATUS_SUCCESS);
    InfoNode *node;
    CHECK(cfr_info_store_get_or_create(&destination, key, 5, &node) == CFR_STATUS_SUCCESS);
    Probability average[5];
    CHECK(cfr_info_node_average_strategy(node, average, 5) == CFR_STATUS_SUCCESS);
    CHECK(fabs(average[0] - (.95 * .75 + .01)) < 1e-12);
    CHECK(fabs(average[4] - (.95 * .25 + .01)) < 1e-12);
    if (after != NULL) CHECK(cfr_checkpoint_write(after, &source) == CFR_STATUS_SUCCESS);
    if (before != NULL && after != NULL) CHECK(streams_equal(before, after));
    if (before != NULL) fclose(before);
    if (after != NULL) fclose(after);
    /* Checkpoints retain seeded values, while future insertions need reattachment. */
    Trainer trained, restored;
    MusGameState root;
    CHECK(mus_score_game_state_init(&root, &score) == CFR_STATUS_SUCCESS);
    CHECK(cfr_trainer_init_mccfr(&trained, &score.game, game_state(&root), &destination, 7) == CFR_STATUS_SUCCESS);
    FILE *checkpoint = tmpfile();
    CHECK(checkpoint != NULL);
    if (checkpoint != NULL) {
        CHECK(cfr_checkpoint_write(checkpoint, &trained) == CFR_STATUS_SUCCESS);
        CHECK(cfr_info_store_destroy(&destination) == CFR_STATUS_SUCCESS);
        rewind(checkpoint);
        CHECK(cfr_checkpoint_read(checkpoint, &score.game, game_state(&root),
            &destination, &restored) == CFR_STATUS_SUCCESS);
        CHECK(destination.initializer == NULL);
        CHECK(mus_score_prior_attach(&prior, &score, &options, &destination, &stats) == CFR_STATUS_SUCCESS);
        CHECK(stats.existing_nodes == 1 && stats.imported_nodes == 0);
        CHECK(cfr_info_store_get_or_create(&destination, key, 5, &node) == CFR_STATUS_SUCCESS);
        CHECK(cfr_info_node_average_strategy(node, average, 5) == CFR_STATUS_SUCCESS);
        CHECK(fabs(average[0] - (.95 * .75 + .01)) < 1e-12);
        CHECK(stats.imported_nodes == 0);
        fclose(checkpoint);
    }
    CHECK(cfr_info_store_destroy(&destination) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_destroy(&source_store) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_init(&destination) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_init(&source_store) == CFR_STATUS_SUCCESS);
    seed_legacy_action(&source_store, candidates[0], MUS_ACTION_BET_TEN, 1);
    CHECK(mus_score_prior_attach(&prior, &score, &options, &destination, &stats) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_get_or_create(&destination, key, 5, &node) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_node_average_strategy(node, average, 5) == CFR_STATUS_SUCCESS);
    for (size_t i = 0; i < 5; ++i) CHECK(fabs(average[i] - .2) < 1e-12);
    CHECK(cfr_info_store_destroy(&destination) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_init(&destination) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_destroy(&source_store) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_init(&source_store) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_prior_attach(&prior, &score, &options, &destination, &stats) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_get_or_create(&destination, key, 5, &node) == CFR_STATUS_SUCCESS);
    CHECK(stats.missing_source_nodes == 1 && stats.imported_nodes == 0);
    CHECK(cfr_info_node_average_strategy(node, average, 5) == CFR_STATUS_SUCCESS);
    for (size_t i = 0; i < 5; ++i) CHECK(fabs(average[i] - .2) < 1e-12);
    CHECK(cfr_info_store_destroy(&destination) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_destroy(&source_store) == CFR_STATUS_SUCCESS);
}

static void test_score_prior_paid_bounds(void) {
    MusScoreTable table;
    MusScoreGame score;
    MusGameState state, legacy;
    InfoStore source_store = {0}, destination = {0};
    Trainer source;
    MusScorePrior prior;
    MusScorePreloadStats stats;
    const uint8_t kings[8] = {1, 0, 0, 0, 0, 0, 0, 3};
    const uint8_t jacks[8] = {1, 0, 0, 0, 0, 3, 0, 0};
    CHECK(mus_score_table_init(&table, 40) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_init(&score, &table, 30, 29) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_init(&source_store) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_init(&destination) == CFR_STATUS_SUCCESS);
    CHECK(mus_ordago_game_state_init(&legacy) == CFR_STATUS_SUCCESS);
    CHECK(cfr_trainer_init_mccfr(&source, ordago_game(), game_state(&legacy),
                                &source_store, 7) == CFR_STATUS_SUCCESS);
    ordago_deal(&legacy, kings, jacks);
    ordago_apply(&legacy, MUS_ACTION_NO_MUS);
    score_deal(&score, &state, kings, jacks);
    const Action history[] = {MUS_ACTION_BET_TWO, MUS_ACTION_REJECT,
        MUS_ACTION_PASS, MUS_ACTION_BET_TWO, MUS_ACTION_REJECT};
    for (size_t i = 0; i < sizeof(history) / sizeof(history[0]); ++i) {
        ordago_apply(&legacy, history[i]);
        score_apply(&score, &state, history[i]);
    }
    CHECK(state.model.phase == MUS_PHASE_PARES);
    CHECK(state.model.paid_scores[0] == 1 && state.model.paid_scores[1] == 1);
    CHECK(state.model.prior_wager_total == 0);
    InfoSetKey key, candidates[41];
    size_t count = 0;
    CHECK(cfr_game_information_set_key(&score.game, game_state(&state), &key) ==
          CFR_STATUS_SUCCESS);
    CHECK(mus_score_information_set_ordago_candidates(&score, key, candidates, 41,
                                                      &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 1);
    const InfoSetKey valid_key = ordago_information_key(&legacy);
    CHECK(candidates[0] == valid_key);
    CHECK(mus_score_information_set_ordago_candidates(&score, key, candidates, 1,
                                                      &count) == CFR_STATUS_SUCCESS);
    MusOrdagoInformationSet information;
    CHECK(mus_ordago_game_information_set_decode(valid_key, &information) == CFR_STATUS_SUCCESS);
    CHECK(information.max_settled_score == 1);
    /* Total two with one already paid to each player cannot have maximum two. */
    information.max_settled_score = 2;
    InfoSetKey impossible_key;
    CHECK(mus_ordago_game_information_set_encode(&information, &impossible_key) == CFR_STATUS_SUCCESS);
    seed_legacy_action(&source_store, valid_key, MUS_ACTION_PASS, 1);
    seed_legacy_action(&source_store, impossible_key, MUS_ACTION_ORDAGO, 99);
    const MusScorePreloadOptions options = {.source = &source, .weight = 2, .uniform_mix = .05};
    CHECK(mus_score_prior_attach(&prior, &score, &options, &destination, &stats) == CFR_STATUS_SUCCESS);
    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS];
    CHECK(cfr_game_legal_actions(&score.game, game_state(&state), actions,
                                MUS_GAME_MAX_LEGAL_ACTIONS, &count) == CFR_STATUS_SUCCESS);
    InfoNode *node;
    CHECK(cfr_info_store_get_or_create(&destination, key, count, &node) == CFR_STATUS_SUCCESS);
    Probability average[MUS_GAME_MAX_LEGAL_ACTIONS];
    CHECK(cfr_info_node_average_strategy(node, average, count) == CFR_STATUS_SUCCESS);
    for (size_t i = 0; i < count; ++i) {
        const double expected = .05 / (double)count + (actions[i] == MUS_ACTION_PASS ? .95 : 0);
        CHECK(fabs(average[i] - expected) < 1e-12);
    }
    /* Add a pending pair wager: total 5..7 includes intrinsic pair points.
     * With one public point per player, the maximum can only be 3..6. */
    score_apply(&score, &state, MUS_ACTION_BET_TWO);
    score_apply(&score, &state, MUS_ACTION_ACCEPT);
    CHECK(state.model.phase == MUS_PHASE_JUEGO && state.model.prior_wager_total == 2);
    CHECK(cfr_game_information_set_key(&score.game, game_state(&state), &key) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_information_set_ordago_candidates(&score, key, candidates, 41,
                                                      &count) == CFR_STATUS_SUCCESS);
    CHECK(count == 4);
    for (size_t i = 0; i < count; ++i) {
        CHECK(mus_ordago_game_information_set_decode(candidates[i], &information) == CFR_STATUS_SUCCESS);
        CHECK(information.max_settled_score == 3 + i);
    }
    CHECK(cfr_info_store_destroy(&destination) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_destroy(&source_store) == CFR_STATUS_SUCCESS);
}

static void score_pass_to_end(const MusScoreGame *game, MusGameState *state) {
    size_t count = 0;
    while (state->model.phase != MUS_PHASE_TERMINAL && count < 8) {
        score_apply(game, state, MUS_ACTION_PASS);
        count += 1;
    }
    CHECK(state->model.phase == MUS_PHASE_TERMINAL);
}

static void test_score_utilities_and_order(void) {
    MusScoreTable table;
    MusScoreGame game;
    MusGameState state;
    Utility u0 = 9, u1 = 9;
    CHECK(mus_score_table_init(&table, 40) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_init(&game, &table, 35, 18) == CFR_STATUS_SUCCESS);
    score_deal(&game, &state, SCORE_HAND_A, SCORE_HAND_B);
    score_pass_to_end(&game, &state);
    CHECK(state.model.settled_scores[0] == 1);
    CHECK(state.model.settled_scores[1] == 2);
    CHECK(cfr_game_terminal_utility(&game.game, game_state(&state), 0, &u0) ==
          CFR_STATUS_NOT_FOUND);
    CHECK(u0 == 9);
    table.cells[20][36] = (MusScoreCell){.ready = true, .value = -0.84,
                                       .iterations = 1, .evaluation_samples = 1};
    CHECK(cfr_game_terminal_utility(&game.game, game_state(&state), 0, &u0) ==
          CFR_STATUS_SUCCESS);
    CHECK(cfr_game_terminal_utility(&game.game, game_state(&state), 1, &u1) ==
          CFR_STATUS_SUCCESS);
    CHECK(fabs(u0 - 0.84) < 1e-12 && u0 + u1 == 0);
    table.cells[20][36].value = 2;
    CHECK(mus_score_terminal_utility(&state, 0, &u0) == CFR_STATUS_NUMERIC_ERROR);
    state.model.settled_scores[0] = 0;
    state.model.settled_scores[1] = 0;
    CHECK(mus_score_terminal_utility(&state, 0, &u0) == CFR_STATUS_INVALID_ARGUMENT);

    CHECK(mus_score_game_init(&game, &table, 39, 39) == CFR_STATUS_SUCCESS);
    score_deal(&game, &state, SCORE_HAND_A, SCORE_HAND_B);
    score_apply(&game, &state, MUS_ACTION_PASS);
    MusGameModel before = state.model;
    score_apply(&game, &state, MUS_ACTION_PASS);
    CHECK(state.model.phase == MUS_PHASE_CHICA);
    CHECK(state.model.settled_scores[0] == 0 && state.model.settled_scores[1] == 0);
    CHECK(state.model.pending_lances == 1);
    CHECK(state.model.outcomes[1] == MUS_LANCE_UNRESOLVED);
    CHECK(cfr_game_terminal_utility(&game.game, game_state(&state), 0, &u0) ==
          CFR_STATUS_INVALID_ARGUMENT);
    CHECK(cfr_game_undo_action(&game.game, game_state(&state)) == CFR_STATUS_SUCCESS);
    CHECK(memcmp(&before, &state.model, sizeof(before)) == 0);
    score_apply(&game, &state, MUS_ACTION_PASS);
    score_apply(&game, &state, MUS_ACTION_ORDAGO);
    score_apply(&game, &state, MUS_ACTION_ACCEPT);
    CHECK(cfr_game_terminal_utility(&game.game, game_state(&state), 0, &u0) ==
          CFR_STATUS_SUCCESS && u0 == 1); /* Chica ordago overrides pending grande. */

    /* An accepted ordago replaces all points from earlier lances. */
    CHECK(mus_score_game_init(&game, &table, 35, 18) == CFR_STATUS_SUCCESS);
    score_deal(&game, &state, SCORE_HAND_A, SCORE_HAND_B);
    score_apply(&game, &state, MUS_ACTION_PASS);
    score_apply(&game, &state, MUS_ACTION_PASS);
    score_apply(&game, &state, MUS_ACTION_ORDAGO);
    score_apply(&game, &state, MUS_ACTION_ACCEPT);
    CHECK(state.model.settled_scores[0] == 40 && state.model.settled_scores[1] == 0);
    CHECK(cfr_game_terminal_utility(&game.game, game_state(&state), 0, &u0) ==
          CFR_STATUS_SUCCESS && u0 == 1);

    /* Rejecting an ordago covers the preceding numeric total. */
    score_deal(&game, &state, SCORE_HAND_A, SCORE_HAND_B);
    score_apply(&game, &state, MUS_ACTION_BET_TWO);
    score_apply(&game, &state, MUS_ACTION_ORDAGO);
    score_apply(&game, &state, MUS_ACTION_REJECT);
    CHECK(state.model.phase == MUS_PHASE_CHICA);
    CHECK(state.model.settled_scores[1] == 2);
    CHECK(state.model.prior_wager_total == 0);
    score_pass_to_end(&game, &state);
    table.cells[21][36] = (MusScoreCell){.ready = true, .value = -0.52,
                                       .iterations = 1, .evaluation_samples = 1};
    CHECK(cfr_game_terminal_utility(&game.game, game_state(&state), 0, &u0) ==
          CFR_STATUS_SUCCESS && u0 == 0.52);

    CHECK(mus_score_game_init(&game, &table, 0, 39) == CFR_STATUS_SUCCESS);
    score_deal(&game, &state, SCORE_HAND_A, SCORE_HAND_B);
    score_apply(&game, &state, MUS_ACTION_PASS);
    score_apply(&game, &state, MUS_ACTION_ORDAGO);
    score_apply(&game, &state, MUS_ACTION_REJECT);
    CHECK(state.model.settled_scores[1] == 1);
    CHECK(cfr_game_terminal_utility(&game.game, game_state(&state), 0, &u0) ==
          CFR_STATUS_SUCCESS && u0 == -1);

    /* Skipped pares remain pending while juego or punto is negotiated. */
    const uint8_t pair[8] = {2, 1, 0, 0, 1, 0, 0, 0};
    CHECK(mus_score_game_init(&game, &table, 38, 0) == CFR_STATUS_SUCCESS);
    score_deal(&game, &state, pair, SCORE_HAND_B);
    for (int i = 0; i < 4; i += 1)
        score_apply(&game, &state, MUS_ACTION_PASS);
    CHECK(state.model.phase == MUS_PHASE_PUNTO);
    CHECK(state.model.settled_scores[0] == 0);
    CHECK(state.model.game_revealed && (state.model.pending_lances & 4) != 0);
    CHECK(cfr_game_validate_state(&game.game, game_state(&state)) == CFR_STATUS_SUCCESS);
    score_pass_to_end(&game, &state);
    CHECK(state.model.settled_scores[0] == 2);
}

static void check_score_actions(const MusScoreGame *game, MusGameState *state) {
    InfoSetKey key;
    Action actual[16], decoded[16], trusted[16];
    size_t a, b, c;
    MusOrdagoInformationSet info;
    CHECK(cfr_game_information_set_key(&game->game, game_state(state), &key) ==
          CFR_STATUS_SUCCESS);
    CHECK(key >= 0);
    CHECK(mus_score_game_information_set_decode(key, game->continuation->target, &info) ==
          CFR_STATUS_SUCCESS);
    CHECK(info.score_enabled);
    CHECK(cfr_game_legal_actions(&game->game, game_state(state), actual, 16, &a) ==
          CFR_STATUS_SUCCESS);
    CHECK(mus_ordago_game_information_set_legal_actions(&info, decoded, 16, &b) ==
          CFR_STATUS_SUCCESS);
    CHECK(game->game.trusted_operations->legal_actions(game->game.context,
          game_state(state), trusted, 16, &c) == CFR_STATUS_SUCCESS);
    CHECK(a == b && b == c && memcmp(actual, decoded, a * sizeof(Action)) == 0 &&
          memcmp(actual, trusted, a * sizeof(Action)) == 0);
}

static InfoSetKey score_information_key(const MusScoreGame *game, MusGameState *state) {
    InfoSetKey key = -1;
    CHECK(cfr_game_information_set_key(&game->game, game_state(state), &key) == CFR_STATUS_SUCCESS);
    return key;
}

/* FEMUS IV.5 and V.1: negotiated payments precede the final ordered count. */
static void test_score_payment_timing(void) {
    MusScoreTable table;
    MusScoreGame game;
    MusGameState state;
    Utility value;
    CHECK(mus_score_table_init(&table, 5) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_init(&game, &table, 3, 0) == CFR_STATUS_SUCCESS);
    score_deal(&game, &state, SCORE_HAND_B, SCORE_HAND_A);
    score_apply(&game, &state, MUS_ACTION_BET_FOUR);
    score_apply(&game, &state, MUS_ACTION_ACCEPT);
    CHECK(state.model.phase == MUS_PHASE_CHICA);
    CHECK(state.model.settled_scores[0] == 0 && state.model.pending_lances == 1);
    score_apply(&game, &state, MUS_ACTION_PASS);
    score_apply(&game, &state, MUS_ACTION_ORDAGO);
    score_apply(&game, &state, MUS_ACTION_ACCEPT);
    CHECK(cfr_game_terminal_utility(&game.game, game_state(&state), 0, &value) ==
          CFR_STATUS_SUCCESS && value == -1); /* Postre wins chica despite losing grande. */

    CHECK(mus_score_table_init(&table, 40) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_init(&game, &table, 39, 39) == CFR_STATUS_SUCCESS);
    score_deal(&game, &state, SCORE_HAND_B, SCORE_HAND_A);
    score_apply(&game, &state, MUS_ACTION_PASS);
    score_apply(&game, &state, MUS_ACTION_PASS);
    score_apply(&game, &state, MUS_ACTION_PASS);
    score_apply(&game, &state, MUS_ACTION_ORDAGO);
    const MusGameModel before_deje = state.model;
    score_apply(&game, &state, MUS_ACTION_REJECT);
    CHECK(state.model.phase == MUS_PHASE_TERMINAL && state.model.pending_lances == 0);
    CHECK(state.model.paid_scores[1] == 1 && state.model.settled_scores[0] == 0);
    CHECK(cfr_game_terminal_utility(&game.game, game_state(&state), 0, &value) ==
          CFR_STATUS_SUCCESS && value == -1); /* The deje cancels pending grande. */
    CHECK(cfr_game_undo_action(&game.game, game_state(&state)) == CFR_STATUS_SUCCESS);
    CHECK(memcmp(&before_deje, &state.model, sizeof(before_deje)) == 0);

    score_deal(&game, &state, SCORE_HAND_B, SCORE_HAND_A);
    score_pass_to_end(&game, &state);
    CHECK(state.model.settled_scores[0] == 1 && state.model.settled_scores[1] == 0);
    CHECK(state.model.paid_scores[0] == 0 && state.model.paid_scores[1] == 0);
    CHECK(cfr_game_terminal_utility(&game.game, game_state(&state), 0, &value) ==
          CFR_STATUS_SUCCESS && value == 1); /* Grande is first in the final count. */

    CHECK(mus_score_table_init(&table, 15) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_init(&game, &table, 0, 13) == CFR_STATUS_SUCCESS);
    score_deal(&game, &state, SCORE_HAND_B, SCORE_HAND_A);
    score_apply(&game, &state, MUS_ACTION_BET_TEN);
    score_apply(&game, &state, MUS_ACTION_ACCEPT);
    score_apply(&game, &state, MUS_ACTION_BET_TWO);
    score_apply(&game, &state, MUS_ACTION_ORDAGO);
    score_apply(&game, &state, MUS_ACTION_REJECT);
    CHECK(state.model.phase == MUS_PHASE_TERMINAL && state.model.paid_scores[1] == 2);
    CHECK(state.model.settled_scores[0] == 0);
    CHECK(cfr_game_terminal_utility(&game.game, game_state(&state), 0, &value) ==
          CFR_STATUS_SUCCESS && value == -1); /* Covered two are paid before the pending ten. */

    const uint8_t pair[8] = {2, 1, 1, 0, 0, 0, 0, 0};
    const uint8_t duples[8] = {0, 0, 0, 2, 2, 0, 0, 0};
    CHECK(mus_score_table_init(&table, 40) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_init(&game, &table, 0, 0) == CFR_STATUS_SUCCESS);
    score_deal(&game, &state, pair, duples);
    for (int i = 0; i < 4; i += 1) score_apply(&game, &state, MUS_ACTION_PASS);
    CHECK(state.model.phase == MUS_PHASE_PARES);
    score_apply(&game, &state, MUS_ACTION_BET_TWO);
    score_apply(&game, &state, MUS_ACTION_REJECT);
    CHECK(state.model.phase == MUS_PHASE_PUNTO && state.model.settled_scores[0] == 1);
    CHECK(state.model.paid_scores[0] == 1 && (state.model.pending_lances & 4) != 0);
    score_pass_to_end(&game, &state);
    CHECK(state.model.settled_scores[0] == 3 && state.model.settled_scores[1] == 2);
    CHECK(state.model.paid_scores[0] == 1); /* Pair value is added once, at the end. */
}

static void test_score_public_information(void) {
    MusScoreTable table;
    MusScoreGame game;
    MusGameState states[2];
    const uint8_t low[8] = {4, 0, 0, 0, 0, 0, 0, 0};
    const uint8_t high[8] = {0, 0, 0, 0, 0, 0, 0, 4};
    CHECK(mus_score_table_init(&table, 40) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_init(&game, &table, 0, 0) == CFR_STATUS_SUCCESS);
    /* Own cards and public actions are identical. The winner of grande changes. */
    for (int accepted = 0; accepted < 2; accepted += 1) {
        for (int rival = 0; rival < 2; rival += 1) {
            score_deal(&game, &states[rival], SCORE_HAND_A, rival == 0 ? low : high);
            score_apply(&game, &states[rival], accepted ? MUS_ACTION_BET_TWO : MUS_ACTION_PASS);
            score_apply(&game, &states[rival], accepted ? MUS_ACTION_ACCEPT : MUS_ACTION_PASS);
            CHECK(states[rival].model.phase == MUS_PHASE_CHICA);
            CHECK(states[rival].model.settled_scores[0] == 0 && states[rival].model.settled_scores[1] == 0);
            check_score_actions(&game, &states[rival]);
        }
        CHECK(score_information_key(&game, &states[0]) == score_information_key(&game, &states[1]));
    }
    /* Public pares declarations are equal; the opponent's intrinsic value differs. */
    const uint8_t pair[8] = {0, 1, 1, 0, 0, 0, 0, 2};
    const uint8_t duples[8] = {0, 0, 0, 2, 2, 0, 0, 0};
    for (int rival = 0; rival < 2; rival += 1) {
        score_deal(&game, &states[rival], SCORE_HAND_A, rival == 0 ? pair : duples);
        for (int i = 0; i < 4; i += 1) score_apply(&game, &states[rival], MUS_ACTION_PASS);
        CHECK(states[rival].model.phase == MUS_PHASE_PUNTO);
        CHECK(states[rival].model.settled_scores[1] == 0);
    }
    CHECK(score_information_key(&game, &states[0]) == score_information_key(&game, &states[1]));

    /* The abstraction merges 2+4 and 4+2; the model still pays each lance exactly. */
    const uint8_t low_pair[8] = {2, 1, 1, 0, 0, 0, 0, 0};
    CHECK(mus_score_game_init(&game, &table, 30, 30) == CFR_STATUS_SUCCESS);
    for (int history = 0; history < 2; history += 1) {
        score_deal(&game, &states[history], low_pair, duples);
        score_apply(&game, &states[history], history == 0 ? MUS_ACTION_BET_TWO : MUS_ACTION_BET_FOUR);
        score_apply(&game, &states[history], MUS_ACTION_ACCEPT);
        score_apply(&game, &states[history], history == 0 ? MUS_ACTION_BET_FOUR : MUS_ACTION_BET_TWO);
        score_apply(&game, &states[history], MUS_ACTION_ACCEPT);
        CHECK(states[history].model.phase == MUS_PHASE_PARES);
        CHECK(states[history].model.paid_scores[0] == 0 && states[history].model.paid_scores[1] == 0);
        MusOrdagoInformationSet information;
        CHECK(mus_score_game_information_set_decode(score_information_key(&game, &states[history]),
                  40, &information) == CFR_STATUS_SUCCESS);
        CHECK(information.prior_wager_total == 6);
        CHECK(information.scores[0] == 30 && information.scores[1] == 30);
        CHECK(information.previous_strengths[0] == MUS_LANCE_STRENGTH_BOTH &&
              information.previous_strengths[1] == MUS_LANCE_STRENGTH_BOTH);
        CHECK(states[history].model.outcome_wagers[0] == (history == 0 ? 2 : 4));
        CHECK(states[history].model.outcome_wagers[1] == (history == 0 ? 4 : 2));
        check_score_actions(&game, &states[history]);
    }
    CHECK(score_information_key(&game, &states[0]) == score_information_key(&game, &states[1]));
    for (int history = 0; history < 2; history += 1) {
        score_pass_to_end(&game, &states[history]);
        CHECK(states[history].model.settled_scores[0] == (history == 0 ? 4 : 2));
        CHECK(states[history].model.settled_scores[1] == (history == 0 ? 6 : 8));
    }
}

static void score_check_response_tree(const MusScoreGame *game, MusGameState *state) {
    if (state->model.phase != MUS_PHASE_GRANDE) return;
    check_score_actions(game, state);
    MusOrdagoInformationSet information;
    CHECK(mus_score_game_information_set_decode(score_information_key(game, state), 40,
                                               &information) == CFR_STATUS_SUCCESS);
    CHECK(information.current_wager == state->model.current_wager);
    CHECK(information.previous_wager == state->model.previous_wager);
    CHECK(memcmp(information.used_wager_action_masks, state->model.used_wager_action_masks, 2) == 0);
    Action actions[16];
    size_t count;
    CHECK(cfr_game_legal_actions(&game->game, game_state(state), actions, 16, &count) == CFR_STATUS_SUCCESS);
    const MusGameModel before = state->model;
    for (size_t i = 0; i < count; i += 1) {
        score_apply(game, state, actions[i]);
        CHECK(cfr_game_validate_state(&game->game, game_state(state)) == CFR_STATUS_SUCCESS);
        score_check_response_tree(game, state);
        CHECK(cfr_game_undo_action(&game->game, game_state(state)) == CFR_STATUS_SUCCESS);
        CHECK(memcmp(&before, &state->model, sizeof(before)) == 0);
    }
}

static void test_score_response_codec(void) {
    MusScoreTable table;
    MusScoreGame game;
    MusGameState state;
    CHECK(mus_score_table_init(&table, 40) == CFR_STATUS_SUCCESS);
    const unsigned int scores[][2] = {{0, 0}, {30, 32}, {37, 0}, {39, 39}};
    for (size_t i = 0; i < sizeof(scores) / sizeof(scores[0]); i += 1) {
        CHECK(mus_score_game_init(&game, &table, scores[i][0], scores[i][1]) == CFR_STATUS_SUCCESS);
        score_deal(&game, &state, SCORE_HAND_A, SCORE_HAND_B);
        score_check_response_tree(&game, &state);
    }
    CHECK(mus_score_game_init(&game, &table, 30, 30) == CFR_STATUS_SUCCESS);
    score_deal(&game, &state, SCORE_HAND_A, SCORE_HAND_B);
    score_apply(&game, &state, MUS_ACTION_BET_TWO);
    MusOrdagoInformationSet decoded;
    InfoSetKey original = score_information_key(&game, &state), encoded = -1;
    CHECK(mus_score_game_information_set_decode(original, 40, &decoded) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_information_set_encode(&decoded, 40, &encoded) == CFR_STATUS_SUCCESS);
    CHECK(encoded == original);
    for (int field = 0; field < 9; ++field) {
        MusOrdagoInformationSet invalid = decoded;
        switch (field) {
        case 0: invalid.score_enabled = false; break;
        case 1: invalid.max_settled_score = 29; break;
        case 2: invalid.scores[0] = invalid.max_settled_score = 40; break;
        case 3: invalid.current_wager = 3; break;
        case 4: invalid.previous_wager = 2; break;
        case 5: invalid.used_wager_action_masks[0] = 0; break;
        case 6: invalid.numeric_wager_count = 2; break;
        case 7: invalid.prior_wager_total = 1; break;
        case 8: invalid.current_wager_is_ordago = true; break;
        }
        encoded = -1;
        CHECK(mus_score_game_information_set_encode(&invalid, 40, &encoded) ==
              CFR_STATUS_INVALID_ARGUMENT);
        CHECK(encoded == -1);
    }
    CHECK(mus_score_game_information_set_encode(&decoded, 0, &encoded) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(mus_score_game_information_set_encode(&decoded, 41, &encoded) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(mus_ordago_game_information_set_encode(&decoded, &encoded) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(encoded == -1);
}

/* Expected menus are written independently of the game's action generator. */
static void expect_score_menu(const MusScoreGame *game, MusGameState *state,
                              const Action *expected, size_t expected_count) {
    Action actual[MUS_GAME_MAX_LEGAL_ACTIONS];
    size_t count = 0;
    CHECK(cfr_game_legal_actions(&game->game, game_state(state), actual,
                                  MUS_GAME_MAX_LEGAL_ACTIONS, &count) == CFR_STATUS_SUCCESS);
    CHECK(count == expected_count);
    if (count == expected_count)
        CHECK(memcmp(actual, expected, count * sizeof(*actual)) == 0);
    check_score_actions(game, state);
}

static void test_score_wager_menus_and_settlement(void) {
    static const Action opening[] = {MUS_ACTION_PASS, MUS_ACTION_BET_TWO,
        MUS_ACTION_BET_FOUR, MUS_ACTION_BET_SEVEN, MUS_ACTION_BET_TEN, MUS_ACTION_ORDAGO};
    static const Action opening_four[] = {MUS_ACTION_PASS, MUS_ACTION_BET_TWO,
        MUS_ACTION_BET_FOUR, MUS_ACTION_ORDAGO};
    static const Action opening_two[] = {MUS_ACTION_PASS, MUS_ACTION_BET_TWO, MUS_ACTION_ORDAGO};
    static const Action opening_none[] = {MUS_ACTION_PASS, MUS_ACTION_ORDAGO};
    static const Action response_two[] = {MUS_ACTION_REJECT, MUS_ACTION_ACCEPT,
        MUS_ACTION_BET_TWO, MUS_ACTION_BET_FOUR, MUS_ACTION_BET_TEN, MUS_ACTION_ORDAGO};
    static const Action response_after_two_twice[] = {MUS_ACTION_REJECT,
        MUS_ACTION_ACCEPT, MUS_ACTION_BET_FOUR, MUS_ACTION_BET_TEN, MUS_ACTION_ORDAGO};
    static const Action response_at_limit[] = {
        MUS_ACTION_REJECT, MUS_ACTION_ACCEPT, MUS_ACTION_ORDAGO};
    MusScoreTable table;
    MusScoreGame game;
    MusGameState state;
    CHECK(mus_score_table_init(&table, 40) == CFR_STATUS_SUCCESS);
    const struct {
        unsigned int a, b;
        const Action *actions;
        size_t count;
    } cases[] = {
        {0, 0, opening, 6}, {35, 18, opening_four, 4}, {37, 0, opening_two, 3},
        {38, 19, opening_none, 2}, {19, 38, opening_none, 2}, {39, 39, opening_none, 2},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i += 1) {
        CHECK(mus_score_game_init(&game, &table, cases[i].a, cases[i].b) == CFR_STATUS_SUCCESS);
        score_deal(&game, &state, SCORE_HAND_B, SCORE_HAND_A);
        expect_score_menu(&game, &state, cases[i].actions, cases[i].count);
        score_apply(&game, &state, MUS_ACTION_PASS);
        expect_score_menu(&game, &state, cases[i].actions, cases[i].count);
    }

    /* Pending accepted stakes reduce the budget, without changing collected scores. */
    CHECK(mus_score_game_init(&game, &table, 36, 0) == CFR_STATUS_SUCCESS);
    score_deal(&game, &state, SCORE_HAND_B, SCORE_HAND_A);
    score_apply(&game, &state, MUS_ACTION_BET_TWO);
    expect_score_menu(&game, &state, response_at_limit, 3);
    score_apply(&game, &state, MUS_ACTION_ACCEPT);
    CHECK(state.model.settled_scores[0] == 0 && state.model.prior_wager_total == 2);
    expect_score_menu(&game, &state, opening_none, 2);
    score_apply(&game, &state, MUS_ACTION_PASS);
    score_apply(&game, &state, MUS_ACTION_ORDAGO);
    score_apply(&game, &state, MUS_ACTION_ACCEPT);
    Utility value;
    CHECK(cfr_game_terminal_utility(&game.game, game_state(&state), 0, &value) ==
          CFR_STATUS_SUCCESS && value == -1);

    /* A rejected opening costs one stone, counted once through the public score. */
    score_deal(&game, &state, SCORE_HAND_B, SCORE_HAND_A);
    score_apply(&game, &state, MUS_ACTION_BET_TWO);
    score_apply(&game, &state, MUS_ACTION_REJECT);
    CHECK(state.model.paid_scores[0] == 1 && state.model.prior_wager_total == 0);
    expect_score_menu(&game, &state, opening_two, 3); /* 37 + 2 < 40. */

    /* User example: 29:30, a deje for p0, covered two for p1, accepted pares two. */
    const uint8_t pair[8] = {2, 1, 1, 0, 0, 0, 0, 0};
    const uint8_t duples[8] = {0, 0, 0, 2, 2, 0, 0, 0};
    CHECK(mus_score_game_init(&game, &table, 29, 30) == CFR_STATUS_SUCCESS);
    score_deal(&game, &state, pair, duples);
    score_apply(&game, &state, MUS_ACTION_ORDAGO);
    score_apply(&game, &state, MUS_ACTION_REJECT);
    score_apply(&game, &state, MUS_ACTION_BET_TWO);
    score_apply(&game, &state, MUS_ACTION_ORDAGO);
    score_apply(&game, &state, MUS_ACTION_REJECT);
    CHECK(state.model.phase == MUS_PHASE_PARES);
    score_apply(&game, &state, MUS_ACTION_BET_TWO);
    score_apply(&game, &state, MUS_ACTION_ACCEPT);
    CHECK(state.model.phase == MUS_PHASE_PUNTO);
    CHECK(state.model.paid_scores[0] == 1 && state.model.paid_scores[1] == 2);
    CHECK(state.model.prior_wager_total == 2);
    MusOrdagoInformationSet information;
    CHECK(mus_score_game_information_set_decode(score_information_key(&game, &state), 40,
                                               &information) == CFR_STATUS_SUCCESS);
    CHECK(information.scores[0] == 30 && information.scores[1] == 32);
    CHECK(information.max_settled_score == 32 && information.prior_wager_total == 2);
    expect_score_menu(&game, &state, opening_four, 4);
    score_apply(&game, &state, MUS_ACTION_BET_TWO);
    static const Action example_response[] = {
        MUS_ACTION_REJECT, MUS_ACTION_ACCEPT, MUS_ACTION_BET_TWO, MUS_ACTION_ORDAGO};
    expect_score_menu(&game, &state, example_response, 4);
    const MusGameModel before_six = state.model;
    /* Raising by four proposes a total of six: 32 + 2 + 6 == 40, excluded. */
    CHECK(cfr_game_apply_action(&game.game, game_state(&state), MUS_ACTION_BET_FOUR) ==
          CFR_STATUS_ILLEGAL_ACTION);
    CHECK(memcmp(&state.model, &before_six, sizeof(before_six)) == 0);
    CHECK(game.game.trusted_operations->apply_action(game.game.context, game_state(&state),
                                                      MUS_ACTION_BET_FOUR) == CFR_STATUS_ILLEGAL_ACTION);
    CHECK(memcmp(&state.model, &before_six, sizeof(before_six)) == 0);
    score_apply(&game, &state, MUS_ACTION_BET_TWO); /* Total four: 32 + 2 + 4 == 38. */
    expect_score_menu(&game, &state, response_at_limit, 3);

    /* A budget of 33 allows the same total of six strictly below 40. */
    CHECK(mus_score_game_init(&game, &table, 31, 0) == CFR_STATUS_SUCCESS);
    score_deal(&game, &state, pair, duples);
    score_apply(&game, &state, MUS_ACTION_BET_TWO);
    score_apply(&game, &state, MUS_ACTION_ACCEPT);
    score_apply(&game, &state, MUS_ACTION_BET_TWO);
    static const Action response_six[] = {MUS_ACTION_REJECT, MUS_ACTION_ACCEPT,
        MUS_ACTION_BET_TWO, MUS_ACTION_BET_FOUR, MUS_ACTION_ORDAGO};
    expect_score_menu(&game, &state, response_six, 5);
    score_apply(&game, &state, MUS_ACTION_BET_FOUR);
    CHECK(state.model.current_wager == 6);
    expect_score_menu(&game, &state, response_at_limit, 3);

    /* The per-player increment masks and five-offer limit still apply. */
    CHECK(mus_score_game_init(&game, &table, 0, 0) == CFR_STATUS_SUCCESS);
    score_deal(&game, &state, SCORE_HAND_B, SCORE_HAND_A);
    score_apply(&game, &state, MUS_ACTION_BET_TWO);
    expect_score_menu(&game, &state, response_two, 6);
    score_apply(&game, &state, MUS_ACTION_BET_TWO);
    expect_score_menu(&game, &state, response_after_two_twice, 5);
    score_apply(&game, &state, MUS_ACTION_BET_FOUR);
    score_apply(&game, &state, MUS_ACTION_BET_FOUR);
    score_apply(&game, &state, MUS_ACTION_BET_TEN);
    expect_score_menu(&game, &state, response_at_limit, 3);
    const MusGameModel before = state.model;
    CHECK(cfr_game_apply_action(&game.game, game_state(&state), MUS_ACTION_BET_TEN) ==
          CFR_STATUS_ILLEGAL_ACTION);
    CHECK(memcmp(&state.model, &before, sizeof(before)) == 0);
}

/* Fixed results specify scoring independently of the continuation recursion. */
static void test_score_reduced_match_scoring_cases(void) {
    const uint8_t pair[8] = {2, 1, 0, 0, 1, 0, 0, 0};
    const uint8_t game_31[8] = {1, 0, 0, 0, 0, 1, 1, 1};
    const struct {
        unsigned int target, a, b;
        const uint8_t *hand_a;
        const uint8_t *hand_b;
        unsigned int pass_count;
        uint8_t earned_a, earned_b;
        Utility value;
    } cases[] = {
        {3, 0, 2, SCORE_HAND_A, SCORE_HAND_B, 6, 0, 1, -1}, /* Grande. */
        {3, 2, 0, SCORE_HAND_A, SCORE_HAND_B, 6, 1, 1, 1},  /* Chica. */
        {3, 1, 1, SCORE_HAND_A, SCORE_HAND_B, 6, 1, 2, -1}, /* Punto. */
        {5, 0, 3, SCORE_HAND_A, SCORE_HAND_B, 6, 1, 2, -1},
        {5, 3, 0, pair, SCORE_HAND_B, 6, 2, 1, 1},         /* Solo pares. */
        {5, 1, 0, game_31, SCORE_HAND_B, 4, 4, 1, 1},      /* Solo juego. */
        {3, 2, 2, SCORE_HAND_A, SCORE_HAND_A, 6, 1, 0, 1},  /* Mano wins a tie. */
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i += 1) {
        MusScoreTable table;
        MusScoreGame game;
        MusGameState state;
        CHECK(mus_score_table_init(&table, cases[i].target) == CFR_STATUS_SUCCESS);
        CHECK(mus_score_game_init(&game, &table, cases[i].a, cases[i].b) == CFR_STATUS_SUCCESS);
        score_deal(&game, &state, cases[i].hand_a, cases[i].hand_b);
        for (unsigned int action = 0; action < cases[i].pass_count; action += 1) {
            CHECK(state.model.phase != MUS_PHASE_TERMINAL);
            score_apply(&game, &state, MUS_ACTION_PASS);
        }
        CHECK(state.model.phase == MUS_PHASE_TERMINAL);
        CHECK(state.model.settled_scores[0] == cases[i].earned_a);
        CHECK(state.model.settled_scores[1] == cases[i].earned_b);
        Utility value;
        CHECK(cfr_game_terminal_utility(&game.game, game_state(&state), 0, &value) ==
              CFR_STATUS_SUCCESS && value == cases[i].value);
        CHECK(cfr_game_terminal_utility(&game.game, game_state(&state), 1, &value) ==
              CFR_STATUS_SUCCESS && value == -cases[i].value);
    }
}

static void test_score_export_rejects_foreign_games(void) {
    InfoStore store = {0};
    CHECK(cfr_info_store_init(&store) == CFR_STATUS_SUCCESS);
    FILE *file = tmpfile();
    CHECK(file != NULL);
    if (file == NULL) {
        (void)cfr_info_store_destroy(&store);
        return;
    }
    /* A foreign game's state must not be read as a larger MusGameState. */
    unsigned char *foreign = malloc(1);
    CHECK(foreign != NULL);
    if (foreign != NULL) {
        *foreign = 1;
        Trainer trainer = {.game = cfr_kuhn_poker_descriptor(),
                           .state = (GameState *)foreign, .store = &store};
        CHECK(mus_score_strategy_write_text(file, &trainer) == CFR_STATUS_INVALID_ARGUMENT);
        CHECK(ftell(file) == 0);
        trainer.game = NULL;
        CHECK(mus_score_strategy_write_text(file, &trainer) == CFR_STATUS_INVALID_ARGUMENT);
        free(foreign);
    }
    MusScoreTable table;
    MusScoreGame score;
    MusGameState state;
    Trainer trainer;
    CHECK(!mus_score_game_matches(NULL));
    CHECK(!mus_score_game_matches(ordago_game()));
    CHECK(mus_score_table_init(&table, 3) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_init(&score, &table, 2, 2) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_matches(&score.game));
    CHECK(mus_score_game_state_init(&state, &score) == CFR_STATUS_SUCCESS);
    CHECK(cfr_trainer_init_mccfr(&trainer, &score.game, game_state(&state), &store, 0) ==
          CFR_STATUS_SUCCESS);
    CHECK(mus_score_strategy_write_text(file, &trainer) == CFR_STATUS_SUCCESS);
    CHECK(ftell(file) > 0);
    CHECK(mus_ordago_game_state_init(&state) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_strategy_write_text(file, &trainer) == CFR_STATUS_INVALID_ARGUMENT);
    fclose(file);
    CHECK(cfr_info_store_destroy(&store) == CFR_STATUS_SUCCESS);
}

static double score_full_match(unsigned int target, unsigned int a, unsigned int b);

/* Exhaustive subgame: fixed hands, no mus, opening two or ordago, no numeric raises. */
static double score_exhaustive_hand(const MusScoreGame *game, MusGameState *state,
                                    bool continue_full_match) {
    if (state->model.phase == MUS_PHASE_TERMINAL) {
        if (continue_full_match) {
            const unsigned int target = state->model.score_target;
            const unsigned int a = state->model.initial_scores[0] + state->model.settled_scores[0];
            const unsigned int b = state->model.initial_scores[1] + state->model.settled_scores[1];
            if (a >= target)
                return 1;
            if (b >= target)
                return -1;
            return -score_full_match(target, b, a);
        }
        Utility value = 10;
        CHECK(cfr_game_terminal_utility(&game->game, game_state(state), 0, &value) ==
              CFR_STATUS_SUCCESS);
        return value;
    }
    check_score_actions(game, state);
    Action actions[16];
    size_t count;
    CHECK(cfr_game_legal_actions(&game->game, game_state(state), actions, 16, &count) ==
          CFR_STATUS_SUCCESS);
    const bool maximize = state->model.current_player == 0;
    double best = maximize ? -2 : 2;
    MusGameModel before = state->model;
    const size_t undo_count = state->undo_count;
    for (size_t i = 0; i < count; i += 1) {
        if (actions[i] == MUS_ACTION_BET_FOUR || actions[i] == MUS_ACTION_BET_SEVEN ||
            actions[i] == MUS_ACTION_BET_TEN ||
            (actions[i] == MUS_ACTION_BET_TWO && state->model.bet_stage == MUS_BET_RESPONSE))
            continue;
        CHECK(game->game.trusted_operations->apply_action(game->game.context,
              game_state(state), actions[i]) == CFR_STATUS_SUCCESS);
        CHECK(cfr_game_validate_state(&game->game, game_state(state)) == CFR_STATUS_SUCCESS);
        const double value = score_exhaustive_hand(game, state, continue_full_match);
        if ((maximize && value > best) || (!maximize && value < best))
            best = value;
        CHECK(game->game.trusted_operations->undo_action(game->game.context,
              game_state(state)) == CFR_STATUS_SUCCESS);
        CHECK(memcmp(&before, &state->model, sizeof(before)) == 0);
        CHECK(state->undo_count == undo_count);
    }
    return best;
}

static double score_full_match(unsigned int target, unsigned int a, unsigned int b) {
    MusScoreTable table;
    MusScoreGame game;
    MusGameState state;
    CHECK(mus_score_table_init(&table, target) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_init(&game, &table, a, b) == CFR_STATUS_SUCCESS);
    score_deal(&game, &state, SCORE_HAND_A, SCORE_HAND_B);
    return score_exhaustive_hand(&game, &state, true);
}

static void test_score_keys_and_full_match(void) {
    MusScoreTable table;
    MusScoreGame game, other;
    MusGameState state;
    InfoSetKey key_a, key_b;
    Action actions[16];
    size_t count;
    CHECK(mus_score_table_init(&table, 40) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_init(&game, &table, 37, 18) == CFR_STATUS_SUCCESS);
    score_deal(&game, &state, SCORE_HAND_A, SCORE_HAND_B);
    check_score_actions(&game, &state);
    CHECK(cfr_game_information_set_key(&game.game, game_state(&state), &key_a) ==
          CFR_STATUS_SUCCESS);
    CHECK(cfr_game_legal_actions(&game.game, game_state(&state), actions, 16, &count) ==
          CFR_STATUS_SUCCESS);
    CHECK(count == 3 && actions[1] == MUS_ACTION_BET_TWO);
    CHECK(mus_score_game_init(&other, &table, 37, 19) == CFR_STATUS_SUCCESS);
    score_deal(&other, &state, SCORE_HAND_A, SCORE_HAND_B);
    CHECK(cfr_game_information_set_key(&other.game, game_state(&state), &key_b) ==
          CFR_STATUS_SUCCESS);
    CHECK(key_a != key_b && strcmp(game.schema, other.schema) != 0);
    CHECK(mus_score_game_init(&other, &table, 38, 19) == CFR_STATUS_SUCCESS);
    score_deal(&other, &state, SCORE_HAND_A, SCORE_HAND_B);
    CHECK(cfr_game_legal_actions(&other.game, game_state(&state), actions, 16, &count) ==
          CFR_STATUS_SUCCESS);
    CHECK(count == 2 && actions[1] == MUS_ACTION_ORDAGO);
    CHECK(cfr_game_validate_state(ordago_game(), game_state(&state)) ==
          CFR_STATUS_INVALID_ARGUMENT);
    MusGameModel before = state.model;
    CHECK(cfr_game_apply_action(&other.game, game_state(&state), MUS_ACTION_ACCEPT) ==
          CFR_STATUS_ILLEGAL_ACTION);
    CHECK(memcmp(&before, &state.model, sizeof(before)) == 0);

    CHECK(mus_score_table_init(&table, 3) == CFR_STATUS_SUCCESS);
    for (int sum = 4; sum >= 0; sum -= 1) {
        for (int a = 0; a < 3; a += 1) {
            const int b = sum - a;
            if (b < 0 || b >= 3)
                continue;
            CHECK(mus_score_game_init(&game, &table, (unsigned int)a, (unsigned int)b) ==
                  CFR_STATUS_SUCCESS);
            score_deal(&game, &state, SCORE_HAND_A, SCORE_HAND_B);
            const double value = score_exhaustive_hand(&game, &state, false);
            const double full = score_full_match(3, (unsigned int)a, (unsigned int)b);
            CHECK(fabs(value - full) < 1e-12);
            table.cells[a][b] = (MusScoreCell){.ready = true, .value = value,
                                             .iterations = 1, .evaluation_samples = 1};
        }
    }
}

typedef struct {
    unsigned int cells;
    unsigned int diagonals;
    FILE *checkpoint;
    char schema[256];
} ScoreTestSave;

static Status test_score_save_cell(const MusScoreGame *game, const Trainer *trainer,
                                   const MusScoreCell *cell, void *context) {
    ScoreTestSave *saved = context;
    const unsigned int a = game->initial_scores[0], b = game->initial_scores[1];
    const MusScoreTable *table = game->continuation;
    for (unsigned int x = 0; x < table->target; x += 1) {
        for (unsigned int y = 0; y < table->target; y += 1) {
            if (x + y == a + b)
                CHECK(!table->cells[x][y].ready);
            if (x + y > a + b)
                CHECK(table->cells[x][y].ready);
        }
    }
    CHECK(cell->value >= -1 && cell->value <= 1);
    CHECK(trainer->training_iterations == 2);
    const MusGameState *state = (const MusGameState *)trainer->state;
    CHECK(state->undo_count == 0 && state->model.phase == MUS_PHASE_DEAL_PLAYER_0);
    double evaluated = 10;
    const uint64_t seed = 7U + a * MUS_ORDAGO_WAGER + b;
    CHECK(mus_score_evaluate(game, trainer->store, 50,
          seed ^ UINT64_C(0xd1b54a32d192ed03), &evaluated) == CFR_STATUS_SUCCESS);
    CHECK(evaluated == cell->value);
    FILE *text = tmpfile();
    CHECK(text != NULL);
    if (text != NULL) {
        CHECK(mus_score_strategy_write_text(text, trainer) == CFR_STATUS_SUCCESS);
        fclose(text);
    }
    if (a == 1 && b == 1) {
        saved->checkpoint = tmpfile();
        CHECK(saved->checkpoint != NULL);
        if (saved->checkpoint != NULL)
            CHECK(cfr_checkpoint_write(saved->checkpoint, trainer) == CFR_STATUS_SUCCESS);
        memcpy(saved->schema, game->schema, sizeof(saved->schema));
    }
    saved->cells += 1;
    return CFR_STATUS_SUCCESS;
}

static Status test_score_save_diagonal(const MusScoreTable *table, void *context) {
    ScoreTestSave *saved = context;
    FILE *file = tmpfile();
    MusScoreTable loaded;
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(mus_score_table_write(file, table) == CFR_STATUS_SUCCESS);
        rewind(file);
        CHECK(mus_score_table_read(file, &loaded) == CFR_STATUS_SUCCESS);
        CHECK(loaded.target == table->target);
        for (int a = 0; a < table->target; a += 1) {
            for (int b = 0; b < table->target; b += 1) {
                CHECK(loaded.cells[a][b].ready == table->cells[a][b].ready);
                CHECK(loaded.cells[a][b].value == table->cells[a][b].value);
                CHECK(loaded.cells[a][b].training_seed == table->cells[a][b].training_seed);
            }
        }
        fclose(file);
    }
    saved->diagonals += 1;
    return CFR_STATUS_SUCCESS;
}

static Status fail_score_cell(const MusScoreGame *game, const Trainer *trainer,
                              const MusScoreCell *cell, void *context) {
    (void)trainer;
    (void)cell;
    (void)context;
    return game->initial_scores[0] == 2 && game->initial_scores[1] == 1
               ? CFR_STATUS_IO_ERROR : CFR_STATUS_SUCCESS;
}

typedef struct {
    FILE *checkpoints[3][3];
    pthread_t caller;
    size_t cells;
    size_t diagonals;
    bool compare;
} ScoreParallelSave;

static Status test_parallel_score_cell(const MusScoreGame *game,
                                       const Trainer *trainer,
                                       const MusScoreCell *cell, void *context) {
    static const unsigned int order[9][2] = {
        {2, 2}, {1, 2}, {2, 1}, {0, 2}, {1, 1},
        {2, 0}, {0, 1}, {1, 0}, {0, 0},
    };
    ScoreParallelSave *saved = context;
    const unsigned int a = game->initial_scores[0];
    const unsigned int b = game->initial_scores[1];
    CHECK(pthread_equal(pthread_self(), saved->caller));
    CHECK(saved->cells < 9);
    if (saved->cells < 9) {
        CHECK(a == order[saved->cells][0]);
        CHECK(b == order[saved->cells][1]);
    }
    for (unsigned int x = 0; x < 3; x += 1) {
        for (unsigned int y = 0; y < 3; y += 1)
            CHECK(game->continuation->cells[x][y].ready == (x + y > a + b));
    }
    CHECK(cell->training_seed == 7 + a * MUS_ORDAGO_WAGER + b);
    FILE *checkpoint = tmpfile();
    CHECK(checkpoint != NULL);
    if (checkpoint != NULL) {
        CHECK(cfr_checkpoint_write(checkpoint, trainer) == CFR_STATUS_SUCCESS);
        if (saved->compare) {
            CHECK(saved->checkpoints[a][b] != NULL);
            if (saved->checkpoints[a][b] != NULL)
                CHECK(streams_equal(saved->checkpoints[a][b], checkpoint));
            fclose(checkpoint);
        } else {
            saved->checkpoints[a][b] = checkpoint;
        }
    }
    saved->cells += 1;
    return CFR_STATUS_SUCCESS;
}

static Status test_parallel_score_diagonal(const MusScoreTable *table,
                                           void *context) {
    ScoreParallelSave *saved = context;
    CHECK(pthread_equal(pthread_self(), saved->caller));
    size_t ready = 0;
    for (unsigned int a = 0; a < table->target; a += 1) {
        for (unsigned int b = 0; b < table->target; b += 1)
            ready += table->cells[a][b].ready ? 1U : 0U;
    }
    CHECK(ready == saved->cells);
    saved->diagonals += 1;
    return CFR_STATUS_SUCCESS;
}

static void test_score_parallel_training(void) {
    MusScoreTable serial;
    MusScoreTable parallel;
    MusScoreTrainingOptions options = {
        .iterations = 2, .evaluation_samples = 50, .seed = 7,
    };
    ScoreParallelSave saved = {.caller = pthread_self()};
    CHECK(mus_score_table_init(&serial, 3) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_train(&serial, 0, &options, test_parallel_score_cell,
                          test_parallel_score_diagonal, &saved) == CFR_STATUS_SUCCESS);
    CHECK(saved.cells == 9 && saved.diagonals == 5);
    const size_t thread_counts[] = {1, 2, SIZE_MAX};
    for (size_t i = 0; i < sizeof(thread_counts) / sizeof(thread_counts[0]); i += 1) {
        saved.cells = 0;
        saved.diagonals = 0;
        saved.compare = true;
        options.threads = thread_counts[i];
        options.threads_per_cell = 1;
        CHECK(mus_score_table_init(&parallel, 3) == CFR_STATUS_SUCCESS);
        CHECK(mus_score_train(&parallel, 2, &options, test_parallel_score_cell,
                              test_parallel_score_diagonal, &saved) == CFR_STATUS_SUCCESS);
        CHECK(saved.cells == 6 && saved.diagonals == 3);
        CHECK(mus_score_train(&parallel, 0, &options, test_parallel_score_cell,
                              test_parallel_score_diagonal, &saved) == CFR_STATUS_SUCCESS);
        CHECK(saved.cells == 9 && saved.diagonals == 5);
        FILE *left = tmpfile();
        FILE *right = tmpfile();
        CHECK(left != NULL && right != NULL);
        if (left != NULL && right != NULL) {
            CHECK(mus_score_table_write(left, &serial) == CFR_STATUS_SUCCESS);
            CHECK(mus_score_table_write(right, &parallel) == CFR_STATUS_SUCCESS);
            CHECK(streams_equal(left, right));
        }
        if (left != NULL)
            fclose(left);
        if (right != NULL)
            fclose(right);
        CHECK(mus_score_table_init(&parallel, 3) == CFR_STATUS_SUCCESS);
        CHECK(mus_score_train(&parallel, 0, &options, fail_score_cell,
                              NULL, NULL) == CFR_STATUS_IO_ERROR);
        CHECK(parallel.cells[2][2].ready);
        CHECK(!parallel.cells[1][2].ready && !parallel.cells[2][1].ready);
    }
    for (size_t a = 0; a < 3; a += 1) {
        for (size_t b = 0; b < 3; b += 1) {
            if (saved.checkpoints[a][b] != NULL)
                fclose(saved.checkpoints[a][b]);
        }
    }
}

static Status test_shared_score_cell(const MusScoreGame *score, const Trainer *trainer,
                                     const MusScoreCell *cell, void *context) {
    const MusGameState *state = (const MusGameState *)trainer->state;
    CHECK(cell->iterations == 3 && trainer->training_iterations == 3);
    CHECK(trainer->stats.iterations == 3 && trainer->stats.traversals == 6);
    CHECK(trainer->stats.visited_nodes > 0 && trainer->stats.errors == 0);
    CHECK(state->undo_count == 0 && state->model.phase == MUS_PHASE_DEAL_PLAYER_0);
    CHECK(state->continuation == score->continuation);
    CHECK(cfr_game_validate_state(&score->game, trainer->state) == CFR_STATUS_SUCCESS);
    double value = 10;
    CHECK(mus_score_evaluate(score, trainer->store, cell->evaluation_samples,
                            cell->training_seed ^ UINT64_C(0xd1b54a32d192ed03),
                            &value) == CFR_STATUS_SUCCESS);
    CHECK(value == cell->value);
    return test_parallel_score_cell(score, trainer, cell, context);
}

static void test_score_shared_strategy_training(void) {
    MusScoreTable table;
    MusScoreTrainingOptions options = {
        .iterations = 3, .evaluation_samples = 50, .seed = 7,
        .threads = 2, .threads_per_cell = 2,
    };
    ScoreParallelSave saved = {.caller = pthread_self()};
    CHECK(mus_score_table_init(&table, 3) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_train(&table, 2, &options, test_shared_score_cell,
                          test_parallel_score_diagonal, &saved) == CFR_STATUS_SUCCESS);
    CHECK(saved.cells == 6 && saved.diagonals == 3);
    options.threads = SIZE_MAX;
    options.threads_per_cell = SIZE_MAX;
    CHECK(mus_score_train(&table, 0, &options, test_shared_score_cell,
                          test_parallel_score_diagonal, &saved) == CFR_STATUS_SUCCESS);
    CHECK(saved.cells == 9 && saved.diagonals == 5);
    CHECK(mus_score_train(&table, 0, &options, test_shared_score_cell,
                          test_parallel_score_diagonal, &saved) == CFR_STATUS_SUCCESS);
    CHECK(saved.cells == 9 && saved.diagonals == 5);
    for (unsigned int a = 0; a < 3; a += 1) {
        for (unsigned int b = 0; b < 3; b += 1) {
            MusScoreGame score;
            MusGameState state;
            InfoStore store = {0};
            Trainer restored;
            CHECK(table.cells[a][b].ready && table.cells[a][b].iterations == 3);
            CHECK(mus_score_game_init(&score, &table, a, b) == CFR_STATUS_SUCCESS);
            CHECK(mus_score_game_state_init(&state, &score) == CFR_STATUS_SUCCESS);
            CHECK(saved.checkpoints[a][b] != NULL);
            if (saved.checkpoints[a][b] != NULL) {
                rewind(saved.checkpoints[a][b]);
                const Status status = cfr_checkpoint_read(
                    saved.checkpoints[a][b], &score.game, game_state(&state), &store, &restored);
                CHECK(status == CFR_STATUS_SUCCESS);
                if (status == CFR_STATUS_SUCCESS) {
                    CHECK(restored.training_iterations == 3);
                    CHECK(restored.stats.iterations == 3 && restored.stats.traversals == 6);
                    CHECK(cfr_info_store_destroy(&store) == CFR_STATUS_SUCCESS);
                }
                fclose(saved.checkpoints[a][b]);
            }
        }
    }
}

typedef struct {
    ScoreParallelSave source;
    ScoreParallelSave destination;
    const MusScoreTable *source_table;
    const MusScoreTable *destination_table;
    size_t loads;
    size_t reports;
} ScoreExtensionTest;

static Status load_test_score_extension(const MusScoreGame *game, MusGameState *state,
    InfoStore *store, Trainer *trainer, void *context) {
    ScoreExtensionTest *test = context;
    const unsigned int a = game->initial_scores[0], b = game->initial_scores[1];
    CHECK(game->continuation == test->source_table);
    CHECK(state->continuation == test->source_table);
    FILE *file = test->source.checkpoints[a][b];
    if (file == NULL) return CFR_STATUS_NOT_FOUND;
    rewind(file);
    Status status = cfr_checkpoint_read(file, &game->game, game_state(state), store, trainer);
    if (status == CFR_STATUS_SUCCESS) {
        CHECK(trainer->training_iterations == 2);
        test->loads += 1;
    }
    return status;
}

static Status report_test_score_extension(const MusScoreGame *game, const Trainer *trainer,
    size_t completed, const MusScorePreloadStats *stats, void *context) {
    ScoreExtensionTest *test = context;
    (void)stats;
    CHECK(game->continuation == test->destination_table);
    CHECK(((const MusGameState *)trainer->state)->continuation == test->destination_table);
    CHECK(trainer->game == &game->game);
    CHECK(trainer->training_iterations == 2 + completed);
    CHECK(trainer->stats.iterations == 2 + completed);
    CHECK(trainer->stats.traversals == 2 * (2 + completed));
    test->reports += 1;
    return CFR_STATUS_SUCCESS;
}

static void test_score_extension(void) {
    MusScoreTable source, destination;
    ScoreExtensionTest test = {.source = {.caller = pthread_self()},
        .destination = {.caller = pthread_self()}, .source_table = &source,
        .destination_table = &destination};
    CHECK(mus_score_table_init(&source, 3) == CFR_STATUS_SUCCESS);
    MusScoreTrainingOptions options = {.iterations = 2, .evaluation_samples = 50, .seed = 7};
    CHECK(mus_score_train(&source, 0, &options, test_parallel_score_cell,
        test_parallel_score_diagonal, &test.source) == CFR_STATUS_SUCCESS);
    MusScoreTable original = source;
    const MusScoreExtension extension = {.table = &source,
        .load_cell = load_test_score_extension, .context = &test};
    options = (MusScoreTrainingOptions){.iterations = 3, .evaluation_samples = 1,
        .extension = &extension, .report_every = 1, .progress = report_test_score_extension,
        .progress_context = &test};
    CHECK(mus_score_table_init(&destination, 3) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_train(&destination, 2, &options, test_parallel_score_cell,
        test_parallel_score_diagonal, &test.destination) == CFR_STATUS_SUCCESS);
    CHECK(test.loads == 6 && test.reports == 18);
    CHECK(mus_score_train(&destination, 0, &options, test_parallel_score_cell,
        test_parallel_score_diagonal, &test.destination) == CFR_STATUS_SUCCESS);
    CHECK(test.loads == 9 && test.reports == 27 && test.destination.cells == 9);
    CHECK(mus_score_train(&destination, 0, &options, NULL, NULL, NULL) == CFR_STATUS_SUCCESS);
    CHECK(test.loads == 9);
    CHECK(memcmp(&source, &original, sizeof(source)) == 0);
    size_t changed_schemas = 0;
    for (unsigned int a = 0; a < 3; ++a) {
        for (unsigned int b = 0; b < 3; ++b) {
            CHECK(destination.cells[a][b].iterations == 5);
            MusScoreGame old_game, new_game;
            MusGameState state;
            InfoStore store = {0};
            Trainer trainer;
            CHECK(mus_score_game_init(&old_game, &source, a, b) == CFR_STATUS_SUCCESS);
            CHECK(mus_score_game_init(&new_game, &destination, a, b) == CFR_STATUS_SUCCESS);
            CHECK(mus_score_game_state_init(&state, &new_game) == CFR_STATUS_SUCCESS);
            FILE *file = test.destination.checkpoints[a][b];
            rewind(file);
            CHECK(cfr_checkpoint_read(file, &new_game.game, game_state(&state), &store, &trainer) == CFR_STATUS_SUCCESS);
            CHECK(trainer.training_iterations == 5 && trainer.stats.iterations == 5);
            CHECK(cfr_info_store_destroy(&store) == CFR_STATUS_SUCCESS);
            if (strcmp(old_game.schema, new_game.schema) != 0) {
                ++changed_schemas;
                rewind(file);
                CHECK(cfr_checkpoint_read(file, &old_game.game, game_state(&state), &store, &trainer) == CFR_STATUS_INCOMPATIBLE_GAME);
            }
        }
    }
    CHECK(changed_schemas > 0);
    /* Top cell: unchanged continuation, so serial extension must equal exact
     * checkpoint resume, including arrays, RNG and all training counters. */
    MusScoreGame top;
    MusGameState state;
    InfoStore store = {0};
    Trainer trainer;
    CHECK(mus_score_game_init(&top, &source, 2, 2) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_state_init(&state, &top) == CFR_STATUS_SUCCESS);
    rewind(test.source.checkpoints[2][2]);
    CHECK(cfr_checkpoint_read(test.source.checkpoints[2][2], &top.game, game_state(&state), &store, &trainer) == CFR_STATUS_SUCCESS);
    CHECK(cfr_trainer_run(&trainer, 3) == CFR_STATUS_SUCCESS);
    FILE *reference = tmpfile();
    CHECK(reference != NULL);
    if (reference != NULL) {
        CHECK(cfr_checkpoint_write(reference, &trainer) == CFR_STATUS_SUCCESS);
        CHECK(streams_equal(reference, test.destination.checkpoints[2][2]));
        fclose(reference);
    }
    CHECK(cfr_info_store_destroy(&store) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_train(&source, 0, &options, NULL, NULL, NULL) == CFR_STATUS_INVALID_ARGUMENT);
    options.iterations = 4;
    CHECK(mus_score_train(&destination, 0, &options, NULL, NULL, NULL) == CFR_STATUS_INCOMPATIBLE_GAME);
    options.iterations = 3;
    CHECK(mus_score_table_init(&destination, 3) == CFR_STATUS_SUCCESS);
    source.cells[0][0].ready = false;
    CHECK(mus_score_train(&destination, 0, &options, NULL, NULL, NULL) == CFR_STATUS_NOT_FOUND);
    source = original;
    source.cells[0][0].iterations = SIZE_MAX;
    CHECK(mus_score_train(&destination, 0, &options, NULL, NULL, NULL) == CFR_STATUS_INVALID_ARGUMENT);
    source = original;
    source.cells[2][2].value = source.cells[2][2].value == 1 ? -1 : 1;
    CHECK(mus_score_train(&destination, 0, &options, NULL, NULL, NULL) == CFR_STATUS_INCOMPATIBLE_GAME);
    CHECK(destination.cells[2][2].ready && !destination.cells[1][2].ready);
    for (unsigned int a = 0; a < 3; ++a) {
        for (unsigned int b = 0; b < 3; ++b) {
            fclose(test.source.checkpoints[a][b]);
            fclose(test.destination.checkpoints[a][b]);
        }
    }
}

typedef struct {
    size_t cells[3][3];
    size_t diagonals;
    bool fail;
} ScoreRetrySave;

static Status count_retry_score_cell(const MusScoreGame *game, const Trainer *trainer,
                                     const MusScoreCell *cell, void *context) {
    (void)trainer;
    (void)cell;
    ScoreRetrySave *saved = context;
    const unsigned int a = game->initial_scores[0], b = game->initial_scores[1];
    CHECK(!game->continuation->cells[a][b].ready);
    saved->cells[a][b] += 1;
    return CFR_STATUS_SUCCESS;
}

static Status retry_score_diagonal(const MusScoreTable *table, void *context) {
    ScoreRetrySave *saved = context;
    saved->diagonals += 1;
    CHECK(table->cells[2][2].ready);
    if (saved->fail) {
        CHECK(table->cells[1][2].ready && table->cells[2][1].ready);
        saved->fail = false;
        return CFR_STATUS_IO_ERROR;
    }
    return CFR_STATUS_SUCCESS;
}

static void test_score_diagonal_retry(void) {
    for (size_t threads = 1; threads <= 2; ++threads) {
        MusScoreTable table, expected;
        ScoreRetrySave saved = {0};
        const MusScoreTrainingOptions options = {
            .iterations = 2, .evaluation_samples = 10, .seed = 7, .threads = threads,
        };
        CHECK(mus_score_table_init(&table, 3) == CFR_STATUS_SUCCESS);
        CHECK(mus_score_train(&table, 4, &options, count_retry_score_cell,
                              retry_score_diagonal, &saved) == CFR_STATUS_SUCCESS);
        /* Unready metadata is not validated, but must survive a failed save. */
        table.cells[1][2].training_seed = 123;
        table.cells[2][1].value = 0.25;
        const MusScoreCell top = table.cells[2][2];
        saved.fail = true;
        CHECK(mus_score_train(&table, 3, &options, count_retry_score_cell,
                              retry_score_diagonal, &saved) == CFR_STATUS_IO_ERROR);
        CHECK(saved.diagonals == 2);
        CHECK(!table.cells[1][2].ready && !table.cells[2][1].ready);
        CHECK(table.cells[1][2].training_seed == 123);
        CHECK(table.cells[2][1].value == 0.25);
        CHECK(table.cells[2][2].ready && table.cells[2][2].value == top.value);
        CHECK(table.cells[2][2].training_seed == top.training_seed);
        CHECK(!table.cells[0][2].ready && !table.cells[1][1].ready);
        CHECK(mus_score_train(&table, 3, &options, count_retry_score_cell,
                              retry_score_diagonal, &saved) == CFR_STATUS_SUCCESS);
        CHECK(saved.diagonals == 3);
        CHECK(saved.cells[2][2] == 1);
        CHECK(saved.cells[1][2] == 2 && saved.cells[2][1] == 2);
        CHECK(mus_score_train(&table, 3, &options, count_retry_score_cell,
                              retry_score_diagonal, &saved) == CFR_STATUS_SUCCESS);
        CHECK(saved.diagonals == 3);
        CHECK(mus_score_train(&table, 0, &options, count_retry_score_cell,
                              retry_score_diagonal, &saved) == CFR_STATUS_SUCCESS);
        CHECK(mus_score_table_init(&expected, 3) == CFR_STATUS_SUCCESS);
        CHECK(mus_score_train(&expected, 0, &options, NULL, NULL, NULL) ==
              CFR_STATUS_SUCCESS);
        FILE *left = tmpfile(), *right = tmpfile();
        CHECK(left != NULL && right != NULL);
        if (left != NULL && right != NULL) {
            CHECK(mus_score_table_write(left, &table) == CFR_STATUS_SUCCESS);
            CHECK(mus_score_table_write(right, &expected) == CFR_STATUS_SUCCESS);
            CHECK(streams_equal(left, right));
        }
        if (left != NULL)
            fclose(left);
        if (right != NULL)
            fclose(right);
    }
}

typedef struct {
    MusScoreCell saved[3][3];
    char schemas[3][3][256];
    size_t saves[3][3];
    size_t recoveries[3][3];
    bool fail;
    bool invalid;
    Status recovery_error;
} ScoreRecoveryTest;

static Status save_recovery_test_cell(const MusScoreGame *game, const Trainer *trainer,
    const MusScoreCell *cell, void *context) {
    (void)trainer;
    ScoreRecoveryTest *test = context;
    const unsigned int a = game->initial_scores[0], b = game->initial_scores[1];
    if (test->fail && a == 2 && b == 1) return CFR_STATUS_IO_ERROR;
    test->saves[a][b]++;
    test->saved[a][b] = *cell;
    strcpy(test->schemas[a][b], game->schema);
    return CFR_STATUS_SUCCESS;
}

static Status recover_test_cell(const MusScoreGame *game, MusScoreCell *cell, void *context) {
    ScoreRecoveryTest *test = context;
    const unsigned int a = game->initial_scores[0], b = game->initial_scores[1];
    if (test->recovery_error != CFR_STATUS_SUCCESS) return test->recovery_error;
    if (!test->saved[a][b].ready || strcmp(test->schemas[a][b], game->schema) != 0)
        return CFR_STATUS_NOT_FOUND;
    test->recoveries[a][b]++;
    *cell = test->saved[a][b];
    if (test->invalid) cell->iterations++;
    return CFR_STATUS_SUCCESS;
}

static void test_score_completed_cell_recovery(void) {
    for (size_t threads = 1; threads <= 2; ++threads) {
        MusScoreTable table, reference;
        ScoreRecoveryTest test = {.fail = true};
        MusScoreTrainingOptions options = {
            .iterations = 2, .evaluation_samples = 50, .seed = 7, .threads = threads,
            .recover_cell = recover_test_cell, .recovery_context = &test,
        };
        CHECK(mus_score_table_init(&table, 3) == CFR_STATUS_SUCCESS);
        CHECK(mus_score_train(&table, 0, &options, save_recovery_test_cell, NULL, &test) ==
              CFR_STATUS_IO_ERROR);
        CHECK(table.cells[2][2].ready && !table.cells[1][2].ready && !table.cells[2][1].ready);
        CHECK(test.saved[1][2].ready && test.saves[1][2] == 1);
        test.fail = false;
        test.invalid = true;
        CHECK(mus_score_train(&table, 0, &options, save_recovery_test_cell, NULL, &test) ==
              CFR_STATUS_INCOMPATIBLE_GAME);
        CHECK(!table.cells[1][2].ready && test.saves[1][2] == 1);
        test.invalid = false;
        test.recovery_error = CFR_STATUS_IO_ERROR;
        CHECK(mus_score_train(&table, 0, &options, save_recovery_test_cell, NULL, &test) ==
              CFR_STATUS_IO_ERROR);
        test.recovery_error = CFR_STATUS_SUCCESS;
        CHECK(mus_score_train(&table, 0, &options, save_recovery_test_cell, NULL, &test) ==
              CFR_STATUS_SUCCESS);
        CHECK(test.saves[1][2] == 1 && test.recoveries[1][2] == 2);
        for (unsigned int a = 0; a < 3; ++a)
            for (unsigned int b = 0; b < 3; ++b) CHECK(test.saves[a][b] == 1);
        CHECK(mus_score_table_init(&reference, 3) == CFR_STATUS_SUCCESS);
        options.recover_cell = NULL;
        CHECK(mus_score_train(&reference, 0, &options, NULL, NULL, NULL) == CFR_STATUS_SUCCESS);
        for (unsigned int a = 0; a < 3; ++a)
            for (unsigned int b = 0; b < 3; ++b)
                CHECK(table.cells[a][b].value == reference.cells[a][b].value);
    }
}

typedef struct {
    FILE *checkpoints[4][4];
    bool compare;
} ScoreRegionTest;

/* Compare the complete CFR payload, excluding the differing schema and the
 * checksum that covers that schema. These files are generated by the writer. */
static bool score_region_payloads_equal(FILE *a, FILE *b) {
    FILE *streams[2] = {a, b};
    long lengths[2];
    for (size_t i = 0; i < 2; ++i) {
        unsigned char header[16];
        if (fseek(streams[i], 0, SEEK_SET) != 0 || fread(header, 1, 16, streams[i]) != 16)
            return false;
        const uint32_t schema_length = (uint32_t)header[12] |
            (uint32_t)header[13] << 8 | (uint32_t)header[14] << 16 | (uint32_t)header[15] << 24;
        if (schema_length > 255 || fseek(streams[i], -4, SEEK_END) != 0) return false;
        lengths[i] = ftell(streams[i]) - 16 - (long)schema_length;
        if (lengths[i] < 0 || fseek(streams[i], 16 + (long)schema_length, SEEK_SET) != 0) return false;
    }
    if (lengths[0] != lengths[1]) return false;
    for (long i = 0; i < lengths[0]; ++i) {
        const int byte = fgetc(a);
        if (byte == EOF || byte != fgetc(b)) return false;
    }
    return true;
}

static Status save_region_test_cell(const MusScoreGame *game, const Trainer *trainer,
    const MusScoreCell *cell, void *context) {
    (void)cell;
    ScoreRegionTest *test = context;
    const unsigned int a = game->initial_scores[0], b = game->initial_scores[1];
    FILE *stream = tmpfile();
    if (stream == NULL) return CFR_STATUS_IO_ERROR;
    Status status = cfr_checkpoint_write(stream, trainer);
    if (test->compare) {
        CHECK(test->checkpoints[a][b] != NULL);
        if (test->checkpoints[a][b] != NULL) CHECK(score_region_payloads_equal(test->checkpoints[a][b], stream));
        fclose(stream);
    } else test->checkpoints[a][b] = stream;
    return status;
}

static void test_score_closed_regions(void) {
    static const unsigned int anchors[][3] = {{30, 30, 100}, {35, 18, 195}, {0, 0, 1600}};
    for (size_t i = 0; i < sizeof(anchors) / sizeof(anchors[0]); ++i) {
        MusScoreTable table;
        CHECK(mus_score_table_init_region(&table, 40, anchors[i][0], anchors[i][1]) == CFR_STATUS_SUCCESS);
        size_t count = 0;
        for (unsigned int a = 0; a < 40; ++a) {
            for (unsigned int b = 0; b < 40; ++b) {
                if (!mus_score_table_contains(&table, a, b)) continue;
                count++;
                CHECK(mus_score_table_contains(&table, b, a));
                if (a < 39) CHECK(mus_score_table_contains(&table, a + 1, b));
                if (b < 39) CHECK(mus_score_table_contains(&table, a, b + 1));
            }
        }
        CHECK(count == anchors[i][2]);
        CHECK(!mus_score_table_contains(&table, 40, 0));
    }
    MusScoreTable full, region, loaded;
    ScoreRegionTest test = {0};
    MusScoreTrainingOptions options = {.iterations = 2, .evaluation_samples = 50, .seed = 7};
    CHECK(mus_score_table_init(&full, 4) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_train(&full, 0, &options, save_region_test_cell, NULL, &test) == CFR_STATUS_SUCCESS);
    static const unsigned int small[][2] = {{1, 1}, {0, 2}, {2, 3}, {3, 2}};
    test.compare = true;
    for (size_t i = 0; i < sizeof(small) / sizeof(small[0]); ++i) {
        CHECK(mus_score_table_init_region(&region, 4, small[i][0], small[i][1]) == CFR_STATUS_SUCCESS);
        options.threads = i % 2 + 1;
        CHECK(mus_score_train(&region, 0, &options, save_region_test_cell, NULL, &test) == CFR_STATUS_SUCCESS);
        FILE *stream = tmpfile();
        CHECK(stream != NULL);
        if (stream == NULL) continue;
        CHECK(mus_score_table_write(stream, &region) == CFR_STATUS_SUCCESS);
        rewind(stream);
        CHECK(mus_score_table_read(stream, &loaded) == CFR_STATUS_SUCCESS);
        CHECK(loaded.region_enabled && loaded.region_scores[0] == region.region_scores[0] &&
              loaded.region_scores[1] == region.region_scores[1]);
        fclose(stream);
        for (unsigned int a = 0; a < 4; ++a) {
            for (unsigned int b = 0; b < 4; ++b) {
                MusScoreGame game;
                CHECK(region.cells[a][b].ready == mus_score_table_contains(&region, a, b));
                if (region.cells[a][b].ready) {
                    CHECK(region.cells[a][b].value == full.cells[a][b].value);
                    CHECK(loaded.cells[a][b].value == full.cells[a][b].value);
                    CHECK(mus_score_game_init(&game, &region, a, b) == CFR_STATUS_SUCCESS);
                    CHECK(strstr(game.schema, MUS_SCORE_REGION_SCHEMA_ID) == game.schema);
                } else CHECK(mus_score_game_init(&game, &region, a, b) == CFR_STATUS_INVALID_ARGUMENT);
            }
        }
    }
    for (unsigned int a = 0; a < 4; ++a)
        for (unsigned int b = 0; b < 4; ++b) fclose(test.checkpoints[a][b]);
    CHECK(mus_score_table_init_region(&region, 4, 4, 0) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(mus_score_table_init_region(&region, 4, 2, 2) == CFR_STATUS_SUCCESS);
    region.cells[0][3] = (MusScoreCell){.ready = true, .iterations = 1, .evaluation_samples = 1};
    FILE *stream = tmpfile();
    CHECK(stream != NULL);
    if (stream != NULL) {
        CHECK(mus_score_table_write(stream, &region) == CFR_STATUS_INVALID_ARGUMENT);
        fclose(stream);
    }
}

static void test_score_training_and_persistence(void) {
    MusScoreTable table;
    ScoreTestSave saved = {0};
    const MusScoreTrainingOptions options = {.iterations = 2, .evaluation_samples = 50,
                                             .seed = 7};
    CHECK(mus_score_table_init(&table, 3) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_train(&table, 0, &options, fail_score_cell, NULL, NULL) ==
          CFR_STATUS_IO_ERROR);
    CHECK(table.cells[2][2].ready);
    CHECK(!table.cells[1][2].ready && !table.cells[2][1].ready);
    CHECK(mus_score_table_init(&table, 3) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_train(&table, 2, &options, test_score_save_cell,
                          test_score_save_diagonal, &saved) == CFR_STATUS_SUCCESS);
    CHECK(saved.cells == 6 && saved.diagonals == 3);
    CHECK(mus_score_train(&table, 0, &options, test_score_save_cell,
                          test_score_save_diagonal, &saved) == CFR_STATUS_SUCCESS);
    CHECK(saved.cells == 9 && saved.diagonals == 5);
    CHECK(mus_score_train(&table, 0, &options, test_score_save_cell,
                          test_score_save_diagonal, &saved) == CFR_STATUS_SUCCESS);
    CHECK(saved.cells == 9 && saved.diagonals == 5);
    MusScoreGame game;
    MusGameState state;
    InfoStore store = {0};
    Trainer trainer;
    CHECK(mus_score_game_init(&game, &table, 1, 1) == CFR_STATUS_SUCCESS);
    CHECK(strcmp(game.schema, saved.schema) == 0);
    CHECK(mus_score_game_state_init(&state, &game) == CFR_STATUS_SUCCESS);
    if (saved.checkpoint != NULL) {
        rewind(saved.checkpoint);
        CHECK(cfr_checkpoint_read(saved.checkpoint, &game.game, game_state(&state),
                                   &store, &trainer) == CFR_STATUS_SUCCESS);
        CHECK(cfr_trainer_run(&trainer, 1) == CFR_STATUS_SUCCESS);
        CHECK(cfr_info_store_destroy(&store) == CFR_STATUS_SUCCESS);
        char *version = strstr(game.schema, "/v4/");
        CHECK(version != NULL);
        if (version != NULL) {
            for (char old_version = '1'; old_version <= '3'; old_version += 1) {
                version[2] = old_version;
                rewind(saved.checkpoint);
                CHECK(cfr_checkpoint_read(saved.checkpoint, &game.game, game_state(&state),
                                           &store, &trainer) == CFR_STATUS_INCOMPATIBLE_GAME);
            }
            version[2] = '4';
        }
        table.cells[2][2].value = table.cells[2][2].value == 1 ? 0 : 1;
        CHECK(mus_score_game_init(&game, &table, 1, 1) == CFR_STATUS_SUCCESS);
        CHECK(strcmp(game.schema, saved.schema) != 0);
        rewind(saved.checkpoint);
        CHECK(cfr_checkpoint_read(saved.checkpoint, &game.game, game_state(&state),
                                   &store, &trainer) == CFR_STATUS_INCOMPATIBLE_GAME);
        rewind(saved.checkpoint);
        CHECK(mus_ordago_game_state_init(&state) == CFR_STATUS_SUCCESS);
        CHECK(cfr_checkpoint_read(saved.checkpoint, ordago_game(), game_state(&state),
                                   &store, &trainer) == CFR_STATUS_INCOMPATIBLE_GAME);
        fclose(saved.checkpoint);
    }
    table.cells[2][2].ready = false;
    CHECK(mus_score_train(&table, 0, &options, NULL, NULL, NULL) ==
          CFR_STATUS_INVALID_ARGUMENT);
    FILE *file = tmpfile();
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(mus_score_table_write(file, &table) == CFR_STATUS_INVALID_ARGUMENT);
        fputs(MUS_SCORE_SCHEMA_ID " base=" MUS_ORDAGO_STRATEGY_SCHEMA_ID
              " target=3 orientation=mano utility=signed-one\n2 2 nan 1 1 0\n", file);
        rewind(file);
        CHECK(mus_score_table_read(file, &table) == CFR_STATUS_FORMAT_ERROR);
        fclose(file);
    }
    for (int old_version = 1; old_version <= 2; old_version += 1) {
        file = tmpfile();
        CHECK(file != NULL);
        if (file != NULL) {
            fprintf(file, "mus.ordago-score/v%d base=" MUS_ORDAGO_STRATEGY_SCHEMA_ID
                    " target=3 orientation=mano utility=signed-one\n2 2 0 1 1 0\n", old_version);
            rewind(file);
            CHECK(mus_score_table_read(file, &table) == CFR_STATUS_FORMAT_ERROR);
            fclose(file);
        }
    }
}

static void test_score_sampling_statistics(void) {
    MusScoreTable table;
    MusScoreGame game;
    InfoStore store = {0};
    CHECK(mus_score_table_init(&table, 1) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_init(&game, &table, 0, 0) == CFR_STATUS_SUCCESS);
    CHECK(cfr_info_store_init(&store) == CFR_STATUS_SUCCESS);
    MusScoreEvaluationOptions options = {.min_samples = 1000, .max_samples = 1000};
    MusScoreEvaluation result;
    CHECK(mus_score_evaluate_with_options(&game, &store, &options, 7, &result) == CFR_STATUS_SUCCESS);
    CHECK(result.samples == 1000 && result.variance_known && !result.target_reached);
    /* With target one every return is -1 or +1. This independently gives
     * sum(x*x) = n and the sample variance from the measured mean. */
    const double expected = 1000.0 * (1.0 - result.value * result.value) / 999.0;
    CHECK(fabs(result.sample_variance - expected) < 1e-12);
    CHECK(fabs(result.standard_error - sqrt(expected / 1000.0)) < 1e-12);
    double legacy;
    CHECK(mus_score_evaluate(&game, &store, 1000, 7, &legacy) == CFR_STATUS_SUCCESS);
    CHECK(result.value == legacy);

    options = (MusScoreEvaluationOptions){.min_samples = 32, .max_samples = 1000,
                                          .target_standard_error = 0.08};
    CHECK(mus_score_evaluate_with_options(&game, &store, &options, 7, &result) == CFR_STATUS_SUCCESS);
    CHECK(result.samples > 32 && result.samples < 1000 && result.target_reached);
    CHECK(result.standard_error <= 0.08);
    MusScoreEvaluation prefix;
    MusScoreEvaluationOptions fixed = {.min_samples = result.samples, .max_samples = result.samples};
    CHECK(mus_score_evaluate_with_options(&game, &store, &fixed, 7, &prefix) == CFR_STATUS_SUCCESS);
    CHECK(prefix.value == result.value && prefix.sample_variance == result.sample_variance);
    fixed.min_samples--; fixed.max_samples--;
    CHECK(mus_score_evaluate_with_options(&game, &store, &fixed, 7, &prefix) == CFR_STATUS_SUCCESS);
    CHECK(prefix.standard_error > 0.08);
    options.target_standard_error = 1e-12;
    CHECK(mus_score_evaluate_with_options(&game, &store, &options, 7, &result) == CFR_STATUS_SUCCESS);
    CHECK(result.samples == 1000 && !result.target_reached && result.standard_error > 1e-12);
    options = (MusScoreEvaluationOptions){.min_samples = 1, .max_samples = 1};
    CHECK(mus_score_evaluate_with_options(&game, &store, &options, 7, &result) == CFR_STATUS_SUCCESS);
    CHECK(result.samples == 1 && !result.variance_known && result.sample_variance == 0);
    const MusScoreEvaluation preserved = result;
    static const MusScoreEvaluationOptions invalid[] = {
        {0, 100, 0.1}, {1, 100, 0.1}, {10, 5, 0.1}, {10, 100, 0},
        {10, 100, -1}, {10, 100, INFINITY}, {10, 100, NAN},
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(mus_score_evaluate_with_options(&game, &store, &invalid[i], 7, &result) ==
              CFR_STATUS_INVALID_ARGUMENT);
        CHECK(memcmp(&result, &preserved, sizeof(result)) == 0);
    }
    CHECK(cfr_info_store_destroy(&store) == CFR_STATUS_SUCCESS);

    MusScoreTrainingOptions training = {.iterations = 2, .evaluation_samples = 100,
        .evaluation_min_samples = 10, .evaluation_target_se = 0.5, .seed = 7};
    CHECK(mus_score_table_init(&table, 2) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_train(&table, 0, &training, NULL, NULL, NULL) == CFR_STATUS_SUCCESS);
    for (unsigned int a = 0; a < 2; ++a)
        for (unsigned int b = 0; b < 2; ++b) {
            CHECK(table.cells[a][b].evaluation_samples == 10);
            CHECK(table.cells[a][b].variance_known);
        }
    char schema[256];
    CHECK(mus_score_game_init(&game, &table, 0, 0) == CFR_STATUS_SUCCESS);
    strcpy(schema, game.schema);
    table.cells[1][1].variance_known = false;
    table.cells[1][1].sample_variance = 0;
    CHECK(mus_score_game_init(&game, &table, 0, 0) == CFR_STATUS_SUCCESS);
    CHECK(strcmp(schema, game.schema) == 0);
    FILE *stream = tmpfile();
    CHECK(stream != NULL);
    if (stream != NULL) {
        MusScoreTable loaded;
        CHECK(mus_score_table_write(stream, &table) == CFR_STATUS_SUCCESS);
        rewind(stream);
        CHECK(mus_score_table_read(stream, &loaded) == CFR_STATUS_SUCCESS);
        CHECK(!loaded.cells[1][1].variance_known && loaded.cells[1][1].sample_variance == 0);
        CHECK(loaded.cells[0][0].variance_known);
        CHECK(loaded.cells[0][0].sample_variance == table.cells[0][0].sample_variance);
        table.cells[0][0].sample_variance = -1;
        CHECK(mus_score_table_write(stream, &table) == CFR_STATUS_INVALID_ARGUMENT);
        fclose(stream);
    }
}

int main(void) {
    test_score_sampling_statistics();
    test_score_payment_timing();
    test_score_public_information();
    test_score_response_codec();
    test_score_preload_key_mapping();
    test_score_preload_policy();
    test_score_prior_all_lances();
    test_score_prior_paid_bounds();
    test_score_wager_menus_and_settlement();
    test_score_reduced_match_scoring_cases();
    test_score_export_rejects_foreign_games();
    test_score_utilities_and_order();
    test_score_keys_and_full_match();
    test_score_training_and_persistence();
    test_score_completed_cell_recovery();
    test_score_closed_regions();
    test_score_diagonal_retry();
    test_score_extension();
    test_score_parallel_training();
    test_score_shared_strategy_training();
    test_shared_training_runner();
    test_ordago_reservation_limits();
    test_apply_undo_and_contract();
    test_cached_hand_summary_validation();
    test_explicit_format_apis_preserve_single_bet();
    test_exact_deal_distribution();
    test_information_boundaries();
    test_raises_to_eight_actions_infosets_and_undo();
    test_ordago_mus_discards_bets_and_infosets();
    test_ordago_no_mus_scoring_and_export();
    test_ordago_betting_tree_round_trip();
    test_other_formats_reject_ordago_wager_masks();
    test_ordago_additive_wagers();
    test_ordago_abstract_hands_and_strengths();
    test_ordago_public_pairs_and_scoring();
    test_ordago_decoder_rejects_invalid_keys();
    test_literal_scoring();
    test_raises_to_eight_scoring();
    test_streaming_evaluator_on_subgame();
    test_raises_to_eight_evaluator_on_subgame();
    test_streaming_evaluator_error_outputs();
    test_ordago_evaluator_on_subgame();
    test_training_resume_and_export();
    test_raises_to_eight_training_and_export();
    test_ordago_training_and_export();

    if (failures != 0) {
        fprintf(stderr, "%d pruebas fallaron.\n", failures);
        return 1;
    }
    puts("Todas las pruebas del solver de mus pasaron.");
    return 0;
}
