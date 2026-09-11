#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mus_solver/mus_game.h"
#include "mus_solver/mus_score.h"

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
#define MUS_ORDAGO_DISCARD_COUNT_RADIX MUS_GAME_HAND_SIZE
#define MUS_ORDAGO_DISCARD_ROUND_RADIX                                  \
    (MUS_ORDAGO_DISCARD_COUNT_RADIX * MUS_ORDAGO_DISCARD_COUNT_RADIX)
#define MUS_ORDAGO_WAGER_RADIX (MUS_ORDAGO_WAGER + 1)
#define MUS_ORDAGO_WAGER_ACTION_MASK_RADIX                              \
    MUS_ORDAGO_WAGER_ACTION_MASK_COUNT
#define MUS_ORDAGO_SCORE_RADIX (MUS_ORDAGO_WAGER + 1)
#define MUS_ORDAGO_TOTAL_WAGER_RADIX (MUS_ORDAGO_WAGER + 1)
#define MUS_ORDAGO_SITUATION_COUNT                                      \
    (MUS_GAME_MAX_MUSES * 4 + 5 * 4)
#define MUS_ORDAGO_STRENGTH_RADIX (MUS_LANCE_STRENGTH_BOTH + 1)
#define MUS_ORDAGO_PAIRS_STRENGTH_RADIX                                  \
    (MUS_LANCE_STRENGTH_PARES_SKIPPED_PLAYER_1 + 1)
#if MUS_GAME_MAX_MUSES == 1
#define MUS_ORDAGO_PUBLIC_MUS_RADIX                                     \
    (MUS_GAME_PLAYER_COUNT + MUS_ORDAGO_DISCARD_ROUND_RADIX)
#else
#define MUS_ORDAGO_PUBLIC_MUS_RADIX                                     \
    (MUS_GAME_PLAYER_COUNT * (1 + MUS_ORDAGO_DISCARD_ROUND_RADIX) +       \
     MUS_ORDAGO_DISCARD_ROUND_RADIX * MUS_ORDAGO_DISCARD_ROUND_RADIX)
#endif

/* The masks determine the sum and last increment: increments never decrease. */
#define MUS_SCORE_RESPONSE_RADIX \
    (MUS_ORDAGO_WAGER_ACTION_MASK_RADIX * MUS_ORDAGO_WAGER_ACTION_MASK_RADIX * 2)
/* Bound all betting phases, without hashing or information-set collisions. */
#define MUS_SCORE_KEY_RADIX_PRODUCT                                    \
    (UINT64_C(1) * MUS_ORDAGO_PAIRS_HAND_RADIX *                         \
     MUS_ORDAGO_PUBLIC_MUS_RADIX * MUS_ORDAGO_STRENGTH_RADIX *          \
     MUS_ORDAGO_STRENGTH_RADIX * MUS_ORDAGO_PAIRS_STRENGTH_RADIX *      \
     MUS_ORDAGO_SCORE_RADIX * MUS_ORDAGO_SCORE_RADIX *                   \
     MUS_ORDAGO_TOTAL_WAGER_RADIX *                                    \
     MUS_SCORE_RESPONSE_RADIX * MUS_ORDAGO_SITUATION_COUNT)
_Static_assert(MUS_ORDAGO_PAIRS_HAND_RADIX >= MUS_ORDAGO_GRANDE_HAND_RADIX &&
                   MUS_ORDAGO_PAIRS_HAND_RADIX >= MUS_ORDAGO_CHICA_HAND_RADIX &&
                   MUS_ORDAGO_PAIRS_HAND_RADIX >= MUS_ORDAGO_GAME_HAND_RADIX &&
                   MUS_ORDAGO_PAIRS_HAND_RADIX >= MUS_ORDAGO_POINT_HAND_RADIX,
               "The score key bound must cover every betting hand radix");
_Static_assert(MUS_SCORE_KEY_RADIX_PRODUCT <= INT64_MAX,
               "Score information-set keys must fit in int64_t");

