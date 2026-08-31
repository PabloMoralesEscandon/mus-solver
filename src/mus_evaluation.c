#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "cfr/evaluation.h"
#include "cfr/info_node.h"
#include "mus_solver/mus_evaluation.h"
#include "mus_solver/mus_game.h"

#define BR_INITIAL_CAPACITY 1024
#define BR_ACTION_CAPACITY 2
#define BR_MAX_DECISION_DEPTH (MUS_GAME_LANCE_COUNT * 2)
#define EVALUATION_EPSILON 1e-9

typedef struct {
    bool occupied;
    bool best_ready;
    InfoSetKey key;
    size_t decision_depth;
    size_t action_count;
    size_t best_action;
    Utility action_values[BR_ACTION_CAPACITY];
} BestResponseEntry;

typedef struct {
    BestResponseEntry *entries;
    size_t capacity;
    size_t size;
} BestResponseStore;

static uint64_t hash_key(InfoSetKey key) {
    return (uint64_t)key * UINT64_C(11400714819323198485);
}

static Status best_store_init(BestResponseStore *store) {
    if (store == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    store->entries = calloc(BR_INITIAL_CAPACITY, sizeof(*store->entries));
    if (store->entries == NULL)
        return CFR_STATUS_OUT_OF_MEMORY;
    store->capacity = BR_INITIAL_CAPACITY;
    store->size = 0;
    return CFR_STATUS_SUCCESS;
}

static void best_store_destroy(BestResponseStore *store) {
    if (store == NULL)
        return;
    free(store->entries);
    *store = (BestResponseStore){0};
}

static size_t best_store_slot(const BestResponseStore *store, InfoSetKey key,
                              bool *found) {
    const size_t mask = store->capacity - 1;
    size_t slot = (size_t)hash_key(key) & mask;

    for (;;) {
        if (!store->entries[slot].occupied) {
            *found = false;
            return slot;
        }
        if (store->entries[slot].key == key) {
            *found = true;
            return slot;
        }
        slot = (slot + 1) & mask;
    }
}

static Status best_store_grow(BestResponseStore *store) {
    BestResponseEntry *grown;
    size_t new_capacity;

    if (store->capacity > SIZE_MAX / 2)
        return CFR_STATUS_OUT_OF_MEMORY;
    new_capacity = store->capacity * 2;
    if (new_capacity > SIZE_MAX / sizeof(*grown))
        return CFR_STATUS_OUT_OF_MEMORY;
    grown = calloc(new_capacity, sizeof(*grown));
    if (grown == NULL)
        return CFR_STATUS_OUT_OF_MEMORY;

    for (size_t index = 0; index < store->capacity; index += 1) {
        BestResponseEntry entry = store->entries[index];
        size_t slot;
        size_t mask;

        if (!entry.occupied)
            continue;
        mask = new_capacity - 1;
        slot = (size_t)hash_key(entry.key) & mask;
        while (grown[slot].occupied)
            slot = (slot + 1) & mask;
        grown[slot] = entry;
    }
    free(store->entries);
    store->entries = grown;
    store->capacity = new_capacity;
    return CFR_STATUS_SUCCESS;
}

static Status best_store_get_or_create(BestResponseStore *store,
                                       InfoSetKey key, size_t decision_depth,
                                       size_t action_count,
                                       BestResponseEntry **entry_out) {
    bool found;
    size_t slot;

    if (store == NULL || entry_out == NULL || action_count == 0 ||
        action_count > BR_ACTION_CAPACITY) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    if (store->size + 1 > store->capacity - store->capacity / 4) {
        Status status = best_store_grow(store);
        if (status != CFR_STATUS_SUCCESS)
            return status;
    }
    slot = best_store_slot(store, key, &found);
    if (found) {
        BestResponseEntry *entry = &store->entries[slot];
        if (entry->decision_depth != decision_depth ||
            entry->action_count != action_count) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
        *entry_out = entry;
        return CFR_STATUS_SUCCESS;
    }
    store->entries[slot] =
        (BestResponseEntry){.occupied = true,
                            .key = key,
                            .decision_depth = decision_depth,
                            .action_count = action_count};
    store->size += 1;
    *entry_out = &store->entries[slot];
    return CFR_STATUS_SUCCESS;
}

static Status best_store_find_ready(const BestResponseStore *store,
                                    InfoSetKey key, size_t decision_depth,
                                    size_t action_count,
                                    const BestResponseEntry **entry_out) {
    bool found;
    size_t slot;

    if (store == NULL || entry_out == NULL || store->capacity == 0)
        return CFR_STATUS_INVALID_ARGUMENT;
    slot = best_store_slot(store, key, &found);
    if (!found)
        return CFR_STATUS_NOT_FOUND;
    if (!store->entries[slot].best_ready ||
        store->entries[slot].decision_depth != decision_depth ||
        store->entries[slot].action_count != action_count) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    *entry_out = &store->entries[slot];
    return CFR_STATUS_SUCCESS;
}

static Status best_store_finish_depth(BestResponseStore *store,
                                      size_t decision_depth) {
    if (store == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    for (size_t index = 0; index < store->capacity; index += 1) {
        BestResponseEntry *entry = &store->entries[index];

        if (!entry->occupied || entry->decision_depth != decision_depth)
            continue;
        if (entry->best_ready)
            return CFR_STATUS_INVALID_ARGUMENT;
        entry->best_action = 0;
        for (size_t action = 1; action < entry->action_count; action += 1) {
            if (entry->action_values[action] >
                entry->action_values[entry->best_action]) {
                entry->best_action = action;
            }
        }
        entry->best_ready = true;
    }
    return CFR_STATUS_SUCCESS;
}

static Status average_strategy(const InfoStore *store, InfoSetKey key,
                               size_t action_count,
                               Probability strategy[BR_ACTION_CAPACITY]) {
    const InfoNode *node = NULL;
    Status status;

    if (store == NULL || action_count == 0 ||
        action_count > BR_ACTION_CAPACITY) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    status = cfr_info_store_find_const(store, key, &node);
    if (status == CFR_STATUS_NOT_FOUND) {
        for (size_t action = 0; action < action_count; action += 1)
            strategy[action] = 1.0 / (Probability)action_count;
        return CFR_STATUS_SUCCESS;
    }
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (node->action_count != action_count)
        return CFR_STATUS_INVALID_ARGUMENT;
    return cfr_info_node_average_strategy(node, strategy,
                                          BR_ACTION_CAPACITY);
}

static Status collect_actions_and_actor(
    const Game *game, MusGameState *state, Actor *actor_out,
    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS],
    Probability probabilities[MUS_GAME_MAX_LEGAL_ACTIONS],
    size_t *action_count_out) {
    GameState *game_state = mus_game_state_as_game_state(state);
    Status status =
        cfr_game_current_actor(game, game_state, actor_out);

    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (actor_out->kind == CFR_ACTOR_CHANCE) {
        return cfr_game_chance_outcomes(
            game, game_state, actions, probabilities,
            MUS_GAME_MAX_LEGAL_ACTIONS, action_count_out);
    }
    return cfr_game_legal_actions(game, game_state, actions,
                                  MUS_GAME_MAX_LEGAL_ACTIONS,
                                  action_count_out);
}

static Status apply_branch(const Game *game, MusGameState *state,
                           Action action) {
    return cfr_game_apply_action(game, mus_game_state_as_game_state(state),
                                 action);
}

static Status undo_branch(const Game *game, MusGameState *state) {
    return cfr_game_undo_action(game, mus_game_state_as_game_state(state));
}

static Status profile_value_recursive(const Game *game, MusGameState *state,
                                      const InfoStore *store,
                                      Utility *value_out) {
    GameState *game_state = mus_game_state_as_game_state(state);
    bool terminal;
    Status status = cfr_game_is_terminal(game, game_state, &terminal);

    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (terminal)
        return cfr_game_terminal_utility(game, game_state, CFR_PLAYER_0,
                                         value_out);

    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS];
    Probability probabilities[MUS_GAME_MAX_LEGAL_ACTIONS] = {0};
    Actor actor;
    size_t action_count = 0;
    Utility total = 0.0;
    status = collect_actions_and_actor(game, state, &actor, actions,
                                       probabilities, &action_count);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (actor.kind == CFR_ACTOR_PLAYER) {
        InfoSetKey key;

        if (action_count > BR_ACTION_CAPACITY)
            return CFR_STATUS_INVALID_ARGUMENT;
        status = cfr_game_information_set_key(game, game_state, &key);
        if (status != CFR_STATUS_SUCCESS)
            return status;
        status = average_strategy(store, key, action_count, probabilities);
        if (status != CFR_STATUS_SUCCESS)
            return status;
    }
    for (size_t action = 0; action < action_count; action += 1) {
        Utility branch;
        Status branch_status;
        Status undo_status;

        status = apply_branch(game, state, actions[action]);
        if (status != CFR_STATUS_SUCCESS)
            return status;
        branch_status = profile_value_recursive(game, state, store, &branch);
        undo_status = undo_branch(game, state);
        if (undo_status != CFR_STATUS_SUCCESS)
            return undo_status;
        if (branch_status != CFR_STATUS_SUCCESS)
            return branch_status;
        total += probabilities[action] * branch;
        if (!isfinite(total))
            return CFR_STATUS_NUMERIC_ERROR;
    }
    *value_out = total;
    return CFR_STATUS_SUCCESS;
}

