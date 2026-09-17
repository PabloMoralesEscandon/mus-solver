#include <pthread.h>
#include <string.h>

#include "mus_simplified_internal.h"

bool mus_simplified_wager_masks_valid(const uint8_t *masks, size_t players) {
    /* Bits 0, 1 and 3 represent 2, 4 and 10. Each increment can occur once
     * across all players because the base rules already require monotonic raises. */
    unsigned int used = 0;
    for (size_t p = 0; p < players; ++p) {
        if ((masks[p] & ~11U) != 0 || (used & masks[p]) != 0)
            return false;
        used |= masks[p];
    }
    return true;
}

static unsigned int last_increment(unsigned int current, unsigned int previous) {
    return current == 0 ? 0 : previous == 1 ? current : current - previous;
}

static bool action_allowed(Action action, unsigned int minimum) {
    const unsigned int increment = action == MUS_ACTION_BET_TWO ? 2U
        : action == MUS_ACTION_BET_FOUR ? 4U : action == MUS_ACTION_BET_TEN ? 10U : 0;
    return action != MUS_ACTION_BET_SEVEN && (increment == 0 || increment > minimum);
}

static size_t filter_actions(Action *actions, size_t count, unsigned int minimum) {
    size_t retained = 0;
    for (size_t i = 0; i < count; ++i) {
        const Action action = actions[i];
        if (!action_allowed(action, minimum))
            continue;
        actions[retained++] = action;
    }
    return retained;
}

static Status copy_actions(Action *actions, size_t capacity, size_t *required,
                           const Action *menu, size_t count) {
    *required = count;
    if (capacity < count)
        return CFR_STATUS_BUFFER_TOO_SMALL;
    memcpy(actions, menu, count * sizeof(*actions));
    return CFR_STATUS_SUCCESS;
}

Status mus_simplified_ordago_information_set_legal_actions(
    const MusOrdagoInformationSet *information, Action *actions,
    size_t capacity, size_t *required_count) {
    if (information == NULL || actions == NULL || required_count == NULL ||
        !mus_simplified_wager_masks_valid(information->used_wager_action_masks, 2))
        return CFR_STATUS_INVALID_ARGUMENT;
    Action menu[MUS_GAME_MAX_LEGAL_ACTIONS]; size_t count;
    Status status = mus_ordago_game_information_set_legal_actions(
        information, menu, MUS_GAME_MAX_LEGAL_ACTIONS, &count);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (information->phase >= MUS_PHASE_GRANDE && information->phase <= MUS_PHASE_PUNTO)
        count = filter_actions(menu, count, last_increment(information->current_wager,
                                                          information->previous_wager));
    return copy_actions(actions, capacity, required_count, menu, count);
}

static bool proposal_valid(unsigned int proposal, unsigned int current, unsigned int previous) {
    const unsigned int increment = proposal == MUS_FOUR_RESPONSE_BET_TWO ? 2U
        : proposal == MUS_FOUR_RESPONSE_BET_FOUR ? 4U
        : proposal == MUS_FOUR_RESPONSE_BET_TEN ? 10U : 0;
    return increment == 0 || increment > last_increment(current, previous);
}

static bool four_information_valid(const MusFourInformationSet *information) {
    return mus_simplified_wager_masks_valid(information->used_wager_action_masks, 4) &&
        proposal_valid(information->pending_response, information->current_wager,
                       information->previous_wager);
}

Status mus_simplified_four_information_set_legal_actions(
    const MusFourInformationSet *information, bool score_enabled,
    Action *actions, size_t capacity, size_t *required_count) {
    if (information == NULL || actions == NULL || required_count == NULL ||
        !four_information_valid(information))
        return CFR_STATUS_INVALID_ARGUMENT;
    Action menu[MUS_GAME_MAX_LEGAL_ACTIONS]; size_t count;
    Status status = score_enabled
        ? mus_four_score_information_set_legal_actions(information, menu, MUS_GAME_MAX_LEGAL_ACTIONS, &count)
        : mus_four_information_set_legal_actions(information, menu, MUS_GAME_MAX_LEGAL_ACTIONS, &count);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (information->phase >= MUS_FOUR_PHASE_GRANDE && information->phase <= MUS_FOUR_PHASE_PUNTO)
        count = filter_actions(menu, count, last_increment(information->current_wager,
                                                          information->previous_wager));
    return copy_actions(actions, capacity, required_count, menu, count);
}

static const GameOperations *base_ops(const Game *base) {
    return base->trusted_operations == NULL ? base->operations : base->trusted_operations;
}

static Status validate(const void *context, const GameState *state) {
    const Game *base = context;
    Status status = cfr_game_validate_state(base, state);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (base->strategic_player_count == 4) {
        const MusFourGameModel *model = &((const MusFourGameState *)state)->model;
        if (!mus_simplified_wager_masks_valid(model->used_wager_action_masks, 4) ||
            !proposal_valid(model->pending_response, model->current_wager, model->previous_wager))
            return CFR_STATUS_INVALID_ARGUMENT;
    } else if (!mus_simplified_wager_masks_valid(((const MusGameState *)state)->model.used_wager_action_masks, 2)) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    return CFR_STATUS_SUCCESS;
}

static Status is_terminal(const void *context, const GameState *state, bool *result) {
    const Game *base = context;
    return base_ops(base)->is_terminal(base->context, state, result);
}

static Status terminal_utility(const void *context, const GameState *state, Player player, Utility *result) {
    const Game *base = context;
    return base_ops(base)->terminal_utility(base->context, state, player, result);
}

static Status current_actor(const void *context, const GameState *state, Actor *result) {
    const Game *base = context;
    return base_ops(base)->current_actor(base->context, state, result);
}