typedef struct {
    bool raises_to_eight;
    bool ordago_with_mus;
    bool score_enabled;
    MusAccountingMode accounting_mode;
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

static const MusGameFormat MUS_ORDAGO_PUBLIC_FORMAT = {
    .ordago_with_mus = true,
    .accounting_mode = MUS_ACCOUNTING_PUBLIC_DEFERRED,
};

static const MusGameFormat MUS_SCORE_FORMAT = {
    .ordago_with_mus = true,
    .score_enabled = true,
    .accounting_mode = MUS_ACCOUNTING_PUBLIC_DEFERRED,
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
static Status mus_ordago_trusted_undo_action(const void *context,
                                             GameState *state);
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
static int compact_hand_code(const uint8_t hand[MUS_GAME_CLASS_COUNT]);
static Status summarize_ordago_hand_codes(
    const MusGameModel *model,
    MusOrdagoHandCodes codes[MUS_GAME_PLAYER_COUNT]);
static bool score_match_finished(const MusGameModel *model);
static bool outcome_is_accepted(MusLanceOutcome outcome);
static bool outcome_is_rejected(MusLanceOutcome outcome);
static int outcome_bettor(MusLanceOutcome outcome);
static int ordago_showdown_winner(
    const MusGameModel *model,
    const MusHandSummary summaries[MUS_GAME_PLAYER_COUNT], int lance);
static bool settle_ordago_lance_score(
    const MusGameModel *model,
    const MusHandSummary summaries[MUS_GAME_PLAYER_COUNT], int lance,
    uint8_t scores[MUS_GAME_PLAYER_COUNT], bool include_rejected_stakes);
static Status recompute_ordago_settled_scores(
    const MusGameModel *model,
    const MusHandSummary summaries[MUS_GAME_PLAYER_COUNT],
    uint8_t scores[MUS_GAME_PLAYER_COUNT]);

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
    .undo_action = mus_ordago_trusted_undo_action,
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
    .strategy_schema_id = MUS_ORDAGO_STRATEGY_SCHEMA_ID,
    .trusted_operations = &MUS_ORDAGO_TRUSTED_OPERATIONS,
};

static const Game MUS_ORDAGO_PUBLIC_GAME = {
    .operations = &MUS_OPERATIONS,
    .context = &MUS_ORDAGO_PUBLIC_FORMAT,
    .strategic_player_count = MUS_GAME_PLAYER_COUNT,
    .max_legal_actions = MUS_GAME_MAX_LEGAL_ACTIONS,
    .strategy_schema_id = MUS_ORDAGO_PUBLIC_STRATEGY_SCHEMA_ID,
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
    if (context == &MUS_ORDAGO_PUBLIC_FORMAT)
        return &MUS_ORDAGO_PUBLIC_FORMAT;
    if (context == &MUS_SCORE_FORMAT)
        return &MUS_SCORE_FORMAT;
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

bool mus_ordago_game_discard_mask_is_canonical(
    const uint8_t hand[MUS_GAME_CLASS_COUNT], unsigned int mask) {
    unsigned int position = 0;

    if (hand == NULL || hand_total(hand) != MUS_GAME_HAND_SIZE || mask == 0 ||
        mask >= MUS_GAME_DISCARD_MASK_COUNT) {
        return false;
    }
    for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT;
         card_class += 1) {
        const unsigned int count = hand[card_class];

        if (count > 0) {
            const unsigned int class_mask = (1U << count) - 1U;
            const unsigned int selected = (mask >> position) & class_mask;

            if ((selected & (selected + 1U)) != 0)
                return false;
        }
        position += count;
    }
    return true;
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
    return mus_trusted_legal_actions(context, state, actions,
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
    memset(model->used_wager_action_masks, 0,
           sizeof(model->used_wager_action_masks));
    model->current_wager_is_ordago = false;
    model->current_strength = MUS_LANCE_STRENGTH_NONE;
}

static void start_mus_round(MusGameModel *model) {
    model->phase = MUS_PHASE_MUS_PLAYER_0;
    model->current_player = CFR_PLAYER_0;
    model->mus_stopped = false;
    model->mus_stop_player = CFR_PLAYER_0;
}

static int ordago_outcome_stake(const MusGameModel *model, int lance,
                                MusLanceOutcome outcome) {
    if (outcome_is_accepted(outcome))
        return model->outcome_wagers[lance];
    if (outcome_is_rejected(outcome)) {
        const int previous = model->outcome_previous_wagers[lance];

        return previous == 0 ? 1 : previous;
    }
    return 0;
}

static void add_ordago_score(uint8_t scores[MUS_GAME_PLAYER_COUNT],
                             int player, int points) {
    const int total = scores[player] + points;

    scores[player] =
        (uint8_t)(total < MUS_ORDAGO_WAGER ? total : MUS_ORDAGO_WAGER);
}

static bool settle_ordago_lance_score(
    const MusGameModel *model,
    const MusHandSummary summaries[MUS_GAME_PLAYER_COUNT], int lance,
    uint8_t scores[MUS_GAME_PLAYER_COUNT], bool include_rejected_stakes) {
    const MusLanceOutcome outcome = model->outcomes[lance];
    int winner;
    int points = 0;

    if (outcome == MUS_LANCE_UNRESOLVED)
        return false;
    if (model->outcome_wager_is_ordago[lance] &&
        outcome_is_accepted(outcome)) {
        winner = ordago_showdown_winner(model, summaries, lance);
        scores[0] = 0;
        scores[1] = 0;
        scores[winner] = MUS_ORDAGO_WAGER;
        return true;
    }
    if (lance == 0 || lance == 1) {
        if (outcome_is_rejected(outcome)) {
            winner = outcome_bettor(outcome);
            points = include_rejected_stakes ? ordago_outcome_stake(model, lance, outcome) : 0;
        } else {
            winner = ordago_showdown_winner(model, summaries, lance);
            points = outcome_is_accepted(outcome)
                         ? ordago_outcome_stake(model, lance, outcome)
                         : 1;
        }
    } else if (lance == 2) {
        if (!model->pairs[0] && !model->pairs[1])
            return false;
        if (outcome_is_rejected(outcome)) {
            winner = outcome_bettor(outcome);
            points = include_rejected_stakes ? ordago_outcome_stake(model, lance, outcome) : 0;
        } else if (model->pairs[0] && model->pairs[1]) {
            winner = ordago_showdown_winner(model, summaries, lance);
        } else {
            winner = model->pairs[0] ? 0 : 1;
        }
        points += summaries[winner].pairs_points;
        if (outcome_is_accepted(outcome))
            points += ordago_outcome_stake(model, lance, outcome);
    } else if (lance == 3) {
        if (model->game[0] || model->game[1]) {
            if (outcome_is_rejected(outcome)) {
                winner = outcome_bettor(outcome);
                points = include_rejected_stakes ? ordago_outcome_stake(model, lance, outcome) : 0;
            } else if (model->game[0] && model->game[1]) {
                winner = ordago_showdown_winner(model, summaries, lance);
            } else {
                winner = model->game[0] ? 0 : 1;
            }
            points += summaries[winner].game_points;
            if (outcome_is_accepted(outcome))
                points += ordago_outcome_stake(model, lance, outcome);
        } else {
            winner = outcome_is_rejected(outcome)
                         ? outcome_bettor(outcome)
                         : ordago_showdown_winner(model, summaries, lance);
            points = 1;
            if (outcome_is_accepted(outcome) ||
                (include_rejected_stakes && outcome_is_rejected(outcome))) {
                points += ordago_outcome_stake(model, lance, outcome);
            }
        }
    } else {
        return false;
    }
    add_ordago_score(scores, winner, points);
    return false;
}

/* Pending contracts are public. Their winners and card values are evaluated
 * only when the last lance closes, or when an ordago is accepted. */
static void compute_score_accounting(
    const MusGameModel *model, const MusHandSummary summaries[MUS_GAME_PLAYER_COUNT],
    uint8_t scores[MUS_GAME_PLAYER_COUNT], uint8_t paid[MUS_GAME_PLAYER_COUNT],
    uint8_t *pending) {
    scores[0] = scores[1] = paid[0] = paid[1] = 0;
    *pending = 0;
    for (int lance = 0; lance < MUS_GAME_LANCE_COUNT; lance += 1) {
        const MusLanceOutcome outcome = model->outcomes[lance];
        if (outcome_is_accepted(outcome) && model->outcome_wager_is_ordago[lance]) {
            (void)settle_ordago_lance_score(model, summaries, lance, scores, true);
            *pending = 0;
            return;
        }
        if (outcome_is_rejected(outcome)) {
            const int winner = outcome_bettor(outcome);
            add_ordago_score(paid, winner, ordago_outcome_stake(model, lance, outcome));
            scores[winner] = paid[winner];
            if (model->score_target != 0 &&
                model->initial_scores[winner] + paid[winner] >= model->score_target) {
                *pending = 0;
                return;
            }
        }
        if (outcome != MUS_LANCE_UNRESOLVED &&
            !(lance == 2 && !model->pairs[0] && !model->pairs[1]) &&
            (lance >= 2 || !outcome_is_rejected(outcome)))
            *pending |= (uint8_t)(1U << lance);
    }
    if (model->outcomes[3] == MUS_LANCE_UNRESOLVED)
        return;
    /* Dejes have priority because they were paid during negotiations. All
     * remaining payments now follow grande, chica, pares, juego/punto. */
    *pending = 0;
    for (int lance = 0; lance < MUS_GAME_LANCE_COUNT; lance += 1) {
        (void)settle_ordago_lance_score(model, summaries, lance, scores, false);
        if (model->score_target != 0 &&
            (model->initial_scores[0] + scores[0] >= model->score_target ||
             model->initial_scores[1] + scores[1] >= model->score_target))
            return;
    }
}

/* Exactly one new contract closes at each call. Retain the public payments
 * from earlier lances, and resolve the pending contracts only at final count.
 * compute_score_accounting remains an independent check of this cache. */
static void update_score_accounting(
    MusGameModel *model, const MusHandSummary summaries[MUS_GAME_PLAYER_COUNT],
    int lance) {
    const MusLanceOutcome outcome = model->outcomes[lance];

    if (outcome_is_accepted(outcome) &&
        model->outcome_wager_is_ordago[lance]) {
        (void)settle_ordago_lance_score(
            model, summaries, lance, model->settled_scores, true);
        model->pending_lances = 0;
        return;
    }
    if (outcome_is_rejected(outcome)) {
        const int winner = outcome_bettor(outcome);

        add_ordago_score(model->paid_scores, winner,
                         ordago_outcome_stake(model, lance, outcome));
        model->settled_scores[winner] = model->paid_scores[winner];
        if (model->score_target != 0 &&
            model->initial_scores[winner] + model->paid_scores[winner] >=
                model->score_target) {
            model->pending_lances = 0;
            return;
        }
    }
    if (!(lance == 2 && !model->pairs[0] && !model->pairs[1]) &&
        (lance >= 2 || !outcome_is_rejected(outcome))) {
        model->pending_lances |= (uint8_t)(1U << lance);
    }
    if (lance != MUS_GAME_LANCE_COUNT - 1)
        return;
    model->pending_lances = 0;
    for (int pending = 0; pending < MUS_GAME_LANCE_COUNT; pending += 1) {
        (void)settle_ordago_lance_score(
            model, summaries, pending, model->settled_scores, false);
        if (score_match_finished(model))
            return;
    }
}

static Status recompute_ordago_settled_scores(
    const MusGameModel *model,
    const MusHandSummary summaries[MUS_GAME_PLAYER_COUNT],
    uint8_t scores[MUS_GAME_PLAYER_COUNT]) {
    if (model == NULL || summaries == NULL || scores == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (model->accounting_mode == MUS_ACCOUNTING_PUBLIC_DEFERRED) {
        uint8_t paid[MUS_GAME_PLAYER_COUNT], pending;
        compute_score_accounting(model, summaries, scores, paid, &pending);
        return CFR_STATUS_SUCCESS;
    }
    scores[0] = 0;
    scores[1] = 0;
    for (int lance = 0; lance < MUS_GAME_LANCE_COUNT; lance += 1) {
        if (settle_ordago_lance_score(model, summaries, lance, scores, true) ||
            (model->score_target != 0 &&
             (model->initial_scores[0] + scores[0] >= model->score_target ||
              model->initial_scores[1] + scores[1] >= model->score_target)))
            break;
    }
    return CFR_STATUS_SUCCESS;
}

static void prepare_final_lance(
    MusGameModel *model, const MusGameFormat *format,
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
        if (format->accounting_mode == MUS_ACCOUNTING_PUBLIC_DEFERRED) {
            update_score_accounting(model, summaries, 3);
        } else if (format->ordago_with_mus) {
            (void)settle_ordago_lance_score(
                model, summaries, 3, model->settled_scores, true);
        }
        model->phase = MUS_PHASE_TERMINAL;
    }
}

static void advance_lance(
    MusGameModel *model, MusLanceOutcome outcome,
    const MusGameFormat *format,
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
        if (format->ordago_with_mus) {
            int contribution;

            if (outcome == MUS_LANCE_FIRST_BET_REJECTED ||
                outcome == MUS_LANCE_SECOND_BET_REJECTED) {
                /* A score game's rejected stake is already in paid_scores. */
                contribution = format->accounting_mode == MUS_ACCOUNTING_PUBLIC_DEFERRED
                                   ? 0 : model->previous_wager;
            } else if (model->current_wager_is_ordago) {
                contribution = model->previous_wager == 1
                                   ? 0
                                   : model->previous_wager;
            } else {
                contribution = model->current_wager;
            }
            const int total = model->prior_wager_total + contribution;

            model->prior_wager_total =
                (uint8_t)(total < MUS_ORDAGO_WAGER ? total
                                                   : MUS_ORDAGO_WAGER);
            if (format->accounting_mode == MUS_ACCOUNTING_PUBLIC_DEFERRED)
                update_score_accounting(model, summaries, lance);
            else
                (void)settle_ordago_lance_score(
                    model, summaries, lance, model->settled_scores, true);
        }
    }
    if (score_match_finished(model) ||
        (format->accounting_mode == MUS_ACCOUNTING_PUBLIC_DEFERRED &&
         model->current_wager_is_ordago && outcome_is_accepted(outcome))) {
        model->phase = MUS_PHASE_TERMINAL;
        return;
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
            if (format->ordago_with_mus) {
                model->outcome_strengths[2] =
                    model->pairs[0]
                        ? MUS_LANCE_STRENGTH_PARES_SKIPPED_PLAYER_0
                    : model->pairs[1]
                        ? MUS_LANCE_STRENGTH_PARES_SKIPPED_PLAYER_1
                        : MUS_LANCE_STRENGTH_PARES_SKIPPED_NONE;
                if (format->accounting_mode == MUS_ACCOUNTING_PUBLIC_DEFERRED)
                    update_score_accounting(model, summaries, 2);
                else
                    (void)settle_ordago_lance_score(
                        model, summaries, 2, model->settled_scores, true);
            }
            if (score_match_finished(model))
                model->phase = MUS_PHASE_TERMINAL;
            else
                prepare_final_lance(model, format, summaries);
        }
    } else if (completed_phase == MUS_PHASE_PARES) {
        prepare_final_lance(model, format, summaries);
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

static bool ordago_initial_increment_is_valid(uint8_t increment) {
    return increment == 2 || increment == 4 || increment == 7 ||
           increment == 10;
}

static bool ordago_response_increment_is_valid(uint8_t increment) {
    return increment == 2 || increment == 4 || increment == 10;
}

static uint8_t ordago_wager_action_bit(uint8_t increment) {
    switch (increment) {
    case 2:
        return 1U << 0;
    case 4:
        return 1U << 1;
    case 7:
        return 1U << 2;
    case 10:
        return 1U << 3;
    default:
        return 0;
    }
}

static bool ordago_wager_action_is_unused(uint8_t used_action_mask,
                                           uint8_t increment) {
    const uint8_t action_bit = ordago_wager_action_bit(increment);

    return action_bit != 0 && (used_action_mask & action_bit) == 0;
}

static uint8_t ordago_numeric_wager_count(
    const uint8_t used_action_masks[MUS_GAME_PLAYER_COUNT]) {
    static const uint8_t counts[MUS_ORDAGO_WAGER_ACTION_MASK_COUNT] = {
        0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
    };

    return (uint8_t)(counts[used_action_masks[0] & 15U] +
                     counts[used_action_masks[1] & 15U]);
}

/* This makes every reachable numeric-wager sequence non-decreasing. */
static uint8_t ordago_minimum_response_increment(uint8_t current_wager,
                                                  uint8_t previous_wager) {
    if (previous_wager == 1)
        return current_wager;
    if (previous_wager < current_wager)
        return (uint8_t)(current_wager - previous_wager);
    return 0;
}

static int recompute_ordago_prior_wager_total(const MusGameModel *model) {
    int total = 0;

    for (int lance = 0; lance < MUS_GAME_LANCE_COUNT; lance += 1) {
        if (model->outcomes[lance] < MUS_LANCE_FIRST_BET_REJECTED ||
            model->outcomes[lance] > MUS_LANCE_SECOND_BET_ACCEPTED) {
            continue;
        }
        if (model->outcomes[lance] == MUS_LANCE_FIRST_BET_REJECTED ||
            model->outcomes[lance] == MUS_LANCE_SECOND_BET_REJECTED) {
            if (model->accounting_mode == MUS_ACCOUNTING_HISTORICAL)
                total += model->outcome_previous_wagers[lance];
            if (total >= MUS_ORDAGO_WAGER)
                return MUS_ORDAGO_WAGER;
            continue;
        }
        if (model->outcome_wager_is_ordago[lance]) {
            const int covered = model->outcome_previous_wagers[lance];

            total += covered == 1 ? 0 : covered;
        } else {
            total += model->outcome_wagers[lance];
        }
        if (total >= MUS_ORDAGO_WAGER)
            return MUS_ORDAGO_WAGER;
    }
    return total;
}

static int ordago_prior_wager_total(const MusGameModel *model) {
    return model->prior_wager_total;
}

static int ordago_max_settled_score(const MusGameModel *model) {
    const int a = model->settled_scores[0] + model->initial_scores[0];
    const int b = model->settled_scores[1] + model->initial_scores[1];
    return a > b ? a : b;
}

/* Pending showdown results must not affect the score game's numeric menu. */
static int ordago_budget_score(const MusGameModel *model) {
    if (model->accounting_mode == MUS_ACCOUNTING_HISTORICAL)
        return ordago_max_settled_score(model);
    const int a = model->initial_scores[0] + model->paid_scores[0];
    const int b = model->initial_scores[1] + model->paid_scores[1];
    return a > b ? a : b;
}

static int ordago_budget_prior_wager_total(const MusGameModel *model) {
    return ordago_prior_wager_total(model);
}

static bool score_match_finished(const MusGameModel *model) {
    return model->score_target != 0 &&
           (model->initial_scores[0] + model->settled_scores[0] >=
                model->score_target ||
            model->initial_scores[1] + model->settled_scores[1] >=
                model->score_target);
}

static bool ordago_numeric_budget_allows(int max_settled_score,
                                         int prior_wager_total,
                                         int active_wager, int increment) {
    return max_settled_score + prior_wager_total + active_wager + increment <
           MUS_ORDAGO_WAGER;
}

static int ordago_active_numeric_wager(uint8_t current_wager,
                                       uint8_t previous_wager,
                                       bool is_ordago) {
    if (!is_ordago)
        return current_wager;
    return previous_wager == 1 ? 0 : previous_wager;
}

static bool ordago_response_increment_is_legal(int max_settled_score,
                                                int prior_wager_total,
                                                uint8_t current_wager,
                                                uint8_t previous_wager,
                                                uint8_t numeric_wager_count,
                                                uint8_t used_action_mask,
                                                uint8_t increment) {
    const uint8_t minimum = ordago_minimum_response_increment(
        current_wager, previous_wager);

    return numeric_wager_count < MUS_ORDAGO_MAX_NUMERIC_WAGERS &&
           minimum > 0 && ordago_response_increment_is_valid(increment) &&
           ordago_wager_action_is_unused(used_action_mask, increment) &&
           increment >= minimum &&
           ordago_numeric_budget_allows(max_settled_score,
                                        prior_wager_total,
                                        current_wager, increment);
}

static bool ordago_offer_is_valid(uint8_t wager, uint8_t previous_wager,
                                  bool is_ordago) {
    if (is_ordago) {
        return wager == MUS_ORDAGO_WAGER && previous_wager >= 1 &&
               previous_wager <= MUS_ORDAGO_WAGER;
    }
    if (previous_wager == 1)
        return ordago_initial_increment_is_valid(wager);
    return previous_wager >= MUS_ORDAGO_MIN_WAGER &&
           previous_wager < wager && wager <= MUS_ORDAGO_WAGER &&
           ordago_response_increment_is_valid(
               (uint8_t)(wager - previous_wager));
}

static bool ordago_numeric_wager_count_is_valid(
    uint8_t previous_wager, bool is_ordago, uint8_t numeric_wager_count) {
    if (numeric_wager_count > MUS_ORDAGO_MAX_NUMERIC_WAGERS)
        return false;
    if (previous_wager == 1)
        return numeric_wager_count == (is_ordago ? 0 : 1);
    if (is_ordago)
        return numeric_wager_count >= 1;
    return numeric_wager_count >= 2;
}

static bool ordago_wager_action_masks_are_valid(
    const uint8_t used_action_masks[MUS_GAME_PLAYER_COUNT],
    uint8_t active_wager, Player last_numeric_player,
    uint8_t latest_numeric_increment) {
    static const uint8_t increments[MUS_ORDAGO_WAGER_ACTION_COUNT] = {
        2, 4, 7, 10,
    };
    uint8_t expected_masks[MUS_GAME_PLAYER_COUNT] = {0};
    unsigned int action_count = 0;
    unsigned int wager_total = 0;
    uint8_t last_increment = 0;
    Player first_numeric_player;
    uint8_t numeric_wager_count;

    if (used_action_masks == NULL ||
        used_action_masks[0] >= MUS_ORDAGO_WAGER_ACTION_MASK_COUNT ||
        used_action_masks[1] >= MUS_ORDAGO_WAGER_ACTION_MASK_COUNT) {
        return false;
    }
    numeric_wager_count =
        ordago_numeric_wager_count(used_action_masks);
    if (numeric_wager_count > MUS_ORDAGO_MAX_NUMERIC_WAGERS)
        return false;
    if (numeric_wager_count == 0) {
        return active_wager == 0 && latest_numeric_increment == 0 &&
               used_action_masks[0] == 0 && used_action_masks[1] == 0;
    }
    if (!player_is_valid(last_numeric_player))
        return false;
    first_numeric_player = numeric_wager_count % 2 == 0
                               ? (Player)(1 - last_numeric_player)
                               : last_numeric_player;
    /*
     * Numeric increments are non-decreasing because the minimum response is
     * the preceding increment. Therefore, ascending value order is also the
     * chronological order and can reconstruct ownership from the masks.
     */
    for (int increment_index = 0;
         increment_index < MUS_ORDAGO_WAGER_ACTION_COUNT;
         increment_index += 1) {
        const uint8_t increment = increments[increment_index];
        const uint8_t action_bit = 1U << increment_index;

        for (int occurrence = 0; occurrence < MUS_GAME_PLAYER_COUNT;
             occurrence += 1) {
            Player player;

            if ((used_action_masks[occurrence] & action_bit) == 0)
                continue;
            if (action_count > 0 &&
                !ordago_response_increment_is_valid(increment)) {
                return false;
            }
            player = action_count % 2 == 0
                         ? first_numeric_player
                         : (Player)(1 - first_numeric_player);
            expected_masks[player] |= action_bit;
            action_count += 1;
            wager_total += increment;
            last_increment = increment;
        }
    }
    return action_count == numeric_wager_count &&
           wager_total == active_wager &&
           (latest_numeric_increment == 0 ||
            last_increment == latest_numeric_increment) &&
           expected_masks[0] == used_action_masks[0] &&
           expected_masks[1] == used_action_masks[1];
}

static Player ordago_last_numeric_player(Player current_player,
                                         Player bettor,
                                         bool current_wager_is_ordago) {
    return current_wager_is_ordago ? current_player : bettor;
}

static bool ordago_wager_action_history_is_valid(
    uint8_t current_wager, uint8_t previous_wager,
    Player current_player, Player bettor, bool current_wager_is_ordago,
    const uint8_t used_action_masks[MUS_GAME_PLAYER_COUNT]) {
    uint8_t active_wager;
    uint8_t latest_numeric_increment;
    Player last_numeric_player;
    const uint8_t numeric_wager_count =
        ordago_numeric_wager_count(used_action_masks);

    if (numeric_wager_count == 0) {
        return ordago_wager_action_masks_are_valid(
            used_action_masks, 0, CFR_PLAYER_0, 0);
    }
    if (!player_is_valid(current_player) || !player_is_valid(bettor) ||
        current_wager == 0 || previous_wager == 0 ||
        (!current_wager_is_ordago && previous_wager >= current_wager)) {
        return false;
    }
    active_wager = (uint8_t)ordago_active_numeric_wager(
        current_wager, previous_wager, current_wager_is_ordago);
    last_numeric_player = ordago_last_numeric_player(
        current_player, bettor, current_wager_is_ordago);
    latest_numeric_increment =
        current_wager_is_ordago
            ? 0
            : (previous_wager == 1
                   ? current_wager
                   : (uint8_t)(current_wager - previous_wager));
    return ordago_wager_action_masks_are_valid(
        used_action_masks, active_wager, last_numeric_player,
        latest_numeric_increment);
}

static Status collect_ordago_betting_actions_unchecked(
    MusBetStage bet_stage, int max_settled_score, int prior_wager_total,
    uint8_t current_wager, uint8_t previous_wager, Player current_player,
    const uint8_t used_action_masks[MUS_GAME_PLAYER_COUNT],
    bool current_wager_is_ordago,
    Action *actions, size_t capacity, size_t *required_count) {
    static const Action wager_actions[4] = {
        MUS_ACTION_BET_TWO, MUS_ACTION_BET_FOUR,
        MUS_ACTION_BET_SEVEN, MUS_ACTION_BET_TEN,
    };
    static const uint8_t increments[4] = {2, 4, 7, 10};
    Action temporary[7];
    Action *output = capacity >= 7 ? actions : temporary;
    size_t count = 0;
    const uint8_t used = used_action_masks[current_player];
    const int budget = MUS_ORDAGO_WAGER - max_settled_score - prior_wager_total;

    if (bet_stage == MUS_BET_RESPONSE) {
        output[count++] = MUS_ACTION_REJECT;
        output[count++] = MUS_ACTION_ACCEPT;
        if (!current_wager_is_ordago) {
            const uint8_t minimum = ordago_minimum_response_increment(
                current_wager, previous_wager);

            if (minimum > 0 && ordago_numeric_wager_count(used_action_masks) <
                                   MUS_ORDAGO_MAX_NUMERIC_WAGERS) {
                for (int index = 0; index < 4; index += 1) {
                    if (index != 2 && (used & (1U << index)) == 0 &&
                        increments[index] >= minimum &&
                        increments[index] < budget - current_wager) {
                        output[count++] = wager_actions[index];
                    }
                }
            }
            output[count++] = MUS_ACTION_ORDAGO;
        }
    } else {
        output[count++] = MUS_ACTION_PASS;
        for (int index = 0; index < 4; index += 1) {
            if (increments[index] < budget &&
                (used & (1U << index)) == 0) {
                output[count++] = wager_actions[index];
            }
        }
        output[count++] = MUS_ACTION_ORDAGO;
    }
    *required_count = count;
    if (capacity < count)
        return CFR_STATUS_BUFFER_TOO_SMALL;
    if (output == temporary)
        memcpy(actions, temporary, count * sizeof(*actions));
    return CFR_STATUS_SUCCESS;
}

static Status collect_ordago_betting_actions(
    MusBetStage bet_stage, int max_settled_score, int prior_wager_total,
    uint8_t current_wager, uint8_t previous_wager, Player current_player,
    Player bettor,
    const uint8_t used_action_masks[MUS_GAME_PLAYER_COUNT],
    bool current_wager_is_ordago,
    Action *actions, size_t capacity, size_t *required_count) {
    if (actions == NULL || required_count == NULL ||
        max_settled_score < 0 || max_settled_score > MUS_ORDAGO_WAGER ||
        prior_wager_total < 0 || prior_wager_total > MUS_ORDAGO_WAGER ||
        !player_is_valid(current_player) || !player_is_valid(bettor) ||
        used_action_masks == NULL ||
        bet_stage < MUS_BET_FIRST_TO_ACT ||
        bet_stage > MUS_BET_RESPONSE) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    if (bet_stage == MUS_BET_RESPONSE) {
        const int active_wager = ordago_active_numeric_wager(
            current_wager, previous_wager, current_wager_is_ordago);
        const uint8_t numeric_wager_count =
            ordago_numeric_wager_count(used_action_masks);

        if (current_player == bettor ||
            !ordago_offer_is_valid(current_wager, previous_wager,
                                   current_wager_is_ordago) ||
            !ordago_numeric_wager_count_is_valid(
                previous_wager, current_wager_is_ordago,
                numeric_wager_count) ||
            !ordago_wager_action_history_is_valid(
                current_wager, previous_wager, current_player, bettor,
                current_wager_is_ordago, used_action_masks) ||
            (active_wager > 0 &&
             !ordago_numeric_budget_allows(
                 max_settled_score, prior_wager_total, active_wager, 0))) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
    } else {
        if (current_wager != 0 || previous_wager != 0 ||
            used_action_masks[0] != 0 || used_action_masks[1] != 0 ||
            current_wager_is_ordago) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
    }
    return collect_ordago_betting_actions_unchecked(
        bet_stage, max_settled_score, prior_wager_total, current_wager,
        previous_wager, current_player, used_action_masks,
        current_wager_is_ordago, actions, capacity, required_count);
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
        const bool skipped_pairs_summary =
            format->ordago_with_mus && outcome == MUS_LANCE_SKIPPED &&
            strength >= MUS_LANCE_STRENGTH_PARES_SKIPPED_NONE &&
            strength <= MUS_LANCE_STRENGTH_PARES_SKIPPED_PLAYER_1;

        return wager == 0 && previous_wager == 0 && !is_ordago &&
               (strength == MUS_LANCE_STRENGTH_NONE ||
                skipped_pairs_summary);
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
    return true;
}

static Status validate_model(const MusGameModel *model,
                             const MusGameFormat *format) {
    size_t out_of_play_total = 0;
    size_t discard_total = 0;
    bool accepted_ordago = false;

    if (model == NULL || format == NULL ||
        model->phase > MUS_PHASE_REDRAW_PLAYER_1 ||
        !player_is_valid(model->current_player) ||
        !player_is_valid(model->bettor)) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    if (model->accounting_mode != format->accounting_mode)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (format->score_enabled) {
        if (model->score_target == 0 || model->score_target > MUS_ORDAGO_WAGER ||
            model->initial_scores[0] >= model->score_target ||
            model->initial_scores[1] >= model->score_target)
            return CFR_STATUS_INVALID_ARGUMENT;
    } else if (model->score_target != 0 || model->initial_scores[0] != 0 ||
               model->initial_scores[1] != 0) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    if (format->accounting_mode == MUS_ACCOUNTING_HISTORICAL &&
        (model->paid_scores[0] != 0 || model->paid_scores[1] != 0 ||
         model->pending_lances != 0))
        return CFR_STATUS_INVALID_ARGUMENT;
    if (score_match_finished(model) && model->phase != MUS_PHASE_TERMINAL)
        return CFR_STATUS_INVALID_ARGUMENT;
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
        if (format->accounting_mode == MUS_ACCOUNTING_PUBLIC_DEFERRED) {
            if (accepted_ordago && model->outcomes[lance] != MUS_LANCE_SKIPPED)
                return CFR_STATUS_INVALID_ARGUMENT;
            if (model->outcome_wager_is_ordago[lance] &&
                outcome_is_accepted(model->outcomes[lance])) {
                if (model->phase != MUS_PHASE_TERMINAL)
                    return CFR_STATUS_INVALID_ARGUMENT;
                accepted_ordago = true;
            }
        }
        const bool skipped_pairs_strength =
            model->outcome_strengths[lance] >=
                MUS_LANCE_STRENGTH_PARES_SKIPPED_NONE &&
            model->outcome_strengths[lance] <=
                MUS_LANCE_STRENGTH_PARES_SKIPPED_PLAYER_1;

        if (model->outcomes[lance] > MUS_LANCE_SKIPPED ||
            (skipped_pairs_strength && lance != 2) ||
            !outcome_wager_is_valid(
                format, model->outcomes[lance],
                model->outcome_wagers[lance],
                model->outcome_previous_wagers[lance],
                model->outcome_wager_is_ordago[lance],
                model->outcome_strengths[lance])) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
    }
    if (format->ordago_with_mus &&
        (ordago_prior_wager_total(model) > MUS_ORDAGO_WAGER ||
         !ordago_wager_action_history_is_valid(
             model->current_wager, model->previous_wager,
             (Player)model->current_player, (Player)model->bettor,
             model->current_wager_is_ordago,
             model->used_wager_action_masks) ||
         ordago_prior_wager_total(model) !=
             recompute_ordago_prior_wager_total(model)))
        return CFR_STATUS_INVALID_ARGUMENT;
    if (!format->ordago_with_mus &&
        (model->used_wager_action_masks[0] != 0 ||
         model->used_wager_action_masks[1] != 0))
        return CFR_STATUS_INVALID_ARGUMENT;
    if (model->mus_count > MUS_GAME_MAX_MUSES)
        return CFR_STATUS_INVALID_ARGUMENT;
    for (int round = 0; round < MUS_GAME_MAX_MUSES; round += 1) {
        for (int player = 0; player < MUS_GAME_PLAYER_COUNT; player += 1) {
            const uint8_t mask = model->discard_masks[round][player];
            const bool completed_entry = round < model->mus_count;
            const bool current_player_0_entry =
                model->phase == MUS_PHASE_DISCARD_PLAYER_1 &&
                round == model->mus_count && player == 0;
            const bool entry_is_present =
                completed_entry || current_player_0_entry;
            unsigned int count = 0;

            if (mask >= MUS_GAME_DISCARD_MASK_COUNT ||
                model->discard_counts[round][player] > MUS_GAME_HAND_SIZE) {
                return CFR_STATUS_INVALID_ARGUMENT;
            }
            for (int bit = 0; bit < MUS_GAME_HAND_SIZE; bit += 1)
                count += (mask >> bit) & 1U;
            if (count != model->discard_counts[round][player] ||
                (entry_is_present && mask == 0) ||
                (!entry_is_present && mask != 0)) {
                return CFR_STATUS_INVALID_ARGUMENT;
            }
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
                const int active_wager = ordago_active_numeric_wager(
                    model->current_wager, model->previous_wager,
                    model->current_wager_is_ordago);
                const uint8_t numeric_wager_count =
                    ordago_numeric_wager_count(
                        model->used_wager_action_masks);

                if (!ordago_offer_is_valid(
                        model->current_wager, model->previous_wager,
                        model->current_wager_is_ordago) ||
                    !ordago_numeric_wager_count_is_valid(
                        model->previous_wager,
                        model->current_wager_is_ordago,
                        numeric_wager_count) ||
                    (active_wager > 0 &&
                     !ordago_numeric_budget_allows(
                         ordago_budget_score(model),
                         ordago_budget_prior_wager_total(model), active_wager, 0)) ||
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
             model->used_wager_action_masks[0] != 0 ||
             model->used_wager_action_masks[1] != 0 ||
             model->current_wager_is_ordago ||
             model->current_strength != MUS_LANCE_STRENGTH_NONE)) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
        if (model->bet_stage > MUS_BET_RESPONSE) {
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

static bool ordago_hand_codes_are_equal(const MusOrdagoHandCodes *left,
                                        const MusOrdagoHandCodes *right) {
    return left->exact == right->exact && left->grande == right->grande &&
           left->chica == right->chica && left->pairs == right->pairs;
}

static Status validate_ordago_hand_codes(
    const MusGameModel *model,
    const MusOrdagoHandCodes codes[MUS_GAME_PLAYER_COUNT]) {
    MusOrdagoHandCodes expected[MUS_GAME_PLAYER_COUNT];

    if (summarize_ordago_hand_codes(model, expected) != CFR_STATUS_SUCCESS)
        return CFR_STATUS_INVALID_ARGUMENT;
    for (int player = 0; player < MUS_GAME_PLAYER_COUNT; player += 1) {
        if (!ordago_hand_codes_are_equal(&codes[player], &expected[player]))
            return CFR_STATUS_INVALID_ARGUMENT;
    }
    return CFR_STATUS_SUCCESS;
}

static Status validate_state_data(const MusGameState *state,
                                  const MusGameFormat *format) {
    Status status;

    if (state == NULL || format == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (format->score_enabled &&
        (state->continuation == NULL ||
         state->continuation->target != state->model.score_target))
        return CFR_STATUS_INVALID_ARGUMENT;
    status = validate_model(&state->model, format);
    if (status != CFR_STATUS_SUCCESS || phase_is_deal(state->model.phase) ||
        phase_is_discard(state->model.phase))
        return status;
    if (format->ordago_with_mus) {
        status = validate_ordago_hand_codes(&state->model,
                                            state->ordago_hand_codes);
        if (status != CFR_STATUS_SUCCESS)
            return status;
    }
    status = validate_hand_summaries(&state->model, state->hand_summaries);
    if (status != CFR_STATUS_SUCCESS || !format->ordago_with_mus)
        return status;

    uint8_t expected_scores[MUS_GAME_PLAYER_COUNT];
    if (format->accounting_mode == MUS_ACCOUNTING_PUBLIC_DEFERRED) {
        uint8_t paid[MUS_GAME_PLAYER_COUNT], pending;
        compute_score_accounting(&state->model, state->hand_summaries,
                                 expected_scores, paid, &pending);
        if (memcmp(state->model.paid_scores, paid, sizeof(paid)) != 0 ||
            state->model.pending_lances != pending)
            return CFR_STATUS_INVALID_ARGUMENT;
    }
    status = recompute_ordago_settled_scores(
        &state->model, state->hand_summaries, expected_scores);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    for (int player = 0; player < MUS_GAME_PLAYER_COUNT; player += 1) {
        if (state->model.settled_scores[player] != expected_scores[player])
            return CFR_STATUS_INVALID_ARGUMENT;
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

Status mus_ordago_public_game_state_init(MusOrdagoPublicGameState *state) {
    const Status status = mus_game_state_init(state);
    if (status == CFR_STATUS_SUCCESS)
        state->model.accounting_mode = MUS_ACCOUNTING_PUBLIC_DEFERRED;
    return status;
}

const Game *mus_ordago_public_game_descriptor(void) {
    return &MUS_ORDAGO_PUBLIC_GAME;
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

static int ordago_showdown_winner(
    const MusGameModel *model,
    const MusHandSummary summaries[MUS_GAME_PLAYER_COUNT], int lance) {
    if (lance == 0)
        return summaries[1].grande_key > summaries[0].grande_key ? 1 : 0;
    if (lance == 1)
        return summaries[1].chica_key > summaries[0].chica_key ? 1 : 0;
    if (lance == 2)
        return summaries[1].pairs_key > summaries[0].pairs_key ? 1 : 0;
    if (model->game[0] && model->game[1])
        return summaries[1].game_key > summaries[0].game_key ? 1 : 0;
    return summaries[1].point_value > summaries[0].point_value ? 1 : 0;
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

    if (format->ordago_with_mus) {
        for (int lance = 0; lance < MUS_GAME_LANCE_COUNT; lance += 1) {
            if (model->outcome_wager_is_ordago[lance] &&
                outcome_is_accepted(model->outcomes[lance])) {
                winner = ordago_showdown_winner(model, summaries, lance);
                scores[winner] = MUS_ORDAGO_WAGER;
                return CFR_STATUS_SUCCESS;
            }
        }
    }

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
    if (format->ordago_with_mus) {
        for (int player = 0; player < MUS_GAME_PLAYER_COUNT; player += 1) {
            if (scores[player] > MUS_ORDAGO_WAGER)
                scores[player] = MUS_ORDAGO_WAGER;
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
    if (game_format(context)->score_enabled)
        return mus_score_terminal_utility(mus, player, result);
    if (game_format(context)->ordago_with_mus) {
        const uint8_t *cached_scores = mus->model.settled_scores;

        *result = player == CFR_PLAYER_0
                      ? (Utility)((int)cached_scores[0] -
                                  (int)cached_scores[1])
                      : (Utility)((int)cached_scores[1] -
                                  (int)cached_scores[0]);
        return CFR_STATUS_SUCCESS;
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
        Action temporary[MUS_GAME_DISCARD_MASK_COUNT - 1];
        const uint8_t *hand =
            mus->model.hands[mus->model.current_player];
        size_t count = 0;
        unsigned int equal_neighbors = 0;
        unsigned int position = 0;

        /* Within each class, a selected card must include the card before it.
         * Build the adjacent equal-card positions once for all 15 masks. */
        for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT;
             card_class += 1) {
            const unsigned int cards = hand[card_class];

            if (cards > 1)
                equal_neighbors |= ((1U << (cards - 1)) - 1U) << position;
            position += cards;
        }

        for (unsigned int mask = 1; mask < MUS_GAME_DISCARD_MASK_COUNT;
             mask += 1) {
            if (((mask >> 1) & ~mask & equal_neighbors) == 0)
                temporary[count++] = MUS_ACTION_DISCARD_MASK(mask);
        }
        *required_count = count;
        if (capacity < count)
            return CFR_STATUS_BUFFER_TOO_SMALL;
        memcpy(actions, temporary, count * sizeof(*actions));
        return CFR_STATUS_SUCCESS;
    }
    if (!phase_is_betting(mus->model.phase))
        return CFR_STATUS_INVALID_ARGUMENT;
    if (format != NULL && format->ordago_with_mus) {
        return collect_ordago_betting_actions_unchecked(
            mus->model.bet_stage, ordago_budget_score(&mus->model),
            ordago_budget_prior_wager_total(&mus->model),
            mus->model.current_wager, mus->model.previous_wager,
            (Player)mus->model.current_player,
            mus->model.used_wager_action_masks,
            mus->model.current_wager_is_ordago, actions, capacity,
            required_count);
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
                                   Action action, uint8_t ordago_wager) {
    if (format != NULL && format->ordago_with_mus) {
        const int max_settled_score = ordago_budget_score(model);
        const int prior_wager_total = ordago_budget_prior_wager_total(model);

        if (model->bet_stage == MUS_BET_RESPONSE) {
            return action == MUS_ACTION_REJECT ||
                   action == MUS_ACTION_ACCEPT ||
                   (!model->current_wager_is_ordago &&
                    (action == MUS_ACTION_ORDAGO ||
                     ordago_response_increment_is_legal(
                         max_settled_score, prior_wager_total,
                         model->current_wager, model->previous_wager,
                         ordago_numeric_wager_count(
                             model->used_wager_action_masks),
                         model->used_wager_action_masks[
                             model->current_player],
                         ordago_wager)));
        }
        return action == MUS_ACTION_PASS || action == MUS_ACTION_ORDAGO ||
               (ordago_initial_increment_is_valid(ordago_wager) &&
                ordago_wager_action_is_unused(
                    model->used_wager_action_masks[
                        model->current_player],
                    ordago_wager) &&
                ordago_numeric_budget_allows(
                    max_settled_score, prior_wager_total, 0,
                    ordago_wager));
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
        if (!mus_ordago_game_discard_mask_is_canonical(
                model->hands[player], (unsigned int)raw_mask)) {
            return CFR_STATUS_ILLEGAL_ACTION;
        }
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
    const uint8_t ordago_wager =
        format->ordago_with_mus ? ordago_wager_for_action(action) : 0;

    if (!player_action_is_legal(model, format, action, ordago_wager))
        return CFR_STATUS_ILLEGAL_ACTION;

    if (model->bet_stage == MUS_BET_FIRST_TO_ACT) {
        if (action == MUS_ACTION_PASS) {
            model->bet_stage = MUS_BET_SECOND_AFTER_PASS;
            model->current_player = CFR_PLAYER_1;
        } else {
            const uint8_t wager = format->ordago_with_mus
                                      ? ordago_wager
                                      : MUS_SINGLE_BET_WAGER;

            model->bet_stage = MUS_BET_RESPONSE;
            model->bettor = CFR_PLAYER_0;
            model->current_player = CFR_PLAYER_1;
            model->current_wager = wager;
            if (format->ordago_with_mus) {
                model->previous_wager = 1;
                if (action != MUS_ACTION_ORDAGO) {
                    model->used_wager_action_masks[CFR_PLAYER_0] |=
                        ordago_wager_action_bit(ordago_wager);
                }
                model->current_wager_is_ordago =
                    action == MUS_ACTION_ORDAGO;
                model->current_strength =
                    MUS_LANCE_STRENGTH_PLAYER_0;
            }
        }
    } else if (model->bet_stage == MUS_BET_SECOND_AFTER_PASS) {
        if (action == MUS_ACTION_PASS) {
            advance_lance(model, MUS_LANCE_ALL_PASS, format, summaries);
        } else {
            const uint8_t wager = format->ordago_with_mus
                                      ? ordago_wager
                                      : MUS_SINGLE_BET_WAGER;

            model->bet_stage = MUS_BET_RESPONSE;
            model->bettor = CFR_PLAYER_1;
            model->current_player = CFR_PLAYER_0;
            model->current_wager = wager;
            if (format->ordago_with_mus) {
                model->previous_wager = 1;
                if (action != MUS_ACTION_ORDAGO) {
                    model->used_wager_action_masks[CFR_PLAYER_1] |=
                        ordago_wager_action_bit(ordago_wager);
                }
                model->current_wager_is_ordago =
                    action == MUS_ACTION_ORDAGO;
                model->current_strength =
                    MUS_LANCE_STRENGTH_PLAYER_1;
            }
        }
    } else {
        MusLanceOutcome outcome;
        bool accepted_ordago;
        int completed_lance;

        if (action == MUS_ACTION_RAISE_DOUBLE ||
            (format->ordago_with_mus && ordago_wager > 0)) {
            const Player previous_bettor = model->bettor;
            const Player raising_player = model->current_player;
            const uint8_t next_wager =
                format->ordago_with_mus
                    ? (action == MUS_ACTION_ORDAGO
                           ? MUS_ORDAGO_WAGER
                           : (uint8_t)(model->current_wager + ordago_wager))
                    : (uint8_t)(model->current_wager * 2);

            model->bettor = model->current_player;
            model->current_player = previous_bettor;
            if (format->ordago_with_mus) {
                model->previous_wager = model->current_wager;
                if (action != MUS_ACTION_ORDAGO) {
                    model->used_wager_action_masks[raising_player] |=
                        ordago_wager_action_bit(ordago_wager);
                }
                model->current_wager_is_ordago =
                    action == MUS_ACTION_ORDAGO;
                model->current_strength = MUS_LANCE_STRENGTH_BOTH;
            }
            model->current_wager = next_wager;
            return CFR_STATUS_SUCCESS;
        }

        if (format->ordago_with_mus && action == MUS_ACTION_ACCEPT)
            model->current_strength = MUS_LANCE_STRENGTH_BOTH;

        accepted_ordago = format->ordago_with_mus &&
                           model->current_wager_is_ordago &&
                           action == MUS_ACTION_ACCEPT;
        completed_lance = phase_lance_index(model->phase);

        if (model->bettor == CFR_PLAYER_0) {
            outcome = action == MUS_ACTION_ACCEPT
                          ? MUS_LANCE_FIRST_BET_ACCEPTED
                          : MUS_LANCE_FIRST_BET_REJECTED;
        } else {
            outcome = action == MUS_ACTION_ACCEPT
                          ? MUS_LANCE_SECOND_BET_ACCEPTED
                          : MUS_LANCE_SECOND_BET_REJECTED;
        }
        advance_lance(model, outcome, format, summaries);
        if (accepted_ordago) {
            const int winner =
                ordago_showdown_winner(model, summaries, completed_lance);

            for (int lance = completed_lance + 1;
                 lance < MUS_GAME_LANCE_COUNT; lance += 1) {
                model->outcomes[lance] = MUS_LANCE_SKIPPED;
                model->outcome_wagers[lance] = 0;
                model->outcome_previous_wagers[lance] = 0;
                model->outcome_wager_is_ordago[lance] = false;
                model->outcome_strengths[lance] =
                    MUS_LANCE_STRENGTH_NONE;
            }
            model->settled_scores[0] = 0;
            model->settled_scores[1] = 0;
            model->settled_scores[winner] = MUS_ORDAGO_WAGER;
            model->phase = MUS_PHASE_TERMINAL;
        }
    }
    return CFR_STATUS_SUCCESS;
}

static Status apply_action_to_state(MusGameState *mus, Action action,
                                    const MusGameFormat *format) {
    MusGameModel temporary;
    MusHandSummary completed_summaries[MUS_GAME_PLAYER_COUNT];
    MusOrdagoHandCodes completed_codes[MUS_GAME_PLAYER_COUNT];
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
        if (format->ordago_with_mus) {
            status = summarize_ordago_hand_codes(&temporary,
                                                 completed_codes);
            if (status != CFR_STATUS_SUCCESS)
                return status;
        }
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
        if (format->ordago_with_mus) {
            memcpy(mus->ordago_hand_codes, completed_codes,
                   sizeof(completed_codes));
        }
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

/* Cards and mus history stay fixed throughout the betting subtree. */
static void copy_ordago_betting_model(MusGameModel *target,
                                      const MusGameModel *source) {
    const size_t offset = offsetof(MusGameModel, current_wager);

    target->phase = source->phase;
    memcpy((unsigned char *)target + offset,
           (const unsigned char *)source + offset, sizeof(*target) - offset);
}

static Status apply_ordago_trusted_action_to_state(MusGameState *mus,
                                                    Action action,
                                                    const MusGameFormat *format) {
    MusHandSummary completed_summaries[MUS_GAME_PLAYER_COUNT];
    MusOrdagoHandCodes completed_codes[MUS_GAME_PLAYER_COUNT];
    MusGameModel *undo_model;
    bool completed_deal;
    Status status;

    if (mus == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (mus->undo_count >= MUS_GAME_UNDO_CAPACITY)
        return CFR_STATUS_BUFFER_TOO_SMALL;

    undo_model = &mus->undo_history[mus->undo_count];
    if (phase_is_betting(mus->model.phase)) {
        copy_ordago_betting_model(undo_model, &mus->model);
        status = apply_to_model(&mus->model, format, action,
                                mus->hand_summaries);
        if (status == CFR_STATUS_SUCCESS)
            mus->undo_count += 1;
        else
            copy_ordago_betting_model(&mus->model, undo_model);
        return status;
    }
    *undo_model = mus->model;
    memcpy(mus->undo_hand_summaries[mus->undo_count],
           mus->hand_summaries, sizeof(mus->hand_summaries));
    status = apply_to_model(&mus->model, format, action,
                            mus->hand_summaries);
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
        status = summarize_ordago_hand_codes(&mus->model,
                                             completed_codes);
        if (status != CFR_STATUS_SUCCESS) {
            mus->model = *undo_model;
            return status;
        }
        memcpy(mus->hand_summaries, completed_summaries,
               sizeof(completed_summaries));
        memcpy(mus->ordago_hand_codes, completed_codes,
               sizeof(completed_codes));
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
    return apply_ordago_trusted_action_to_state(as_mus(state), action,
                                                game_format(context));
}

static Status mus_undo_action(const void *context, GameState *state) {
    MusGameState *mus = as_mus(state);
    const MusGameFormat *format = game_format(context);

    if (validate_state_data(mus, format) != CFR_STATUS_SUCCESS) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    if (format->ordago_with_mus)
        return mus_ordago_trusted_undo_action(context, state);
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

static Status mus_ordago_trusted_undo_action(const void *context,
                                             GameState *state) {
    MusGameState *mus = as_mus(state);

    if (mus == NULL || mus->undo_count == 0 ||
        mus->undo_count > MUS_GAME_UNDO_CAPACITY)
        return CFR_STATUS_INVALID_ARGUMENT;
    const MusGameModel *undo_model = &mus->undo_history[mus->undo_count - 1];
    if (phase_is_betting(undo_model->phase)) {
        mus->undo_count -= 1;
        copy_ordago_betting_model(&mus->model, undo_model);
        return CFR_STATUS_SUCCESS;
    }
    Status status = mus_trusted_undo_action(context, state);

    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (phase_is_mus_decision(mus->model.phase) ||
        mus->model.phase == MUS_PHASE_DISCARD_PLAYER_0) {
        status = summarize_ordago_hand_codes(&mus->model,
                                             mus->ordago_hand_codes);
    } else if (mus->model.phase == MUS_PHASE_DISCARD_PLAYER_1) {
        const int player = mus->model.current_player;
        const int exact = compact_hand_code(mus->model.hands[player]);

        if (exact < 0)
            return CFR_STATUS_INVALID_ARGUMENT;
        mus->ordago_hand_codes[player].exact = (int16_t)exact;
    }
    if (status != CFR_STATUS_SUCCESS)
        return CFR_STATUS_INVALID_ARGUMENT;
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
    MUS_ORDAGO_MUS_SITUATION_COUNT = MUS_GAME_MAX_MUSES * 4,
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

static Status summarize_ordago_hand_codes(
    const MusGameModel *model,
    MusOrdagoHandCodes codes[MUS_GAME_PLAYER_COUNT]) {
    if (model == NULL || codes == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    for (int player = 0; player < MUS_GAME_PLAYER_COUNT; player += 1) {
        const uint8_t *hand = model->hands[player];
        const int exact = compact_hand_code(hand);
        const int grande = grande_hand_code(hand);
        const int chica = chica_hand_code(hand);
        const int pairs = pairs_hand_code(hand);

        if (exact < 0 || grande < 0 || chica < 0)
            return CFR_STATUS_INVALID_ARGUMENT;
        codes[player].exact = (int16_t)exact;
        codes[player].grande = (int8_t)grande;
        codes[player].chica = (int8_t)chica;
        codes[player].pairs = (int8_t)pairs;
    }
    return CFR_STATUS_SUCCESS;
}

static int ordago_hand_code(const MusGameModel *model,
                            const MusOrdagoHandCodes *codes,
                            const MusHandSummary *summary) {

    if (phase_is_mus_decision(model->phase) ||
        phase_is_discard(model->phase)) {
        return codes->exact;
    }
    if (model->phase == MUS_PHASE_GRANDE)
        return codes->grande;
    if (model->phase == MUS_PHASE_CHICA)
        return codes->chica;
    if (model->phase == MUS_PHASE_PARES)
        return codes->pairs;
    if (model->phase == MUS_PHASE_JUEGO)
        return summary->game_key;
    if (model->phase == MUS_PHASE_PUNTO && summary->point_value >= 4 &&
        summary->point_value <= 30) {
        return summary->point_value - 4;
    }
    return -1;
}

static int ordago_discard_history_code(const MusGameModel *model,
                                        int completed_rounds) {
    int code = 0;

    if (completed_rounds < 0 || completed_rounds > MUS_GAME_MAX_MUSES)
        return -1;
    for (int round = 0; round < completed_rounds; round += 1) {
        for (int player = 0; player < MUS_GAME_PLAYER_COUNT; player += 1) {
            const int count = model->discard_counts[round][player];

            if (count < 1 || count > MUS_GAME_HAND_SIZE)
                return -1;
            code = code * MUS_ORDAGO_DISCARD_COUNT_RADIX + count - 1;
        }
    }
    return code;
}

static int ordago_public_mus_radix(void) {
    return MUS_ORDAGO_PUBLIC_MUS_RADIX;
}

static int ordago_public_mus_code(const MusGameModel *model) {
    const int completed_rounds = model->mus_count;
    const int discard_code =
        ordago_discard_history_code(model, completed_rounds);
    int offset = 0;
    int history_radix = 1;

    if (discard_code < 0)
        return -1;
    for (int round = 0; round < completed_rounds; round += 1) {
        offset += MUS_GAME_PLAYER_COUNT * history_radix;
        history_radix *= MUS_ORDAGO_DISCARD_ROUND_RADIX;
    }
    if (model->mus_stopped) {
        if (completed_rounds >= MUS_GAME_MAX_MUSES ||
            !player_is_valid(model->mus_stop_player)) {
            return -1;
        }
        return offset + (int)model->mus_stop_player * history_radix +
               discard_code;
    }
    if (completed_rounds != MUS_GAME_MAX_MUSES)
        return -1;
    return offset + discard_code;
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

static int ordago_strength_radix(int lance) {
    return lance == 2 ? MUS_ORDAGO_PAIRS_STRENGTH_RADIX
                      : MUS_ORDAGO_STRENGTH_RADIX;
}

static InfoSetKey score_betting_information_set_key(const MusGameModel *model, int hand) {
    InfoSetKey key = (InfoSetKey)hand * ordago_public_mus_radix() + ordago_public_mus_code(model);
    for (int lance = 0; lance < ordago_previous_strength_count(model->phase); lance += 1)
        key = key * ordago_strength_radix(lance) + model->outcome_strengths[lance];
    for (int player = 0; player < MUS_GAME_PLAYER_COUNT; player += 1)
        key = key * MUS_ORDAGO_SCORE_RADIX + model->initial_scores[player] + model->paid_scores[player];
    key = key * MUS_ORDAGO_TOTAL_WAGER_RADIX + model->prior_wager_total;
    if (model->bet_stage == MUS_BET_RESPONSE) {
        const unsigned int response = ((unsigned int)model->used_wager_action_masks[0] *
                              MUS_ORDAGO_WAGER_ACTION_MASK_RADIX +
                              model->used_wager_action_masks[1]) * 2 + model->current_wager_is_ordago;
        key = key * MUS_SCORE_RESPONSE_RADIX + response;
    }
    return key * MUS_ORDAGO_SITUATION_COUNT + ordago_situation_code(model);
}

static InfoSetKey ordago_information_set_key(
    const MusGameModel *model, const MusHandSummary *summaries,
    const MusOrdagoHandCodes *codes) {
    const int situation = ordago_situation_code(model);
    const int current_player_index = model->current_player;
    const int hand =
        ordago_hand_code(model, &codes[current_player_index],
                         &summaries[current_player_index]);
    InfoSetKey key;

    if (model->accounting_mode == MUS_ACCOUNTING_PUBLIC_DEFERRED && phase_is_betting(model->phase))
        return score_betting_information_set_key(model, hand);
    key = hand;
    if (phase_is_mus_decision(model->phase) ||
        phase_is_discard(model->phase)) {
        for (int round = 0; round < model->mus_count; round += 1) {
            for (int player = 0; player < MUS_GAME_PLAYER_COUNT;
                 player += 1) {
                const int count = model->discard_counts[round][player];

                key = key * MUS_ORDAGO_DISCARD_COUNT_RADIX + count - 1;
            }
        }
        if (model->phase == MUS_PHASE_DISCARD_PLAYER_1) {
            key = key * MUS_ORDAGO_DISCARD_COUNT_RADIX +
                  model->discard_counts[model->mus_count][0] - 1;
        }
    } else {
        const int mus = ordago_public_mus_code(model);
        const int mus_radix = ordago_public_mus_radix();
        const int prior_wager_total = ordago_prior_wager_total(model);
        const int max_settled_score = ordago_max_settled_score(model);
        const int strength_count =
            ordago_previous_strength_count(model->phase);

        key = key * mus_radix + mus;
        for (int lance = 0; lance < strength_count; lance += 1) {
            const int strength_radix = ordago_strength_radix(lance);

            key = key * strength_radix +
                  model->outcome_strengths[lance];
        }
        key = key * MUS_ORDAGO_SCORE_RADIX + max_settled_score;
        key = key * MUS_ORDAGO_TOTAL_WAGER_RADIX + prior_wager_total;
        if (model->bet_stage == MUS_BET_RESPONSE) {
            key = key * MUS_ORDAGO_WAGER_RADIX + model->current_wager;
            key = key * MUS_ORDAGO_WAGER_RADIX + model->previous_wager;
            for (int player = 0; player < MUS_GAME_PLAYER_COUNT;
                 player += 1) {
                key = key * MUS_ORDAGO_WAGER_ACTION_MASK_RADIX +
                      model->used_wager_action_masks[player];
            }
            key = key * 2 + model->current_wager_is_ordago;
        }
    }
    return key * MUS_ORDAGO_SITUATION_COUNT + situation;
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
    if (format->ordago_with_mus) {
        *result = ordago_information_set_key(
            model, mus->hand_summaries, mus->ordago_hand_codes);
        return CFR_STATUS_SUCCESS;
    }
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
    if (mus == NULL || result == NULL ||
        (!phase_is_betting(mus->model.phase) &&
         !phase_is_mus_decision(mus->model.phase) &&
         !phase_is_discard(mus->model.phase)))
        return CFR_STATUS_INVALID_ARGUMENT;
    *result = ordago_information_set_key(
        &mus->model, mus->hand_summaries, mus->ordago_hand_codes);
    return CFR_STATUS_SUCCESS;
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

static bool decode_ordago_discard_history(
    int code, int completed_rounds,
    MusOrdagoInformationSet *information) {
    for (int round = completed_rounds - 1; round >= 0; round -= 1) {
        for (int player = MUS_GAME_PLAYER_COUNT - 1; player >= 0;
             player -= 1) {
            information->discard_counts[round][player] =
                (uint8_t)(code % MUS_ORDAGO_DISCARD_COUNT_RADIX + 1);
            code /= MUS_ORDAGO_DISCARD_COUNT_RADIX;
        }
    }
    return code == 0;
}

static bool decode_ordago_public_mus(
    int code, MusOrdagoInformationSet *information) {
    int history_radix = 1;

    if (code < 0 || code >= ordago_public_mus_radix())
        return false;
    for (int completed_rounds = 0;
         completed_rounds < MUS_GAME_MAX_MUSES;
         completed_rounds += 1) {
        const int stopped_block =
            MUS_GAME_PLAYER_COUNT * history_radix;

        if (code < stopped_block) {
            information->mus_count = (uint8_t)completed_rounds;
            information->mus_stopped = true;
            information->mus_stop_player =
                (Player)(code / history_radix);
            return decode_ordago_discard_history(
                code % history_radix, completed_rounds, information);
        }
        code -= stopped_block;
        history_radix *= MUS_ORDAGO_DISCARD_ROUND_RADIX;
    }
    information->mus_count = MUS_GAME_MAX_MUSES;
    information->mus_stopped = false;
    information->mus_stop_player = CFR_PLAYER_0;
    return decode_ordago_discard_history(
        code, MUS_GAME_MAX_MUSES, information);
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

static bool decode_score_betting_payload(InfoSetKey *payload, unsigned int target,
                                         MusOrdagoInformationSet *information) {
    InfoSetKey key = *payload;
    if (information->bet_stage == MUS_BET_RESPONSE) {
        unsigned int response = (unsigned int)(key % MUS_SCORE_RESPONSE_RADIX);
        key /= MUS_SCORE_RESPONSE_RADIX;
        information->current_wager_is_ordago = (bool)(response % 2);
        response /= 2;
        information->used_wager_action_masks[1] = (uint8_t)(response % MUS_ORDAGO_WAGER_ACTION_MASK_RADIX);
        information->used_wager_action_masks[0] = (uint8_t)(response / MUS_ORDAGO_WAGER_ACTION_MASK_RADIX);
        information->numeric_wager_count = ordago_numeric_wager_count(information->used_wager_action_masks);
        const int increments[MUS_ORDAGO_WAGER_ACTION_COUNT] = {2, 4, 7, 10};
        int total = 0, last = 0;
        for (int bit = 0; bit < MUS_ORDAGO_WAGER_ACTION_COUNT; bit += 1) {
            for (int player = 0; player < MUS_GAME_PLAYER_COUNT; player += 1) {
                if ((information->used_wager_action_masks[player] & (1U << bit)) != 0) {
                    total += increments[bit];
                    last = increments[bit];
                }
            }
        }
        information->current_wager = information->current_wager_is_ordago ? MUS_ORDAGO_WAGER : (uint8_t)total;
        information->previous_wager = information->current_wager_is_ordago
            ? (uint8_t)(total == 0 ? 1 : total)
            : (uint8_t)(information->numeric_wager_count == 1 ? 1 : total - last);
    }
    information->prior_wager_total = (uint8_t)(key % MUS_ORDAGO_TOTAL_WAGER_RADIX);
    key /= MUS_ORDAGO_TOTAL_WAGER_RADIX;
    for (int player = MUS_GAME_PLAYER_COUNT - 1; player >= 0; player -= 1) {
        information->scores[player] = (uint8_t)(key % MUS_ORDAGO_SCORE_RADIX);
        key /= MUS_ORDAGO_SCORE_RADIX;
        if (information->scores[player] >= target)
            return false;
    }
    information->max_settled_score = information->scores[0] > information->scores[1]
        ? information->scores[0] : information->scores[1];
    for (int lance = information->previous_strength_count - 1; lance >= 0; lance -= 1) {
        information->previous_strengths[lance] =
            (MusLanceStrength)(key % ordago_strength_radix(lance));
        key /= ordago_strength_radix(lance);
    }
    if (!decode_ordago_public_mus((int)(key % ordago_public_mus_radix()), information))
        return false;
    key /= ordago_public_mus_radix();
    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS];
    size_t count;
    if (mus_ordago_game_information_set_legal_actions(information, actions,
            MUS_GAME_MAX_LEGAL_ACTIONS, &count) != CFR_STATUS_SUCCESS)
        return false;
    *payload = key;
    return true;
}

static Status decode_ordago_information_set(
    InfoSetKey key, unsigned int target, bool public_encoding,
    MusOrdagoInformationSet *information_set) {
    int situation;
    int round = -1;
    int hand_code_value;

    if (information_set == NULL || key < 0)
        return CFR_STATUS_INVALID_ARGUMENT;
    *information_set = (MusOrdagoInformationSet){.score_enabled = target != 0};
    situation = (int)(key % MUS_ORDAGO_SITUATION_COUNT);
    key /= MUS_ORDAGO_SITUATION_COUNT;
    if (!decode_ordago_situation(situation, information_set, &round))
        return CFR_STATUS_INVALID_ARGUMENT;
    if (round >= 0) {
        if (information_set->phase == MUS_PHASE_DISCARD_PLAYER_1) {
            information_set->discard_counts[round][0] =
                (uint8_t)(key % MUS_ORDAGO_DISCARD_COUNT_RADIX + 1);
            key /= MUS_ORDAGO_DISCARD_COUNT_RADIX;
        }
        for (int completed_round = round - 1; completed_round >= 0;
             completed_round -= 1) {
            for (int player = MUS_GAME_PLAYER_COUNT - 1; player >= 0;
                 player -= 1) {
                information_set->discard_counts[completed_round][player] =
                    (uint8_t)(key % MUS_ORDAGO_DISCARD_COUNT_RADIX + 1);
                key /= MUS_ORDAGO_DISCARD_COUNT_RADIX;
            }
        }
    } else if (public_encoding) {
        if (!decode_score_betting_payload(&key, target == 0 ? MUS_ORDAGO_SCORE_RADIX : target, information_set))
            return CFR_STATUS_INVALID_ARGUMENT;
    } else {
        if (information_set->bet_stage == MUS_BET_RESPONSE) {
            information_set->current_wager_is_ordago = (bool)(key % 2);
            key /= 2;
            for (int player = MUS_GAME_PLAYER_COUNT - 1; player >= 0;
                 player -= 1) {
                information_set->used_wager_action_masks[player] =
                    (uint8_t)(key %
                              MUS_ORDAGO_WAGER_ACTION_MASK_RADIX);
                key /= MUS_ORDAGO_WAGER_ACTION_MASK_RADIX;
            }
            information_set->numeric_wager_count =
                ordago_numeric_wager_count(
                    information_set->used_wager_action_masks);
            information_set->previous_wager =
                (uint8_t)(key % MUS_ORDAGO_WAGER_RADIX);
            key /= MUS_ORDAGO_WAGER_RADIX;
            information_set->current_wager =
                (uint8_t)(key % MUS_ORDAGO_WAGER_RADIX);
            key /= MUS_ORDAGO_WAGER_RADIX;
        }
        information_set->prior_wager_total =
            (uint8_t)(key % MUS_ORDAGO_TOTAL_WAGER_RADIX);
        key /= MUS_ORDAGO_TOTAL_WAGER_RADIX;
        information_set->max_settled_score = (uint8_t)(key % MUS_ORDAGO_SCORE_RADIX);
        key /= MUS_ORDAGO_SCORE_RADIX;
        if ((information_set->previous_strength_count == 0 &&
             ((!information_set->score_enabled && information_set->max_settled_score != 0) ||
              information_set->prior_wager_total != 0)) ||
            (information_set->bet_stage == MUS_BET_RESPONSE &&
             !ordago_offer_is_valid(
                 information_set->current_wager,
                 information_set->previous_wager,
                 information_set->current_wager_is_ordago)) ||
            (information_set->bet_stage == MUS_BET_RESPONSE &&
             !ordago_numeric_wager_count_is_valid(
                 information_set->previous_wager,
                 information_set->current_wager_is_ordago,
                 information_set->numeric_wager_count)) ||
            (information_set->bet_stage == MUS_BET_RESPONSE &&
             !ordago_wager_action_history_is_valid(
                 information_set->current_wager,
                 information_set->previous_wager,
                 information_set->player,
                 information_set->bettor,
                 information_set->current_wager_is_ordago,
                 information_set->used_wager_action_masks))) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
        if (information_set->bet_stage == MUS_BET_RESPONSE) {
            const int active_wager = ordago_active_numeric_wager(
                information_set->current_wager,
                information_set->previous_wager,
                information_set->current_wager_is_ordago);

            if (active_wager > 0 &&
                !ordago_numeric_budget_allows(
                    information_set->max_settled_score,
                    information_set->prior_wager_total,
                    active_wager, 0)) {
                return CFR_STATUS_INVALID_ARGUMENT;
            }
        }
        for (int lance = information_set->previous_strength_count - 1;
             lance >= 0; lance -= 1) {
            const int strength_radix = ordago_strength_radix(lance);

            information_set->previous_strengths[lance] =
                (MusLanceStrength)(key % strength_radix);
            key /= strength_radix;
        }
        const int mus_radix = ordago_public_mus_radix();
        const int mus_code = (int)(key % mus_radix);

        key /= mus_radix;
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

Status mus_ordago_game_information_set_decode(
    InfoSetKey key, MusOrdagoInformationSet *information_set) {
    return decode_ordago_information_set(key, 0, false, information_set);
}

Status mus_ordago_public_game_information_set_decode(
    InfoSetKey key, MusOrdagoInformationSet *information_set) {
    if (information_set == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    MusOrdagoInformationSet decoded;
    const Status status = decode_ordago_information_set(key, 0, true, &decoded);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    *information_set = decoded;
    return CFR_STATUS_SUCCESS;
}

Status mus_score_game_information_set_decode(
    InfoSetKey key, unsigned int target, MusOrdagoInformationSet *information_set) {
    if (target == 0 || target > MUS_ORDAGO_WAGER)
        return CFR_STATUS_INVALID_ARGUMENT;
    return decode_ordago_information_set(key, target, true, information_set);
}

Status mus_score_information_set_from_ordago(
    const MusScoreGame *game, InfoSetKey source_key,
    unsigned int earned_a, unsigned int earned_b, InfoSetKey *result) {
    if (game == NULL || !mus_score_game_matches(&game->game) ||
        game->continuation == NULL || result == NULL ||
        game->continuation->target == 0 || game->continuation->target > MUS_ORDAGO_WAGER ||
        game->initial_scores[0] >= game->continuation->target ||
        game->initial_scores[1] >= game->continuation->target ||
        earned_a >= MUS_ORDAGO_WAGER || earned_b >= MUS_ORDAGO_WAGER)
        return CFR_STATUS_INVALID_ARGUMENT;
    MusOrdagoInformationSet information;
    Status status = mus_ordago_game_information_set_decode(source_key, &information);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (information.hand_kind != MUS_ORDAGO_HAND_EXACT && information.phase != MUS_PHASE_GRANDE)
        return CFR_STATUS_NOT_FOUND;
    if (earned_a != 0 || earned_b != 0)
        return CFR_STATUS_INVALID_ARGUMENT;
    InfoSetKey key = source_key;
    if (information.phase == MUS_PHASE_GRANDE) {
        MusGameModel model = {
            .phase = MUS_PHASE_GRANDE, .score_target = game->continuation->target,
            .accounting_mode = MUS_ACCOUNTING_PUBLIC_DEFERRED,
            .initial_scores = {game->initial_scores[0], game->initial_scores[1]},
            .mus_count = information.mus_count, .mus_stopped = information.mus_stopped,
            .mus_stop_player = (uint8_t)information.mus_stop_player,
            .bet_stage = (uint8_t)information.bet_stage,
            .current_player = (uint8_t)information.player,
            .current_wager_is_ordago = information.current_wager_is_ordago,
        };
        memcpy(model.discard_counts, information.discard_counts, sizeof(model.discard_counts));
        memcpy(model.used_wager_action_masks, information.used_wager_action_masks,
               sizeof(model.used_wager_action_masks));
        const int hand = information.king_count == MUS_GAME_HAND_SIZE
            ? MUS_ORDAGO_GRANDE_HAND_RADIX - 1
            : information.king_count * 7 + (int)information.highest_non_king;
        key = score_betting_information_set_key(&model, hand);
    }
    status = mus_score_game_information_set_decode(key, game->continuation->target,
                                                   &information);
    if (status == CFR_STATUS_SUCCESS)
        *result = key;
    return status == CFR_STATUS_INVALID_ARGUMENT ? CFR_STATUS_NOT_FOUND : status;
}

Status mus_score_information_set_ordago_candidates(
    const MusScoreGame *game, InfoSetKey key, InfoSetKey *keys,
    size_t capacity, size_t *count) {
    if (game == NULL || !mus_score_game_matches(&game->game) ||
        game->continuation == NULL || keys == NULL || count == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    MusOrdagoInformationSet information;
    Status status = mus_score_game_information_set_decode(
        key, game->continuation->target, &information);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (information.hand_kind == MUS_ORDAGO_HAND_EXACT) {
        if (capacity == 0)
            return CFR_STATUS_BUFFER_TOO_SMALL;
        keys[0] = key;
        *count = 1;
        return CFR_STATUS_SUCCESS;
    }
    int paid[2];
    for (int player = 0; player < 2; ++player) {
        paid[player] = information.scores[player] - game->initial_scores[player];
        if (game->initial_scores[player] >= game->continuation->target || paid[player] < 0)
            return CFR_STATUS_INVALID_ARGUMENT;
    }
    /* In reachable legacy histories, a rejected opening has previous_wager=1.
     * Thus each payment equals its contribution to the legacy wager total. */
    const int wagers = information.prior_wager_total + paid[0] + paid[1];
    if (wagers > MUS_ORDAGO_WAGER ||
        (information.phase == MUS_PHASE_GRANDE && wagers != 0))
        return CFR_STATUS_INVALID_ARGUMENT;
    int minimum_total = wagers, maximum_total = wagers;
    for (int lance = 0; lance < information.previous_strength_count; ++lance) {
        const MusLanceStrength strength = information.previous_strengths[lance];
        if (lance < 2 && strength == MUS_LANCE_STRENGTH_NONE) {
            minimum_total += 1;
            maximum_total += 1;
        } else if (lance == 2 && strength != MUS_LANCE_STRENGTH_PARES_SKIPPED_NONE) {
            /* The old model also settled the winner's intrinsic pair points. */
            minimum_total += 1;
            maximum_total += 3;
        }
    }
    int minimum = (minimum_total + 1) / 2;
    if (minimum < paid[0]) minimum = paid[0];
    if (minimum < paid[1]) minimum = paid[1];
    /* Each player already owns its public payments. Even if every pending
     * point goes to one player, the other's payments cannot move with it. */
    const int minimum_paid = paid[0] < paid[1] ? paid[0] : paid[1];
    const int maximum_score = maximum_total - minimum_paid;
    int maximum = maximum_score < MUS_ORDAGO_WAGER ? maximum_score : MUS_ORDAGO_WAGER;
    if (information.bet_stage == MUS_BET_RESPONSE) {
        const int active = ordago_active_numeric_wager(information.current_wager,
            information.previous_wager, information.current_wager_is_ordago);
        if (active > 0 && maximum >= MUS_ORDAGO_WAGER - wagers - active)
            maximum = MUS_ORDAGO_WAGER - wagers - active - 1;
    }
    if (minimum > maximum) {
        *count = 0;
        return CFR_STATUS_SUCCESS;
    }
    const size_t required = (size_t)(maximum - minimum + 1);
    if (capacity < required)
        return CFR_STATUS_BUFFER_TOO_SMALL;
    information.score_enabled = false;
    information.scores[0] = information.scores[1] = 0;
    information.prior_wager_total = (uint8_t)wagers;
    information.max_settled_score = (uint8_t)minimum;
    InfoSetKey first;
    status = mus_ordago_game_information_set_encode(&information, &first);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    InfoSetKey stride = (InfoSetKey)MUS_ORDAGO_TOTAL_WAGER_RADIX * MUS_ORDAGO_SITUATION_COUNT;
    if (information.bet_stage == MUS_BET_RESPONSE)
        stride *= (InfoSetKey)MUS_ORDAGO_WAGER_RADIX * MUS_ORDAGO_WAGER_RADIX *
                  MUS_ORDAGO_WAGER_ACTION_MASK_RADIX * MUS_ORDAGO_WAGER_ACTION_MASK_RADIX * 2;
    for (size_t i = 0; i < required; ++i)
        keys[i] = first + (InfoSetKey)i * stride;
    *count = required;
    return CFR_STATUS_SUCCESS;
}

/* Each score cell uses a separate store and a distinct checkpoint schema. */
bool mus_score_game_matches(const Game *game) {
    return game != NULL && game->context == &MUS_SCORE_FORMAT &&
           game->operations == &MUS_OPERATIONS &&
           game->trusted_operations == &MUS_ORDAGO_TRUSTED_OPERATIONS &&
           game->strategic_player_count == MUS_GAME_PLAYER_COUNT &&
           game->max_legal_actions == MUS_GAME_MAX_LEGAL_ACTIONS;
}

Status mus_score_game_init(MusScoreGame *score_game, const MusScoreTable *table,
                           unsigned int a, unsigned int b) {
    if (score_game == NULL || table == NULL || table->target == 0 ||
        table->target > MUS_ORDAGO_WAGER || a >= table->target || b >= table->target)
        return CFR_STATUS_INVALID_ARGUMENT;
    score_game->continuation = table;
    score_game->initial_scores[0] = (uint8_t)a;
    score_game->initial_scores[1] = (uint8_t)b;
    score_game->game = MUS_ORDAGO_GAME;
    score_game->game.context = &MUS_SCORE_FORMAT;
    score_game->game.strategy_schema_id = score_game->schema;
    return mus_score_game_set_schema(score_game);
}

Status mus_score_game_state_init(MusGameState *state, const MusScoreGame *game) {
    if (state == NULL || game == NULL || game->continuation == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    Status status = mus_ordago_game_state_init(state);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    state->continuation = game->continuation;
    state->model.accounting_mode = MUS_ACCOUNTING_PUBLIC_DEFERRED;
    state->model.score_target = game->continuation->target;
    memcpy(state->model.initial_scores, game->initial_scores,
           sizeof(state->model.initial_scores));
    return validate_state_data(state, &MUS_SCORE_FORMAT);
}

static bool ordago_observed_hand_equal(const MusOrdagoInformationSet *a,
                                        const MusOrdagoInformationSet *b) {
    if (a->hand_kind != b->hand_kind)
        return false;
    switch (a->hand_kind) {
    case MUS_ORDAGO_HAND_EXACT:
        return memcmp(a->hand, b->hand, sizeof(a->hand)) == 0;
    case MUS_ORDAGO_HAND_GRANDE:
        return a->king_count == b->king_count &&
               a->has_highest_non_king == b->has_highest_non_king &&
               (!a->has_highest_non_king ||
                a->highest_non_king == b->highest_non_king);
    case MUS_ORDAGO_HAND_CHICA:
        return a->pito_count == b->pito_count &&
               a->has_lowest_non_pito == b->has_lowest_non_pito &&
               (!a->has_lowest_non_pito ||
                a->lowest_non_pito == b->lowest_non_pito);
    case MUS_ORDAGO_HAND_PARES:
        return a->pairs_type == b->pairs_type &&
               a->pairs_value == b->pairs_value &&
               a->has_second_pairs_value == b->has_second_pairs_value &&
               (!a->has_second_pairs_value ||
                a->second_pairs_value == b->second_pairs_value);
    case MUS_ORDAGO_HAND_JUEGO:
        return a->game_value == b->game_value;
    case MUS_ORDAGO_HAND_PUNTO:
        return a->point_value == b->point_value;
    }
    return false;
}

Status mus_ordago_game_information_set_set_hand(
    MusOrdagoInformationSet *information,
    const uint8_t hand[MUS_GAME_CLASS_COUNT]) {
    MusHandSummary summary;
    MusOrdagoInformationSet temporary;
    int code = -1;
    if (information == NULL || hand == NULL ||
        summarize_hand(hand, &summary) != CFR_STATUS_SUCCESS)
        return CFR_STATUS_INVALID_ARGUMENT;
    temporary = *information;
    temporary.has_second_pairs_value = false;
    if (phase_is_mus_decision(temporary.phase) || phase_is_discard(temporary.phase)) {
        temporary.hand_kind = MUS_ORDAGO_HAND_EXACT;
        code = compact_hand_code(hand);
    } else {
        const int phase_index = ordago_betting_phase_index(temporary.phase);
        if (phase_index < 0)
            return CFR_STATUS_INVALID_ARGUMENT;
        temporary.hand_kind = (MusOrdagoHandKind)(MUS_ORDAGO_HAND_GRANDE + phase_index);
        switch (temporary.phase) {
        case MUS_PHASE_GRANDE: code = grande_hand_code(hand); break;
        case MUS_PHASE_CHICA: code = chica_hand_code(hand); break;
        case MUS_PHASE_PARES: code = pairs_hand_code(hand); break;
        case MUS_PHASE_JUEGO: code = summary.game_key; break;
        case MUS_PHASE_PUNTO:
            code = summary.point_value <= 30 ? summary.point_value - 4 : -1;
            break;
        default: break;
        }
    }
    if (!decode_ordago_hand(code, &temporary))
        return CFR_STATUS_INVALID_ARGUMENT;
    *information = temporary;
    return CFR_STATUS_SUCCESS;
}

static Status encode_ordago_information_set(
    const MusOrdagoInformationSet *information, unsigned int target, bool public_encoding,
    InfoSetKey *result) {
    MusGameModel model = {0};
    MusOrdagoInformationSet decoded = {0};
    int hand_code = -1;
    int situation;
    InfoSetKey key;
    if (information == NULL || result == NULL || target > MUS_ORDAGO_WAGER ||
        information->score_enabled != (target != 0) ||
        !player_is_valid(information->player) ||
        (information->mus_stopped && !player_is_valid(information->mus_stop_player)) ||
        information->mus_count > MUS_GAME_MAX_MUSES)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (public_encoding &&
        (information->scores[0] >= (target == 0 ? MUS_ORDAGO_SCORE_RADIX : target) ||
         information->scores[1] >= (target == 0 ? MUS_ORDAGO_SCORE_RADIX : target) ||
         information->max_settled_score !=
             (information->scores[0] > information->scores[1]
                  ? information->scores[0] : information->scores[1])))
        return CFR_STATUS_INVALID_ARGUMENT;
    model.phase = (uint8_t)information->phase;
    model.current_player = (uint8_t)information->player;
    model.bet_stage = (uint8_t)information->bet_stage;
    model.mus_count = information->mus_count;
    model.mus_stopped = information->mus_stopped;
    model.mus_stop_player = (uint8_t)information->mus_stop_player;
    memcpy(model.discard_counts, information->discard_counts, sizeof(model.discard_counts));
    situation = ordago_situation_code(&model);
    if (situation < 0 ||
        !decode_ordago_situation(situation, &decoded, &(int){0}) ||
        decoded.phase != information->phase || decoded.player != information->player ||
        decoded.hand_kind != information->hand_kind)
        return CFR_STATUS_INVALID_ARGUMENT;
    for (int code = 0; code < ordago_hand_radix(information->phase); ++code) {
        MusOrdagoInformationSet candidate = decoded;
        if (decode_ordago_hand(code, &candidate) &&
            ordago_observed_hand_equal(information, &candidate)) {
            hand_code = code;
            break;
        }
    }
    if (hand_code < 0)
        return CFR_STATUS_INVALID_ARGUMENT;
    key = hand_code;
    if (phase_is_mus_decision(information->phase) || phase_is_discard(information->phase)) {
        if (ordago_discard_history_code(&model, model.mus_count) < 0)
            return CFR_STATUS_INVALID_ARGUMENT;
        for (int round = 0; round < model.mus_count; ++round)
            for (int player = 0; player < 2; ++player)
                key = key * 4 + model.discard_counts[round][player] - 1;
        if (information->phase == MUS_PHASE_DISCARD_PLAYER_1) {
            int count = model.discard_counts[model.mus_count][0];
            if (count < 1 || count > 4)
                return CFR_STATUS_INVALID_ARGUMENT;
            key = key * 4 + count - 1;
        }
    } else {
        int mus = ordago_public_mus_code(&model);
        int strength_count = ordago_previous_strength_count(information->phase);
        Action actions[MUS_GAME_MAX_LEGAL_ACTIONS];
        size_t count;
        if (mus < 0 || information->previous_strength_count != strength_count ||
            information->numeric_wager_count !=
                ordago_numeric_wager_count(information->used_wager_action_masks) ||
            mus_ordago_game_information_set_legal_actions(information, actions,
                MUS_GAME_MAX_LEGAL_ACTIONS, &count) != CFR_STATUS_SUCCESS)
            return CFR_STATUS_INVALID_ARGUMENT;
        key = key * ordago_public_mus_radix() + mus;
        for (int lance = 0; lance < strength_count; ++lance) {
            int strength = information->previous_strengths[lance];
            if (strength < 0 || strength >= ordago_strength_radix(lance))
                return CFR_STATUS_INVALID_ARGUMENT;
            key = key * ordago_strength_radix(lance) + strength;
        }
        if (public_encoding) {
            for (int player = 0; player < MUS_GAME_PLAYER_COUNT; ++player)
                key = key * MUS_ORDAGO_SCORE_RADIX + information->scores[player];
        } else {
            key = key * MUS_ORDAGO_SCORE_RADIX + information->max_settled_score;
        }
        key = key * MUS_ORDAGO_TOTAL_WAGER_RADIX + information->prior_wager_total;
        if (information->bet_stage == MUS_BET_RESPONSE) {
            if (!public_encoding) {
                key = key * MUS_ORDAGO_WAGER_RADIX + information->current_wager;
                key = key * MUS_ORDAGO_WAGER_RADIX + information->previous_wager;
            }
            for (int player = 0; player < 2; ++player)
                key = key * MUS_ORDAGO_WAGER_ACTION_MASK_RADIX +
                      information->used_wager_action_masks[player];
            key = key * 2 + information->current_wager_is_ordago;
        }
    }
    key = key * MUS_ORDAGO_SITUATION_COUNT + situation;
    if (decode_ordago_information_set(key, target, public_encoding, &decoded) != CFR_STATUS_SUCCESS)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (public_encoding && phase_is_betting(information->phase) &&
        (decoded.current_wager != information->current_wager ||
         decoded.previous_wager != information->previous_wager ||
         decoded.numeric_wager_count != information->numeric_wager_count ||
         decoded.current_wager_is_ordago != information->current_wager_is_ordago ||
         memcmp(decoded.used_wager_action_masks, information->used_wager_action_masks,
                sizeof(decoded.used_wager_action_masks)) != 0))
        return CFR_STATUS_INVALID_ARGUMENT;
    *result = key;
    return CFR_STATUS_SUCCESS;
}

Status mus_ordago_game_information_set_encode(
    const MusOrdagoInformationSet *information, InfoSetKey *result) {
    return encode_ordago_information_set(information, 0, false, result);
}

Status mus_ordago_public_game_information_set_encode(
    const MusOrdagoInformationSet *information, InfoSetKey *result) {
    return encode_ordago_information_set(information, 0, true, result);
}

Status mus_score_game_information_set_encode(
    const MusOrdagoInformationSet *information, unsigned int target, InfoSetKey *result) {
    if (target == 0)
        return CFR_STATUS_INVALID_ARGUMENT;
    return encode_ordago_information_set(information, target, true, result);
}

Status mus_ordago_game_information_set_legal_actions(
    const MusOrdagoInformationSet *information_set, Action *actions,
    size_t capacity, size_t *required_count) {
    if (information_set == NULL || actions == NULL ||
        required_count == NULL) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    if (information_set->phase == MUS_PHASE_MUS_PLAYER_0 ||
        information_set->phase == MUS_PHASE_MUS_PLAYER_1) {
        *required_count = 2;
        if (capacity < 2)
            return CFR_STATUS_BUFFER_TOO_SMALL;
        actions[0] = MUS_ACTION_MUS;
        actions[1] = MUS_ACTION_NO_MUS;
        return CFR_STATUS_SUCCESS;
    }
    if (information_set->phase == MUS_PHASE_DISCARD_PLAYER_0 ||
        information_set->phase == MUS_PHASE_DISCARD_PLAYER_1) {
        Action temporary[MUS_GAME_DISCARD_MASK_COUNT - 1];
        size_t count = 0;

        if (hand_total(information_set->hand) != MUS_GAME_HAND_SIZE)
            return CFR_STATUS_INVALID_ARGUMENT;
        for (unsigned int mask = 1; mask < MUS_GAME_DISCARD_MASK_COUNT;
             mask += 1) {
            if (mus_ordago_game_discard_mask_is_canonical(
                    information_set->hand, mask)) {
                temporary[count++] = MUS_ACTION_DISCARD_MASK(mask);
            }
        }
        *required_count = count;
        if (capacity < count)
            return CFR_STATUS_BUFFER_TOO_SMALL;
        memcpy(actions, temporary, count * sizeof(*actions));
        return CFR_STATUS_SUCCESS;
    }
    if (!phase_is_betting(information_set->phase) ||
        (information_set->previous_strength_count == 0 &&
         ((!information_set->score_enabled && information_set->max_settled_score != 0) ||
          information_set->prior_wager_total != 0))) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    return collect_ordago_betting_actions(
        information_set->bet_stage,
        information_set->max_settled_score,
        information_set->prior_wager_total,
        information_set->current_wager,
        information_set->previous_wager, information_set->player,
        information_set->bettor,
        information_set->used_wager_action_masks,
        information_set->current_wager_is_ordago, actions, capacity,
        required_count);
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
