#include <math.h>
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
        const Status apply_status = cfr_game_apply_action(
            ordago_game(), ordago_game_state(state),
            model_actions[action_index]);

        CHECK(apply_status == CFR_STATUS_SUCCESS);
        if (apply_status != CFR_STATUS_SUCCESS)
            continue;
        if (state->model.phase == lance) {
            check_ordago_betting_tree_round_trip(
                state, lance, visited);
        }
        CHECK(cfr_game_undo_action(
                  ordago_game(), ordago_game_state(state)) ==
              CFR_STATUS_SUCCESS);
    }
}

static void test_ordago_betting_tree_round_trip(void) {
    const uint8_t kings[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 0, 0, 0, 0, 4};
    const uint8_t knights[MUS_GAME_CLASS_COUNT] = {0, 0, 0, 0, 0, 0, 4, 0};
    MusOrdagoGameState state;
    size_t visited = 0;

    ordago_deal(&state, kings, knights);
    ordago_apply(&state, MUS_ACTION_NO_MUS);
    check_ordago_betting_tree_round_trip(
        &state, MUS_PHASE_GRANDE, &visited);
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

int main(void) {
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