static unsigned int minimum_increment(const Game *base, const GameState *state, bool *betting) {
    unsigned int current, previous;
    if (base->strategic_player_count == 4) {
        const MusFourGameModel *m = &((const MusFourGameState *)state)->model;
        *betting = m->phase >= MUS_FOUR_PHASE_GRANDE && m->phase <= MUS_FOUR_PHASE_PUNTO;
        current = m->current_wager; previous = m->previous_wager;
    } else {
        const MusGameModel *m = &((const MusGameState *)state)->model;
        *betting = m->phase >= MUS_PHASE_GRANDE && m->phase <= MUS_PHASE_PUNTO;
        current = m->current_wager; previous = m->previous_wager;
    }
    return *betting ? last_increment(current, previous) : 0;
}

static Status legal_actions(const void *context, const GameState *state,
    Action *actions, size_t capacity, size_t *required_count) {
    const Game *base = context;
    if (state == NULL || actions == NULL || required_count == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    Action menu[MUS_GAME_MAX_LEGAL_ACTIONS]; size_t count;
    Status status = base_ops(base)->legal_actions(base->context, state, menu,
                                                 MUS_GAME_MAX_LEGAL_ACTIONS, &count);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    bool betting;
    const unsigned int minimum = minimum_increment(base, state, &betting);
    if (betting)
        count = filter_actions(menu, count, minimum);
    return copy_actions(actions, capacity, required_count, menu, count);
}

static Status apply_action(const void *context, GameState *state, Action action) {
    const Game *base = context;
    if (state == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    bool betting;
    const unsigned int minimum = minimum_increment(base, state, &betting);
    if (betting && !action_allowed(action, minimum))
        return CFR_STATUS_ILLEGAL_ACTION;
    return base_ops(base)->apply_action(base->context, state, action);
}

static Status undo_action(const void *context, GameState *state) {
    const Game *base = context;
    return base_ops(base)->undo_action(base->context, state);
}

static Status chance_probability(const void *context, const GameState *state,
                                 Action action, Probability *result) {
    const Game *base = context;
    return base_ops(base)->chance_probability(base->context, state, action, result);
}

static Status information_set_key(const void *context, const GameState *state, InfoSetKey *result) {
    const Game *base = context;
    return base_ops(base)->information_set_key(base->context, state, result);
}

static Status chance_outcomes(const void *context, const GameState *state,
    Action *actions, Probability *probabilities, size_t capacity, size_t *required_count) {
    const Game *base = context;
    return base_ops(base)->chance_outcomes(base->context, state, actions, probabilities, capacity, required_count);
}

#define CHECKED(name, parameters, arguments) \
    static Status checked_##name parameters { \
        Status status = validate(context, state); \
        return status == CFR_STATUS_SUCCESS ? name arguments : status; \
    }
CHECKED(is_terminal, (const void *context, const GameState *state, bool *result), (context, state, result))
CHECKED(terminal_utility, (const void *context, const GameState *state, Player player, Utility *result),
        (context, state, player, result))
CHECKED(current_actor, (const void *context, const GameState *state, Actor *result), (context, state, result))
CHECKED(legal_actions, (const void *context, const GameState *state, Action *actions, size_t capacity, size_t *required_count),
        (context, state, actions, capacity, required_count))
CHECKED(apply_action, (const void *context, GameState *state, Action action), (context, state, action))
CHECKED(undo_action, (const void *context, GameState *state), (context, state))
CHECKED(chance_probability, (const void *context, const GameState *state, Action action, Probability *result),
        (context, state, action, result))
CHECKED(information_set_key, (const void *context, const GameState *state, InfoSetKey *result), (context, state, result))
CHECKED(chance_outcomes, (const void *context, const GameState *state, Action *actions, Probability *probabilities,
        size_t capacity, size_t *required_count), (context, state, actions, probabilities, capacity, required_count))
#undef CHECKED

#define OPERATIONS(prefix) { \
    .is_terminal = prefix##is_terminal, .terminal_utility = prefix##terminal_utility, \
    .current_actor = prefix##current_actor, .legal_actions = prefix##legal_actions, \
    .apply_action = prefix##apply_action, .undo_action = prefix##undo_action, \
    .chance_probability = prefix##chance_probability, .information_set_key = prefix##information_set_key, \
    .chance_outcomes = prefix##chance_outcomes, .validate_state = validate \
}
static const GameOperations operations = OPERATIONS(checked_);
static const GameOperations trusted_operations = OPERATIONS();
#undef OPERATIONS

bool mus_simplified_game_matches(const Game *game) {
    return game != NULL && game->operations == &operations &&
           game->trusted_operations == &trusted_operations && game->context != NULL;
}

const Game *mus_simplified_game_base(const Game *game) {
    return mus_simplified_game_matches(game) ? game->context : game;
}

void mus_simplified_game_wrap(Game *game, const Game *base, const char *schema) {
    *game = *base;
    game->operations = &operations;
    game->trusted_operations = &trusted_operations;
    game->context = base;
    game->strategy_schema_id = schema;
}

static Game single_hand_games[2];
static pthread_once_t once = PTHREAD_ONCE_INIT;

static void initialize(void) {
    mus_simplified_game_wrap(&single_hand_games[0], mus_ordago_public_game_descriptor(),
                             MUS_SIMPLIFIED_STRATEGY_SCHEMA_ID);
    mus_simplified_game_wrap(&single_hand_games[1], mus_four_game_descriptor(),
                             MUS_FOUR_SIMPLIFIED_STRATEGY_SCHEMA_ID);
}

const Game *mus_simplified_game_descriptor(bool four_player) {
    pthread_once(&once, initialize);
    return &single_hand_games[four_player ? 1 : 0];
}