static Status best_continuation_value(
    const Game *game, MusGameState *state, const InfoStore *store,
    Player responder, size_t decision_depth, const BestResponseStore *best,
    Utility *value_out) {
    GameState *game_state = mus_game_state_as_game_state(state);
    bool terminal;
    Status status = cfr_game_is_terminal(game, game_state, &terminal);

    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (terminal)
        return cfr_game_terminal_utility(game, game_state, responder,
                                         value_out);

    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS];
    Probability probabilities[MUS_GAME_MAX_LEGAL_ACTIONS] = {0};
    Actor actor;
    size_t action_count = 0;
    status = collect_actions_and_actor(game, state, &actor, actions,
                                       probabilities, &action_count);
    if (status != CFR_STATUS_SUCCESS)
        return status;

    if (actor.kind == CFR_ACTOR_PLAYER && actor.player == responder) {
        InfoSetKey key;
        const BestResponseEntry *entry;
        Utility branch;
        Status branch_status;
        Status undo_status;

        status = cfr_game_information_set_key(game, game_state, &key);
        if (status != CFR_STATUS_SUCCESS)
            return status;
        status = best_store_find_ready(best, key, decision_depth,
                                       action_count, &entry);
        if (status != CFR_STATUS_SUCCESS)
            return status;
        status = apply_branch(game, state, actions[entry->best_action]);
        if (status != CFR_STATUS_SUCCESS)
            return status;
        branch_status = best_continuation_value(
            game, state, store, responder, decision_depth + 1, best, &branch);
        undo_status = undo_branch(game, state);
        if (undo_status != CFR_STATUS_SUCCESS)
            return undo_status;
        if (branch_status != CFR_STATUS_SUCCESS)
            return branch_status;
        *value_out = branch;
        return CFR_STATUS_SUCCESS;
    }

    if (actor.kind == CFR_ACTOR_PLAYER) {
        InfoSetKey key;

        if (action_count > BR_ACTION_CAPACITY)
            return CFR_STATUS_INVALID_ARGUMENT;
        status = cfr_game_information_set_key(game, game_state, &key);
        if (status != CFR_STATUS_SUCCESS)
            return status;
        status = average_strategy(store, key, action_count, probabilities);
        if (status != CFR_STATUS_SUCCESS)
            return status;
    }

    Utility total = 0.0;
    for (size_t action = 0; action < action_count; action += 1) {
        Utility branch;
        Status branch_status;
        Status undo_status;

        status = apply_branch(game, state, actions[action]);
        if (status != CFR_STATUS_SUCCESS)
            return status;
        branch_status = best_continuation_value(
            game, state, store, responder, decision_depth, best, &branch);
        undo_status = undo_branch(game, state);
        if (undo_status != CFR_STATUS_SUCCESS)
            return undo_status;
        if (branch_status != CFR_STATUS_SUCCESS)
            return branch_status;
        total += probabilities[action] * branch;
        if (!isfinite(total))
            return CFR_STATUS_NUMERIC_ERROR;
    }
    *value_out = total;
    return CFR_STATUS_SUCCESS;
}

