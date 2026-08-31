#include <math.h>
#include <stdint.h>
#include <string.h>

#include "baraja_espanola.h"
#include "mus.h"
#include "mus_solver/mus_game.h"

#define MUS_HAND_CODE_RADIX 390625
#define MUS_INFORMATION_PHASE_COUNT 5
#define MUS_OUTCOME_RADIX 7
#define MUS_CURRENT_HISTORY_RADIX 4

static const uint8_t CLASS_CAPACITIES[MUS_GAME_CLASS_COUNT] = {
    8, 4, 4, 4, 4, 4, 4, 8,
};

static const int CLASS_NUMBERS[MUS_GAME_CLASS_COUNT] = {
    AS, CUATRO, CINCO, SEIS, SIETE, SOTA, CABALLO, REY,
};

static Status mus_is_terminal(const void *context, const GameState *state,
                              bool *result);
static Status mus_terminal_utility(const void *context,
                                   const GameState *state, Player player,
                                   Utility *result);
static Status mus_current_actor(const void *context, const GameState *state,
                                Actor *result);
static Status mus_legal_actions(const void *context, const GameState *state,
                                Action *actions, size_t capacity,
                                size_t *required_count);
static Status mus_apply_action(const void *context, GameState *state,
                               Action action);
static Status mus_undo_action(const void *context, GameState *state);
static Status mus_chance_probability(const void *context,
                                     const GameState *state, Action action,
                                     Probability *result);
static Status mus_information_set_key(const void *context,
                                      const GameState *state,
                                      InfoSetKey *result);
static Status mus_validate_state(const void *context, const GameState *state);
static Status mus_chance_outcomes(const void *context, const GameState *state,
                                  Action *actions, Probability *probabilities,
                                  size_t capacity, size_t *required_count);

static const GameOperations MUS_OPERATIONS = {
    .is_terminal = mus_is_terminal,
    .terminal_utility = mus_terminal_utility,
    .current_actor = mus_current_actor,
    .legal_actions = mus_legal_actions,
    .apply_action = mus_apply_action,
    .undo_action = mus_undo_action,
    .chance_probability = mus_chance_probability,
    .information_set_key = mus_information_set_key,
    .validate_state = mus_validate_state,
    .chance_outcomes = mus_chance_outcomes,
};

static const Game MUS_GAME = {
    .operations = &MUS_OPERATIONS,
    .context = NULL,
    .strategic_player_count = MUS_GAME_PLAYER_COUNT,
    .max_legal_actions = MUS_GAME_MAX_LEGAL_ACTIONS,
    .strategy_schema_id = "mus.heads-up-single-hand/v1",
    .trusted_operations = NULL,
};

static MusGameState *as_mus(GameState *state) {
    return (MusGameState *)state;
}

static const MusGameState *as_mus_const(const GameState *state) {
    return (const MusGameState *)state;
}

static bool player_is_valid(Player player) {
    return player == CFR_PLAYER_0 || player == CFR_PLAYER_1;
}

static bool phase_is_deal(MusGamePhase phase) {
    return phase == MUS_PHASE_DEAL_PLAYER_0 ||
           phase == MUS_PHASE_DEAL_PLAYER_1;
}

static bool phase_is_betting(MusGamePhase phase) {
    return phase >= MUS_PHASE_GRANDE && phase <= MUS_PHASE_PUNTO;
}

static int deal_player(const MusGameModel *model) {
    return model->phase == MUS_PHASE_DEAL_PLAYER_0 ? 0 : 1;
}

static size_t hand_total(const uint8_t hand[MUS_GAME_CLASS_COUNT]) {
    size_t total = 0;

    for (size_t index = 0; index < MUS_GAME_CLASS_COUNT; index += 1)
        total += hand[index];
    return total;
}

static void make_hands(const MusGameModel *model, Mano hands[2],
                       Carta cards[2][MUS_GAME_HAND_SIZE]) {
    for (int player = 0; player < MUS_GAME_PLAYER_COUNT; player += 1) {
        size_t position = 0;

        for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT;
             card_class += 1) {
            for (uint8_t copy = 0; copy < model->hands[player][card_class];
                 copy += 1) {
                cards[player][position] =
                    (Carta){.numero = CLASS_NUMBERS[card_class],
                            .palo = (int)(position % 4)};
                position += 1;
            }
        }
        hands[player] = (Mano){.cartas = cards[player], .tamano = position};
    }
}

