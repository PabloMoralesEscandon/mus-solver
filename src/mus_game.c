#include <math.h>
#include <stdint.h>
#include <string.h>

#include "mus_solver/mus_game.h"

#define MUS_HAND_CODE_RADIX 390625
#define MUS_INFORMATION_PHASE_COUNT 5
#define MUS_OUTCOME_RADIX 7
#define MUS_CURRENT_HISTORY_RADIX 4
#define MUS_DECK_SIZE 40
#define MUS_RAISES_OUTCOME_RADIX (MUS_OUTCOME_RADIX * 4)
#define MUS_RAISES_CURRENT_HISTORY_RADIX 8
#define MUS_ORDAGO_EXACT_HAND_RADIX 330
#define MUS_ORDAGO_GRANDE_HAND_RADIX 29
#define MUS_ORDAGO_CHICA_HAND_RADIX 29
#define MUS_ORDAGO_PAIRS_HAND_RADIX 52
#define MUS_ORDAGO_GAME_HAND_RADIX 8
#define MUS_ORDAGO_POINT_HAND_RADIX 27
#define MUS_ORDAGO_DISCARD_COUNT_RADIX (MUS_GAME_HAND_SIZE + 1)
#define MUS_ORDAGO_PUBLIC_MUS_RADIX 677
#define MUS_ORDAGO_STRENGTH_RADIX 4
#define MUS_ORDAGO_WAGER_RADIX (MUS_ORDAGO_WAGER + 1)
#define MUS_ORDAGO_SITUATION_COUNT 28

typedef struct {
    bool raises_to_eight;
    bool ordago_with_mus;
} MusGameFormat;

static const MusGameFormat MUS_SINGLE_BET_FORMAT = {
    .raises_to_eight = false,
    .ordago_with_mus = false,
};

static const MusGameFormat MUS_RAISES_TO_EIGHT_FORMAT = {
    .raises_to_eight = true,
    .ordago_with_mus = false,
};

static const MusGameFormat MUS_ORDAGO_FORMAT = {
    .raises_to_eight = false,
    .ordago_with_mus = true,
};

static const uint8_t CLASS_CAPACITIES[MUS_GAME_CLASS_COUNT] = {
    8, 4, 4, 4, 4, 4, 4, 8,
};

static const uint32_t BINOMIAL_COEFFICIENTS[MUS_DECK_SIZE + 1]
                                                   [MUS_GAME_HAND_SIZE + 1] = {
    {1, 0, 0, 0, 0},
    {1, 1, 0, 0, 0},
    {1, 2, 1, 0, 0},
    {1, 3, 3, 1, 0},
    {1, 4, 6, 4, 1},
    {1, 5, 10, 10, 5},
    {1, 6, 15, 20, 15},
    {1, 7, 21, 35, 35},
    {1, 8, 28, 56, 70},
    {1, 9, 36, 84, 126},
    {1, 10, 45, 120, 210},
    {1, 11, 55, 165, 330},
    {1, 12, 66, 220, 495},
    {1, 13, 78, 286, 715},
    {1, 14, 91, 364, 1001},
    {1, 15, 105, 455, 1365},
    {1, 16, 120, 560, 1820},
    {1, 17, 136, 680, 2380},
    {1, 18, 153, 816, 3060},
    {1, 19, 171, 969, 3876},
    {1, 20, 190, 1140, 4845},
    {1, 21, 210, 1330, 5985},
    {1, 22, 231, 1540, 7315},
    {1, 23, 253, 1771, 8855},
    {1, 24, 276, 2024, 10626},
    {1, 25, 300, 2300, 12650},
    {1, 26, 325, 2600, 14950},
    {1, 27, 351, 2925, 17550},
    {1, 28, 378, 3276, 20475},
    {1, 29, 406, 3654, 23751},
    {1, 30, 435, 4060, 27405},
    {1, 31, 465, 4495, 31465},
    {1, 32, 496, 4960, 35960},
    {1, 33, 528, 5456, 40920},
    {1, 34, 561, 5984, 46376},
    {1, 35, 595, 6545, 52360},
    {1, 36, 630, 7140, 58905},
    {1, 37, 666, 7770, 66045},
    {1, 38, 703, 8436, 73815},
    {1, 39, 741, 9139, 82251},
    {1, 40, 780, 9880, 91390},
};

static const int GRANDE_WEIGHTS[MUS_GAME_CLASS_COUNT] = {
    1, 10, 100, 1000, 10000, 100000, 1000000, 10000000,
};

static const int CHICA_WEIGHTS[MUS_GAME_CLASS_COUNT] = {
    10000000, 1000000, 100000, 10000, 1000, 100, 10, 1,
};

static const int POINT_VALUES[MUS_GAME_CLASS_COUNT] = {
    1, 4, 5, 6, 7, 10, 10, 10,
};

static const int GAME_VALUES[8] = {
    33, 34, 35, 36, 37, 40, 32, 31,
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
static Status mus_trusted_is_terminal(const void *context,
                                      const GameState *state, bool *result);
static Status mus_trusted_terminal_utility(const void *context,
                                           const GameState *state,
                                           Player player, Utility *result);
static Status mus_trusted_current_actor(const void *context,
                                        const GameState *state, Actor *result);
static Status mus_trusted_legal_actions(const void *context,
                                        const GameState *state,
                                        Action *actions, size_t capacity,
                                        size_t *required_count);
static Status mus_trusted_undo_action(const void *context, GameState *state);
static Status mus_trusted_chance_probability(const void *context,
                                             const GameState *state,
                                             Action action,
                                             Probability *result);
static Status mus_trusted_information_set_key(const void *context,
                                              const GameState *state,
                                              InfoSetKey *result);
static Status mus_trusted_chance_outcomes(
    const void *context, const GameState *state, Action *actions,
    Probability *probabilities, size_t capacity, size_t *required_count);
static Status collect_deal_actions(const MusGameModel *model, Action *actions,
                                   Probability *probabilities,
                                   size_t capacity, size_t *count_out);
static Status mus_single_bet_trusted_legal_actions(
    const void *context, const GameState *state, Action *actions,
    size_t capacity, size_t *required_count);
static Status mus_single_bet_trusted_terminal_utility(
    const void *context, const GameState *state, Player player,
    Utility *result);
static Status mus_single_bet_trusted_apply_action(const void *context,
                                                  GameState *state,
                                                  Action action);
static Status mus_single_bet_trusted_information_set_key(
    const void *context, const GameState *state, InfoSetKey *result);
static Status mus_raises_to_eight_trusted_legal_actions(
    const void *context, const GameState *state, Action *actions,
    size_t capacity, size_t *required_count);
static Status mus_raises_to_eight_trusted_apply_action(const void *context,
                                                       GameState *state,
                                                       Action action);
static Status mus_raises_to_eight_trusted_information_set_key(
    const void *context, const GameState *state, InfoSetKey *result);
static Status mus_ordago_trusted_legal_actions(
    const void *context, const GameState *state, Action *actions,
    size_t capacity, size_t *required_count);
static Status mus_ordago_trusted_apply_action(const void *context,
                                              GameState *state,
                                              Action action);
static Status mus_ordago_trusted_information_set_key(
    const void *context, const GameState *state, InfoSetKey *result);

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

static const GameOperations MUS_SINGLE_BET_TRUSTED_OPERATIONS = {
    .is_terminal = mus_trusted_is_terminal,
    .terminal_utility = mus_single_bet_trusted_terminal_utility,
    .current_actor = mus_trusted_current_actor,
    .legal_actions = mus_single_bet_trusted_legal_actions,
    .apply_action = mus_single_bet_trusted_apply_action,
    .undo_action = mus_trusted_undo_action,
    .chance_probability = mus_trusted_chance_probability,
    .information_set_key = mus_single_bet_trusted_information_set_key,
    .chance_outcomes = mus_trusted_chance_outcomes,
};

static const GameOperations MUS_RAISES_TO_EIGHT_TRUSTED_OPERATIONS = {
    .is_terminal = mus_trusted_is_terminal,
    .terminal_utility = mus_trusted_terminal_utility,
    .current_actor = mus_trusted_current_actor,
    .legal_actions = mus_raises_to_eight_trusted_legal_actions,
    .apply_action = mus_raises_to_eight_trusted_apply_action,
    .undo_action = mus_trusted_undo_action,
    .chance_probability = mus_trusted_chance_probability,
    .information_set_key = mus_raises_to_eight_trusted_information_set_key,
    .chance_outcomes = mus_trusted_chance_outcomes,
};

static const GameOperations MUS_ORDAGO_TRUSTED_OPERATIONS = {
    .is_terminal = mus_trusted_is_terminal,
    .terminal_utility = mus_trusted_terminal_utility,
    .current_actor = mus_trusted_current_actor,
    .legal_actions = mus_ordago_trusted_legal_actions,
    .apply_action = mus_ordago_trusted_apply_action,
    .undo_action = mus_trusted_undo_action,
    .chance_probability = mus_trusted_chance_probability,
    .information_set_key = mus_ordago_trusted_information_set_key,
    .chance_outcomes = mus_trusted_chance_outcomes,
};

static const Game MUS_GAME = {
    .operations = &MUS_OPERATIONS,
    .context = &MUS_SINGLE_BET_FORMAT,
    .strategic_player_count = MUS_GAME_PLAYER_COUNT,
    .max_legal_actions = MUS_GAME_MAX_LEGAL_ACTIONS,
    .strategy_schema_id = "mus.heads-up-single-hand/v1",
    .trusted_operations = &MUS_SINGLE_BET_TRUSTED_OPERATIONS,
};

static const Game MUS_RAISES_TO_EIGHT_GAME = {
    .operations = &MUS_OPERATIONS,
    .context = &MUS_RAISES_TO_EIGHT_FORMAT,
    .strategic_player_count = MUS_GAME_PLAYER_COUNT,
    .max_legal_actions = MUS_GAME_MAX_LEGAL_ACTIONS,
    .strategy_schema_id = "mus.heads-up-single-hand.raises-to-eight/v1",
    .trusted_operations = &MUS_RAISES_TO_EIGHT_TRUSTED_OPERATIONS,
};

static const Game MUS_ORDAGO_GAME = {
    .operations = &MUS_OPERATIONS,
    .context = &MUS_ORDAGO_FORMAT,
    .strategic_player_count = MUS_GAME_PLAYER_COUNT,
    .max_legal_actions = MUS_GAME_MAX_LEGAL_ACTIONS,
    .strategy_schema_id = "mus.heads-up-single-hand.ordago-with-mus/v2",
    .trusted_operations = &MUS_ORDAGO_TRUSTED_OPERATIONS,
};

static MusGameState *as_mus(GameState *state) {
    return (MusGameState *)state;
}

static const MusGameState *as_mus_const(const GameState *state) {
    return (const MusGameState *)state;
}

static const MusGameFormat *game_format(const void *context) {
    if (context == &MUS_SINGLE_BET_FORMAT)
        return &MUS_SINGLE_BET_FORMAT;
    if (context == &MUS_RAISES_TO_EIGHT_FORMAT)
        return &MUS_RAISES_TO_EIGHT_FORMAT;
    if (context == &MUS_ORDAGO_FORMAT)
        return &MUS_ORDAGO_FORMAT;
    return NULL;
}

static bool player_is_valid(Player player) {
    return player == CFR_PLAYER_0 || player == CFR_PLAYER_1;
}

static bool phase_is_deal(MusGamePhase phase) {
    return phase == MUS_PHASE_DEAL_PLAYER_0 ||
           phase == MUS_PHASE_DEAL_PLAYER_1 ||
           phase == MUS_PHASE_REDRAW_PLAYER_0 ||
           phase == MUS_PHASE_REDRAW_PLAYER_1;
}

static bool phase_is_betting(MusGamePhase phase) {
    return phase >= MUS_PHASE_GRANDE && phase <= MUS_PHASE_PUNTO;
}

static int deal_player(const MusGameModel *model) {
    return model->phase == MUS_PHASE_DEAL_PLAYER_0 ||
                   model->phase == MUS_PHASE_REDRAW_PLAYER_0
               ? 0
               : 1;
}

static bool phase_is_mus_decision(MusGamePhase phase) {
    return phase == MUS_PHASE_MUS_PLAYER_0 ||
           phase == MUS_PHASE_MUS_PLAYER_1;
}

static bool phase_is_discard(MusGamePhase phase) {
    return phase == MUS_PHASE_DISCARD_PLAYER_0 ||
           phase == MUS_PHASE_DISCARD_PLAYER_1;
}

static size_t hand_total(const uint8_t hand[MUS_GAME_CLASS_COUNT]) {
    size_t total = 0;

    for (size_t index = 0; index < MUS_GAME_CLASS_COUNT; index += 1)
        total += hand[index];
    return total;
}

static int game_key(int point_value) {
    for (int index = 0; index < 8; index += 1) {
        if (point_value == GAME_VALUES[index])
            return index;
    }
    return -1;
}

static Status summarize_hand(const uint8_t hand[MUS_GAME_CLASS_COUNT],
                             MusHandSummary *summary) {
    enum { SUMMARY_NO_PAIRS, SUMMARY_PAIRS, SUMMARY_TRIPLES, SUMMARY_DUPLEX };
    int pairs_type = SUMMARY_NO_PAIRS;
    int high_pair = 0;
    int low_pair = 0;

    if (hand == NULL || summary == NULL ||
        hand_total(hand) != MUS_GAME_HAND_SIZE) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    *summary = (MusHandSummary){.game_key = -1};
    for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT;
         card_class += 1) {
        const int count = hand[card_class];

        summary->grande_key += count * GRANDE_WEIGHTS[card_class];
        summary->chica_key += count * CHICA_WEIGHTS[card_class];
        summary->point_value += count * POINT_VALUES[card_class];
    }
    for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT;
         card_class += 1) {
        const int count = hand[card_class];

        if (count == 4) {
            pairs_type = SUMMARY_DUPLEX;
            high_pair = card_class + 1;
            low_pair = high_pair;
            break;
        }
        if (count == 3) {
            pairs_type = SUMMARY_TRIPLES;
            high_pair = card_class + 1;
        } else if (count == 2) {
            if (pairs_type == SUMMARY_PAIRS) {
                pairs_type = SUMMARY_DUPLEX;
                low_pair = high_pair;
                high_pair = card_class + 1;
                break;
            }
            pairs_type = SUMMARY_PAIRS;
            high_pair = card_class + 1;
        }
    }
    if (pairs_type == SUMMARY_PAIRS) {
        summary->pairs_key = high_pair;
        summary->pairs_points = 1;
    } else if (pairs_type == SUMMARY_TRIPLES) {
        summary->pairs_key = high_pair * 10;
        summary->pairs_points = 2;
    } else if (pairs_type == SUMMARY_DUPLEX) {
        summary->pairs_key = high_pair * 1000 + low_pair * 100;
        summary->pairs_points = 3;
    }
    if (summary->point_value >= 31) {
        summary->game_key = game_key(summary->point_value);
        summary->game_points = summary->point_value == 31 ? 3 : 2;
        if (summary->game_key < 0)
            return CFR_STATUS_INVALID_ARGUMENT;
    }
    return CFR_STATUS_SUCCESS;
}