static Status collect_counterfactual_values(
    const Game *game, MusGameState *state, const InfoStore *store,
    Player responder, size_t decision_depth, size_t target_depth,
    Probability counterfactual_reach, BestResponseStore *best) {
    GameState *game_state = mus_game_state_as_game_state(state);
    bool terminal;
    Status status = cfr_game_is_terminal(game, game_state, &terminal);

    if (status != CFR_STATUS_SUCCESS || terminal)
        return status;

    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS];
    Probability probabilities[MUS_GAME_MAX_LEGAL_ACTIONS] = {0};
    Actor actor;
    size_t action_count = 0;
    status = collect_actions_and_actor(game, state, &actor, actions,
                                       probabilities, &action_count);
    if (status != CFR_STATUS_SUCCESS)
        return status;

    if (actor.kind == CFR_ACTOR_PLAYER && actor.player == responder) {
        if (decision_depth > target_depth)
            return CFR_STATUS_INVALID_ARGUMENT;
        if (decision_depth == target_depth) {
            InfoSetKey key;
            BestResponseEntry *entry;

            status = cfr_game_information_set_key(game, game_state, &key);
            if (status != CFR_STATUS_SUCCESS)
                return status;
            status = best_store_get_or_create(best, key, decision_depth,
                                              action_count, &entry);
            if (status != CFR_STATUS_SUCCESS || entry->best_ready)
                return status == CFR_STATUS_SUCCESS
                           ? CFR_STATUS_INVALID_ARGUMENT
                           : status;
            for (size_t action = 0; action < action_count; action += 1) {
                Utility branch;
                Status branch_status;
                Status undo_status;

                status = apply_branch(game, state, actions[action]);
                if (status != CFR_STATUS_SUCCESS)
                    return status;
                branch_status = best_continuation_value(
                    game, state, store, responder, decision_depth + 1, best,
                    &branch);
                undo_status = undo_branch(game, state);
                if (undo_status != CFR_STATUS_SUCCESS)
                    return undo_status;
                if (branch_status != CFR_STATUS_SUCCESS)
                    return branch_status;
                entry->action_values[action] +=
                    counterfactual_reach * branch;
                if (!isfinite(entry->action_values[action]))
                    return CFR_STATUS_NUMERIC_ERROR;
            }
            return CFR_STATUS_SUCCESS;
        }
        for (size_t action = 0; action < action_count; action += 1) {
            Status branch_status;
            Status undo_status;

            status = apply_branch(game, state, actions[action]);
            if (status != CFR_STATUS_SUCCESS)
                return status;
            branch_status = collect_counterfactual_values(
                game, state, store, responder, decision_depth + 1,
                target_depth, counterfactual_reach, best);
            undo_status = undo_branch(game, state);
            if (undo_status != CFR_STATUS_SUCCESS)
                return undo_status;
            if (branch_status != CFR_STATUS_SUCCESS)
                return branch_status;
        }
        return CFR_STATUS_SUCCESS;
    }

    if (actor.kind == CFR_ACTOR_PLAYER) {
        InfoSetKey key;

        if (action_count > BR_ACTION_CAPACITY)
            return CFR_STATUS_INVALID_ARGUMENT;
        status = cfr_game_information_set_key(game, game_state, &key);
        if (status != CFR_STATUS_SUCCESS)
            return status;
        status = average_strategy(store, key, action_count, probabilities);
        if (status != CFR_STATUS_SUCCESS)
            return status;
    }
    for (size_t action = 0; action < action_count; action += 1) {
        const Probability branch_reach =
            counterfactual_reach * probabilities[action];
        Status branch_status;
        Status undo_status;

        if (!isfinite(branch_reach))
            return CFR_STATUS_NUMERIC_ERROR;
        status = apply_branch(game, state, actions[action]);
        if (status != CFR_STATUS_SUCCESS)
            return status;
        branch_status = collect_counterfactual_values(
            game, state, store, responder, decision_depth, target_depth,
            branch_reach, best);
        undo_status = undo_branch(game, state);
        if (undo_status != CFR_STATUS_SUCCESS)
            return undo_status;
        if (branch_status != CFR_STATUS_SUCCESS)
            return branch_status;
    }
    return CFR_STATUS_SUCCESS;
}