static int player_has_pairs(const MusGameModel *model, int player) {
    Mano hands[2];
    Carta cards[2][MUS_GAME_HAND_SIZE];

    make_hands(model, hands, cards);
    return tipoPares(hands[player]) != NO_PAR;
}

static int player_has_game(const MusGameModel *model, int player) {
    Mano hands[2];
    Carta cards[2][MUS_GAME_HAND_SIZE];

    make_hands(model, hands, cards);
    return tieneJuego(hands[player]);
}

static int phase_lance_index(MusGamePhase phase) {
    switch (phase) {
    case MUS_PHASE_GRANDE:
        return 0;
    case MUS_PHASE_CHICA:
        return 1;
    case MUS_PHASE_PARES:
        return 2;
    case MUS_PHASE_JUEGO:
    case MUS_PHASE_PUNTO:
        return 3;
    default:
        return -1;
    }
}

static void start_betting(MusGameModel *model, MusGamePhase phase) {
    model->phase = phase;
    model->current_player = CFR_PLAYER_0;
    model->bet_stage = MUS_BET_FIRST_TO_ACT;
    model->bettor = CFR_PLAYER_0;
}

static void prepare_final_lance(MusGameModel *model) {
    model->game_revealed = true;
    model->game[0] = player_has_game(model, 0) != 0;
    model->game[1] = player_has_game(model, 1) != 0;

    if (model->game[0] && model->game[1]) {
        start_betting(model, MUS_PHASE_JUEGO);
    } else if (!model->game[0] && !model->game[1]) {
        start_betting(model, MUS_PHASE_PUNTO);
    } else {
        model->outcomes[3] = MUS_LANCE_SKIPPED;
        model->phase = MUS_PHASE_TERMINAL;
    }
}

static void advance_lance(MusGameModel *model, MusLanceOutcome outcome) {
    const MusGamePhase completed_phase = model->phase;
    const int lance = phase_lance_index(completed_phase);

    if (lance >= 0)
        model->outcomes[lance] = outcome;
    if (completed_phase == MUS_PHASE_GRANDE) {
        start_betting(model, MUS_PHASE_CHICA);
    } else if (completed_phase == MUS_PHASE_CHICA) {
        model->pairs_revealed = true;
        model->pairs[0] = player_has_pairs(model, 0) != 0;
        model->pairs[1] = player_has_pairs(model, 1) != 0;
        if (model->pairs[0] && model->pairs[1]) {
            start_betting(model, MUS_PHASE_PARES);
        } else {
            model->outcomes[2] = MUS_LANCE_SKIPPED;
            prepare_final_lance(model);
        }
    } else if (completed_phase == MUS_PHASE_PARES) {
        prepare_final_lance(model);
    } else if (completed_phase == MUS_PHASE_JUEGO ||
               completed_phase == MUS_PHASE_PUNTO) {
        model->phase = MUS_PHASE_TERMINAL;
    }
}

static uint64_t combinations(int n, int k) {
    uint64_t result = 1;

    if (k < 0 || k > n)
        return 0;
    if (k > n - k)
        k = n - k;
    for (int index = 1; index <= k; index += 1)
        result = result * (uint64_t)(n - k + index) / (uint64_t)index;
    return result;
}

static int available_in_class(const MusGameModel *model, int player,
                              int card_class) {
    int available = CLASS_CAPACITIES[card_class];

    if (player == 1)
        available -= model->hands[0][card_class];
    return available;
}

static int available_after_class(const MusGameModel *model, int player,
                                 int card_class) {
    int available = 0;

    for (int index = card_class + 1; index < MUS_GAME_CLASS_COUNT;
         index += 1) {
        available += available_in_class(model, player, index);
    }
    return available;
}

static bool deal_count_is_legal(const MusGameModel *model, int count) {
    const int player = deal_player(model);
    const int current_available =
        available_in_class(model, player, model->deal_class);
    const int later_available =
        available_after_class(model, player, model->deal_class);

    return count >= 0 && count <= model->deal_remaining &&
           count <= current_available &&
           model->deal_remaining - count <= later_available;
}