static Status mus_single_bet_trusted_legal_actions(
    const void *context, const GameState *state, Action *actions,
    size_t capacity, size_t *required_count) {
    const MusGameState *mus = as_mus_const(state);

    (void)context;
    if (mus == NULL || actions == NULL || required_count == NULL ||
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
    if (!phase_is_betting(mus->model.phase))
        return CFR_STATUS_INVALID_ARGUMENT;
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

static Status mus_raises_to_eight_trusted_legal_actions(
    const void *context, const GameState *state, Action *actions,
    size_t capacity, size_t *required_count) {
    (void)context;
    return mus_trusted_legal_actions(&MUS_RAISES_TO_EIGHT_FORMAT, state,
                                     actions, capacity, required_count);
}

static Status mus_ordago_trusted_legal_actions(
    const void *context, const GameState *state, Action *actions,
    size_t capacity, size_t *required_count) {
    (void)context;
    return mus_trusted_legal_actions(&MUS_ORDAGO_FORMAT, state, actions,
                                     capacity, required_count);
}

static Status summarize_hands(
    const MusGameModel *model,
    MusHandSummary summaries[MUS_GAME_PLAYER_COUNT]) {
    MusHandSummary temporary[MUS_GAME_PLAYER_COUNT];

    if (model == NULL || summaries == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    for (int player = 0; player < MUS_GAME_PLAYER_COUNT; player += 1) {
        Status status = summarize_hand(model->hands[player],
                                       &temporary[player]);

        if (status != CFR_STATUS_SUCCESS)
            return status;
    }
    memcpy(summaries, temporary, sizeof(temporary));
    return CFR_STATUS_SUCCESS;
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
    model->current_wager = 0;
    model->previous_wager = 0;
    model->current_wager_is_ordago = false;
    model->current_strength = MUS_LANCE_STRENGTH_NONE;
}

static void start_mus_round(MusGameModel *model) {
    model->phase = MUS_PHASE_MUS_PLAYER_0;
    model->current_player = CFR_PLAYER_0;
    model->mus_stopped = false;
    model->mus_stop_player = CFR_PLAYER_0;
}

static void prepare_final_lance(
    MusGameModel *model,
    const MusHandSummary summaries[MUS_GAME_PLAYER_COUNT]) {
    model->game_revealed = true;
    model->game[0] = summaries[0].game_key >= 0;
    model->game[1] = summaries[1].game_key >= 0;

    if (model->game[0] && model->game[1]) {
        start_betting(model, MUS_PHASE_JUEGO);
    } else if (!model->game[0] && !model->game[1]) {
        start_betting(model, MUS_PHASE_PUNTO);
    } else {
        model->outcomes[3] = MUS_LANCE_SKIPPED;
        model->phase = MUS_PHASE_TERMINAL;
    }
}

static void advance_lance(
    MusGameModel *model, MusLanceOutcome outcome,
    const MusHandSummary summaries[MUS_GAME_PLAYER_COUNT]) {
    const MusGamePhase completed_phase = model->phase;
    const int lance = phase_lance_index(completed_phase);

    if (lance >= 0) {
        model->outcomes[lance] = outcome;
        model->outcome_wagers[lance] = model->current_wager;
        model->outcome_previous_wagers[lance] = model->previous_wager;
        model->outcome_wager_is_ordago[lance] =
            model->current_wager_is_ordago;
        model->outcome_strengths[lance] = model->current_strength;
    }
    if (completed_phase == MUS_PHASE_GRANDE) {
        start_betting(model, MUS_PHASE_CHICA);
    } else if (completed_phase == MUS_PHASE_CHICA) {
        model->pairs_revealed = true;
        model->pairs[0] = summaries[0].pairs_key > 0;
        model->pairs[1] = summaries[1].pairs_key > 0;
        if (model->pairs[0] && model->pairs[1]) {
            start_betting(model, MUS_PHASE_PARES);
        } else {
            model->outcomes[2] = MUS_LANCE_SKIPPED;
            prepare_final_lance(model, summaries);
        }
    } else if (completed_phase == MUS_PHASE_PARES) {
        prepare_final_lance(model, summaries);
    } else if (completed_phase == MUS_PHASE_JUEGO ||
               completed_phase == MUS_PHASE_PUNTO) {
        model->phase = MUS_PHASE_TERMINAL;
    }
}

static uint64_t combinations(int n, int k) {
    if (n < 0 || n > MUS_DECK_SIZE || k < 0 || k > MUS_GAME_HAND_SIZE ||
        k > n) {
        return 0;
    }
    return BINOMIAL_COEFFICIENTS[n][k];
}

static int available_in_class(const MusGameModel *model, int player,
                              int card_class) {
    (void)player;
    return CLASS_CAPACITIES[card_class] - model->hands[0][card_class] -
           model->hands[1][card_class] - model->out_of_play[card_class];
}

static int expected_later_available(const MusGameModel *model) {
    const int player = deal_player(model);
    int available = 0;

    for (int index = model->deal_class + 1; index < MUS_GAME_CLASS_COUNT;
         index += 1) {
        available += available_in_class(model, player, index);
    }
    return available;
}

static bool deal_count_is_legal_for_counts(int count, int remaining,
                                           int current_available,
                                           int later_available) {
    return count >= 0 && count <= remaining &&
           count <= current_available &&
           remaining - count <= later_available;
}

static bool deal_count_is_legal(const MusGameModel *model, int count) {
    const int player = deal_player(model);

    return deal_count_is_legal_for_counts(
        count, model->deal_remaining,
        available_in_class(model, player, model->deal_class),
        model->deal_later_available);
}

static Probability deal_count_probability_for_counts(
    int count, int remaining, int current_available, int later_available) {
    const uint64_t denominator =
        combinations(current_available + later_available, remaining);
    const uint64_t numerator = combinations(current_available, count) *
                               combinations(later_available,
                                            remaining - count);

    if (denominator == 0)
        return 0.0;
    return (Probability)numerator / (Probability)denominator;
}

static Probability deal_count_probability(const MusGameModel *model,
                                           int count) {
    const int player = deal_player(model);

    return deal_count_probability_for_counts(
        count, model->deal_remaining,
        available_in_class(model, player, model->deal_class),
        model->deal_later_available);
}

static Status collect_deal_actions(const MusGameModel *model, Action *actions,
                                   Probability *probabilities,
                                   size_t capacity, size_t *count_out) {
    const int player = deal_player(model);
    const int remaining = model->deal_remaining;
    const int current_available =
        available_in_class(model, player, model->deal_class);
    const int later_available = model->deal_later_available;
    size_t count = 0;

    for (int candidate = 0; candidate <= MUS_GAME_HAND_SIZE; candidate += 1) {
        if (!deal_count_is_legal_for_counts(candidate, remaining,
                                            current_available,
                                            later_available)) {
            continue;
        }
        if (count == capacity)
            return CFR_STATUS_BUFFER_TOO_SMALL;
        actions[count] = candidate;
        if (probabilities != NULL) {
            probabilities[count] = deal_count_probability_for_counts(
                candidate, remaining, current_available, later_available);
        }
        count += 1;
    }
    *count_out = count;
    return count == 0 ? CFR_STATUS_INVALID_ARGUMENT : CFR_STATUS_SUCCESS;
}

static bool wager_is_valid(const MusGameFormat *format, uint8_t wager) {
    if (format == NULL)
        return false;
    if (format->ordago_with_mus)
        return wager >= MUS_ORDAGO_MIN_WAGER && wager <= MUS_ORDAGO_WAGER;
    if (!format->raises_to_eight)
        return wager == MUS_SINGLE_BET_WAGER;
    return wager == 2 || wager == 4 || wager == 8;
}

static bool ordago_increment_is_valid(uint8_t increment) {
    return increment == 2 || increment == 4 || increment == 7 ||
           increment == 10;
}

static bool ordago_offer_is_valid(uint8_t wager, uint8_t previous_wager,
                                  bool is_ordago) {
    if (is_ordago) {
        return wager == MUS_ORDAGO_WAGER && previous_wager >= 1 &&
               previous_wager <= MUS_ORDAGO_WAGER;
    }
    if (previous_wager == 1)
        return ordago_increment_is_valid(wager);
    return previous_wager >= MUS_ORDAGO_MIN_WAGER &&
           previous_wager < wager && wager <= MUS_ORDAGO_WAGER &&
           ordago_increment_is_valid((uint8_t)(wager - previous_wager));
}

static bool outcome_wager_is_valid(const MusGameFormat *format,
                                   MusLanceOutcome outcome,
                                   uint8_t wager, uint8_t previous_wager,
                                   bool is_ordago,
                                   MusLanceStrength strength) {
    const bool has_bet = outcome == MUS_LANCE_FIRST_BET_REJECTED ||
                         outcome == MUS_LANCE_FIRST_BET_ACCEPTED ||
                         outcome == MUS_LANCE_SECOND_BET_REJECTED ||
                         outcome == MUS_LANCE_SECOND_BET_ACCEPTED;
    const bool accepted = outcome == MUS_LANCE_FIRST_BET_ACCEPTED ||
                          outcome == MUS_LANCE_SECOND_BET_ACCEPTED;
    const int bettor =
        outcome == MUS_LANCE_FIRST_BET_REJECTED ||
                outcome == MUS_LANCE_FIRST_BET_ACCEPTED
            ? 0
            : 1;

    if (!has_bet) {
        return wager == 0 && previous_wager == 0 && !is_ordago &&
               strength == MUS_LANCE_STRENGTH_NONE;
    }
    if (!format->ordago_with_mus) {
        return wager_is_valid(format, wager) && previous_wager == 0 &&
               !is_ordago && strength == MUS_LANCE_STRENGTH_NONE;
    }
    if (!ordago_offer_is_valid(wager, previous_wager, is_ordago) ||
        strength < MUS_LANCE_STRENGTH_PLAYER_0 ||
        strength > MUS_LANCE_STRENGTH_BOTH ||
        ((int)strength & (1 << bettor)) == 0) {
        return false;
    }
    if (accepted)
        return strength == MUS_LANCE_STRENGTH_BOTH;
    if (!has_bet)
        return false;
    return true;
}

static Status validate_model(const MusGameModel *model,
                             const MusGameFormat *format) {
    size_t out_of_play_total = 0;
    size_t discard_total = 0;

    if (model == NULL || format == NULL ||
        model->phase < MUS_PHASE_DEAL_PLAYER_0 ||
        model->phase > MUS_PHASE_REDRAW_PLAYER_1 ||
        !player_is_valid(model->current_player) ||
        !player_is_valid(model->bettor)) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT;
         card_class += 1) {
        if ((int)model->hands[0][card_class] +
                (int)model->hands[1][card_class] +
                (int)model->out_of_play[card_class] >
            CLASS_CAPACITIES[card_class]) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
        out_of_play_total += model->out_of_play[card_class];
    }
    for (int lance = 0; lance < MUS_GAME_LANCE_COUNT; lance += 1) {
        if (model->outcomes[lance] < MUS_LANCE_UNRESOLVED ||
            model->outcomes[lance] > MUS_LANCE_SKIPPED ||
            !outcome_wager_is_valid(
                format, model->outcomes[lance],
                model->outcome_wagers[lance],
                model->outcome_previous_wagers[lance],
                model->outcome_wager_is_ordago[lance],
                model->outcome_strengths[lance])) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
    }
    if (model->mus_count > MUS_GAME_MAX_MUSES)
        return CFR_STATUS_INVALID_ARGUMENT;
    for (int round = 0; round < MUS_GAME_MAX_MUSES; round += 1) {
        for (int player = 0; player < MUS_GAME_PLAYER_COUNT; player += 1) {
            const uint8_t mask = model->discard_masks[round][player];
            unsigned int count = 0;

            if (mask >= MUS_GAME_DISCARD_MASK_COUNT ||
                model->discard_counts[round][player] > MUS_GAME_HAND_SIZE) {
                return CFR_STATUS_INVALID_ARGUMENT;
            }
            for (int bit = 0; bit < MUS_GAME_HAND_SIZE; bit += 1)
                count += (mask >> bit) & 1U;
            if (count != model->discard_counts[round][player])
                return CFR_STATUS_INVALID_ARGUMENT;
            discard_total += model->discard_counts[round][player];
        }
    }
    if (out_of_play_total != discard_total)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (format->ordago_with_mus && model->mus_stopped &&
        !phase_is_betting(model->phase) &&
        model->phase != MUS_PHASE_TERMINAL)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (!format->ordago_with_mus) {
        if (model->mus_count != 0 || model->mus_stopped ||
            model->current_wager_is_ordago ||
            model->current_strength != MUS_LANCE_STRENGTH_NONE ||
            model->previous_wager != 0)
            return CFR_STATUS_INVALID_ARGUMENT;
        for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT;
             card_class += 1) {
            if (model->out_of_play[card_class] != 0)
                return CFR_STATUS_INVALID_ARGUMENT;
        }
        for (int round = 0; round < MUS_GAME_MAX_MUSES; round += 1) {
            for (int player = 0; player < MUS_GAME_PLAYER_COUNT; player += 1) {
                if (model->discard_masks[round][player] != 0 ||
                    model->discard_counts[round][player] != 0)
                    return CFR_STATUS_INVALID_ARGUMENT;
            }
        }
        if (phase_is_mus_decision(model->phase) ||
            phase_is_discard(model->phase) ||
            model->phase == MUS_PHASE_REDRAW_PLAYER_0 ||
            model->phase == MUS_PHASE_REDRAW_PLAYER_1) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
    }
    if (phase_is_deal(model->phase)) {
        const int player = deal_player(model);
        const size_t total = hand_total(model->hands[player]);
        const bool redraw = model->phase == MUS_PHASE_REDRAW_PLAYER_0 ||
                            model->phase == MUS_PHASE_REDRAW_PLAYER_1;

        if (model->deal_class >= MUS_GAME_CLASS_COUNT - 1 ||
            model->deal_remaining > MUS_GAME_HAND_SIZE ||
            total + model->deal_remaining != MUS_GAME_HAND_SIZE ||
            model->deal_later_available != expected_later_available(model) ||
            (player == 1 && hand_total(model->hands[0]) !=
                                MUS_GAME_HAND_SIZE) ||
            (!redraw && model->mus_count != 0)) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
        return CFR_STATUS_SUCCESS;
    }
    if (phase_is_discard(model->phase)) {
        if (!format->ordago_with_mus ||
            model->mus_count >= MUS_GAME_MAX_MUSES ||
            (model->phase == MUS_PHASE_DISCARD_PLAYER_0 &&
             (model->current_player != CFR_PLAYER_0 ||
              hand_total(model->hands[0]) != MUS_GAME_HAND_SIZE ||
              hand_total(model->hands[1]) != MUS_GAME_HAND_SIZE)) ||
            (model->phase == MUS_PHASE_DISCARD_PLAYER_1 &&
             (model->current_player != CFR_PLAYER_1 ||
              hand_total(model->hands[0]) +
                      model->discard_counts[model->mus_count][0] !=
                  MUS_GAME_HAND_SIZE ||
              hand_total(model->hands[1]) != MUS_GAME_HAND_SIZE))) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
        return CFR_STATUS_SUCCESS;
    }
    if (hand_total(model->hands[0]) != MUS_GAME_HAND_SIZE ||
        hand_total(model->hands[1]) != MUS_GAME_HAND_SIZE) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    if (phase_is_mus_decision(model->phase)) {
        if (!format->ordago_with_mus || model->mus_stopped ||
            model->mus_count >= MUS_GAME_MAX_MUSES ||
            (model->phase == MUS_PHASE_MUS_PLAYER_0 &&
             model->current_player != CFR_PLAYER_0) ||
            (model->phase == MUS_PHASE_MUS_PLAYER_1 &&
             model->current_player != CFR_PLAYER_1)) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
        return CFR_STATUS_SUCCESS;
    }
    if (format->ordago_with_mus &&
        (phase_is_betting(model->phase) ||
         model->phase == MUS_PHASE_TERMINAL) &&
        ((model->mus_count < MUS_GAME_MAX_MUSES && !model->mus_stopped) ||
         (model->mus_count == MUS_GAME_MAX_MUSES && model->mus_stopped))) {
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
        if (model->bet_stage == MUS_BET_RESPONSE) {
            const MusLanceStrength bettor_strength =
                model->bettor == CFR_PLAYER_0
                    ? MUS_LANCE_STRENGTH_PLAYER_0
                    : MUS_LANCE_STRENGTH_PLAYER_1;

            if (model->current_player == model->bettor ||
                !wager_is_valid(format, model->current_wager)) {
                return CFR_STATUS_INVALID_ARGUMENT;
            }
            if (format->ordago_with_mus) {
                if (!ordago_offer_is_valid(
                        model->current_wager, model->previous_wager,
                        model->current_wager_is_ordago) ||
                    (model->previous_wager == 1 &&
                     model->current_strength != bettor_strength) ||
                    (model->previous_wager > 1 &&
                     model->current_strength != MUS_LANCE_STRENGTH_BOTH)) {
                    return CFR_STATUS_INVALID_ARGUMENT;
                }
            } else if (model->previous_wager != 0 ||
                       model->current_wager_is_ordago ||
                       model->current_strength !=
                           MUS_LANCE_STRENGTH_NONE) {
                return CFR_STATUS_INVALID_ARGUMENT;
            }
        }
        if (model->bet_stage != MUS_BET_RESPONSE &&
            (model->current_wager != 0 || model->previous_wager != 0 ||
             model->current_wager_is_ordago ||
             model->current_strength != MUS_LANCE_STRENGTH_NONE)) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
        if (model->bet_stage < MUS_BET_FIRST_TO_ACT ||
            model->bet_stage > MUS_BET_RESPONSE) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
    }
    return CFR_STATUS_SUCCESS;
}

static bool summaries_are_equal(const MusHandSummary *left,
                                const MusHandSummary *right) {
    return left->grande_key == right->grande_key &&
           left->chica_key == right->chica_key &&
           left->pairs_key == right->pairs_key &&
           left->pairs_points == right->pairs_points &&
           left->game_key == right->game_key &&
           left->game_points == right->game_points &&
           left->point_value == right->point_value;
}

static Status validate_hand_summaries(
    const MusGameModel *model,
    const MusHandSummary summaries[MUS_GAME_PLAYER_COUNT]) {
    MusHandSummary expected[MUS_GAME_PLAYER_COUNT];

    if (summarize_hands(model, expected) != CFR_STATUS_SUCCESS)
        return CFR_STATUS_INVALID_ARGUMENT;
    for (int player = 0; player < MUS_GAME_PLAYER_COUNT; player += 1) {
        if (!summaries_are_equal(&summaries[player], &expected[player]))
            return CFR_STATUS_INVALID_ARGUMENT;
    }
    return CFR_STATUS_SUCCESS;
}

static Status validate_state_data(const MusGameState *state,
                                  const MusGameFormat *format) {
    Status status;

    if (state == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    status = validate_model(&state->model, format);
    if (status != CFR_STATUS_SUCCESS || phase_is_deal(state->model.phase) ||
        phase_is_discard(state->model.phase))
        return status;
    return validate_hand_summaries(&state->model, state->hand_summaries);
}

Status mus_game_state_init(MusGameState *state) {
    if (state == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    *state = (MusGameState){0};
    state->model.phase = MUS_PHASE_DEAL_PLAYER_0;
    state->model.deal_class = 0;
    state->model.deal_remaining = MUS_GAME_HAND_SIZE;
    state->model.deal_later_available =
        MUS_DECK_SIZE - CLASS_CAPACITIES[0];
    state->model.current_player = CFR_PLAYER_0;
    state->model.bettor = CFR_PLAYER_0;
    return CFR_STATUS_SUCCESS;
}

const Game *mus_game_descriptor(void) {
    return &MUS_GAME;
}

Status mus_single_bet_game_state_init(MusSingleBetGameState *state) {
    return mus_game_state_init(state);
}

const Game *mus_single_bet_game_descriptor(void) {
    return &MUS_GAME;
}

Status mus_raises_to_eight_game_state_init(MusRaisesToEightGameState *state) {
    return mus_game_state_init(state);
}

const Game *mus_raises_to_eight_game_descriptor(void) {
    return &MUS_RAISES_TO_EIGHT_GAME;
}

Status mus_ordago_game_state_init(MusOrdagoGameState *state) {
    return mus_game_state_init(state);
}

const Game *mus_ordago_game_descriptor(void) {
    return &MUS_ORDAGO_GAME;
}

GameState *mus_game_state_as_game_state(MusGameState *state) {
    return (GameState *)state;
}

const GameState *mus_game_state_as_game_state_const(const MusGameState *state) {
    return (const GameState *)state;
}

GameState *mus_single_bet_game_state_as_game_state(
    MusSingleBetGameState *state) {
    return mus_game_state_as_game_state(state);
}

const GameState *mus_single_bet_game_state_as_game_state_const(
    const MusSingleBetGameState *state) {
    return mus_game_state_as_game_state_const(state);
}

GameState *mus_raises_to_eight_game_state_as_game_state(
    MusRaisesToEightGameState *state) {
    return mus_game_state_as_game_state(state);
}

const GameState *mus_raises_to_eight_game_state_as_game_state_const(
    const MusRaisesToEightGameState *state) {
    return mus_game_state_as_game_state_const(state);
}

GameState *mus_ordago_game_state_as_game_state(MusOrdagoGameState *state) {
    return mus_game_state_as_game_state(state);
}

const GameState *mus_ordago_game_state_as_game_state_const(
    const MusOrdagoGameState *state) {
    return mus_game_state_as_game_state_const(state);
}

static Status mus_is_terminal(const void *context, const GameState *state,
                              bool *result) {
    const MusGameState *mus = as_mus_const(state);

    if (result == NULL ||
        validate_state_data(mus, game_format(context)) !=
            CFR_STATUS_SUCCESS) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    return mus_trusted_is_terminal(context, state, result);
}

static Status mus_trusted_is_terminal(const void *context,
                                      const GameState *state, bool *result) {
    const MusGameState *mus = as_mus_const(state);

    (void)context;
    if (result == NULL || mus == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
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

static int outcome_stake(const MusGameModel *model,
                         const MusGameFormat *format, int lance,
                         MusLanceOutcome outcome) {
    const int wager = model->outcome_wagers[lance];

    if (outcome_is_accepted(outcome))
        return wager;
    if (outcome_is_rejected(outcome) && format->ordago_with_mus) {
        const int previous = model->outcome_previous_wagers[lance];

        return previous == 0 ? 1 : previous;
    }
    if (outcome_is_rejected(outcome))
        return wager == MUS_SINGLE_BET_WAGER ? 1 : wager / 2;
    return 0;
}

static Status score_terminal(const MusGameState *mus,
                             const MusGameFormat *format, int scores[2]) {
    const MusGameModel *model;
    const MusHandSummary *summaries;
    int winner;
    MusLanceOutcome outcome;

    if (mus == NULL || format == NULL || scores == NULL ||
        mus->model.phase != MUS_PHASE_TERMINAL) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    model = &mus->model;
    summaries = mus->hand_summaries;
    scores[0] = 0;
    scores[1] = 0;

    outcome = model->outcomes[0];
    if (outcome_is_rejected(outcome)) {
        scores[outcome_bettor(outcome)] +=
            outcome_stake(model, format, 0, outcome);
    } else {
        winner = summaries[1].grande_key > summaries[0].grande_key ? 1 : 0;
        scores[winner] +=
            outcome_is_accepted(outcome)
                ? outcome_stake(model, format, 0, outcome)
                                         : 1;
    }

    outcome = model->outcomes[1];
    if (outcome_is_rejected(outcome)) {
        scores[outcome_bettor(outcome)] +=
            outcome_stake(model, format, 1, outcome);
    } else {
        winner = summaries[1].chica_key > summaries[0].chica_key ? 1 : 0;
        scores[winner] +=
            outcome_is_accepted(outcome)
                ? outcome_stake(model, format, 1, outcome)
                                         : 1;
    }

    outcome = model->outcomes[2];
    if (model->pairs[0] || model->pairs[1]) {
        if (outcome_is_rejected(outcome)) {
            winner = outcome_bettor(outcome);
            scores[winner] += outcome_stake(model, format, 2, outcome);
        } else if (model->pairs[0] && model->pairs[1]) {
            winner = summaries[1].pairs_key > summaries[0].pairs_key ? 1 : 0;
        } else {
            winner = model->pairs[0] ? 0 : 1;
        }
        scores[winner] += summaries[winner].pairs_points;
        if (outcome_is_accepted(outcome))
            scores[winner] += outcome_stake(model, format, 2, outcome);
    }

    outcome = model->outcomes[3];
    if (model->game[0] || model->game[1]) {
        if (outcome_is_rejected(outcome)) {
            winner = outcome_bettor(outcome);
            scores[winner] += outcome_stake(model, format, 3, outcome);
        } else if (model->game[0] && model->game[1]) {
            winner = summaries[1].game_key > summaries[0].game_key ? 1 : 0;
        } else {
            winner = model->game[0] ? 0 : 1;
        }
        scores[winner] += summaries[winner].game_points;
        if (outcome_is_accepted(outcome))
            scores[winner] += outcome_stake(model, format, 3, outcome);
    } else {
        if (outcome_is_rejected(outcome)) {
            winner = outcome_bettor(outcome);
            scores[winner] +=
                1 + outcome_stake(model, format, 3, outcome);
        } else {
            winner = summaries[1].point_value > summaries[0].point_value
                         ? 1
                         : 0;
            scores[winner] +=
                1 + (outcome_is_accepted(outcome)
                         ? outcome_stake(model, format, 3, outcome)
                         : 0);
        }
    }
    return CFR_STATUS_SUCCESS;
}

static Status score_terminal_single_bet(const MusGameState *mus,
                                        int scores[2]) {
    const MusGameModel *model;
    const MusHandSummary *summaries;
    int winner;
    MusLanceOutcome outcome;

    if (mus == NULL || scores == NULL ||
        mus->model.phase != MUS_PHASE_TERMINAL) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    model = &mus->model;
    summaries = mus->hand_summaries;
    scores[0] = 0;
    scores[1] = 0;

    outcome = model->outcomes[0];
    if (outcome_is_rejected(outcome)) {
        scores[outcome_bettor(outcome)] += 1;
    } else {
        winner = summaries[1].grande_key > summaries[0].grande_key ? 1 : 0;
        scores[winner] += outcome_is_accepted(outcome) ? 2 : 1;
    }

    outcome = model->outcomes[1];
    if (outcome_is_rejected(outcome)) {
        scores[outcome_bettor(outcome)] += 1;
    } else {
        winner = summaries[1].chica_key > summaries[0].chica_key ? 1 : 0;
        scores[winner] += outcome_is_accepted(outcome) ? 2 : 1;
    }

    outcome = model->outcomes[2];
    if (model->pairs[0] || model->pairs[1]) {
        if (outcome_is_rejected(outcome)) {
            winner = outcome_bettor(outcome);
            scores[winner] += 1;
        } else if (model->pairs[0] && model->pairs[1]) {
            winner = summaries[1].pairs_key > summaries[0].pairs_key ? 1 : 0;
        } else {
            winner = model->pairs[0] ? 0 : 1;
        }
        scores[winner] += summaries[winner].pairs_points;
        if (outcome_is_accepted(outcome))
            scores[winner] += 2;
    }

    outcome = model->outcomes[3];
    if (model->game[0] || model->game[1]) {
        if (outcome_is_rejected(outcome)) {
            winner = outcome_bettor(outcome);
            scores[winner] += 1;
        } else if (model->game[0] && model->game[1]) {
            winner = summaries[1].game_key > summaries[0].game_key ? 1 : 0;
        } else {
            winner = model->game[0] ? 0 : 1;
        }
        scores[winner] += summaries[winner].game_points;
        if (outcome_is_accepted(outcome))
            scores[winner] += 2;
    } else if (outcome_is_rejected(outcome)) {
        winner = outcome_bettor(outcome);
        scores[winner] += 2;
    } else {
        winner = summaries[1].point_value > summaries[0].point_value ? 1 : 0;
        scores[winner] += 1 + (outcome_is_accepted(outcome) ? 2 : 0);
    }
    return CFR_STATUS_SUCCESS;
}

static Status mus_terminal_utility(const void *context,
                                   const GameState *state, Player player,
                                   Utility *result) {
    const MusGameState *mus = as_mus_const(state);

    if (result == NULL || !player_is_valid(player) ||
        validate_state_data(mus, game_format(context)) !=
            CFR_STATUS_SUCCESS) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    return mus_trusted_terminal_utility(context, state, player, result);
}

static Status mus_trusted_terminal_utility(const void *context,
                                           const GameState *state,
                                           Player player, Utility *result) {
    const MusGameState *mus = as_mus_const(state);
    int scores[2];
    Status status;

    if (mus == NULL || result == NULL || !player_is_valid(player) ||
        mus->model.phase != MUS_PHASE_TERMINAL ||
        game_format(context) == NULL) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    status = score_terminal(mus, game_format(context), scores);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    *result = player == CFR_PLAYER_0 ? (Utility)(scores[0] - scores[1])
                                    : (Utility)(scores[1] - scores[0]);
    return CFR_STATUS_SUCCESS;
}

static Status mus_single_bet_trusted_terminal_utility(
    const void *context, const GameState *state, Player player,
    Utility *result) {
    const MusGameState *mus = as_mus_const(state);
    int scores[2];
    Status status;

    (void)context;
    if (mus == NULL || result == NULL || !player_is_valid(player) ||
        mus->model.phase != MUS_PHASE_TERMINAL) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    status = score_terminal_single_bet(mus, scores);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    *result = player == CFR_PLAYER_0 ? (Utility)(scores[0] - scores[1])
                                    : (Utility)(scores[1] - scores[0]);
    return CFR_STATUS_SUCCESS;
}

static Status mus_current_actor(const void *context, const GameState *state,
                                Actor *result) {
    const MusGameState *mus = as_mus_const(state);

    if (result == NULL ||
        validate_state_data(mus, game_format(context)) !=
            CFR_STATUS_SUCCESS) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    return mus_trusted_current_actor(context, state, result);
}

static Status mus_trusted_current_actor(const void *context,
                                        const GameState *state,
                                        Actor *result) {
    const MusGameState *mus = as_mus_const(state);

    (void)context;
    if (mus == NULL || result == NULL ||
        mus->model.phase == MUS_PHASE_TERMINAL) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    if (phase_is_deal(mus->model.phase)) {
        *result = (Actor){.kind = CFR_ACTOR_CHANCE, .player = CFR_PLAYER_0};
    } else if (phase_is_betting(mus->model.phase) ||
               phase_is_mus_decision(mus->model.phase) ||
               phase_is_discard(mus->model.phase)) {
        *result = (Actor){.kind = CFR_ACTOR_PLAYER,
                          .player = mus->model.current_player};
    } else {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    return CFR_STATUS_SUCCESS;
}

static Status mus_legal_actions(const void *context, const GameState *state,
                                Action *actions, size_t capacity,
                                size_t *required_count) {
    const MusGameState *mus = as_mus_const(state);

    if (actions == NULL || required_count == NULL ||
        validate_state_data(mus, game_format(context)) !=
            CFR_STATUS_SUCCESS) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    return mus_trusted_legal_actions(context, state, actions, capacity,
                                     required_count);
}

static Status mus_trusted_legal_actions(const void *context,
                                        const GameState *state,
                                        Action *actions, size_t capacity,
                                        size_t *required_count) {
    const MusGameState *mus = as_mus_const(state);

    if (mus == NULL || actions == NULL || required_count == NULL ||
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
    const MusGameFormat *format = game_format(context);
    if (format != NULL && format->ordago_with_mus &&
        phase_is_mus_decision(mus->model.phase)) {
        *required_count = 2;
        if (capacity < 2)
            return CFR_STATUS_BUFFER_TOO_SMALL;
        actions[0] = MUS_ACTION_MUS;
        actions[1] = MUS_ACTION_NO_MUS;
        return CFR_STATUS_SUCCESS;
    }
    if (format != NULL && format->ordago_with_mus &&
        phase_is_discard(mus->model.phase)) {
        *required_count = MUS_GAME_DISCARD_MASK_COUNT;
        if (capacity < MUS_GAME_DISCARD_MASK_COUNT)
            return CFR_STATUS_BUFFER_TOO_SMALL;
        for (int mask = 0; mask < MUS_GAME_DISCARD_MASK_COUNT; mask += 1)
            actions[mask] = MUS_ACTION_DISCARD_MASK(mask);
        return CFR_STATUS_SUCCESS;
    }
    if (!phase_is_betting(mus->model.phase))
        return CFR_STATUS_INVALID_ARGUMENT;
    if (format != NULL && format->ordago_with_mus) {
        Action temporary[7];
        size_t count = 0;

        if (mus->model.bet_stage == MUS_BET_RESPONSE) {
            temporary[count++] = MUS_ACTION_REJECT;
            temporary[count++] = MUS_ACTION_ACCEPT;
            static const Action wager_actions[4] = {
                MUS_ACTION_BET_TWO, MUS_ACTION_BET_FOUR,
                MUS_ACTION_BET_SEVEN, MUS_ACTION_BET_TEN,
            };
            static const uint8_t increments[4] = {2, 4, 7, 10};

            if (!mus->model.current_wager_is_ordago) {
                for (int index = 0; index < 4; index += 1) {
                    if ((int)mus->model.current_wager + increments[index] <=
                        MUS_ORDAGO_WAGER) {
                        temporary[count++] = wager_actions[index];
                    }
                }
                temporary[count++] = MUS_ACTION_ORDAGO;
            }
        } else {
            static const Action opening_actions[6] = {
                MUS_ACTION_PASS,      MUS_ACTION_BET_TWO,
                MUS_ACTION_BET_FOUR,  MUS_ACTION_BET_SEVEN,
                MUS_ACTION_BET_TEN,   MUS_ACTION_ORDAGO,
            };

            count = 6;
            memcpy(temporary, opening_actions, sizeof(opening_actions));
        }
        *required_count = count;
        if (capacity < count)
            return CFR_STATUS_BUFFER_TOO_SMALL;
        memcpy(actions, temporary, count * sizeof(*actions));
        return CFR_STATUS_SUCCESS;
    }
    const bool can_raise =
        mus->model.bet_stage == MUS_BET_RESPONSE && format != NULL &&
        format->raises_to_eight &&
        mus->model.current_wager < MUS_RAISES_TO_EIGHT_MAX_WAGER;
    const size_t count = can_raise ? 3 : 2;

    *required_count = count;
    if (capacity < count)
        return CFR_STATUS_BUFFER_TOO_SMALL;
    if (mus->model.bet_stage == MUS_BET_RESPONSE) {
        actions[0] = MUS_ACTION_REJECT;
        actions[1] = MUS_ACTION_ACCEPT;
        if (can_raise)
            actions[2] = MUS_ACTION_RAISE_DOUBLE;
    } else {
        actions[0] = MUS_ACTION_PASS;
        actions[1] = MUS_ACTION_BET_TWO;
    }
    return CFR_STATUS_SUCCESS;
}

static uint8_t ordago_wager_for_action(Action action) {
    switch (action) {
    case MUS_ACTION_BET_TWO:
        return 2;
    case MUS_ACTION_BET_FOUR:
        return 4;
    case MUS_ACTION_BET_SEVEN:
        return 7;
    case MUS_ACTION_BET_TEN:
        return 10;
    case MUS_ACTION_ORDAGO:
        return MUS_ORDAGO_WAGER;
    default:
        return 0;
    }
}

static bool player_action_is_legal(const MusGameModel *model,
                                   const MusGameFormat *format,
                                   Action action) {
    if (format != NULL && format->ordago_with_mus) {
        const uint8_t amount = ordago_wager_for_action(action);

        if (model->bet_stage == MUS_BET_RESPONSE) {
            return action == MUS_ACTION_REJECT ||
                   action == MUS_ACTION_ACCEPT ||
                   (!model->current_wager_is_ordago &&
                    ((action == MUS_ACTION_ORDAGO) ||
                     (ordago_increment_is_valid(amount) &&
                      (int)model->current_wager + amount <=
                          MUS_ORDAGO_WAGER)));
        }
        return action == MUS_ACTION_PASS || action == MUS_ACTION_ORDAGO ||
               ordago_increment_is_valid(amount);
    }
    if (model->bet_stage == MUS_BET_RESPONSE) {
        return action == MUS_ACTION_REJECT || action == MUS_ACTION_ACCEPT ||
               (action == MUS_ACTION_RAISE_DOUBLE && format != NULL &&
                format->raises_to_eight &&
                model->current_wager < MUS_RAISES_TO_EIGHT_MAX_WAGER);
    }
    return action == MUS_ACTION_PASS || action == MUS_ACTION_BET_TWO;
}

static unsigned int discard_from_canonical_hand(
    MusGameModel *model, int player, unsigned int mask) {
    unsigned int position = 0;
    unsigned int discarded = 0;

    for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT;
         card_class += 1) {
        const unsigned int original_count = model->hands[player][card_class];

        for (unsigned int copy = 0; copy < original_count; copy += 1) {
            if ((mask & (1U << position)) != 0) {
                model->hands[player][card_class] -= 1;
                model->out_of_play[card_class] += 1;
                discarded += 1;
            }
            position += 1;
        }
    }
    return discarded;
}

static void finish_mus_round(MusGameModel *model) {
    model->deal_remaining = 0;
    model->deal_class = 0;
    model->deal_later_available = 0;
    if (model->mus_count < MUS_GAME_MAX_MUSES)
        start_mus_round(model);
    else
        start_betting(model, MUS_PHASE_GRANDE);
}

static void start_redraw(MusGameModel *model, int player) {
    const int round = (int)model->mus_count - 1;

    model->phase = player == 0 ? MUS_PHASE_REDRAW_PLAYER_0
                               : MUS_PHASE_REDRAW_PLAYER_1;
    model->current_player = (Player)player;
    model->deal_class = 0;
    model->deal_remaining = model->discard_counts[round][player];
    model->deal_later_available =
        (uint8_t)expected_later_available(model);
}

static void finish_or_start_redraw(MusGameModel *model) {
    const int round = (int)model->mus_count - 1;

    if (model->discard_counts[round][0] > 0)
        start_redraw(model, 0);
    else if (model->discard_counts[round][1] > 0)
        start_redraw(model, 1);
    else
        finish_mus_round(model);
}

static Status apply_to_model(
    MusGameModel *model, const MusGameFormat *format, Action action,
    const MusHandSummary summaries[MUS_GAME_PLAYER_COUNT]) {
    if (format == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (phase_is_deal(model->phase)) {
        const MusGamePhase completed_phase = model->phase;
        const int player = deal_player(model);
        const int count = action;

        if (!deal_count_is_legal(model, count))
            return CFR_STATUS_ILLEGAL_ACTION;
        model->hands[player][model->deal_class] += (uint8_t)count;
        model->deal_remaining = (uint8_t)(model->deal_remaining - count);
        model->deal_class += 1;
        if (model->deal_class == MUS_GAME_CLASS_COUNT - 1) {
            const int last_available =
                available_in_class(model, player, MUS_GAME_CLASS_COUNT - 1);

            if (model->deal_remaining > last_available)
                return CFR_STATUS_INVALID_ARGUMENT;
            model->hands[player][MUS_GAME_CLASS_COUNT - 1] +=
                model->deal_remaining;
            model->deal_class = 0;
            if (completed_phase == MUS_PHASE_DEAL_PLAYER_0) {
                model->phase = MUS_PHASE_DEAL_PLAYER_1;
                model->deal_remaining = MUS_GAME_HAND_SIZE;
                model->deal_later_available =
                    (uint8_t)expected_later_available(model);
            } else if (completed_phase == MUS_PHASE_DEAL_PLAYER_1) {
                model->deal_remaining = 0;
                model->deal_later_available = 0;
                if (format->ordago_with_mus)
                    start_mus_round(model);
                else
                    start_betting(model, MUS_PHASE_GRANDE);
            } else if (completed_phase == MUS_PHASE_REDRAW_PLAYER_0) {
                const int round = (int)model->mus_count - 1;

                if (model->discard_counts[round][1] > 0)
                    start_redraw(model, 1);
                else
                    finish_mus_round(model);
            } else {
                finish_mus_round(model);
            }
        } else {
            model->deal_later_available =
                (uint8_t)(model->deal_later_available -
                          available_in_class(model, player,
                                             model->deal_class));
        }
        return CFR_STATUS_SUCCESS;
    }
    if (phase_is_mus_decision(model->phase)) {
        if (!format->ordago_with_mus ||
            (action != MUS_ACTION_MUS && action != MUS_ACTION_NO_MUS)) {
            return CFR_STATUS_ILLEGAL_ACTION;
        }
        if (action == MUS_ACTION_NO_MUS) {
            model->mus_stopped = true;
            model->mus_stop_player = model->current_player;
            start_betting(model, MUS_PHASE_GRANDE);
        } else if (model->phase == MUS_PHASE_MUS_PLAYER_0) {
            model->phase = MUS_PHASE_MUS_PLAYER_1;
            model->current_player = CFR_PLAYER_1;
        } else {
            model->phase = MUS_PHASE_DISCARD_PLAYER_0;
            model->current_player = CFR_PLAYER_0;
        }
        return CFR_STATUS_SUCCESS;
    }
    if (phase_is_discard(model->phase)) {
        const int player = model->phase == MUS_PHASE_DISCARD_PLAYER_0 ? 0 : 1;
        int raw_mask;
        unsigned int discarded;

        if (!format->ordago_with_mus ||
            action < MUS_ACTION_DISCARD_MASK_0 ||
            action >= MUS_ACTION_DISCARD_MASK_0 +
                          MUS_GAME_DISCARD_MASK_COUNT)
            return CFR_STATUS_ILLEGAL_ACTION;
        raw_mask = action - MUS_ACTION_DISCARD_MASK_0;
        discarded = discard_from_canonical_hand(
            model, player, (unsigned int)raw_mask);
        model->discard_masks[model->mus_count][player] =
            (uint8_t)raw_mask;
        model->discard_counts[model->mus_count][player] =
            (uint8_t)discarded;
        if (player == 0) {
            model->phase = MUS_PHASE_DISCARD_PLAYER_1;
            model->current_player = CFR_PLAYER_1;
        } else {
            model->mus_count += 1;
            finish_or_start_redraw(model);
        }
        return CFR_STATUS_SUCCESS;
    }
    if (!phase_is_betting(model->phase))
        return CFR_STATUS_INVALID_ARGUMENT;
    if (!player_action_is_legal(model, format, action))
        return CFR_STATUS_ILLEGAL_ACTION;

    if (model->bet_stage == MUS_BET_FIRST_TO_ACT) {
        if (action == MUS_ACTION_PASS) {
            model->bet_stage = MUS_BET_SECOND_AFTER_PASS;
            model->current_player = CFR_PLAYER_1;
        } else {
            const uint8_t wager = format->ordago_with_mus
                                      ? ordago_wager_for_action(action)
                                      : MUS_SINGLE_BET_WAGER;

            model->bet_stage = MUS_BET_RESPONSE;
            model->bettor = CFR_PLAYER_0;
            model->current_player = CFR_PLAYER_1;
            model->current_wager = wager;
            if (format->ordago_with_mus) {
                model->previous_wager = 1;
                model->current_wager_is_ordago =
                    action == MUS_ACTION_ORDAGO;
                model->current_strength =
                    MUS_LANCE_STRENGTH_PLAYER_0;
            }
        }
    } else if (model->bet_stage == MUS_BET_SECOND_AFTER_PASS) {
        if (action == MUS_ACTION_PASS) {
            advance_lance(model, MUS_LANCE_ALL_PASS, summaries);
        } else {
            const uint8_t wager = format->ordago_with_mus
                                      ? ordago_wager_for_action(action)
                                      : MUS_SINGLE_BET_WAGER;

            model->bet_stage = MUS_BET_RESPONSE;
            model->bettor = CFR_PLAYER_1;
            model->current_player = CFR_PLAYER_0;
            model->current_wager = wager;
            if (format->ordago_with_mus) {
                model->previous_wager = 1;
                model->current_wager_is_ordago =
                    action == MUS_ACTION_ORDAGO;
                model->current_strength =
                    MUS_LANCE_STRENGTH_PLAYER_1;
            }
        }
    } else {
        MusLanceOutcome outcome;

        if (action == MUS_ACTION_RAISE_DOUBLE ||
            (format->ordago_with_mus &&
             ordago_wager_for_action(action) > 0)) {
            const Player previous_bettor = model->bettor;
            const uint8_t amount = format->ordago_with_mus
                                       ? ordago_wager_for_action(action)
                                       : 0;
            const uint8_t next_wager =
                format->ordago_with_mus
                    ? (action == MUS_ACTION_ORDAGO
                           ? MUS_ORDAGO_WAGER
                           : (uint8_t)(model->current_wager + amount))
                    : (uint8_t)(model->current_wager * 2);

            model->bettor = model->current_player;
            model->current_player = previous_bettor;
            if (format->ordago_with_mus) {
                model->previous_wager = model->current_wager;
                model->current_wager_is_ordago =
                    action == MUS_ACTION_ORDAGO;
                model->current_strength = MUS_LANCE_STRENGTH_BOTH;
            }
            model->current_wager = next_wager;
            return CFR_STATUS_SUCCESS;
        }

        if (format->ordago_with_mus && action == MUS_ACTION_ACCEPT)
            model->current_strength = MUS_LANCE_STRENGTH_BOTH;

        if (model->bettor == CFR_PLAYER_0) {
            outcome = action == MUS_ACTION_ACCEPT
                          ? MUS_LANCE_FIRST_BET_ACCEPTED
                          : MUS_LANCE_FIRST_BET_REJECTED;
        } else {
            outcome = action == MUS_ACTION_ACCEPT
                          ? MUS_LANCE_SECOND_BET_ACCEPTED
                          : MUS_LANCE_SECOND_BET_REJECTED;
        }
        advance_lance(model, outcome, summaries);
    }
    return CFR_STATUS_SUCCESS;
}

static Status apply_action_to_state(MusGameState *mus, Action action,
                                    const MusGameFormat *format) {
    MusGameModel temporary;
    MusHandSummary completed_summaries[MUS_GAME_PLAYER_COUNT];
    const MusHandSummary *active_summaries;
    bool completed_deal;
    Status status;

    if (mus == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (mus->undo_count >= MUS_GAME_UNDO_CAPACITY)
        return CFR_STATUS_BUFFER_TOO_SMALL;
    temporary = mus->model;
    status = apply_to_model(&temporary, format, action,
                            mus->hand_summaries);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    completed_deal = phase_is_deal(mus->model.phase) &&
                     !phase_is_deal(temporary.phase);
    active_summaries = mus->hand_summaries;
    if (completed_deal) {
        status = summarize_hands(&temporary, completed_summaries);
        if (status != CFR_STATUS_SUCCESS)
            return status;
        active_summaries = completed_summaries;
    }
    status = validate_model(&temporary, format);
    if (status == CFR_STATUS_SUCCESS && !phase_is_deal(temporary.phase) &&
        !phase_is_discard(temporary.phase)) {
        status = validate_hand_summaries(&temporary, active_summaries);
    }
    if (status != CFR_STATUS_SUCCESS)
        return CFR_STATUS_INVALID_ARGUMENT;
    mus->undo_history[mus->undo_count] = mus->model;
    memcpy(mus->undo_hand_summaries[mus->undo_count],
           mus->hand_summaries, sizeof(mus->hand_summaries));
    mus->undo_count += 1;
    mus->model = temporary;
    if (completed_deal) {
        memcpy(mus->hand_summaries, completed_summaries,
               sizeof(completed_summaries));
    }
    return CFR_STATUS_SUCCESS;
}

static Status apply_trusted_action_to_state(MusGameState *mus, Action action,
                                            const MusGameFormat *format) {
    MusHandSummary completed_summaries[MUS_GAME_PLAYER_COUNT];
    MusGameModel *undo_model;
    bool completed_deal;
    Status status;

    if (mus == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (mus->undo_count >= MUS_GAME_UNDO_CAPACITY)
        return CFR_STATUS_BUFFER_TOO_SMALL;

    undo_model = &mus->undo_history[mus->undo_count];
    *undo_model = mus->model;
    memcpy(mus->undo_hand_summaries[mus->undo_count],
           mus->hand_summaries, sizeof(mus->hand_summaries));
    status =
        apply_to_model(&mus->model, format, action, mus->hand_summaries);
    if (status != CFR_STATUS_SUCCESS) {
        mus->model = *undo_model;
        return status;
    }
    completed_deal = phase_is_deal(undo_model->phase) &&
                     !phase_is_deal(mus->model.phase);
    if (completed_deal) {
        status = summarize_hands(&mus->model, completed_summaries);
        if (status != CFR_STATUS_SUCCESS) {
            mus->model = *undo_model;
            return status;
        }
        memcpy(mus->hand_summaries, completed_summaries,
               sizeof(completed_summaries));
    }
    mus->undo_count += 1;
    return CFR_STATUS_SUCCESS;
}

static Status mus_apply_action(const void *context, GameState *state,
                               Action action) {
    MusGameState *mus = as_mus(state);
    const MusGameFormat *format = game_format(context);

    if (validate_state_data(mus, format) != CFR_STATUS_SUCCESS)
        return CFR_STATUS_INVALID_ARGUMENT;
    return apply_action_to_state(mus, action, format);
}

static Status mus_single_bet_trusted_apply_action(const void *context,
                                                  GameState *state,
                                                  Action action) {
    (void)context;
    return apply_trusted_action_to_state(as_mus(state), action,
                                         &MUS_SINGLE_BET_FORMAT);
}

static Status mus_raises_to_eight_trusted_apply_action(const void *context,
                                                       GameState *state,
                                                       Action action) {
    (void)context;
    return apply_trusted_action_to_state(as_mus(state), action,
                                         &MUS_RAISES_TO_EIGHT_FORMAT);
}

static Status mus_ordago_trusted_apply_action(const void *context,
                                              GameState *state,
                                              Action action) {
    (void)context;
    return apply_trusted_action_to_state(as_mus(state), action,
                                         &MUS_ORDAGO_FORMAT);
}

static Status mus_undo_action(const void *context, GameState *state) {
    MusGameState *mus = as_mus(state);

    if (validate_state_data(mus, game_format(context)) !=
        CFR_STATUS_SUCCESS) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    return mus_trusted_undo_action(context, state);
}

static Status mus_trusted_undo_action(const void *context, GameState *state) {
    MusGameState *mus = as_mus(state);

    (void)context;
    if (mus == NULL || mus->undo_count == 0 ||
        mus->undo_count > MUS_GAME_UNDO_CAPACITY) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    mus->undo_count -= 1;
    mus->model = mus->undo_history[mus->undo_count];
    memcpy(mus->hand_summaries,
           mus->undo_hand_summaries[mus->undo_count],
           sizeof(mus->hand_summaries));
    return CFR_STATUS_SUCCESS;
}

static Status mus_chance_probability(const void *context,
                                     const GameState *state, Action action,
                                     Probability *result) {
    const MusGameState *mus = as_mus_const(state);

    if (result == NULL ||
        validate_state_data(mus, game_format(context)) !=
            CFR_STATUS_SUCCESS) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    return mus_trusted_chance_probability(context, state, action, result);
}

static Status mus_trusted_chance_probability(const void *context,
                                             const GameState *state,
                                             Action action,
                                             Probability *result) {
    const MusGameState *mus = as_mus_const(state);

    (void)context;
    if (mus == NULL || result == NULL || !phase_is_deal(mus->model.phase))
        return CFR_STATUS_INVALID_ARGUMENT;
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

static int wager_code(uint8_t wager) {
    switch (wager) {
    case 0:
        return 0;
    case 2:
        return 1;
    case 4:
        return 2;
    case 8:
        return 3;
    default:
        return -1;
    }
}

static uint8_t wager_from_code(int code) {
    static const uint8_t wagers[4] = {0, 2, 4, 8};

    if (code < 0 || code >= 4)
        return 0;
    return wagers[code];
}

static int raises_current_history_code(const MusGameModel *model) {
    const int current_wager_code = wager_code(model->current_wager);

    if (model->bet_stage == MUS_BET_FIRST_TO_ACT)
        return 0;
    if (model->bet_stage == MUS_BET_SECOND_AFTER_PASS)
        return 1;
    if (current_wager_code < 1)
        return -1;
    return 2 + (int)model->bettor * 3 + current_wager_code - 1;
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

static int small_combinations(int n, int k) {
    int result = 1;

    if (k < 0 || k > n)
        return 0;
    if (k > n - k)
        k = n - k;
    for (int index = 1; index <= k; index += 1)
        result = result * (n - k + index) / index;
    return result;
}

static int compact_hand_code(const uint8_t hand[MUS_GAME_CLASS_COUNT]) {
    int rank = 0;
    int remaining = MUS_GAME_HAND_SIZE;

    if (hand_total(hand) != MUS_GAME_HAND_SIZE)
        return -1;
    for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT - 1;
         card_class += 1) {
        const int tail_classes = MUS_GAME_CLASS_COUNT - card_class - 1;

        for (int count = 0; count < hand[card_class]; count += 1) {
            const int tail_total = remaining - count;

            rank += small_combinations(tail_total + tail_classes - 1,
                                       tail_classes - 1);
        }
        remaining -= hand[card_class];
    }
    return remaining == hand[MUS_GAME_CLASS_COUNT - 1] ? rank : -1;
}

static bool compact_hand_from_code(int rank,
                                   uint8_t hand[MUS_GAME_CLASS_COUNT]) {
    int remaining = MUS_GAME_HAND_SIZE;

    if (rank < 0 || rank >= MUS_ORDAGO_EXACT_HAND_RADIX)
        return false;
    memset(hand, 0, MUS_GAME_CLASS_COUNT);
    for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT - 1;
         card_class += 1) {
        const int tail_classes = MUS_GAME_CLASS_COUNT - card_class - 1;
        int count;

        for (count = 0; count <= remaining; count += 1) {
            const int tail_total = remaining - count;
            const int block = small_combinations(
                tail_total + tail_classes - 1, tail_classes - 1);

            if (rank < block)
                break;
            rank -= block;
        }
        if (count > remaining)
            return false;
        hand[card_class] = (uint8_t)count;
        remaining -= count;
    }
    hand[MUS_GAME_CLASS_COUNT - 1] = (uint8_t)remaining;
    return rank == 0;
}

enum {
    MUS_ORDAGO_MUS_SITUATION_COUNT = 8,
    MUS_ORDAGO_BETTING_SITUATION_COUNT = 4
};

static int ordago_betting_phase_index(MusGamePhase phase) {
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

static int ordago_previous_strength_count(MusGamePhase phase) {
    const int phase_index = ordago_betting_phase_index(phase);

    if (phase_index < 0)
        return -1;
    return phase_index > 3 ? 3 : phase_index;
}

static int ordago_situation_code(const MusGameModel *model) {
    int phase_index;
    int stage;

    if (phase_is_mus_decision(model->phase) ||
        phase_is_discard(model->phase)) {
        if (model->mus_count >= MUS_GAME_MAX_MUSES)
            return -1;
        if (model->phase == MUS_PHASE_MUS_PLAYER_0)
            stage = 0;
        else if (model->phase == MUS_PHASE_MUS_PLAYER_1)
            stage = 1;
        else if (model->phase == MUS_PHASE_DISCARD_PLAYER_0)
            stage = 2;
        else
            stage = 3;
        return (int)model->mus_count * 4 + stage;
    }
    phase_index = ordago_betting_phase_index(model->phase);
    if (phase_index < 0)
        return -1;
    if (model->bet_stage == MUS_BET_FIRST_TO_ACT)
        stage = 0;
    else if (model->bet_stage == MUS_BET_SECOND_AFTER_PASS)
        stage = 1;
    else if (model->bet_stage == MUS_BET_RESPONSE)
        stage = model->current_player == CFR_PLAYER_0 ? 2 : 3;
    else
        return -1;
    return MUS_ORDAGO_MUS_SITUATION_COUNT +
           phase_index * MUS_ORDAGO_BETTING_SITUATION_COUNT + stage;
}

static int grande_hand_code(const uint8_t hand[MUS_GAME_CLASS_COUNT]) {
    const int kings = hand[MUS_CLASS_KINGS];

    if (kings == MUS_GAME_HAND_SIZE)
        return MUS_ORDAGO_GRANDE_HAND_RADIX - 1;
    for (int card_class = MUS_CLASS_KNIGHTS;
         card_class >= MUS_CLASS_PITOS; card_class -= 1) {
        if (hand[card_class] > 0)
            return kings * 7 + card_class;
    }
    return -1;
}

static int chica_hand_code(const uint8_t hand[MUS_GAME_CLASS_COUNT]) {
    const int pitos = hand[MUS_CLASS_PITOS];

    if (pitos == MUS_GAME_HAND_SIZE)
        return MUS_ORDAGO_CHICA_HAND_RADIX - 1;
    for (int card_class = MUS_CLASS_FOURS; card_class <= MUS_CLASS_KINGS;
         card_class += 1) {
        if (hand[card_class] > 0)
            return pitos * 7 + card_class - 1;
    }
    return -1;
}

static int pairs_hand_code(const uint8_t hand[MUS_GAME_CLASS_COUNT]) {
    int first_pair = -1;

    for (int card_class = MUS_CLASS_PITOS; card_class <= MUS_CLASS_KINGS;
         card_class += 1) {
        if (hand[card_class] == 4) {
            return 16 + card_class * (card_class + 1) / 2 + card_class;
        }
        if (hand[card_class] == 3)
            return 8 + card_class;
        if (hand[card_class] != 2)
            continue;
        if (first_pair < 0) {
            first_pair = card_class;
        } else {
            return 16 + card_class * (card_class + 1) / 2 + first_pair;
        }
    }
    return first_pair;
}

static int ordago_hand_code(const MusGameModel *model) {
    const uint8_t *hand = model->hands[model->current_player];
    int point_value = 0;

    if (phase_is_mus_decision(model->phase) ||
        phase_is_discard(model->phase)) {
        return compact_hand_code(hand);
    }
    if (model->phase == MUS_PHASE_GRANDE)
        return grande_hand_code(hand);
    if (model->phase == MUS_PHASE_CHICA)
        return chica_hand_code(hand);
    if (model->phase == MUS_PHASE_PARES)
        return pairs_hand_code(hand);
    for (int card_class = MUS_CLASS_PITOS; card_class <= MUS_CLASS_KINGS;
         card_class += 1) {
        point_value += hand[card_class] * POINT_VALUES[card_class];
    }
    if (model->phase == MUS_PHASE_JUEGO)
        return game_key(point_value);
    if (model->phase == MUS_PHASE_PUNTO && point_value >= 4 &&
        point_value <= 30) {
        return point_value - 4;
    }
    return -1;
}

static int ordago_public_mus_code(const MusGameModel *model) {
    int discard_code;

    if (model->mus_stopped) {
        if (model->mus_count == 0)
            return model->mus_stop_player;
        if (model->mus_count == 1) {
            discard_code =
                model->discard_counts[0][0] *
                    MUS_ORDAGO_DISCARD_COUNT_RADIX +
                model->discard_counts[0][1];
            return 2 + (int)model->mus_stop_player * 25 + discard_code;
        }
        return -1;
    }
    if (model->mus_count != MUS_GAME_MAX_MUSES)
        return -1;
    discard_code = 0;
    for (int round = 0; round < MUS_GAME_MAX_MUSES; round += 1) {
        for (int player = 0; player < MUS_GAME_PLAYER_COUNT; player += 1) {
            discard_code =
                discard_code * MUS_ORDAGO_DISCARD_COUNT_RADIX +
                model->discard_counts[round][player];
        }
    }
    return 52 + discard_code;
}

static int ordago_hand_radix(MusGamePhase phase) {
    if (phase_is_mus_decision(phase) || phase_is_discard(phase))
        return MUS_ORDAGO_EXACT_HAND_RADIX;
    switch (phase) {
    case MUS_PHASE_GRANDE:
        return MUS_ORDAGO_GRANDE_HAND_RADIX;
    case MUS_PHASE_CHICA:
        return MUS_ORDAGO_CHICA_HAND_RADIX;
    case MUS_PHASE_PARES:
        return MUS_ORDAGO_PAIRS_HAND_RADIX;
    case MUS_PHASE_JUEGO:
        return MUS_ORDAGO_GAME_HAND_RADIX;
    case MUS_PHASE_PUNTO:
        return MUS_ORDAGO_POINT_HAND_RADIX;
    default:
        return 0;
    }
}

static Status ordago_information_set_key(const MusGameModel *model,
                                         InfoSetKey *result) {
    const int situation = ordago_situation_code(model);
    const int hand = ordago_hand_code(model);
    const int hand_radix = ordago_hand_radix(model->phase);
    InfoSetKey key;

    if (result == NULL || situation < 0 || hand < 0 ||
        hand >= hand_radix)
        return CFR_STATUS_INVALID_ARGUMENT;
    key = hand;
    if (phase_is_mus_decision(model->phase) ||
        phase_is_discard(model->phase)) {
        if (model->mus_count == 1) {
            key = key * MUS_ORDAGO_DISCARD_COUNT_RADIX +
                  model->discard_counts[0][0];
            key = key * MUS_ORDAGO_DISCARD_COUNT_RADIX +
                  model->discard_counts[0][1];
        }
        if (model->phase == MUS_PHASE_DISCARD_PLAYER_1) {
            key = key * MUS_ORDAGO_DISCARD_COUNT_RADIX +
                  model->discard_counts[model->mus_count][0];
        }
    } else {
        const int mus = ordago_public_mus_code(model);
        const int strength_count =
            ordago_previous_strength_count(model->phase);

        if (mus < 0 || mus >= MUS_ORDAGO_PUBLIC_MUS_RADIX ||
            strength_count < 0)
            return CFR_STATUS_INVALID_ARGUMENT;
        key = key * MUS_ORDAGO_PUBLIC_MUS_RADIX + mus;
        for (int lance = 0; lance < strength_count; lance += 1) {
            if (model->outcome_strengths[lance] <
                    MUS_LANCE_STRENGTH_NONE ||
                model->outcome_strengths[lance] >
                    MUS_LANCE_STRENGTH_BOTH) {
                return CFR_STATUS_INVALID_ARGUMENT;
            }
            key = key * MUS_ORDAGO_STRENGTH_RADIX +
                  model->outcome_strengths[lance];
        }
        if (model->bet_stage == MUS_BET_RESPONSE) {
            if (!ordago_offer_is_valid(
                    model->current_wager, model->previous_wager,
                    model->current_wager_is_ordago)) {
                return CFR_STATUS_INVALID_ARGUMENT;
            }
            key = key * MUS_ORDAGO_WAGER_RADIX + model->current_wager;
            key = key * MUS_ORDAGO_WAGER_RADIX + model->previous_wager;
            key = key * 2 + model->current_wager_is_ordago;
        }
    }
    *result = key * MUS_ORDAGO_SITUATION_COUNT + situation;
    return CFR_STATUS_SUCCESS;
}

static Status mus_information_set_key(const void *context,
                                      const GameState *state,
                                      InfoSetKey *result) {
    const MusGameState *mus = as_mus_const(state);

    if (result == NULL ||
        validate_state_data(mus, game_format(context)) !=
            CFR_STATUS_SUCCESS) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    return mus_trusted_information_set_key(context, state, result);
}

static Status mus_trusted_information_set_key(const void *context,
                                              const GameState *state,
                                              InfoSetKey *result) {
    const MusGameState *mus = as_mus_const(state);
    const MusGameModel *model;
    const MusGameFormat *format = game_format(context);
    InfoSetKey key;
    int phase_code;

    if (mus == NULL || format == NULL || result == NULL ||
        (!phase_is_betting(mus->model.phase) &&
         !(format->ordago_with_mus &&
           (phase_is_mus_decision(mus->model.phase) ||
            phase_is_discard(mus->model.phase))))) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    model = &mus->model;
    if (format->ordago_with_mus)
        return ordago_information_set_key(model, result);
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
    if (format->raises_to_eight) {
        for (int lance = 0; lance < MUS_GAME_LANCE_COUNT; lance += 1) {
            const int outcome_code =
                (int)model->outcomes[lance] * 4 +
                wager_code(model->outcome_wagers[lance]);

            key = key * MUS_RAISES_OUTCOME_RADIX + outcome_code;
        }
        key = key * MUS_RAISES_CURRENT_HISTORY_RADIX +
              raises_current_history_code(model);
    } else {
        for (int lance = 0; lance < MUS_GAME_LANCE_COUNT; lance += 1)
            key = key * MUS_OUTCOME_RADIX + model->outcomes[lance];
        key = key * MUS_CURRENT_HISTORY_RADIX +
              current_history_code(model);
    }
    *result = key;
    return CFR_STATUS_SUCCESS;
}

static Status mus_single_bet_trusted_information_set_key(
    const void *context, const GameState *state, InfoSetKey *result) {
    const MusGameState *mus = as_mus_const(state);
    const MusGameModel *model;
    InfoSetKey key;
    int phase_code;

    (void)context;
    if (mus == NULL || result == NULL ||
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

static Status mus_raises_to_eight_trusted_information_set_key(
    const void *context, const GameState *state, InfoSetKey *result) {
    (void)context;
    return mus_trusted_information_set_key(&MUS_RAISES_TO_EIGHT_FORMAT, state,
                                           result);
}

static Status mus_ordago_trusted_information_set_key(
    const void *context, const GameState *state, InfoSetKey *result) {
    const MusGameState *mus = as_mus_const(state);

    (void)context;
    if (mus == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    return ordago_information_set_key(&mus->model, result);
}

static Status mus_validate_state(const void *context, const GameState *state) {
    const MusGameState *mus = as_mus_const(state);

    if (mus == NULL || mus->undo_count > MUS_GAME_UNDO_CAPACITY)
        return CFR_STATUS_INVALID_ARGUMENT;
    return validate_state_data(mus, game_format(context));
}

static Status mus_chance_outcomes(const void *context, const GameState *state,
                                  Action *actions, Probability *probabilities,
                                  size_t capacity, size_t *required_count) {
    const MusGameState *mus = as_mus_const(state);

    if (actions == NULL || probabilities == NULL || required_count == NULL ||
        validate_state_data(mus, game_format(context)) !=
            CFR_STATUS_SUCCESS) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    return mus_trusted_chance_outcomes(
        context, state, actions, probabilities, capacity, required_count);
}

static Status mus_trusted_chance_outcomes(
    const void *context, const GameState *state, Action *actions,
    Probability *probabilities, size_t capacity, size_t *required_count) {
    const MusGameState *mus = as_mus_const(state);
    Action temporary_actions[MUS_GAME_MAX_LEGAL_ACTIONS];
    Probability temporary_probabilities[MUS_GAME_MAX_LEGAL_ACTIONS];
    size_t count = 0;
    Status status;

    (void)context;
    if (mus == NULL || actions == NULL || probabilities == NULL ||
        required_count == NULL ||
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

static Status decode_information_base(InfoSetKey key,
                                      MusInformationSet *information_set) {
    int hand;

    if (information_set == NULL || key < 0)
        return CFR_STATUS_INVALID_ARGUMENT;
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
        hand_total(information_set->hand) != MUS_GAME_HAND_SIZE)
        return CFR_STATUS_INVALID_ARGUMENT;
    return CFR_STATUS_SUCCESS;
}

Status mus_game_information_set_decode(InfoSetKey key,
                                       MusInformationSet *information_set) {
    int history_code;
    Status status;

    if (information_set == NULL || key < 0)
        return CFR_STATUS_INVALID_ARGUMENT;
    *information_set = (MusInformationSet){0};
    history_code = (int)(key % MUS_CURRENT_HISTORY_RADIX);
    key /= MUS_CURRENT_HISTORY_RADIX;
    for (int lance = MUS_GAME_LANCE_COUNT - 1; lance >= 0; lance -= 1) {
        information_set->outcomes[lance] =
            (MusLanceOutcome)(key % MUS_OUTCOME_RADIX);
        if (information_set->outcomes[lance] < MUS_LANCE_UNRESOLVED ||
            information_set->outcomes[lance] > MUS_LANCE_SKIPPED) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
        if (outcome_is_accepted(information_set->outcomes[lance]) ||
            outcome_is_rejected(information_set->outcomes[lance])) {
            information_set->outcome_wagers[lance] = MUS_SINGLE_BET_WAGER;
        }
        key /= MUS_OUTCOME_RADIX;
    }
    status = decode_information_base(key, information_set);
    if (status != CFR_STATUS_SUCCESS || history_code < 0 ||
        history_code >= MUS_CURRENT_HISTORY_RADIX) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
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
        information_set->current_wager = MUS_SINGLE_BET_WAGER;
    }
    return CFR_STATUS_SUCCESS;
}

Status mus_single_bet_game_information_set_decode(
    InfoSetKey key, MusSingleBetInformationSet *information_set) {
    return mus_game_information_set_decode(key, information_set);
}

Status mus_raises_to_eight_game_information_set_decode(
    InfoSetKey key, MusRaisesToEightInformationSet *information_set) {
    int history_code;
    Status status;

    if (information_set == NULL || key < 0)
        return CFR_STATUS_INVALID_ARGUMENT;
    *information_set = (MusRaisesToEightInformationSet){0};
    history_code = (int)(key % MUS_RAISES_CURRENT_HISTORY_RADIX);
    key /= MUS_RAISES_CURRENT_HISTORY_RADIX;
    for (int lance = MUS_GAME_LANCE_COUNT - 1; lance >= 0; lance -= 1) {
        const int outcome_code = (int)(key % MUS_RAISES_OUTCOME_RADIX);
        const MusLanceOutcome outcome =
            (MusLanceOutcome)(outcome_code / 4);
        const uint8_t wager = wager_from_code(outcome_code % 4);

        if (outcome < MUS_LANCE_UNRESOLVED || outcome > MUS_LANCE_SKIPPED ||
            !outcome_wager_is_valid(
                &MUS_RAISES_TO_EIGHT_FORMAT, outcome, wager, 0, false,
                MUS_LANCE_STRENGTH_NONE)) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
        information_set->outcomes[lance] = outcome;
        information_set->outcome_wagers[lance] = wager;
        key /= MUS_RAISES_OUTCOME_RADIX;
    }
    status = decode_information_base(key, information_set);
    if (status != CFR_STATUS_SUCCESS || history_code < 0 ||
        history_code >= MUS_RAISES_CURRENT_HISTORY_RADIX) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    if (history_code == 0) {
        information_set->bet_stage = MUS_BET_FIRST_TO_ACT;
        information_set->bettor = CFR_PLAYER_0;
    } else if (history_code == 1) {
        information_set->bet_stage = MUS_BET_SECOND_AFTER_PASS;
        information_set->bettor = CFR_PLAYER_0;
    } else {
        const int response_code = history_code - 2;

        information_set->bet_stage = MUS_BET_RESPONSE;
        information_set->bettor = (Player)(response_code / 3);
        information_set->current_wager =
            wager_from_code(response_code % 3 + 1);
    }
    return CFR_STATUS_SUCCESS;
}

static bool decode_ordago_situation(int situation,
                                    MusOrdagoInformationSet *information,
                                    int *round_out) {
    static const MusGamePhase decision_phases[4] = {
        MUS_PHASE_MUS_PLAYER_0,
        MUS_PHASE_MUS_PLAYER_1,
        MUS_PHASE_DISCARD_PLAYER_0,
        MUS_PHASE_DISCARD_PLAYER_1,
    };
    static const MusGamePhase betting_phases[5] = {
        MUS_PHASE_GRANDE,
        MUS_PHASE_CHICA,
        MUS_PHASE_PARES,
        MUS_PHASE_JUEGO,
        MUS_PHASE_PUNTO,
    };

    if (situation < 0 || situation >= MUS_ORDAGO_SITUATION_COUNT)
        return false;
    if (situation < MUS_ORDAGO_MUS_SITUATION_COUNT) {
        const int stage = situation % 4;

        *round_out = situation / 4;
        information->phase = decision_phases[stage];
        information->player = stage == 0 || stage == 2 ? CFR_PLAYER_0
                                                        : CFR_PLAYER_1;
        information->mus_count = (uint8_t)*round_out;
        information->hand_kind = MUS_ORDAGO_HAND_EXACT;
        return true;
    }
    situation -= MUS_ORDAGO_MUS_SITUATION_COUNT;
    const int phase_index =
        situation / MUS_ORDAGO_BETTING_SITUATION_COUNT;
    const int stage = situation % MUS_ORDAGO_BETTING_SITUATION_COUNT;

    if (phase_index < 0 || phase_index >= 5)
        return false;
    information->phase = betting_phases[phase_index];
    information->previous_strength_count =
        (uint8_t)(phase_index > 3 ? 3 : phase_index);
    if (stage == 0) {
        information->player = CFR_PLAYER_0;
        information->bet_stage = MUS_BET_FIRST_TO_ACT;
        information->bettor = CFR_PLAYER_0;
    } else if (stage == 1) {
        information->player = CFR_PLAYER_1;
        information->bet_stage = MUS_BET_SECOND_AFTER_PASS;
        information->bettor = CFR_PLAYER_0;
    } else {
        information->player = stage == 2 ? CFR_PLAYER_0 : CFR_PLAYER_1;
        information->bet_stage = MUS_BET_RESPONSE;
        information->bettor = information->player == CFR_PLAYER_0
                                  ? CFR_PLAYER_1
                                  : CFR_PLAYER_0;
    }
    information->hand_kind =
        (MusOrdagoHandKind)(MUS_ORDAGO_HAND_GRANDE + phase_index);
    return true;
}

static bool decode_ordago_public_mus(
    int code, MusOrdagoInformationSet *information) {
    if (code < 0 || code >= MUS_ORDAGO_PUBLIC_MUS_RADIX)
        return false;
    if (code < 2) {
        information->mus_stopped = true;
        information->mus_stop_player = (Player)code;
        return true;
    }
    if (code < 52) {
        const int stopped_code = code - 2;
        const int discard_code = stopped_code % 25;

        information->mus_count = 1;
        information->mus_stopped = true;
        information->mus_stop_player = (Player)(stopped_code / 25);
        information->discard_counts[0][0] =
            (uint8_t)(discard_code / MUS_ORDAGO_DISCARD_COUNT_RADIX);
        information->discard_counts[0][1] =
            (uint8_t)(discard_code % MUS_ORDAGO_DISCARD_COUNT_RADIX);
        return true;
    }
    code -= 52;
    information->mus_count = MUS_GAME_MAX_MUSES;
    information->mus_stopped = false;
    information->mus_stop_player = CFR_PLAYER_0;
    for (int round = MUS_GAME_MAX_MUSES - 1; round >= 0; round -= 1) {
        for (int player = MUS_GAME_PLAYER_COUNT - 1; player >= 0;
             player -= 1) {
            information->discard_counts[round][player] =
                (uint8_t)(code % MUS_ORDAGO_DISCARD_COUNT_RADIX);
            code /= MUS_ORDAGO_DISCARD_COUNT_RADIX;
        }
    }
    return code == 0;
}

static bool decode_ordago_hand(int code,
                                MusOrdagoInformationSet *information) {
    if (code < 0 || code >= ordago_hand_radix(information->phase))
        return false;
    switch (information->hand_kind) {
    case MUS_ORDAGO_HAND_EXACT:
        return compact_hand_from_code(code, information->hand);
    case MUS_ORDAGO_HAND_GRANDE:
        if (code == MUS_ORDAGO_GRANDE_HAND_RADIX - 1) {
            information->king_count = MUS_GAME_HAND_SIZE;
            information->has_highest_non_king = false;
        } else {
            information->king_count = (uint8_t)(code / 7);
            information->highest_non_king = (MusCardClass)(code % 7);
            information->has_highest_non_king = true;
        }
        return true;
    case MUS_ORDAGO_HAND_CHICA:
        if (code == MUS_ORDAGO_CHICA_HAND_RADIX - 1) {
            information->pito_count = MUS_GAME_HAND_SIZE;
            information->has_lowest_non_pito = false;
        } else {
            information->pito_count = (uint8_t)(code / 7);
            information->lowest_non_pito =
                (MusCardClass)(code % 7 + MUS_CLASS_FOURS);
            information->has_lowest_non_pito = true;
        }
        return true;
    case MUS_ORDAGO_HAND_PARES:
        if (code < 8) {
            information->pairs_type = MUS_PAIRS_PAIR;
            information->pairs_value = (MusCardClass)code;
        } else if (code < 16) {
            information->pairs_type = MUS_PAIRS_MEDIAS;
            information->pairs_value = (MusCardClass)(code - 8);
        } else {
            int pair_code = code - 16;

            information->pairs_type = MUS_PAIRS_DUPLES;
            information->has_second_pairs_value = true;
            for (int high = MUS_CLASS_PITOS; high <= MUS_CLASS_KINGS;
                 high += 1) {
                if (pair_code <= high) {
                    information->pairs_value = (MusCardClass)high;
                    information->second_pairs_value =
                        (MusCardClass)pair_code;
                    return true;
                }
                pair_code -= high + 1;
            }
            return false;
        }
        return true;
    case MUS_ORDAGO_HAND_JUEGO:
        information->game_value = (uint8_t)GAME_VALUES[code];
        return true;
    case MUS_ORDAGO_HAND_PUNTO:
        information->point_value = (uint8_t)(code + 4);
        return true;
    }
    return false;
}

Status mus_ordago_game_information_set_decode(
    InfoSetKey key, MusOrdagoInformationSet *information_set) {
    int situation;
    int round = -1;
    int hand_code_value;

    if (information_set == NULL || key < 0)
        return CFR_STATUS_INVALID_ARGUMENT;
    *information_set = (MusOrdagoInformationSet){0};
    situation = (int)(key % MUS_ORDAGO_SITUATION_COUNT);
    key /= MUS_ORDAGO_SITUATION_COUNT;
    if (!decode_ordago_situation(situation, information_set, &round))
        return CFR_STATUS_INVALID_ARGUMENT;
    if (round >= 0) {
        if (information_set->phase == MUS_PHASE_DISCARD_PLAYER_1) {
            information_set->discard_counts[round][0] =
                (uint8_t)(key % MUS_ORDAGO_DISCARD_COUNT_RADIX);
            key /= MUS_ORDAGO_DISCARD_COUNT_RADIX;
        }
        if (round == 1) {
            information_set->discard_counts[0][1] =
                (uint8_t)(key % MUS_ORDAGO_DISCARD_COUNT_RADIX);
            key /= MUS_ORDAGO_DISCARD_COUNT_RADIX;
            information_set->discard_counts[0][0] =
                (uint8_t)(key % MUS_ORDAGO_DISCARD_COUNT_RADIX);
            key /= MUS_ORDAGO_DISCARD_COUNT_RADIX;
        }
    } else {
        if (information_set->bet_stage == MUS_BET_RESPONSE) {
            information_set->current_wager_is_ordago = (bool)(key % 2);
            key /= 2;
            information_set->previous_wager =
                (uint8_t)(key % MUS_ORDAGO_WAGER_RADIX);
            key /= MUS_ORDAGO_WAGER_RADIX;
            information_set->current_wager =
                (uint8_t)(key % MUS_ORDAGO_WAGER_RADIX);
            key /= MUS_ORDAGO_WAGER_RADIX;
            if (!ordago_offer_is_valid(
                    information_set->current_wager,
                    information_set->previous_wager,
                    information_set->current_wager_is_ordago)) {
                return CFR_STATUS_INVALID_ARGUMENT;
            }
        }
        for (int lance = information_set->previous_strength_count - 1;
             lance >= 0; lance -= 1) {
            information_set->previous_strengths[lance] =
                (MusLanceStrength)(key % MUS_ORDAGO_STRENGTH_RADIX);
            key /= MUS_ORDAGO_STRENGTH_RADIX;
        }
        const int mus_code = (int)(key % MUS_ORDAGO_PUBLIC_MUS_RADIX);

        key /= MUS_ORDAGO_PUBLIC_MUS_RADIX;
        if (!decode_ordago_public_mus(mus_code, information_set))
            return CFR_STATUS_INVALID_ARGUMENT;
    }
    if (key >= ordago_hand_radix(information_set->phase))
        return CFR_STATUS_INVALID_ARGUMENT;
    hand_code_value = (int)key;
    if (!decode_ordago_hand(hand_code_value, information_set))
        return CFR_STATUS_INVALID_ARGUMENT;
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
    case MUS_PHASE_MUS_PLAYER_0:
        return "mus_mano";
    case MUS_PHASE_MUS_PLAYER_1:
        return "mus_postre";
    case MUS_PHASE_DISCARD_PLAYER_0:
        return "descarte_mano";
    case MUS_PHASE_DISCARD_PLAYER_1:
        return "descarte_postre";
    case MUS_PHASE_REDRAW_PLAYER_0:
        return "robo_mano";
    case MUS_PHASE_REDRAW_PLAYER_1:
        return "robo_postre";
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

const char *mus_single_bet_game_outcome_name(MusLanceOutcome outcome) {
    return mus_game_outcome_name(outcome);
}

const char *mus_raises_to_eight_game_outcome_name(MusLanceOutcome outcome,
                                                  uint8_t wager) {
    if (outcome == MUS_LANCE_UNRESOLVED && wager == 0)
        return "pendiente";
    if (outcome == MUS_LANCE_ALL_PASS && wager == 0)
        return "paso-paso";
    if (outcome == MUS_LANCE_SKIPPED && wager == 0)
        return "sin_negociacion";

    switch (outcome) {
    case MUS_LANCE_FIRST_BET_REJECTED:
        if (wager == 2)
            return "envite_2_mano-no_querido";
        if (wager == 4)
            return "paso-envite_2_postre-subida_4_mano-no_querida";
        if (wager == 8)
            return "envite_2_mano-subida_4_postre-subida_8_mano-no_querida";
        break;
    case MUS_LANCE_FIRST_BET_ACCEPTED:
        if (wager == 2)
            return "envite_2_mano-querido";
        if (wager == 4)
            return "paso-envite_2_postre-subida_4_mano-querida";
        if (wager == 8)
            return "envite_2_mano-subida_4_postre-subida_8_mano-querida";
        break;
    case MUS_LANCE_SECOND_BET_REJECTED:
        if (wager == 2)
            return "paso-envite_2_postre-no_querido";
        if (wager == 4)
            return "envite_2_mano-subida_4_postre-no_querida";
        if (wager == 8)
            return "paso-envite_2_postre-subida_4_mano-subida_8_postre-no_querida";
        break;
    case MUS_LANCE_SECOND_BET_ACCEPTED:
        if (wager == 2)
            return "paso-envite_2_postre-querido";
        if (wager == 4)
            return "envite_2_mano-subida_4_postre-querida";
        if (wager == 8)
            return "paso-envite_2_postre-subida_4_mano-subida_8_postre-querida";
        break;
    case MUS_LANCE_UNRESOLVED:
    case MUS_LANCE_ALL_PASS:
    case MUS_LANCE_SKIPPED:
        break;
    }
    return "desconocido";
}

const char *mus_ordago_game_outcome_name(MusLanceOutcome outcome,
                                         uint8_t wager) {
    if (outcome == MUS_LANCE_UNRESOLVED && wager == 0)
        return "pendiente";
    if (outcome == MUS_LANCE_ALL_PASS && wager == 0)
        return "paso-paso";
    if (outcome == MUS_LANCE_SKIPPED && wager == 0)
        return "sin_negociacion";

#define ORDAGO_OUTCOME_CASE(outcome_value, suffix)                             \
    if (outcome == (outcome_value)) {                                          \
        if (wager == 2)                                                        \
            return "envite_2_" suffix;                                        \
        if (wager == 4)                                                        \
            return "envite_4_" suffix;                                        \
        if (wager == 7)                                                        \
            return "envite_7_" suffix;                                        \
        if (wager == 10)                                                       \
            return "envite_10_" suffix;                                       \
        if (wager == MUS_ORDAGO_WAGER)                                         \
            return "ordago_" suffix;                                          \
    }

    ORDAGO_OUTCOME_CASE(MUS_LANCE_FIRST_BET_REJECTED,
                        "mano-no_querido")
    ORDAGO_OUTCOME_CASE(MUS_LANCE_FIRST_BET_ACCEPTED, "mano-querido")
    ORDAGO_OUTCOME_CASE(MUS_LANCE_SECOND_BET_REJECTED,
                        "postre-no_querido")
    ORDAGO_OUTCOME_CASE(MUS_LANCE_SECOND_BET_ACCEPTED,
                        "postre-querido")
#undef ORDAGO_OUTCOME_CASE
    return "desconocido";
}
