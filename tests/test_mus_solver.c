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
    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS] = {99, 99, 99, 99, 99};
    size_t count = 99;
    bool terminal = true;

    CHECK(mus_game_state_init(NULL) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(mus_game_state_init(&state) == CFR_STATUS_SUCCESS);
    CHECK(game()->trusted_operations != NULL);
    root = state.model;
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

int main(void) {
    test_apply_undo_and_contract();
    test_cached_hand_summary_validation();
    test_exact_deal_distribution();
    test_information_boundaries();
    test_literal_scoring();
    test_streaming_evaluator_on_subgame();
    test_training_resume_and_export();

    if (failures != 0) {
        fprintf(stderr, "%d pruebas fallaron.\n", failures);
        return 1;
    }
    puts("Todas las pruebas del solver de mus pasaron.");
    return 0;
}