static Probability deal_count_probability(const MusGameModel *model,
                                           int count) {
    const int player = deal_player(model);
    const int current_available =
        available_in_class(model, player, model->deal_class);
    const int later_available =
        available_after_class(model, player, model->deal_class);
    const int remaining = model->deal_remaining;
    const uint64_t denominator =
        combinations(current_available + later_available, remaining);
    const uint64_t numerator = combinations(current_available, count) *
                               combinations(later_available,
                                            remaining - count);

    if (denominator == 0)
        return 0.0;
    return (Probability)numerator / (Probability)denominator;
}

static Status collect_deal_actions(const MusGameModel *model, Action *actions,
                                   Probability *probabilities,
                                   size_t capacity, size_t *count_out) {
    size_t count = 0;

    for (int candidate = 0; candidate <= MUS_GAME_HAND_SIZE; candidate += 1) {
        if (!deal_count_is_legal(model, candidate))
            continue;
        if (count == capacity)
            return CFR_STATUS_BUFFER_TOO_SMALL;
        actions[count] = candidate;
        if (probabilities != NULL)
            probabilities[count] = deal_count_probability(model, candidate);
        count += 1;
    }
    *count_out = count;
    return count == 0 ? CFR_STATUS_INVALID_ARGUMENT : CFR_STATUS_SUCCESS;
}

static Status validate_model(const MusGameModel *model) {
    if (model == NULL || model->phase < MUS_PHASE_DEAL_PLAYER_0 ||
        model->phase > MUS_PHASE_TERMINAL ||
        !player_is_valid(model->current_player) ||
        !player_is_valid(model->bettor)) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT;
         card_class += 1) {
        if (model->hands[0][card_class] > CLASS_CAPACITIES[card_class] ||
            model->hands[1][card_class] >
                CLASS_CAPACITIES[card_class] -
                    model->hands[0][card_class]) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
    }
    if (phase_is_deal(model->phase)) {
        const int player = deal_player(model);
        const size_t total = hand_total(model->hands[player]);

        if (model->deal_class >= MUS_GAME_CLASS_COUNT - 1 ||
            model->deal_remaining > MUS_GAME_HAND_SIZE ||
            total + model->deal_remaining != MUS_GAME_HAND_SIZE ||
            (player == 1 && hand_total(model->hands[0]) !=
                                MUS_GAME_HAND_SIZE)) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
        return CFR_STATUS_SUCCESS;
    }
    if (hand_total(model->hands[0]) != MUS_GAME_HAND_SIZE ||
        hand_total(model->hands[1]) != MUS_GAME_HAND_SIZE) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    if (phase_is_betting(model->phase)) {
        if (model->bet_stage == MUS_BET_FIRST_TO_ACT &&
            model->current_player != CFR_PLAYER_0) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
        if (model->bet_stage == MUS_BET_SECOND_AFTER_PASS &&
            model->current_player != CFR_PLAYER_1) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
        if (model->bet_stage == MUS_BET_RESPONSE &&
            model->current_player == model->bettor) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
        if (model->bet_stage < MUS_BET_FIRST_TO_ACT ||
            model->bet_stage > MUS_BET_RESPONSE) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
    }
    return CFR_STATUS_SUCCESS;
}