static Status exact_best_response(const MusGameState *root,
                                  const InfoStore *store, Player responder,
                                  Utility *value_out) {
    const Game *game = mus_game_descriptor();
    MusGameState state;
    BestResponseStore best = {0};
    Status status = best_store_init(&best);

    if (status != CFR_STATUS_SUCCESS)
        return status;
    for (size_t reverse_depth = BR_MAX_DECISION_DEPTH; reverse_depth > 0;
         reverse_depth -= 1) {
        const size_t depth = reverse_depth - 1;

        state = *root;
        status = collect_counterfactual_values(
            game, &state, store, responder, 0, depth, 1.0, &best);
        if (status == CFR_STATUS_SUCCESS)
            status = best_store_finish_depth(&best, depth);
        if (status != CFR_STATUS_SUCCESS)
            goto cleanup;
    }
    state = *root;
    status = best_continuation_value(game, &state, store, responder, 0,
                                     &best, value_out);

cleanup:
    best_store_destroy(&best);
    return status;
}

Status mus_evaluation_metrics_from_state(const MusGameState *root,
                                         const InfoStore *store,
                                         EvaluationMetrics *metrics_out) {
    const Game *game = mus_game_descriptor();
    MusGameState state;
    EvaluationMetrics metrics = {0};
    Status status;

    if (root == NULL || store == NULL || metrics_out == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    status = cfr_game_validate_state(
        game, mus_game_state_as_game_state_const(root));
    if (status != CFR_STATUS_SUCCESS)
        return status;
    state = *root;
    status = profile_value_recursive(game, &state, store,
                                     &metrics.profile_value_player_0);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    metrics.profile_value_player_1 = -metrics.profile_value_player_0;
    status = exact_best_response(root, store, CFR_PLAYER_0,
                                 &metrics.best_response_value_player_0);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    status = exact_best_response(root, store, CFR_PLAYER_1,
                                 &metrics.best_response_value_player_1);
    if (status != CFR_STATUS_SUCCESS)
        return status;

    metrics.improvement_player_0 = metrics.best_response_value_player_0 -
                                   metrics.profile_value_player_0;
    metrics.improvement_player_1 = metrics.best_response_value_player_1 -
                                   metrics.profile_value_player_1;
    if (metrics.improvement_player_0 < 0.0 &&
        metrics.improvement_player_0 > -EVALUATION_EPSILON) {
        metrics.improvement_player_0 = 0.0;
    }
    if (metrics.improvement_player_1 < 0.0 &&
        metrics.improvement_player_1 > -EVALUATION_EPSILON) {
        metrics.improvement_player_1 = 0.0;
    }
    if (metrics.improvement_player_0 < 0.0 ||
        metrics.improvement_player_1 < 0.0) {
        return CFR_STATUS_NUMERIC_ERROR;
    }
    metrics.nash_conv =
        metrics.improvement_player_0 + metrics.improvement_player_1;
    metrics.exploitability = metrics.nash_conv / 2.0;
    if (!isfinite(metrics.exploitability))
        return CFR_STATUS_NUMERIC_ERROR;
    *metrics_out = metrics;
    return CFR_STATUS_SUCCESS;
}

Status mus_evaluation_metrics(const InfoStore *store,
                              EvaluationMetrics *metrics_out) {
    MusGameState root;
    Status status = mus_game_state_init(&root);

    if (status != CFR_STATUS_SUCCESS)
        return status;
    return mus_evaluation_metrics_from_state(&root, store, metrics_out);
}