Status mus_game_state_init(MusGameState *state) {
    if (state == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    *state = (MusGameState){0};
    state->model.phase = MUS_PHASE_DEAL_PLAYER_0;
    state->model.deal_class = 0;
    state->model.deal_remaining = MUS_GAME_HAND_SIZE;
    state->model.current_player = CFR_PLAYER_0;
    state->model.bettor = CFR_PLAYER_0;
    return CFR_STATUS_SUCCESS;
}

const Game *mus_game_descriptor(void) {
    return &MUS_GAME;
}

GameState *mus_game_state_as_game_state(MusGameState *state) {
    return (GameState *)state;
}

const GameState *mus_game_state_as_game_state_const(const MusGameState *state) {
    return (const GameState *)state;
}

static Status mus_is_terminal(const void *context, const GameState *state,
                              bool *result) {
    const MusGameState *mus = as_mus_const(state);

    (void)context;
    if (result == NULL || mus == NULL ||
        validate_model(&mus->model) != CFR_STATUS_SUCCESS) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    *result = mus->model.phase == MUS_PHASE_TERMINAL;
    return CFR_STATUS_SUCCESS;
}

static bool outcome_is_accepted(MusLanceOutcome outcome) {
    return outcome == MUS_LANCE_FIRST_BET_ACCEPTED ||
           outcome == MUS_LANCE_SECOND_BET_ACCEPTED;
}

static bool outcome_is_rejected(MusLanceOutcome outcome) {
    return outcome == MUS_LANCE_FIRST_BET_REJECTED ||
           outcome == MUS_LANCE_SECOND_BET_REJECTED;
}

static int outcome_bettor(MusLanceOutcome outcome) {
    if (outcome == MUS_LANCE_FIRST_BET_REJECTED ||
        outcome == MUS_LANCE_FIRST_BET_ACCEPTED) {
        return 0;
    }
    if (outcome == MUS_LANCE_SECOND_BET_REJECTED ||
        outcome == MUS_LANCE_SECOND_BET_ACCEPTED) {
        return 1;
    }
    return -1;
}

static Status score_terminal(const MusGameModel *model, int scores[2]) {
    Mano hands[2];
    Carta cards[2][MUS_GAME_HAND_SIZE];
    int winner;
    MusLanceOutcome outcome;

    if (model == NULL || scores == NULL ||
        model->phase != MUS_PHASE_TERMINAL) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    scores[0] = 0;
    scores[1] = 0;
    make_hands(model, hands, cards);

    outcome = model->outcomes[0];
    if (outcome_is_rejected(outcome)) {
        scores[outcome_bettor(outcome)] += 1;
    } else {
        winner = ganadorGrandeConJugadores(hands, 2, 0);
        if (winner < 0)
            return CFR_STATUS_INVALID_ARGUMENT;
        scores[winner] += outcome_is_accepted(outcome) ? 2 : 1;
    }

    outcome = model->outcomes[1];
    if (outcome_is_rejected(outcome)) {
        scores[outcome_bettor(outcome)] += 1;
    } else {
        winner = ganadorChicaConJugadores(hands, 2, 0);
        if (winner < 0)
            return CFR_STATUS_INVALID_ARGUMENT;
        scores[winner] += outcome_is_accepted(outcome) ? 2 : 1;
    }

    outcome = model->outcomes[2];
    if (model->pairs[0] || model->pairs[1]) {
        if (outcome_is_rejected(outcome)) {
            winner = outcome_bettor(outcome);
            scores[winner] += 1;
        } else if (model->pairs[0] && model->pairs[1]) {
            winner = ganadorParConJugadores(hands, 2, 0);
        } else {
            winner = model->pairs[0] ? 0 : 1;
        }
        {
            const int intrinsic = tantosPares(hands[winner]);
            if (intrinsic < 0)
                return CFR_STATUS_INVALID_ARGUMENT;
            scores[winner] += intrinsic;
        }
        if (outcome_is_accepted(outcome))
            scores[winner] += 2;
    }

    outcome = model->outcomes[3];
    if (model->game[0] || model->game[1]) {
        if (outcome_is_rejected(outcome)) {
            winner = outcome_bettor(outcome);
            scores[winner] += 1;
        } else if (model->game[0] && model->game[1]) {
            winner = ganadorJuegoConJugadores(hands, 2, 0);
        } else {
            winner = model->game[0] ? 0 : 1;
        }
        {
            const int intrinsic = tantosJuego(hands[winner]);
            if (intrinsic < 0)
                return CFR_STATUS_INVALID_ARGUMENT;
            scores[winner] += intrinsic;
        }
        if (outcome_is_accepted(outcome))
            scores[winner] += 2;
    } else {
        if (outcome_is_rejected(outcome)) {
            winner = outcome_bettor(outcome);
            scores[winner] += 2;
        } else {
            winner = ganadorPuntoConJugadores(hands, 2, 0);
            if (winner < 0)
                return CFR_STATUS_INVALID_ARGUMENT;
            scores[winner] += 1 + (outcome_is_accepted(outcome) ? 2 : 0);
        }
    }
    return CFR_STATUS_SUCCESS;
}

static Status mus_terminal_utility(const void *context,
                                   const GameState *state, Player player,
                                   Utility *result) {
    const MusGameState *mus = as_mus_const(state);
    int scores[2];
    Status status;

    (void)context;
    if (mus == NULL || result == NULL || !player_is_valid(player) ||
        validate_model(&mus->model) != CFR_STATUS_SUCCESS ||
        mus->model.phase != MUS_PHASE_TERMINAL) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    status = score_terminal(&mus->model, scores);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    *result = player == CFR_PLAYER_0 ? (Utility)(scores[0] - scores[1])
                                    : (Utility)(scores[1] - scores[0]);
    return CFR_STATUS_SUCCESS;
}

static Status mus_current_actor(const void *context, const GameState *state,
                                Actor *result) {
    const MusGameState *mus = as_mus_const(state);

    (void)context;
    if (mus == NULL || result == NULL ||
        validate_model(&mus->model) != CFR_STATUS_SUCCESS ||
        mus->model.phase == MUS_PHASE_TERMINAL) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    if (phase_is_deal(mus->model.phase)) {
        *result = (Actor){.kind = CFR_ACTOR_CHANCE, .player = CFR_PLAYER_0};
    } else {
        *result = (Actor){.kind = CFR_ACTOR_PLAYER,
                          .player = mus->model.current_player};
    }
    return CFR_STATUS_SUCCESS;
}

static Status mus_legal_actions(const void *context, const GameState *state,
                                Action *actions, size_t capacity,
                                size_t *required_count) {
    const MusGameState *mus = as_mus_const(state);

    (void)context;
    if (mus == NULL || actions == NULL || required_count == NULL ||
        validate_model(&mus->model) != CFR_STATUS_SUCCESS ||
        mus->model.phase == MUS_PHASE_TERMINAL) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    if (phase_is_deal(mus->model.phase)) {
        Action temporary[MUS_GAME_MAX_LEGAL_ACTIONS];
        size_t count = 0;
        Status status = collect_deal_actions(
            &mus->model, temporary, NULL, MUS_GAME_MAX_LEGAL_ACTIONS, &count);

        *required_count = count;
        if (status != CFR_STATUS_SUCCESS)
            return status;
        if (capacity < count)
            return CFR_STATUS_BUFFER_TOO_SMALL;
        memcpy(actions, temporary, count * sizeof(*actions));
        return CFR_STATUS_SUCCESS;
    }
    *required_count = 2;
    if (capacity < 2)
        return CFR_STATUS_BUFFER_TOO_SMALL;
    if (mus->model.bet_stage == MUS_BET_RESPONSE) {
        actions[0] = MUS_ACTION_REJECT;
        actions[1] = MUS_ACTION_ACCEPT;
    } else {
        actions[0] = MUS_ACTION_PASS;
        actions[1] = MUS_ACTION_BET_TWO;
    }
    return CFR_STATUS_SUCCESS;
}

static bool player_action_is_legal(const MusGameModel *model, Action action) {
    if (model->bet_stage == MUS_BET_RESPONSE)
        return action == MUS_ACTION_REJECT || action == MUS_ACTION_ACCEPT;
    return action == MUS_ACTION_PASS || action == MUS_ACTION_BET_TWO;
}

static Status apply_to_model(MusGameModel *model, Action action) {
    if (phase_is_deal(model->phase)) {
        const int player = deal_player(model);
        const int count = action;

        if (!deal_count_is_legal(model, count))
            return CFR_STATUS_ILLEGAL_ACTION;
        model->hands[player][model->deal_class] = (uint8_t)count;
        model->deal_remaining = (uint8_t)(model->deal_remaining - count);
        model->deal_class += 1;
        if (model->deal_class == MUS_GAME_CLASS_COUNT - 1) {
            const int last_available =
                available_in_class(model, player, MUS_GAME_CLASS_COUNT - 1);

            if (model->deal_remaining > last_available)
                return CFR_STATUS_INVALID_ARGUMENT;
            model->hands[player][MUS_GAME_CLASS_COUNT - 1] =
                model->deal_remaining;
            model->deal_remaining = MUS_GAME_HAND_SIZE;
            model->deal_class = 0;
            if (player == 0) {
                model->phase = MUS_PHASE_DEAL_PLAYER_1;
            } else {
                model->deal_remaining = 0;
                start_betting(model, MUS_PHASE_GRANDE);
            }
        }
        return CFR_STATUS_SUCCESS;
    }
    if (!phase_is_betting(model->phase))
        return CFR_STATUS_INVALID_ARGUMENT;
    if (!player_action_is_legal(model, action))
        return CFR_STATUS_ILLEGAL_ACTION;

    if (model->bet_stage == MUS_BET_FIRST_TO_ACT) {
        if (action == MUS_ACTION_PASS) {
            model->bet_stage = MUS_BET_SECOND_AFTER_PASS;
            model->current_player = CFR_PLAYER_1;
        } else {
            model->bet_stage = MUS_BET_RESPONSE;
            model->bettor = CFR_PLAYER_0;
            model->current_player = CFR_PLAYER_1;
        }
    } else if (model->bet_stage == MUS_BET_SECOND_AFTER_PASS) {
        if (action == MUS_ACTION_PASS) {
            advance_lance(model, MUS_LANCE_ALL_PASS);
        } else {
            model->bet_stage = MUS_BET_RESPONSE;
            model->bettor = CFR_PLAYER_1;
            model->current_player = CFR_PLAYER_0;
        }
    } else {
        MusLanceOutcome outcome;

        if (model->bettor == CFR_PLAYER_0) {
            outcome = action == MUS_ACTION_ACCEPT
                          ? MUS_LANCE_FIRST_BET_ACCEPTED
                          : MUS_LANCE_FIRST_BET_REJECTED;
        } else {
            outcome = action == MUS_ACTION_ACCEPT
                          ? MUS_LANCE_SECOND_BET_ACCEPTED
                          : MUS_LANCE_SECOND_BET_REJECTED;
        }
        advance_lance(model, outcome);
    }
    return CFR_STATUS_SUCCESS;
}

static Status mus_apply_action(const void *context, GameState *state,
                               Action action) {
    MusGameState *mus = as_mus(state);
    MusGameModel temporary;
    Status status;

    (void)context;
    if (mus == NULL || validate_model(&mus->model) != CFR_STATUS_SUCCESS)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (mus->undo_count >= MUS_GAME_UNDO_CAPACITY)
        return CFR_STATUS_BUFFER_TOO_SMALL;
    temporary = mus->model;
    status = apply_to_model(&temporary, action);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (validate_model(&temporary) != CFR_STATUS_SUCCESS)
        return CFR_STATUS_INVALID_ARGUMENT;
    mus->undo_history[mus->undo_count] = mus->model;
    mus->undo_count += 1;
    mus->model = temporary;
    return CFR_STATUS_SUCCESS;
}

static Status mus_undo_action(const void *context, GameState *state) {
    MusGameState *mus = as_mus(state);

    (void)context;
    if (mus == NULL || mus->undo_count == 0 ||
        validate_model(&mus->model) != CFR_STATUS_SUCCESS) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    mus->undo_count -= 1;
    mus->model = mus->undo_history[mus->undo_count];
    return CFR_STATUS_SUCCESS;
}

static Status mus_chance_probability(const void *context,
                                     const GameState *state, Action action,
                                     Probability *result) {
    const MusGameState *mus = as_mus_const(state);

    (void)context;
    if (mus == NULL || result == NULL ||
        validate_model(&mus->model) != CFR_STATUS_SUCCESS ||
        !phase_is_deal(mus->model.phase)) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    if (!deal_count_is_legal(&mus->model, action))
        return CFR_STATUS_ILLEGAL_ACTION;
    *result = deal_count_probability(&mus->model, action);
    return CFR_STATUS_SUCCESS;
}

static int information_phase_code(MusGamePhase phase) {
    switch (phase) {
    case MUS_PHASE_GRANDE:
        return 0;
    case MUS_PHASE_CHICA:
        return 1;
    case MUS_PHASE_PARES:
        return 2;
    case MUS_PHASE_JUEGO:
        return 3;
    case MUS_PHASE_PUNTO:
        return 4;
    default:
        return -1;
    }
}

static int current_history_code(const MusGameModel *model) {
    if (model->bet_stage == MUS_BET_FIRST_TO_ACT)
        return 0;
    if (model->bet_stage == MUS_BET_SECOND_AFTER_PASS)
        return 1;
    return model->bettor == CFR_PLAYER_0 ? 2 : 3;
}

static int hand_code(const uint8_t hand[MUS_GAME_CLASS_COUNT]) {
    int code = 0;
    int factor = 1;

    for (int index = 0; index < MUS_GAME_CLASS_COUNT; index += 1) {
        code += hand[index] * factor;
        factor *= 5;
    }
    return code;
}

static Status mus_information_set_key(const void *context,
                                      const GameState *state,
                                      InfoSetKey *result) {
    const MusGameState *mus = as_mus_const(state);
    const MusGameModel *model;
    InfoSetKey key;
    int phase_code;

    (void)context;
    if (mus == NULL || result == NULL ||
        validate_model(&mus->model) != CFR_STATUS_SUCCESS ||
        !phase_is_betting(mus->model.phase)) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    model = &mus->model;
    phase_code = information_phase_code(model->phase);
    if (phase_code < 0)
        return CFR_STATUS_INVALID_ARGUMENT;

    key = model->current_player;
    key = key * MUS_HAND_CODE_RADIX +
          hand_code(model->hands[model->current_player]);
    key = key * MUS_INFORMATION_PHASE_COUNT + phase_code;
    key = key * 2 + model->pairs_revealed;
    key = key * 2 + model->pairs[0];
    key = key * 2 + model->pairs[1];
    key = key * 2 + model->game_revealed;
    key = key * 2 + model->game[0];
    key = key * 2 + model->game[1];
    for (int lance = 0; lance < MUS_GAME_LANCE_COUNT; lance += 1)
        key = key * MUS_OUTCOME_RADIX + model->outcomes[lance];
    key = key * MUS_CURRENT_HISTORY_RADIX + current_history_code(model);
    *result = key;
    return CFR_STATUS_SUCCESS;
}

static Status mus_validate_state(const void *context, const GameState *state) {
    const MusGameState *mus = as_mus_const(state);

    (void)context;
    if (mus == NULL || mus->undo_count > MUS_GAME_UNDO_CAPACITY)
        return CFR_STATUS_INVALID_ARGUMENT;
    return validate_model(&mus->model);
}

static Status mus_chance_outcomes(const void *context, const GameState *state,
                                  Action *actions, Probability *probabilities,
                                  size_t capacity, size_t *required_count) {
    const MusGameState *mus = as_mus_const(state);
    Action temporary_actions[MUS_GAME_MAX_LEGAL_ACTIONS];
    Probability temporary_probabilities[MUS_GAME_MAX_LEGAL_ACTIONS];
    size_t count = 0;
    Status status;

    (void)context;
    if (mus == NULL || actions == NULL || probabilities == NULL ||
        required_count == NULL ||
        validate_model(&mus->model) != CFR_STATUS_SUCCESS ||
        !phase_is_deal(mus->model.phase)) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    status = collect_deal_actions(&mus->model, temporary_actions,
                                  temporary_probabilities,
                                  MUS_GAME_MAX_LEGAL_ACTIONS, &count);
    *required_count = count;
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (capacity < count)
        return CFR_STATUS_BUFFER_TOO_SMALL;
    memcpy(actions, temporary_actions, count * sizeof(*actions));
    memcpy(probabilities, temporary_probabilities,
           count * sizeof(*probabilities));
    return CFR_STATUS_SUCCESS;
}

Status mus_game_information_set_decode(InfoSetKey key,
                                       MusInformationSet *information_set) {
    int history_code;
    int hand;

    if (information_set == NULL || key < 0)
        return CFR_STATUS_INVALID_ARGUMENT;
    *information_set = (MusInformationSet){0};
    history_code = (int)(key % MUS_CURRENT_HISTORY_RADIX);
    key /= MUS_CURRENT_HISTORY_RADIX;
    for (int lance = MUS_GAME_LANCE_COUNT - 1; lance >= 0; lance -= 1) {
        information_set->outcomes[lance] =
            (MusLanceOutcome)(key % MUS_OUTCOME_RADIX);
        key /= MUS_OUTCOME_RADIX;
    }
    information_set->game[1] = (bool)(key % 2);
    key /= 2;
    information_set->game[0] = (bool)(key % 2);
    key /= 2;
    information_set->game_revealed = (bool)(key % 2);
    key /= 2;
    information_set->pairs[1] = (bool)(key % 2);
    key /= 2;
    information_set->pairs[0] = (bool)(key % 2);
    key /= 2;
    information_set->pairs_revealed = (bool)(key % 2);
    key /= 2;
    information_set->phase = (MusGamePhase)(MUS_PHASE_GRANDE +
                                            key % MUS_INFORMATION_PHASE_COUNT);
    key /= MUS_INFORMATION_PHASE_COUNT;
    hand = (int)(key % MUS_HAND_CODE_RADIX);
    key /= MUS_HAND_CODE_RADIX;
    if (key > 1)
        return CFR_STATUS_INVALID_ARGUMENT;
    information_set->player = (Player)key;
    for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT;
         card_class += 1) {
        information_set->hand[card_class] = (uint8_t)(hand % 5);
        hand /= 5;
    }
    if (hand != 0 ||
        hand_total(information_set->hand) != MUS_GAME_HAND_SIZE ||
        history_code < 0 || history_code >= MUS_CURRENT_HISTORY_RADIX)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (history_code == 0) {
        information_set->bet_stage = MUS_BET_FIRST_TO_ACT;
        information_set->bettor = CFR_PLAYER_0;
    } else if (history_code == 1) {
        information_set->bet_stage = MUS_BET_SECOND_AFTER_PASS;
        information_set->bettor = CFR_PLAYER_0;
    } else {
        information_set->bet_stage = MUS_BET_RESPONSE;
        information_set->bettor = history_code == 2 ? CFR_PLAYER_0
                                                     : CFR_PLAYER_1;
    }
    return CFR_STATUS_SUCCESS;
}

const char *mus_game_class_name(MusCardClass card_class) {
    static const char *const names[MUS_GAME_CLASS_COUNT] = {
        "pitos", "cuatros", "cincos", "seises",
        "sietes", "sotas", "caballos", "cerdos",
    };

    if (card_class < MUS_CLASS_PITOS || card_class > MUS_CLASS_KINGS)
        return "desconocida";
    return names[card_class];
}

const char *mus_game_phase_name(MusGamePhase phase) {
    switch (phase) {
    case MUS_PHASE_DEAL_PLAYER_0:
        return "reparto_jugador_0";
    case MUS_PHASE_DEAL_PLAYER_1:
        return "reparto_jugador_1";
    case MUS_PHASE_GRANDE:
        return "grande";
    case MUS_PHASE_CHICA:
        return "chica";
    case MUS_PHASE_PARES:
        return "pares";
    case MUS_PHASE_JUEGO:
        return "juego";
    case MUS_PHASE_PUNTO:
        return "punto";
    case MUS_PHASE_TERMINAL:
        return "terminal";
    }
    return "desconocida";
}

const char *mus_game_outcome_name(MusLanceOutcome outcome) {
    switch (outcome) {
    case MUS_LANCE_UNRESOLVED:
        return "pendiente";
    case MUS_LANCE_ALL_PASS:
        return "paso-paso";
    case MUS_LANCE_FIRST_BET_REJECTED:
        return "envite_mano-no_querido";
    case MUS_LANCE_FIRST_BET_ACCEPTED:
        return "envite_mano-querido";
    case MUS_LANCE_SECOND_BET_REJECTED:
        return "paso-envite_postre-no_querido";
    case MUS_LANCE_SECOND_BET_ACCEPTED:
        return "paso-envite_postre-querido";
    case MUS_LANCE_SKIPPED:
        return "sin_negociacion";
    }
    return "desconocido";
}
